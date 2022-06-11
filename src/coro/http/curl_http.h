#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include <variant>

#include "coro/http/http.h"
#include "coro/stdx/stop_callback.h"

struct event_base;

namespace coro::http {

class CurlHttpBase {
 public:
  CurlHttpBase(event_base* event_loop, std::optional<std::string> cache_path);
  ~CurlHttpBase();

  Task<Response<>> Fetch(Request<> request, stdx::stop_token stop_token) const;

 private:
  class Impl;

  std::unique_ptr<Impl> d_;
};

using CurlHttp = ToHttpClient<CurlHttpBase>;

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
