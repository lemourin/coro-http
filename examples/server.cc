#include <coro/generator.h>
#include <coro/http/curl_http.h>
#include <coro/http/http_server.h>

#include <csignal>
#include <memory>

constexpr const char *kUrl =
    R"(http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4)";

template <typename T, typename Deleter>
auto MakePointer(T *ptr, Deleter &&deleter) {
  return std::unique_ptr<T, Deleter>(ptr, std::forward<Deleter>(deleter));
}

template <coro::http::HttpClient HttpClient>
class HttpHandler {
 public:
  explicit HttpHandler(HttpClient &http) : http_(http) {}

  coro::Task<typename HttpClient::ResponseType> operator()(
      const coro::http::Request &request,
      const coro::stdx::stop_token &stop_token) const {
    std::unordered_multimap<std::string, std::string> headers;
    auto range_it = request.headers.find("Range");
    if (range_it != std::end(request.headers)) {
      headers.emplace(*range_it);
    }
    auto pipe_request =
        coro::http::Request{.url = kUrl, .headers = std::move(headers)};
    auto pipe = co_await http_.Fetch(std::move(pipe_request), stop_token);
    co_return typename HttpClient::ResponseType{.status = pipe.status,
                                                .headers = pipe.headers,
                                                .body = std::move(pipe.body)};
  }

 private:
  HttpClient &http_;
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

  auto base = MakePointer(event_base_new(), event_base_free);
  coro::http::CurlHttp http(base.get());
  coro::http::HttpServer http_server(base.get(), HttpHandler{http});
  event_base_dispatch(base.get());
  return 0;
}