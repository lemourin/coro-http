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
  enum Flag { kRead = 1 << 0, kWrite = 1 << 1 } flags;

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
  virtual ~Http() = default;

  virtual Task<Response<>> Fetch(Request<>, stdx::stop_token) const = 0;
  virtual Task<Response<>> Fetch(Request<std::string>,
                                 stdx::stop_token) const = 0;
  virtual Task<Response<>> Fetch(std::string url, stdx::stop_token) const = 0;

  virtual Task<Response<>> FetchOk(Request<> url, stdx::stop_token) const = 0;
  virtual Task<Response<>> FetchOk(Request<std::string> url,
                                   stdx::stop_token) const = 0;
};

template <typename Impl>
class HttpImpl : public Http {
 public:
  template <typename... Args>
  explicit HttpImpl(Args&&... args) : impl_(std::forward<Args>(args)...) {}

  Task<Response<>> Fetch(Request<> request,
                         stdx::stop_token stop_token) const override {
    return impl_.Fetch(std::move(request), std::move(stop_token));
  }

  Task<Response<>> Fetch(Request<std::string> request,
                         stdx::stop_token stop_token) const override {
    return impl_.Fetch(std::move(request), std::move(stop_token));
  }

  Task<Response<>> Fetch(std::string url,
                         stdx::stop_token stop_token) const override {
    return impl_.Fetch(std::move(url), std::move(stop_token));
  }

  Task<Response<>> FetchOk(Request<> request,
                           stdx::stop_token stop_token) const override {
    return impl_.FetchOk(std::move(request), std::move(stop_token));
  }

  Task<Response<>> FetchOk(Request<std::string> request,
                           stdx::stop_token stop_token) const override {
    return impl_.FetchOk(std::move(request), std::move(stop_token));
  }

 private:
  Impl impl_;
};

template <typename Impl>
class ToHttpClient : public Impl {
 public:
  using Impl::Impl;

  auto Fetch(Request<std::string> request,
             stdx::stop_token stop_token = stdx::stop_token()) const {
    auto headers = std::move(request.headers);
    if (request.body && !GetHeader(headers, "Content-Length")) {
      headers.emplace_back("Content-Length",
                           std::to_string(request.body->length()));
    }
    return Fetch(
        Request<>{.url = std::move(request.url),
                  .method = std::move(request.method),
                  .headers = std::move(headers),
                  .body = request.body ? std::make_optional(CreateBody(
                                             std::move(*request.body)))
                                       : std::nullopt,
                  .flags = static_cast<Request<>::Flag>(request.flags)},
        std::move(stop_token));
  }

  auto Fetch(Request<> request,
             stdx::stop_token stop_token = stdx::stop_token()) const {
    return Impl::Fetch(std::move(request), std::move(stop_token));
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
};

}  // namespace coro::http

namespace std {
template <>
struct hash<coro::http::Request<std::string>> {
  size_t operator()(const coro::http::Request<std::string>& r) const;
};
}  // namespace std

#endif  // CORO_HTTP_HTTP_H
