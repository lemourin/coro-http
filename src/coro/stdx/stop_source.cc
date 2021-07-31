#include "coro/stdx/stop_source.h"

#include <memory>

namespace coro::stdx {

stop_source::stop_source()
    : state_(std::make_shared<internal::stop_source_state>()) {}

bool stop_source::request_stop() noexcept {
  if (!state_) {
    return false;
  }
  state_->stopped = true;

  auto state = state_;
  while (!state->stop_callback.empty()) {
    auto* cb = *state->stop_callback.begin();
    state->stop_callback.erase(state->stop_callback.begin());
    (*cb)();
  }

  return true;
}

stop_token stop_source::get_token() const noexcept {
  return stop_token{state_};
}

}  // namespace coro::stdx
