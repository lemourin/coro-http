#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <coro/interrupted_exception.h>
#include <coro/stdx/stop_callback.h>
#include <coro/stdx/stop_token.h>
#include <coro/task.h>
#include <event2/event.h>
#include <event2/event_struct.h>

namespace coro::util {

class EventLoop {
 public:
  explicit EventLoop(event_base* event_loop) : event_loop_(event_loop) {}

  class WaitTask {
   public:
    WaitTask(event_base* event_loop, int msec, stdx::stop_token);

    WaitTask(const WaitTask&) = delete;
    WaitTask(WaitTask&&) = delete;

    WaitTask& operator=(const WaitTask&) = delete;
    WaitTask& operator=(WaitTask&&) = delete;

    ~WaitTask();

    bool await_ready() {
      return interrupted_ || !event_pending(&event_, EV_TIMEOUT, nullptr);
    }
    void await_suspend(stdx::coroutine_handle<void> handle) {
      handle_ = handle;
    }
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

  WaitTask Wait(int msec, stdx::stop_token = stdx::stop_token()) const;

 private:
  event_base* event_loop_;
};

}  // namespace coro::util

#endif  // CORO_HTTP_WAIT_TASK_H
