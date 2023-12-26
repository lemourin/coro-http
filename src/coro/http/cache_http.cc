#include "coro/http/cache_http.h"

namespace coro::http {

namespace {

Task<Request<std::string>> GetRequest(Request<> request) {
  Request<std::string> result{.url = std::move(request.url),
                              .method = request.method,
                              .headers = std::move(request.headers)};
  if (request.body) {
    result.body = co_await GetBody(std::move(*request.body));
  }
  co_return result;
}

bool IsCacheable(const Request<>& request) {
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

Generator<std::string> ToGenerator(std::string string) {
  co_yield std::move(string);
}

int64_t GetTime() {
  return std::chrono::system_clock::now().time_since_epoch() /
         std::chrono::milliseconds(1);
}

}  // namespace

Task<Response<>> CacheHttp::Fetch(Request<> request,
                                  stdx::stop_token stop_token) const {
  bool should_invalidate_cache = request.invalidates_cache;
  if (!IsCacheable(request)) {
    auto response =
        co_await http_->Fetch(std::move(request), std::move(stop_token));
    if (should_invalidate_cache && response.status / 100 == 2) {
      InvalidateCache();
    }
    co_return response;
  }
  auto r = co_await GetRequest(std::move(request));
  auto cached_response = cache_.GetCached(r);
  if (cached_response && !IsStale(*cached_response)) {
    co_return ConvertResponse(*cached_response);
  } else {
    cache_.Invalidate(r);
  }

  auto response = co_await cache_.Get(std::move(r), std::move(stop_token));
  if (should_invalidate_cache && response.status / 100 == 2) {
    InvalidateCache();
  }
  co_return ConvertResponse(response);
}

Response<> CacheHttp::ConvertResponse(CacheableResponse response) {
  return {.status = response.status,
          .headers = std::move(response.headers),
          .body = ToGenerator(std::move(response.body))};
}

bool CacheHttp::IsStale(const CacheableResponse& response) const {
  if (response.status >= 400) {
    return true;
  }
  if (response.timestamp <= last_invalidate_ms_) {
    return true;
  }
  return GetTime() - response.timestamp >= max_staleness_ms_;
}

void CacheHttp::InvalidateCache() const { last_invalidate_ms_ = GetTime(); }

auto CacheHttp::Factory::operator()(Request<std::string> request,
                                    stdx::stop_token stop_token) const
    -> Task<CacheableResponse> {
  auto response =
      co_await http->Fetch(std::move(request), std::move(stop_token));
  CacheableResponse result{.status = response.status,
                           .headers = std::move(response.headers),
                           .timestamp = GetTime()};
  result.body = co_await GetBody(std::move(response.body));
  co_return result;
}

}  // namespace coro::http