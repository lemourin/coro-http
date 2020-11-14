#include <coro/generator.h>
#include <coro/http/curl_http.h>
#include <coro/stdx/stop_source.h>
#include <coro/wait_task.h>

#include <iostream>
#include <memory>

template <typename T, typename Deleter>
auto MakePointer(T *ptr, Deleter &&deleter) {
  return std::unique_ptr<T, Deleter>(ptr, std::forward<Deleter>(deleter));
}

class CancelRequest {
 public:
  CancelRequest(event_base *event_loop,
                coro::stdx::stop_source request_stop_source)
      : task_(Init(event_loop, std::move(request_stop_source))) {}

  ~CancelRequest() { timeout_stop_source_.request_stop(); }

 private:
  coro::Task<> Init(event_base *event_loop,
                    coro::stdx::stop_source request_stop_source) {
    try {
      co_await coro::Wait(event_loop, 3000, timeout_stop_source_.get_token());
      std::cerr << "REQUESTING STOP\n";
      request_stop_source.request_stop();
    } catch (const coro::InterruptedException &) {
    }
  };

  coro::stdx::stop_source timeout_stop_source_;
  coro::Task<> task_;
};

coro::Generator<int> Iota() {
  int idx = 0;
  while (true) {
    co_yield idx;
    idx++;
  }
}

template <typename Http>
coro::Task<int> CoMain(event_base *event_loop, Http *http) noexcept {
  try {
    coro::stdx::stop_source stop_source;
    CancelRequest cancel_request(event_loop, stop_source);

    for (int d : Iota()) {
      std::cerr << d << "\n";
      if (d == 5) {
        break;
      }
    }

    coro::http::Response response =
        co_await http->Fetch("https://samples.ffmpeg.org/Matroska/haruhi.mkv",
                             stop_source.get_token());

    std::cerr << "HTTP: " << response.status << "\n";
    for (const auto &[header_name, header_value] : response.headers) {
      std::cerr << header_name << ": " << header_value << "\n";
    }

    int size = 0;
    FOR_CO_AWAIT(const std::string &bytes, response.body, {
      std::cerr << "awaiting...\n";
      co_await coro::Wait(event_loop, 1000);
      std::cerr << "bytes:" << bytes.size() << "\n";
      size += bytes.size();
    });

    std::cerr << "DONE (SIZE=" << size << ")\n";

    co_return 0;
  } catch (const coro::http::HttpException &exception) {
    std::cerr << "exception: " << exception.what() << "\n";
    co_return -1;
  }
}

int main() {
#ifdef _WIN32
  WORD version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;

  (void)WSAStartup(version_requested, &wsa_data);
#endif

  auto base = MakePointer(event_base_new(), event_base_free);
  coro::http::CurlHttp http(base.get());

  auto main_task = CoMain(base.get(), &http);
  event_base_dispatch(base.get());
  return 0;
}