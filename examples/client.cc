#include <csignal>
#include <iostream>
#include <memory>

#include "coro/generator.h"
#include "coro/http/curl_http.h"
#include "coro/stdx/stop_source.h"
#include "coro/util/event_loop.h"

class CancelRequest {
 public:
  CancelRequest(const coro::util::EventLoop &event_loop,
                coro::stdx::stop_source request_stop_source) {
    coro::RunTask(Init(event_loop, std::move(request_stop_source)));
  }

  ~CancelRequest() { timeout_stop_source_.request_stop(); }

 private:
  coro::Task<> Init(const coro::util::EventLoop &event_loop,
                    coro::stdx::stop_source request_stop_source) noexcept {
    try {
      co_await event_loop.Wait(3000, timeout_stop_source_.get_token());
      std::cerr << "REQUESTING STOP\n";
      request_stop_source.request_stop();
    } catch (const coro::InterruptedException &) {
    }
  };

  coro::stdx::stop_source timeout_stop_source_;
};

coro::Task<> CoMain(const coro::util::EventLoop *event_loop) noexcept {
  try {
    coro::http::Http http{coro::http::CurlHttp(event_loop)};
    coro::stdx::stop_source stop_source;
    CancelRequest cancel_request(*event_loop, stop_source);

    coro::http::Response response =
        co_await http.Fetch("https://samples.ffmpeg.org/Matroska/haruhi.mkv",
                            stop_source.get_token());

    std::cerr << "HTTP: " << response.status << "\n";
    for (const auto &[header_name, header_value] : response.headers) {
      std::cerr << header_name << ": " << header_value << "\n";
    }

    std::size_t size = 0;
    FOR_CO_AWAIT(const std::string &bytes, response.body) {
      std::cerr << "awaiting...\n";
      co_await event_loop->Wait(1000, stop_source.get_token());
      std::cerr << "bytes:" << bytes.size() << "\n";
      size += bytes.size();
    }

    std::cerr << "DONE (SIZE=" << size << ")\n";
  } catch (const coro::http::HttpException &exception) {
    std::cerr << "exception: " << exception.what() << "\n";
    co_return;
  } catch (const coro::InterruptedException &) {
    std::cerr << "interrupted\n";
    co_return;
  }
}

int main() {
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  coro::util::EventLoop event_loop;
  coro::RunTask(CoMain(&event_loop));
  event_loop.EnterLoop();
  return 0;
}