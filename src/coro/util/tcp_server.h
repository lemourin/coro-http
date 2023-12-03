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

using TcpRequestDataProvider =
    stdx::any_invocable<Task<std::vector<uint8_t>>(uint32_t byte_cnt)>;

class TcpResponseChunk {
 public:
  TcpResponseChunk(std::vector<uint8_t> chunk) : chunk_(std::move(chunk)) {}
  TcpResponseChunk(std::string chunk) : chunk_(std::move(chunk)) {}

  std::span<const uint8_t> chunk() const;

 private:
  std::variant<std::vector<uint8_t>, std::string> chunk_;
};

using TcpRequestHandler = stdx::any_invocable<Generator<TcpResponseChunk>(
    TcpRequestDataProvider, stdx::stop_token)>;

class TcpServer {
 public:
  struct Config {
    std::string address;
    uint16_t port;
  };

  TcpServer(TcpRequestHandler request_handler, const EventLoop* event_loop,
            const Config& config);

  TcpServer(const TcpServer&) = delete;
  TcpServer(TcpServer&&) = delete;

  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer& operator=(TcpServer&&) = delete;

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
      const Config& config);
  Task<> ListenerCallback(EvconnListener*, socket_t fd, void* sockaddr,
                          int socklen) noexcept;
  void OnQuit();

  TcpRequestHandler request_handler_;
  const coro::util::EventLoop* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<EvconnListener, EvconnListenerDeleter> listener_;
};

Task<> DrainTcpDataProvider(TcpRequestDataProvider);

}  // namespace coro::util

#endif  // CORO_UTIL_BASE_SERVER_H