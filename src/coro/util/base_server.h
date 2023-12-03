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

class BaseResponseChunk {
 public:
  BaseResponseChunk(std::vector<uint8_t> chunk) : chunk_(std::move(chunk)) {}
  BaseResponseChunk(std::string chunk) : chunk_(std::move(chunk)) {}

  std::span<const uint8_t> chunk() const;

 private:
  std::variant<std::vector<uint8_t>, std::string> chunk_;
};

using BaseRequestHandler = stdx::any_invocable<Generator<BaseResponseChunk>(
    BaseRequestDataProvider, stdx::stop_token)>;

struct ServerConfig {
  std::string address;
  uint16_t port;
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

  uint16_t GetPort() const;
  Task<> Quit();

 private:
  struct EvconnListener;

  struct EvconnListenerDeleter {
    void operator()(EvconnListener* listener) const noexcept;
  };

#ifdef _WIN32
  using socket_t = intptr_t;
#else
  using socket_t = int;
#endif

  std::unique_ptr<EvconnListener, EvconnListenerDeleter> CreateListener(
      const ServerConfig& config);
  Task<> ListenerCallback(EvconnListener*, socket_t fd, void* sockaddr,
                          int socklen) noexcept;
  void OnQuit();

  BaseRequestHandler request_handler_;
  const coro::util::EventLoop* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<EvconnListener, EvconnListenerDeleter> listener_;
};

Task<> DrainDataProvider(BaseRequestDataProvider);

}  // namespace coro::util

#endif  // CORO_UTIL_BASE_SERVER_H