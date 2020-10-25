#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include <curl/curl.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include "http.h"

namespace coro::http {

class CurlHttp;

class CurlHttpOperation : public HttpOperationImpl {
 public:
  CurlHttpOperation(CurlHttp* http, Request&&);
  ~CurlHttpOperation() override;

  void resume();

 private:
  friend class CurlHttp;

  bool await_ready() override;
  void await_suspend(coroutine_handle<void> awaiting_coroutine) override;
  Response await_resume() override;

  static size_t WriteCallback(char* ptr, size_t size, size_t nmemb,
                              void* userdata);
  static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                               void* userdata);

  Request request_;
  Response response_;
  coroutine_handle<void> awaiting_coroutine_;
  CurlHttp* http_;
  CURL* handle_;
  curl_slist* header_list_;
  std::exception_ptr exception_ptr_;
  event headers_ready_;
  bool headers_ready_event_posted_;
};

class CurlHttp : public Http {
 public:
  explicit CurlHttp(event_base* event_loop);
  ~CurlHttp() override;

  HttpOperation Fetch(Request&& request) override;

 private:
  friend class CurlHttpOperation;

  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);
  static void SocketEvent(evutil_socket_t fd, short event, void* multi_handle);
  static void ProcessEvents(CURLM* handle);

  CURLM* curl_handle_;
  event_base* event_loop_;
  event timeout_event_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
