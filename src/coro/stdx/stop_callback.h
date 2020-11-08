#ifndef CORO_HTTP_STOP_CALLBACK_H
#define CORO_HTTP_STOP_CALLBACK_H

#include "stop_source.h"

namespace stdx {

template <typename C>
class stop_callback : private internal::base_stop_callback {
 public:
  template <typename Callable>
  stop_callback(stop_token&& stop_token, Callable&& callable)
      : stop_token_(std::move(stop_token)),
        callable_(std::forward<Callable>(callable)) {
    if (stop_token_.stop_possible()) {
      if (stop_token_.stop_requested()) {
        callable_();
      } else {
        stop_token_.state_->stop_callback.insert(this);
      }
    }
  }

  template <typename Callable>
  stop_callback(const stop_token& stop_token, Callable&& callable)
      : stop_callback(stdx::stop_token{stop_token},
                      std::forward<Callable>(callable)) {}

  ~stop_callback() {
    if (stop_token_.stop_possible()) {
      auto it = stop_token_.state_->stop_callback.find(this);
      if (it != std::end(stop_token_.state_->stop_callback)) {
        stop_token_.state_->stop_callback.erase(it);
      }
    }
  }

  stop_callback(const stop_callback&) = delete;
  stop_callback(stop_callback&&) = delete;

  stop_callback& operator=(const stop_callback&) = delete;
  stop_callback& operator=(stop_callback&&) = delete;

 private:
  void operator()() override { callable_(); }

  stop_token stop_token_;
  C callable_;
};

template <typename C>
stop_callback(stop_token, C) -> stop_callback<C>;

}  // namespace coro

#endif  // CORO_HTTP_STOP_CALLBACK_H
