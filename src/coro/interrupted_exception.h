#ifndef CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
#define CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H

#include "coro/exception.h"

namespace coro {

class InterruptedException : public Exception {
 public:
  explicit InterruptedException(
      stdx::source_location location = stdx::source_location::current())
      : Exception(std::move(location)) {}

  [[nodiscard]] const char* what() const noexcept final {
    return "interrupted";
  }
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
