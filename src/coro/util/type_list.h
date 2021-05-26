#ifndef CORO_HTTP_TYPE_LIST_H
#define CORO_HTTP_TYPE_LIST_H

#include <type_traits>

namespace coro::util {

template <typename...>
struct TypeList;

template <typename, std::size_t, typename = void>
struct TypeAt;

template <typename Head, typename... Tail>
struct TypeAt<TypeList<Head, Tail...>, 0> : std::type_identity<Head> {};

template <typename Head, typename... Tail, std::size_t Index>
struct TypeAt<TypeList<Head, Tail...>, Index, std::enable_if_t<(Index > 0)>>
    : TypeAt<TypeList<Tail...>, Index - 1> {};

template <typename Lst, std::size_t Index>
using TypeAtT = typename TypeAt<Lst, Index>::type;

template <typename>
struct TypeListLength;

template <typename... T>
struct TypeListLength<TypeList<T...>>
    : std::integral_constant<std::size_t, sizeof...(T)> {};

template <typename Lst>
constexpr auto TypeListLengthV = TypeListLength<Lst>{};

template <typename...>
struct Concat;

template <typename... Ts>
using ConcatT = typename Concat<Ts...>::type;

template <>
struct Concat<> : std::type_identity<TypeList<>> {};

namespace internal {

template <typename, typename>
struct Concat2;

template <typename... T1, typename... T2>
struct Concat2<TypeList<T1...>, TypeList<T2...>>
    : std::type_identity<TypeList<T1..., T2...>> {};

}  // namespace internal

template <typename... T1, typename... Ts>
struct Concat<TypeList<T1...>, Ts...>
    : internal::Concat2<TypeList<T1...>, ConcatT<Ts...>> {};

template <template <typename...> typename F, typename Lst, typename... Args>
struct Filter;

template <template <typename...> typename F, typename Lst, typename... Args>
using FilterT = typename Filter<F, Lst, Args...>::type;

template <template <typename...> typename F, typename... Args>
struct Filter<F, TypeList<>, Args...> : std::type_identity<TypeList<>> {};

template <template <typename...> typename F, typename Head, typename... Tail,
          typename... Args>
struct Filter<F, TypeList<Head, Tail...>, Args...>
    : Concat<std::conditional_t<F<Head, Args...>{}, TypeList<Head>, TypeList<>>,
             FilterT<F, TypeList<Tail...>, Args...>> {};

template <template <typename...> typename F, typename Lst, typename... Args>
struct Map;

template <template <typename...> typename F, typename Lst, typename... Args>
using MapT = typename Map<F, Lst, Args...>::type;

template <template <typename...> typename F, typename... Ts, typename... Args>
struct Map<F, TypeList<Ts...>, Args...>
    : std::type_identity<TypeList<typename F<Ts, Args...>::type...>> {};

template <template <typename...> typename Head, typename Lst>
struct FromTypeList;

template <template <typename...> typename Head, typename Lst>
using FromTypeListT = typename FromTypeList<Head, Lst>::type;

template <template <typename...> typename Head, typename... Ts>
struct FromTypeList<Head, TypeList<Ts...>> : std::type_identity<Head<Ts...>> {};

template <template <typename...> typename Head, typename T>
struct ToTypeList;

template <template <typename...> typename Head, typename T>
using ToTypeListT = typename ToTypeList<Head, T>::type;

template <template <typename...> typename Head, typename... Ts>
struct ToTypeList<Head, Head<Ts...>> : std::type_identity<TypeList<Ts...>> {};

}  // namespace coro::util

#endif  // CORO_HTTP_TYPE_LIST_H
