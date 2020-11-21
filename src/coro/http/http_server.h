#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include <coro/stdx/stop_callback.h>
#include <coro/stdx/stop_source.h>
#include <coro/task.h>
#include <coro/util/function_traits.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <evhttp.h>

#include <memory>

#include "http.h"

namespace coro::http {

// clang-format off
template <typename T>
concept Handler = requires (T v) {
  { v(std::declval<Request>(), stdx::stop_token()).await_resume() } -> ResponseType;
};
// clang-format on

template <Handler HandlerType>
class HttpServer {
 public:
  explicit HttpServer(event_base* event_loop, HandlerType&& on_request)
      : event_loop_(event_loop),
        http_(evhttp_new(event_loop)),
        on_request_(std::move(on_request)) {
    Check(evhttp_bind_socket(http_, "0.0.0.0", 4444));
    evhttp_set_gencb(http_, OnHttpRequest, this);
    Check(event_assign(&quit_event_, event_loop, -1, 0, OnQuit, this));
  }

  ~HttpServer() {
    if (http_) {
      evhttp_free(http_);
    }
    Check(event_del(&quit_event_));
  }

  HttpServer(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;

  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

  Task<> Quit() noexcept {
    quitting_ = true;
    stop_source_.request_stop();
    if (current_connections_ == 0) {
      timeval tv = {};
      Check(event_add(&quit_event_, &tv));
    }
    co_await quit_semaphore_;
  }

 private:
  Task<> OnHttpRequest(evhttp_request* ev_request) noexcept {
    if (quitting_) {
      evhttp_send_reply(ev_request, 500, nullptr, nullptr);
      co_return;
    }
    if (evhttp_request_get_uri(ev_request) == std::string("/quit")) {
      evhttp_send_reply(ev_request, 200, nullptr, nullptr);
      co_await Quit();
      co_return;
    }

    request_type request{.url = evhttp_request_get_uri(ev_request)};
    stdx::stop_source stop_source;
    stdx::stop_callback stop_callback(stop_source_.get_token(),
                                      [&] { stop_source.request_stop(); });
    evhttp_connection_set_closecb(evhttp_request_get_connection(ev_request),
                                  OnConnectionClose, &stop_source);

    evkeyvalq* ev_headers = evhttp_request_get_input_headers(ev_request);
    evkeyval* header = ev_headers ? ev_headers->tqh_first : nullptr;
    while (header != nullptr) {
      request.headers.emplace(header->key, header->value);
      header = header->next.tqe_next;
    }

    bool reply_started = false;
    current_connections_++;
    try {
      auto response = co_await on_request_(request, stop_source.get_token());

      evkeyvalq* response_headers =
          evhttp_request_get_output_headers(ev_request);
      for (const auto& [key, value] : response.headers) {
        Check(evhttp_add_header(response_headers, key.c_str(), value.c_str()));
      }

      reply_started = true;
      evhttp_send_reply_start(ev_request, response.status, nullptr);
      auto guard = Make(ev_request, [](evhttp_request* ev_request) {
        ResetOnCloseCallback(ev_request);
        evhttp_send_reply_end(ev_request);
      });

      auto buffer = Make(evbuffer_new(), evbuffer_free);
      int size = 0;
      FOR_CO_AWAIT(const std::string& chunk, response.body, {
        evbuffer_add(buffer.get(), chunk.c_str(), chunk.size());
        size += chunk.size();
        Semaphore semaphore;
        evhttp_send_reply_chunk_with_cb(ev_request, buffer.get(), OnWriteReady,
                                        &semaphore);
        stdx::stop_callback stop_callback_dl(stop_source_.get_token(),
                                             [&] { semaphore.resume(); });
        co_await semaphore;
      });
    } catch (const coro::http::HttpException&) {
      if (!reply_started) {
        ResetOnCloseCallback(ev_request);
        evhttp_send_reply(ev_request, 500, nullptr, nullptr);
      }
    }
    current_connections_--;
    if (current_connections_ == 0 && quitting_) {
      timeval tv = {};
      Check(event_add(&quit_event_, &tv));
    }
  }

  static void OnConnectionClose(evhttp_connection*, void* arg) {
    reinterpret_cast<stdx::stop_source*>(arg)->request_stop();
  }

  static void OnHttpRequest(evhttp_request* request, void* arg) {
    reinterpret_cast<HttpServer*>(arg)->OnHttpRequest(request);
  }

  static void OnWriteReady(evhttp_connection*, void* arg) {
    reinterpret_cast<Semaphore*>(arg)->resume();
  }

  static void OnQuit(evutil_socket_t, short, void* handle) {
    auto http_server = reinterpret_cast<HttpServer*>(handle);
    evhttp_free(std::exchange(http_server->http_, nullptr));
    http_server->quit_semaphore_.resume();
  }

  static void ResetOnCloseCallback(evhttp_request* request) {
    evhttp_connection* connection = evhttp_request_get_connection(request);
    if (connection) {
      evhttp_connection_set_closecb(connection, nullptr, nullptr);
    }
  }

  static void Check(int code) {
    if (code != 0) {
      throw HttpException(code, "http server error");
    }
  }

  template <typename T, typename Callable>
  static auto Make(T* p, Callable deleter) {
    return std::unique_ptr<T, Callable>(p, std::move(deleter));
  }

  class Semaphore {
   public:
    bool await_ready() { return resumed_; }
    void await_suspend(stdx::coroutine_handle<void> continuation) {
      continuation_ = continuation;
    }
    void await_resume() {}
    void resume() {
      resumed_ = true;
      if (continuation_) {
        continuation_.resume();
      }
    }

   private:
    bool resumed_ = false;
    stdx::coroutine_handle<void> continuation_;
  };

  using handler_argument_list = util::ArgumentListTypeT<HandlerType>;
  static_assert(util::TypeListLengthV<handler_argument_list> == 2);

  using request_type =
      std::remove_cvref_t<util::TypeAtT<handler_argument_list, 0>>;
  using response_type = util::ReturnType<HandlerType>;

  event_base* event_loop_;
  evhttp* http_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  event quit_event_;
  Semaphore quit_semaphore_;
  HandlerType on_request_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
