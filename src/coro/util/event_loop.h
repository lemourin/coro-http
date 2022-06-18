#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <functional>
#include <future>
#include <stdexcept>

#include "coro/interrupted_exception.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_token.h"
#include "coro/task.h"

namespace coro::util {

enum class EventLoopType {
  ExitOnEmpty,
  NoExitOnEmpty,
};

class EventLoop {
 public:
  class WaitTask;

  EventLoop();
  ~EventLoop();

  WaitTask Wait(int msec, stdx::stop_token = stdx::stop_token()) const;

  template <typename F>
  requires requires(F func) {
    { func() } -> Awaitable<void>;
  }
  void RunOnEventLoop(F func) const {
    RunOnce([func = std::move(func)]() mutable {
      coro::RunTask([func = std::move(func)]() mutable -> Task<> {
        co_await std::move(func)();
      });
    });
  }

  template <typename F>
  void RunOnEventLoop(F func) const {
    RunOnce([func = std::move(func)]() mutable { std::move(func)(); });
  }

  template <typename F,
            typename ResultType = typename decltype(std::declval<F>()())::type>
  requires requires(F func) {
    { func() } -> Awaitable<ResultType>;
  }
  ResultType Do(F func) const {
    std::promise<ResultType> result;
    RunOnEventLoop([&result, &func]() -> Task<> {
      try {
        if constexpr (std::is_same_v<ResultType, void>) {
          co_await std::move(func)();
          result.set_value();
        } else {
          result.set_value(co_await std::move(func)());
        }
      } catch (...) {
        result.set_exception(std::current_exception());
      }
    });
    return std::move(result).get_future().get();
  }

  template <typename F, typename ResultType = decltype(std::declval<F>()())>
  ResultType Do(F func) const {
    std::promise<ResultType> result;
    RunOnEventLoop([&result, &func] {
      try {
        if constexpr (std::is_same_v<ResultType, void>) {
          std::move(func)();
          result.set_value();
        } else {
          result.set_value(std::move(func)());
        }
      } catch (...) {
        result.set_exception(std::current_exception());
      }
    });
    return std::move(result).get_future().get();
  }

  void EnterLoop(EventLoopType = EventLoopType::ExitOnEmpty);

  void ExitLoop();

 private:
  struct EventBase;
  struct Event;

  struct EventBaseDeleter {
    void operator()(EventBase* event_base) const;
  };

  struct EventDeleter {
    void operator()(Event*) const;
  };

  friend EventBase* GetEventLoop(const EventLoop& e) {
    return e.event_loop_.get();
  }

  void RunOnce(stdx::any_invocable<void() &&>) const;

  std::unique_ptr<EventBase, EventBaseDeleter> event_loop_;
};

class EventLoop::WaitTask {
 public:
  WaitTask(EventBase* event_loop, int msec, stdx::stop_token);

  WaitTask(const WaitTask&) = delete;
  WaitTask(WaitTask&&) = delete;

  WaitTask& operator=(const WaitTask&) = delete;
  WaitTask& operator=(WaitTask&&) = delete;

  bool await_ready();
  void await_suspend(stdx::coroutine_handle<void> handle);
  void await_resume();

 private:
  struct OnCancel {
    void operator()() const;
    WaitTask* task;
  };

  stdx::coroutine_handle<void> handle_;
  std::unique_ptr<Event, EventDeleter> event_;
  stdx::stop_token stop_token_;
  bool interrupted_ = false;
  stdx::stop_callback<OnCancel> stop_callback_;
};

}  // namespace coro::util

#endif  // CORO_HTTP_WAIT_TASK_H
