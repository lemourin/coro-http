#ifndef CORO_UTIL_RAII_UTILS_H
#define CORO_UTIL_RAII_UTILS_H

#include <memory>

namespace coro::util {

namespace internal {

template <typename F>
struct Guard {
  explicit Guard(F func) : func(std::move(func)) {}
  ~Guard() {
    if (func) {
      (*func)();
    }
  }
  Guard(const Guard&) = delete;
  template <typename Other>
  Guard(Other&& other) noexcept {
    func = std::move(other.func);
    other.func = std::nullopt;
  }
  Guard& operator=(const Guard&) = delete;
  template <typename Other>
  Guard& operator=(Other&& other) noexcept {
    func = std::move(other.func);
    other.func = std::nullopt;
    return *this;
  }

  std::optional<F> func;
};

}  // namespace internal

template <typename F>
auto AtScopeExit(F func) {
  return internal::Guard(std::move(func));
}

template <typename T, typename F>
auto MakePointer(T* pointer, F deleter) {
  return std::unique_ptr<T, F>(pointer, std::move(deleter));
}

}  // namespace coro::util

#endif  // CORO_UTIL_RAII_UTILS_H
