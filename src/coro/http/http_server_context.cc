#include "coro/http/http_server_context.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>

#include "coro/stdx/stop_callback.h"
#include "coro/util/raii_utils.h"
#include "coro/util/regex.h"

namespace coro::http::internal {

namespace {

namespace re = util::re;

constexpr int kMaxLineLength = 16192;
constexpr int kMaxHeaderCount = 128;

struct BufferEventDeleter {
  void operator()(bufferevent* bev) const {
    if (bev) {
      bufferevent_free(bev);
    }
  }
};

struct RequestContextBase {
  std::string url;
  Method method;
  std::vector<std::pair<std::string, std::string>> headers;
  std::optional<Generator<std::string>> body;
  std::optional<int64_t> content_length;
  std::optional<int64_t> current_chunk_length;
  int64_t read_count;
  Promise<void> semaphore;
  stdx::stop_source stop_source;
  enum class Stage { kUrl, kHeaders, kBody, kInvalid } stage = Stage::kUrl;
};

struct RequestContext : RequestContextBase {
  std::optional<Response<>> response;
};

struct FreeDeleter {
  void operator()(char* d) const {
    if (d) {
      free(d);  // NOLINT
    }
  }
};

void Check(int code) {
  if (code != 0) {
    throw HttpException(code, "http server error");
  }
}

bool IsInvalidStage(RequestContext::Stage stage) {
  return stage != RequestContext::Stage::kBody &&
         stage != RequestContext::Stage::kInvalid;
}

Task<> Wait(RequestContextBase* context) {
  if (context->stop_source.get_token().stop_requested()) {
    throw HttpException(HttpException::kAborted);
  } else {
    co_await context->semaphore;
  }
}

std::string GetHtmlErrorMessage(std::string_view what,
                                const std::string* formatted_stacktrace) {
  std::stringstream stream;
  stream << what;
  if (formatted_stacktrace) {
    stream << "<br><br>Stacktrace:<br>" << *formatted_stacktrace;
  }
  return std::move(stream).str();
}

std::string GetErrorMessage(std::string_view what,
                            const std::string* stacktrace) {
  std::stringstream stream;
  stream << what;
  if (stacktrace) {
    stream << "\n\nStacktrace:\n" << *stacktrace;
  }
  return std::move(stream).str();
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

Task<> FlushInput(RequestContext* context, bufferevent* bev) {
  FOR_CO_AWAIT(std::string & chunk, GetBodyGenerator(bev, context)) {
    (void)chunk;
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

Task<Response<>> GetResponse(const HttpServerContext::OnRequest& on_request,
                             RequestContext* context, bufferevent* bev) {
  context->url.clear();
  context->method = Method::kGet;
  context->headers.clear();
  context->body.reset();
  context->semaphore = Promise<void>();
  context->stage = RequestContext::Stage::kUrl;
  context->content_length = std::nullopt;
  context->current_chunk_length = std::nullopt;
  context->read_count = 0;
  Check(bufferevent_enable(bev, EV_READ));
  Check(bufferevent_disable(bev, EV_WRITE));
  co_await Wait(context);
  if (HasHeader(context->headers, "Expect", "100-continue")) {
    co_await Write(context, bev, "HTTP/1.1 100 Continue\r\n\r\n");
  }
  co_return co_await on_request(
      Request<>{.url = std::move(context->url),
                .method = context->method,
                .headers = std::move(context->headers),
                .body = std::move(context->body)},
      context->stop_source.get_token());
}

Task<> WriteMessage(RequestContext* context, bufferevent* bev, int status,
                    std::string_view message) {
  if (status < 100 || status >= 600) {
    status = 500;
  }
  if (!IsInvalidStage(context->stage)) {
    co_await FlushInput(context, bev);
  }
  std::stringstream stream;
  stream << "<!DOCTYPE html>"
            "<html>"
            "<body>"
         << message
         << "</body>"
            "</html>";
  std::string data = std::move(stream).str();
  std::vector<std::pair<std::string, std::string>> headers{
      {"Content-Type", "text/html; charset=UTF-8"},
      {"Content-Length", std::to_string(data.size())},
      {"Connection", IsInvalidStage(context->stage) ? "close" : "keep-alive"}};
  co_await Write(context, bev, GetHeader(status, headers));
  if (context->method != Method::kHead &&
      HasBody(status, /*content_length=*/std::nullopt)) {
    co_await Write(context, bev, data);
  }
}

Task<bool> HandleRequest(const HttpServerContext::OnRequest& on_request,
                         RequestContext* context, bufferevent* bev) {
  std::optional<int> error_status;
  std::optional<std::string> error_message;
  std::optional<std::string> stacktrace;
  std::optional<std::string> html_stacktrace;
  try {
    context->response = co_await GetResponse(on_request, context, bev);
  } catch (const HttpException& e) {
    error_status = e.status();
    error_message = e.what();
    if (std::string trace{e.stacktrace()}; !trace.empty()) {
      stacktrace = std::move(trace);
      html_stacktrace = e.html_stacktrace();
    }
  } catch (const Exception& e) {
    error_status = 500;
    error_message = e.what();
    if (std::string trace{e.stacktrace()}; !trace.empty()) {
      stacktrace = std::move(trace);
      html_stacktrace = e.html_stacktrace();
    }
  } catch (const std::exception& e) {
    error_status = 500;
    error_message = e.what();
  }

  if (error_message) {
    co_await WriteMessage(
        context, bev, *error_status,
        GetHtmlErrorMessage(*error_message,
                            html_stacktrace ? &*html_stacktrace : nullptr));
    co_return IsInvalidStage(context->stage);
  }

  bool is_chunked = IsChunked(context->response->headers);
  bool has_body = HasBody(context->response->status, context->content_length);
  bool is_html = http::GetHeader(context->response->headers, "Content-Type")
                     .value_or("")
                     .find("text/html") != std::string::npos;
  if (context->method == Method::kHead || !has_body) {
    co_await FlushInput(context, bev);
  }

  if (is_chunked && has_body) {
    context->response->headers.emplace_back("Transfer-Encoding", "chunked");
  }
  context->response->headers.emplace_back("Connection", "keep-alive");
  co_await Write(
      context, bev,
      GetHeader(context->response->status, context->response->headers));

  if (context->method == Method::kHead || !has_body) {
    co_return false;
  }

  auto chunk_to_send = [&](std::string_view chunk) {
    if (is_chunked) {
      return GetChunk(chunk);
    } else {
      return std::string(chunk);
    }
  };
  try {
    auto it = co_await context->response->body.begin();
    while (it != context->response->body.end()) {
      std::string chunk = std::move(*it);
      co_await ++it;
      if (it == context->response->body.end() && !is_chunked) {
        co_await FlushInput(context, bev);
      }
      co_await Write(context, bev, chunk_to_send(chunk));
    }
  } catch (const Exception& e) {
    error_message = e.what();
    if (std::string trace{e.stacktrace()}; !trace.empty()) {
      stacktrace = std::move(trace);
      html_stacktrace = e.html_stacktrace();
    }
  } catch (const std::exception& e) {
    error_message = e.what();
  }
  if (error_message) {
    co_await Write(
        context, bev,
        chunk_to_send(
            is_html ? GetHtmlErrorMessage(
                          *error_message,
                          html_stacktrace ? &*html_stacktrace : nullptr)
                    : GetErrorMessage(*error_message,
                                      stacktrace ? &*stacktrace : nullptr)));
  }
  if (is_chunked) {
    if (!error_message) {
      co_await FlushInput(context, bev);
    }
    co_await Write(context, bev, "0\r\n\r\n");
  }
  co_return error_message;
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

Task<> ListenerCallback(HttpServerContext* server_context,
                        struct evconnlistener*, evutil_socket_t fd,
                        struct sockaddr*, int socklen) noexcept {
  RequestContext context{};
  try {
    if (server_context->quitting()) {
      co_return;
    }
    server_context->IncreaseCurrentConnections();
    auto scope_guard = util::AtScopeExit([&] {
      server_context->DecreaseCurrentConnections();
      if (server_context->quitting() &&
          server_context->current_connections() == 0) {
        server_context->OnQuit();
      }
    });
    stdx::stop_callback stop_callback1(server_context->stop_token(), [&] {
      context.stop_source.request_stop();
    });
    stdx::stop_callback stop_callback2(context.stop_source.get_token(), [&] {
      context.semaphore.SetException(HttpException(HttpException::kAborted));
    });
    auto bev = CreateBufferEvent(reinterpret_cast<event_base*>(GetEventLoop(
                                     *server_context->event_loop())),
                                 fd, &context);
    while (true) {
      bool error = co_await HandleRequest(server_context->on_request(),
                                          &context, bev.get());
      context.response.reset();
      if (error) {
        break;
      }
    }
  } catch (...) {
    context.stop_source.request_stop();
  }
}

void EvListenerCallback(struct evconnlistener* listener, evutil_socket_t socket,
                        struct sockaddr* addr, int socklen, void* d) {
  auto* context = reinterpret_cast<HttpServerContext*>(d);
  RunTask(ListenerCallback(context, listener, socket, addr, socklen));
}

std::unique_ptr<EvconnListener, EvconnListenerDeleter> CreateListener(
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
  return std::unique_ptr<EvconnListener, EvconnListenerDeleter>(
      reinterpret_cast<EvconnListener*>(listener));
}

std::unique_ptr<EvconnListener, EvconnListenerDeleter> CreateListener(
    HttpServerContext* context, const HttpServerConfig& config) {
  return CreateListener(
      reinterpret_cast<event_base*>(GetEventLoop(*context->event_loop())),
      EvListenerCallback, context, config);
}

}  // namespace

void EvconnListenerDeleter::operator()(EvconnListener* listener) const {
  if (listener) {
    evconnlistener_free(reinterpret_cast<evconnlistener*>(listener));
  }
}

HttpServerContext::HttpServerContext(const coro::util::EventLoop* event_loop,
                                     const HttpServerConfig& config,
                                     OnRequest on_request)
    : event_loop_(event_loop),
      on_request_(std::move(on_request)),
      listener_(CreateListener(this, config)) {}

uint16_t HttpServerContext::GetPort() const {
  sockaddr_in addr;
  socklen_t length = sizeof(addr);
  Check(getsockname(
      evconnlistener_get_fd(reinterpret_cast<evconnlistener*>(listener_.get())),
      reinterpret_cast<sockaddr*>(&addr), &length));
  return ntohs(addr.sin_port);
}

void HttpServerContext::OnQuit() {
  event_loop_->RunOnEventLoop([this] {
    listener_.reset();
    quit_semaphore_.SetValue();
  });
}

Task<> HttpServerContext::Quit(Task<> on_quit) {
  if (quitting_) {
    co_return;
  }
  stop_source_.request_stop();
  quitting_ = true;
  if (current_connections_ == 0) {
    OnQuit();
  }
  co_await std::move(on_quit);
  co_await quit_semaphore_;
}

}  // namespace coro::http::internal
