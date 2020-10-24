#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include <curl/curl.h>
#include <event2/event.h>

#include "http.h"

namespace coro::http {

class CurlHttp;

class CurlHttpOperation : public HttpOperationImpl {
 public:
  CurlHttpOperation(CurlHttp* http, std::string_view url);
  ~CurlHttpOperation() override;

  void set_response(Response&& response);

 private:
  void await_suspend(
      std::experimental::coroutine_handle<> awaiting_coroutine) override;

  Response await_resume() override;

  std::experimental::coroutine_handle<> awaiting_coroutine_;
  Response response_;
  CurlHttp* http_;
  CURL* handle_;
};

class CurlHttp : public Http {
 public:
  explicit CurlHttp(event_base* event_loop);
  ~CurlHttp() override;

  HttpOperation Fetch(std::string_view url) override;

 private:
  friend class CurlHttpOperation;

  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);

  CURLM* curl_handle_;
  event_base* event_loop_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
