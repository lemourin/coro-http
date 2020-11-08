#ifndef CORO_HTTP_STOP_TOKEN_H
#define CORO_HTTP_STOP_TOKEN_H

#include <memory>

namespace stdx {

namespace internal {
struct stop_source_state;
}

class stop_token {
 public:
  stop_token() noexcept = default;

  [[nodiscard]] bool stop_requested() const noexcept;
  [[nodiscard]] bool stop_possible() const noexcept;

 private:
  friend class stop_source;
  template <typename C>
  friend class stop_callback;

  explicit stop_token(
      std::shared_ptr<internal::stop_source_state> stop_state) noexcept;

  std::shared_ptr<internal::stop_source_state> state_;
};

}  // namespace stdx

#endif  // CORO_HTTP_STOP_TOKEN_H
