#ifndef CORO_CLOUDSTORAGE_SEMAPHORE_H
#define CORO_CLOUDSTORAGE_SEMAPHORE_H

#include <coro/stdx/coroutine.h>

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
      continuation_.resume();
    }
  }

 private:
  bool resumed_ = false;
  stdx::coroutine_handle<void> continuation_;
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_SEMAPHORE_H
