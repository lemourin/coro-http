#ifndef CORO_HTTP_HTTP_BODY_GENERATOR_H
#define CORO_HTTP_HTTP_BODY_GENERATOR_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "coro/stdx/coroutine.h"

namespace coro::http {

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
    bool operator==(const Iterator& iterator) const;
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
  auto GetBufferedByteCount() const { return data_.size(); }

 private:
  stdx::coroutine_handle<void> handle_;
  std::string data_;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
};

template <typename Impl>
HttpBodyGenerator<Impl>::Iterator::Iterator(
    HttpBodyGenerator* http_body_generator, int64_t offset, std::string data)
    : http_body_generator_(http_body_generator),
      offset_(offset),
      data_(std::move(data)) {}

template <typename Impl>
bool HttpBodyGenerator<Impl>::Iterator::operator!=(
    const Iterator& iterator) const {
  return offset_ != iterator.offset_;
}

template <typename Impl>
bool HttpBodyGenerator<Impl>::Iterator::operator==(
    const Iterator& iterator) const {
  return !this->operator!=(iterator);
}

template <typename Impl>
auto HttpBodyGenerator<Impl>::Iterator::operator++() -> Awaitable<Iterator&> {
  offset_++;
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

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_BODY_GENERATOR_H
