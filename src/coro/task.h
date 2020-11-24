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
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this));
    }
    stdx::suspend_always initial_suspend() { return {}; }
    final_awaitable final_suspend() { return {this}; }
    template <typename V>
    void return_value(V&& v) {
      value = std::make_unique<T>(std::forward<V>(v));
    }
    void unhandled_exception() { std::terminate(); }

    stdx::coroutine_handle<void> continuation;
    std::unique_ptr<T> value;
  };

  explicit Task(stdx::coroutine_handle<promise_type> handle)
      : handle_(handle) {}

  Task(const Task&) = delete;
  Task(Task&& task) noexcept : handle_(std::exchange(task.handle_, nullptr)) {}
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    ~Task();
    handle_ = std::exchange(task.handle_, nullptr);
  }

  bool await_ready() { return false; }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
    handle_.resume();
  }
  T await_resume() { return std::move(*handle_.promise().value); }

 private:
  stdx::coroutine_handle<promise_type> handle_;
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

  Task(stdx::coroutine_handle<promise_type> handle, std::shared_ptr<bool> ready)
      : handle_(handle), ready_(std::move(ready)) {}

  Task(const Task&) = delete;
  Task(Task&& task) noexcept
      : handle_(std::exchange(task.handle_, nullptr)),
        ready_(std::move(task.ready_)) {}
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    this->~Task();
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
