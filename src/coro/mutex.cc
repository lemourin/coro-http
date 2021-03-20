#include "mutex.h"

#include <coro/util/raii_utils.h>

#include <algorithm>

namespace coro {

Mutex::Mutex() : locked_() {}

Task<> Mutex::Lock() {
  if (locked_) {
    Promise<void> promise;
    queued_.emplace_back(&promise);
    auto guard = util::AtScopeExit([&] {
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

UniqueLock::~UniqueLock() {
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

}  // namespace coro