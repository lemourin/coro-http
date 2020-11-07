#ifndef CORO_HTTP_STOP_SOURCE_H
#define CORO_HTTP_STOP_SOURCE_H

#include <coro/stop_token.h>

namespace coro {

class stop_source {
 public:
  stop_source(): state_(std::make_shared<std::atomic_bool>(false)) {}

  bool request_stop() noexcept {
    *state_ = true;
    return true;
  }

  [[nodiscard]] stop_token get_token() const noexcept {
    return stop_token{state_};
  }

 private:
  std::shared_ptr<std::atomic_bool> state_;
};

}

#endif  // CORO_HTTP_STOP_SOURCE_H
