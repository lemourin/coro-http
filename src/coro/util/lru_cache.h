#ifndef CORO_CLOUDSTORAGE_LRU_CACHE_H
#define CORO_CLOUDSTORAGE_LRU_CACHE_H

#include <set>
#include <unordered_map>
#include <utility>

#include "coro/shared_promise.h"
#include "coro/stdx/stop_source.h"
#include "coro/stdx/stop_token.h"
#include "coro/task.h"
#include "coro/util/raii_utils.h"

namespace coro::util {

template <typename Key, typename Factory, typename Hash = std::hash<Key>>
class LRUCache {
 public:
  using Value = typename decltype(std::declval<Factory>()(
      std::declval<Key>(), std::declval<stdx::stop_token>()))::type;

  LRUCache(int size, Factory factory)
      : size_(size), factory_(std::move(factory)), queue_(Compare{this}) {}

  LRUCache(LRUCache&& other) noexcept
      : size_(other.size_),
        factory_(std::move(other.factory_)),
        time_(other.time_),
        map_(std::move(other.map_)),
        pending_(std::move(other.pending_)),
        last_access_(std::move(other.last_access_)),
        queue_(other.queue_.begin(), other.queue_.end(), Compare{this}),
        stop_source_(std::move(other.stop_source_)) {}

  LRUCache& operator=(LRUCache&& other) noexcept {
    size_ = other.size_;
    factory_ = std::move(other.factory_);
    time_ = other.time_;
    map_ = std::move(other.map_);
    pending_ = std::move(other.pending_);
    last_access_ = std::move(other.last_access_);
    queue_ = std::set<Key, Compare>(other.queue_.begin(), other.queue_.end(),
                                    Compare{this});
    stop_source_ = std::move(other.stop_source_);
    return *this;
  }

  ~LRUCache() { stop_source_.request_stop(); }

  Task<Value> Get(Key key, stdx::stop_token stop_token) {
    RegisterAccess(key);
    if (auto result = GetCached(key)) {
      co_return std::move(*result);
    }
    auto promise_it = pending_.find(key);
    if (promise_it != std::end(pending_)) {
      co_return co_await promise_it->second.Get(std::move(stop_token));
    }
    promise_it =
        pending_
            .emplace(std::piecewise_construct, std::forward_as_tuple(key),
                     std::forward_as_tuple(
                         ProduceValue{.d = this,
                                      .key = key,
                                      .stop_token = stop_source_.get_token()}))
            .first;
    auto guard = AtScopeExit([&] { pending_.erase(key); });
    co_return co_await promise_it->second.Get(std::move(stop_token));
  }

  void Invalidate(const Key& key) {
    last_access_.erase(key);
    map_.erase(key);
    queue_.erase(key);
  }

  std::optional<Value> GetCached(const Key& key) const {
    auto it = map_.find(key);
    if (it == map_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

 private:
  void Insert(Key key, Value value) {
    while (map_.size() >= static_cast<size_t>(size_)) {
      Invalidate(*queue_.begin());
    }
    map_.emplace(std::move(key), std::move(value));
  }

  void RegisterAccess(Key key) {
    queue_.erase(key);
    last_access_[key] = time_++;
    queue_.emplace(std::move(key));
  }

  struct ProduceValue {
    Task<Value> operator()() && {
      auto result = co_await d->factory_(key, std::move(stop_token));
      d->Insert(key, result);
      co_return result;
    }
    LRUCache* d;
    Key key;
    stdx::stop_token stop_token;
  };

  struct Compare {
    bool operator()(const Key& k1, const Key& k2) const {
      return d->last_access_[k1] < d->last_access_[k2];
    }
    LRUCache* d;
  };

  int size_;
  Factory factory_;
  int time_ = 0;
  std::unordered_map<Key, Value, Hash> map_;
  std::unordered_map<Key, SharedPromise<ProduceValue>, Hash> pending_;
  std::unordered_map<Key, int, Hash> last_access_;
  std::set<Key, Compare> queue_;
  stdx::stop_source stop_source_;
};

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_LRU_CACHE_H
