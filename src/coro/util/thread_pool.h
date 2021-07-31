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

template <typename EventLoop>
class ThreadPool {
 public:
  explicit ThreadPool(
      const EventLoop* event_loop,
      unsigned int thread_count = std::thread::hardware_concurrency())
      : event_loop_(event_loop) {
    for (unsigned int i = 0; i < std::max<unsigned int>(thread_count, 2u);
         i++) {
      threads_.emplace_back([&] { Work(); });
    }
  }

  ~ThreadPool() {
    {
      std::unique_lock lock(mutex_);
      quit_ = true;
      condition_variable_.notify_all();
    }
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool(ThreadPool&) = delete;
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
  void Work() {
    SetThreadName("coro-threadpool");
    while (true) {
      std::unique_lock lock(mutex_);
      condition_variable_.wait(lock, [&] { return !tasks_.empty() || quit_; });
      if (quit_ && tasks_.empty()) {
        break;
      }
      auto coroutine = tasks_.back();
      tasks_.pop_back();
      lock.unlock();
      coroutine.resume();
    }
  }

  Task<> SwitchToThreadLoop() {
    struct Awaiter {
      bool await_ready() const { return false; }
      void await_resume() {}
      void await_suspend(stdx::coroutine_handle<void> continuation) {
        std::unique_lock lock(thread_pool->mutex_);
        thread_pool->tasks_.emplace_back(continuation);
        thread_pool->condition_variable_.notify_one();
      }
      ThreadPool* thread_pool;
    };
    co_await Awaiter{this};
  }

  Task<> SwitchToEventLoop() {
    struct Awaiter {
      bool await_ready() const { return false; }
      void await_resume() {}
      void await_suspend(stdx::coroutine_handle<void> continuation) {
        event_loop->RunOnEventLoop([=]() mutable { continuation.resume(); });
      }
      const EventLoop* event_loop;
    };
    co_await Awaiter{event_loop_};
  }

  std::vector<stdx::coroutine_handle<void>> tasks_;
  std::vector<std::thread> threads_;
  bool quit_ = false;
  std::condition_variable condition_variable_;
  std::mutex mutex_;
  const EventLoop* event_loop_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_THREAD_POOL_H
