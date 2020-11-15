#ifndef CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
#define CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_

#include <curl/curl.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include <variant>

#include "http.h"

namespace coro::http {

class CurlHttp;
class CurlHttpOperation;
class CurlHttpBodyGenerator;

class CurlHandle {
 public:
  CurlHandle(const CurlHandle&) = delete;
  CurlHandle(CurlHandle&&) noexcept;
  ~CurlHandle();

  CurlHandle& operator=(const CurlHandle&) = delete;
  CurlHandle& operator=(CurlHandle&&) = delete;

 private:
  template <typename Owner>
  CurlHandle(CurlHttp*, const Request&, stdx::stop_token&&, Owner*);

  template <typename NewOwner>
  CurlHandle(CurlHandle&&, NewOwner*);

  static size_t WriteCallback(char* ptr, size_t size, size_t nmemb,
                              void* userdata);
  static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                               void* userdata);
  static int ProgressCallback(void* clientp, curl_off_t dltotal,
                              curl_off_t dlnow, curl_off_t ultotal,
                              curl_off_t ulnow);

  friend class CurlHttp;
  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;

  CurlHttp* http_;
  CURL* handle_;
  curl_slist* header_list_;
  stdx::stop_token stop_token_;
  std::variant<CurlHttpOperation*, CurlHttpBodyGenerator*> owner_;
};

class CurlHttpBodyGenerator : public HttpBodyGenerator<CurlHttpBodyGenerator> {
 public:
  CurlHttpBodyGenerator(CurlHandle&& handle, std::string&& initial_chunk);

  CurlHttpBodyGenerator(const CurlHttpBodyGenerator&) = delete;
  CurlHttpBodyGenerator(CurlHttpBodyGenerator&&) = delete;
  ~CurlHttpBodyGenerator();

  CurlHttpBodyGenerator& operator=(const CurlHttpBodyGenerator&) = delete;
  CurlHttpBodyGenerator& operator=(CurlHttpBodyGenerator&&) = delete;

  void Pause();
  void Resume();

 private:
  static void OnChunkReady(evutil_socket_t, short, void* handle);
  static void OnBodyReady(evutil_socket_t, short, void* handle);

  friend class CurlHttpOperation;
  friend class CurlHandle;
  friend class CurlHttp;

  CurlHandle handle_;
  event chunk_ready_;
  event body_ready_;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
  std::string data_;
};

class CurlHttpOperation {
 public:
  CurlHttpOperation(const CurlHttpOperation&) = delete;
  CurlHttpOperation(CurlHttpOperation&&) = delete;
  ~CurlHttpOperation();

  CurlHttpOperation& operator=(const CurlHttpOperation&) = delete;
  CurlHttpOperation& operator=(CurlHttpOperation&&) = delete;

  bool await_ready();
  void await_suspend(stdx::coroutine_handle<void> awaiting_coroutine);
  Response<std::unique_ptr<CurlHttpBodyGenerator>> await_resume();

 private:
  CurlHttpOperation(CurlHttp* http, Request&&, stdx::stop_token&&);

  void resume();

  friend class CurlHttp;
  friend class CurlHandle;

  Request request_;
  stdx::coroutine_handle<void> awaiting_coroutine_;
  CurlHandle handle_;
  std::exception_ptr exception_ptr_;
  event headers_ready_;
  bool headers_ready_event_posted_;
  int status_ = -1;
  std::unordered_multimap<std::string, std::string> headers_;
  std::string body_;
};

class CurlHttp {
 public:
  explicit CurlHttp(event_base* event_loop);
  CurlHttp(const CurlHttp&) = delete;
  CurlHttp(CurlHttp&&) = delete;
  ~CurlHttp();

  CurlHttp& operator=(const CurlHttp&) = delete;
  CurlHttp& operator=(CurlHttp&&) = delete;

  CurlHttpOperation Fetch(Request request,
                          stdx::stop_token = stdx::stop_token());
  CurlHttpOperation Fetch(std::string url,
                          stdx::stop_token = stdx::stop_token());

 private:
  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);
  static void SocketEvent(evutil_socket_t fd, short event, void* multi_handle);
  static void ProcessEvents(CURLM* handle);

  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;
  friend class CurlHandle;

  CURLM* curl_handle_;
  event_base* event_loop_;
  event timeout_event_;
};

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
