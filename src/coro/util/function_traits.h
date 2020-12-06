#ifndef CORO_HTTP_FUNCTION_TRAITS_H
#define CORO_HTTP_FUNCTION_TRAITS_H

#include <utility>

#include "type_list.h"

namespace coro::util {

namespace internal {

template <typename Ret, typename... Args>
struct FunctionTraits {
  using return_type = Ret;
  using argument_list_type = TypeList<Args...>;
};

template <typename Ret, typename... Args>
FunctionTraits<Ret, Args...> MakeFunctionTraits(Ret (*)(Args...));

template <typename Class, typename Ret, typename... Args>
FunctionTraits<Ret, Args...> MakeFunctionTraits(Ret (Class::*)(Args...) const);

template <typename F>
decltype(MakeFunctionTraits(&F::operator())) MakeFunctionTraits(F&&);

}  // namespace internal

template <typename T>
struct ReturnType {
  using type = typename decltype(internal::MakeFunctionTraits(
      std::declval<T>()))::return_type;
};

template <typename T>
using ReturnTypeT = typename ReturnType<T>::type;

template <typename T>
struct ArgumentListType {
  using type = typename decltype(internal::MakeFunctionTraits(
      std::declval<T>()))::argument_list_type;
};

template <typename T>
using ArgumentListTypeT = typename ArgumentListType<T>::type;

}  // namespace coro::util

#endif  // CORO_HTTP_FUNCTION_TRAITS_H
