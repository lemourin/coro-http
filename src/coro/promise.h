#ifndef CORO_CLOUDSTORAGE_PROMISE_H
#define CORO_CLOUDSTORAGE_PROMISE_H

#include <coro/interrupted_exception.h>
#include <coro/semaphore.h>
#include <coro/stdx/stop_callback.h>
#include <coro/util/make_pointer.h>

#include <unordered_set>
#include <utility>

namespace coro {

template <typename T>
class Promise {
 public:
  template <typename F>
  explicit Promise(F producer)
      : shared_data_(std::make_shared<SharedData>(
            SharedData{.producer = std::move(producer)})) {}

  Task<std::reference_wrapper<const T>> Get(
      coro::stdx::stop_token stop_token) const {
    auto shared_data = shared_data_;
    if (shared_data->producer) {
      [shared_data_capture = shared_data,
       producer = std::exchange(shared_data->producer, nullptr)]() -> Task<> {
        auto shared_data = shared_data_capture;
        try {
          shared_data->result = co_await producer();
        } catch (const std::exception&) {
          shared_data->result = std::current_exception();
        }
        while (!shared_data->awaiters.empty()) {
          (*shared_data->awaiters.begin())->resume();
        }
      }();
    }
    return Get(shared_data, std::move(stop_token));
  }

 private:
  struct SharedData {
    std::unordered_set<Semaphore*> awaiters;
    std::variant<std::monostate, std::exception_ptr, T> result;
    std::function<Task<T>()> producer;
  };

  static Task<std::reference_wrapper<const T>> Get(
      std::shared_ptr<SharedData> shared_data,
      coro::stdx::stop_token stop_token) {
    if (std::holds_alternative<std::monostate>(shared_data->result)) {
      Semaphore semaphore;
      shared_data->awaiters.insert(&semaphore);
      auto guard = coro::util::MakePointer(
          &semaphore, [shared_data](Semaphore* semaphore) {
            shared_data->awaiters.erase(semaphore);
          });
      coro::stdx::stop_callback stop_callback(stop_token,
                                              [&] { semaphore.resume(); });
      co_await semaphore;
    }
    if (std::holds_alternative<T>(shared_data->result)) {
      co_return std::get<T>(shared_data->result);
    } else if (std::holds_alternative<std::exception_ptr>(
                   shared_data->result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(shared_data->result));
    } else {
      throw InterruptedException();
    }
  }

  std::shared_ptr<SharedData> shared_data_;
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_PROMISE_H
