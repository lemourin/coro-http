#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/concepts.h>
#include <coro/stdx/coroutine.h>

#include <functional>
#include <memory>
#include <utility>
#include <variant>

namespace coro {

template <typename... Ts>
class Task;

template <typename T>
class Task<T> {
 public:
  using DataType =
      std::variant<std::monostate, std::unique_ptr<T>, std::exception_ptr>;

  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return true; }
      template <typename C>
      auto await_suspend(C) noexcept {}
      void await_resume() noexcept {
        if (promise->continuation) {
          promise->continuation.resume();
        }
      }

      promise_type* promise;
    };

    Task get_return_object() {
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this),
                  value);
    }
    stdx::suspend_never initial_suspend() { return {}; }
    final_awaitable final_suspend() { return {this}; }
    template <typename V>
    void return_value(V&& v) {
      *value = std::make_unique<T>(std::forward<V>(v));
    }
    void unhandled_exception() { *value = std::current_exception(); }

    stdx::coroutine_handle<void> continuation;
    std::shared_ptr<DataType> value = std::make_shared<DataType>();
  };

  Task(const Task&) = delete;
  Task(Task&& task) noexcept
      : handle_(std::exchange(task.handle_, nullptr)),
        value_(std::move(task.value_)) {}
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    handle_ = std::exchange(task.handle_, nullptr);
    value_ = std::move(task.value_);
    return *this;
  }

  bool await_ready() {
    return !std::holds_alternative<std::monostate>(*value_);
  }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  T await_resume() {
    if (std::holds_alternative<std::exception_ptr>(*value_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(*value_));
    }
    return std::move(*std::get<std::unique_ptr<T>>(*value_));
  }

 private:
  Task(stdx::coroutine_handle<promise_type> handle,
       std::shared_ptr<DataType> value)
      : handle_(handle), value_(std::move(value)) {}

  stdx::coroutine_handle<promise_type> handle_;
  std::shared_ptr<DataType> value_;
};

template <>
class Task<> {
 public:
  using DataType = std::variant<std::monostate, bool, std::exception_ptr>;

  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return true; }
      template <typename V>
      auto await_suspend(V) noexcept {}
      void await_resume() noexcept {
        if (promise->continuation) {
          promise->continuation.resume();
        }
      }

      promise_type* promise;
    };

    Task get_return_object() {
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this),
                  value);
    }
    stdx::suspend_never initial_suspend() { return {}; }
    final_awaitable final_suspend() { return {this}; }
    void return_void() { *value = true; }
    void unhandled_exception() { *value = std::current_exception(); }

    stdx::coroutine_handle<void> continuation;
    std::shared_ptr<DataType> value = std::make_shared<DataType>();
  };

  Task(const Task&) = delete;
  Task(Task&& task) noexcept
      : handle_(std::exchange(task.handle_, nullptr)),
        value_(std::move(task.value_)) {}
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    handle_ = std::exchange(task.handle_, nullptr);
    value_ = std::move(task.value_);
    return *this;
  }

  bool await_ready() {
    return !std::holds_alternative<std::monostate>(*value_);
  }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  void await_resume() {
    if (std::holds_alternative<std::exception_ptr>(*value_)) {
      std::rethrow_exception(std::get<std::exception_ptr>(*value_));
    }
  }

 private:
  Task(stdx::coroutine_handle<promise_type> handle,
       std::shared_ptr<DataType> value)
      : handle_(handle), value_(std::move(value)) {}

  stdx::coroutine_handle<promise_type> handle_;
  std::shared_ptr<DataType> value_;
};

// clang-format off
template <typename T>
concept Awaitable = requires(T v, stdx::coroutine_handle<void> handle) {
  v.await_resume();
  v.await_suspend(handle);
  { v.await_ready() } -> stdx::same_as<bool>;
};
// clang-format on

}  // namespace coro

#endif  // CORO_TASK_H
