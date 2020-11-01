#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <event2/event.h>
#include <event2/event_struct.h>
#include <coro/task.h>

namespace coro {

class WaitTask {
 public:
  WaitTask(event_base *event_loop, int msec);
  ~WaitTask();

  bool await_ready() { return false; }
  void await_suspend(coroutine_handle<void> handle) { handle_ = handle; }
  void await_resume() {}

 private:
  coroutine_handle<void> handle_;
  event event_;
};

WaitTask Wait(event_base *event_loop, int msec) noexcept;

}  // namespace coro

#endif  // CORO_HTTP_WAIT_TASK_H
