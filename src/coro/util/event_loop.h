#ifndef CORO_HTTP_WAIT_TASK_H
#define CORO_HTTP_WAIT_TASK_H

#include <event2/event.h>
#include <event2/event_struct.h>

#include <functional>
#include <future>
#include <stdexcept>

#include "coro/interrupted_exception.h"
#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_token.h"
#include "coro/task.h"

namespace coro::util {

struct EventBaseDeleter {
  void operator()(event_base* event_base) const {
    if (event_base) {
      event_base_free(event_base);
    }
  }
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

    ~WaitTask();

    bool await_ready() {
      return interrupted_ || !event_pending(&event_, EV_TIMEOUT, nullptr);
    }
    void await_suspend(stdx::coroutine_handle<void> handle) {
      handle_ = handle;
    }
    void await_resume() {
      if (interrupted_) {
        throw InterruptedException();
      }
    }

   private:
    struct OnCancel {
      void operator()() const;
      WaitTask* task;
    };

    stdx::coroutine_handle<void> handle_;
    event event_ = {};
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
    F* data = new F(std::move(func));
    if (event_base_once(
            event_loop_, -1, EV_TIMEOUT,
            [](evutil_socket_t, short, void* d) {
              coro::RunTask([func = reinterpret_cast<F*>(d)]() -> Task<> {
                co_await (*func)();
                delete func;
              });
            },
            data, nullptr) != 0) {
      delete data;
      throw std::runtime_error("can't run on event loop");
    }
  }

  template <typename F>
  void RunOnEventLoop(F func) const {
    F* data = new F(std::move(func));
    if (event_base_once(
            event_loop_, -1, EV_TIMEOUT,
            [](evutil_socket_t, short, void* d) {
              auto func = reinterpret_cast<F*>(d);
              (*func)();
              delete func;
            },
            data, nullptr) != 0) {
      delete data;
      throw std::runtime_error("can't run on event loop");
    }
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
          co_await func();
          result.set_value();
        } else {
          result.set_value(co_await func());
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
          func();
          result.set_value();
        } else {
          result.set_value(func());
        }
      } catch (...) {
        result.set_exception(std::current_exception());
      }
    });
    return std::move(result).get_future().get();
  }

 private:
  event_base* event_loop_;
};

class WaitF : public std::function<Task<>(int, stdx::stop_token)> {
 public:
  template <typename F>
  explicit WaitF(const F* f)
      : std::function<Task<>(int, stdx::stop_token)>(
            [f](int ms, stdx::stop_token stop_token) -> Task<> {
              co_await f->Wait(ms, std::move(stop_token));
            }) {}
};

}  // namespace coro::util

#endif  // CORO_HTTP_WAIT_TASK_H
