#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/coroutine.h>

#include <memory>
#include <utility>

namespace coro {

namespace internal {

class PromiseType;
class NoValuePromiseType;
template <typename T>
class ValuePromiseType;

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

  bool await_ready() { return false; }
  void await_suspend(stdx::coroutine_handle<void>) {}

 protected:
  explicit BaseTask(promise_type* promise) : promise_(promise) {}

  promise_type* promise_;
};

}  // namespace internal

template <typename... Ts>
class Task;

template <typename T>
class Task<T> : public internal::BaseTask<internal::ValuePromiseType<T>> {
 public:
  T await_resume();

 private:
  template <typename>
  friend class internal::ValuePromiseType;
  friend class internal::PromiseType;

  using internal::BaseTask<internal::ValuePromiseType<T>>::BaseTask;
};

template <>
class Task<> : public internal::BaseTask<internal::NoValuePromiseType> {
 public:
  void await_resume();

 private:
  friend class internal::NoValuePromiseType;
  friend class internal::PromiseType;

  using internal::BaseTask<internal::NoValuePromiseType>::BaseTask;
};

namespace internal {

class PromiseType {
 public:
  stdx::suspend_never initial_suspend() { return {}; }
  stdx::suspend_always final_suspend() { return {}; }
  void unhandled_exception() { std::terminate(); }

 private:
  template <typename>
  friend class coro::internal::BaseTask;
};

template <typename T>
class ValuePromiseType : public PromiseType {
 public:
  Task<T> get_return_object() { return Task<T>(this); }

  template <typename V>
  void return_value(V&& value) {
    value_ = std::make_unique<T>(std::forward<V>(value));
  }

 private:
  template <typename...>
  friend class coro::Task;

  std::unique_ptr<T> value_;
};

class NoValuePromiseType : public PromiseType {
 public:
  Task<> get_return_object() { return Task<>(this); }

  void return_void() {}

 private:
  template <typename...>
  friend class coro::Task;
};

}  // namespace internal

inline void Task<>::await_resume() {}

template <typename T>
T Task<T>::await_resume() {
  return *std::move(this->promise_->value_);
}

}  // namespace coro

#endif  // CORO_TASK_H
