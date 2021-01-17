#ifndef CORO_UTIL_RAII_UTILS_H
#define CORO_UTIL_RAII_UTILS_H

#include <memory>

namespace coro::util {

template <typename F>
auto AtScopeExit(F func) {
  struct Guard {
    explicit Guard(F func) : func(std::move(func)) {}
    ~Guard() {
      if (func) {
        (*func)();
      }
    }
    Guard(const Guard&) = delete;
    Guard(Guard&& other) noexcept {
      func = std::move(other.func);
      other.func = std::nullopt;
    }
    Guard& operator=(const Guard&) = delete;
    Guard& operator=(Guard&& other) noexcept {
      func = std::move(other.func);
      other.func = std::nullopt;
      return *this;
    }

    std::optional<F> func;
  };
  return Guard(std::move(func));
}

template <typename T, typename F>
auto MakePointer(T* pointer, F deleter) {
  return std::unique_ptr<T, F>(pointer, std::move(deleter));
}

}  // namespace coro::util

#endif  // CORO_UTIL_RAII_UTILS_H
