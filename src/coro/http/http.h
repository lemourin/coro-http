#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/stdx/stop_token.h>
#include <coro/task.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace coro::http {

const int MAX_BUFFER_SIZE = 1u << 16u;

struct Request {
  std::string url;
  std::unordered_multimap<std::string, std::string> headers;
  std::string body;
};

class HttpBodyGenerator {
 public:
  virtual ~HttpBodyGenerator() = default;

  class Iterator {
   public:
    Iterator(HttpBodyGenerator* http_body_generator, int64_t offset);

    bool operator!=(const Iterator& iterator) const;
    Iterator& operator++();
    const std::string& operator*() const;

    [[nodiscard]] bool await_ready() const;
    void await_suspend(coroutine_handle<void> handle);
    Iterator& await_resume();

   private:
    HttpBodyGenerator* http_body_generator_;
    int64_t offset_;
  };

  Iterator begin();
  Iterator end();

 protected:
  virtual void Pause() = 0;
  virtual void Resume() = 0;

  void ReceivedData(std::string_view data);
  void Close(int status);
  void Close(std::exception_ptr);

 private:
  coroutine_handle<void> handle_;
  std::string data_;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
  bool paused_ = false;
};

struct Response {
  int status;
  std::unordered_multimap<std::string, std::string> headers;
  std::unique_ptr<HttpBodyGenerator> body;
};

class HttpException : public std::exception {
 public:
  HttpException(int status, std::string_view message);

  [[nodiscard]] const char* what() const noexcept override;
  [[nodiscard]] int status() const noexcept;

 private:
  int status_;
  std::string message_;
};

class HttpOperation {
 public:
  virtual ~HttpOperation() = default;

  virtual bool await_ready() = 0;
  virtual void await_suspend(coroutine_handle<void> awaiting_coroutine) = 0;
  virtual Response await_resume() = 0;
};

class Http {
 public:
  virtual ~Http() = default;

  std::unique_ptr<HttpOperation> Fetch(std::string_view url,
                                       stdx::stop_token = stdx::stop_token());
  virtual std::unique_ptr<HttpOperation> Fetch(
      Request request, stdx::stop_token = stdx::stop_token()) = 0;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_H
