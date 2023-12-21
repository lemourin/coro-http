#ifndef CORO_HTTP_COROUTINE_H
#define CORO_HTTP_COROUTINE_H

#if defined(CORO_HTTP_COROUTINE_SUPPORTED)
#include <coroutine>
namespace coro {
namespace std_ns = std;
}  // namespace coro
#elif defined(CORO_HTTP_EXPERIMENTAL_COROUTINE_SUPPORTED)
#include <experimental/coroutine>
namespace coro {
namespace std_ns = std::experimental;
}  // namespace coro
#else
#error "Coroutines unsupported."
#endif

namespace coro::stdx {
using coro::std_ns::coroutine_handle;
using coro::std_ns::noop_coroutine;
using coro::std_ns::suspend_always;
using coro::std_ns::suspend_never;
}  // namespace coro::stdx

#define FOR_CO_AWAIT(decl_expr, container_expr)                              \
  /*NOLINTNEXTLINE*/                                                         \
  if (auto &&coro_container = container_expr; false) {                       \
  } else if (auto coro_begin = co_await std::begin(coro_container); false) { \
  } else if (auto coro_end = std::end(coro_container); false) {              \
  } else                                                                     \
    for (; coro_begin != coro_end; co_await ++coro_begin, 0)                 \
      if (decl_expr = *coro_begin; false) {                                  \
      } else

#endif  // CORO_HTTP_COROUTINE_H
