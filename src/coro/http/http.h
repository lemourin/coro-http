#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/stdx/coroutine.h>
#include <coro/stdx/stop_token.h>
#include <coro/util/wrap.h>

#include <concepts>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace coro::http {

template <typename BodyGenerator = Generator<std::string>>
struct Request {
  std::string url;
  std::string method = "GET";
  std::unordered_multimap<std::string, std::string> headers;
  std::optional<BodyGenerator> body;
};

template <GeneratorLike HttpBodyGenerator = Generator<std::string>>
struct Response {
  int status = -1;
  std::unordered_multimap<std::string, std::string> headers;
  HttpBodyGenerator body;
};

template <GeneratorLike HttpBodyGenerator>
Task<std::string> GetBody(HttpBodyGenerator&& body) {
  std::string result;
  FOR_CO_AWAIT(const std::string& piece, body, { result += piece; });
  co_return result;
}

class HttpException : public std::exception {
 public:
  static constexpr int kAborted = -1;
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
      if (i.http_body_generator_->exception_ptr_) {
        std::rethrow_exception(i.http_body_generator_->exception_ptr_);
      }
      return i;
    }
    Iterator i;
  };

  class Iterator {
   public:
    Iterator(HttpBodyGenerator* http_body_generator, int64_t offset);

    bool operator!=(const Iterator& iterator) const;
    Awaitable<Iterator&> operator++();
    const std::string& operator*() const;

   private:
    template <typename>
    friend struct Awaitable;
    HttpBodyGenerator* http_body_generator_;
    int64_t offset_;
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
    HttpBodyGenerator* http_body_generator, int64_t offset)
    : http_body_generator_(http_body_generator), offset_(offset) {}

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
  http_body_generator_->data_.clear();
  static_cast<Impl*>(http_body_generator_)->Resume();
  return Awaitable<Iterator&>{*this};
}

template <typename Impl>
const std::string& HttpBodyGenerator<Impl>::Iterator::operator*() const {
  return http_body_generator_->data_;
}

template <typename Impl>
auto HttpBodyGenerator<Impl>::begin() -> Awaitable<Iterator> {
  return Awaitable<Iterator>{Iterator(this, 0)};
}

template <typename Impl>
typename HttpBodyGenerator<Impl>::Iterator HttpBodyGenerator<Impl>::end() {
  return Iterator(this, INT64_MAX);
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
concept ResponseLike = requires (T v) {
  { v.status } -> std::convertible_to<int>;
  { v.headers } -> std::convertible_to<std::unordered_multimap<std::string, std::string>>;
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
  using ResponseType = decltype(std::declval<Impl>()
                                    .Fetch(std::declval<Request<>>())
                                    .await_resume());

  using Impl::Impl;

  auto Fetch(Request<std::string> request,
             stdx::stop_token stop_token = stdx::stop_token()) {
    auto headers = std::move(request.headers);
    if (request.body) {
      headers.insert(
          {"Content-Length", std::to_string(request.body->length())});
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
             stdx::stop_token stop_token = stdx::stop_token()) {
    return Impl::Fetch(std::move(request), std::move(stop_token));
  }
  auto Fetch(std::string url,
             stdx::stop_token stop_token = stdx::stop_token()) {
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

#endif  // CORO_HTTP_HTTP_H
