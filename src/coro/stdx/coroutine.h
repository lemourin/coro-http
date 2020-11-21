#ifndef CORO_HTTP_COROUTINE_H
#define CORO_HTTP_COROUTINE_H

#if __has_include(<coroutine>)
#include <coroutine>
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
template <typename T>
using coroutine_handle = coro::std_ns::coroutine_handle<T>;
using suspend_never = coro::std_ns::suspend_never;
using suspend_always = coro::std_ns::suspend_always;
}  // namespace coro::stdx

#define FOR_CO_AWAIT(decl_expr, container_expr, code) \
  {                                                   \
    auto &&___container = container_expr;             \
    auto ___begin = std::begin(___container);         \
    auto ___end = std::end(___container);             \
    while (___begin != ___end) {                      \
      co_await std::move(___begin);                   \
      decl_expr = *___begin;                          \
      code;                                           \
      ++___begin;                                     \
    }                                                 \
  }

#endif  // CORO_HTTP_COROUTINE_H
