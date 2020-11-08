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

namespace internal {

inline void Check(CURLMcode code) {
  if (code != CURLM_OK) {
    throw HttpException(code, curl_multi_strerror(code));
  }
}

inline void Check(CURLcode code) {
  if (code != CURLE_OK) {
    throw HttpException(code, curl_easy_strerror(code));
  }
}

inline void Check(int code) {
  if (code != 0) {
    throw HttpException(code, "Unknown error.");
  }
}

}  // namespace internal

class CurlHandle {
 public:
  template <typename Owner>
  CurlHandle(CurlHttp*, const Request&, stdx::stop_token&&, Owner*);

  template <typename NewOwner>
  CurlHandle(CurlHandle&&, NewOwner*);

  ~CurlHandle();

 private:
  friend class CurlHttp;
  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;

  static size_t WriteCallback(char* ptr, size_t size, size_t nmemb,
                              void* userdata);
  static size_t HeaderCallback(char* buffer, size_t size, size_t nitems,
                               void* userdata);
  static int ProgressCallback(void* clientp, curl_off_t dltotal,
                              curl_off_t dlnow, curl_off_t ultotal,
                              curl_off_t ulnow);

  CurlHttp* http_;
  CURL* handle_;
  curl_slist* header_list_;
  stdx::stop_token stop_token_;
  std::variant<CurlHttpOperation*, CurlHttpBodyGenerator*> owner_;
};

class CurlHttpBodyGenerator : public HttpBodyGenerator {
 public:
  explicit CurlHttpBodyGenerator(CurlHandle&& handle);
  ~CurlHttpBodyGenerator() override;

 private:
  friend class CurlHttpOperation;
  friend class CurlHandle;
  friend class CurlHttp;

  void Pause() override;
  void Resume() override;

  CurlHandle handle_;
  event chunk_ready_;
  event body_ready_;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
  std::string data_;
};

class CurlHttpOperation : public HttpOperation {
 public:
  CurlHttpOperation(CurlHttp* http, Request&&, stdx::stop_token&&);
  ~CurlHttpOperation() override;

  void resume();

 private:
  friend class CurlHttp;
  friend class CurlHandle;

  bool await_ready() override;
  void await_suspend(stdx::coroutine_handle<void> awaiting_coroutine) override;
  Response await_resume() override;

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

class CurlHttp : public Http {
 public:
  explicit CurlHttp(event_base* event_loop);
  ~CurlHttp() override;

  std::unique_ptr<HttpOperation> Fetch(Request request,
                                       stdx::stop_token) override;

 private:
  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;
  friend class CurlHandle;

  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);
  static void SocketEvent(evutil_socket_t fd, short event, void* multi_handle);
  static void ProcessEvents(CURLM* handle);

  CURLM* curl_handle_;
  event_base* event_loop_;
  event timeout_event_;
};

template <typename Owner>
CurlHandle::CurlHandle(CurlHttp* http, const Request& request,
                       stdx::stop_token&& stop_token, Owner* owner)
    : http_(http),
      handle_(curl_easy_init()),
      header_list_(),
      stop_token_(std::move(stop_token)),
      owner_(owner) {
  using internal::Check;

  Check(curl_easy_setopt(handle_, CURLOPT_URL, request.url.data()));
  Check(curl_easy_setopt(handle_, CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEFUNCTION, WriteCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERFUNCTION, HeaderCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFOFUNCTION, ProgressCallback));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFODATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_NOPROGRESS, 0L));
  Check(curl_easy_setopt(handle_, CURLOPT_SSL_VERIFYPEER, 0L));
  for (const auto& [header_name, header_value] : request.headers) {
    std::string header_line = header_name;
    header_line += ": ";
    header_line += header_value;
    header_list_ = curl_slist_append(header_list_, header_line.c_str());
  }
  Check(curl_easy_setopt(handle_, CURLOPT_HTTPHEADER, header_list_));
  Check(curl_multi_add_handle(http->curl_handle_, handle_));
}

template <typename NewOwner>
CurlHandle::CurlHandle(CurlHandle&& handle, NewOwner* owner)
    : http_(handle.http_),
      handle_(handle.handle_),
      header_list_(handle.header_list_),
      stop_token_(std::move(handle.stop_token_)),
      owner_(owner) {
  using internal::Check;

  handle.http_ = nullptr;

  Check(curl_easy_setopt(handle_, CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_, CURLOPT_WRITEDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_, CURLOPT_XFERINFODATA, this));
}

}  // namespace coro::http

#endif  // CORO_HTTP_SRC_CORO_HTTP_CURL_HTTP_H_
