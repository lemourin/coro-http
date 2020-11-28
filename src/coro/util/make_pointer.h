#ifndef CORO_CLOUDSTORAGE_MAKE_POINTER_H
#define CORO_CLOUDSTORAGE_MAKE_POINTER_H

#include <memory>

namespace coro::util {

template <typename T, typename F>
auto MakePointer(T* pointer, F deleter) {
  return std::unique_ptr<T, F>(pointer, std::move(deleter));
}

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_MAKE_POINTER_H
