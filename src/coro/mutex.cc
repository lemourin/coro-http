#include "coro/mutex.h"

#include <algorithm>

#include "coro/util/raii_utils.h"

namespace coro {

using ::coro::util::AtScopeExit;

Mutex::Mutex() : locked_() {}

Task<> Mutex::Lock() {
  if (locked_) {
    Promise<void> promise;
    queued_.emplace_back(&promise);
    auto guard = AtScopeExit([&] {
      queued_.erase(std::find(queued_.begin(), queued_.end(), &promise));
    });
    co_await promise;
  }
  locked_ = true;
}

void Mutex::Unlock() {
  locked_ = false;
  if (!queued_.empty()) {
    queued_.front()->SetValue();
  }
}

UniqueLock::UniqueLock(Mutex* mutex) : mutex_(mutex) {}

UniqueLock::UniqueLock(UniqueLock&& other) noexcept
    : mutex_(std::exchange(other.mutex_, nullptr)) {}

UniqueLock::~UniqueLock() noexcept {
  if (mutex_) {
    mutex_->Unlock();
  }
}

UniqueLock& UniqueLock::operator=(UniqueLock&& other) noexcept {
  mutex_ = std::exchange(other.mutex_, nullptr);
  return *this;
}

Task<UniqueLock> UniqueLock::Create(Mutex* mutex) {
  co_await mutex->Lock();
  co_return UniqueLock(mutex);
}

ReadWriteMutex::ReadWriteMutex() : reader_count_(), writer_count_() {}

Task<> ReadWriteMutex::ReadLock() {
  if (writer_count_ > 0) {
    Promise<void> promise;
    queued_readers_.emplace_back(&promise);
    auto guard = AtScopeExit([&] {
      queued_readers_.erase(
          std::find(queued_readers_.begin(), queued_readers_.end(), &promise));
    });
    co_await promise;
  }
  reader_count_++;
}

void ReadWriteMutex::ReadUnlock() {
  reader_count_--;
  if (reader_count_ == 0 && !queued_writers_.empty()) {
    queued_writers_.front()->SetValue();
  }
}

Task<> ReadWriteMutex::WriteLock() {
  if (reader_count_ > 0) {
    Promise<void> promise;
    queued_writers_.emplace_back(&promise);
    auto guard = AtScopeExit([&] {
      queued_writers_.erase(
          std::find(queued_writers_.begin(), queued_writers_.end(), &promise));
    });
    co_await promise;
  }
  writer_count_++;
}

void ReadWriteMutex::WriteUnlock() {
  writer_count_--;
  if (writer_count_ == 0) {
    if (!queued_writers_.empty()) {
      queued_writers_.front()->SetValue();
    } else {
      while (writer_count_ == 0 && !queued_readers_.empty()) {
        queued_readers_.front()->SetValue();
      }
    }
  }
}

ReadLock::ReadLock(ReadWriteMutex* mutex) : mutex_(mutex) {}

ReadLock::ReadLock(ReadLock&& other) noexcept
    : mutex_(std::exchange(other.mutex_, nullptr)) {}

ReadLock& ReadLock::operator=(ReadLock&& other) noexcept {
  mutex_ = std::exchange(other.mutex_, nullptr);
  return *this;
}

Task<ReadLock> ReadLock::Create(ReadWriteMutex* mutex) {
  co_await mutex->ReadLock();
  co_return ReadLock(mutex);
}

ReadLock::~ReadLock() noexcept {
  if (mutex_) {
    mutex_->ReadUnlock();
  }
}

WriteLock::WriteLock(WriteLock&& other) noexcept
    : mutex_(std::exchange(other.mutex_, nullptr)) {}

WriteLock& WriteLock::operator=(WriteLock&& other) noexcept {
  mutex_ = std::exchange(other.mutex_, nullptr);
  return *this;
}

WriteLock::~WriteLock() noexcept {
  if (mutex_) {
    mutex_->WriteUnlock();
  }
}

Task<WriteLock> WriteLock::Create(ReadWriteMutex* mutex) {
  co_await mutex->WriteLock();
  co_return WriteLock(mutex);
}

WriteLock::WriteLock(ReadWriteMutex* mutex) : mutex_(mutex) {}

}  // namespace coro
