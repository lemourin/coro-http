#include <coro/http/curl_http.h>
#include <coro/http/http.h>
#include <coro/task.h>
#include <event2/event.h>

#include <iostream>
#include <memory>

template <typename T, typename Deleter>
auto MakePointer(T *ptr, Deleter &&deleter) {
  return std::unique_ptr<T, Deleter>(ptr, std::forward<Deleter>(deleter));
}

coro::Task<int> CoMain(coro::http::Http &http) noexcept {
  try {
    coro::http::Response response = co_await http.Fetch("http://example.com");

    std::cerr << "status:" << response.status << "\n";
    std::cerr << "body: " << response.body << "\n";

    co_return 0;
  } catch (const std::exception &exception) {
    std::cerr << "exception: " << exception.what() << "\n";
    co_return -1;
  }
}

coro::Task<int> RunCoMain(coro::http::Http &http, event_base *base) {
  int result = co_await CoMain(http);
  event_base_loopbreak(base);
  co_return result;
}

int main() {
#ifdef _WIN32
  WORD version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;

  (void)WSAStartup(version_requested, &wsa_data);
#endif

  auto base = MakePointer(event_base_new(), event_base_free);
  coro::http::CurlHttp http(base.get());

  RunCoMain(http, base.get());
  event_base_dispatch(base.get());
  return 0;
}