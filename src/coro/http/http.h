#ifndef CORO_HTTP_HTTP_H
#define CORO_HTTP_HTTP_H

#include <coro/task.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace coro::http {

const int MAX_BUFFER_SIZE = 1u << 20u;

class HttpBodyGenerator {
 public:
  virtual ~HttpBodyGenerator() = default;

  class Iterator {
   public:
    Iterator(HttpBodyGenerator* http_body_generator, int64_t offset)
        : http_body_generator_(http_body_generator), offset_(offset) {}

    bool operator!=(const Iterator& iterator) const {
      return offset_ != iterator.offset_;
    }

    Iterator& operator++() {
      if (http_body_generator_->status_ != -1) {
        offset_ = INT64_MAX;
      } else {
        offset_++;
      }
      http_body_generator_->data_.clear();
      if (http_body_generator_->paused_) {
        http_body_generator_->paused_ = false;
        http_body_generator_->Resume();
      }
      return *this;
    }

    const std::string& operator*() const { return http_body_generator_->data_; }

    [[nodiscard]] bool await_ready() const {
      return !http_body_generator_->data_.empty() ||
             http_body_generator_->status_ != -1;
    }

    void await_suspend(coroutine_handle<void> handle) {
      http_body_generator_->handle_ = handle;
    }

    Iterator& await_resume() { return *this; }

   private:
    HttpBodyGenerator* http_body_generator_;
    int64_t offset_;
  };

  Iterator begin() { return Iterator(this, 0); }

  Iterator end() { return Iterator(this, INT64_MAX); }

  void ReceivedData(std::string_view data) {
    data_ += data;
    if (data_.size() >= MAX_BUFFER_SIZE && !paused_) {
      paused_ = true;
      Pause();
    }
    if (handle_) {
      handle_.resume();
    }
  }

  void Close(int status) {
    status_ = status;
    if (handle_) {
      handle_.resume();
    }
  }

  virtual void Pause() = 0;
  virtual void Resume() = 0;

 private:
  coroutine_handle<void> handle_;
  std::string data_;
  int status_ = -1;
  bool paused_ = false;
};

struct Request {
  std::string url;
  std::unordered_multimap<std::string, std::string> headers;
  std::string body;
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

  std::unique_ptr<HttpOperation> Fetch(std::string_view url);
  virtual std::unique_ptr<HttpOperation> Fetch(Request&& request) = 0;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_H
