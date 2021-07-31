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

template <HttpClient Http>
class CacheHttpImpl {
 public:
  template <typename... Args>
  CacheHttpImpl(const CacheHttpConfig& config, Args&&... args)
      : http_(std::forward<Args>(args)...),
        cache_(config.cache_size, Factory{&http_}),
        max_staleness_ms_(config.max_staleness_ms) {}

  CacheHttpImpl(CacheHttpImpl&&) = delete;
  CacheHttpImpl& operator=(CacheHttpImpl&&) = delete;

  Task<Response<>> Fetch(Request<> request, stdx::stop_token stop_token) const {
    bool should_invalidate_cache =
        (request.method != Method::kGet && request.method != Method::kHead &&
         request.method != Method::kOptions &&
         request.method != Method::kPropfind &&
         !(request.flags & Request<>::kRead)) ||
        (request.flags & Request<>::kWrite);
    auto at_exit = util::AtScopeExit([&] {
      if (should_invalidate_cache) {
        InvalidateCache();
      }
    });
    if (!IsCacheable(request)) {
      co_return ConvertResponse(
          co_await http_.Fetch(std::move(request), std::move(stop_token)));
    }
    auto r = co_await GetRequest(std::move(request));
    auto cached_response = cache_.GetCached(r);
    if (cached_response && !IsStale(*cached_response)) {
      co_return ConvertResponse(*cached_response);
    } else {
      cache_.Invalidate(r);
    }

    auto response = co_await cache_.Get(std::move(r), std::move(stop_token));
    co_return ConvertResponse(response);
  }

 private:
  void InvalidateCache() const { last_invalidate_ms_ = GetTime(); }

  struct CacheableResponse {
    int status;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    int64_t timestamp;
  };

  static Task<Request<std::string>> GetRequest(Request<> request) {
    Request<std::string> result{.url = std::move(request.url),
                                .method = request.method,
                                .headers = std::move(request.headers)};
    if (request.body) {
      result.body = co_await GetBody(std::move(*request.body));
    }
    co_return result;
  }

  template <GeneratorLike<std::string_view> T>
  static Generator<std::string> ConvertGenerator(T generator) {
    FOR_CO_AWAIT(auto& d, generator) { co_yield std::move(d); }
  }

  static Generator<std::string> ToGenerator(std::string string) {
    co_yield std::move(string);
  }

  static Response<> ConvertResponse(typename Http::ResponseType response) {
    return {.status = response.status,
            .headers = std::move(response.headers),
            .body = ConvertGenerator(std::move(response.body))};
  }

  static Response<> ConvertResponse(CacheableResponse response) {
    return {.status = response.status,
            .headers = std::move(response.headers),
            .body = ToGenerator(std::move(response.body))};
  }

  struct Factory {
    Task<CacheableResponse> operator()(Request<std::string> request,
                                       stdx::stop_token stop_token) const {
      auto response =
          co_await http->Fetch(std::move(request), std::move(stop_token));
      CacheableResponse result{.status = response.status,
                               .headers = std::move(response.headers),
                               .timestamp = GetTime()};
      result.body = co_await GetBody(std::move(response.body));
      co_return result;
    }
    Http* http;
  };

  static bool IsCacheable(const Request<>& request) {
    auto accept = GetHeader(request.headers, "Accept");
    if (!accept ||
        (accept != "application/json" && accept != "application/xml")) {
      return false;
    }
    auto content_type = GetHeader(request.headers, "Content-Type");
    return !content_type || content_type == "application/json" ||
           content_type == "application/xml" ||
           content_type == "application/x-www-form-urlencoded";
  }

  bool IsStale(const CacheableResponse& response) const {
    if (response.status >= 400) {
      return true;
    }
    if (response.timestamp <= last_invalidate_ms_) {
      return true;
    }
    return GetTime() - response.timestamp >= max_staleness_ms_;
  }

  static int64_t GetTime() {
    return std::chrono::system_clock::now().time_since_epoch() /
           std::chrono::milliseconds(1);
  }

  Http http_;
  mutable util::LRUCache<Request<std::string>, Factory> cache_;
  int max_staleness_ms_;
  mutable int64_t last_invalidate_ms_ = 0;
};

template <HttpClient Http>
using CacheHttp = ToHttpClient<CacheHttpImpl<Http>>;

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CACHE_HTTP_H_