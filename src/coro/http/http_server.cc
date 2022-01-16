#include "coro/http/http_server.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <algorithm>
#include <string>
#include <utility>

#include "coro/util/regex.h"

namespace coro::http::internal {

namespace {

namespace re = util::re;

struct FreeDeleter {
  void operator()(char* d) const {
    if (d) {
      free(d);  // NOLINT
    }
  }
};

void WriteCallback(struct bufferevent*, void* user_data) {
  auto* context = reinterpret_cast<RequestContextBase*>(user_data);
  context->semaphore.SetValue();
}

void EventCallback(struct bufferevent*, short events, void* user_data) {
  auto* context = reinterpret_cast<RequestContextBase*>(user_data);
  if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
    context->stop_source.request_stop();
  }
}

void ReadCallback(struct bufferevent* bev, void* user_data) {
  auto* context = reinterpret_cast<RequestContextBase*>(user_data);
  struct evbuffer* input = bufferevent_get_input(bev);
  while (evbuffer_get_length(input) > 0) {
    if (context->stage == RequestContextBase::Stage::kUrl) {
      size_t length;
      std::unique_ptr<char, FreeDeleter> line(
          evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
      if (line) {
        re::regex regex(R"(([A-Z]+) (\S+) HTTP\/1\.[01])");
        std::string_view view(line.get(), length);
        re::match_results<std::string_view::const_iterator> match;
        if (re::regex_match(view.begin(), view.end(), match, regex)) {
          try {
            context->method = ToMethod(match[1].str());
          } catch (const HttpException&) {
            context->stage = RequestContextBase::Stage::kInvalid;
            context->semaphore.SetException(HttpException(501));
            return;
          }
          context->url = match[2];
          context->stage = RequestContextBase::Stage::kHeaders;
        } else {
          context->stage = RequestContextBase::Stage::kInvalid;
          return context->semaphore.SetException(
              HttpException(HttpException::kBadRequest, "malformed url"));
        }
      } else {
        context->stage = RequestContextBase::Stage::kInvalid;
        return context->semaphore.SetException(HttpException(414));
      }
    }
    if (context->stage == RequestContextBase::Stage::kHeaders) {
      size_t length;
      std::unique_ptr<char, FreeDeleter> line(
          evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
      if (line) {
        if (length == 0) {
          if (auto header =
                  http::GetHeader(context->headers, "Content-Length")) {
            context->content_length = std::stoll(*header);
          } else {
            context->content_length = 0;
          }
          for (const auto& [key, value] : context->headers) {
            if (ToLowerCase(key) == ToLowerCase("Transfer-Encoding") &&
                value.find("chunked") != std::string::npos) {
              context->content_length = std::nullopt;
              break;
            }
          }
          context->stage = RequestContextBase::Stage::kBody;
        } else {
          re::regex regex(R"((\S+):\s*(.+)$)");
          std::string_view view(line.get(), length);
          re::match_results<std::string_view::const_iterator> match;
          if (re::regex_match(view.begin(), view.end(), match, regex)) {
            context->headers.emplace_back(match[1], match[2]);
            if (context->headers.size() > kMaxHeaderCount) {
              context->stage = RequestContextBase::Stage::kInvalid;
              return context->semaphore.SetException(HttpException(
                  HttpException::kBadRequest, "too many headers"));
            }
          } else {
            context->stage = RequestContextBase::Stage::kInvalid;
            return context->semaphore.SetException(
                HttpException(HttpException::kBadRequest, "malformed header"));
          }
        }
      } else {
        context->stage = RequestContextBase::Stage::kInvalid;
        return context->semaphore.SetException(HttpException(431));
      }
    }
    if (context->stage == RequestContextBase::Stage::kBody) {
      if (!context->body) {
        context->body = GetBodyGenerator(bev, context);
      }
      context->semaphore.SetValue();
      break;
    }
  }
}

}  // namespace

void Check(int code) {
  if (code != 0) {
    throw HttpException(code, "http server error");
  }
}

Task<> Wait(RequestContextBase* context) {
  if (context->stop_source.get_token().stop_requested()) {
    throw HttpException(HttpException::kAborted);
  } else {
    co_await context->semaphore;
  }
}

Task<> Write(RequestContextBase* context, bufferevent* bev,
             std::string_view chunk) {
  Check(bufferevent_enable(bev, EV_WRITE));
  context->semaphore = Promise<void>();
  Check(bufferevent_write(bev, chunk.data(), chunk.size()));
  co_await Wait(context);
  Check(bufferevent_disable(bev, EV_WRITE));
}

bool HasBody(int response_status, std::optional<int64_t> content_length) {
  return (response_status / 100 != 1 && response_status != 204 &&
          response_status != 304) ||
         (content_length && *content_length > 0);
}

bool IsChunked(std::span<std::pair<std::string, std::string>> headers) {
  return !http::GetHeader(headers, "Content-Length").has_value();
}

std::string GetHeader(int response_status,
                      std::span<std::pair<std::string, std::string>> headers) {
  std::stringstream header;
  header << "HTTP/1.1 " << response_status << " "
         << ToStatusString(response_status) << "\r\n";
  for (const auto& [key, value] : headers) {
    header << key << ": " << value << "\r\n";
  }
  header << "\r\n";
  return std::move(header).str();
}

std::string GetChunk(std::string_view chunk) {
  std::stringstream stream;
  stream << std::hex << chunk.size() << "\r\n" << chunk << "\r\n";
  return std::move(stream).str();
}

std::unique_ptr<evconnlistener, EvconnListenerDeleter> CreateListener(
    event_base* event_loop, evconnlistener_cb cb, void* userdata,
    const HttpServerConfig& config) {
  union {
    struct sockaddr_in sin;
    struct sockaddr sockaddr;
  } d;
  memset(&d.sin, 0, sizeof(sockaddr_in));
  inet_pton(AF_INET, config.address.c_str(), &d.sin.sin_addr);
  d.sin.sin_family = AF_INET;
  d.sin.sin_port = htons(config.port);
  auto* listener = evconnlistener_new_bind(
      event_loop, cb, userdata, LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
      /*backlog=*/-1, &d.sockaddr, sizeof(sockaddr_in));
  if (listener == nullptr) {
    throw HttpException(HttpException::kUnknown, "http server error");
  }
  return std::unique_ptr<evconnlistener, EvconnListenerDeleter>(listener);
}

Generator<std::string> GetBodyGenerator(struct bufferevent* bev,
                                        RequestContextBase* context) {
  struct evbuffer* input = bufferevent_get_input(bev);
  if (!context->content_length) {
    try {
      while (!context->content_length) {
        while (!context->current_chunk_length) {
          size_t length;
          std::unique_ptr<char, FreeDeleter> line(
              evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
          if (line) {
            context->current_chunk_length =
                std::stoll(line.get(), /*pos=*/nullptr, /*base=*/16);
          } else {
            if (evbuffer_get_length(input) >= kMaxLineLength) {
              throw HttpException(HttpException::kBadRequest);
            }
            co_await Wait(context);
            context->semaphore = Promise<void>();
          }
        }
        bool terminated = context->current_chunk_length == 0;
        while (*context->current_chunk_length > 0) {
          if (evbuffer_get_length(input) == 0) {
            co_await Wait(context);
            context->semaphore = Promise<void>();
          }
          std::string buffer(
              std::min<size_t>(
                  evbuffer_get_length(input),
                  static_cast<size_t>(*context->current_chunk_length)),
              0);
          if (evbuffer_remove(input, buffer.data(), buffer.size()) !=
              buffer.size()) {
            throw HttpException(HttpException::kUnknown,
                                "evbuffer_remove failed");
          }
          *context->current_chunk_length -= static_cast<int64_t>(buffer.size());
          co_yield std::move(buffer);
        }
        while (true) {
          size_t length;
          std::unique_ptr<char, FreeDeleter> line(
              evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
          if (line) {
            if (length != 0) {
              throw HttpException(HttpException::kBadRequest);
            }
            break;
          } else {
            co_await Wait(context);
            context->semaphore = Promise<void>();
          }
        }
        context->current_chunk_length = std::nullopt;
        if (terminated) {
          context->content_length = 0;
        }
      }
    } catch (...) {
      context->stage = RequestContextBase::Stage::kInvalid;
      throw;
    }
  } else {
    while (context->read_count < *context->content_length) {
      if (evbuffer_get_length(input) == 0) {
        co_await Wait(context);
        context->semaphore = Promise<void>();
      }
      std::string buffer(
          std::min<size_t>(evbuffer_get_length(input),
                           static_cast<size_t>(*context->content_length -
                                               context->read_count)),
          0);
      if (evbuffer_remove(input, reinterpret_cast<void*>(buffer.data()),
                          buffer.size()) != buffer.size()) {
        throw HttpException(HttpException::kUnknown, "evbuffer_remove failed");
      }
      context->read_count += static_cast<int64_t>(buffer.size());
      co_yield std::move(buffer);
    }
  }
}

std::unique_ptr<bufferevent, BufferEventDeleter> CreateBufferEvent(
    event_base* event_loop, evutil_socket_t fd, RequestContextBase* context) {
  std::unique_ptr<bufferevent, BufferEventDeleter> bev(
      bufferevent_socket_new(event_loop, fd, BEV_OPT_CLOSE_ON_FREE));
  if (!bev) {
    throw HttpException(HttpException::kUnknown,
                        "bufferevent_socket_new failed");
  }
  bufferevent_setwatermark(bev.get(), EV_READ | EV_WRITE, 0,
                           2 * kMaxLineLength);
  bufferevent_setcb(bev.get(), ReadCallback, WriteCallback, EventCallback,
                    context);
  return bev;
}

}  // namespace coro::http::internal
