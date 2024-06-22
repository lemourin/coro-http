#ifndef CORO_UTIL_THREAD_POOL_H
#define CORO_UTIL_THREAD_POOL_H

#include <algorithm>
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
      unsigned int thread_count = std::thread::hardware_concurrency(),
      std::string name = "coro-tpool");
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;
  ThreadPool& operator=(ThreadPool&&) = delete;

  template <typename T>
  using TaskT = std::conditional_t<std::is_void_v<T>, Task<>, Task<T>>;

  template <typename Func, typename... Args>
  TaskT<util::ReturnTypeT<Func>> Do(stdx::stop_token stop_token, Func&& func,
                                    Args&&... args) {
    ThreadLoopAwaiter awaiter{this, stdx::coroutine_handle<void>(), false};
    stdx::stop_callback cb(std::move(stop_token), [&] {
      std::unique_lock lock(mutex_);
      if (auto it =
              std::find(tasks_.begin(), tasks_.end(), awaiter.continuation);
          it != tasks_.end()) {
        tasks_.erase(it);
        lock.unlock();
        awaiter.interrupted = true;
        awaiter.continuation.resume();
      }
    });
    co_await awaiter;
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

  template <typename... Args>
  auto Do(Args&&... args) {
    return Do(stdx::stop_token(), std::forward<Args>(args)...);
  }

 private:
  void Work();
  Task<> SwitchToEventLoop();

  struct ThreadLoopAwaiter {
    static bool await_ready() { return false; }
    void await_resume() const {
      if (interrupted) {
        throw InterruptedException();
      }
    }
    void await_suspend(stdx::coroutine_handle<void> handle) {
      std::unique_lock lock(thread_pool->mutex_);
      continuation = handle;
      thread_pool->tasks_.emplace_back(handle);
      thread_pool->condition_variable_.notify_one();
    }
    ThreadPool* thread_pool;
    stdx::coroutine_handle<void> continuation;
    bool interrupted;
  };

  std::vector<stdx::coroutine_handle<void>> tasks_;
  std::vector<std::thread> threads_;
  bool quit_ = false;
  std::condition_variable condition_variable_;
  std::mutex mutex_;
  const EventLoop* event_loop_;
  std::string name_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_THREAD_POOL_H
