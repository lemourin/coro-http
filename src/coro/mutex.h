#ifndef CORO_MUTEX_H
#define CORO_MUTEX_H

#include <vector>

#include "coro/promise.h"
#include "coro/task.h"

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
  explicit UniqueLock(Mutex*);

  Mutex* mutex_;
};

class ReadWriteMutex {
 public:
  ReadWriteMutex();
  ReadWriteMutex(const ReadWriteMutex&) = delete;
  ReadWriteMutex(ReadWriteMutex&&) = delete;
  ReadWriteMutex& operator=(const ReadWriteMutex&) = delete;
  ReadWriteMutex& operator=(ReadWriteMutex&&) = delete;

  Task<> ReadLock();
  void ReadUnlock();

  Task<> WriteLock();
  void WriteUnlock();

 private:
  int reader_count_;
  std::vector<Promise<void>*> queued_readers_;
  int writer_count_;
  std::vector<Promise<void>*> queued_writers_;
};

class ReadLock {
 public:
  ReadLock(const ReadLock&) = delete;
  ReadLock(ReadLock&&) noexcept;
  ReadLock& operator=(const ReadLock&) = delete;
  ReadLock& operator=(ReadLock&&) noexcept;

  ~ReadLock();

  static Task<ReadLock> Create(ReadWriteMutex*);

 private:
  explicit ReadLock(ReadWriteMutex* mutex);

  ReadWriteMutex* mutex_;
};

class WriteLock {
 public:
  WriteLock(const WriteLock&) = delete;
  WriteLock(WriteLock&&) noexcept;
  WriteLock& operator=(const WriteLock&) = delete;
  WriteLock& operator=(WriteLock&&) noexcept;

  ~WriteLock();

  static Task<WriteLock> Create(ReadWriteMutex*);

 private:
  explicit WriteLock(ReadWriteMutex* mutex);

  ReadWriteMutex* mutex_;
};

}  // namespace coro

#endif