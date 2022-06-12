#include "coro/util/event_loop.h"

#include <event2/event.h>
#include <event2/thread.h>

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
  return WaitTask(event_loop_.get(), msec, std::move(stop_token));
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
          event_loop_.get(), -1, EV_TIMEOUT,
          [](evutil_socket_t, short, void *d) {
            std::unique_ptr<F> data(static_cast<F *>(d));
            std::move (*data)();
          },
          data, nullptr) != 0) {
    delete data;
    throw std::runtime_error("can't run on event loop");
  }
}

EventLoop::EventLoop()
    : event_loop_([] {
        static bool init_status = [] {
#ifdef WIN32
          WORD version_requested = MAKEWORD(2, 2);
          WSADATA wsa_data;
          (void)WSAStartup(version_requested, &wsa_data);
          return evthread_use_windows_threads();
#else
          return evthread_use_pthreads();
#endif
        }();
        return event_base_new();
      }()) {
}

EventLoop::~EventLoop() = default;

void EventLoop::EnterLoop(EventLoopType type) {
  if (event_base_loop(event_loop_.get(), [&] {
        switch (type) {
          case EventLoopType::NoExitOnEmpty:
            return EVLOOP_NO_EXIT_ON_EMPTY;
          case EventLoopType::ExitOnEmpty:
            return 0;
        }
      }()) < 0) {
    throw std::runtime_error("event_base_loop error");
  }
}

void EventLoop::ExitLoop() {
  if (event_base_loopexit(event_loop_.get(), nullptr) != 0) {
    throw std::runtime_error("event_base_loopexit error");
  }
}

}  // namespace coro::util
