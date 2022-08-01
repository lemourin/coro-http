#ifndef CORO_HTTP_EXCEPTION_H
#define CORO_HTTP_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <string_view>

#include "coro/stdx/source_location.h"
#include "coro/stdx/stacktrace.h"

namespace coro {

class Exception : public std::exception {
 public:
  Exception(stdx::source_location location = stdx::source_location::current(),
            stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : stacktrace_(std::move(stacktrace)),
        source_location_(std::move(location)) {}

  const stdx::stacktrace& stacktrace() const { return stacktrace_; }

  const stdx::source_location& source_location() const {
    return source_location_;
  }

 private:
  stdx::source_location source_location_;
  stdx::stacktrace stacktrace_;
};

class RuntimeError : public Exception {
 public:
  explicit RuntimeError(
      std::string message,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : Exception(std::move(location), std::move(stacktrace)),
        message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class LogicError : public Exception {
 public:
  explicit LogicError(
      std::string message,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : Exception(std::move(location), std::move(stacktrace)),
        message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class InvalidArgument : public LogicError {
 public:
  explicit InvalidArgument(
      std::string message,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : LogicError(std::move(message), std::move(location),
                   std::move(stacktrace)) {}
};

}  // namespace coro

#endif