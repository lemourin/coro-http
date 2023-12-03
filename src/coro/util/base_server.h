#ifndef CORO_UTIL_BASE_SERVER_H
#define CORO_UTIL_BASE_SERVER_H

#include <span>
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

class BaseResponseChunk {
 public:
  BaseResponseChunk(std::vector<uint8_t> chunk) : chunk_(std::move(chunk)) {}
  BaseResponseChunk(std::string chunk) : chunk_(std::move(chunk)) {}

  std::span<const uint8_t> chunk() const {
    if (auto* chunk = std::get_if<std::string>(&chunk_)) {
      return std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(chunk->data()), chunk->size());
    } else {
      return std::get<std::vector<uint8_t>>(chunk_);
    }
  }

 private:
  std::variant<std::vector<uint8_t>, std::string> chunk_;
};

using BaseRequestHandler = stdx::any_invocable<Generator<BaseResponseChunk>(
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

  BaseRequestHandler& request_handler() { return request_handler_; }
  const coro::util::EventLoop* event_loop() const { return event_loop_; }
  bool quitting() const { return quitting_; }
  int current_connections() const { return current_connections_; }
  stdx::stop_token stop_token() const { return stop_source_.get_token(); }
  uint16_t port() const;

  void IncreaseCurrentConnections() { current_connections_++; }
  void DecreaseCurrentConnections() { current_connections_--; }

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