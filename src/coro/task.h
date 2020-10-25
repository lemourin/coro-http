#ifndef CORO_TASK_H
#define CORO_TASK_H

#ifdef __has_include
#if __has_include(<version>)
#include <version>
#endif
#endif

#ifdef __cpp_lib_coroutine
#include <coroutine>
#define HAVE_COROUTINES
#define HAVE_COROUTINE_SUPPORT
#endif

#ifdef __has_include
#if __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
#define HAVE_EXPERIMENTAL_COROUTINES
#define HAVE_COROUTINE_SUPPORT
#endif
#endif

#include <memory>

namespace coro {

#ifdef HAVE_COROUTINES
template <typename T>
using coroutine_handle = std::coroutine_handle<T>;
using suspend_never = std::suspend_never;
#endif

#ifdef HAVE_EXPERIMENTAL_COROUTINES
template <typename T>
using coroutine_handle = std::experimental::coroutine_handle<T>;
using suspend_never = std::experimental::suspend_never;
#endif

#ifdef HAVE_COROUTINE_SUPPORT

template <typename... Ts>
class Task;

template <typename T>
class Task<T> {
 public:
  struct promise_type {
    Task& get_return_object() { return promise; }

    suspend_never initial_suspend() noexcept { return {}; }
    suspend_never final_suspend() noexcept { return {}; }

    void unhandled_exception() { std::terminate(); }

    template <typename ValueT>
    void return_value(ValueT&& value) {
      promise.data_->value = std::make_unique<T>(std::forward<ValueT>(value));
      if (promise.data_->handle) {
        promise.data_->handle.resume();
      }
    }

    Task promise;
  };

  bool await_ready() { return bool(data_->value); }

  T await_resume() { return *std::move(data_->value); }

  void await_suspend(coroutine_handle<void> handle) { data_->handle = handle; }

  struct CommonData {
    std::unique_ptr<T> value;
    coroutine_handle<void> handle;
  };

  std::shared_ptr<CommonData> data_ = std::make_shared<CommonData>();
};

template <>
class Task<> {
 public:
  struct promise_type {
    Task& get_return_object() { return *promise; }

    suspend_never initial_suspend() noexcept { return {}; }
    suspend_never final_suspend() noexcept { return {}; }

    void unhandled_exception() { std::terminate(); }

    void return_void() {
      promise->data_->ready = true;
      if (promise->data_->handle) {
        promise->data_->handle.resume();
      }
    }

    std::unique_ptr<Task> promise = std::make_unique<Task>();
  };

  bool await_ready() { return data_->ready; }

  void await_suspend(coroutine_handle<void> handle) { data_->handle = handle; }

  struct CommonData {
    bool ready = false;
    coroutine_handle<void> handle;
  };

  std::shared_ptr<CommonData> data_ = std::make_shared<CommonData>();
};

}  // namespace coro

#else
#error "Coroutines unsupported."
#endif

#endif  // CORO_TASK_H