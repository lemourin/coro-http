#ifndef CORO_CLOUDSTORAGE_WRAP_H
#define CORO_CLOUDSTORAGE_WRAP_H

#include <coro/stdx/coroutine.h>

#include <concepts>
#include <memory>

namespace coro::util {

// clang-format off
template <typename T>
concept Awaitable = requires(T v) {
  v.await_resume();
  v.await_suspend(std::declval<stdx::coroutine_handle<void>>());
  { v.await_ready() } -> std::same_as<bool>;
};
// clang-format on

template <Awaitable T>
class WrapAwaitable {
 public:
  explicit WrapAwaitable(std::unique_ptr<T>&& awaitable)
      : awaitable_(std::move(awaitable)) {}

  auto await_resume() { return awaitable_->await_resume(); }
  bool await_ready() { return awaitable_->await_ready(); }
  auto await_suspend(stdx::coroutine_handle<void> handle) {
    return awaitable_->await_suspend(handle);
  }

 private:
  std::unique_ptr<T> awaitable_;
};

// clang-format off
template <typename T>
concept GeneratorLike = requires(T v) {
  { v.begin() } -> Awaitable;
  { v.end() } -> Awaitable;
};
// clang-format on

template <GeneratorLike T>
class WrapGenerator {
 public:
  explicit WrapGenerator(std::unique_ptr<T>&& generator)
      : generator_(std::move(generator)) {}

  auto begin() { return generator_->begin(); }
  auto end() { return generator_->end(); }

 private:
  std::unique_ptr<T> generator_;
};

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_WRAP_H
