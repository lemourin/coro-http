#include "wait_task.h"

namespace coro {

WaitTask::WaitTask(event_base *event_loop, int msec) {
  timeval tv = {.tv_sec = msec / 1000, .tv_usec = msec % 1000 * 1000};
  event_assign(
      &event_, event_loop, -1, EV_TIMEOUT,
      [](evutil_socket_t, short, void *data) {
        auto task = reinterpret_cast<WaitTask *>(data);
        if (task->handle_) {
          task->handle_.resume();
        }
      },
      this);
  event_add(&event_, &tv);
}

WaitTask::~WaitTask() {
  event_del(&event_);
}

WaitTask Wait(event_base *event_loop, int msec) noexcept {
  return WaitTask(event_loop, msec);
}

}  // namespace coro