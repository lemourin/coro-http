#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <memory>
#include <tuple>
#include <utility>

#include "coro/interrupted_exception.h"
#include "coro/stdx/concepts.h"
#include "coro/stdx/coroutine.h"

namespace coro {

namespace detail {

enum class TaskResultType { kNone, kValue, kException };

template <typename T>
struct TaskResult {
  TaskResult() noexcept {}
  TaskResult(const TaskResult&) = delete;
  TaskResult(TaskResult&&) = delete;
  TaskResult& operator=(const TaskResult&) = delete;
  TaskResult& operator=(TaskResult&&) = delete;

  ~TaskResult() {
    switch (type) {
      case TaskResultType::kValue:
        if constexpr (!std::is_reference_v<T>) {
          value.~T();
        }
        break;
      case TaskResultType::kException:
        exception.~exception_ptr();
        break;
      default:
        break;
    }
  }

  union {
    std::conditional_t<std::is_reference_v<T>, std::remove_reference_t<T>*, T>
        value;
    std::exception_ptr exception;
  };
  TaskResultType type = TaskResultType::kNone;
};

template <>
struct TaskResult<void> {
  std::exception_ptr exception;
  TaskResultType type = TaskResultType::kNone;
};

template <typename Derived, typename T>
struct ReturnValue {
  template <typename V>
  void return_value(V&& v) {
    auto* d = static_cast<Derived*>(this);
    if constexpr (std::is_reference_v<T>) {
      d->result.value = &v;
    } else {
      new (static_cast<void*>(std::addressof(d->result.value)))
          T(std::forward<V>(v));
    }
    d->result.type = TaskResultType::kValue;
  }
};

template <typename Derived>
struct ReturnValue<Derived, void> {
  void return_void() {
    static_cast<Derived*>(this)->result.type = TaskResultType::kValue;
  }
};

template <typename... Ts>
class Task;

template <typename T>
class [[nodiscard]] Task<T> {
 public:
  using type = T;

  struct promise_type : ReturnValue<promise_type, T> {
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
    stdx::suspend_always initial_suspend() { return {}; }
    final_awaitable final_suspend() noexcept { return {}; }
    void unhandled_exception() {
      new (static_cast<void*>(std::addressof(result.exception)))
          std::exception_ptr(std::current_exception());
      result.type = TaskResultType::kException;
    }

    stdx::coroutine_handle<void> continuation = stdx::noop_coroutine();
    TaskResult<T> result;
  };

  Task(const Task&) = delete;
  Task(Task&& task) noexcept : handle_(std::exchange(task.handle_, nullptr)) {}
  ~Task() {
    if (handle_) {
      handle_.destroy();
    }
  }
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& task) noexcept {
    this->~Task();
    handle_ = std::exchange(task.handle_, nullptr);
    return *this;
  }

  bool await_ready() {
    return handle_.promise().result.type != TaskResultType::kNone;
  }
  auto await_suspend(stdx::coroutine_handle<void> continuation) {
    handle_.promise().continuation = continuation;
    return handle_;
  }
  T await_resume() {
    if (handle_.promise().result.type == TaskResultType::kException) {
      std::rethrow_exception(handle_.promise().result.exception);
    }
    if constexpr (std::is_reference_v<T>) {
      return *handle_.promise().result.value;
    } else if constexpr (std::is_void_v<T>) {
      return;
    } else {
      return std::move(handle_.promise().result.value);
    }
  }

 private:
  explicit Task(stdx::coroutine_handle<promise_type> handle)
      : handle_(handle) {}

  stdx::coroutine_handle<promise_type> handle_;
};

template <typename...>
struct TaskT;

template <>
struct TaskT<> {
  using type = Task<void>;
};

template <typename T>
struct TaskT<T> {
  using type = Task<T>;
};

}  // namespace detail

template <typename... Ts>
using Task = typename detail::TaskT<Ts...>::type;

template <typename T, typename Result>
concept Awaitable = requires(T v, stdx::coroutine_handle<void> handle) {
  { v.await_resume() } -> stdx::convertible_to<Result>;
  v.await_suspend(handle);
  { v.await_ready() } -> stdx::same_as<bool>;
};

struct RunTaskT {
  struct promise_type {
    auto get_return_object() { return RunTaskT(); }
    stdx::suspend_never initial_suspend() { return {}; }
    stdx::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};

inline RunTaskT RunTask(Task<> task) {
  try {
    co_await task;
  } catch (const InterruptedException&) {
  }
}

template <typename F, typename... Args>
RunTaskT RunTask(F func, Args&&... args) {
  try {
    co_await func(std::forward<Args>(args)...);
  } catch (const InterruptedException&) {
  }
}

}  // namespace coro

#endif  // CORO_TASK_H
