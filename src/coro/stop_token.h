#ifndef CORO_HTTP_STOP_TOKEN_H
#define CORO_HTTP_STOP_TOKEN_H

#include <memory>

namespace coro {

class stop_token {
 public:
  stop_token() noexcept = default;

  [[nodiscard]] bool stop_requested() const noexcept {
    return stop_state_ && *stop_state_;
  }
  [[nodiscard]] bool stop_possible() const noexcept {
    return stop_state_ == nullptr;
  }

 private:
  friend class stop_source;

  explicit stop_token(std::shared_ptr<std::atomic_bool> stop_state) noexcept
      : stop_state_(std::move(stop_state)) {}

  std::shared_ptr<std::atomic_bool> stop_state_;
};

}  // namespace coro

#endif  // CORO_HTTP_STOP_TOKEN_H
