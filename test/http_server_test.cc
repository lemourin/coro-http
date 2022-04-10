#include "coro/http/http_server.h"

#include <event2/event.h>
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
  co_return ResponseContent{
      .status = r.status,
      .headers = std::move(r.headers),
      .body = co_await GetBody(std::move(r.body)),
  };
}

class HttpHandler {
 public:
  HttpHandler(std::optional<coro::http::Request<std::string>>* request,
              Response* response)
      : request_(request), response_(response) {}

  Task<Response> operator()(Request request, stdx::stop_token) const {
    *request_ = coro::http::Request<std::string>{
        .url = std::move(request.url),
        .method = request.method,
        .headers = std::move(request.headers),
        .body = request.body ? co_await GetBody(std::move(*request.body))
                             : std::optional<std::string>()};
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
    RunTask([&]() -> Task<> {
      try {
        co_await std::move(func)();
        semaphore_.SetValue();
      } catch (...) {
        semaphore_.SetException(std::current_exception());
      }
    });
    WaitForSemaphore(std::move(handler));
  }

  template <typename F>
  void Run(F func) {
    Run(HttpHandler{&last_request_, &response_}, std::move(func));
  }

  auto& http() { return http_; }
  const auto& last_request() const { return last_request_; }

 private:
  template <typename HttpHandlerT>
  void WaitForSemaphore(HttpHandlerT handler) {
    std::exception_ptr exception;
    RunTask([&]() -> Task<> {
      try {
        HttpServer<HttpHandlerT> http_server{
            base_.get(), HttpServerConfig{.address = "127.0.0.1", .port = 4444},
            std::move(handler)};
        co_await semaphore_;
        co_await http_server.Quit();
      } catch (...) {
        exception = std::current_exception();
      }
    });
    event_base_dispatch(base_.get());
    if (exception) {
      std::rethrow_exception(exception);
    }
  }

  std::unique_ptr<event_base, util::EventBaseDeleter> base_{event_base_new()};
  std::optional<coro::http::Request<std::string>> last_request_;
  Response response_{
      .status = 200,
      .headers = {{"Content-Type", "application/octet-stream"}},
      .body = CreateBody("response"),
  };
  Promise<void> semaphore_;
  CurlHttp http_{base_.get()};
};

TEST_F(HttpServerTest, SendsExpectedResponse) {
  std::optional<ResponseContent> response;
  Run([&]() -> Task<> {
    response = co_await ToResponseContent(
        co_await http().Fetch("http://127.0.0.1:4444"));
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
        Request{.url = "http://127.0.0.1:4444/some_path?some_query=value",
                .method = http::Method::kPost,
                .body = CreateBody("input")});
  });

  ASSERT_TRUE(last_request().has_value());
  EXPECT_THAT(last_request()->url, "/some_path?some_query=value");
  EXPECT_EQ(last_request()->body, "input");
}

TEST_F(HttpServerTest, RejectsTooLongHeader) {
  EXPECT_THROW(Run([&]() -> Task<> {
                 co_await http().Fetch(Request{
                     .url = "http://127.0.0.1:4444",
                     .headers = {{"SomeHeader", std::string(5000, 'x')}}});
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
        co_await http().Fetch(Request{.url = "http://127.0.0.1:4444",
                                      .headers = std::move(headers)});
      }),
      http::HttpException);
}

TEST_F(HttpServerTest, RejectsTooLongUrl) {
  EXPECT_THROW(Run([&]() -> Task<> {
                 co_await http().Fetch(
                     Request{.url = std::string("http://127.0.0.1:4444/") +
                                    std::string(5000, 'x')});
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
      co_return Response{.status = 200, .body = CreateBody(index_)};
    }

    Generator<std::string> CreateBody(int index) {
      co_await promise_.Get(stdx::stop_token());
      co_yield "message";
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
    auto [r1, r2, r3] =
        co_await coro::WhenAll(http().Fetch("http://127.0.0.1:4444"),
                               http().Fetch("http://127.0.0.1:4444"),
                               http().Fetch("http://127.0.0.1:4444"));
    EXPECT_EQ(co_await http::GetBody(std::move(r1.body)), "message");
    EXPECT_EQ(co_await http::GetBody(std::move(r2.body)), "message");
    EXPECT_EQ(co_await http::GetBody(std::move(r3.body)), "message");
  });
}

}  // namespace
}  // namespace coro::http
