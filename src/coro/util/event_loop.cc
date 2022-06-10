#include "coro/util/event_loop.h"

#include <event2/event.h>

#include <utility>

namespace coro::util {

void EventBaseDeleter::operator()(event_base *event_base) const {
  if (event_base) {
    event_base_free(event_base);
  }
}

void EventDeleter::operator()(event *e) const {
  if (e) {
    event_free(e);
  }
}

bool EventLoop::WaitTask::await_ready() {
  return interrupted_ || !event_pending(event_.get(), EV_TIMEOUT, nullptr);
}

void EventLoop::WaitTask::await_suspend(stdx::coroutine_handle<void> handle) {
  handle_ = handle;
}

void EventLoop::WaitTask::await_resume() {
  if (interrupted_) {
    throw InterruptedException();
  }
}

EventLoop::WaitTask::WaitTask(event_base *event_loop, int msec,
                              stdx::stop_token stop_token)
    : stop_token_(std::move(stop_token)),
      stop_callback_(stop_token_, OnCancel{this}) {
  if (!interrupted_) {
    timeval tv = {.tv_sec = msec / 1000, .tv_usec = msec % 1000 * 1000};
    event_.reset(event_new(
        event_loop, -1, EV_TIMEOUT,
        [](evutil_socket_t, short, void *data) {
          auto *task = reinterpret_cast<WaitTask *>(data);
          if (task->handle_) {
            std::exchange(task->handle_, nullptr).resume();
          }
        },
        this));
    event_add(event_.get(), &tv);
  }
}

EventLoop::WaitTask EventLoop::Wait(int msec,
                                    stdx::stop_token stop_token) const {
  return WaitTask(event_loop_, msec, std::move(stop_token));
}

void EventLoop::WaitTask::OnCancel::operator()() const {
  task->interrupted_ = true;
  if (task->event_) {
    event_del(task->event_.get());
  }
  if (task->handle_) {
    std::exchange(task->handle_, nullptr).resume();
  }
}

void EventLoop::RunOnce(stdx::any_invocable<void() &&> f) const {
  using F = stdx::any_invocable<void() &&>;

  auto *data = new F(std::move(f));
  if (event_base_once(
          event_loop_, -1, EV_TIMEOUT,
          [](evutil_socket_t, short, void *d) {
            std::unique_ptr<F> data(static_cast<F *>(d));
            std::move (*data)();
          },
          data, nullptr) != 0) {
    delete data;
    throw std::runtime_error("can't run on event loop");
  }
}

}  // namespace coro::util
