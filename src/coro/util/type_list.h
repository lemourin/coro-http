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

}  // namespace coro::util

#endif  // CORO_HTTP_TYPE_LIST_H
