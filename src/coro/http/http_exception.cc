#include "coro/http/http_exception.h"

namespace coro::http {

std::string HttpException::ToString(int status) {
  switch (status) {
    case kAborted:
      return "Aborted.";
    case kNotFound:
      return "Not found.";
    case kMalformedResponse:
      return "Malformed response.";
    case kInvalidMethod:
      return "Invalid method.";
    case kBadRequest:
      return "Bad request.";
    case kRangeNotSatisfiable:
      return "Range not satisfiable.";
    case kRequestHeaderFieldsTooLarge:
      return "Request Header Fields Too Large.";
    default:
      return "Unknown.";
  }
}


}  // namespace coro::http
