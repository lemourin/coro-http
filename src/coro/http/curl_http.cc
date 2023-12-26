#include "coro/http/curl_http.h"

#include <curl/curl.h>
#include <event2/event.h>
#include <event2/event_struct.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "coro/http/http_body_generator.h"
#include "coro/interrupted_exception.h"

namespace coro::http {

namespace {

class CurlHttpImpl;
class CurlHttpOperation;
class CurlHttpBodyGenerator;

class EventData {
 public:
  EventData(struct event_base* base, evutil_socket_t fd, short events,
            event_callback_fn callback, void* callback_arg);

  ~EventData() noexcept;
  EventData(const EventData&) = delete;
  EventData(EventData&&) = delete;
  EventData& operator=(const EventData&) = delete;
  EventData& operator=(EventData&&) = delete;

  struct event* event() { return &event_; }
  const struct event* event() const { return &event_; }

 private:
  struct event event_ {};
};

template <typename T>
T* CheckNotNull(T* p) {
  if (!p) {
    throw std::runtime_error("Unexpected null pointer.");
  }
  return p;
}

void Check(CURLMcode code) {
  if (code != CURLM_OK) {
    throw HttpException(code, curl_multi_strerror(code));
  }
}

void Check(CURLcode code) {
  if (code != CURLE_OK) {
    throw HttpException(code, curl_easy_strerror(code));
  }
}

void Check(int code) {
  if (code != 0) {
    throw HttpException(code, "Unknown error.");
  }
}

struct CurlHandleDeleter {
  void operator()(CURL* handle) const noexcept {
    Check(curl_multi_remove_handle(multi_handle, handle));
    curl_easy_cleanup(handle);
  }
  CURLM* multi_handle;
};

struct CurlListDeleter {
  void operator()(curl_slist* list) const noexcept {
    curl_slist_free_all(list);
  }
};

EventData::EventData(struct event_base* base, evutil_socket_t fd, short events,
                     event_callback_fn callback, void* callback_arg) {
  Check(event_assign(&event_, base, fd, events, callback, callback_arg));
}

EventData::~EventData() noexcept {
  if (event_.ev_base) {
    Check(event_del(&event_));
  }
}

class CurlGlobalInitializer {
 public:
  CurlGlobalInitializer() { Check(curl_global_init(CURL_GLOBAL_DEFAULT)); }
  ~CurlGlobalInitializer() noexcept { curl_global_cleanup(); }
  CurlGlobalInitializer(const CurlGlobalInitializer&) = delete;
  CurlGlobalInitializer(CurlGlobalInitializer&&) = delete;
  CurlGlobalInitializer& operator=(const CurlGlobalInitializer&) = delete;
  CurlGlobalInitializer& operator=(CurlGlobalInitializer&&) = delete;
};

class CurlHandle {
 public:
  using Owner = std::variant<CurlHttpOperation*, CurlHttpBodyGenerator*>;

  CurlHandle(CURLM* http, event_base* event_loop, Request<>,
             const CurlHttpConfig& config, stdx::stop_token, Owner);

 private:
  friend class CurlHttpImpl;
  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;

  struct OnCancel {
    void operator()() const;
    CurlHandle* data;
  };

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
                                              void* userdata);

  void Cleanup();

  void HandleException(std::exception_ptr exception);

  CURLM* http_;
  event_base* event_loop_;
  std::unique_ptr<curl_slist, CurlListDeleter> header_list_;
  std::optional<Generator<std::string>> request_body_;
  std::optional<Generator<std::string>::iterator> request_body_it_;
  std::optional<size_t> request_body_chunk_index_;
  stdx::stop_token stop_token_;
  Owner owner_;
  EventData next_request_body_chunk_;
  std::unique_ptr<CURL, CurlHandleDeleter> handle_;
  stdx::stop_callback<OnCancel> stop_callback_;
};

class CurlHttpBodyGenerator : public HttpBodyGenerator<CurlHttpBodyGenerator> {
 public:
  CurlHttpBodyGenerator(std::unique_ptr<CurlHandle> handle,
                        std::string_view initial_chunk);

  void Resume();

 private:
  static void OnChunkReady(evutil_socket_t, short, void* handle);
  static void OnBodyReady(evutil_socket_t, short, void* handle);

  friend class CurlHttpOperation;
  friend class CurlHandle;
  friend class CurlHttpImpl;

  EventData chunk_ready_;
  EventData body_ready_;
  bool body_ready_fired_ = false;
  int status_ = -1;
  std::exception_ptr exception_ptr_;
  std::string data_;
  std::unique_ptr<CurlHandle> handle_;
};

class CurlHttpOperation {
 public:
  CurlHttpOperation(CURLM* http, event_base* event_loop, Request<>,
                    const CurlHttpConfig& config, stdx::stop_token);

  bool await_ready();
  void await_suspend(stdx::coroutine_handle<void> awaiting_coroutine);
  std::unique_ptr<Response<CurlHttpBodyGenerator>> await_resume();

 private:
  static void OnHeadersReady(evutil_socket_t fd, short event, void* handle);

  friend class CurlHttpImpl;
  friend class CurlHandle;

  stdx::coroutine_handle<void> awaiting_coroutine_;
  std::exception_ptr exception_ptr_;
  EventData headers_ready_;
  bool headers_ready_event_posted_ = false;
  int status_ = -1;
  std::vector<std::pair<std::string, std::string>> headers_;
  std::string body_;
  bool no_body_ = false;
  std::unique_ptr<CurlHandle> handle_;
};

class CurlHttpImpl {
 public:
  CurlHttpImpl(event_base* event_loop, CurlHttpConfig);

  CurlHttpOperation Fetch(Request<> request,
                          stdx::stop_token = stdx::stop_token()) const;

 private:
  static int SocketCallback(CURL* handle, curl_socket_t socket, int what,
                            void* userp, void* socketp);
  static int TimerCallback(CURLM* handle, long timeout_ms, void* userp);
  static void SocketEvent(evutil_socket_t fd, short event, void* multi_handle);
  static void TimeoutEvent(evutil_socket_t fd, short event, void* handle);
  static void ProcessEvents(CURLM* handle);
  static std::string GetCaCertBlob();

  friend class CurlHttpOperation;
  friend class CurlHttpBodyGenerator;
  friend class CurlHandle;

  struct CurlMultiDeleter {
    void operator()(CURLM* handle) const;
  };

  CurlGlobalInitializer initializer_;
  std::unique_ptr<CURLM, CurlMultiDeleter> curl_handle_;
  event_base* event_loop_;
  EventData timeout_event_;
  CurlHttpConfig config_;
};

void CurlHandle::Cleanup() {
  if (http_) {
    Check(curl_multi_remove_handle(http_, handle_.get()));
    http_ = nullptr;
  }
  if (next_request_body_chunk_.event()->ev_base) {
    Check(event_del(next_request_body_chunk_.event()));
  }
}

void CurlHandle::HandleException(std::exception_ptr exception) {
  if (auto* operation = std::get_if<CurlHttpOperation*>(&owner_)) {
    Cleanup();
    (*operation)->exception_ptr_ = std::move(exception);
    if ((*operation)->awaiting_coroutine_) {
      std::exchange((*operation)->awaiting_coroutine_, nullptr).resume();
    }
  } else if (auto* generator = std::get_if<CurlHttpBodyGenerator*>(&owner_)) {
    Cleanup();
    (*generator)->exception_ptr_ = std::move(exception);
    (*generator)->Close((*generator)->exception_ptr_);
  }
}

size_t CurlHandle::HeaderCallback(char* buffer, size_t size, size_t nitems,
                                  void* userdata) {
  auto* data = reinterpret_cast<CurlHandle*>(userdata);
  if (!std::holds_alternative<CurlHttpOperation*>(data->owner_)) {
    return 0;
  }
  auto* http_operation = std::get<CurlHttpOperation*>(data->owner_);
  std::string_view view(buffer, size * nitems);
  auto index = view.find_first_of(':');
  if (index != std::string::npos) {
    http_operation->headers_.emplace_back(
        ToLowerCase(std::string(view.begin(), view.begin() + index)),
        TrimWhitespace(std::string(view.begin() + index + 1, view.end())));
  } else if (view.starts_with("HTTP")) {
    std::istringstream stream{std::string(view)};
    std::string http_version;
    int code;
    stream >> http_version >> code;
    http_operation->headers_.clear();
    http_operation->status_ = code;
  }
  return size * nitems;
}

size_t CurlHandle::WriteCallback(char* ptr, size_t size, size_t nmemb,
                                 void* userdata) {
  auto* data = reinterpret_cast<CurlHandle*>(userdata);
  if (std::holds_alternative<CurlHttpOperation*>(data->owner_)) {
    auto* http_operation = std::get<CurlHttpOperation*>(data->owner_);
    if (!http_operation->headers_ready_event_posted_) {
      http_operation->headers_ready_event_posted_ = true;
      evuser_trigger(http_operation->headers_ready_.event());
    }
    http_operation->body_ += std::string(ptr, ptr + size * nmemb);
  } else if (std::holds_alternative<CurlHttpBodyGenerator*>(data->owner_)) {
    auto* http_body_generator = std::get<CurlHttpBodyGenerator*>(data->owner_);
    if (!http_body_generator->data_.empty() ||
        http_body_generator->GetBufferedByteCount() > 0) {
      return CURL_WRITEFUNC_PAUSE;
    }
    http_body_generator->data_ += std::string(ptr, ptr + size * nmemb);
    evuser_trigger(http_body_generator->chunk_ready_.event());
  }
  return size * nmemb;
}

int CurlHandle::ProgressCallback(void* clientp, curl_off_t /*dltotal*/,
                                 curl_off_t /*dlnow*/, curl_off_t /*ultotal*/,
                                 curl_off_t /*ulnow*/) {
  auto* data = reinterpret_cast<CurlHandle*>(clientp);
  return data->stop_token_.stop_requested() ? -1 : 0;
}

size_t CurlHandle::ReadCallback(char* buffer, size_t size, size_t nitems,
                                void* userdata) {
  auto* data = reinterpret_cast<CurlHandle*>(userdata);
  if (!data->request_body_it_ || !data->request_body_chunk_index_) {
    return CURL_READFUNC_PAUSE;
  }
  if (data->request_body_it_ == std::end(*data->request_body_)) {
    return 0;
  }
  std::string& current_chunk = **data->request_body_it_;
  size_t offset = 0;
  for (; offset < size * nitems &&
         data->request_body_chunk_index_ < current_chunk.size();
       offset++) {
    buffer[offset] = current_chunk[(*data->request_body_chunk_index_)++];
  }
  if (data->request_body_chunk_index_ == current_chunk.size()) {
    data->request_body_chunk_index_ = std::nullopt;
    evuser_trigger(data->next_request_body_chunk_.event());
  }
  return offset > 0 ? offset : CURL_READFUNC_PAUSE;
}

void CurlHandle::OnNextRequestBodyChunkRequested(evutil_socket_t, short,
                                                 void* userdata) {
  RunTask([data = reinterpret_cast<CurlHandle*>(userdata)]() -> Task<> {
    try {
      data->request_body_it_ = co_await ++*data->request_body_it_;
      data->request_body_chunk_index_ = 0;
      curl_easy_pause(data->handle_.get(), CURLPAUSE_SEND_CONT);
    } catch (...) {
      data->HandleException(std::current_exception());
    }
  });
}

void CurlHandle::OnCancel::operator()() const {
  data->HandleException(std::make_exception_ptr(InterruptedException()));
}

CurlHandle::CurlHandle(CURLM* http, event_base* event_loop, Request<> request,
                       const CurlHttpConfig& config,
                       stdx::stop_token stop_token, Owner owner)
    : http_(http),
      event_loop_(event_loop),
      header_list_(),
      request_body_(std::move(request.body)),
      stop_token_(std::move(stop_token)),
      owner_(owner),
      next_request_body_chunk_(event_loop, -1, 0,
                               OnNextRequestBodyChunkRequested, this),
      handle_(CheckNotNull(curl_easy_init()),
              CurlHandleDeleter{.multi_handle = http}),
      stop_callback_(stop_token_, OnCancel{this}) {
  Check(curl_easy_setopt(handle_.get(), CURLOPT_URL, request.url.data()));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_PRIVATE, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_WRITEFUNCTION, WriteCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_WRITEDATA, this));
  Check(
      curl_easy_setopt(handle_.get(), CURLOPT_HEADERFUNCTION, HeaderCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_HEADERDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_XFERINFOFUNCTION,
                         ProgressCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_XFERINFODATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_READFUNCTION, ReadCallback));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_READDATA, this));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_NOPROGRESS, 0L));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_SSL_VERIFYPEER, 1L));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_CUSTOMREQUEST,
                         std::string(MethodToString(request.method)).c_str()));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_HTTP_VERSION,
                         CURL_HTTP_VERSION_NONE));
  Check(curl_easy_setopt(handle_.get(), CURLOPT_SSL_OPTIONS,
                         CURLSSLOPT_NATIVE_CA));
  if (request.method == Method::kHead) {
    Check(curl_easy_setopt(handle_.get(), CURLOPT_NOBODY, 1L));
  }
  if (config.alt_svc_path) {
    Check(curl_easy_setopt(handle_.get(), CURLOPT_ALTSVC,
                           config.alt_svc_path->c_str()));
  }
  if (config.ca_cert_blob && !config.ca_cert_blob->empty()) {
    curl_blob ca_cert{.data = const_cast<void*>(reinterpret_cast<const void*>(
                          config.ca_cert_blob->data())),
                      .len = config.ca_cert_blob->size()};
    Check(curl_easy_setopt(handle_.get(), CURLOPT_CAINFO_BLOB, &ca_cert));
  }
  std::optional<curl_off_t> content_length;
  for (const auto& [header_name, header_value] : request.headers) {
    std::string header_line = header_name;
    header_line += ": ";
    header_line += header_value;
    header_list_.reset(CheckNotNull(
        curl_slist_append(header_list_.release(), header_line.c_str())));
    if (ToLowerCase(header_name) == "content-length") {
      content_length = std::stoll(header_value);
    }
  }
  Check(
      curl_easy_setopt(handle_.get(), CURLOPT_HTTPHEADER, header_list_.get()));

  if (request_body_) {
    if (request.method == Method::kPost) {
      Check(curl_easy_setopt(handle_.get(), CURLOPT_POST, 1L));
      if (content_length) {
        Check(curl_easy_setopt(handle_.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                               *content_length));
      }
    } else {
      curl_easy_setopt(handle_.get(), CURLOPT_UPLOAD, 1L);
      if (content_length) {
        Check(curl_easy_setopt(handle_.get(), CURLOPT_INFILESIZE_LARGE,
                               *content_length));
      }
    }
    RunTask([d = this]() -> Task<> {
      try {
        d->request_body_it_ = co_await d->request_body_->begin();
        d->request_body_chunk_index_ = 0;
        curl_easy_pause(d->handle_.get(), CURLPAUSE_SEND_CONT);
      } catch (...) {
        d->HandleException(std::current_exception());
      }
    });
  }

  Check(curl_multi_add_handle(http, handle_.get()));
}

CurlHttpBodyGenerator::CurlHttpBodyGenerator(std::unique_ptr<CurlHandle> handle,
                                             std::string_view initial_chunk)
    : chunk_ready_(handle->event_loop_, -1, 0, OnChunkReady, this),
      body_ready_(handle->event_loop_, -1, 0, OnBodyReady, this),
      handle_(std::move(handle)) {
  handle_->owner_ = this;
  ReceivedData(initial_chunk);
}

void CurlHttpBodyGenerator::OnChunkReady(evutil_socket_t, short, void* handle) {
  auto* curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  std::string data = std::move(curl_http_body_generator->data_);
  curl_http_body_generator->data_.clear();
  if (curl_http_body_generator->status_ != -1 &&
      !curl_http_body_generator->body_ready_fired_) {
    curl_http_body_generator->body_ready_fired_ = true;
    evuser_trigger(curl_http_body_generator->body_ready_.event());
  }
  curl_http_body_generator->ReceivedData(std::move(data));
}

void CurlHttpBodyGenerator::OnBodyReady(evutil_socket_t, short, void* handle) {
  auto* curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  if (curl_http_body_generator->exception_ptr_) {
    curl_http_body_generator->Close(curl_http_body_generator->exception_ptr_);
  } else {
    curl_http_body_generator->Close(curl_http_body_generator->status_);
  }
}

void CurlHttpBodyGenerator::Resume() {
  if (status_ == -1 && !exception_ptr_) {
    curl_easy_pause(handle_->handle_.get(), CURLPAUSE_RECV_CONT);
  }
}

CurlHttpOperation::CurlHttpOperation(CURLM* http, event_base* event_loop,
                                     Request<> request,
                                     const CurlHttpConfig& config,
                                     stdx::stop_token stop_token)
    : headers_ready_(event_loop, -1, 0, OnHeadersReady, this),
      handle_(std::make_unique<CurlHandle>(http, event_loop, std::move(request),
                                           config, std::move(stop_token),
                                           this)) {}

void CurlHttpOperation::OnHeadersReady(evutil_socket_t, short, void* handle) {
  auto* http_operation = reinterpret_cast<CurlHttpOperation*>(handle);
  if (http_operation->awaiting_coroutine_) {
    std::exchange(http_operation->awaiting_coroutine_, nullptr).resume();
  }
}

bool CurlHttpOperation::await_ready() {
  return exception_ptr_ || status_ != -1;
}

void CurlHttpOperation::await_suspend(
    stdx::coroutine_handle<void> awaiting_coroutine) {
  awaiting_coroutine_ = awaiting_coroutine;
}

std::unique_ptr<Response<CurlHttpBodyGenerator>>
CurlHttpOperation::await_resume() {
  if (exception_ptr_) {
    std::rethrow_exception(exception_ptr_);
  }
  std::unique_ptr<Response<CurlHttpBodyGenerator>> response(
      new Response<CurlHttpBodyGenerator>{
          .status = status_,
          .headers = std::move(headers_),
          .body = {std::move(handle_), std::move(body_)}});
  if (no_body_) {
    response->body.Close(status_);
  }
  return response;
}

CurlHttpImpl::CurlHttpImpl(event_base* event_loop, CurlHttpConfig config)
    : curl_handle_(CheckNotNull(curl_multi_init())),
      event_loop_(event_loop),
      timeout_event_(event_loop, -1, 0, TimeoutEvent, curl_handle_.get()),
      config_(std::move(config)) {
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETFUNCTION,
                          SocketCallback));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERFUNCTION,
                          TimerCallback));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERDATA, this));
}

void CurlHttpImpl::TimeoutEvent(evutil_socket_t, short, void* handle) {
  int running_handles;
  Check(curl_multi_socket_action(handle, CURL_SOCKET_TIMEOUT, 0,
                                 &running_handles));
  ProcessEvents(handle);
}

void CurlHttpImpl::ProcessEvents(CURLM* multi_handle) {
  CURLMsg* message;
  do {
    int message_count;
    message = curl_multi_info_read(multi_handle, &message_count);
    if (message && message->msg == CURLMSG_DONE) {
      CURL* handle = message->easy_handle;
      CurlHandle* data;
      Check(curl_easy_getinfo(handle, CURLINFO_PRIVATE, &data));
      if (std::holds_alternative<CurlHttpOperation*>(data->owner_)) {
        auto* operation = std::get<CurlHttpOperation*>(data->owner_);
        if (message->data.result == CURLE_OK) {
          long response_code;
          Check(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE,
                                  &response_code));
          operation->status_ = static_cast<int>(response_code);
        } else {
          operation->exception_ptr_ = std::make_exception_ptr(HttpException(
              message->data.result, curl_easy_strerror(message->data.result)));
        }
        operation->no_body_ = true;
        Check(event_base_once(
            operation->handle_->event_loop_, -1, EV_TIMEOUT,
            [](evutil_socket_t, short, void* handle) {
              stdx::coroutine_handle<void>::from_address(handle).resume();
            },
            std::exchange(operation->awaiting_coroutine_, nullptr).address(),
            nullptr));
      } else if (std::holds_alternative<CurlHttpBodyGenerator*>(data->owner_)) {
        auto* curl_http_body_generator =
            std::get<CurlHttpBodyGenerator*>(data->owner_);
        curl_http_body_generator->status_ =
            static_cast<int>(message->data.result);
        if (message->data.result != CURLE_OK) {
          curl_http_body_generator->exception_ptr_ = std::make_exception_ptr(
              HttpException(message->data.result,
                            curl_easy_strerror(message->data.result)));
        }
        if (!(curl_http_body_generator->chunk_ready_.event()
                  ->ev_evcallback.evcb_flags &
              EVLIST_ACTIVE)) {
          curl_http_body_generator->body_ready_fired_ = true;
          evuser_trigger(curl_http_body_generator->body_ready_.event());
        }
      }
    }
  } while (message != nullptr);
}

void CurlHttpImpl::SocketEvent(evutil_socket_t fd, short event, void* handle) {
  int running_handles;
  Check(
      curl_multi_socket_action(handle, fd,
                               ((event & EV_READ) ? CURL_CSELECT_IN : 0) |
                                   ((event & EV_WRITE) ? CURL_CSELECT_OUT : 0),
                               &running_handles));
  ProcessEvents(handle);
}

int CurlHttpImpl::SocketCallback(CURL*, curl_socket_t socket, int what,
                                 void* userp, void* socketp) {
  auto* http = reinterpret_cast<CurlHttpImpl*>(userp);
  if (what == CURL_POLL_REMOVE) {
    delete reinterpret_cast<EventData*>(socketp);
  } else {
    auto* data = reinterpret_cast<EventData*>(socketp);
    auto events = static_cast<short>(((what & CURL_POLL_IN) ? EV_READ : 0) |
                                     ((what & CURL_POLL_OUT) ? EV_WRITE : 0) |
                                     EV_PERSIST);
    if (!data) {
      data = new EventData(http->event_loop_, socket, events, SocketEvent,
                           http->curl_handle_.get());
      Check(curl_multi_assign(http->curl_handle_.get(), socket, data));
    } else {
      Check(event_del(data->event()));
      Check(event_assign(data->event(), http->event_loop_, socket, events,
                         SocketEvent, http->curl_handle_.get()));
    }
    Check(event_add(data->event(), /*timeout=*/nullptr));
  }
  return 0;
}

int CurlHttpImpl::TimerCallback(CURLM*, long timeout_ms, void* userp) {
  auto* http = reinterpret_cast<CurlHttpImpl*>(userp);
  if (timeout_ms == -1) {
    Check(event_del(http->timeout_event_.event()));
  } else {
    timeval tv = {
        .tv_sec = static_cast<decltype(tv.tv_sec)>(timeout_ms / 1000),
        .tv_usec = static_cast<decltype(tv.tv_usec)>(timeout_ms % 1000 * 1000)};
    Check(event_add(http->timeout_event_.event(), &tv));
  }
  return 0;
}

CurlHttpOperation CurlHttpImpl::Fetch(Request<> request,
                                      stdx::stop_token token) const {
  return {curl_handle_.get(), event_loop_, std::move(request), config_,
          std::move(token)};
}

void CurlHttpImpl::CurlMultiDeleter::operator()(CURLM* handle) const {
  Check(curl_multi_cleanup(handle));
}

Generator<std::string> ToBody(
    std::unique_ptr<Response<CurlHttpBodyGenerator>> d) {
  FOR_CO_AWAIT(std::string & chunk, d->body) { co_yield std::move(chunk); }
}

}  // namespace

struct CurlHttp::Impl {
  CurlHttpImpl impl;
};

std::string GetNativeCaCertBlob() {
  constexpr int kMaxCaCertBlobSize = 1024 * 1024 * 10;
  constexpr int kBufferSize = 4 * 1024;
  std::string blob;
  for (std::string_view cert_directory :
       std::initializer_list<std::string_view>{
#if defined(__ANDROID__)
           "/system/etc/security/cacerts",
#elif !defined(_WIN32)
           "/etc/ssl/certs", "/etc/pki/ca-trust/source/anchors",
           "/etc/pki/tls/certs"
#endif
       }) {
    std::string buffer(kBufferSize, 0);
    std::filesystem::path cert_directory_path(cert_directory);
    if (!std::filesystem::is_directory(cert_directory_path)) {
      continue;
    }
    for (const auto& e :
         std::filesystem::directory_iterator(cert_directory_path)) {
      if (!std::filesystem::is_regular_file(e)) {
        continue;
      }
      std::ifstream stream(e.path().string(), std::ifstream::binary);
      while (stream) {
        stream.read(buffer.data(), buffer.size());
        blob += std::string(buffer.data(), stream.gcount());
        if (blob.size() > kMaxCaCertBlobSize) {
          throw RuntimeError("Native CA bundle too large.");
        }
      }
    }
  }

  return blob;
}

CurlHttp::CurlHttp(const coro::util::EventLoop* event_loop,
                   CurlHttpConfig config)
    : d_(new Impl{
          {reinterpret_cast<struct event_base*>(GetEventLoop(*event_loop)),
           std::move(config)}}) {}

CurlHttp::~CurlHttp() = default;

CurlHttp::CurlHttp(CurlHttp&&) noexcept = default;

CurlHttp& CurlHttp::operator=(CurlHttp&&) noexcept = default;

Task<Response<>> CurlHttp::Fetch(Request<> request,
                                 stdx::stop_token stop_token) const {
  auto response =
      co_await d_->impl.Fetch(std::move(request), std::move(stop_token));
  auto status = response->status;
  auto headers = std::move(response->headers);
  co_return Response<>{.status = status,
                       .headers = std::move(headers),
                       .body = ToBody(std::move(response))};
}

}  // namespace coro::http
