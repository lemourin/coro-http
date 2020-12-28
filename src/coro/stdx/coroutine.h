#ifndef CORO_HTTP_COROUTINE_H
#define CORO_HTTP_COROUTINE_H

#if __has_include(<coroutine>)
#include <version>
#if defined(__clang__) && !defined(__cpp_impl_coroutine)
#define __cpp_impl_coroutine 1
#endif
#include <coroutine>
#if defined(__clang__)
namespace std::experimental {
template <typename... Ts>
struct coroutine_traits : std::coroutine_traits<Ts...> {};
template <typename... Ts>
struct coroutine_handle : std::coroutine_handle<Ts...> {};
}  // namespace std::experimental
#endif
namespace coro {
namespace std_ns = std;
}
#elif __has_include(<experimental/coroutine>)
#include <experimental/coroutine>
namespace coro {
namespace std_ns = std::experimental;
}
#else
#error "Coroutines unsupported."
#endif

namespace coro::stdx {
template <typename... Ts>
using coroutine_handle = coro::std_ns::coroutine_handle<Ts...>;
using suspend_never = coro::std_ns::suspend_never;
using suspend_always = coro::std_ns::suspend_always;
}  // namespace coro::stdx

#define FOR_CO_AWAIT(decl_expr, container_expr)                              \
  if (auto &&coro_container = container_expr; false) {                       \
  } else if (auto coro_begin = co_await std::begin(coro_container); false) { \
  } else if (auto coro_end = std::end(coro_container); false) {              \
  } else                                                                     \
    for (; coro_begin != coro_end; co_await ++coro_begin, 0)                 \
      if (decl_expr = *coro_begin; false) {                                  \
      } else

#endif  // CORO_HTTP_COROUTINE_H
