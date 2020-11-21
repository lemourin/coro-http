#ifndef CORO_CLOUDSTORAGE_CONCEPTS_H
#define CORO_CLOUDSTORAGE_CONCEPTS_H

#include <concepts>

namespace coro::stdx {

template <typename T>
concept integral = std::is_integral_v<T>;

}

#endif  // CORO_CLOUDSTORAGE_CONCEPTS_H
