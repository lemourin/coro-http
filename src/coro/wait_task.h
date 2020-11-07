#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <coro/task.h>
#include <coro/stop_token.h>
#include <event2/event.h>
#include <event2/event_struct.h>

namespace coro {

class WaitTask {
 public:
  WaitTask(event_base* event_loop, int msec, coro::stop_token&&);

  WaitTask(const WaitTask&) = delete;
  WaitTask(WaitTask&&) = delete;

  WaitTask& operator=(const WaitTask&) = delete;
  WaitTask& operator=(WaitTask&&) = delete;

  ~WaitTask();

  bool await_ready() { return false; }
  void await_suspend(coroutine_handle<void> handle) { handle_ = handle; }
  void await_resume() {}

 private:
  coroutine_handle<void> handle_;
  event event_;
  coro::stop_token stop_token_;
};

WaitTask Wait(event_base* event_loop, int msec,
              coro::stop_token&& = coro::stop_token()) noexcept;

}  // namespace coro

#endif  // CORO_HTTP_WAIT_TASK_H
