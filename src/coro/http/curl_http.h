#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include "coro/http/http.h"
#include "coro/util/event_loop.h"

namespace coro::http {

std::string GetNativeCaCertBlob();

struct CurlHttpConfig {
  std::optional<std::string> alt_svc_path;
  std::optional<std::string> ca_cert_blob = GetNativeCaCertBlob();
};

class CurlHttp {
 public:
  explicit CurlHttp(const coro::util::EventLoop* event_loop, CurlHttpConfig = {});
  CurlHttp(const CurlHttp&) = delete;
  CurlHttp(CurlHttp&&) noexcept;
  CurlHttp& operator=(const CurlHttp&) = delete;
  CurlHttp& operator=(CurlHttp&&) noexcept;
  ~CurlHttp();

  Task<Response<>> Fetch(Request<> request, stdx::stop_token stop_token) const;

 private:
  struct Impl;

  std::unique_ptr<Impl> d_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
