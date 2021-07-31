#include "coro/util/thread_pool.h"

#ifdef WIN32
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
    info.dwThreadID = -1;
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

}  // namespace

void SetThreadName(std::string_view name) {
  SetThreadNameImpl(std::string(name));
}

}  // namespace coro::util
