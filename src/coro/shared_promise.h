#ifndef CORO_UTIL_SHARED_PROMISE_H
#define CORO_UTIL_SHARED_PROMISE_H

#include <coro/interrupted_exception.h>
#include <coro/promise.h>
#include <coro/stdx/stop_callback.h>
#include <coro/util/raii_utils.h>

#include <functional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace coro {

template <typename F>
class SharedPromise {
 public:
  using T = typename decltype(std::declval<F>()())::type;
  using TaskT = std::conditional_t<
      std::is_same_v<void, T>, Task<>,
      Task<std::conditional_t<std::is_reference_v<T>, const T,
                              std::reference_wrapper<const T>>>>;

  explicit SharedPromise(F producer)
      : shared_data_(std::make_shared<SharedData>(
            SharedData{.producer = std::move(producer)})) {}

  SharedPromise(const SharedPromise&) = delete;
  SharedPromise(SharedPromise&&) noexcept = default;
  SharedPromise& operator=(const SharedPromise&) = delete;
  SharedPromise& operator=(SharedPromise&&) noexcept = default;

  TaskT Get(coro::stdx::stop_token stop_token) const {
    auto shared_data = shared_data_;
    if (shared_data->producer) {
      Invoke([shared_data,
              producer = *std::exchange(shared_data->producer,
                                        std::nullopt)]() mutable -> Task<> {
        try {
          if constexpr (std::is_same_v<void, T>) {
            co_await producer();
            shared_data->result = std::monostate();
          } else {
            if constexpr (std::is_reference_v<T>) {
              shared_data->result = &co_await producer();
            } else {
              shared_data->result = co_await producer();
            }
          }
        } catch (...) {
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
  struct NotReady {};
  struct SharedData {
    std::unordered_set<Promise<void>*> awaiters;
    std::variant<
        NotReady, std::exception_ptr,
        std::conditional_t<std::is_same_v<T, void>, std::monostate,
                           std::conditional_t<std::is_reference_v<T>,
                                              std::remove_reference_t<T>*, T>>>
        result;
    std::optional<F> producer;
  };

  static TaskT Get(std::shared_ptr<SharedData> shared_data,
                   coro::stdx::stop_token stop_token) {
    if (std::holds_alternative<NotReady>(shared_data->result)) {
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
    if (std::holds_alternative<std::exception_ptr>(shared_data->result)) {
      std::rethrow_exception(std::get<std::exception_ptr>(shared_data->result));
    }
    if constexpr (!std::is_same_v<T, void>) {
      if constexpr (std::is_reference_v<T>) {
        co_return* std::get<std::remove_reference_t<T>*>(shared_data->result);
      } else {
        co_return std::cref(std::get<T>(shared_data->result));
      }
    }
  }

  std::shared_ptr<SharedData> shared_data_;
};

namespace internal {
template <typename T>
using TaskT = std::conditional_t<std::is_same_v<T, void>, Task<>, Task<T>>;
}

template <typename T, typename ReturnT = typename T::type>
internal::TaskT<ReturnT> InterruptibleAwait(T&& task,
                                            stdx::stop_token stop_token) {
  SharedPromise promise(
      [task = std::move(task)]() mutable -> internal::TaskT<ReturnT> {
        co_return co_await std::move(task);
      });
  co_return co_await promise.Get(std::move(stop_token));
}

template <typename T, typename ReturnT = typename T::type>
internal::TaskT<ReturnT> InterruptibleAwait(T& task,
                                            stdx::stop_token stop_token) {
  SharedPromise promise([&]() -> internal::TaskT<ReturnT> {
    auto& ref = task;
    co_return co_await ref;
  });
  co_return co_await promise.Get(std::move(stop_token));
}

}  // namespace coro

#endif  // CORO_UTIL_SHARED_PROMISE_H
