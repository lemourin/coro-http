#ifndef CORO_HTTP_HTTP_EXCEPTION_H
#define CORO_HTTP_HTTP_EXCEPTION_H

#include <stdexcept>
#include <string>
#include <string_view>

namespace coro::http {

class HttpException : public std::exception {
 public:
  static constexpr int kAborted = -1;
  static constexpr int kMalformedResponse = -2;
  static constexpr int kUnknown = -3;
  static constexpr int kInvalidMethod = -4;
  static constexpr int kBadRequest = 400;
  static constexpr int kNotFound = 404;
  static constexpr int kRangeNotSatisfiable = 416;

  HttpException(int status) : HttpException(status, ToString(status)) {}

  HttpException(int status, std::string_view message)
      : status_(status), message_(message) {}

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
