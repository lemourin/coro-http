#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/interrupted_exception.h>
#include <coro/stdx/concepts.h>
#include <coro/stdx/coroutine.h>

#include <memory>
#include <tuple>
#include <utility>

namespace coro {

template <typename... Ts>
class Task;

template <typename T>
class [[nodiscard]] Task<T> {
 public:
  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return false; }
      auto await_suspend(stdx::coroutine_handle<promise_type> coro) noexcept {
        return coro.promise().continuation;
      }
      void await_resume() noexcept {}
    };

    promise_type() noexcept {}

    ~promise_type() {
      switch (type) {
        case Type::kValue:
          value.~T();
          break;
        case Type::kException:
          exception.~exception_ptr();
          break;
        default:
          break;
      }
    }

    Task get_return_object() {
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this));
    }
    stdx::suspend_never initial_suspend() { return {}; }
    final_awaitable final_suspend() noexcept { return {}; }
    template <typename V>
    void return_value(V&& v) {
      new (static_cast<void*>(std::addressof(value))) T(std::forward<V>(v));
      type = Type::kValue;
    }
    void unhandled_exception() {
      new (static_cast<void*>(std::addressof(exception)))
          std::exception_ptr(std::current_exception());
      type = Type::kException;
    }

    stdx::coroutine_handle<void> continuation = stdx::noop_coroutine();
    union {
      T value;
      std::exception_ptr exception;
    };
    enum class Type { kNone, kValue, kException } type = Type::kNone;
  };

  Task(const Task&) = delete;
  Task(Task && task) noexcept : handle_(std::exchange(task.handle_, nullptr)) {}
  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    handle_ = std::exchange(task.handle_, nullptr);
    return *this;
  }

  bool await_ready() {
    return handle_.promise().type != promise_type::Type::kNone;
  }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  T await_resume() {
    if (handle_.promise().type == promise_type::Type::kException) {
      std::rethrow_exception(handle_.promise().exception);
    }
    return std::move(handle_.promise().value);
  }

 private:
  explicit Task(stdx::coroutine_handle<promise_type> handle)
      : handle_(handle) {}

  stdx::coroutine_handle<promise_type> handle_;
};

template <>
class [[nodiscard]] Task<> {
 public:
  struct promise_type {
    struct final_awaitable {
      bool await_ready() noexcept { return false; }
      auto await_suspend(stdx::coroutine_handle<promise_type> coro) noexcept {
        return coro.promise().continuation;
      }
      void await_resume() noexcept {}
    };

    Task get_return_object() {
      return Task(stdx::coroutine_handle<promise_type>::from_promise(*this));
    }
    stdx::suspend_never initial_suspend() { return {}; }
    final_awaitable final_suspend() noexcept { return {}; }
    void return_void() { done = true; }
    void unhandled_exception() { exception = std::current_exception(); }

    stdx::coroutine_handle<void> continuation = stdx::noop_coroutine();
    std::exception_ptr exception;
    bool done = false;
  };

  Task(const Task&) = delete;
  Task(Task && task) noexcept : handle_(std::exchange(task.handle_, nullptr)) {}
  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    handle_ = std::exchange(task.handle_, nullptr);
    return *this;
  }

  bool await_ready() {
    return handle_.promise().done || handle_.promise().exception;
  }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
  }
  void await_resume() {
    if (handle_.promise().exception) {
      std::rethrow_exception(handle_.promise().exception);
    }
  }

 private:
  explicit Task(stdx::coroutine_handle<promise_type> handle)
      : handle_(handle) {}

  stdx::coroutine_handle<promise_type> handle_;
};

// clang-format off
template <typename T>
concept Awaitable = requires(T v, stdx::coroutine_handle<void> handle) {
  v.await_resume();
  v.await_suspend(handle);
  { v.await_ready() } -> stdx::same_as<bool>;
};
// clang-format on

struct RunTask {
  struct promise_type {
    auto get_return_object() { return RunTask(); }
    stdx::suspend_never initial_suspend() { return {}; }
    stdx::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

inline RunTask Invoke(Task<> task) {
  try {
    co_await task;
  } catch (const InterruptedException&) {
  }
}

template <typename F, typename... Args>
RunTask Invoke(F func, Args&&... args) {
  co_await func(std::forward<Args>(args)...);
}

template <typename... T>
Task<std::tuple<T...>> WhenAll(Task<T>... tasks) {
  co_return std::make_tuple((co_await tasks)...);
}

}  // namespace coro

#endif  // CORO_TASK_H
