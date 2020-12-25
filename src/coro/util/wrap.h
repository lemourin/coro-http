#ifndef CORO_CLOUDSTORAGE_WRAP_H
#define CORO_CLOUDSTORAGE_WRAP_H

#include <coro/generator.h>
#include <coro/stdx/concepts.h>
#include <coro/stdx/coroutine.h>
#include <coro/task.h>

#include <memory>

namespace coro::util {

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

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_WRAP_H
