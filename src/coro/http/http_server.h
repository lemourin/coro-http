#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/event_struct.h>
#include <event2/listener.h>

#include <memory>
#include <span>
#include <sstream>
#include <vector>

#include "coro/http/http.h"
#include "coro/http/http_parse.h"
#include "coro/promise.h"
#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_source.h"
#include "coro/task.h"
#include "coro/util/function_traits.h"
#include "coro/util/raii_utils.h"

namespace coro::http {

template <typename T>
concept Handler = requires(T v, Request<> request,
                           stdx::stop_token stop_token) {
  { v(std::move(request), stop_token).await_resume() } -> ResponseLike;
};

template <typename T>
concept HasQuit = requires(T v) {
  { v.Quit() } -> Awaitable<void>;
};

struct HttpServerConfig {
  std::string address;
  uint16_t port;
};

namespace internal {

inline constexpr int kMaxLineLength = 16192;
inline constexpr int kMaxHeaderCount = 128;

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

struct EvconnListenerDeleter {
  void operator()(evconnlistener* listener) const {
    if (listener) {
      evconnlistener_free(listener);
    }
  }
};

struct BufferEventDeleter {
  void operator()(bufferevent* bev) const {
    if (bev) {
      bufferevent_free(bev);
    }
  }
};

Generator<std::string> GetBodyGenerator(struct bufferevent* bev,
                                        RequestContextBase* context);
Generator<std::string> GetBodyGenerator(std::string chunk);

Task<> Wait(RequestContextBase* context);

Task<> Write(RequestContextBase* context, bufferevent* bev,
             std::string_view chunk);

void Check(int code);

std::unique_ptr<evconnlistener, EvconnListenerDeleter> CreateListener(
    event_base* event_loop, evconnlistener_cb cb, void* userdata,
    const HttpServerConfig& config);

std::unique_ptr<bufferevent, BufferEventDeleter> CreateBufferEvent(
    event_base* event_loop, evutil_socket_t fd, RequestContextBase* context);

bool HasBody(int response_status, std::optional<int64_t> content_length);

bool IsChunked(std::span<std::pair<std::string, std::string>> headers);

std::string GetHeader(int response_status,
                      std::span<std::pair<std::string, std::string>> headers);

std::string GetChunk(std::string_view chunk);

uint16_t GetPort(evconnlistener*);

template <Handler HandlerType>
class HttpServer {
 public:
  template <typename... Args>
  HttpServer(event_base* event_loop, const HttpServerConfig& config,
             Args&&... args)
      : event_loop_(event_loop),
        listener_(CreateListener(event_loop, EvListenerCallback, this, config)),
        on_request_(std::forward<Args>(args)...) {
    Check(event_assign(&quit_event_, event_loop, -1, 0, OnQuit, this));
  }

  ~HttpServer() { Check(event_del(&quit_event_)); }

  HttpServer(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;

  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  uint16_t GetPort() const {
    return ::coro::http::internal::GetPort(listener_.get());
  }

  Task<> Quit() noexcept {
    if (quitting_) {
      co_return;
    }
    quitting_ = true;
    stop_source_.request_stop();
    if (current_connections_ == 0) {
      evuser_trigger(&quit_event_);
    }
    if constexpr (HasQuit<HandlerType>) {
      co_await on_request_.Quit();
    }
    co_await quit_semaphore_;
  }

 private:
  using HandlerArgumentList = util::ArgumentListTypeT<HandlerType>;
  static_assert(util::TypeListLengthV<HandlerArgumentList> == 2);

  using RequestType = util::TypeAtT<HandlerArgumentList, 0>;
  using ResponseType = typename util::ReturnTypeT<HandlerType>::type;

  struct RequestContext : RequestContextBase {
    std::optional<ResponseType> response;
  };

  Task<ResponseType> GetResponse(RequestContext* context, bufferevent* bev) {
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
    co_return co_await on_request_(
        RequestType{.url = std::move(context->url),
                    .method = context->method,
                    .headers = std::move(context->headers),
                    .body = std::move(context->body)},
        context->stop_source.get_token());
  }

  static bool IsInvalidStage(typename RequestContext::Stage stage) {
    return stage != RequestContext::Stage::kBody &&
           stage != RequestContext::Stage::kInvalid;
  }

  static Task<> FlushInput(RequestContext* context, bufferevent* bev) {
    FOR_CO_AWAIT(std::string & chunk, GetBodyGenerator(bev, context)) {
      (void)chunk;
    }
  }

  Task<> WriteMessage(RequestContext* context, bufferevent* bev, int status,
                      std::string_view message) {
    if (status < 100 || status >= 600) {
      status = 500;
    }
    if (!IsInvalidStage(context->stage)) {
      co_await FlushInput(context, bev);
    }
    std::vector<std::pair<std::string, std::string>> headers{
        {"Content-Length", std::to_string(message.size())},
        {"Connection",
         IsInvalidStage(context->stage) ? "close" : "keep-alive"}};
    co_await Write(context, bev, GetHeader(status, headers));
    if (context->method != Method::kHead &&
        HasBody(status, /*content_length=*/std::nullopt)) {
      co_await Write(context, bev, message);
    }
  }

  Task<bool> HandleRequest(RequestContext* context, bufferevent* bev) {
    std::optional<int> error_status;
    std::optional<std::string> error_message;
    try {
      context->response = co_await GetResponse(context, bev);
    } catch (const HttpException& e) {
      error_status = e.status();
      error_message = e.what();
    } catch (const std::exception& e) {
      error_status = 500;
      error_message = e.what();
    }

    if (error_message) {
      co_await WriteMessage(context, bev, *error_status, *error_message);
      co_return IsInvalidStage(context->stage);
    }

    bool is_chunked = IsChunked(context->response->headers);
    bool has_body = HasBody(context->response->status, context->content_length);
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
    } catch (const std::exception& e) {
      error_message = e.what();
    }
    if (error_message) {
      co_await Write(context, bev, chunk_to_send(*error_message));
    }
    if (is_chunked) {
      if (!error_message) {
        co_await FlushInput(context, bev);
      }
      co_await Write(context, bev, "0\r\n\r\n");
    }
    co_return error_message;
  }

  Task<> ListenerCallback(struct evconnlistener*, evutil_socket_t fd,
                          struct sockaddr*, int socklen) noexcept {
    RequestContext context{};
    try {
      if (quitting_) {
        co_return;
      }
      current_connections_++;
      auto scope_guard = util::AtScopeExit([&] {
        current_connections_--;
        if (quitting_ && current_connections_ == 0) {
          evuser_trigger(&quit_event_);
        }
      });
      stdx::stop_callback stop_callback1(stop_source_.get_token(), [&] {
        context.stop_source.request_stop();
      });
      stdx::stop_callback stop_callback2(context.stop_source.get_token(), [&] {
        context.semaphore.SetException(HttpException(HttpException::kAborted));
      });
      auto bev = CreateBufferEvent(event_loop_, fd, &context);
      while (!co_await HandleRequest(&context, bev.get())) {
      }
    } catch (...) {
      context.stop_source.request_stop();
    }
  }

  static void EvListenerCallback(struct evconnlistener* listener,
                                 evutil_socket_t socket, struct sockaddr* addr,
                                 int socklen, void* d) {
    RunTask(reinterpret_cast<HttpServer*>(d)->ListenerCallback(listener, socket,
                                                               addr, socklen));
  }

  static void OnQuit(evutil_socket_t, short, void* handle) {
    auto http_server = reinterpret_cast<HttpServer*>(handle);
    http_server->listener_.reset();
    http_server->quit_semaphore_.SetValue();
  }

  event_base* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  event quit_event_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<evconnlistener, EvconnListenerDeleter> listener_;
  HandlerType on_request_;
};

}  // namespace internal

template <Handler HandlerType>
class HttpServer : public internal::HttpServer<HandlerType> {
 public:
  template <typename... Args>
  HttpServer(event_base* event_loop, const HttpServerConfig& config,
             Args&&... args)
      : internal::HttpServer<HandlerType>(event_loop, config,
                                          std::forward<Args>(args)...) {}
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
