#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#ifndef WIN32
#include <arpa/inet.h>
#endif
#include <coro/http/http_parse.h>
#include <coro/promise.h>
#include <coro/stdx/stop_callback.h>
#include <coro/stdx/stop_source.h>
#include <coro/task.h>
#include <coro/util/function_traits.h>
#include <coro/util/raii_utils.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/listener.h>

#include <memory>
#include <regex>
#include <sstream>
#include <vector>

#include "http.h"

namespace coro::http {

// clang-format off
template <typename T>
concept Handler = requires (T v, Request<> request, stdx::stop_token stop_token) {
  { v(std::move(request), stop_token).await_resume() } -> ResponseLike;
};
// clang-format on

struct HttpServerConfig {
  std::string address;
  uint16_t port;
};

template <Handler HandlerType>
class HttpServer {
 public:
  HttpServer(event_base* event_loop, const HttpServerConfig& config,
             HandlerType on_request)
      : event_loop_(event_loop),
        listener_([&] {
          union {
            struct sockaddr_in sin;
            struct sockaddr sockaddr;
          } d;
          memset(&d.sin, 0, sizeof(sockaddr_in));
          inet_pton(AF_INET, config.address.c_str(), &d.sin.sin_addr);
          d.sin.sin_family = AF_INET;
          d.sin.sin_port = htons(config.port);
          auto listener = evconnlistener_new_bind(
              event_loop, EvListenerCallback, this,
              LEV_OPT_REUSEABLE | LEV_OPT_CLOSE_ON_FREE,
              /*backlog=*/-1, &d.sockaddr, sizeof(sockaddr_in));
          if (listener == nullptr) {
            throw HttpException(HttpException::kUnknown, "http server error");
          }
          return listener;
        }()),
        on_request_(std::move(on_request)) {
    Check(event_assign(&quit_event_, event_loop, -1, 0, OnQuit, this));
  }

  ~HttpServer() { Check(event_del(&quit_event_)); }

  HttpServer(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;

  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  Task<> Quit() noexcept {
    if (quitting_) {
      co_return;
    }
    quitting_ = true;
    stop_source_.request_stop();
    if (current_connections_ == 0) {
      evuser_trigger(&quit_event_);
    }
    co_await quit_semaphore_;
  }

 private:
  static inline constexpr int kMaxLineLength = 16192;

  using HandlerArgumentList = util::ArgumentListTypeT<HandlerType>;
  static_assert(util::TypeListLengthV<HandlerArgumentList> == 2);

  using RequestType =
      std::remove_cvref_t<util::TypeAtT<HandlerArgumentList, 0>>;
  using ResponseType = typename util::ReturnTypeT<HandlerType>::type;

  struct BufferEventDeleter {
    void operator()(bufferevent* bev) const {
      if (bev) {
        bufferevent_free(bev);
      }
    }
  };

  struct FreeDeleter {
    void operator()(char* d) const {
      if (d) {
        free(d);
      }
    }
  };

  struct RequestContext {
    RequestType request;
    std::optional<ResponseType> response;
    std::optional<int64_t> content_length;
    std::optional<int64_t> current_chunk_length;
    int64_t read_count;
    Promise<void> semaphore;
    stdx::stop_source stop_source;
    enum class Stage { kUrl, kHeaders, kBody, kInvalid } stage = Stage::kUrl;
  };

  static void Check(int code) {
    if (code != 0) {
      throw HttpException(code, "http server error");
    }
  }

  static Task<> Wait(RequestContext* context) {
    if (context->stop_source.get_token().stop_requested()) {
      throw HttpException(HttpException::kAborted);
    } else {
      co_await context->semaphore;
    }
  }

  Task<> ListenerCallback(struct evconnlistener*, evutil_socket_t fd,
                          struct sockaddr*, int socklen) {
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
      std::unique_ptr<bufferevent, BufferEventDeleter> bev(
          bufferevent_socket_new(event_loop_, fd, BEV_OPT_CLOSE_ON_FREE));
      if (!bev) {
        throw HttpException(HttpException::kUnknown,
                            "bufferevent_socket_new failed");
      }
      stdx::stop_callback stop_callback1(stop_source_.get_token(), [&] {
        context.stop_source.request_stop();
      });
      stdx::stop_callback stop_callback2(context.stop_source.get_token(), [&] {
        context.semaphore.SetException(HttpException(HttpException::kAborted));
      });
      bufferevent_setcb(bev.get(), ReadCallback, WriteCallback, EventCallback,
                        &context);
      bufferevent_setwatermark(bev.get(), EV_READ | EV_WRITE, 0,
                               2 * kMaxLineLength);
      bool error = false;
      while (!error) {
        context.request = {};
        context.semaphore = Promise<void>();
        context.stage = RequestContext::Stage::kUrl;
        context.content_length = std::nullopt;
        context.current_chunk_length = std::nullopt;
        context.read_count = 0;
        Check(bufferevent_enable(bev.get(), EV_READ));
        Check(bufferevent_disable(bev.get(), EV_WRITE));
        try {
          co_await Wait(&context);
          context.response.emplace(co_await on_request_(
              std::move(context.request), context.stop_source.get_token()));
        } catch (const HttpException& e) {
          context.response.emplace(GetResponse(e.status(), e.what()));
          error = context.stage != RequestContext::Stage::kBody &&
                  context.stage != RequestContext::Stage::kInvalid;
        } catch (const std::exception& e) {
          context.response.emplace(GetResponse(500, e.what()));
          error = context.stage != RequestContext::Stage::kBody &&
                  context.stage != RequestContext::Stage::kInvalid;
        }
        bool is_chunked =
            !GetHeader(context.response->headers, "Content-Length").has_value();
        std::stringstream header;
        header << "HTTP/1.1 " << context.response->status << " "
               << ToStatusString(context.response->status) << "\r\n";
        if (is_chunked) {
          header << "Transfer-Encoding: chunked\r\n";
        }
        for (const auto& [key, value] : context.response->headers) {
          header << key << ": " << value << "\r\n";
        }
        header << "Connection: " << (error ? "close" : "keep-alive") << "\r\n";
        header << "\r\n";
        std::string chunk = std::move(header).str();
        Check(bufferevent_enable(bev.get(), EV_WRITE));
        context.semaphore = Promise<void>();
        Check(bufferevent_write(bev.get(), chunk.data(), chunk.size()));
        co_await Wait(&context);

        if (context.request.method == Method::kHead) {
          continue;
        }

        auto write = [&](std::string&& chunk) {
          if (is_chunked) {
            std::stringstream stream;
            stream << std::hex << chunk.size() << "\r\n"
                   << std::move(chunk) << "\r\n";
            chunk = std::move(stream).str();
            Check(bufferevent_write(bev.get(), chunk.data(), chunk.size()));
          } else {
            Check(bufferevent_write(bev.get(), chunk.data(), chunk.size()));
          }
        };
        std::optional<typename decltype(context.response->body.begin())::type>
            it;
        try {
          it.emplace(co_await context.response->body.begin());
        } catch (const std::exception& e) {
          write(e.what());
        }
        while (it && *it != context.response->body.end()) {
          Check(bufferevent_enable(bev.get(), EV_WRITE));
          context.semaphore = Promise<void>();
          chunk = std::move(**it);
          write(std::move(chunk));
          try {
            co_await ++*it;
          } catch (const std::exception& e) {
            write(e.what());
            error = true;
            break;
          }
          co_await Wait(&context);
        }
        if (is_chunked) {
          context.semaphore = Promise<void>();
          const std::string_view trailer = "0\r\n\r\n";
          Check(bufferevent_write(bev.get(), trailer.data(), trailer.length()));
          co_await Wait(&context);
        }
        FOR_CO_AWAIT(std::string & chunk,
                     GetBodyGenerator(bev.get(), &context)) {}
      }
    } catch (...) {
      context.stop_source.request_stop();
    }
  }

  static void ReadCallback(struct bufferevent* bev, void* user_data) {
    auto* context = reinterpret_cast<RequestContext*>(user_data);
    struct evbuffer* input = bufferevent_get_input(bev);
    while (evbuffer_get_length(input) > 0) {
      if (context->stage == RequestContext::Stage::kUrl) {
        size_t length;
        std::unique_ptr<char, FreeDeleter> line(
            evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
        if (line) {
          std::regex regex(R"(([A-Z]+) (\S+) HTTP\/1\.[01])");
          std::string_view view(line.get(), length);
          std::match_results<std::string_view::const_iterator> match;
          if (std::regex_match(view.begin(), view.end(), match, regex)) {
            try {
              context->request.method = ToMethod(match[1].str());
            } catch (const HttpException&) {
              context->semaphore.SetException(HttpException(501));
              return;
            }
            context->request.url = match[2];
            context->stage = RequestContext::Stage::kHeaders;
          } else {
            context->semaphore.SetException(
                HttpException(HttpException::kBadRequest, "malformed url"));
            return;
          }
        } else if (evbuffer_get_length(input) >= kMaxLineLength) {
          return context->semaphore.SetException(HttpException(414));
        }
      }
      if (context->stage == RequestContext::Stage::kHeaders) {
        size_t length;
        std::unique_ptr<char, FreeDeleter> line(
            evbuffer_readln(input, &length, EVBUFFER_EOL_CRLF_STRICT));
        if (line) {
          if (length == 0) {
            if (auto header =
                    GetHeader(context->request.headers, "Content-Length")) {
              context->content_length = std::stoll(*header);
            } else {
              context->content_length = 0;
            }
            for (const auto& [key, value] : context->request.headers) {
              if (ToLowerCase(key) == ToLowerCase("Transfer-Encoding") &&
                  value.find("chunked") != std::string::npos) {
                context->content_length = std::nullopt;
                break;
              }
            }
            context->stage = RequestContext::Stage::kBody;
          } else {
            std::regex regex(R"((\S+):\s*(.+)$)");
            std::string_view view(line.get(), length);
            std::match_results<std::string_view::const_iterator> match;
            if (std::regex_match(view.begin(), view.end(), match, regex)) {
              context->request.headers.emplace_back(match[1], match[2]);
            } else {
              context->semaphore.SetException(HttpException(
                  HttpException::kBadRequest, "malformed header"));
              return;
            }
          }
        } else if (evbuffer_get_length(input) >= kMaxLineLength) {
          return context->semaphore.SetException(HttpException(431));
        }
      }
      if (context->stage == RequestContext::Stage::kBody) {
        if (!context->request.body) {
          context->request.body = GetBodyGenerator(bev, context);
        }
        Check(bufferevent_disable(bev, EV_READ));
        context->semaphore.SetValue();
        break;
      }
    }
  }

  static ResponseType GetResponse(int status, std::string body) {
    ResponseType response;
    response.status = status;
    response.headers.emplace_back("Content-Length",
                                  std::to_string(body.size()));
    response.body = GetBodyGenerator(std::move(body));
    return response;
  }

  static Generator<std::string> GetBodyGenerator(std::string chunk) {
    co_yield std::move(chunk);
  }

  static Generator<std::string> GetBodyGenerator(struct bufferevent* bev,
                                                 RequestContext* context) {
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
              bufferevent_enable(bev, EV_READ);
              co_await Wait(context);
              context->semaphore = Promise<void>();
            }
          }
          bool terminated = context->current_chunk_length == 0;
          while (*context->current_chunk_length > 0) {
            if (evbuffer_get_length(input) == 0) {
              bufferevent_enable(bev, EV_READ);
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
            *context->current_chunk_length -= buffer.size();
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
              bufferevent_enable(bev, EV_READ);
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
        context->stage = RequestContext::Stage::kInvalid;
        throw;
      }
    } else {
      while (context->read_count < *context->content_length) {
        bufferevent_enable(bev, EV_READ);
        co_await Wait(context);
        context->semaphore = Promise<void>();
        std::string buffer(evbuffer_get_length(input), 0);
        if (evbuffer_remove(input, reinterpret_cast<void*>(buffer.data()),
                            buffer.size()) != buffer.size()) {
          throw HttpException(HttpException::kUnknown,
                              "evbuffer_remove failed");
        }
        context->read_count += buffer.size();
        co_yield std::move(buffer);
      }
    }
  }

  static void WriteCallback(struct bufferevent* bev, void* user_data) {
    auto* context = reinterpret_cast<RequestContext*>(user_data);
    context->semaphore.SetValue();
  }

  static void EventCallback(struct bufferevent* bev, short events,
                            void* user_data) {
    auto* context = reinterpret_cast<RequestContext*>(user_data);
    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
      context->stop_source.request_stop();
    }
  }

  static void EvListenerCallback(struct evconnlistener* listener,
                                 evutil_socket_t socket, struct sockaddr* addr,
                                 int socklen, void* d) {
    coro::Invoke(reinterpret_cast<HttpServer*>(d)->ListenerCallback(
        listener, socket, addr, socklen));
  }

  static void OnQuit(evutil_socket_t, short, void* handle) {
    auto http_server = reinterpret_cast<HttpServer*>(handle);
    http_server->listener_.reset();
    http_server->quit_semaphore_.SetValue();
  }

  struct EvconnListenerDeleter {
    void operator()(evconnlistener* listener) const {
      if (listener) {
        evconnlistener_free(listener);
      }
    }
  };

  event_base* event_loop_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  event quit_event_;
  Promise<void> quit_semaphore_;
  std::unique_ptr<evconnlistener, EvconnListenerDeleter> listener_;
  HandlerType on_request_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
