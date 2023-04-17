#ifndef CORO_UTIl_STOP_TOKEN_OR_H
#define CORO_UTIl_STOP_TOKEN_OR_H

#include <array>
#include <memory>
#include <utility>

#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_source.h"
#include "coro/stdx/stop_token.h"

namespace coro::util {

namespace internal {
struct StopTokenOrOnCancel {
  void operator()() const { stop_source->request_stop(); }
  stdx::stop_source* stop_source;
};
}  // namespace internal

template <int N>
class StopTokenOr {
 public:
  template <typename... Args>
  explicit StopTokenOr(Args&&... args)
      : stop_callbacks_{{{std::forward<Args>(args),
                          internal::StopTokenOrOnCancel{&stop_source_}}...}} {}

  template <typename... Args>
  explicit StopTokenOr(stdx::stop_source stop_source, Args&&... args)
      : stop_source_(std::move(stop_source)),
        stop_callbacks_{{{std::forward<Args>(args),
                          internal::StopTokenOrOnCancel{&stop_source_}}...}} {}

  stdx::stop_token GetToken() const noexcept {
    return stop_source_.get_token();
  }

 private:
  stdx::stop_source stop_source_;
  std::array<stdx::stop_callback<internal::StopTokenOrOnCancel>, N>
      stop_callbacks_;
};

template <typename... Args>
auto MakeUniqueStopTokenOr(Args&&... stop_token) {
  return std::make_unique<StopTokenOr<sizeof...(Args)>>(
      std::forward<Args>(stop_token)...);
}

template <typename... Args>
auto MakeStopTokenOr(Args&&... stop_token) {
  return StopTokenOr<sizeof...(Args)>(std::forward<Args>(stop_token)...);
}

}  // namespace coro::util

#endif  // CORO_UTIL_STOP_TOKEN_OR_H