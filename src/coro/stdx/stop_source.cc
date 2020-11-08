#include "stop_source.h"

namespace stdx {

stop_source::stop_source()
    : state_(std::make_shared<internal::stop_source_state>()) {}

bool stop_source::request_stop() noexcept {
  state_->stopped = true;

  while (!state_->stop_callback.empty()) {
    auto cb = *state_->stop_callback.begin();
    state_->stop_callback.erase(state_->stop_callback.begin());
    (*cb)();
  }

  return true;
}

stop_token stop_source::get_token() const noexcept {
  return stop_token{state_};
}

}  // namespace coro