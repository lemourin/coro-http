#include "curl_http.h"

#include <event2/event_struct.h>

#include <iostream>

namespace coro::http {

int CurlHttp::SocketCallback(CURL*, curl_socket_t socket, int what, void* userp,
                             void* socketp) {
  auto http = reinterpret_cast<CurlHttp*>(userp);
  if (what == CURL_POLL_REMOVE) {
    std::cerr << "removing socket\n";
  } else {
    event* event = event_new(
        http->event_loop_, socket,
        ((what & CURL_POLL_IN) ? EV_READ : 0) |
            ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST,
        [](evutil_socket_t fd, short event, void* handle) {
          int running_handles;
          curl_multi_socket_action(
              handle, fd,
              ((event & EV_READ) ? CURL_CSELECT_IN : 0) |
                  ((event & EV_WRITE) ? CURL_CSELECT_OUT : 0),
              &running_handles);

          CURLMsg* message;
          do {
            int message_count;
            message = curl_multi_info_read(handle, &message_count);
            if (message && message->msg == CURLMSG_DONE) {
              CURL* handle = message->easy_handle;
              CurlHttpOperation* operation;
              curl_easy_getinfo(handle, CURLINFO_PRIVATE, &operation);
              Response response;
              long response_code;
              curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &response_code);
              response.status = response_code;
              operation->set_response(std::move(response));
            }
          } while (message != nullptr);
        },
        http->curl_handle_);

    event_add(event, nullptr);
  }
  return 0;
}

int CurlHttp::TimerCallback(CURLM* handle, long timeout_ms, void* userp) {
  auto http = reinterpret_cast<CurlHttp*>(userp);
  event* timeout = event_new(
      http->event_loop_, -1, 0,
      [](evutil_socket_t fd, short event, void* handle) {
        int running_handles;
        curl_multi_socket_action(handle, CURL_SOCKET_TIMEOUT, 0,
                                 &running_handles);
      },
      handle);

  timeval tv = {.tv_sec = timeout_ms / 1000, .tv_usec = timeout_ms % 1000};
  event_add(timeout, &tv);
  return 0;
}

CurlHttpOperation::CurlHttpOperation(CurlHttp* http, std::string_view url)
    : http_(http), handle_(curl_easy_init()) {
  curl_easy_setopt(handle_, CURLOPT_URL, url.data());
  curl_easy_setopt(handle_, CURLOPT_PRIVATE, this);
}

CurlHttpOperation::~CurlHttpOperation() {
  curl_multi_remove_handle(http_->curl_handle_, handle_);
  curl_easy_cleanup(handle_);
}

void CurlHttpOperation::set_response(Response&& response) {
  response_ = std::move(response);
  awaiting_coroutine_.resume();
}

void CurlHttpOperation::await_suspend(
    coroutine_handle<void> awaiting_coroutine) {
  awaiting_coroutine_ = awaiting_coroutine;
  curl_multi_add_handle(http_->curl_handle_, handle_);
}

Response CurlHttpOperation::await_resume() { return std::move(response_); }

CurlHttp::CurlHttp(event_base* event_loop) : event_loop_(event_loop) {
  curl_handle_ = curl_multi_init();
  curl_multi_setopt(curl_handle_, CURLMOPT_SOCKETFUNCTION, SocketCallback);
  curl_multi_setopt(curl_handle_, CURLMOPT_TIMERFUNCTION, TimerCallback);
  curl_multi_setopt(curl_handle_, CURLMOPT_SOCKETDATA, this);
  curl_multi_setopt(curl_handle_, CURLMOPT_TIMERDATA, this);
}

CurlHttp::~CurlHttp() { curl_multi_cleanup(curl_handle_); }

HttpOperation CurlHttp::Fetch(std::string_view url) {
  return HttpOperation(std::make_unique<CurlHttpOperation>(this, url));
}

}  // namespace coro::http