#include "coro/rpc/rpc_server.h"

#include <iostream>
#include <iterator>
#include <span>
#include <string_view>

#include "coro/promise.h"
#include "coro/rpc/rpc_exception.h"
#include "coro/util/raii_utils.h"

namespace coro::rpc {

namespace {

using ::coro::util::AtScopeExit;
using ::coro::util::BaseRequestDataProvider;
using ::coro::util::BaseResponseChunk;
using ::coro::util::EvconnListener;
using ::coro::util::EvconnListenerDeleter;
using ::coro::util::ServerConfig;

constexpr int kMaxCredLength = 400;

enum class MessageType : int32_t { kCall = 0, kReply = 1 };
enum class ReplyStat : int32_t { kMsgAccepted = 0, kMsgDenied = 1 };

template <typename U>
U RoundUpPower2(U num, size_t bits) {
  return ((num + (static_cast<U>(1) << bits) - 1) >> bits) << bits;
}

BaseRequestDataProvider GetDecodedChunks(bool last_fragment, uint32_t length,
                                         BaseRequestDataProvider provider) {
  return [last_fragment, length, provider = std::move(provider)](
             uint32_t byte_cnt) mutable -> Task<std::vector<uint8_t>> {
    if (last_fragment && length == 0 && byte_cnt == UINT32_MAX) {
      co_return std::vector<uint8_t>{};
    }
    std::vector<uint8_t> buffer;
    if (byte_cnt != UINT32_MAX) {
      buffer.reserve(byte_cnt);
    }
    while (buffer.size() < byte_cnt) {
      if (length == 0) {
        if (last_fragment) {
          throw RpcException(RpcException::kMalformedRequest,
                             "buffer underflow");
        }
        uint32_t encoded_length = ParseUInt32(co_await provider(4));
        last_fragment = encoded_length & (1 << 31);
        length = encoded_length & ~(1 << 31);
      }
      uint32_t current_read = std::min(byte_cnt, length);
      std::vector<uint8_t> chunk = co_await provider(current_read);
      length -= current_read;
      if (byte_cnt == UINT32_MAX) {
        co_return chunk;
      }
      std::copy(chunk.begin(), chunk.end(), std::back_inserter(buffer));
    }
    co_return buffer;
  };
}

std::vector<uint8_t> GetChunkToSend(std::vector<uint8_t> data, bool last) {
  std::vector<uint8_t> output;
  output.reserve(4 + data.size());
  XdrSerializer{&output}.Put((last ? 1u << 31 : 0) |
                             static_cast<uint32_t>(data.size()));
  std::copy(data.begin(), data.end(), std::back_inserter(output));
  return output;
}

struct RpcHandlerT {
  Generator<BaseResponseChunk> operator()(
      coro::util::BaseRequestDataProvider provider,
      stdx::stop_token stop_token) {
    RpcRequest rpc_request{};
    uint32_t encoded_length = ParseUInt32(co_await provider(4));
    bool last_fragment = encoded_length & (1 << 31);
    uint32_t length = encoded_length & ~(1 << 31);

    rpc_request.xid = ParseUInt32(co_await provider(4));
    int32_t message_type = ParseInt32(co_await provider(4));
    if (message_type != 0) {
      throw RpcException(RpcException::kMalformedRequest,
                         "expected message_type = 0");
    }

    rpc_request.body.rpcvers = ParseUInt32(co_await provider(4));
    if (rpc_request.body.rpcvers != 2) {
      throw RpcException(RpcException::kMalformedRequest,
                         "expected rpcvers = 2");
    }
    rpc_request.body.prog = ParseUInt32(co_await provider(4));
    rpc_request.body.vers = ParseUInt32(co_await provider(4));
    rpc_request.body.proc = ParseUInt32(co_await provider(4));
    rpc_request.body.cred.flavor = ParseUInt32(co_await provider(4));
    rpc_request.body.cred.body =
        co_await GetVariableLengthOpaque(provider, kMaxCredLength);
    rpc_request.body.verf.flavor = ParseUInt32(co_await provider(4));
    rpc_request.body.verf.body =
        co_await GetVariableLengthOpaque(provider, kMaxCredLength);
    rpc_request.body.data = GetDecodedChunks(
        last_fragment,
        static_cast<uint32_t>(
            length -
            (4 * 10 + RoundUpPower2(rpc_request.body.cred.body.size(), 2) +
             RoundUpPower2(rpc_request.body.verf.body.size(), 2))),
        std::move(provider));
    uint32_t xid = rpc_request.xid;
    auto response =
        co_await rpc_handler(std::move(rpc_request), std::move(stop_token));
    std::vector<uint8_t> data;
    XdrSerializer serializer{&data};
    serializer.Put(xid).Put(MessageType::kReply);
    if (auto* accepted =
            std::get_if<RpcResponseAcceptedBody>(&response.body.body)) {
      serializer.Put(ReplyStat::kMsgAccepted);
      serializer.Put(accepted->verf.flavor);
      if (!accepted->verf.body.empty()) {
        throw RpcException(RpcException::kAborted, "unimplemented");
      }
      serializer.Put(static_cast<uint32_t>(accepted->verf.body.size()));
      serializer.Put(accepted->stat);

      bool header_sent = false;
      std::vector<uint8_t> previous_chunk;
      FOR_CO_AWAIT(BaseResponseChunk ctl, accepted->data) {
        std::vector<uint8_t> chunk(ctl.chunk().begin(), ctl.chunk().end());
        if (!header_sent) {
          std::vector<uint8_t> tmp;
          tmp.reserve(data.size() + chunk.size());
          std::copy(data.begin(), data.end(), std::back_inserter(tmp));
          std::copy(chunk.begin(), chunk.end(), std::back_inserter(tmp));
          chunk = std::move(tmp);
          header_sent = true;
        }
        if (!previous_chunk.empty()) {
          co_yield GetChunkToSend(std::move(previous_chunk), /*last=*/false);
        }
        previous_chunk = std::move(chunk);
      }
      if (!header_sent) {
        previous_chunk = std::move(data);
      }
      if (!previous_chunk.empty()) {
        co_yield GetChunkToSend(std::move(previous_chunk), /*last=*/true);
      }
    } else {
      throw RpcException(RpcException::kAborted, "unimplemented");
    }
  }

  RpcHandler rpc_handler;
};

}  // namespace

XdrSerializer& XdrSerializer::Put(uint32_t value) {
  dest_->push_back((value >> 24) & 255);
  dest_->push_back((value >> 16) & 255);
  dest_->push_back((value >> 8) & 255);
  dest_->push_back(value & 255);
  return *this;
}

XdrSerializer& XdrSerializer::Put(uint64_t value) {
  dest_->push_back((value >> 56) & 255);
  dest_->push_back((value >> 48) & 255);
  dest_->push_back((value >> 40) & 255);
  dest_->push_back((value >> 32) & 255);
  dest_->push_back((value >> 24) & 255);
  dest_->push_back((value >> 16) & 255);
  dest_->push_back((value >> 8) & 255);
  dest_->push_back(value & 255);
  return *this;
}

XdrSerializer& XdrSerializer::Put(bool value) {
  Put(static_cast<uint32_t>(value));
  return *this;
}

XdrSerializer& XdrSerializer::Put(std::span<const uint8_t> bytes) {
  Put(static_cast<uint32_t>(bytes.size()));
  PutFixedSize(bytes);
  return *this;
}

XdrSerializer& XdrSerializer::PutFixedSize(std::span<const uint8_t> bytes) {
  std::copy(bytes.begin(), bytes.end(), std::back_inserter(*dest_));
  dest_->resize(dest_->size() + RoundUpPower2(bytes.size(), 2) - bytes.size());
  return *this;
}

XdrSerializer& XdrSerializer::Put(std::string_view bytes) {
  auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  return Put(std::span<const uint8_t>(data, data + bytes.length()));
}

Task<std::vector<uint8_t>> GetVariableLengthOpaque(
    coro::util::BaseRequestDataProvider& provider, uint32_t max_length) {
  uint32_t length = ParseUInt32(co_await provider(4));
  if (length > max_length) {
    throw RpcException(RpcException::kMalformedRequest,
                       "opaque length too long");
  }
  auto result = co_await provider(length);
  co_await provider(RoundUpPower2(length, 2) - length);
  co_return result;
}

coro::util::BaseServer CreateRpcServer(RpcHandler rpc_handler,
                                       const coro::util::EventLoop* event_loop,
                                       const coro::util::ServerConfig& config) {
  return coro::util::BaseServer(RpcHandlerT(std::move(rpc_handler)), event_loop,
                                config);
}

}  // namespace coro::rpc