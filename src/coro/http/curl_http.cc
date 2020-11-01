#include "curl_http.h"

#include <iostream>
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
  while (it1 < str.size() && std::isspace(str[it1])) {
    it1++;
  }
  int it2 = static_cast<int>(str.size()) - 1;
  while (it2 > it1 && std::isspace(str[it2])) {
    it2--;
  }
  return std::string(str.begin() + it1, str.begin() + it2 + 1);
}

}  // namespace

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
          operation->response_.status = static_cast<int>(response_code);
        } else {
          operation->exception_ptr_ = std::make_exception_ptr(HttpException(
              message->data.result, curl_easy_strerror(message->data.result)));
        }
        operation->resume();
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
    timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000};
    Check(event_add(&http->timeout_event_, &tv));
  }
  return 0;
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
    http_operation->response_.headers.insert(std::make_pair(
        ToLowerCase(std::string(view.begin(), view.begin() + index)),
        TrimWhitespace(std::string(view.begin() + index + 1, view.end()))));
  } else if (view.starts_with("HTTP")) {
    std::istringstream stream{std::string(view)};
    std::string http_version;
    int code;
    stream >> http_version >> code;
    http_operation->response_.headers.clear();
    http_operation->response_.status = code;
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
    http_operation->response_.body += std::string(ptr, ptr + size * nmemb);
  }
  return size * nmemb;
}

CurlHandle::CurlHandle(CurlHttp* http, CurlHttpOperation* http_operation,
                       const Request& request)
    : http_(http),
      handle_(curl_easy_init()),
      header_list_(),
      owner_(http_operation) {
  Check(curl_easy_setopt(handle_, CURLOPT_URL, request.url.data()));
  Check(curl_easy_setopt(handle_, CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, WriteCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERFUNCTION, HeaderCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this));
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

CurlHandle::~CurlHandle() {
  if (http_) {
    Check(curl_multi_remove_handle(http_->curl_handle_, handle_));
    curl_easy_cleanup(handle_);
    curl_slist_free_all(header_list_);
  }
}

CurlHttpOperation::CurlHttpOperation(CurlHttp* http, Request&& request)
    : request_(std::move(request)),
      handle_(http, this, request_),
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

CurlHttpOperation::~CurlHttpOperation() { event_del(&headers_ready_); }

void CurlHttpOperation::resume() { awaiting_coroutine_.resume(); }

bool CurlHttpOperation::await_ready() { return false; }

void CurlHttpOperation::await_suspend(
    coroutine_handle<void> awaiting_coroutine) {
  awaiting_coroutine_ = awaiting_coroutine;
}

Response CurlHttpOperation::await_resume() {
  if (exception_ptr_) {
    std::rethrow_exception(exception_ptr_);
  }
  return std::move(response_);
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

HttpOperation CurlHttp::Fetch(Request&& request) {
  return HttpOperation(
      std::make_unique<CurlHttpOperation>(this, std::move(request)));
}

}  // namespace coro::http