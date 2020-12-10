#ifndef CORO_CLOUDSTORAGE_SEMAPHORE_H
#define CORO_CLOUDSTORAGE_SEMAPHORE_H

#include <coro/stdx/coroutine.h>

#include <utility>

namespace coro {

class Semaphore {
 public:
  bool await_ready() { return resumed_; }
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
  stdx::coroutine_handle<void> continuation_;
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_SEMAPHORE_H
