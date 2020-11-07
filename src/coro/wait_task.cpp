#include "wait_task.h"

#include <coro/stop_token.h>

namespace coro {

WaitTask::WaitTask(event_base *event_loop, int msec,
                   coro::stop_token &&stop_token)
    : stop_token_(std::move(stop_token)) {
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

WaitTask::~WaitTask() { event_del(&event_); }

WaitTask Wait(event_base *event_loop, int msec,
              coro::stop_token &&stop_token) noexcept {
  return WaitTask(event_loop, msec, std::move(stop_token));
}

}  // namespace coro