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

    stdx::coroutine_handle<void> continuation = coro::std_ns::noop_coroutine();
    union {
      T value;
      std::exception_ptr exception;
    };
    enum class Type { kNone, kValue, kException } type = Type::kNone;
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
    final_awaitable final_suspend() noexcept { return {this}; }
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

template <typename F, typename... Args>
Task<> Invoke(F func, Args&&... args) {
  co_await func(std::forward<Args>(args)...);
}

}  // namespace coro

#endif  // CORO_TASK_H
