#ifndef CORO_UTIl_STOP_TOKEN_OR_H
#define CORO_UTIl_STOP_TOKEN_OR_H

#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_token.h"

namespace coro::util {

class StopTokenOr {
 public:
  StopTokenOr(stdx::stop_source stop_source, stdx::stop_token fst_token,
              stdx::stop_token nd_token)
      : stop_source_(std::move(stop_source)),
        callback_fst_(std::move(fst_token), Callback{&stop_source_}),
        callback_nd_(std::move(nd_token), Callback{&stop_source_}) {}

  StopTokenOr(stdx::stop_token fst_token, stdx::stop_token nd_token)
      : StopTokenOr(stdx::stop_source(), std::move(fst_token),
                    std::move(nd_token)) {}

  stdx::stop_token GetToken() const noexcept {
    return stop_source_.get_token();
  }

 private:
  struct Callback {
    void operator()() { stop_source->request_stop(); }
    stdx::stop_source* stop_source;
  };
  stdx::stop_source stop_source_;
  stdx::stop_callback<Callback> callback_fst_;
  stdx::stop_callback<Callback> callback_nd_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_STOP_TOKEN_OR_H