#include <coro/generator.h>
#include <coro/http/curl_http.h>
#include <coro/http/http_parse.h>
#include <coro/http/http_server.h>
#include <coro/util/event_loop.h>
#include <coro/util/raii_utils.h>

#include <csignal>
#include <memory>

constexpr const char *kUrl =
    R"(http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4)";

template <coro::http::HttpClient HttpClient>
class HttpHandler {
 public:
  HttpHandler(const HttpClient &http, coro::Promise<void> *semaphore)
      : http_(http), semaphore_(semaphore) {}

  coro::Task<typename HttpClient::ResponseType> operator()(
      const coro::http::Request<> &request,
      coro::stdx::stop_token stop_token) const {
    coro::http::Request<> pipe_request{.url = kUrl};
    if (auto range_header = coro::http::GetHeader(request.headers, "Range")) {
      pipe_request.headers.emplace_back("Range", std::move(*range_header));
    }
    if (request.url == "/quit") {
      semaphore_->SetValue();
    }
    auto pipe =
        co_await http_.Fetch(std::move(pipe_request), std::move(stop_token));
    co_return typename HttpClient::ResponseType{.status = pipe.status,
                                                .headers = pipe.headers,
                                                .body = std::move(pipe.body)};
  }

 private:
  const HttpClient &http_;
  coro::Promise<void> *semaphore_;
};

int main() {
#ifdef _WIN32
  WORD version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;

  (void)WSAStartup(version_requested, &wsa_data);
#endif

#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  std::unique_ptr<event_base, coro::util::EventBaseDeleter> base(
      event_base_new());
  coro::Invoke([base = base.get()]() -> coro::Task<> {
    coro::http::CurlHttp http(base);
    coro::Promise<void> semaphore;
    coro::http::HttpServer<HttpHandler<coro::http::CurlHttp>> http_server(
        base, {.address = "127.0.0.1", .port = 4444}, http, &semaphore);
    co_await semaphore;
    co_await http_server.Quit();
  });
  event_base_dispatch(base.get());
  return 0;
}