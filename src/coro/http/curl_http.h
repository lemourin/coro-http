#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include <coro/promise.h>
#include <coro/stdx/stop_callback.h>
#include <curl/curl.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include <variant>

#include "http.h"

namespace coro::http {

class CurlHttpImpl;
class CurlHttpOperation;
class CurlHttpBodyGenerator;

class CurlHandle {
 public:
  CurlHandle(CurlHandle&&) = default;
  CurlHandle& operator=(CurlHandle&&) = default;

  ~CurlHandle();

 private:
  template <typename Owner>
  CurlHandle(CURLM* http, event_base* event_loop, Request<>, stdx::stop_token,
             Owner*);

  template <typename NewOwner>
  CurlHandle(CurlHandle, NewOwner*);

  static size_t WriteCallback(char* ptr, size_t size, size_t nmemb,
                              void* userdata);
  static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                               void* userdata);
  static int ProgressCallback(void* clientp, curl_off_t dltotal,
                              curl_off_t dlnow, curl_off_t ultotal,
                              curl_off_t ulnow);
  static size_t ReadCallback(char* buffer, size_t size, size_t nitems,
                             void* userdata);
  static void OnNextRequestBodyChunkRequested(evutil_socket_t, short,
                                              void* handle);

  struct Data;

  struct OnCancel {
    void operator()() const;
    Data* data;
  };

  friend class CurlHttpImpl;
  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;

  std::unique_ptr<Data> d_;
};

class CurlHttpBodyGenerator : public HttpBodyGenerator<CurlHttpBodyGenerator> {
 public:
  CurlHttpBodyGenerator(CurlHandle handle, std::string initial_chunk);

  CurlHttpBodyGenerator(const CurlHttpBodyGenerator&) = delete;
  CurlHttpBodyGenerator(CurlHttpBodyGenerator&&) noexcept;
  ~CurlHttpBodyGenerator();

  CurlHttpBodyGenerator& operator=(const CurlHttpBodyGenerator&) = delete;
  CurlHttpBodyGenerator& operator=(CurlHttpBodyGenerator&&) noexcept;

  void Resume();

 private:
  static void OnChunkReady(evutil_socket_t, short, void* handle);
  static void OnBodyReady(evutil_socket_t, short, void* handle);

  friend class CurlHttpOperation;
  friend class CurlHandle;
  friend class CurlHttpImpl;

  event chunk_ready_;
  event body_ready_;
  bool body_ready_fired_ = false;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
  std::string data_;
  CurlHandle handle_;
};

class CurlHttpOperation {
 public:
  CurlHttpOperation(CURLM* http, event_base* event_loop, Request<>,
                    stdx::stop_token);
  CurlHttpOperation(const CurlHttpOperation&) = delete;
  CurlHttpOperation(CurlHttpOperation&&) noexcept;
  ~CurlHttpOperation();

  CurlHttpOperation& operator=(const CurlHttpOperation&) = delete;
  CurlHttpOperation& operator=(CurlHttpOperation&&) = delete;

  bool await_ready();
  void await_suspend(stdx::coroutine_handle<void> awaiting_coroutine);
  Response<CurlHttpBodyGenerator> await_resume();

 private:
  static void OnHeadersReady(evutil_socket_t fd, short event, void* handle);

  friend class CurlHttpImpl;
  friend class CurlHandle;

  stdx::coroutine_handle<void> awaiting_coroutine_;
  std::exception_ptr exception_ptr_;
  event headers_ready_;
  bool headers_ready_event_posted_;
  int status_ = -1;
  std::vector<std::pair<std::string, std::string>> headers_;
  std::string body_;
  bool no_body_ = false;
  CurlHandle handle_;
};

class CurlHttpImpl {
 public:
  explicit CurlHttpImpl(event_base* event_loop);

  CurlHttpImpl(CurlHttpImpl&&) = delete;
  CurlHttpImpl& operator=(CurlHttpImpl&&) = delete;

  ~CurlHttpImpl();

  CurlHttpOperation Fetch(Request<> request,
                          stdx::stop_token = stdx::stop_token()) const;

 private:
  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);
  static void SocketEvent(evutil_socket_t fd, short event, void* multi_handle);
  static void TimeoutEvent(evutil_socket_t fd, short event, void* handle);
  static void ProcessEvents(CURLM* handle);

  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;
  friend class CurlHandle;

  struct CurlMultiDeleter {
    void operator()(CURLM* handle) const {
      if (handle) {
        curl_multi_cleanup(handle);
      }
    }
  };

  std::unique_ptr<CURLM, CurlMultiDeleter> curl_handle_;
  event_base* event_loop_;
  event timeout_event_;
};

using CurlHttp = ToHttpClient<CurlHttpImpl>;

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
