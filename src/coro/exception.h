#ifndef CORO_HTTP_EXCEPTION_H
#define CORO_HTTP_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <string_view>

namespace coro {

class Exception : public std::exception {
 public:
  Exception();

  std::string_view stacktrace() const { return stacktrace_; }

 private:
  std::string stacktrace_;
};

class RuntimeError : public Exception {
 public:
  explicit RuntimeError(std::string message) : message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class LogicError : public Exception {
 public:
  explicit LogicError(std::string message) : message_(std::move(message)) {}

  const char* what() const noexcept override { return message_.c_str(); }

 private:
  std::string message_;
};

class InvalidArgument : public LogicError {
 public:
  using LogicError::LogicError;
};

}  // namespace coro

#endif