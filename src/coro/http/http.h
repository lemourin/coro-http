#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/task.h>

#include <memory>
#include <string>

namespace coro::http {

struct Response {
  int status;
  std::string body;
};

class HttpException : public std::exception {
 public:
  HttpException(int status, std::string_view message);

  const char* what() const noexcept override;
  int status() const noexcept;

 private:
  int status_;
  std::string message_;
};

class HttpOperationImpl {
 public:
  virtual ~HttpOperationImpl() = default;
  virtual void await_suspend(coroutine_handle<void> awaiting_coroutine) = 0;
  virtual Response await_resume() = 0;
};

class HttpOperation {
 public:
  explicit HttpOperation(std::unique_ptr<HttpOperationImpl>&&);

  static bool await_ready();
  void await_suspend(coroutine_handle<void> awaiting_coroutine);
  Response await_resume();

 private:
  std::unique_ptr<HttpOperationImpl> impl_;
};

class Http {
 public:
  virtual ~Http() = default;

  virtual HttpOperation Fetch(std::string_view url) = 0;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_H
