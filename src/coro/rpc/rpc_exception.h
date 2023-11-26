#ifndef CORO_RPC_RPC_EXCEPTION_H
#define CORO_RPC_RPC_EXCEPTION_H

#include "coro/exception.h"

namespace coro::rpc {

class RpcException : public Exception {
 public:
  static constexpr int kAborted = -1;
  static constexpr int kMalformedRequest = -2;

  RpcException(
      int status,
      stdx::source_location location = stdx::source_location::current(),
      stdx::stacktrace stacktrace = stdx::stacktrace::current())
      : RpcException(status, ToString(status), location, stacktrace) {}

  RpcException(
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

}  // namespace coro::rpc

#endif  // CORO_RPC_RPC_EXCEPTION_H