#ifndef CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
#define CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H

#include "coro/exception.h"

namespace coro {

class InterruptedException : public Exception {
 public:
  [[nodiscard]] const char* what() const noexcept final {
    return "interrupted";
  }
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
