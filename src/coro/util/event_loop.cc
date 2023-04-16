#include "coro/util/event_loop.h"

#include <event2/event.h>
#include <event2/thread.h>

#include <utility>

namespace coro::util {

namespace {

template <typename T>
event_base *ToEventBase(T *d) {
  return reinterpret_cast<struct event_base *>(d);
}

template <typename T>
event *ToEvent(T *d) {
  return reinterpret_cast<struct event *>(d);
}

}  // namespace

void EventLoop::EventBaseDeleter::operator()(EventBase *event_base) const {
  event_base_free(ToEventBase(event_base));
}

void EventLoop::EventDeleter::operator()(Event *e) const {
  event_free(reinterpret_cast<struct event *>(e));
}

bool EventLoop::WaitTask::await_ready() {
  return interrupted_ ||
         !event_pending(ToEvent(event_.get()), EV_TIMEOUT, nullptr);
}

void EventLoop::WaitTask::await_suspend(stdx::coroutine_handle<void> handle) {
  handle_ = handle;
}

void EventLoop::WaitTask::await_resume() {
  if (interrupted_) {
    throw InterruptedException();
  }
}

EventLoop::WaitTask::WaitTask(EventBase *event_loop, int msec,
                              stdx::stop_token stop_token)
    : stop_token_(std::move(stop_token)),
      stop_callback_(stop_token_, OnCancel{this}) {
  if (!interrupted_) {
    timeval tv = {.tv_sec = msec / 1000, .tv_usec = msec % 1000 * 1000};
    event_.reset(reinterpret_cast<Event *>(event_new(
        ToEventBase(event_loop), -1, EV_TIMEOUT,
        [](evutil_socket_t, short, void *data) {
          auto *task = reinterpret_cast<WaitTask *>(data);
          if (task->handle_) {
            std::exchange(task->handle_, nullptr).resume();
          }
        },
        this)));
    event_add(ToEvent(event_.get()), &tv);
  }
}

EventLoop::WaitTask EventLoop::Wait(int msec,
                                    stdx::stop_token stop_token) const {
  return WaitTask(event_loop_.get(), msec, std::move(stop_token));
}

void EventLoop::WaitTask::OnCancel::operator()() const {
  task->interrupted_ = true;
  if (task->event_) {
    event_del(ToEvent(task->event_.get()));
  }
  if (task->handle_) {
    std::exchange(task->handle_, nullptr).resume();
  }
}

void EventLoop::RunOnce(stdx::any_invocable<void() &&> f) const {
  using F = stdx::any_invocable<void() &&>;

  auto *data = new F(std::move(f));
  if (event_base_once(
          ToEventBase(event_loop_.get()), -1, EV_TIMEOUT,
          [](evutil_socket_t, short, void *d) {
            std::unique_ptr<F> data(static_cast<F *>(d));
            std::move (*data)();
          },
          data, nullptr) != 0) {
    delete data;
    throw RuntimeError("can't run on event loop");
  }
}

EventLoop::EventLoop()
    : event_loop_([] {
#ifdef WIN32
        WORD version_requested = MAKEWORD(2, 2);
        WSADATA wsa_data;
        if (WSAStartup(version_requested, &wsa_data) != 0) {
          throw RuntimeError("WSAStartup error");
        }
        if (evthread_use_windows_threads() != 0) {
          throw RuntimeError("evthread_use_windows_threads error");
        }
#else
        if (evthread_use_pthreads() != 0) {
          throw RuntimeError("evthread_use_pthreads error");
        }
#endif
        event_base *event_base = event_base_new();
        if (!event_base) {
          throw RuntimeError("event_base_new error");
        }
        return reinterpret_cast<EventBase *>(event_base);
      }()) {
}

#ifdef WIN32
EventLoop::~EventLoop() noexcept {
  if (WSACleanup() != 0) {
    std::terminate();
  }
}
#else
EventLoop::~EventLoop() noexcept = default;
#endif

void EventLoop::EnterLoop(EventLoopType type) {
  if (event_base_loop(ToEventBase(event_loop_.get()), [&] {
        switch (type) {
          case EventLoopType::NoExitOnEmpty:
            return EVLOOP_NO_EXIT_ON_EMPTY;
          case EventLoopType::ExitOnEmpty:
            return 0;
        }
      }()) < 0) {
    throw RuntimeError("event_base_loop error");
  }
}

void EventLoop::ExitLoop() {
  if (event_base_loopexit(ToEventBase(event_loop_.get()), nullptr) != 0) {
    throw RuntimeError("event_base_loopexit error");
  }
}

}  // namespace coro::util
