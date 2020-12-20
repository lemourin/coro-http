#include "wait_task.h"

namespace coro {

WaitTask::WaitTask(event_base *event_loop, int msec,
                   stdx::stop_token stop_token)
    : stop_token_(std::move(stop_token)),
      stop_callback_(stop_token_, OnCancel{this}) {
  if (!interrupted_) {
    timeval tv = {.tv_sec = msec / 1000, .tv_usec = msec % 1000 * 1000};
    event_assign(
        &event_, event_loop, -1, EV_TIMEOUT,
        [](evutil_socket_t, short, void *data) {
          auto task = reinterpret_cast<WaitTask *>(data);
          if (task->handle_) {
            std::exchange(task->handle_, nullptr).resume();
          }
        },
        this);
    event_add(&event_, &tv);
  }
}

WaitTask::~WaitTask() {
  if (event_.ev_base) {
    event_del(&event_);
  }
}

WaitTask Wait(event_base *event_loop, int msec,
              stdx::stop_token stop_token) noexcept {
  return WaitTask(event_loop, msec, std::move(stop_token));
}

void WaitTask::OnCancel::operator()() const {
  task->interrupted_ = true;
  if (task->event_.ev_base) {
    event_del(&task->event_);
  }
  if (task->handle_) {
    std::exchange(task->handle_, nullptr).resume();
  }
}

}  // namespace coro