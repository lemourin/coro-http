#ifndef CORO_TASK_H
#define CORO_TASK_H

#include <coro/stdx/coroutine.h>

#include <memory>
#include <utility>

namespace coro {

namespace internal {

template <typename...>
class BasePromiseType;

template <typename...>
class PromiseType;

template <typename... T>
class BaseTask {
 public:
  using promise_type = PromiseType<T...>;

  BaseTask(const BaseTask&) = delete;
  BaseTask(BaseTask&& task) noexcept
      : promise_(std::exchange(task.promise_, nullptr)) {}

  BaseTask& operator=(const BaseTask&) = delete;
  BaseTask& operator=(BaseTask&& task) noexcept {
    promise_ = std::exchange(task.promise_, nullptr);
    return *this;
  }

  bool await_ready() noexcept { return false; }

  void await_suspend(stdx::coroutine_handle<void> continuation) {
    promise_->continuation_ = continuation;
  }

 protected:
  explicit BaseTask(promise_type* promise) : promise_(promise) {}

  template <typename...>
  friend class internal::BasePromiseType;

  promise_type* promise_;
};

}  // namespace internal

template <typename... Ts>
class Task;

template <typename T>
class Task<T> : public internal::BaseTask<T> {
 public:
  T await_resume();

 private:
  template <typename...>
  friend class internal::BasePromiseType;

  using internal::BaseTask<T>::BaseTask;
};

template <>
class Task<> : public internal::BaseTask<> {
 public:
  void await_resume();

 private:
  template <typename...>
  friend class internal::BasePromiseType;

  using internal::BaseTask<>::BaseTask;
};

namespace internal {

template <typename... T>
class BasePromiseType {
 public:
  class FinalSuspend {
   public:
    FinalSuspend(BasePromiseType* promise) : promise_(promise) {}

    bool await_ready() noexcept { return true; }
    void await_resume() noexcept {
      if (promise_->continuation_) {
        promise_->continuation_.resume();
      }
    }
    void await_suspend(stdx::coroutine_handle<void>) noexcept {}

   private:
    BasePromiseType* promise_;
  };

  ~BasePromiseType() {
    if (exception_) {
      std::terminate();
    }
  }

  Task<T...> get_return_object() {
    return Task<T...>{static_cast<PromiseType<T...>*>(this)};
  }
  stdx::suspend_never initial_suspend() { return {}; }
  FinalSuspend final_suspend() noexcept { return {this}; }
  void unhandled_exception() { exception_ = std::current_exception(); }

 protected:
  std::exception_ptr exception_;
  stdx::coroutine_handle<void> continuation_;

 private:
  template <typename...>
  friend class coro::internal::BaseTask;
};

template <typename T>
class PromiseType<T> : public BasePromiseType<T> {
 public:
  template <typename V>
  void return_value(V&& value) {
    value_ = std::make_unique<T>(std::forward<V>(value));
  }

 private:
  template <typename...>
  friend class coro::Task;

  std::unique_ptr<T> value_;
};

template <>
class PromiseType<> : public BasePromiseType<> {
 public:
  void return_void() {}

 private:
  template <typename...>
  friend class coro::Task;
};

}  // namespace internal

inline void Task<>::await_resume() {
  if (promise_->exception_) {
    std::rethrow_exception(std::exchange(promise_->exception_, nullptr));
  }
}

template <typename T>
T Task<T>::await_resume() {
  if (this->promise_->exception_) {
    std::rethrow_exception(std::exchange(this->promise_->exception_, nullptr));
  }
  return std::move(*this->promise_->value_);
}

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
