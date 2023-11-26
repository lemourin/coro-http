#ifndef CORO_UTIL_BASE_SERVER_H
#define CORO_UTIL_BASE_SERVER_H

#include <variant>

#include "coro/generator.h"
#include "coro/promise.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/stop_token.h"
#include "coro/util/event_loop.h"

namespace coro::util {

inline constexpr uint32_t kMaxBufferSize = 1024;

using BaseRequestDataProvider =
    stdx::any_invocable<Task<std::vector<uint8_t>>(uint32_t byte_cnt)>;

Task<> DrainDataProvider(BaseRequestDataProvider);

struct BaseResponseFlowControl {
  enum class Type { kSendChunk, kTerminateConnection } type;
  std::vector<uint8_t> chunk;
};

using BaseRequestHandler =
    stdx::any_invocable<Generator<BaseResponseFlowControl>(
        BaseRequestDataProvider, stdx::stop_token)>;

struct ServerConfig {
  std::string address;
  uint16_t port;
};

struct EvconnListener;

struct EvconnListenerDeleter {
  void operator()(EvconnListener* listener) const noexcept;
};

class BaseServer {
 public:
  BaseServer(BaseRequestHandler request_handler,
             const coro::util::EventLoop* event_loop,
             const coro::util::ServerConfig& config);

  BaseServer(const BaseServer&) = delete;
  BaseServer(BaseServer&&) = delete;

  BaseServer& operator=(const BaseServer&) = delete;
  BaseServer& operator=(BaseServer&&) = delete;

  void OnQuit();
  Task<> Quit();

  void IncreaseCurrentConnections() { current_connections_++; }
  void DecreaseCurrentConnections() { current_connections_--; }

  BaseRequestHandler& request_handler() { return request_handler_; }
  const coro::util::EventLoop* event_loop() const { return event_loop_; }
  bool quitting() const { return quitting_; }
  int current_connections() const { return current_connections_; }
  stdx::stop_token stop_token() const { return stop_source_.get_token(); }

 private:
  BaseRequestHandler request_handler_;
  const coro::util::EventLoop* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<EvconnListener, EvconnListenerDeleter> listener_;
};

}  // namespace coro::util

#endif  // CORO_UTIL_BASE_SERVER_H