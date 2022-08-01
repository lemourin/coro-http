#ifndef CORO_HTTP_HTTP_EXCEPTION_H
#define CORO_HTTP_HTTP_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <string_view>

#include "coro/exception.h"

namespace coro::http {

class HttpException : public Exception {
 public:
  static constexpr int kAborted = -1;
  static constexpr int kMalformedResponse = -2;
  static constexpr int kUnknown = -3;
  static constexpr int kInvalidMethod = -4;
  static constexpr int kBadRequest = 400;
  static constexpr int kNotFound = 404;
  static constexpr int kRangeNotSatisfiable = 416;

  HttpException(
      int status,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : HttpException(status, ToString(status), location, stacktrace) {}

  HttpException(
      int status, std::string_view message,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : Exception(std::move(location), std::move(stacktrace)),
        status_(status),
        message_(message) {}

  [[nodiscard]] const char* what() const noexcept override {
    return message_.c_str();
  }
  [[nodiscard]] int status() const noexcept { return status_; }

 private:
  static std::string ToString(int status);

  int status_;
  std::string message_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_EXCEPTION_H
