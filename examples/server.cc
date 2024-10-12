#include <csignal>
#include <memory>

#include "coro/generator.h"
#include "coro/http/curl_http.h"
#include "coro/http/http_parse.h"
#include "coro/http/http_server.h"
#include "coro/util/event_loop.h"
#include "coro/util/raii_utils.h"

constexpr const char *kUrl =
    R"(http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4)";

class HttpHandler {
 public:
  HttpHandler(const coro::http::Http *http, coro::Promise<void> *semaphore)
      : http_(http), semaphore_(semaphore) {}

  coro::Task<coro::http::Response<>> operator()(
      coro::http::Request<> request, coro::stdx::stop_token stop_token) const {
    coro::http::Request<> pipe_request{.url = kUrl};
    if (auto range_header = coro::http::GetHeader(request.headers, "Range")) {
      pipe_request.headers.emplace_back("Range", std::move(*range_header));
    }
    if (request.url == "/quit") {
      co_return coro::http::Response<>{
          .status = 200,
          .body = GetQuitResponse(
              std::unique_ptr<const HttpHandler, QuitDeleter>(this))};
    }
    auto pipe =
        co_await http_->Fetch(std::move(pipe_request), std::move(stop_token));
    co_return coro::http::Response<>{.status = pipe.status,
                                     .headers = pipe.headers,
                                     .body = std::move(pipe.body)};
  }

 private:
  struct QuitDeleter {
    void operator()(const HttpHandler *handler) {
      handler->semaphore_->SetValue();
    }
  };

  coro::Generator<std::string> GetQuitResponse(
      std::unique_ptr<const HttpHandler, QuitDeleter>) const {
    co_yield "QUITTING...\n";
  }

  const coro::http::Http *http_;
  coro::Promise<void> *semaphore_;
};

int main() {
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  coro::util::EventLoop event_loop;
  coro::RunTask([&]() -> coro::Task<> {
    coro::http::Http http{coro::http::CurlHttp(&event_loop)};
    coro::Promise<void> semaphore;
    auto http_server = coro::http::CreateHttpServer(
        HttpHandler{&http, &semaphore}, &event_loop,
        {.address = "127.0.0.1", .port = 4444});
    co_await semaphore;
    co_await http_server.Quit();
  });
  event_loop.EnterLoop();
  return 0;
}