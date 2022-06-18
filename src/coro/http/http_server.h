#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include <memory>
#include <span>
#include <sstream>
#include <vector>

#include "coro/http/http.h"
#include "coro/http/http_parse.h"
#include "coro/promise.h"
#include "coro/stdx/any_invocable.h"
#include "coro/stdx/concepts.h"
#include "coro/stdx/stop_callback.h"
#include "coro/stdx/stop_source.h"
#include "coro/task.h"
#include "coro/util/event_loop.h"
#include "coro/util/function_traits.h"
#include "coro/util/raii_utils.h"

namespace coro::http {

template <typename T>
concept Handler =
    requires(T v, Request<> request, stdx::stop_token stop_token) {
      {
        v(std::move(request), stop_token).await_resume()
        } -> stdx::same_as<Response<>>;
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

struct EvconnListener;

struct EvconnListenerDeleter {
  void operator()(EvconnListener* listener) const;
};

using OnRequest =
    stdx::any_invocable<Task<Response<>>(Request<>, stdx::stop_token) const>;

struct HttpServerContext {
  const coro::util::EventLoop* event_loop;
  bool quitting;
  int current_connections;
  stdx::stop_source stop_source;
  OnRequest on_request;
  Promise<void> quit_semaphore;
  std::unique_ptr<EvconnListener, EvconnListenerDeleter> listener;
};

uint16_t GetPort(EvconnListener*);

void InitHttpServerContext(HttpServerContext*, const coro::util::EventLoop*,
                           const HttpServerConfig&, OnRequest);

void OnQuit(HttpServerContext*);

}  // namespace internal

template <Handler HandlerType>
class HttpServer {
 public:
  template <typename... Args>
  HttpServer(const coro::util::EventLoop* event_loop,
             const HttpServerConfig& config, Args&&... args)
      : on_request_(std::forward<Args>(args)...) {
    internal::InitHttpServerContext(
        &context_, event_loop, config,
        [this](http::Request<> request,
               stdx::stop_token stop_token) -> Task<Response<>> {
          co_return co_await on_request_(std::move(request),
                                         std::move(stop_token));
        });
  }

  HttpServer(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;

  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  uint16_t GetPort() const {
    return ::coro::http::internal::GetPort(context_.listener.get());
  }

  Task<> Quit() noexcept {
    if (context_.quitting) {
      co_return;
    }
    context_.quitting = true;
    context_.stop_source.request_stop();
    if (context_.current_connections == 0) {
      internal::OnQuit(&context_);
    }
    if constexpr (HasQuit<HandlerType>) {
      co_await on_request_.Quit();
    }
    co_await context_.quit_semaphore;
  }

 private:
  HandlerType on_request_;
  internal::HttpServerContext context_{};
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
