#include "coro/stdx/stop_token.h"

#include <utility>

#include "coro/stdx/stop_source.h"

namespace coro::stdx {

bool stop_token::stop_requested() const noexcept {
  return state_ && state_->stopped;
}

bool stop_token::stop_possible() const noexcept { return state_ != nullptr; }

stop_token::stop_token(
    std::shared_ptr<internal::stop_source_state> state) noexcept
    : state_(std::move(state)) {}

}  // namespace coro::stdx
