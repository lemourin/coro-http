#ifndef CORO_UTIL_THREAD_POOL_H
#define CORO_UTIL_THREAD_POOL_H

#include <coro/promise.h>
#include <coro/stdx/any_invocable.h>
#include <coro/task.h>
#include <coro/util/event_loop.h>
#include <coro/util/function_traits.h>

#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace coro::util {

class ThreadPool {
 public:
  explicit ThreadPool(
      const EventLoop& event_loop,
      unsigned int thread_count = std::thread::hardware_concurrency());
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  template <typename T>
  using TaskT = std::conditional_t<std::is_void_v<T>, Task<>, Task<T>>;

  template <typename Func, typename... Args>
  TaskT<util::ReturnTypeT<Func>> Invoke(Func func, Args&&... args) {
    Promise<util::ReturnTypeT<Func>> result;
    Schedule([func = std::move(func), ... args = std::forward<Args>(args),
              &result, event_loop = event_loop_]() mutable {
      try {
        if constexpr (std::is_void_v<util::ReturnTypeT<Func>>) {
          func(std::forward<Args>(args)...);
          event_loop->RunOnEventLoop([&] { result.SetValue(); });
        } else {
          event_loop->RunOnEventLoop(
              [&result, r = func(std::forward<Args>(args)...)]() mutable {
                result.SetValue(std::move(r));
              });
        }
      } catch (...) {
        event_loop->RunOnEventLoop(
            [&result, exception = std::current_exception()]() mutable {
              result.SetException(std::move(exception));
            });
      }
    });
    co_return co_await result;
  }

 private:
  void Work();
  void Schedule(stdx::any_invocable<void()> task);

  std::vector<stdx::any_invocable<void()>> tasks_;
  std::vector<std::thread> threads_;
  bool quit_ = false;
  std::condition_variable condition_variable_;
  std::mutex mutex_;
  const EventLoop* event_loop_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_THREAD_POOL_H
