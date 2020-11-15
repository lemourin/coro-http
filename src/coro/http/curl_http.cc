#include "curl_http.h"

#include <sstream>

namespace coro::http {

namespace {

struct SocketData {
  event socket_event = {};
};

void Check(CURLMcode code) {
  if (code != CURLM_OK) {
    throw HttpException(code, curl_multi_strerror(code));
  }
}

void Check(CURLcode code) {
  if (code != CURLE_OK) {
    throw HttpException(code, curl_easy_strerror(code));
  }
}

void Check(int code) {
  if (code != 0) {
    throw HttpException(code, "Unknown error.");
  }
}

std::string ToLowerCase(std::string str) {
  for (char& c : str) {
    c = static_cast<char>(std::tolower(c));
  }
  return str;
}

std::string TrimWhitespace(std::string_view str) {
  int it1 = 0;
  while (it1 < static_cast<int>(str.size()) && std::isspace(str[it1])) {
    it1++;
  }
  int it2 = static_cast<int>(str.size()) - 1;
  while (it2 > it1 && std::isspace(str[it2])) {
    it2--;
  }
  return std::string(str.begin() + it1, str.begin() + it2 + 1);
}

}  // namespace

CurlHandle::CurlHandle(CurlHandle&& handle) noexcept
    : http_(std::exchange(handle.http_, nullptr)),
      handle_(handle.handle_),
      header_list_(handle.header_list_),
      stop_token_(std::move(handle.stop_token_)),
      owner_(handle.owner_) {
  Check(curl_easy_setopt(handle_, CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFODATA, this));
}

size_t CurlHandle::HeaderCallback(char* buffer, size_t size, size_t nitems,
                                  void* userdata) {
  auto handle = reinterpret_cast<CurlHandle*>(userdata);
  if (!std::holds_alternative<CurlHttpOperation*>(handle->owner_)) {
    return 0;
  }
  auto http_operation = std::get<CurlHttpOperation*>(handle->owner_);
  std::string_view view(buffer, size * nitems);
  auto index = view.find_first_of(":");
  if (index != std::string::npos) {
    http_operation->headers_.insert(std::make_pair(
        ToLowerCase(std::string(view.begin(), view.begin() + index)),
        TrimWhitespace(std::string(view.begin() + index + 1, view.end()))));
  } else if (view.starts_with("HTTP")) {
    std::istringstream stream{std::string(view)};
    std::string http_version;
    int code;
    stream >> http_version >> code;
    http_operation->headers_.clear();
    http_operation->status_ = code;
  }
  return size * nitems;
}

size_t CurlHandle::WriteCallback(char* ptr, size_t size, size_t nmemb,
                                 void* userdata) {
  auto handle = reinterpret_cast<CurlHandle*>(userdata);
  if (std::holds_alternative<CurlHttpOperation*>(handle->owner_)) {
    auto http_operation = std::get<CurlHttpOperation*>(handle->owner_);
    if (!http_operation->headers_ready_event_posted_) {
      http_operation->headers_ready_event_posted_ = true;
      timeval tv = {};
      Check(event_add(&http_operation->headers_ready_, &tv));
    }
    http_operation->body_ += std::string(ptr, ptr + size * nmemb);
  } else if (std::holds_alternative<CurlHttpBodyGenerator*>(handle->owner_)) {
    auto http_body_generator = std::get<CurlHttpBodyGenerator*>(handle->owner_);
    timeval tv = {};
    http_body_generator->data_ += std::string(ptr, ptr + size * nmemb);
    Check(event_add(&http_body_generator->chunk_ready_, &tv));
  }
  return size * nmemb;
}

int CurlHandle::ProgressCallback(void* clientp, curl_off_t /*dltotal*/,
                                 curl_off_t /*dlnow*/, curl_off_t /*ultotal*/,
                                 curl_off_t /*ulnow*/) {
  auto handle = reinterpret_cast<CurlHandle*>(clientp);
  return handle->stop_token_.stop_requested() ? -1 : 0;
}

template <typename Owner>
CurlHandle::CurlHandle(CurlHttp* http, const Request& request,
                       stdx::stop_token&& stop_token, Owner* owner)
    : http_(http),
      handle_(curl_easy_init()),
      header_list_(),
      stop_token_(std::move(stop_token)),
      owner_(owner) {
  Check(curl_easy_setopt(handle_, CURLOPT_URL, request.url.data()));
  Check(curl_easy_setopt(handle_, CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, WriteCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERFUNCTION, HeaderCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFOFUNCTION, ProgressCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFODATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 0L));
  Check(curl_easy_setopt(handle_, CURLOPT_SSL_VERIFYPEER, 0L));
  for (const auto& [header_name, header_value] : request.headers) {
    std::string header_line = header_name;
    header_line += ": ";
    header_line += header_value;
    header_list_ = curl_slist_append(header_list_, header_line.c_str());
  }
  Check(curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, header_list_));
  Check(curl_multi_add_handle(http->curl_handle_, handle_));
}

template <typename NewOwner>
CurlHandle::CurlHandle(CurlHandle&& handle, NewOwner* owner)
    : CurlHandle(std::move(handle)) {
  owner_ = owner;
}

CurlHandle::~CurlHandle() {
  if (http_) {
    Check(curl_multi_remove_handle(http_->curl_handle_, handle_));
    curl_easy_cleanup(handle_);
    curl_slist_free_all(header_list_);
  }
}

CurlHttpBodyGenerator::CurlHttpBodyGenerator(CurlHandle&& handle,
                                             std::string&& initial_chunk)
    : handle_(std::move(handle), this) {
  Check(event_assign(&chunk_ready_, handle_.http_->event_loop_, -1, 0,
                     OnChunkReady, this));
  Check(event_assign(&body_ready_, handle_.http_->event_loop_, -1, 0,
                     OnBodyReady, this));
  ReceivedData(std::move(initial_chunk));
}

void CurlHttpBodyGenerator::OnChunkReady(evutil_socket_t, short, void* handle) {
  auto curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  std::string data = std::move(curl_http_body_generator->data_);
  curl_http_body_generator->data_.clear();
  curl_http_body_generator->ReceivedData(std::move(data));
}

void CurlHttpBodyGenerator::OnBodyReady(evutil_socket_t, short, void* handle) {
  auto curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  if (curl_http_body_generator->exception_ptr_) {
    curl_http_body_generator->Close(curl_http_body_generator->exception_ptr_);
  } else {
    curl_http_body_generator->Close(curl_http_body_generator->status_);
  }
}

CurlHttpBodyGenerator::~CurlHttpBodyGenerator() {
  Check(event_del(&chunk_ready_));
  Check(event_del(&body_ready_));
}

void CurlHttpBodyGenerator::Pause() {
  if (status_ == -1 && !exception_ptr_) {
    Check(curl_easy_pause(handle_.handle_, CURLPAUSE_RECV));
  }
}

void CurlHttpBodyGenerator::Resume() {
  if (status_ == -1 && !exception_ptr_) {
    Check(curl_easy_pause(handle_.handle_, CURLPAUSE_RECV_CONT));
  }
}

CurlHttpOperation::CurlHttpOperation(CurlHttp* http, Request&& request,
                                     stdx::stop_token&& stop_token)
    : request_(std::move(request)),
      handle_(http, request_, std::move(stop_token), this),
      headers_ready_(),
      headers_ready_event_posted_() {
  Check(event_assign(
      &headers_ready_, http->event_loop_, -1, 0,
      [](evutil_socket_t fd, short event, void* handle) {
        auto http_operation = reinterpret_cast<CurlHttpOperation*>(handle);
        http_operation->resume();
      },
      this));
}

CurlHttpOperation::~CurlHttpOperation() { Check(event_del(&headers_ready_)); }

void CurlHttpOperation::resume() { awaiting_coroutine_.resume(); }

bool CurlHttpOperation::await_ready() { return false; }

void CurlHttpOperation::await_suspend(
    stdx::coroutine_handle<void> awaiting_coroutine) {
  awaiting_coroutine_ = awaiting_coroutine;
}

Response<CurlHttpBodyGenerator> CurlHttpOperation::await_resume() {
  if (exception_ptr_) {
    std::rethrow_exception(exception_ptr_);
  }
  return {.status = status_,
          .headers = std::move(headers_),
          .body = CurlHttpBodyGenerator(std::move(handle_), std::move(body_))};
}

CurlHttp::CurlHttp(event_base* event_loop)
    : curl_handle_(curl_multi_init()),
      event_loop_(event_loop),
      timeout_event_() {
  event_assign(
      &timeout_event_, event_loop, -1, 0,
      [](evutil_socket_t fd, short event, void* handle) {
        int running_handles;
        Check(curl_multi_socket_action(handle, CURL_SOCKET_TIMEOUT, 0,
                                       &running_handles));
        ProcessEvents(handle);
      },
      curl_handle_);
  Check(
      curl_multi_setopt(curl_handle_, CURLMOPT_SOCKETFUNCTION, SocketCallback));
  Check(curl_multi_setopt(curl_handle_, CURLMOPT_TIMERFUNCTION, TimerCallback));
  Check(curl_multi_setopt(curl_handle_, CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle_, CURLMOPT_TIMERDATA, this));
}

CurlHttp::~CurlHttp() {
  Check(event_del(&timeout_event_));
  Check(curl_multi_cleanup(curl_handle_));
}

void CurlHttp::ProcessEvents(CURLM* multi_handle) {
  CURLMsg* message;
  do {
    int message_count;
    message = curl_multi_info_read(multi_handle, &message_count);
    if (message && message->msg == CURLMSG_DONE) {
      CURL* handle = message->easy_handle;
      CurlHandle* curl_handle;
      Check(curl_easy_getinfo(handle, CURLINFO_PRIVATE, &curl_handle));
      if (std::holds_alternative<CurlHttpOperation*>(curl_handle->owner_)) {
        auto operation = std::get<CurlHttpOperation*>(curl_handle->owner_);
        if (message->data.result == CURLE_OK) {
          long response_code;
          Check(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE,
                                  &response_code));
          operation->status_ = static_cast<int>(response_code);
        } else {
          operation->exception_ptr_ = std::make_exception_ptr(HttpException(
              message->data.result, curl_easy_strerror(message->data.result)));
        }
        operation->resume();
      } else if (std::holds_alternative<CurlHttpBodyGenerator*>(
                     curl_handle->owner_)) {
        auto curl_http_body_generator =
            std::get<CurlHttpBodyGenerator*>(curl_handle->owner_);
        curl_http_body_generator->status_ =
            static_cast<int>(message->data.result);
        if (message->data.result != CURLE_OK) {
          curl_http_body_generator->exception_ptr_ = std::make_exception_ptr(
              HttpException(message->data.result,
                            curl_easy_strerror(message->data.result)));
        }
        timeval tv = {};
        event_add(&curl_http_body_generator->body_ready_, &tv);
      }
    }
  } while (message != nullptr);
}

void CurlHttp::SocketEvent(evutil_socket_t fd, short event, void* handle) {
  int running_handles;
  Check(
      curl_multi_socket_action(handle, fd,
                               ((event & EV_READ) ? CURL_CSELECT_IN : 0) |
                                   ((event & EV_WRITE) ? CURL_CSELECT_OUT : 0),
                               &running_handles));
  ProcessEvents(handle);
}

int CurlHttp::SocketCallback(CURL*, curl_socket_t socket, int what, void* userp,
                             void* socketp) {
  auto http = reinterpret_cast<CurlHttp*>(userp);
  if (what == CURL_POLL_REMOVE) {
    auto data = reinterpret_cast<SocketData*>(socketp);
    if (data) {
      Check(event_del(&data->socket_event));
      delete data;
    }
  } else {
    auto data = reinterpret_cast<SocketData*>(socketp);
    if (!data) {
      data = new SocketData;
      Check(curl_multi_assign(http->curl_handle_, socket, data));
    } else {
      Check(event_del(&data->socket_event));
    }
    Check(event_assign(&data->socket_event, http->event_loop_, socket,
                       ((what & CURL_POLL_IN) ? EV_READ : 0) |
                           ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST,
                       SocketEvent, http->curl_handle_));
    Check(event_add(&data->socket_event, nullptr));
  }
  return 0;
}

int CurlHttp::TimerCallback(CURLM*, long timeout_ms, void* userp) {
  auto http = reinterpret_cast<CurlHttp*>(userp);
  if (timeout_ms == -1) {
    Check(event_del(&http->timeout_event_));
  } else {
    timeval tv = {.tv_sec = timeout_ms / 1000,
                  .tv_usec = timeout_ms % 1000 * 1000};
    Check(event_add(&http->timeout_event_, &tv));
  }
  return 0;
}

CurlHttpOperation CurlHttp::Fetch(Request request, stdx::stop_token token) {
  return CurlHttpOperation{this, std::move(request), std::move(token)};
}

CurlHttpOperation CurlHttp::Fetch(std::string url, stdx::stop_token token) {
  return Fetch(Request{.url = std::move(url)}, std::move(token));
}

}  // namespace coro::http