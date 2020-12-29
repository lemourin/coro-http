#ifndef CORO_CLOUDSTORAGE_PROMISE_H
#define CORO_CLOUDSTORAGE_PROMISE_H

#include <coro/stdx/coroutine.h>

#include <optional>
#include <utility>
#include <variant>

namespace coro {

template <typename T>
class Promise {
 public:
  Promise() = default;
  Promise(const Promise&) = delete;
  Promise(Promise&& other)
      : result_(std::move(other.result_)),
        continuation_(std::exchange(other.continuation_, nullptr)) {}

  Promise& operator=(const Promise&) = delete;
  Promise& operator=(Promise&& other) {
    result_ = std::move(other.result_);
    continuation_ = std::exchange(other.continuation_, nullptr);
    return *this;
  }

  bool await_ready() const { return bool(result_); }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    continuation_ = continuation;
  }
  T await_resume() {
    if (std::holds_alternative<std::exception_ptr>(*result_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(*result_));
    }
    return std::move(std::get<T>(*result_));
  }
  void SetValue(T value) {
    result_ = std::move(value);
    if (continuation_) {
      std::exchange(continuation_, nullptr).resume();
    }
  }
  template <typename Exception>
  void SetException(Exception e) {
    result_ = std::make_exception_ptr(std::move(e));
    if (continuation_) {
      std::exchange(continuation_, nullptr).resume();
    }
  }

 private:
  std::optional<std::variant<T, std::exception_ptr>> result_;
  stdx::coroutine_handle<void> continuation_ = stdx::noop_coroutine();
};

template <>
class Promise<void> {
 public:
  Promise() = default;
  Promise(const Promise&) = delete;
  Promise(Promise&& other)
      : result_(std::move(other.result_)),
        continuation_(std::exchange(other.continuation_, nullptr)) {}

  Promise& operator=(const Promise&) = delete;
  Promise& operator=(Promise&& other) {
    result_ = std::move(other.result_);
    continuation_ = std::exchange(other.continuation_, nullptr);
    return *this;
  }

  bool await_ready() const { return bool(result_); }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    continuation_ = continuation;
  }
  void await_resume() {
    if (std::holds_alternative<std::exception_ptr>(*result_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(*result_));
    }
  }
  void SetValue() {
    result_ = std::monostate();
    if (continuation_) {
      std::exchange(continuation_, nullptr).resume();
    }
  }
  template <typename Exception>
  void SetException(Exception e) {
    result_ = std::make_exception_ptr(std::move(e));
    if (continuation_) {
      std::exchange(continuation_, nullptr).resume();
    }
  }

 private:
  std::optional<std::variant<std::monostate, std::exception_ptr>> result_;
  stdx::coroutine_handle<void> continuation_ = stdx::noop_coroutine();
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_PROMISE_H
