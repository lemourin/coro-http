#ifndef CORO_UTIL_THREAD_POOL_H
#define CORO_UTIL_THREAD_POOL_H

#include <condition_variable>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#include "coro/promise.h"
#include "coro/task.h"
#include "coro/util/event_loop.h"
#include "coro/util/function_traits.h"

namespace coro::util {

void SetThreadName(std::string_view thread_name);

class ThreadPool {
 public:
  explicit ThreadPool(
      const EventLoop* event_loop,
      unsigned int thread_count = std::thread::hardware_concurrency());
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  template <typename T>
  using TaskT = std::conditional_t<std::is_void_v<T>, Task<>, Task<T>>;

  template <typename Func, typename... Args>
  TaskT<util::ReturnTypeT<Func>> Do(Func&& func, Args&&... args) {
    co_await SwitchToThreadLoop();
    std::exception_ptr exception;
    try {
      if constexpr (std::is_void_v<util::ReturnTypeT<Func>>) {
        std::forward<Func>(func)(std::forward<Args>(args)...);
        co_await SwitchToEventLoop();
        co_return;
      } else {
        auto result = std::forward<Func>(func)(std::forward<Args>(args)...);
        co_await SwitchToEventLoop();
        co_return result;
      }
    } catch (...) {
      exception = std::current_exception();
    }
    co_await SwitchToEventLoop();
    std::rethrow_exception(exception);
  }

 private:
  void Work() ;
  Task<> SwitchToThreadLoop();
  Task<> SwitchToEventLoop();

  std::vector<stdx::coroutine_handle<void>> tasks_;
  std::vector<std::thread> threads_;
  bool quit_ = false;
  std::condition_variable condition_variable_;
  std::mutex mutex_;
  const EventLoop* event_loop_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_THREAD_POOL_H
