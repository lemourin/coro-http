#ifndef CORO_MUTEX_H
#define CORO_MUTEX_H

#include <coro/promise.h>
#include <coro/task.h>

#include <vector>

namespace coro {

class Mutex {
 public:
  Mutex();
  Mutex(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex& operator=(Mutex&&) = delete;

  Task<> Lock();
  void Unlock();

 private:
  bool locked_;
  std::vector<Promise<void>*> queued_;
};

class UniqueLock {
 public:
  UniqueLock(const UniqueLock&) = delete;
  UniqueLock(UniqueLock&&) noexcept;
  UniqueLock& operator=(const UniqueLock&) = delete;
  UniqueLock& operator=(UniqueLock&&) noexcept;

  ~UniqueLock();

  static Task<UniqueLock> Create(Mutex*);

 private:
  UniqueLock(Mutex*);

  Mutex* mutex_;
};

}  // namespace coro

#endif