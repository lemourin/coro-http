#include "thread_pool.h"

namespace coro::util {

ThreadPool::ThreadPool(const EventLoop& event_loop, unsigned int thread_count)
    : event_loop_(&event_loop) {
  for (unsigned int i = 0; i < std::max<unsigned int>(thread_count, 1u); i++) {
    threads_.emplace_back([this] { Work(); });
  }
}

ThreadPool::~ThreadPool() {
  {
    std::unique_lock lock(mutex_);
    quit_ = true;
    condition_variable_.notify_all();
  }
  for (auto& thread : threads_) {
    thread.join();
  }
}

void ThreadPool::Work() {
  while (true) {
    std::unique_lock lock(mutex_);
    condition_variable_.wait(lock, [&] { return !tasks_.empty() || quit_; });
    if (quit_ && tasks_.empty()) {
      break;
    }
    auto task = std::move(tasks_.back());
    tasks_.pop_back();
    lock.unlock();
    std::move(task)();
  }
}

void ThreadPool::Schedule(stdx::any_invocable<void()> task) {
  std::unique_lock lock(mutex_);
  tasks_.emplace_back(std::move(task));
  condition_variable_.notify_one();
}

}  // namespace coro::util