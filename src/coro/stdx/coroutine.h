#ifndef CORO_HTTP_COROUTINE_H
#define CORO_HTTP_COROUTINE_H

#if defined(COROUTINE_SUPPORTED)
#include <version>
#if defined(__clang__) && !defined(__cpp_impl_coroutine)
#define __cpp_impl_coroutine 1
#if defined(_MSC_VER) && !defined(__cpp_lib_coroutine)
#define __cpp_lib_coroutine 1
#endif
#endif
#include <coroutine>
#if defined(__clang__)
namespace std::experimental {
using std::coroutine_handle;
using std::coroutine_traits;
}  // namespace std::experimental
#endif
namespace coro {
namespace std_ns = std;
}
#elif defined(EXPERIMENTAL_COROUTINE_SUPPORTED)
#include <experimental/coroutine>
namespace coro {
namespace std_ns = std::experimental;
}
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
