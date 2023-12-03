#include "coro/util/base_server.h"

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#include <iostream>
#include <span>

#include "coro/exception.h"
#include "coro/util/raii_utils.h"

namespace coro::util {

namespace {

using ::coro::util::AtScopeExit;
using ::coro::util::EvconnListener;
using ::coro::util::EvconnListenerDeleter;
using ::coro::util::ServerConfig;

struct RequestContext {
  Promise<void> semaphore;
  stdx::stop_source stop_source;
  std::vector<uint8_t> request;
};

struct BufferEventDeleter {
  void operator()(bufferevent* bev) const noexcept { bufferevent_free(bev); }
};

struct EvBufferDeleter {
  void operator()(evbuffer* buffer) const noexcept { evbuffer_free(buffer); }
};

void Check(int code) {
  if (code != 0) {
    throw RuntimeError("base server error: " + std::to_string(code));
  }
}

Task<> WaitRead(RequestContext* context) {
  if (context->stop_source.get_token().stop_requested()) {
    throw InterruptedException();
  } else {
    co_await context->semaphore;
    context->semaphore = Promise<void>();
  }
}

Task<> WaitWrite(RequestContext* context) {
  if (context->stop_source.get_token().stop_requested()) {
    throw InterruptedException();
  } else {
    co_await context->semaphore;
    context->semaphore = Promise<void>();
  }
}

std::vector<uint8_t> ToVector(std::span<const uint8_t> span) {
  return std::vector<uint8_t>(span.begin(), span.end());
}

Task<> Write(RequestContext* context, bufferevent* bev,
             std::span<const uint8_t> chunk) {
  Check(bufferevent_enable(bev, EV_WRITE));
  auto at_exit =
      AtScopeExit([bev] { Check(bufferevent_disable(bev, EV_WRITE)); });
  std::unique_ptr<evbuffer, EvBufferDeleter> buffer{evbuffer_new()};
  if (!buffer) {
    throw RuntimeError("evbuffer_new error");
  }
  Check(evbuffer_add_reference(buffer.get(), chunk.data(), chunk.size(),
                               /*cleanupfn=*/nullptr,
                               /*cleanupfnarg=*/nullptr));
  Check(bufferevent_write_buffer(bev, buffer.get()));
  co_await WaitWrite(context);
}

void ReadCallback(struct bufferevent*, void* user_data) {
  auto* context = reinterpret_cast<RequestContext*>(user_data);
  context->semaphore.SetValue();
}

void WriteCallback(struct bufferevent*, void* user_data) {
  auto* context = reinterpret_cast<RequestContext*>(user_data);
  context->semaphore.SetValue();
}

void EventCallback(struct bufferevent*, short events, void* user_data) {
  auto* context = reinterpret_cast<RequestContext*>(user_data);
  if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF)) {
    context->stop_source.request_stop();
  }
}

std::unique_ptr<bufferevent, BufferEventDeleter> CreateBufferEvent(
    event_base* event_loop, evutil_socket_t fd, RequestContext* context) {
  std::unique_ptr<bufferevent, BufferEventDeleter> bev(
      bufferevent_socket_new(event_loop, fd, BEV_OPT_CLOSE_ON_FREE));
  if (!bev) {
    throw RuntimeError("bufferevent_socket_new failed");
  }
  bufferevent_setwatermark(bev.get(), EV_READ | EV_WRITE, /*lowmark=*/0,
                           /*highmark=*/kMaxBufferSize);
  bufferevent_setcb(bev.get(), ReadCallback, WriteCallback, EventCallback,
                    context);
  Check(bufferevent_enable(bev.get(), EV_READ));
  Check(bufferevent_disable(bev.get(), EV_WRITE));
  return bev;
}

BaseRequestDataProvider GetRequestContent(struct bufferevent* bev,
                                          RequestContext* context) {
  return [bev, context](uint32_t byte_cnt) -> Task<std::vector<uint8_t>> {
    if (byte_cnt != UINT32_MAX && byte_cnt > kMaxBufferSize) {
      throw InvalidArgument("requested too big request chunk");
    }
    if (byte_cnt == 0) {
      co_return std::vector<uint8_t>();
    }
    struct evbuffer* input = bufferevent_get_input(bev);
    size_t size = evbuffer_get_length(input);
    if (size == 0) {
      co_await WaitRead(context);
      size = evbuffer_get_length(input);
    }
    if (byte_cnt == UINT32_MAX) {
      std::vector<uint8_t> data(size, 0);
      if (evbuffer_remove(input, data.data(), size) != size) {
        throw RuntimeError("evbuffer_remove error");
      }
      co_return data;
    }
    while (size < byte_cnt) {
      co_await WaitRead(context);
      size = evbuffer_get_length(input);
    }
    std::vector<uint8_t> data(byte_cnt, 0);
    if (evbuffer_remove(input, data.data(), byte_cnt) !=
        static_cast<int>(byte_cnt)) {
      throw RuntimeError("evbuffer_remove error");
    }
    co_return data;
  };
}

Task<> ListenerCallback(BaseServer* server_context, struct evconnlistener*,
                        evutil_socket_t fd, struct sockaddr*,
                        int socklen) noexcept {
  RequestContext context{};
  try {
    if (server_context->quitting()) {
      co_return;
    }
    server_context->IncreaseCurrentConnections();
    auto scope_guard = AtScopeExit([&] {
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
      context.semaphore.SetException(InterruptedException());
    });
    auto bev = CreateBufferEvent(reinterpret_cast<event_base*>(GetEventLoop(
                                     *server_context->event_loop())),
                                 fd, &context);
    bool terminate_connection = false;
    while (!terminate_connection) {
      auto response = server_context->request_handler()(
          GetRequestContent(bev.get(), &context),
          context.stop_source.get_token());
      FOR_CO_AWAIT(BaseResponseFlowControl ctl, response) {
        if (ctl.type == BaseResponseFlowControl::Type::kTerminateConnection) {
          terminate_connection = true;
        }
        if (!ctl.chunk.empty()) {
          co_await Write(&context, bev.get(), ctl.chunk);
        }
      }
    }
  } catch (const Exception& e) {
    std::cerr << "EXCEPTION " << e.what() << '\n';
    context.stop_source.request_stop();
  }
}

void EvListenerCallback(struct evconnlistener* listener, evutil_socket_t socket,
                        struct sockaddr* addr, int socklen, void* d) {
  auto* context = reinterpret_cast<BaseServer*>(d);
  RunTask(ListenerCallback(context, listener, socket, addr, socklen));
}

std::unique_ptr<EvconnListener, EvconnListenerDeleter> CreateListener(
    event_base* event_loop, evconnlistener_cb cb, void* userdata,
    const ServerConfig& config) {
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
    throw RuntimeError("evconnlistener_new_bind error");
  }
  return std::unique_ptr<EvconnListener, EvconnListenerDeleter>(
      reinterpret_cast<EvconnListener*>(listener));
}

std::unique_ptr<EvconnListener, EvconnListenerDeleter> CreateListener(
    BaseServer* rpc_server, const EventLoop* event_loop,
    const ServerConfig& config) {
  return CreateListener(
      reinterpret_cast<event_base*>(GetEventLoop(*event_loop)),
      EvListenerCallback, rpc_server, config);
}

}  // namespace

Task<> DrainDataProvider(BaseRequestDataProvider data_provider) {
  while (!(co_await data_provider(UINT32_MAX)).empty()) {
  }
}

void EvconnListenerDeleter::operator()(
    EvconnListener* listener) const noexcept {
  evconnlistener_free(reinterpret_cast<evconnlistener*>(listener));
}

BaseServer::BaseServer(BaseRequestHandler request_handler,
                       const EventLoop* event_loop, const ServerConfig& config)
    : request_handler_(std::move(request_handler)),
      event_loop_(event_loop),
      listener_(CreateListener(this, event_loop, config)) {}

void BaseServer::OnQuit() {
  event_loop_->RunOnEventLoop([this] {
    listener_.reset();
    quit_semaphore_.SetValue();
  });
}

Task<> BaseServer::Quit() {
  if (quitting_) {
    co_return;
  }
  stop_source_.request_stop();
  quitting_ = true;
  if (current_connections_ == 0) {
    OnQuit();
  }
  co_await quit_semaphore_;
}

uint16_t BaseServer::port() const {
  sockaddr_in addr;
  socklen_t length = sizeof(addr);
  Check(getsockname(
      evconnlistener_get_fd(reinterpret_cast<evconnlistener*>(listener_.get())),
      reinterpret_cast<sockaddr*>(&addr), &length));
  return ntohs(addr.sin_port);
}

}  // namespace coro::util