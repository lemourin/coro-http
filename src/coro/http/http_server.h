#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include "coro/http/http.h"
#include "coro/http/http_server_context.h"
#include "coro/stdx/concepts.h"
#include "coro/task.h"
#include "coro/util/event_loop.h"

namespace coro::http {

template <typename T>
concept Handler = requires(T v, Request<> request,
                           stdx::stop_token stop_token) {
  {
    v(std::move(request), stop_token).await_resume()
    } -> stdx::same_as<Response<>>;
};

template <typename T>
concept HasQuit = requires(T v) {
  { v.Quit() } -> Awaitable<void>;
};

template <Handler HandlerType>
class HttpServer {
 public:
  template <typename... Args>
  HttpServer(const coro::util::EventLoop* event_loop,
             const HttpServerConfig& config, Args&&... args);

  HttpServer(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;

  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  uint16_t GetPort() const;
  Task<> Quit() noexcept;

 private:
  HandlerType on_request_;
  internal::HttpServerContext context_;
};

template <Handler HandlerType>
template <typename... Args>
HttpServer<HandlerType>::HttpServer(const coro::util::EventLoop* event_loop,
                                    const HttpServerConfig& config,
                                    Args&&... args)
    : on_request_(std::forward<Args>(args)...),
      context_(event_loop, config,
               [this](http::Request<> request,
                      stdx::stop_token stop_token) -> Task<Response<>> {
                 co_return co_await on_request_(std::move(request),
                                                std::move(stop_token));
               }) {}

template <Handler HandlerType>
uint16_t HttpServer<HandlerType>::GetPort() const {
  return context_.GetPort();
}

template <Handler HandlerType>
Task<> HttpServer<HandlerType>::Quit() noexcept {
  if constexpr (HasQuit<HandlerType>) {
    return context_.Quit([](HandlerType* on_request) -> Task<> {
      co_await on_request->Quit();
    }(&on_request_));
  } else {
    return context_.Quit([]() -> Task<> { co_return; }());
  }
}

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
