#ifndef CORO_CLOUDSTORAGE_LRU_CACHE_H
#define CORO_CLOUDSTORAGE_LRU_CACHE_H

#include <coro/shared_promise.h>
#include <coro/stdx/stop_source.h>
#include <coro/stdx/stop_token.h>
#include <coro/task.h>

#include <set>
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

  void Invalidate(const Key& key) const { d_->Invalidate(key); }

  std::optional<Value> GetCached(const Key& key) const {
    return d_->GetCached(key);
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

    std::optional<Value> GetCached(const Key& key) const {
      auto it = map_.find(key);
      if (it == map_.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    void Invalidate(const Key& key) {
      queue_.erase(key);
      last_access_.erase(key);
      map_.erase(key);
    }

    void Insert(Key key, Value value) {
      while (map_.size() >= size_) {
        Invalidate(*queue_.begin());
      }
      map_.insert({std::move(key), std::move(value)});
    }

    void RegisterAccess(Key key) {
      queue_.erase(key);
      last_access_[key] = time_++;
      queue_.insert(std::move(key));
      HandlePendingCleanupQueue();
    }

    Task<Value> Get(Key key, stdx::stop_token stop_token = stdx::stop_token()) {
      RegisterAccess(key);
      if (auto result = GetCached(key)) {
        co_return std::move(*result);
      }
      auto promise_it = pending_.find(key);
      if (promise_it != std::end(pending_)) {
        co_return co_await promise_it->second.Get(std::move(stop_token));
      }
      auto promise = SharedPromise(ProduceValue{
          .d = this, .key = key, .stop_token = stop_source_.get_token()});
      promise_it = pending_.insert({key, std::move(promise)}).first;
      co_return co_await promise_it->second.Get(std::move(stop_token));
    }

   private:
    struct ProduceValue {
      Task<Value> operator()() const {
        auto guard = util::MakePointer(d, [this](Data* d) {
          d->pending_cleanup_queue_.emplace_back(key);
        });
        auto result = co_await d->factory_(key, std::move(stop_token));
        d->Insert(std::move(key), result);
        co_return std::move(result);
      }
      Data* d;
      Key key;
      stdx::stop_token stop_token;
    };

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
    std::unordered_map<Key, SharedPromise<ProduceValue>> pending_;
    std::unordered_map<Key, int> last_access_;
    std::set<Key, Compare> queue_;
    stdx::stop_source stop_source_;
    std::vector<Key> pending_cleanup_queue_;
  };
  std::unique_ptr<Data> d_;
};

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_LRU_CACHE_H
