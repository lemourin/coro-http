#include "coro/http/http_server.h"

#include <sstream>
#include <string_view>
#include <vector>

#include "coro/http/http_parse.h"
#include "coro/util/base_server.h"
#include "coro/util/regex.h"

namespace coro::http {

namespace {

using ::coro::util::BaseRequestDataProvider;
using ::coro::util::BaseResponseChunk;
using ::coro::util::BaseServer;
using ::coro::util::EventLoop;
using ::coro::util::ServerConfig;

constexpr int kMaxHeaderSize = 16384;

namespace re = coro::util::re;

std::string ToString(std::span<const uint8_t> bytes) {
  return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<uint8_t> ToByteArray(std::string_view bytes) {
  auto* data = reinterpret_cast<const uint8_t*>(bytes.data());
  return std::vector<uint8_t>(data, data + bytes.size());
}

Generator<std::string> WrapGenerator(
    std::optional<Generator<std::string>>& wrapped) {
  FOR_CO_AWAIT(std::string chunk, *wrapped) { co_yield std::move(chunk); }
  wrapped = std::nullopt;
}

bool HasBody(int response_status, std::optional<uint64_t> content_length) {
  return (response_status / 100 != 1 && response_status != 204 &&
          response_status != 304) ||
         (content_length && *content_length > 0);
}

bool IsChunked(std::span<const std::pair<std::string, std::string>> headers) {
  return !GetHeader(headers, "Content-Length").has_value();
}

Task<std::string> GetHttpHeader(BaseRequestDataProvider& provider) {
  std::string buffer;
  while (!buffer.ends_with("\r\n\r\n")) {
    if (buffer.size() >= kMaxHeaderSize) {
      throw HttpException(HttpException::kBadRequest, "HTTP header too large");
    }
    std::vector<uint8_t> chunk = co_await provider(1);
    buffer.push_back(static_cast<char>(chunk[0]));
  }
  co_return buffer;
}

std::string GetHttpResponseHeader(
    int response_status,
    std::span<const std::pair<std::string, std::string>> headers) {
  std::stringstream header;
  header << "HTTP/1.1 " << response_status << " "
         << ToStatusString(response_status) << "\r\n";
  for (const auto& [key, value] : headers) {
    header << key << ": " << value << "\r\n";
  }
  header << "\r\n";
  return std::move(header).str();
}

Generator<std::string> GetRequestBody(BaseRequestDataProvider& provider,
                                      uint64_t content_length) {
  while (content_length > 0) {
    auto chunk_length = static_cast<uint32_t>(std::min(
        content_length, static_cast<uint64_t>(coro::util::kMaxBufferSize)));
    co_yield ToString(co_await provider(chunk_length));
    content_length -= chunk_length;
  }
}

Generator<std::string> GetChunkedRequestBody(
    BaseRequestDataProvider& provider) {
  while (true) {
    std::string buffer;
    while (!buffer.ends_with("\r\n")) {
      buffer.push_back(static_cast<char>((co_await provider(1))[0]));
      if (buffer.size() >= 8) {
        throw HttpException(HttpException::kBadRequest, "too big chunk length");
      }
    }
    uint64_t chunk_length =
        std::stoull(buffer.data(), /*pos=*/nullptr, /*base=*/16);
    bool last_chunk = chunk_length == 0;
    while (chunk_length > 0) {
      auto piece_length = static_cast<uint32_t>(std::min(
          chunk_length, static_cast<uint64_t>(coro::util::kMaxBufferSize)));
      co_yield ToString(co_await provider(piece_length));
      chunk_length -= piece_length;
    }
    if (ToString(co_await provider(2)) != "\r\n") {
      throw HttpException(HttpException::kBadRequest,
                          "invalid chunk delimiter");
    }
    if (last_chunk) {
      break;
    }
  }
}

std::optional<Generator<std::string>> GetHttpRequestBody(
    BaseRequestDataProvider& provider,
    std::span<const std::pair<std::string, std::string>> headers) {
  auto transfer_encoding = GetHeader(headers, "Transfer-Encoding");
  if (transfer_encoding &&
      transfer_encoding->find("chunked") != std::string::npos) {
    return GetChunkedRequestBody(provider);
  } else if (auto content_length = GetHeader(headers, "Content-Length")) {
    return GetRequestBody(provider, std::stoull(*content_length));
  } else {
    return std::nullopt;
  }
}

Request<> GetHttpRequest(std::string_view http_header) {
  Request<> request{};
  size_t idx = 0;
  while (idx < http_header.size()) {
    size_t len = 0;
    while (idx + len < http_header.size() &&
           !std::string_view(http_header.data() + idx, len).ends_with("\r\n")) {
      len++;
    }
    std::string_view line(http_header.data() + idx, len - strlen("\r\n"));
    if (idx == 0) {
      re::regex regex(R"(([A-Z]+) (\S+) HTTP\/1\.[01])");
      re::match_results<std::string_view::const_iterator> match;
      if (re::regex_match(line.begin(), line.end(), match, regex)) {
        request.method = ToMethod(match[1].str());
        request.url = match[2].str();
      } else {
        throw HttpException(HttpException::kBadRequest, "malformed url");
      }
    } else if (!line.empty()) {
      re::regex regex(R"((\S+):\s*(.+)$)");
      re::match_results<std::string_view::const_iterator> match;
      if (re::regex_match(line.begin(), line.end(), match, regex)) {
        request.headers.emplace_back(match[1], match[2]);
      } else {
        throw HttpException(HttpException::kBadRequest, "malformed header");
      }
    }
    idx += len;
  }
  return request;
}

Task<> DrainRequestBody(std::optional<Generator<std::string>>& body) {
  if (body) {
    FOR_CO_AWAIT(std::string_view chunk, *body) {}
    body = std::nullopt;
  }
}

Generator<std::string> GetResponseChunk(bool is_chunked, std::string chunk) {
  if (is_chunked) {
    std::string length = [&] {
      std::stringstream stream;
      stream << std::hex << chunk.size() << "\r\n";
      return std::move(stream).str();
    }();
    co_yield length;
    co_yield std::move(chunk);
    co_yield "\r\n";
  } else {
    co_yield std::move(chunk);
  }
}

struct HttpHandlerT {
  Generator<BaseResponseChunk> operator()(BaseRequestDataProvider provider,
                                          stdx::stop_token stop_token) {
    auto request = GetHttpRequest(co_await GetHttpHeader(provider));
    std::optional<Generator<std::string>> request_body =
        GetHttpRequestBody(provider, request.headers);
    if (request_body) {
      request.body = WrapGenerator(request_body);
    }
    if (HasHeader(request.headers, "Expect", "100-continue")) {
      co_yield std::string("HTTP/1.1 100 Continue\r\n\r\n");
    }
    auto method = request.method;
    auto response =
        co_await http_handler(std::move(request), std::move(stop_token));
    auto content_length = [&]() -> std::optional<uint64_t> {
      if (auto header = GetHeader(response.headers, "Content-Length")) {
        return std::stoull(*header);
      } else {
        return std::nullopt;
      }
    }();
    bool is_chunked = IsChunked(response.headers);
    bool has_body = HasBody(response.status, content_length);
    if (is_chunked && has_body) {
      response.headers.emplace_back("Transfer-Encoding", "chunked");
    }
    if (method == Method::kHead || !has_body) {
      co_await DrainRequestBody(request_body);
    }
    response.headers.emplace_back("Connection", "keep-alive");
    co_yield GetHttpResponseHeader(response.status, response.headers);

    if (method == Method::kHead || !has_body) {
      co_return;
    }

    auto it = co_await response.body.begin();
    while (it != response.body.end()) {
      std::string chunk = std::move(*it);
      co_await ++it;
      if (it == response.body.end() && !is_chunked) {
        co_await DrainRequestBody(request_body);
      }
      FOR_CO_AWAIT(std::string piece,
                   GetResponseChunk(is_chunked, std::move(chunk))) {
        co_yield std::move(piece);
      }
    }

    if (is_chunked) {
      co_await DrainRequestBody(request_body);
      co_yield std::string("0\r\n\r\n");
    }
  }

  HttpHandler http_handler;
};

}  // namespace

BaseServer CreateHttpServer(HttpHandler http_handler,
                            const EventLoop* event_loop,
                            const ServerConfig& config) {
  return BaseServer(HttpHandlerT{.http_handler = std::move(http_handler)},
                    event_loop, config);
}

}  // namespace coro::http
