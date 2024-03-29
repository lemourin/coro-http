#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "coro/generator.h"
#include "coro/http/http_exception.h"
#include "coro/http/http_parse.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/coroutine.h"
#include "coro/stdx/stop_token.h"

namespace coro::http {

enum class Method {
  kGet,
  kPost,
  kPut,
  kOptions,
  kHead,
  kPatch,
  kDelete,
  kPropfind,
  kProppatch,
  kMkcol,
  kMove,
  kCopy
};

std::string_view MethodToString(Method method);

Task<std::string> GetBody(Generator<std::string> body);

Generator<std::string> CreateBody(std::string body);

template <typename BodyGenerator = Generator<std::string>>
struct Request {
  std::string url;
  Method method = Method::kGet;
  std::vector<std::pair<std::string, std::string>> headers;
  std::optional<BodyGenerator> body;
  bool invalidates_cache = [this] {
    switch (method) {
      case Method::kPost:
      case Method::kPut:
      case Method::kPatch:
      case Method::kDelete:
      case Method::kProppatch:
      case Method::kMkcol:
      case Method::kMove:
      case Method::kCopy:
        return true;
      default:
        return false;
    }
  }();

  friend bool operator==(const Request& r1, const Request& r2) {
    return std::tie(r1.url, r1.method, r1.headers, r1.body) ==
           std::tie(r2.url, r2.method, r2.headers, r2.body);
  }
};

template <GeneratorLike<std::string_view> HttpBodyGenerator =
              Generator<std::string>>
struct Response {
  int status = -1;
  std::vector<std::pair<std::string, std::string>> headers;
  HttpBodyGenerator body;
};

class Http {
 public:
  template <typename HttpClientT>
  explicit Http(HttpClientT client)
      : impl_([d = std::move(client)](Request<> request,
                                      stdx::stop_token stop_token) {
          return d.Fetch(std::move(request), std::move(stop_token));
        }) {}

  auto Fetch(Request<> request,
             stdx::stop_token stop_token = stdx::stop_token()) const {
    return impl_(std::move(request), std::move(stop_token));
  }

  auto Fetch(Request<std::string> request,
             stdx::stop_token stop_token = stdx::stop_token()) const {
    auto headers = std::move(request.headers);
    if (request.body && !GetHeader(headers, "Content-Length")) {
      headers.emplace_back("Content-Length",
                           std::to_string(request.body->length()));
    }
    return Fetch(Request<>{.url = std::move(request.url),
                           .method = request.method,
                           .headers = std::move(headers),
                           .body = request.body ? std::make_optional(CreateBody(
                                                      std::move(*request.body)))
                                                : std::nullopt,
                           .invalidates_cache = request.invalidates_cache},
                 std::move(stop_token));
  }

  auto Fetch(std::string url,
             stdx::stop_token stop_token = stdx::stop_token()) const {
    return Fetch(Request<>{.url = std::move(url)}, std::move(stop_token));
  }

  template <typename RequestT>
  Task<Response<>> FetchOk(RequestT request, stdx::stop_token stop_token =
                                                 stdx::stop_token()) const {
    auto response = co_await Fetch(std::move(request), std::move(stop_token));
    if (response.status / 100 != 2) {
      auto message = co_await GetBody(std::move(response.body));
      throw coro::http::HttpException(response.status, std::move(message));
    }
    co_return response;
  }

 private:
  stdx::any_invocable<Task<Response<>>(Request<>, stdx::stop_token) const>
      impl_;
};

}  // namespace coro::http

namespace std {
template <>
struct hash<coro::http::Request<std::string>> {
  size_t operator()(const coro::http::Request<std::string>& r) const;
};
}  // namespace std

#endif  // CORO_HTTP_HTTP_H
