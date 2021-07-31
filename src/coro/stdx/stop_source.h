#ifndef CORO_HTTP_STOP_SOURCE_H
#define CORO_HTTP_STOP_SOURCE_H

#include <unordered_set>

#include "coro/stdx/stop_token.h"

namespace coro::stdx {

namespace internal {

class base_stop_callback {
 public:
  virtual void operator()() = 0;
};

struct stop_source_state {
  bool stopped = false;
  std::unordered_set<base_stop_callback*> stop_callback;
};

}  // namespace internal

class stop_source {
 public:
  stop_source();

  bool request_stop() noexcept;
  [[nodiscard]] stop_token get_token() const noexcept;

 private:
  std::shared_ptr<internal::stop_source_state> state_;
};

}  // namespace coro::stdx

#endif  // CORO_HTTP_STOP_SOURCE_H
