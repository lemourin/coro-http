#ifndef CORO_HTTP_HTTP_SERVER_CONTEXT_H_
#define CORO_HTTP_HTTP_SERVER_CONTEXT_H_

#include "coro/http/http.h"
#include "coro/promise.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/stop_source.h"
#include "coro/util/event_loop.h"

namespace coro::http {

struct HttpServerConfig {
  std::string address;
  uint16_t port;
};

namespace internal {

struct EvconnListener;

struct EvconnListenerDeleter {
  void operator()(EvconnListener* listener) const;
};

class HttpServerContext {
 public:
  using OnRequest =
      stdx::any_invocable<Task<Response<>>(Request<>, stdx::stop_token) const>;

  HttpServerContext(const coro::util::EventLoop*, const HttpServerConfig&,
                    OnRequest);

  uint16_t GetPort() const;

  void OnQuit();
  Task<> Quit(Task<> on_quit) noexcept;

  void IncreaseCurrentConnections() { current_connections_++; }
  void DecreaseCurrentConnections() { current_connections_--; }

  const coro::util::EventLoop* event_loop() const { return event_loop_; }
  bool quitting() const { return quitting_; }
  int current_connections() const { return current_connections_; }
  stdx::stop_token stop_token() const { return stop_source_.get_token(); }
  const OnRequest& on_request() const { return on_request_; }

 private:
  const coro::util::EventLoop* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  OnRequest on_request_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<EvconnListener, EvconnListenerDeleter> listener_;
};

}  // namespace internal

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_CONTEXT_H_
