#include "coro/rpc/rpc_exception.h"

namespace coro::rpc {

std::string RpcException::ToString(int status) {
  switch (status) {
    case kAborted:
      return "Aborted.";
    case kMalformedRequest:
      return "Malformed request.";
    default:
      return "Unknown.";
  }
}

}  // namespace coro::rpc