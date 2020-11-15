#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/stdx/coroutine.h>
#include <coro/stdx/stop_token.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace coro::http {

const int MAX_BUFFER_SIZE = 1u << 16u;

struct Request {
  std::string url;
  std::unordered_multimap<std::string, std::string> headers;
  std::string body;
};

template <typename HttpBodyGenerator>
struct Response {
  int status = -1;
  std::unordered_multimap<std::string, std::string> headers;
  HttpBodyGenerator body;
};

class HttpException : public std::exception {
 public:
  HttpException(int status, std::string_view message)
      : status_(status), message_(message) {}

  [[nodiscard]] const char* what() const noexcept override {
    return message_.c_str();
  }
  [[nodiscard]] int status() const noexcept { return status_; }

 private:
  int status_;
  std::string message_;
};

template <typename Impl>
class HttpBodyGenerator {
 public:
  class Iterator {
   public:
    Iterator(HttpBodyGenerator* http_body_generator, int64_t offset);

    bool operator!=(const Iterator& iterator) const;
    Iterator& operator++();
    const std::string& operator*() const;

    [[nodiscard]] bool await_ready() const;
    void await_suspend(stdx::coroutine_handle<void> handle);
    Iterator& await_resume();

   private:
    HttpBodyGenerator* http_body_generator_;
    int64_t offset_;
  };

  Iterator begin();
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
  bool paused_ = false;
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
typename HttpBodyGenerator<Impl>::Iterator&
HttpBodyGenerator<Impl>::Iterator::operator++() {
  if (http_body_generator_->status_ != -1 ||
      http_body_generator_->exception_ptr_) {
    offset_ = INT64_MAX;
  } else {
    offset_++;
  }
  http_body_generator_->data_.clear();
  if (http_body_generator_->paused_) {
    http_body_generator_->paused_ = false;
    static_cast<Impl*>(http_body_generator_)->Resume();
  }
  return *this;
}

template <typename Impl>
const std::string& HttpBodyGenerator<Impl>::Iterator::operator*() const {
  return http_body_generator_->data_;
}

template <typename Impl>
bool HttpBodyGenerator<Impl>::Iterator::await_ready() const {
  return !http_body_generator_->data_.empty() ||
         http_body_generator_->status_ != -1 ||
         http_body_generator_->exception_ptr_;
}

template <typename Impl>
void HttpBodyGenerator<Impl>::Iterator::await_suspend(
    stdx::coroutine_handle<void> handle) {
  http_body_generator_->handle_ = handle;
}

template <typename Impl>
typename HttpBodyGenerator<Impl>::Iterator&
HttpBodyGenerator<Impl>::Iterator::await_resume() {
  if (http_body_generator_->exception_ptr_) {
    std::rethrow_exception(http_body_generator_->exception_ptr_);
  }
  return *this;
}

template <typename Impl>
typename HttpBodyGenerator<Impl>::Iterator HttpBodyGenerator<Impl>::begin() {
  return Iterator(this, 0);
}

template <typename Impl>
typename HttpBodyGenerator<Impl>::Iterator HttpBodyGenerator<Impl>::end() {
  return Iterator(this, INT64_MAX);
}

template <typename Impl>
void HttpBodyGenerator<Impl>::ReceivedData(std::string_view data) {
  data_ += data;
  if (data_.size() >= MAX_BUFFER_SIZE && !paused_) {
    paused_ = true;
    static_cast<Impl*>(this)->Pause();
  }
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

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_H
