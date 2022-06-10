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

struct event_base;
struct event;

namespace coro::util {

struct EventBaseDeleter {
  void operator()(event_base* event_base) const;
};

struct EventDeleter {
  void operator()(event*) const;
};

class EventLoop {
 public:
  explicit EventLoop(event_base* event_loop) : event_loop_(event_loop) {}

  class WaitTask {
   public:
    WaitTask(event_base* event_loop, int msec, stdx::stop_token);

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
    std::unique_ptr<event, EventDeleter> event_;
    stdx::stop_token stop_token_;
    bool interrupted_ = false;
    stdx::stop_callback<OnCancel> stop_callback_;
  };

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

 private:
  void RunOnce(stdx::any_invocable<void() &&>) const;

  event_base* event_loop_;
};

}  // namespace coro::util

#endif  // CORO_HTTP_WAIT_TASK_H
