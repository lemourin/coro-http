#include "http.h"

namespace coro::http {

HttpException::HttpException(int status, std::string_view message)
    : status_(status), message_(message) {}

int HttpException::status() const noexcept { return status_; }

const char* HttpException::what() const noexcept { return message_.c_str(); }

HttpOperation::HttpOperation(std::unique_ptr<HttpOperationImpl>&& impl)
    : impl_(std::move(impl)) {}

void HttpOperation::await_suspend(coroutine_handle<void> awaiting_coroutine) {
  impl_->await_suspend(awaiting_coroutine);
}

bool HttpOperation::await_ready() { return impl_->await_ready(); }

Response HttpOperation::await_resume() { return impl_->await_resume(); }

HttpOperation Http::Fetch(std::string_view url) {
  return Fetch(Request{.url = std::string(url)});
}

}  // namespace coro::http