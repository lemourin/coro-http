#ifndef CORO_HTTP_EXCEPTION_H
#define CORO_HTTP_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <string_view>

#include "coro/stdx/source_location.h"

namespace coro {

class Exception : public std::exception {
 public:
  Exception(stdx::source_location location = stdx::source_location::current());

  std::string_view stacktrace() const { return stacktrace_; }

  const stdx::source_location& source_location() const {
    return source_location_;
  }

 private:
  std::string stacktrace_;
  stdx::source_location source_location_;
};

class RuntimeError : public Exception {
 public:
  explicit RuntimeError(
      std::string message,
      stdx::source_location location = stdx::source_location::current())
      : Exception(std::move(location)), message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class LogicError : public Exception {
 public:
  explicit LogicError(std::string message, stdx::source_location location =
                                               stdx::source_location::current())
      : Exception(std::move(location)), message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class InvalidArgument : public LogicError {
 public:
  explicit InvalidArgument(
      std::string message,
      stdx::source_location location = stdx::source_location::current())
      : LogicError(std::move(message), std::move(location)) {}
};

std::string GetHtmlStacktrace(std::string_view stacktrace);

}  // namespace coro

#endif