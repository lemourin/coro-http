#ifndef CORO_CLOUDSTORAGE_FOR_EACH_H
#define CORO_CLOUDSTORAGE_FOR_EACH_H

#include <coro/util/type_list.h>

namespace coro::util {

template <typename TypeList>
struct ForEach;

template <typename... Entry>
struct ForEach<TypeList<Entry...>> {
  template <typename F>
  void operator()(const F& func) const {
    (func.template operator()<Entry>(), ...);
  }
};

}  // namespace coro::util

#endif  // CORO_CLOUDSTORAGE_FOR_EACH_H
