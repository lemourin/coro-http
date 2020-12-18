#ifndef CORO_CLOUDSTORAGE_SEMAPHORE_H
#define CORO_CLOUDSTORAGE_SEMAPHORE_H

#include <coro/stdx/coroutine.h>

#include <utility>

namespace coro {

class Semaphore {
 public:
  Semaphore() = default;
  Semaphore(const Semaphore&) = delete;
  Semaphore(Semaphore&& semaphore)
      : resumed_(semaphore.resumed_),
        continuation_(std::exchange(semaphore.continuation_, nullptr)) {}

  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore& operator=(Semaphore&& semaphore) {
    resumed_ = semaphore.resumed_;
    continuation_ = std::exchange(semaphore.continuation_, nullptr);
    return *this;
  }

  bool await_ready() const { return resumed_; }
  void await_suspend(stdx::coroutine_handle<void> continuation) {
    continuation_ = continuation;
  }
  void await_resume() {}
  void resume() {
    resumed_ = true;
    if (continuation_) {
      std::exchange(continuation_, nullptr).resume();
    }
  }

 private:
  bool resumed_ = false;
  stdx::coroutine_handle<void> continuation_ = coro::std_ns::noop_coroutine();
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_SEMAPHORE_H
