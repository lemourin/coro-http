#include "http.h"

namespace coro::http {

HttpOperation::HttpOperation(std::unique_ptr<HttpOperationImpl>&& impl)
    : impl_(std::move(impl)) {}

void HttpOperation::await_suspend(
    std::experimental::coroutine_handle<> awaiting_coroutine) {
  impl_->await_suspend(awaiting_coroutine);
}

bool HttpOperation::await_ready() { return false; }

Response HttpOperation::await_resume() { return impl_->await_resume(); }

}  // namespace coro::http