#include "coro/http/http_server.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "coro/http/curl_http.h"
#include "coro/shared_promise.h"
#include "coro/util/event_loop.h"
#include "coro/when_all.h"

namespace coro::http {
namespace {

using Request = Request<>;
using Response = Response<>;

using ::testing::Contains;

struct ResponseContent {
  int status;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

template <typename ResponseT>
Task<ResponseContent> ToResponseContent(ResponseT r) {
  std::string body = co_await GetBody(std::move(r.body));
  co_return ResponseContent{
      .status = r.status,
      .headers = std::move(r.headers),
      .body = std::move(body),
  };
}

class HttpHandler {
 public:
  HttpHandler(std::optional<coro::http::Request<std::string>>* request,
              Response* response)
      : request_(request), response_(response) {}

  Task<Response> operator()(Request request, stdx::stop_token) const {
    auto body = request.body ? co_await GetBody(std::move(*request.body))
                             : std::optional<std::string>();
    *request_ =
        coro::http::Request<std::string>{.url = std::move(request.url),
                                         .method = request.method,
                                         .headers = std::move(request.headers),
                                         .body = std::move(body)};
    co_return std::move(*response_);
  }

 private:
  std::optional<coro::http::Request<std::string>>* request_;
  Response* response_;
};

class HttpServerTest : public ::testing::Test {
 protected:
  template <typename HttpHandlerT, typename F>
  void Run(HttpHandlerT handler, F func) {
    std::exception_ptr exception;
    RunTask([&]() -> Task<> {
      try {
        auto http_server = coro::http::CreateHttpServer(
            std::move(handler), &event_loop_,
            coro::util::ServerConfig{.address = "127.0.0.1", .port = 12345});
        address_ = "http://127.0.0.1:" + std::to_string(http_server.GetPort());
        quit_ = [&] { return http_server.Quit(); };
        try {
          co_await std::move(func)();
        } catch (...) {
          exception = std::current_exception();
        }
        co_await http_server.Quit();
      } catch (...) {
        exception = std::current_exception();
      }
    });
    event_loop_.EnterLoop();
    if (exception) {
      std::rethrow_exception(exception);
    }
  }

  template <typename F>
  void Run(F func) {
    Run(HttpHandler{&last_request_, &response_}, std::move(func));
  }

  Task<int> Quit() const {
    co_await quit_();
    co_return 0;
  }

  std::string address() const { return address_.value(); }
  auto& http() { return http_; }
  const auto& last_request() const { return last_request_; }

 private:
  coro::util::EventLoop event_loop_;
  std::optional<coro::http::Request<std::string>> last_request_;
  Response response_{
      .status = 200,
      .headers = {{"Content-Type", "application/octet-stream"}},
      .body = CreateBody("response"),
  };
  std::optional<std::string> address_;
  std::function<Task<>()> quit_;
  CurlHttp http_{&event_loop_, std::nullopt};
};

TEST_F(HttpServerTest, SendsExpectedResponse) {
  std::optional<ResponseContent> response;
  Run([&]() -> Task<> {
    response = co_await ToResponseContent(co_await http().Fetch(address()));
  });

  ASSERT_TRUE(response.has_value());
  EXPECT_EQ(response->status, 200);
  EXPECT_THAT(
      response->headers,
      Contains(std::make_pair("content-type", "application/octet-stream")));
  EXPECT_EQ(response->body, "response");
}

TEST_F(HttpServerTest, ReceivesExpectedRequest) {
  Run([&]() -> Task<> {
    co_await http().Fetch(
        Request{.url = address() + "/some_path?some_query=value",
                .method = http::Method::kPost,
                .body = CreateBody("input"),
                .invalidates_cache = true});
  });

  ASSERT_TRUE(last_request().has_value());
  EXPECT_THAT(last_request()->url, "/some_path?some_query=value");
  EXPECT_EQ(last_request()->body, "input");
}

TEST_F(HttpServerTest, RejectsTooLongHeader) {
  EXPECT_THROW(Run([&]() -> Task<> {
                 Request request{
                     .url = address(),
                     .headers = {{"SomeHeader", std::string(20000, 'x')}},
                     .invalidates_cache = true};
                 co_await http().Fetch(std::move(request));
               }),
               http::HttpException);
}

TEST_F(HttpServerTest, RejectsTooManyHeaders) {
  EXPECT_THROW(
      Run([&]() -> Task<> {
        std::vector<std::pair<std::string, std::string>> headers(10000);
        for (int i = 0; i < 10000; i++) {
          headers[i] = {"SomeHeader", "some_value"};
        }
        co_await http().Fetch(Request{.url = address(),
                                      .headers = std::move(headers),
                                      .invalidates_cache = true});
      }),
      http::HttpException);
}

TEST_F(HttpServerTest, RejectsTooLongUrl) {
  EXPECT_THROW(
      Run([&]() -> Task<> {
        co_await http().Fetch(Request{.url = address() + std::string(5000, 'x'),
                                      .invalidates_cache = true});
      }),
      http::HttpException);
}

TEST_F(HttpServerTest, ServesManyClients) {
  const int kClientCount = 3;
  class HttpHandler {
   public:
    Task<Response> operator()(Request request, stdx::stop_token) {
      index_++;
      if (index_ == kClientCount) {
        semaphore_->SetValue();
      }
      std::string message = "message" + request.url;
      auto size = message.size();
      co_return Response{.status = 200,
                         .headers = {{"Content-Length", std::to_string(size)}},
                         .body = CreateBody(std::move(message))};
    }

    Generator<std::string> CreateBody(std::string message) {
      co_await promise_.Get(stdx::stop_token());
      co_yield message;
    }

   private:
    struct Wait {
      Task<> operator()() const { co_await *semaphore_; }
      Promise<void>* semaphore_;
    };

    std::unique_ptr<Promise<void>> semaphore_ =
        std::make_unique<Promise<void>>();
    SharedPromise<Wait> promise_{Wait{semaphore_.get()}};
    int index_ = 0;
  };
  Run(HttpHandler{}, [&]() -> Task<> {
    auto [r1, r2, r3] = co_await coro::WhenAll(http().Fetch(address() + "/1"),
                                               http().Fetch(address() + "/2"),
                                               http().Fetch(address() + "/3"));
    EXPECT_EQ(co_await http::GetBody(std::move(r1.body)), "message/1");
    EXPECT_EQ(co_await http::GetBody(std::move(r2.body)), "message/2");
    EXPECT_EQ(co_await http::GetBody(std::move(r3.body)), "message/3");
  });
}

TEST_F(HttpServerTest, ServesManyClientsWithNoContentLength) {
  const int kClientCount = 3;
  class HttpHandler {
   public:
    Task<Response> operator()(Request request, stdx::stop_token) {
      index_++;
      if (index_ == kClientCount) {
        semaphore_->SetValue();
      }
      std::string message = "message" + request.url;
      co_return Response{.status = 200, .body = CreateBody(std::move(message))};
    }

    Generator<std::string> CreateBody(std::string message) {
      co_await promise_.Get(stdx::stop_token());
      co_yield message;
    }

   private:
    struct Wait {
      Task<> operator()() const { co_await *semaphore_; }
      Promise<void>* semaphore_;
    };

    std::unique_ptr<Promise<void>> semaphore_ =
        std::make_unique<Promise<void>>();
    SharedPromise<Wait> promise_{Wait{semaphore_.get()}};
    int index_ = 0;
  };
  Run(HttpHandler{}, [&]() -> Task<> {
    auto [r1, r2, r3] = co_await coro::WhenAll(http().Fetch(address() + "/1"),
                                               http().Fetch(address() + "/2"),
                                               http().Fetch(address() + "/3"));
    EXPECT_EQ(co_await http::GetBody(std::move(r1.body)), "message/1");
    EXPECT_EQ(co_await http::GetBody(std::move(r2.body)), "message/2");
    EXPECT_EQ(co_await http::GetBody(std::move(r3.body)), "message/3");
  });
}

TEST_F(HttpServerTest, ReadsResponseBodyConcurrently) {
  class HttpHandler {
   public:
    Task<Response> operator()(Request request, stdx::stop_token) {
      co_return Response{.status = 200,
                         .body = CreateBody("message" + request.url)};
    }

    Generator<std::string> CreateBody(std::string message) { co_yield message; }
  };
  Run(HttpHandler{}, [&]() -> Task<> {
    auto [r1, r2, r3] = co_await coro::WhenAll(http().Fetch(address() + "/1"),
                                               http().Fetch(address() + "/2"),
                                               http().Fetch(address() + "/3"));
    auto [b1, b2, b3] = co_await coro::WhenAll(
        http::GetBody(std::move(r1.body)), http::GetBody(std::move(r2.body)),
        http::GetBody(std::move(r3.body)));
    EXPECT_EQ(b1, "message/1");
    EXPECT_EQ(b2, "message/2");
    EXPECT_EQ(b3, "message/3");
  });
}

TEST_F(HttpServerTest, CancelsHttpRequest) {
  class HttpHandler {
   public:
    HttpHandler(Promise<void>* semaphore, Promise<void>* request_received)
        : semaphore_(semaphore), request_received_(request_received) {}

    Task<Response> operator()(Request request, stdx::stop_token) {
      request_received_->SetValue();
      co_await *semaphore_;
      auto message = "message" + request.url;
      auto size = message.size();
      co_return Response{.status = 200,
                         .headers = {{"Content-Length", std::to_string(size)}},
                         .body = CreateBody(std::move(message))};
    }

    Generator<std::string> CreateBody(std::string message) { co_yield message; }

   private:
    Promise<void>* semaphore_;
    Promise<void>* request_received_;
  };
  Promise<void> semaphore;
  Promise<void> request_received;
  Run(HttpHandler{&semaphore, &request_received}, [&]() -> Task<> {
    stdx::stop_source stop_source;
    auto cancel_task = [](Promise<void>& request_received,
                          stdx::stop_source& stop_source) -> Task<int> {
      co_await request_received;
      stop_source.request_stop();
      co_return 0;
    };
    EXPECT_THROW(
        co_await coro::WhenAll(http().Fetch(address(), stop_source.get_token()),
                               cancel_task(request_received, stop_source)),
        InterruptedException);
    semaphore.SetValue();
  });
}

TEST_F(HttpServerTest, CancelsHttpRequestWhenReadingBody) {
  class HttpHandler {
   public:
    HttpHandler(Promise<void>* semaphore, Promise<void>* request_received)
        : semaphore_(semaphore), request_received_(request_received) {}

    Task<Response> operator()(Request request, stdx::stop_token) {
      auto message = "message" + request.url;
      co_return Response{.status = 200,
                         .headers = {{"SomeHeader", "SomeValue"}},
                         .body = CreateBody(std::move(message))};
    }

    Generator<std::string> CreateBody(std::string message) {
      request_received_->SetValue();
      co_yield "wtf1";
      co_yield "wtf2";
      co_await *semaphore_;
      co_yield message;
    }

   private:
    Promise<void>* semaphore_;
    Promise<void>* request_received_;
  };
  Promise<void> semaphore;
  Promise<void> request_received;
  Run(HttpHandler{&semaphore, &request_received}, [&]() -> Task<> {
    struct {
      Task<int> CancelTask() {
        co_await received_headers;
        stop_source.request_stop();
        co_return 0;
      }

      Task<std::string> Fetch(std::string address) {
        auto response = co_await http.Fetch(address, stop_source.get_token());
        co_await request_received;
        EXPECT_THAT(response.headers,
                    Contains(std::make_pair("someheader", "SomeValue")));
        received_headers.SetValue();
        co_return co_await GetBody(std::move(response.body));
      };

      CurlHttp& http;
      Promise<void>& semaphore;
      Promise<void>& request_received;
      Promise<void> received_headers;
      stdx::stop_source stop_source;
    } state{http(), semaphore, request_received};

    EXPECT_THROW(
        co_await coro::WhenAll(state.Fetch(address()), state.CancelTask()),
        InterruptedException);
    semaphore.SetValue();
  });
}

TEST_F(HttpServerTest, ReadsChunkedResponse) {
  class HttpHandler {
   public:
    HttpHandler(Promise<void>* semaphore, Promise<void>* request_received)
        : semaphore_(semaphore), request_received_(request_received) {}

    Task<Response> operator()(Request request, stdx::stop_token) {
      auto message = "message" + request.url;
      co_return Response{.status = 200, .body = CreateBody(std::move(message))};
    }

    Generator<std::string> CreateBody(std::string message) {
      request_received_->SetValue();
      co_yield "wtf1";
      co_yield "wtf2";
      co_await *semaphore_;
      co_yield message;
    }

   private:
    Promise<void>* semaphore_;
    Promise<void>* request_received_;
  };
  Promise<void> semaphore;
  Promise<void> request_received;
  Run(HttpHandler{&semaphore, &request_received}, [&]() -> Task<> {
    auto response = co_await http().Fetch(address() + "/test");
    co_await request_received;
    std::string message;
    FOR_CO_AWAIT(std::string_view chunk, std::move(response.body)) {
      if (message.empty()) {
        semaphore.SetValue();
      }
      message += chunk;
    }
    EXPECT_EQ(message, "wtf1wtf2message/test");
  });
}

TEST_F(HttpServerTest, HandlesServerSideInterrupt) {
  class HttpHandler {
   public:
    explicit HttpHandler(Promise<void>* request_received)
        : request_received_(request_received) {}

    Task<Response> operator()(Request request, stdx::stop_token stop_token) {
      co_return Response{.status = 200,
                         .body = CreateBody(std::move(stop_token))};
    }

    Generator<std::string> CreateBody(stdx::stop_token stop_token) {
      Promise<void> interrupt;
      stdx::stop_callback cb(
          stop_token, [&] { interrupt.SetException(InterruptedException()); });
      request_received_->SetValue();
      co_yield "wtf1";
      co_yield "wtf2";
      co_await interrupt;
    }

   private:
    Promise<void>* request_received_;
  };
  Promise<void> request_received;
  Run(HttpHandler{&request_received}, [&]() -> Task<> {
    auto response = co_await http().Fetch(address() + "/test");
    co_await request_received;
    EXPECT_THROW(
        co_await WhenAll(http::GetBody(std::move(response.body)), Quit()),
        HttpException);
  });
}

}  // namespace
}  // namespace coro::http
