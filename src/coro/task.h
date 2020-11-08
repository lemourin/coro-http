#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/coroutine.h>

#include <iostream>
#include <memory>
#include <utility>

namespace coro {

namespace internal {

template <typename PromiseTypeT>
class BaseTask {
 public:
  using promise_type = PromiseTypeT;

  ~BaseTask() {
    if (promise_) {
      stdx::coroutine_handle<promise_type>::from_promise(*promise_).destroy();
    }
  }

  BaseTask(const BaseTask&) = delete;
  BaseTask(BaseTask&& task) noexcept
      : promise_(std::exchange(task.promise_, nullptr)) {}

  BaseTask& operator=(const BaseTask&) = delete;
  BaseTask& operator=(BaseTask&& task) noexcept {
    promise_ = std::exchange(task.promise_, nullptr);
    return *this;
  }

  bool await_ready() { return true; }
  void await_suspend(stdx::coroutine_handle<void>) {}

 protected:
  explicit BaseTask(promise_type* promise) : promise_(promise) {}

  promise_type* promise_;
};

template <typename T>
class ValuePromiseType;

class NoValuePromiseType;

}  // namespace internal

template <typename... Ts>
class Task;

template <typename T>
class Task<T> : public internal::BaseTask<internal::ValuePromiseType<T>> {
 public:
  T await_resume() { return *std::move(this->promise_->value_); }

 private:
  template <typename>
  friend class internal::ValuePromiseType;

  using internal::BaseTask<internal::ValuePromiseType<T>>::BaseTask;
};

template <>
class Task<> : public internal::BaseTask<internal::NoValuePromiseType> {
 public:
  void await_resume() {}

 private:
  friend class internal::NoValuePromiseType;

  using internal::BaseTask<internal::NoValuePromiseType>::BaseTask;
};

namespace internal {

template <typename T>
class ValuePromiseType {
 public:
  Task<T> get_return_object() { return Task<T>(this); }

  stdx::suspend_never initial_suspend() { return {}; }
  stdx::suspend_always final_suspend() { return {}; }
  void unhandled_exception() { std::terminate(); }

  template <typename V>
  void return_value(V&& value) {
    value_ = std::make_unique<T>(std::forward<V>(value));
  }

 private:
  template <typename...>
  friend class coro::Task;

  std::unique_ptr<T> value_;
};

class NoValuePromiseType {
 public:
  Task<> get_return_object() { return Task<>(this); }

  stdx::suspend_never initial_suspend() { return {}; }
  stdx::suspend_always final_suspend() { return {}; }
  void unhandled_exception() { std::terminate(); }

  void return_void() {}
};

}  // namespace internal

}  // namespace coro

#endif  // CORO_TASK_H