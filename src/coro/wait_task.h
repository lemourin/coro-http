#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <coro/stdx/stop_callback.h>
#include <coro/stdx/stop_token.h>
#include <coro/task.h>
#include <event2/event.h>
#include <event2/event_struct.h>

namespace coro {

class InterruptedException : public std::exception {
 public:
  [[nodiscard]] const char* what() const noexcept override {
    return "interrupted";
  }
};

class WaitTask {
 public:
  WaitTask(event_base* event_loop, int msec, stdx::stop_token);

  WaitTask(const WaitTask&) = delete;
  WaitTask(WaitTask&&) = delete;

  WaitTask& operator=(const WaitTask&) = delete;
  WaitTask& operator=(WaitTask&&) = delete;

  ~WaitTask();

  bool await_ready() { return !event_pending(&event_, EV_TIMEOUT, nullptr); }
  void await_suspend(stdx::coroutine_handle<void> handle) { handle_ = handle; }
  void await_resume() {
    if (interrupted_) {
      throw InterruptedException();
    }
  }

 private:
  struct OnCancel {
    void operator()() const;
    WaitTask* task;
  };

  stdx::coroutine_handle<void> handle_;
  event event_ = {};
  stdx::stop_token stop_token_;
  bool interrupted_ = false;
  stdx::stop_callback<OnCancel> stop_callback_;
};

WaitTask Wait(event_base* event_loop, int msec,
              stdx::stop_token = stdx::stop_token()) noexcept;

}  // namespace coro

#endif  // CORO_HTTP_WAIT_TASK_H
