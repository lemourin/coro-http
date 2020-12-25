#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/generator.h>
#include <coro/http/http_parse.h>
#include <coro/stdx/concepts.h>
#include <coro/stdx/coroutine.h>
#include <coro/stdx/stop_token.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace coro::http {

enum class Method {
  kGet,
  kPost,
  kOptions,
  kHead,
  kPropfind,
};

inline const char* MethodToString(Method method) {
  switch (method) {
    case Method::kGet:
      return "GET";
    case Method::kPost:
      return "POST";
    case Method::kHead:
      return "HEAD";
    case Method::kOptions:
      return "OPTIONS";
    case Method::kPropfind:
      return "PROPFIND";
    default:
      return "UNKNOWN";
  }
}

template <typename BodyGenerator = Generator<std::string>>
struct Request {
  std::string url;
  Method method = Method::kGet;
  std::vector<std::pair<std::string, std::string>> headers;
  std::optional<BodyGenerator> body;

  friend bool operator==(const Request& r1, const Request& r2) {
    return std::tie(r1.url, r1.method, r1.headers, r1.body) ==
           std::tie(r2.url, r2.method, r2.headers, r2.body);
  }
};

template <GeneratorLike HttpBodyGenerator = Generator<std::string>>
struct Response {
  int status = -1;
  std::vector<std::pair<std::string, std::string>> headers;
  HttpBodyGenerator body;
};

template <GeneratorLike HttpBodyGenerator>
Task<std::string> GetBody(HttpBodyGenerator body) {
  std::string result;
  FOR_CO_AWAIT(const std::string& piece, body, { result += piece; });
  co_return result;
}

class HttpException : public std::exception {
 public:
  static constexpr int kAborted = -1;
  static constexpr int kMalformedResponse = -2;
  static constexpr int kUnknown = -3;
  static constexpr int kInvalidMethod = -4;
  static constexpr int kBadRequest = 400;
  static constexpr int kNotFound = 404;

  HttpException(int status) : HttpException(status, ToString(status)) {}

  HttpException(int status, std::string_view message)
      : status_(status), message_(message) {}

  [[nodiscard]] const char* what() const noexcept override {
    return message_.c_str();
  }
  [[nodiscard]] int status() const noexcept { return status_; }

 private:
  std::string ToString(int status) {
    switch (status) {
      case kAborted:
        return "Aborted.";
      case kNotFound:
        return "Not found.";
      case kMalformedResponse:
        return "Malformed response.";
      case kInvalidMethod:
        return "Invalid method.";
      case kBadRequest:
        return "Bad request.";
      default:
        return "Unknown.";
    }
  }

  int status_;
  std::string message_;
};

template <typename Impl>
class HttpBodyGenerator {
 public:
  template <typename Iterator>
  struct Awaitable {
    [[nodiscard]] bool await_ready() const {
      return !i.http_body_generator_->data_.empty() ||
             i.http_body_generator_->status_ != -1 ||
             i.http_body_generator_->exception_ptr_;
    }
    void await_suspend(stdx::coroutine_handle<void> handle) {
      i.http_body_generator_->handle_ = handle;
    }
    Iterator await_resume() {
      if ((i.http_body_generator_->status_ != -1 &&
           i.http_body_generator_->data_.empty()) ||
          i.http_body_generator_->exception_ptr_) {
        i.offset_ = INT64_MAX;
      }
      i.data_ = std::move(i.http_body_generator_->data_);
      if (i.http_body_generator_->exception_ptr_) {
        std::rethrow_exception(i.http_body_generator_->exception_ptr_);
      }
      return i;
    }
    Iterator i;
  };

  class Iterator {
   public:
    Iterator(HttpBodyGenerator* http_body_generator, int64_t offset,
             std::string data);

    bool operator!=(const Iterator& iterator) const;
    Awaitable<Iterator&> operator++();
    const std::string& operator*() const;
    std::string& operator*();

   private:
    template <typename>
    friend struct Awaitable;
    HttpBodyGenerator* http_body_generator_;
    int64_t offset_;
    std::string data_;
  };

  Awaitable<Iterator> begin();
  Iterator end();

 protected:
  void ReceivedData(std::string_view data);
  void Close(int status);
  void Close(std::exception_ptr);

 private:
  stdx::coroutine_handle<void> handle_;
  std::string data_;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
};

template <typename Impl>
HttpBodyGenerator<Impl>::Iterator::Iterator(
    HttpBodyGenerator* http_body_generator, int64_t offset, std::string data)
    : http_body_generator_(http_body_generator), offset_(offset), data_(data) {}

template <typename Impl>
bool HttpBodyGenerator<Impl>::Iterator::operator!=(
    const Iterator& iterator) const {
  return offset_ != iterator.offset_;
}

template <typename Impl>
auto HttpBodyGenerator<Impl>::Iterator::operator++() -> Awaitable<Iterator&> {
  if (http_body_generator_->status_ != -1 ||
      http_body_generator_->exception_ptr_) {
    offset_ = INT64_MAX;
  } else {
    offset_++;
  }
  static_cast<Impl*>(http_body_generator_)->Resume();
  return Awaitable<Iterator&>{*this};
}

template <typename Impl>
const std::string& HttpBodyGenerator<Impl>::Iterator::operator*() const {
  return data_;
}

template <typename Impl>
std::string& HttpBodyGenerator<Impl>::Iterator::operator*() {
  return data_;
}

template <typename Impl>
auto HttpBodyGenerator<Impl>::begin() -> Awaitable<Iterator> {
  return Awaitable<Iterator>{Iterator(this, 0, "")};
}

template <typename Impl>
typename HttpBodyGenerator<Impl>::Iterator HttpBodyGenerator<Impl>::end() {
  return Iterator(this, INT64_MAX, "");
}

template <typename Impl>
void HttpBodyGenerator<Impl>::ReceivedData(std::string_view data) {
  data_ += data;
  if (handle_) {
    std::exchange(handle_, nullptr).resume();
  }
}

template <typename Impl>
void HttpBodyGenerator<Impl>::Close(int status) {
  status_ = status;
  if (handle_) {
    std::exchange(handle_, nullptr).resume();
  }
}

template <typename Impl>
void HttpBodyGenerator<Impl>::Close(std::exception_ptr exception) {
  exception_ptr_ = std::move(exception);
  if (handle_) {
    std::exchange(handle_, nullptr).resume();
  }
}

// clang-format off

template <typename T>
concept HeaderCollection = requires(T v) {
  std::begin(v);
  std::end(v);
  { std::get<0>(*std::begin(v)) } -> stdx::convertible_to<std::string>;
  { std::get<1>(*std::begin(v)) } -> stdx::convertible_to<std::string>;
};

template <typename T>
concept ResponseLike = requires (T v) {
  { v.status } -> stdx::convertible_to<int>;
  { v.headers } -> HeaderCollection;
  { v.body } -> coro::GeneratorLike;
};

template <typename T>
concept HttpOperation = requires (T v) {
  { v.await_resume() } -> ResponseLike;
};

template <typename T>
concept HttpClientImpl = requires(T v, Request<> request, stdx::stop_token stop_token) {
  { v.Fetch(std::move(request), stop_token) } -> HttpOperation;
};

template <typename T>
concept HttpClient = HttpClientImpl<T> && requires(T v) {
  { v.Fetch(std::string(), stdx::stop_token()) } -> HttpOperation;
  typename T::ResponseType;
};

// clang-format on

template <HttpClientImpl Impl>
class ToHttpClient : protected Impl {
 public:
  using ResponseType = decltype(
      std::declval<Impl>()
          .Fetch(std::declval<Request<>>(), std::declval<stdx::stop_token>())
          .await_resume());

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
                  .body = request.body ? std::make_optional(ToGenerator(
                                             std::move(*request.body)))
                                       : std::nullopt},
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

 private:
  static Generator<std::string> ToGenerator(std::string value) {
    co_yield value;
  }
};

struct HttpOperationStub {
  Response<> await_resume();
};

struct HttpStub {
  using ResponseType = Response<>;
  HttpOperationStub Fetch(std::string, stdx::stop_token) const;
  HttpOperationStub Fetch(Request<>, stdx::stop_token) const;
};

}  // namespace coro::http

namespace std {
template <>
struct hash<coro::http::Request<std::string>> {
  static size_t CombineHash(size_t lhs, size_t rhs) {
    lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
    return lhs;
  }
  size_t operator()(const coro::http::Request<std::string>& r) const {
    return CombineHash(std::hash<std::string>{}(r.url),
                       std::hash<std::optional<std::string>>{}(r.body));
  }
};
}  // namespace std

#endif  // CORO_HTTP_HTTP_H
