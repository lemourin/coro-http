#ifndef CORO_HTTP_HTTP_SERVER_H
#define CORO_HTTP_HTTP_SERVER_H

#include <coro/http/http_parse.h>
#include <coro/promise.h>
#include <coro/stdx/stop_callback.h>
#include <coro/stdx/stop_source.h>
#include <coro/task.h>
#include <coro/util/function_traits.h>
#include <coro/util/raii_utils.h>
#include <event2/buffer.h>
#include <event2/event.h>
#include <event2/http.h>
#include <evhttp.h>

#include <memory>
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
        http_(evhttp_new(event_loop)),
        on_request_(std::move(on_request)) {
    Check(evhttp_bind_socket(http_.get(), config.address.c_str(), config.port));
    evhttp_set_gencb(http_.get(), OnHttpRequest, this);
    Check(event_assign(&quit_event_, event_loop, -1, 0, OnQuit, this));
    evhttp_set_allowed_methods(
        http_.get(), EVHTTP_REQ_PROPFIND | EVHTTP_REQ_GET | EVHTTP_REQ_POST |
                         EVHTTP_REQ_OPTIONS | EVHTTP_REQ_HEAD |
                         EVHTTP_REQ_MKCOL | EVHTTP_REQ_PROPPATCH |
                         EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE | EVHTTP_REQ_MOVE |
                         EVHTTP_REQ_PATCH);
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

    bool reply_started = false;
    current_connections_++;
    try {
      RequestType request{
          .url = evhttp_request_get_uri(ev_request),
          .method = ToMethod(evhttp_request_get_command(ev_request))};
      stdx::stop_source stop_source;
      stdx::stop_callback stop_callback(stop_source_.get_token(),
                                        [&] { stop_source.request_stop(); });
      evhttp_connection_set_closecb(evhttp_request_get_connection(ev_request),
                                    OnConnectionClose, &stop_source);

      evkeyvalq* ev_headers = evhttp_request_get_input_headers(ev_request);
      evkeyval* header = ev_headers ? ev_headers->tqh_first : nullptr;
      while (header != nullptr) {
        request.headers.emplace_back(header->key, header->value);
        header = header->next.tqe_next;
      }

      stdx::stop_source body_stop_source;
      stdx::stop_callback body_stop_callback(
          stop_source.get_token(), [&] { body_stop_source.request_stop(); });
      auto body_size = http::GetHeader(request.headers, "Content-Length");
      if (body_size) {
        auto input_buffer = evhttp_request_get_input_buffer(ev_request);
        if (input_buffer) {
          request.body =
              GenerateRequestBody(input_buffer, std::stoll(*body_size),
                                  body_stop_source.get_token());
        }
      }

      auto response =
          co_await on_request_(std::move(request), stop_source.get_token());

      evkeyvalq* response_headers =
          evhttp_request_get_output_headers(ev_request);
      for (const auto& [key, value] : response.headers) {
        Check(evhttp_add_header(response_headers, key.c_str(), value.c_str()));
      }

      reply_started = true;
      evhttp_send_reply_start(ev_request, response.status, nullptr);

      auto buffer = util::MakePointer(evbuffer_new(), evbuffer_free);
      FOR_CO_AWAIT(const std::string& chunk, response.body) {
        evbuffer_add(buffer.get(), chunk.c_str(), chunk.size());
        Promise<void> semaphore;
        evhttp_send_reply_chunk_with_cb(ev_request, buffer.get(), OnWriteReady,
                                        &semaphore);
        stdx::stop_callback stop_callback_dl1(stop_source_.get_token(), [&] {
          semaphore.SetException(InterruptedException());
        });
        stdx::stop_callback stop_callback_dl2(stop_source.get_token(), [&] {
          semaphore.SetException(InterruptedException());
        });
        co_await semaphore;
      }
      ResetOnCloseCallback(ev_request);
      evhttp_send_reply_end(ev_request);
    } catch (const std::exception& e) {
      ResetOnCloseCallback(ev_request);
      auto buffer = util::MakePointer(evbuffer_new(), evbuffer_free);
      std::string error = std::string(e.what());
      if (!error.empty() && error.back() != '\n') {
        error += '\n';
      }
      evbuffer_add(buffer.get(), error.c_str(), error.length());
      if (!reply_started) {
        evhttp_send_reply(ev_request, 500, nullptr, buffer.get());
      } else {
        evhttp_send_reply_chunk(ev_request, buffer.get());
        evhttp_send_reply_end(ev_request);
      }
    }
    current_connections_--;
    if (current_connections_ == 0 && quitting_) {
      timeval tv = {};
      Check(event_add(&quit_event_, &tv));
    }
  }

  Generator<std::string> GenerateRequestBody(
      evbuffer* buffer, int64_t size, stdx::stop_token stop_token) const {
    size_t initial_length = evbuffer_get_length(buffer);
    size -= initial_length;
    std::string chunk(initial_length, 0);
    if (evbuffer_copyout(buffer, chunk.data(), initial_length) !=
        initial_length) {
      Check(HttpException::kUnknown);
    }
    co_yield chunk;
    Promise<const evbuffer_cb_info*> data;
    stdx::stop_callback stop_callback(
        stop_token, [&] { data.SetException(InterruptedException()); });
    auto cb_entry =
        util::MakePointer(evbuffer_add_cb(buffer, EvBufferCallback, &data),
                          [buffer](evbuffer_cb_entry* cb_entry) {
                            evbuffer_remove_cb_entry(buffer, cb_entry);
                          });
    while (size > 0) {
      const evbuffer_cb_info* info = co_await data;
      data = Promise<const evbuffer_cb_info*>();
      std::cerr << info->n_added << "\n";
      size -= info->n_added;
    }
  }

  static http::Method ToMethod(evhttp_cmd_type type) {
    switch (type) {
      case EVHTTP_REQ_GET:
        return http::Method::kGet;
      case EVHTTP_REQ_POST:
        return http::Method::kPost;
      case EVHTTP_REQ_HEAD:
        return http::Method::kHead;
      case EVHTTP_REQ_OPTIONS:
        return http::Method::kOptions;
      case EVHTTP_REQ_PROPFIND:
        return http::Method::kPropfind;
      case EVHTTP_REQ_PROPPATCH:
        return http::Method::kProppatch;
      case EVHTTP_REQ_MKCOL:
        return http::Method::kMkcol;
      case EVHTTP_REQ_MOVE:
        return http::Method::kMove;
      case EVHTTP_REQ_DELETE:
        return http::Method::kDelete;
      case EVHTTP_REQ_PATCH:
        return http::Method::kPatch;
      case EVHTTP_REQ_PUT:
        return http::Method::kPut;
      default:
        throw http::HttpException(http::HttpException::kInvalidMethod);
    }
  }

  static void OnConnectionClose(evhttp_connection*, void* arg) {
    reinterpret_cast<stdx::stop_source*>(arg)->request_stop();
  }

  static void OnHttpRequest(evhttp_request* request, void* arg) {
    auto http_server = reinterpret_cast<HttpServer*>(arg);
    Invoke(http_server->OnHttpRequest(request));
  }

  static void OnWriteReady(evhttp_connection*, void* arg) {
    reinterpret_cast<Promise<void>*>(arg)->SetValue();
  }

  static void OnQuit(evutil_socket_t, short, void* handle) {
    auto http_server = reinterpret_cast<HttpServer*>(handle);
    evhttp_free(http_server->http_.release());
    http_server->quit_semaphore_.SetValue();
  }

  static void ResetOnCloseCallback(evhttp_request* request) {
    evhttp_connection* connection = evhttp_request_get_connection(request);
    if (connection) {
      evhttp_connection_set_closecb(connection, nullptr, nullptr);
    }
  }

  static void EvBufferCallback(evbuffer*, const evbuffer_cb_info* info,
                               void* arg) {
    reinterpret_cast<Promise<const evbuffer_cb_info*>*>(arg)->SetValue(info);
  }

  static void Check(int code) {
    if (code != 0) {
      throw HttpException(code, "http server error");
    }
  }

  using HandlerArgumentList = util::ArgumentListTypeT<HandlerType>;
  static_assert(util::TypeListLengthV<HandlerArgumentList> == 2);

  using RequestType =
      std::remove_cvref_t<util::TypeAtT<HandlerArgumentList, 0>>;
  using ResponseType = util::ReturnType<HandlerType>;

  struct EvHttpDeleter {
    void operator()(evhttp* http) const {
      if (http) {
        evhttp_free(http);
      }
    }
  };

  event_base* event_loop_;
  std::unique_ptr<evhttp, EvHttpDeleter> http_;
  bool quitting_ = false;
  int current_connections_ = 0;
  stdx::stop_source stop_source_;
  event quit_event_;
  Promise<void> quit_semaphore_;
  HandlerType on_request_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_HTTP_SERVER_H
