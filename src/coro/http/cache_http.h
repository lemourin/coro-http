#ifndef CORO_HTTP_SRC_CORO_HTTP_CACHE_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CACHE_HTTP_H_

#include <chrono>

#include "coro/http/http.h"
#include "coro/http/http_parse.h"
#include "coro/util/lru_cache.h"

namespace coro::http {

struct CacheHttpConfig {
  int cache_size = 1024;
  int max_staleness_ms = 1000;
};

class CacheHttp {
 public:
  CacheHttp(const CacheHttpConfig& config, const Http* http)
      : http_(http),
        cache_(config.cache_size, Factory{http_}),
        max_staleness_ms_(config.max_staleness_ms) {}

  Task<Response<>> Fetch(Request<> request, stdx::stop_token stop_token) const;

 private:
  struct CacheableResponse {
    int status;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int64_t timestamp;
  };

  struct Factory {
    Task<CacheableResponse> operator()(Request<std::string> request,
                                       stdx::stop_token stop_token) const;
    const Http* http;
  };

  static Response<> ConvertResponse(CacheableResponse response);
  bool IsStale(const CacheableResponse& response) const;
  void InvalidateCache() const;

  const Http* http_;
  mutable util::LRUCache<Request<std::string>, Factory> cache_;
  int max_staleness_ms_;
  mutable int64_t last_invalidate_ms_ = 0;
};

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CACHE_HTTP_H_