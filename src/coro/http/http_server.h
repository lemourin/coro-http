#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include "coro/http/http.h"
#include "coro/stdx/stop_token.h"
#include "coro/task.h"
#include "coro/util/event_loop.h"
#include "coro/util/tcp_server.h"

namespace coro::http {

using HttpHandler =
    stdx::any_invocable<Task<Response<>>(Request<>, stdx::stop_token)>;

coro::util::TcpServer CreateHttpServer(
    HttpHandler http_handler, const coro::util::EventLoop* event_loop,
    const coro::util::TcpServer::Config& config);

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
