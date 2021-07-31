#ifndef CORO_WHEN_ALL_H
#define CORO_WHEN_ALL_H

#include <optional>
#include <vector>

#include "coro/promise.h"
#include "coro/task.h"

namespace coro {

namespace internal {

template <typename>
struct WhenAll;

template <size_t... Index>
struct WhenAll<std::index_sequence<Index...>> {
  template <typename... T>
  Task<std::tuple<T...>> operator()(Task<T>... tasks) {
    static_assert(sizeof...(T) > 0);
    std::tuple<T...> result;
    std::optional<std::exception_ptr> exception;
    Promise<void> semaphore;
    size_t ready = 0;
    (RunTask(
         [&](auto task, auto& result) -> Task<> {
           try {
             result = co_await task;
           } catch (...) {
             exception = std::current_exception();
           }
           ready++;
           if (ready == sizeof...(T)) {
             semaphore.SetValue();
           }
         },
         std::move(tasks), std::get<Index>(result)),
     ...);
    co_await semaphore;
    if (exception) {
      std::rethrow_exception(*exception);
    }
    co_return std::move(result);
  }
};

}  // namespace internal

template <typename... T>
Task<std::tuple<T...>> WhenAll(Task<T>... tasks) {
  return internal::WhenAll<std::make_index_sequence<sizeof...(T)>>{}(
      std::move(tasks)...);
}

template <typename Container, typename T = typename Container::value_type::type,
          std::enable_if_t<!std::is_void_v<T>, int> = 0>
Task<std::vector<T>> WhenAll(Container tasks) {
  if (tasks.empty()) {
    co_return std::vector<T>{};
  }
  Promise<void> semaphore;
  size_t not_ready = tasks.size();
  std::vector<T> result(tasks.size());
  std::optional<std::exception_ptr> exception;
  for (size_t i = 0; i < tasks.size(); i++) {
    RunTask(
        [&](auto task, T& result) -> Task<> {
          try {
            result = co_await task;
          } catch (...) {
            exception = std::current_exception();
          }
          not_ready--;
          if (not_ready == 0) {
            semaphore.SetValue();
          }
        },
        std::move(tasks[i]), result[i]);
  }
  co_await semaphore;
  if (exception) {
    std::rethrow_exception(*exception);
  }
  co_return std::move(result);
}

template <typename Container, typename T = typename Container::value_type::type,
          std::enable_if_t<std::is_void_v<T>, int> = 0>
Task<> WhenAll(Container tasks) {
  if (tasks.empty()) {
    co_return;
  }
  Promise<void> semaphore;
  size_t not_ready = tasks.size();
  std::optional<std::exception_ptr> exception;
  for (size_t i = 0; i < tasks.size(); i++) {
    RunTask(
        [&](auto task) -> Task<> {
          try {
            co_await task;
          } catch (...) {
            exception = std::current_exception();
          }
          not_ready--;
          if (not_ready == 0) {
            semaphore.SetValue();
          }
        },
        std::move(tasks[i]));
  }
  co_await semaphore;
  if (exception) {
    std::rethrow_exception(*exception);
  }
}

}  // namespace coro

#endif  // CORO_WHEN_ALL_H