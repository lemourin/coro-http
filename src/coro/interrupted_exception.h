#ifndef CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
#define CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H

#include <exception>

namespace coro {

class InterruptedException : public std::exception {
 public:
  [[nodiscard]] const char* what() const noexcept final {
    return "interrupted";
  }
};

}  // namespace coro

#endif  // CORO_CLOUDSTORAGE_INTERRUPTED_EXCEPTION_H
