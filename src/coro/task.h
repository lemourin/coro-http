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

template <typename T>
class Task;

class TaskPromiseBase {
 public:
  TaskPromiseBase() noexcept {}

  auto initial_suspend() noexcept { return stdx::suspend_always{}; }

  auto final_suspend() noexcept { return FinalAwaitable{}; }

  void set_continuation(stdx::coroutine_handle<> continuation) noexcept {
    continuation_ = continuation;
  }

 private:
  struct FinalAwaitable {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    stdx::coroutine_handle<> await_suspend(
        stdx::coroutine_handle<Promise> coro) noexcept {
      return coro.promise().continuation_;
    }

    void await_resume() noexcept {}
  };

  stdx::coroutine_handle<> continuation_;
};

template <typename T>
class TaskPromise final : public TaskPromiseBase {
 public:
  TaskPromise() noexcept {}

  ~TaskPromise() {
    switch (result_type_) {
      case ResultType::kValue:
        value_.~T();
        break;
      case ResultType::kException:
        exception_.~exception_ptr();
        break;
      default:
        break;
    }
  }

  Task<T> get_return_object() noexcept;

  void unhandled_exception() noexcept {
    ::new (static_cast<void*>(std::addressof(exception_)))
        std::exception_ptr(std::current_exception());
    result_type_ = ResultType::kException;
  }

  template <typename Value,
            typename = std::enable_if_t<std::is_convertible_v<Value&&, T>>>
  void return_value(Value&& value) noexcept(
      std::is_nothrow_constructible_v<T, Value&&>) {
    ::new (static_cast<void*>(std::addressof(value_)))
        T(std::forward<Value>(value));
    result_type_ = ResultType::kValue;
  }

  T& result() & {
    if (result_type_ == ResultType::kException) {
      std::rethrow_exception(exception_);
    }
    return value_;
  }

  T&& result() && {
    if (result_type_ == ResultType::kException) {
      std::rethrow_exception(exception_);
    }
    return std::move(value_);
  }

 private:
  enum class ResultType {
    kEmpty,
    kValue,
    kException
  } result_type_ = ResultType::kEmpty;

  union {
    T value_;
    std::exception_ptr exception_;
  };
};

template <>
class TaskPromise<void> : public TaskPromiseBase {
 public:
  TaskPromise() noexcept = default;

  Task<void> get_return_object() noexcept;

  void return_void() noexcept {}

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  void result() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
  }

 private:
  std::exception_ptr exception_;
};

template <typename T>
class TaskPromise<T&> : public TaskPromiseBase {
 public:
  TaskPromise() noexcept = default;

  Task<T&> get_return_object() noexcept;

  void unhandled_exception() noexcept { exception_ = std::current_exception(); }

  void return_value(T& value) noexcept { value_ = std::addressof(value); }

  T& result() {
    if (exception_) {
      std::rethrow_exception(exception_);
    }
    return *value_;
  }

 private:
  T* value_ = nullptr;
  std::exception_ptr exception_;
};

template <typename T = void>
class [[nodiscard]] Task {
 public:
  using promise_type = TaskPromise<T>;
  using value_type = T;
  using type = T;

  Task() noexcept : coroutine_(nullptr) {}

  explicit Task(stdx::coroutine_handle<promise_type> coroutine)
      : coroutine_(coroutine) {}

  Task(Task&& t) noexcept : coroutine_(t.coroutine_) { t.coroutine_ = nullptr; }

  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;

  ~Task() {
    if (coroutine_) {
      coroutine_.destroy();
    }
  }

  Task& operator=(Task&& other) noexcept {
    if (std::addressof(other) != this) {
      if (coroutine_) {
        coroutine_.destroy();
      }
      coroutine_ = other.coroutine_;
      other.coroutine_ = nullptr;
    }
    return *this;
  }

  auto operator co_await() const& noexcept {
    class Awaitable : public AwaitableBase {
     public:
      using AwaitableBase::AwaitableBase;

      decltype(auto) await_resume() {
        return this->coroutine_.promise().result();
      }
    };
    return Awaitable{coroutine_};
  }

  auto operator co_await() const&& noexcept {
    class Awaitable : public AwaitableBase {
     public:
      using AwaitableBase::AwaitableBase;

      decltype(auto) await_resume() {
        return std::move(this->coroutine_.promise()).result();
      }
    };
    return Awaitable{coroutine_};
  }

 private:
  class AwaitableBase {
   public:
    AwaitableBase(stdx::coroutine_handle<promise_type> coroutine) noexcept
        : coroutine_(coroutine) {}

    bool await_ready() const noexcept {
      return !coroutine_ || coroutine_.done();
    }

    stdx::coroutine_handle<> await_suspend(
        stdx::coroutine_handle<> awaiting_coroutine) noexcept {
      coroutine_.promise().set_continuation(awaiting_coroutine);
      return coroutine_;
    }

   protected:
    stdx::coroutine_handle<promise_type> coroutine_;
  };

  stdx::coroutine_handle<promise_type> coroutine_;
};

template <typename T>
Task<T> TaskPromise<T>::get_return_object() noexcept {
  return Task<T>{stdx::coroutine_handle<TaskPromise>::from_promise(*this)};
}

inline Task<void> TaskPromise<void>::get_return_object() noexcept {
  return Task<void>{stdx::coroutine_handle<TaskPromise>::from_promise(*this)};
}

template <typename T>
Task<T&> TaskPromise<T&>::get_return_object() noexcept {
  return Task<T&>{stdx::coroutine_handle<TaskPromise>::from_promise(*this)};
}

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

template <typename T>
concept HasCoAwaitOp = requires(T d) { d.operator co_await(); };

template <typename T, typename = void>
struct AwaitableType {
  using type = T;
};

template <typename T>
struct AwaitableType<T, std::enable_if_t<HasCoAwaitOp<T>>> {
  using type = decltype(std::declval<T>().operator co_await());
};

template <typename T>
using AwaitableTypeT = typename AwaitableType<T>::type;

}  // namespace detail

template <typename... Ts>
using Task = typename detail::TaskT<Ts...>::type;

template <typename T, typename Result>
concept Awaitable =
    requires(detail::AwaitableTypeT<T> v, stdx::coroutine_handle<void> handle) {
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
