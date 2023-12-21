#ifndef CORO_CLOUDSTORAGE_CONCEPTS_H
#define CORO_CLOUDSTORAGE_CONCEPTS_H

#include <type_traits>

namespace coro::stdx {

template <typename T1, typename T2>
concept same_as = std::is_same_v<T1, T2>;

template <typename T1, typename T2>
concept convertible_to = std::is_convertible_v<T1, T2>;

}  // namespace coro::stdx

#endif  // CORO_CLOUDSTORAGE_CONCEPTS_H
