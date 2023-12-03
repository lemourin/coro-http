#include "coro/util/thread_pool.h"

#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__linux__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#elif defined(__APPLE__)
#include <pthread.h>
#endif

#include <string>

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
    info.dwThreadID = static_cast<DWORD>(-1);
    info.dwFlags = 0;
#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try {
      RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR),
                     reinterpret_cast<ULONG_PTR*>(&info));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#pragma warning(pop)
  }
#endif
#ifdef __linux__
  pthread_setname_np(pthread_self(), name.c_str());
#endif
#ifdef __APPLE__
  pthread_setname_np(name.c_str());
#endif
}

int GetDigitCount(unsigned int d) {
  int result = 0;
  while (d > 0) {
    d /= 10;
    result++;
  }
  return result;
}

std::string PadValue(unsigned int d, int cnt) {
  std::string result(cnt, '0');
  int index = cnt - 1;
  while (d > 0) {
    result[index--] = static_cast<char>((d % 10) + '0');
    d /= 10;
  }
  return result;
}

}  // namespace

void SetThreadName(std::string_view name) {
  SetThreadNameImpl(std::string(name));
}

ThreadPool::ThreadPool(const EventLoop* event_loop, unsigned int thread_count,
                       std::string name)
    : event_loop_(event_loop), name_(std::move(name)) {
  const int cnt = GetDigitCount(thread_count);
  for (unsigned int i = 0; i < thread_count; i++) {
    threads_.emplace_back([&, i, cnt] {
      SetThreadName(name_ + "-" + PadValue(i, cnt));
      Work();
    });
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
    auto coroutine = tasks_.back();
    tasks_.pop_back();
    lock.unlock();
    coroutine.resume();
  }
}

Task<> ThreadPool::SwitchToEventLoop() {
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

}  // namespace coro::util
