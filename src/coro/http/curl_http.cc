#include "curl_http.h"

#include <coro/interrupted_exception.h>

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
      event_loop_(handle.event_loop_),
      handle_(std::move(handle.handle_)),
      header_list_(std::move(handle.header_list_)),
      stop_token_(std::move(handle.stop_token_)),
      owner_(handle.owner_),
      stop_callback_(stop_token_, OnCancel{this}) {
  Check(curl_easy_setopt(handle_.get(), CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_XFERINFODATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_READDATA, this));

  Check(event_del(&handle.next_request_body_chunk_));
  Check(event_assign(&next_request_body_chunk_, event_loop_, -1, 0,
                     OnNextRequestBodyChunkRequested, this));
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
    http_operation->headers_.emplace_back(
        ToLowerCase(std::string(view.begin(), view.begin() + index)),
        TrimWhitespace(std::string(view.begin() + index + 1, view.end())));
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
    if (!http_body_generator->data_.empty()) {
      return CURL_WRITEFUNC_PAUSE;
    }
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

size_t CurlHandle::ReadCallback(char* buffer, size_t size, size_t nitems,
                                void* userdata) {
  auto handle = reinterpret_cast<CurlHandle*>(userdata);
  if (!handle->request_body_it_) {
    return CURL_READFUNC_PAUSE;
  }
  if (handle->request_body_it_ == std::end(*handle->request_body_)) {
    return 0;
  }
  for (char c : **handle->request_body_it_) {
    handle->buffer_.push_back(c);
  }
  auto it = *std::exchange(handle->request_body_it_, std::nullopt);
  size_t sent_cnt = 0;
  for (size_t i = 0; i < size * nitems && !handle->buffer_.empty(); i++) {
    buffer[i] = handle->buffer_.front();
    handle->buffer_.pop_front();
    sent_cnt++;
  }
  if (handle->buffer_.empty()) {
    timeval tv{};
    Check(event_add(&handle->next_request_body_chunk_, &tv));
  }
  return sent_cnt;
}

void CurlHandle::OnNextRequestBodyChunkRequested(evutil_socket_t, short,
                                                 void* userdata) {
  Invoke([handle = reinterpret_cast<CurlHandle*>(userdata)]() -> Task<> {
    handle->request_body_it_ = co_await ++*handle->request_body_it_;
    curl_easy_pause(handle->handle_.get(), CURLPAUSE_SEND_CONT);
  });
}

void CurlHandle::OnCancel::operator()() const {
  if (std::holds_alternative<CurlHttpOperation*>(handle->owner_)) {
    auto operation = std::get<CurlHttpOperation*>(handle->owner_);
    operation->exception_ptr_ = std::make_exception_ptr(InterruptedException());
    if (operation->awaiting_coroutine_) {
      std::exchange(operation->awaiting_coroutine_, nullptr).resume();
    }
  } else if (std::holds_alternative<CurlHttpBodyGenerator*>(handle->owner_)) {
    auto generator = std::get<CurlHttpBodyGenerator*>(handle->owner_);
    generator->exception_ptr_ = std::make_exception_ptr(InterruptedException());
    generator->Close(generator->exception_ptr_);
  }
}

template <typename Owner>
CurlHandle::CurlHandle(CURLM* http, event_base* event_loop, Request<> request,
                       stdx::stop_token stop_token, Owner* owner)
    : http_(http),
      event_loop_(event_loop),
      handle_(curl_easy_init()),
      header_list_(),
      request_body_(std::move(request.body)),
      stop_token_(std::move(stop_token)),
      owner_(owner),
      stop_callback_(stop_token_, OnCancel{this}) {
  Check(curl_easy_setopt(handle_.get(), CURLOPT_URL, request.url.data()));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, WriteCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, this));
  Check(
      curl_easy_setopt(handle_.get(), CURLOPT_HEADERFUNCTION, HeaderCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_XFERINFOFUNCTION,
                         ProgressCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_XFERINFODATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_READFUNCTION, ReadCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_READDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_NOPROGRESS, 0L));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_SSL_VERIFYPEER, 0L));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_CUSTOMREQUEST,
                         MethodToString(request.method)));
  std::optional<long> content_length;
  curl_slist* header_list = nullptr;
  for (const auto& [header_name, header_value] : request.headers) {
    std::string header_line = header_name;
    header_line += ": ";
    header_line += header_value;
    header_list = curl_slist_append(header_list, header_line.c_str());
    if (ToLowerCase(header_name) == "content-length") {
      content_length = std::stol(header_value);
    }
  }
  header_list_ = std::unique_ptr<curl_slist, CurlListDeleter>(header_list);
  Check(curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, header_list));

  if (request_body_) {
    if (request.method == Method::kPost) {
      Check(curl_easy_setopt(handle_.get(), CURLOPT_POST, 1L));
      if (content_length) {
        Check(curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDSIZE,
                               *content_length));
      }
    } else {
      curl_easy_setopt(handle_.get(), CURLOPT_UPLOAD, 1L);
    }
    Invoke([this]() -> Task<> {
      request_body_it_ = co_await request_body_->begin();
      curl_easy_pause(handle_.get(), CURLPAUSE_SEND_CONT);
    });
  }

  Check(event_assign(&next_request_body_chunk_, event_loop, -1, 0,
                     OnNextRequestBodyChunkRequested, this));

  Check(curl_multi_add_handle(http, handle_.get()));
}

template <typename NewOwner>
CurlHandle::CurlHandle(CurlHandle&& handle, NewOwner* owner)
    : CurlHandle(std::move(handle)) {
  owner_ = owner;
}

CurlHandle::~CurlHandle() {
  if (http_) {
    Check(curl_multi_remove_handle(http_, handle_.get()));
    event_del(&next_request_body_chunk_);
  }
}

CurlHttpBodyGenerator::CurlHttpBodyGenerator(CurlHandle handle,
                                             std::string initial_chunk)
    : handle_(std::move(handle), this) {
  Check(event_assign(&chunk_ready_, handle_.event_loop_, -1, 0, OnChunkReady,
                     this));
  Check(event_assign(&body_ready_, handle_.event_loop_, -1, 0, OnBodyReady,
                     this));
  ReceivedData(std::move(initial_chunk));
}

void CurlHttpBodyGenerator::OnChunkReady(evutil_socket_t, short, void* handle) {
  auto curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  std::string data = std::move(curl_http_body_generator->data_);
  curl_http_body_generator->data_.clear();
  if (curl_http_body_generator->status_ != -1 &&
      !curl_http_body_generator->body_ready_fired_) {
    timeval tv = {};
    curl_http_body_generator->body_ready_fired_ = true;
    Check(event_add(&curl_http_body_generator->body_ready_, &tv));
  }
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

void CurlHttpBodyGenerator::Resume() {
  if (status_ == -1 && !exception_ptr_) {
    curl_easy_pause(handle_.handle_.get(), CURLPAUSE_RECV_CONT);
  }
}

CurlHttpOperation::CurlHttpOperation(CURLM* http, event_base* event_loop,
                                     Request<> request,
                                     stdx::stop_token stop_token)
    : handle_(http, event_loop, std::move(request), std::move(stop_token),
              this),
      headers_ready_(),
      headers_ready_event_posted_() {
  Check(event_assign(
      &headers_ready_, event_loop, -1, 0,
      [](evutil_socket_t fd, short event, void* handle) {
        auto http_operation = reinterpret_cast<CurlHttpOperation*>(handle);
        if (http_operation->awaiting_coroutine_) {
          std::exchange(http_operation->awaiting_coroutine_, nullptr).resume();
        }
      },
      this));
}

CurlHttpOperation::~CurlHttpOperation() { Check(event_del(&headers_ready_)); }

bool CurlHttpOperation::await_ready() {
  return exception_ptr_ || status_ != -1;
}

void CurlHttpOperation::await_suspend(
    stdx::coroutine_handle<void> awaiting_coroutine) {
  awaiting_coroutine_ = awaiting_coroutine;
}

Response<util::WrapGenerator<CurlHttpBodyGenerator>>
CurlHttpOperation::await_resume() {
  if (exception_ptr_) {
    std::rethrow_exception(exception_ptr_);
  }
  auto body_generator = std::make_unique<CurlHttpBodyGenerator>(
      std::move(handle_), std::move(body_));
  if (no_body_) {
    body_generator->Close(status_);
  }
  return {.status = status_,
          .headers = std::move(headers_),
          .body = util::WrapGenerator(std::move(body_generator))};
}

CurlHttpImpl::Data::Data(event_base* event_loop)
    : curl_handle(curl_multi_init()), event_loop(event_loop), timeout_event() {
  event_assign(
      &timeout_event, event_loop, -1, 0,
      [](evutil_socket_t fd, short event, void* handle) {
        int running_handles;
        Check(curl_multi_socket_action(handle, CURL_SOCKET_TIMEOUT, 0,
                                       &running_handles));
        ProcessEvents(handle);
      },
      curl_handle.get());
  Check(curl_multi_setopt(curl_handle.get(), CURLMOPT_SOCKETFUNCTION,
                          SocketCallback));
  Check(curl_multi_setopt(curl_handle.get(), CURLMOPT_TIMERFUNCTION,
                          TimerCallback));
  Check(curl_multi_setopt(curl_handle.get(), CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle.get(), CURLMOPT_TIMERDATA, this));
}

CurlHttpImpl::Data::~Data() { Check(event_del(&timeout_event)); }

CurlHttpImpl::CurlHttpImpl(event_base* event_loop)
    : d_(std::make_unique<Data>(event_loop)) {}

void CurlHttpImpl::ProcessEvents(CURLM* multi_handle) {
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
        operation->no_body_ = true;
        Check(event_base_once(
            operation->handle_.event_loop_, -1, EV_TIMEOUT,
            [](evutil_socket_t fd, short event, void* handle) {
              stdx::coroutine_handle<void>::from_address(handle).resume();
            },
            std::exchange(operation->awaiting_coroutine_, nullptr).address(),
            nullptr));
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
        if (!event_pending(&curl_http_body_generator->chunk_ready_, EV_TIMEOUT,
                           nullptr)) {
          timeval tv = {};
          curl_http_body_generator->body_ready_fired_ = true;
          event_add(&curl_http_body_generator->body_ready_, &tv);
        }
      }
    }
  } while (message != nullptr);
}

void CurlHttpImpl::SocketEvent(evutil_socket_t fd, short event, void* handle) {
  int running_handles;
  Check(
      curl_multi_socket_action(handle, fd,
                               ((event & EV_READ) ? CURL_CSELECT_IN : 0) |
                                   ((event & EV_WRITE) ? CURL_CSELECT_OUT : 0),
                               &running_handles));
  ProcessEvents(handle);
}

int CurlHttpImpl::SocketCallback(CURL*, curl_socket_t socket, int what,
                                 void* userp, void* socketp) {
  auto http = reinterpret_cast<Data*>(userp);
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
      Check(curl_multi_assign(http->curl_handle.get(), socket, data));
    } else {
      Check(event_del(&data->socket_event));
    }
    Check(event_assign(&data->socket_event, http->event_loop, socket,
                       ((what & CURL_POLL_IN) ? EV_READ : 0) |
                           ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST,
                       SocketEvent, http->curl_handle.get()));
    Check(event_add(&data->socket_event, nullptr));
  }
  return 0;
}

int CurlHttpImpl::TimerCallback(CURLM*, long timeout_ms, void* userp) {
  auto http = reinterpret_cast<Data*>(userp);
  if (timeout_ms == -1) {
    Check(event_del(&http->timeout_event));
  } else {
    timeval tv = {.tv_sec = timeout_ms / 1000,
                  .tv_usec = timeout_ms % 1000 * 1000};
    Check(event_add(&http->timeout_event, &tv));
  }
  return 0;
}

util::WrapAwaitable<CurlHttpOperation> CurlHttpImpl::Fetch(
    Request<> request, stdx::stop_token token) const {
  return util::WrapAwaitable(std::make_unique<CurlHttpOperation>(
      d_->curl_handle.get(), d_->event_loop, std::move(request),
      std::move(token)));
}

}  // namespace coro::http
