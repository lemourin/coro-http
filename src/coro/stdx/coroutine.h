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

namespace stdx {
template <typename T>
using coroutine_handle = coro::std_ns::coroutine_handle<T>;
using suspend_never = coro::std_ns::suspend_never;
using suspend_always = coro::std_ns::suspend_always;
}  // namespace stdx

#endif  // CORO_HTTP_COROUTINE_H
