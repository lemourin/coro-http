#ifndef CORO_CLOUDSTORAGE_SHARED_PROMISE_H
#define CORO_CLOUDSTORAGE_SHARED_PROMISE_H

#include <coro/interrupted_exception.h>
#include <coro/promise.h>
#include <coro/stdx/stop_callback.h>
#include <coro/util/make_pointer.h>

#include <functional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace coro {

template <typename T>
class SharedPromise {
 public:
  template <typename F>
  explicit SharedPromise(F producer)
      : shared_data_(std::make_shared<SharedData>(
            SharedData{.producer = std::move(producer)})) {}

  SharedPromise(const SharedPromise&) = delete;
  SharedPromise(SharedPromise&&) = default;
  SharedPromise& operator=(const SharedPromise&) = delete;
  SharedPromise& operator=(SharedPromise&&) = default;

  Task<std::reference_wrapper<const T>> Get(
      coro::stdx::stop_token stop_token) const {
    auto shared_data = shared_data_;
    if (shared_data->producer) {
      Invoke([shared_data, producer = std::exchange(shared_data->producer,
                                                    nullptr)]() -> Task<> {
        try {
          shared_data->result = co_await producer();
        } catch (const std::exception&) {
          shared_data->result = std::current_exception();
        }
        while (!shared_data->awaiters.empty()) {
          (*shared_data->awaiters.begin())->SetValue();
        }
      });
    }
    return Get(shared_data, std::move(stop_token));
  }

 private:
  struct SharedData {
    std::unordered_set<Promise<void>*> awaiters;
    std::variant<std::monostate, std::exception_ptr, T> result;
    std::function<Task<T>()> producer;
  };

  static Task<std::reference_wrapper<const T>> Get(
      std::shared_ptr<SharedData> shared_data,
      coro::stdx::stop_token stop_token) {
    if (std::holds_alternative<std::monostate>(shared_data->result)) {
      Promise<void> semaphore;
      shared_data->awaiters.insert(&semaphore);
      auto guard = coro::util::MakePointer(
          &semaphore, [shared_data](Promise<void>* semaphore) {
            shared_data->awaiters.erase(semaphore);
          });
      coro::stdx::stop_callback stop_callback(
          stop_token, [&] { semaphore.SetException(InterruptedException()); });
      co_await semaphore;
    }
    if (std::holds_alternative<T>(shared_data->result)) {
      co_return std::get<T>(shared_data->result);
    } else {
      std::rethrow_exception(std::get<std::exception_ptr>(shared_data->result));
    }
  }

  std::shared_ptr<SharedData> shared_data_;
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_SHARED_PROMISE_H
