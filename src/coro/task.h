#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/coroutine.h>

#include <memory>
#include <utility>

namespace coro {

template <typename... Ts>
class Task;

template <typename T>
class Task<T> {
 public:
  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return true; }
      auto await_suspend(auto) noexcept {}
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
    void unhandled_exception() { std::terminate(); }

    stdx::coroutine_handle<void> continuation;
    std::shared_ptr<std::unique_ptr<T>> value =
        std::make_shared<std::unique_ptr<T>>();
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

  bool await_ready() { return bool(*value_); }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  T await_resume() { return std::move(**value_); }

 private:
  Task(stdx::coroutine_handle<promise_type> handle,
       std::shared_ptr<std::unique_ptr<T>> value)
      : handle_(handle), value_(std::move(value)) {}

  stdx::coroutine_handle<promise_type> handle_;
  std::shared_ptr<std::unique_ptr<T>> value_;
};

template <>
class Task<> {
 public:
  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return true; }
      auto await_suspend(auto) noexcept {}
      void await_resume() noexcept {
        if (promise->continuation) {
          promise->continuation.resume();
        }
      }

      promise_type* promise;
    };

    Task get_return_object() {
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this),
                  ready);
    }
    stdx::suspend_never initial_suspend() { return {}; }
    final_awaitable final_suspend() { return {this}; }
    void return_void() { *ready = true; }
    void unhandled_exception() { std::terminate(); }

    stdx::coroutine_handle<void> continuation;
    std::shared_ptr<bool> ready = std::make_shared<bool>();
  };

  Task(const Task&) = delete;
  Task(Task&& task) noexcept
      : handle_(std::exchange(task.handle_, nullptr)),
        ready_(std::move(task.ready_)) {}
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    handle_ = std::exchange(task.handle_, nullptr);
    ready_ = std::move(task.ready_);
    return *this;
  }

  bool await_ready() { return *ready_; }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  void await_resume() {}

 private:
  Task(stdx::coroutine_handle<promise_type> handle, std::shared_ptr<bool> ready)
      : handle_(handle), ready_(std::move(ready)) {}

  stdx::coroutine_handle<promise_type> handle_;
  std::shared_ptr<bool> ready_;
};

// clang-format off
template <typename T>
concept Awaitable = requires(T v) {
  v.await_resume();
  v.await_suspend(std::declval<stdx::coroutine_handle<void>>());
  { v.await_ready() } -> std::same_as<bool>;
};
// clang-format on

}  // namespace coro

#endif  // CORO_TASK_H
