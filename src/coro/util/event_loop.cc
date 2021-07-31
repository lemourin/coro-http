#include "coro/util/event_loop.h"

#include <utility>

namespace coro::util {

EventLoop::WaitTask::WaitTask(event_base *event_loop, int msec,
                              stdx::stop_token stop_token)
    : stop_token_(std::move(stop_token)),
      stop_callback_(stop_token_, OnCancel{this}) {
  if (!interrupted_) {
    timeval tv = {.tv_sec = msec / 1000, .tv_usec = msec % 1000 * 1000};
    event_assign(
        &event_, event_loop, -1, EV_TIMEOUT,
        [](evutil_socket_t, short, void *data) {
          auto *task = reinterpret_cast<WaitTask *>(data);
          if (task->handle_) {
            std::exchange(task->handle_, nullptr).resume();
          }
        },
        this);
    event_add(&event_, &tv);
  }
}

EventLoop::WaitTask::~WaitTask() {
  if (event_.ev_base) {
    event_del(&event_);
  }
}

EventLoop::WaitTask EventLoop::Wait(int msec,
                                    stdx::stop_token stop_token) const {
  return WaitTask(event_loop_, msec, std::move(stop_token));
}

void EventLoop::WaitTask::OnCancel::operator()() const {
  task->interrupted_ = true;
  if (task->event_.ev_base) {
    event_del(&task->event_);
  }
  if (task->handle_) {
    std::exchange(task->handle_, nullptr).resume();
  }
}

}  // namespace coro::util
