#include "thread_pool.h"

#ifdef WIN32
#include <windows.h>
#endif

#ifdef __linux__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#endif

namespace coro::util {

namespace {

void SetThreadNameImpl(const std::string& name) {
#ifdef _MSC_VER
  const DWORD MS_VC_EXCEPTION = 0x406D1388;
  if (IsDebuggerPresent()) {
#pragma pack(push, 8)
    typedef struct tagTHREADNAME_INFO {
      DWORD dwType;      // Must be 0x1000.
      LPCSTR szName;     // Pointer to name (in user addr space).
      DWORD dwThreadID;  // Thread ID (-1=caller thread).
      DWORD dwFlags;     // Reserved for future use, must be zero.
    } THREADNAME_INFO;
#pragma pack(pop)
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = name.c_str();
    info.dwThreadID = -1;
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try {
      RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                     (ULONG_PTR*)&info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#pragma warning(pop)
  }
#endif
#ifdef __linux__
  pthread_setname_np(pthread_self(), name.c_str());
#endif
}

}  // namespace

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
  SetThreadName("coro-threadpool");
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

void SetThreadName(std::string_view name) {
  SetThreadNameImpl(std::string(name));
}

}  // namespace coro::util