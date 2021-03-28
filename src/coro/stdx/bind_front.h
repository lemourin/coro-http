#ifndef CORO_STDX_BIND_FRONT
#define CORO_STDX_BIND_FRONT

#include <tuple>

namespace coro::stdx {

namespace internal {
template <typename FD, typename... Args>
struct BindFrontF {
  FD func;
  std::tuple<std::decay_t<Args>...> args;

  template <typename... CallArgs>
  auto operator()(CallArgs&&... call_args) && {
    return std::apply(
        func,
        std::tuple_cat(std::move(args), std::forward_as_tuple(call_args...)));
  }
};
}  // namespace internal

template <typename FD, typename... Args>
auto BindFront(FD func, Args&&... args) {
  return internal::BindFrontF<FD, Args...>{
      std::move(func), std::make_tuple(std::forward<Args>(args)...)};
}

}  // namespace coro::stdx

#endif  // CORO_STDX_BIND_FRONT