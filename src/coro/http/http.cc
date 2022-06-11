#include "coro/http/http.h"

#include "coro/http/http_exception.h"

namespace coro::http {

std::string_view MethodToString(Method method) {
  switch (method) {
    case Method::kGet:
      return "GET";
    case Method::kPost:
      return "POST";
    case Method::kPut:
      return "PUT";
    case Method::kHead:
      return "HEAD";
    case Method::kOptions:
      return "OPTIONS";
    case Method::kPatch:
      return "PATCH";
    case Method::kDelete:
      return "DELETE";
    case Method::kPropfind:
      return "PROPFIND";
    case Method::kProppatch:
      return "PROPPATCH";
    case Method::kMkcol:
      return "MKCOL";
    case Method::kMove:
      return "MOVE";
    case Method::kCopy:
      return "COPY";
    default:
      return "UNKNOWN";
  }
}

Task<std::string> GetBody(Generator<std::string> body) {
  std::string result;
  FOR_CO_AWAIT(std::string & piece, body) {
    result += std::move(piece);
    if (result.size() > 10 * 1024 * 1024) {
      throw HttpException(HttpException::kBadRequest, "body too large");
    }
  }
  co_return result;
}
Generator<std::string> CreateBody(std::string body) {
  co_yield std::move(body);
}

}  // namespace coro::http

namespace std {

namespace {

size_t CombineHash(size_t lhs, size_t rhs) {
  lhs ^= rhs + 0x9e3779b9 + (lhs << 6) + (lhs >> 2);
  return lhs;
}

}  // namespace

size_t hash<coro::http::Request<std::string>>::operator()(
    const coro::http::Request<std::string>& r) const {
  return CombineHash(std::hash<std::string>{}(r.url),
                     std::hash<std::optional<std::string>>{}(r.body));
}

}  // namespace std