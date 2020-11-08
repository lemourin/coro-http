#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/coroutine.h>

#include <memory>

namespace coro {

template <typename... Ts>
class Task;

namespace internal {
struct EmptyPromiseType;
template <typename>
struct ValuePromiseType;
}  // namespace internal

template <typename T>
class Task<T> {
 public:
  using promise_type = internal::ValuePromiseType<T>;

  bool await_ready() { return bool(data_->value); }

  T await_resume() { return *std::move(data_->value); }

  void await_suspend(stdx::coroutine_handle<void> handle) {
    data_->handle = handle;
  }

  struct CommonData {
    std::unique_ptr<T> value;
    stdx::coroutine_handle<void> handle;
  };

  std::shared_ptr<CommonData> data_ = std::make_shared<CommonData>();
};

template <>
class Task<> {
 public:
  using promise_type = internal::EmptyPromiseType;

  bool await_ready() { return data_->ready; }

  void await_suspend(stdx::coroutine_handle<void> handle) {
    data_->handle = handle;
  }

  void await_resume() {}

  struct CommonData {
    bool ready = false;
    stdx::coroutine_handle<void> handle;
  };

  std::shared_ptr<CommonData> data_ = std::make_shared<CommonData>();
};

namespace internal {

template <typename T>
struct ValuePromiseType {
  Task<T>& get_return_object() { return promise; }

  stdx::suspend_never initial_suspend() noexcept { return {}; }
  stdx::suspend_never final_suspend() noexcept { return {}; }

  void unhandled_exception() { std::terminate(); }

  template <typename ValueT>
  void return_value(ValueT&& value) {
    promise.data_->value = std::make_unique<T>(std::forward<ValueT>(value));
    if (promise.data_->handle) {
      promise.data_->handle.resume();
    }
  }

  Task<T> promise;
};

struct EmptyPromiseType {
  Task<>& get_return_object() { return promise; }

  stdx::suspend_never initial_suspend() noexcept { return {}; }
  stdx::suspend_never final_suspend() noexcept { return {}; }

  void unhandled_exception() { std::terminate(); }

  void return_void() {
    promise.data_->ready = true;
    if (promise.data_->handle) {
      promise.data_->handle.resume();
    }
  }

  Task<> promise;
};

}  // namespace internal

}  // namespace coro

#endif  // CORO_TASK_H