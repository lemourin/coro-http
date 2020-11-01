#include "http.h"

namespace coro::http {

HttpException::HttpException(int status, std::string_view message)
    : status_(status), message_(message) {}

int HttpException::status() const noexcept { return status_; }

const char* HttpException::what() const noexcept { return message_.c_str(); }

HttpBodyGenerator::Iterator::Iterator(HttpBodyGenerator* http_body_generator,
                                      int64_t offset)
    : http_body_generator_(http_body_generator), offset_(offset) {}

bool HttpBodyGenerator::Iterator::operator!=(const Iterator& iterator) const {
  return offset_ != iterator.offset_;
}

HttpBodyGenerator::Iterator& HttpBodyGenerator::Iterator::operator++() {
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

const std::string& HttpBodyGenerator::Iterator::operator*() const {
  return http_body_generator_->data_;
}

bool HttpBodyGenerator::Iterator::await_ready() const {
  return !http_body_generator_->data_.empty() ||
         http_body_generator_->status_ != -1 ||
         http_body_generator_->exception_ptr_;
}

void HttpBodyGenerator::Iterator::await_suspend(coroutine_handle<void> handle) {
  http_body_generator_->handle_ = handle;
}

HttpBodyGenerator::Iterator& HttpBodyGenerator::Iterator::await_resume() {
  if (http_body_generator_->exception_ptr_) {
    std::rethrow_exception(http_body_generator_->exception_ptr_);
  }
  return *this;
}

HttpBodyGenerator::Iterator HttpBodyGenerator::begin() {
  return Iterator(this, 0);
}

HttpBodyGenerator::Iterator HttpBodyGenerator::end() {
  return Iterator(this, INT64_MAX);
}

void HttpBodyGenerator::ReceivedData(std::string_view data) {
  data_ += data;
  if (data_.size() >= MAX_BUFFER_SIZE && !paused_) {
    paused_ = true;
    Pause();
  }
  if (handle_) {
    auto handle = handle_;
    handle_ = nullptr;
    handle.resume();
  }
}

void HttpBodyGenerator::Close(int status) {
  status_ = status;
  if (handle_) {
    auto handle = handle_;
    handle_ = nullptr;
    handle.resume();
  }
}

void HttpBodyGenerator::Close(std::exception_ptr exception) {
  exception_ptr_ = exception;
  if (handle_) {
    auto handle = handle_;
    handle_ = nullptr;
    handle.resume();
  }
}

std::unique_ptr<HttpOperation> Http::Fetch(std::string_view url) {
  return Fetch(Request{.url = std::string(url)});
}

}  // namespace coro::http