#ifndef CORO_RPC_RPC_SERVER_H
#define CORO_RPC_RPC_SERVER_H

#include <span>
#include <string>
#include <variant>

#include "coro/promise.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/stop_token.h"
#include "coro/task.h"
#include "coro/util/event_loop.h"
#include "coro/util/tcp_server.h"

namespace coro::rpc {

struct RpcOpaqueAuth {
  uint32_t flavor;
  std::vector<uint8_t> body;
};

struct RpcRequestBody {
  uint32_t rpcvers;
  uint32_t prog;
  uint32_t vers;
  uint32_t proc;
  RpcOpaqueAuth cred;
  RpcOpaqueAuth verf;
  coro::util::TcpRequestDataProvider data;
};

struct RpcResponseAcceptedBody {
  RpcOpaqueAuth verf;
  enum class Stat {
    kSuccess = 0,
    kProgUnavail = 1,
    kProgMismatch = 2,
    kProcUnavail = 3,
    kGarbageArgs = 4,
    kSystemErr = 5,
  } stat;
  Generator<coro::util::TcpResponseChunk> data;
};

struct RpcResponseDeniedBody {
  enum class Stat { kRpcMismatch, kAuthError } stat;
};

struct RpcResponseBody {
  std::variant<RpcResponseAcceptedBody, RpcResponseDeniedBody> body;
};

struct RpcRequest {
  uint32_t xid;
  RpcRequestBody body;
};

struct RpcResponse {
  uint32_t xid;
  RpcResponseBody body;
};

using RpcHandler =
    stdx::any_invocable<Task<RpcResponse>(RpcRequest, stdx::stop_token)>;

class XdrSerializer {
 public:
  explicit XdrSerializer(std::vector<uint8_t>* dest) : dest_(dest) {}

  XdrSerializer& Put(uint32_t value);
  XdrSerializer& Put(uint64_t value);
  XdrSerializer& Put(bool value);
  template <typename T>
    requires(std::is_enum_v<T>)
  XdrSerializer& Put(T value) {
    Put(static_cast<uint32_t>(value));
    return *this;
  }
  template <typename T>
  XdrSerializer& Put(const std::optional<T>& value) {
    if (value) {
      Put(1u).Put(*value);
    } else {
      Put(0u);
    }
    return *this;
  }
  XdrSerializer& PutFixedSize(std::span<const uint8_t> value);
  XdrSerializer& Put(std::span<const uint8_t> bytes);
  XdrSerializer& Put(std::string_view bytes);

 private:
  std::vector<uint8_t>* dest_;
};

inline uint32_t ParseUInt32(std::span<const uint8_t> message) {
  return (uint32_t(message[0]) << 24) | (uint32_t(message[1]) << 16) |
         (uint32_t(message[2]) << 8) | message[3];
}

inline int32_t ParseInt32(std::span<const uint8_t> message) {
  return (-int32_t(message[0]) << 24) | (uint32_t(message[1]) << 16) |
         (uint32_t(message[2]) << 8) | message[3];
}

inline uint64_t ParseUInt64(std::span<const uint8_t> message) {
  return (static_cast<uint64_t>(ParseUInt32(message.subspan(0, 4))) << 32) |
         ParseUInt32(message.subspan(4, 4));
}

Task<std::vector<uint8_t>> GetVariableLengthOpaque(
    coro::util::TcpRequestDataProvider& data, uint32_t max_length);

coro::util::TcpServer CreateRpcServer(
    RpcHandler rpc_handler, const coro::util::EventLoop* event_loop,
    const coro::util::TcpServer::Config& config);

}  // namespace coro::rpc

#endif  // CORO_RPC_RPC_SERVER_H