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

#define FOR_CO_AWAIT(decl_expr, container_expr, code)  \
  {                                                    \
    auto &&___container = container_expr;              \
    auto ___begin = co_await std::begin(___container); \
    auto ___end = std::end(___container);              \
    while (___begin != ___end) {                       \
      decl_expr = *___begin;                           \
      code;                                            \
      co_await ++___begin;                             \
    }                                                  \
  }

#endif  // CORO_HTTP_COROUTINE_H
