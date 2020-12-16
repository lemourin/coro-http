#ifndef CORO_CLOUDSTORAGE_LRU_CACHE_H
#define CORO_CLOUDSTORAGE_LRU_CACHE_H

#include <coro/promise.h>
#include <coro/stdx/stop_source.h>
#include <coro/stdx/stop_token.h>
#include <coro/task.h>

#include <unordered_map>
#include <utility>

namespace coro::util {

template <typename Key, typename Factory>
class LRUCache {
 public:
  using Value =
      decltype(std::declval<Factory>()(std::declval<Key>(),
                                       std::declval<stdx::stop_token>())
                   .await_resume());

  LRUCache(int size, Factory factory)
      : d_(std::make_unique<Data>(size, std::move(factory))) {}

  Task<Value> Get(Key key,
                  stdx::stop_token stop_token = stdx::stop_token()) const {
    return d_->Get(std::move(key), std::move(stop_token));
  }

 private:
  class Data {
   public:
    Data(int size, Factory factory)
        : size_(size), factory_(std::move(factory)), queue_(Compare{this}) {}

    ~Data() { stop_source_.request_stop(); }

    void HandlePendingCleanupQueue() {
      while (!pending_cleanup_queue_.empty()) {
        pending_.erase(pending_cleanup_queue_.back());
        pending_cleanup_queue_.pop_back();
      }
    }

    void Insert(const Key& key, Value value) {
      while (map_.size() >= size_) {
        auto to_erase = std::move(*queue_.begin());
        queue_.erase(queue_.begin());
        last_access_.erase(to_erase);
        map_.erase(to_erase);
      }
      map_.insert({key, std::move(value)});
    }

    void RegisterAccess(const Key& key) {
      queue_.erase(key);
      last_access_[key] = time_++;
      queue_.insert(key);
      HandlePendingCleanupQueue();
    }

    Task<Value> Get(Key key, stdx::stop_token stop_token = stdx::stop_token()) {
      RegisterAccess(key);
      auto it = map_.find(key);
      if (it != std::end(map_)) {
        co_return it->second;
      }
      auto promise_it = pending_.find(key);
      if (promise_it != std::end(pending_)) {
        co_return co_await promise_it->second.Get(std::move(stop_token));
      }
      auto promise = Promise<Value>(
          [d_capture = this, key_capture = key,
           stop_token = stop_source_.get_token()]() -> Task<Value> {
            auto d = d_capture;
            auto key = key_capture;
            auto result = co_await d->factory_(key, std::move(stop_token));
            d->pending_cleanup_queue_.push_back(key);
            co_return result;
          });
      promise_it = pending_.insert({key, std::move(promise)}).first;
      co_return co_await promise_it->second.Get(std::move(stop_token));
    }

   private:
    struct Compare {
      bool operator()(const Key& k1, const Key& k2) const {
        return d->last_access_[k1] < d->last_access_[k2];
      }
      Data* d;
    };

    int size_;
    Factory factory_;
    int time_ = 0;
    std::unordered_map<Key, Value> map_;
    std::unordered_map<Key, Promise<Value>> pending_;
    std::unordered_map<Key, int> last_access_;
    std::set<Key, Compare> queue_;
    stdx::stop_source stop_source_;
    std::vector<Key> pending_cleanup_queue_;
  };
  std::unique_ptr<Data> d_;
};

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_LRU_CACHE_H
