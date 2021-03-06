#include "curl_http.h"

#include <coro/interrupted_exception.h>

#include <sstream>

namespace coro::http {

namespace {

struct SocketData {
  event socket_event = {};
};

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

std::string TrimWhitespace(std::string_view str) {
  int it1 = 0;
  while (it1 < static_cast<int>(str.size()) && std::isspace(str[it1])) {
    it1++;
  }
  int it2 = static_cast<int>(str.size()) - 1;
  while (it2 > it1 && std::isspace(str[it2])) {
    it2--;
  }
  return std::string(str.begin() + it1, str.begin() + it2 + 1);
}

event MoveEvent(event* source, void* userdata) {
  event target;
  Check(event_assign(&target, source->ev_base, -1, 0,
                     source->ev_evcallback.evcb_cb_union.evcb_callback,
                     userdata));
  if (evuser_pending(source, nullptr)) {
    evuser_trigger(&target);
  }
  Check(event_del(source));
  return target;
}

void MoveAssignEvent(event* target, event* source, void* userdata) {
  if (target->ev_base) {
    event_del(target);
  }
  *target = MoveEvent(source, userdata);
}

}  // namespace

struct CurlHandle::Data {
  template <typename Owner>
  Data(CURLM* http, event_base* event_loop, Request<> request,
       stdx::stop_token stop_token, Owner* owner)
      : http(http),
        event_loop(event_loop),
        handle(curl_easy_init()),
        header_list(),
        request_body(std::move(request.body)),
        stop_token(std::move(stop_token)),
        owner(owner),
        stop_callback(this->stop_token, OnCancel{this}) {
    Check(curl_easy_setopt(handle.get(), CURLOPT_URL, request.url.data()));
    Check(curl_easy_setopt(handle.get(), CURLOPT_PRIVATE, this));
    Check(curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, WriteCallback));
    Check(curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, this));
    Check(
        curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, HeaderCallback));
    Check(curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, this));
    Check(curl_easy_setopt(handle.get(), CURLOPT_XFERINFOFUNCTION,
                           ProgressCallback));
    Check(curl_easy_setopt(handle.get(), CURLOPT_XFERINFODATA, this));
    Check(curl_easy_setopt(handle.get(), CURLOPT_READFUNCTION, ReadCallback));
    Check(curl_easy_setopt(handle.get(), CURLOPT_READDATA, this));
    Check(curl_easy_setopt(handle.get(), CURLOPT_NOPROGRESS, 0L));
    Check(curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 0L));
    Check(curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST,
                           MethodToString(request.method)));
    Check(curl_easy_setopt(handle.get(), CURLOPT_HTTP_VERSION,
                           CURL_HTTP_VERSION_1_1));
    std::optional<long> content_length;
    curl_slist* header_list = nullptr;
    for (const auto& [header_name, header_value] : request.headers) {
      std::string header_line = header_name;
      header_line += ": ";
      header_line += header_value;
      header_list = curl_slist_append(header_list, header_line.c_str());
      if (ToLowerCase(header_name) == "content-length") {
        content_length = std::stol(header_value);
      }
    }
    this->header_list =
        std::unique_ptr<curl_slist, CurlListDeleter>(header_list);
    Check(curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, header_list));

    if (request_body) {
      if (request.method == Method::kPost) {
        Check(curl_easy_setopt(handle.get(), CURLOPT_POST, 1L));
        if (content_length) {
          Check(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                                 *content_length));
        }
      } else {
        curl_easy_setopt(handle.get(), CURLOPT_UPLOAD, 1L);
        if (content_length) {
          Check(curl_easy_setopt(handle.get(), CURLOPT_INFILESIZE,
                                 *content_length));
        }
      }
      Invoke([d = this]() -> Task<> {
        try {
          d->request_body_it = co_await d->request_body->begin();
          d->request_body_chunk_index_ = 0;
          curl_easy_pause(d->handle.get(), CURLPAUSE_SEND_CONT);
        } catch (...) {
          d->HandleException(std::current_exception());
        }
      });
    }

    Check(event_assign(&next_request_body_chunk, event_loop, -1, 0,
                       OnNextRequestBodyChunkRequested, this));

    Check(curl_multi_add_handle(http, handle.get()));
  }

  ~Data() {
    Check(curl_multi_remove_handle(http, handle.get()));
    event_del(&next_request_body_chunk);
  }

  void HandleException(std::exception_ptr exception) {
    if (auto* operation = std::get_if<CurlHttpOperation*>(&owner)) {
      (*operation)->exception_ptr_ = std::current_exception();
      if ((*operation)->awaiting_coroutine_) {
        std::exchange((*operation)->awaiting_coroutine_, nullptr).resume();
      }
    } else if (auto* generator = std::get_if<CurlHttpBodyGenerator*>(&owner)) {
      (*generator)->exception_ptr_ = std::current_exception();
      (*generator)->Close((*generator)->exception_ptr_);
    }
  }

  CURLM* http;
  event_base* event_loop;
  std::unique_ptr<CURL, CurlHandleDeleter> handle;
  std::unique_ptr<curl_slist, CurlListDeleter> header_list;
  std::optional<Generator<std::string>> request_body;
  std::optional<Generator<std::string>::iterator> request_body_it;
  std::optional<size_t> request_body_chunk_index_;
  stdx::stop_token stop_token;
  std::variant<CurlHttpOperation*, CurlHttpBodyGenerator*> owner;
  event next_request_body_chunk;
  stdx::stop_callback<OnCancel> stop_callback;
};

size_t CurlHandle::HeaderCallback(char* buffer, size_t size, size_t nitems,
                                  void* userdata) {
  auto data = reinterpret_cast<CurlHandle::Data*>(userdata);
  if (!std::holds_alternative<CurlHttpOperation*>(data->owner)) {
    return 0;
  }
  auto http_operation = std::get<CurlHttpOperation*>(data->owner);
  std::string_view view(buffer, size * nitems);
  auto index = view.find_first_of(":");
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
  auto data = reinterpret_cast<CurlHandle::Data*>(userdata);
  if (std::holds_alternative<CurlHttpOperation*>(data->owner)) {
    auto http_operation = std::get<CurlHttpOperation*>(data->owner);
    if (!http_operation->headers_ready_event_posted_) {
      http_operation->headers_ready_event_posted_ = true;
      evuser_trigger(&http_operation->headers_ready_);
    }
    http_operation->body_ += std::string(ptr, ptr + size * nmemb);
  } else if (std::holds_alternative<CurlHttpBodyGenerator*>(data->owner)) {
    auto http_body_generator = std::get<CurlHttpBodyGenerator*>(data->owner);
    if (!http_body_generator->data_.empty()) {
      return CURL_WRITEFUNC_PAUSE;
    }
    http_body_generator->data_ += std::string(ptr, ptr + size * nmemb);
    evuser_trigger(&http_body_generator->chunk_ready_);
  }
  return size * nmemb;
}

int CurlHandle::ProgressCallback(void* clientp, curl_off_t /*dltotal*/,
                                 curl_off_t /*dlnow*/, curl_off_t /*ultotal*/,
                                 curl_off_t /*ulnow*/) {
  auto data = reinterpret_cast<CurlHandle::Data*>(clientp);
  return data->stop_token.stop_requested() ? -1 : 0;
}

size_t CurlHandle::ReadCallback(char* buffer, size_t size, size_t nitems,
                                void* userdata) {
  auto data = reinterpret_cast<CurlHandle::Data*>(userdata);
  if (!data->request_body_it || !data->request_body_chunk_index_) {
    return CURL_READFUNC_PAUSE;
  }
  if (data->request_body_it == std::end(*data->request_body)) {
    return 0;
  }
  std::string& current_chunk = **data->request_body_it;
  size_t offset = 0;
  for (; offset < size * nitems &&
         data->request_body_chunk_index_ < current_chunk.size();
       offset++) {
    buffer[offset] = current_chunk[(*data->request_body_chunk_index_)++];
  }
  if (data->request_body_chunk_index_ == current_chunk.size()) {
    data->request_body_chunk_index_ = std::nullopt;
    evuser_trigger(&data->next_request_body_chunk);
  }
  return offset > 0 ? offset : CURL_READFUNC_PAUSE;
}

void CurlHandle::OnNextRequestBodyChunkRequested(evutil_socket_t, short,
                                                 void* userdata) {
  Invoke([data = reinterpret_cast<CurlHandle::Data*>(userdata)]() -> Task<> {
    try {
      data->request_body_it = co_await ++*data->request_body_it;
      data->request_body_chunk_index_ = 0;
      curl_easy_pause(data->handle.get(), CURLPAUSE_SEND_CONT);
    } catch (...) {
      data->HandleException(std::current_exception());
    }
  });
}

void CurlHandle::OnCancel::operator()() const {
  data->HandleException(std::make_exception_ptr(InterruptedException()));
}

template <typename Owner>
CurlHandle::CurlHandle(CURLM* http, event_base* event_loop, Request<> request,
                       stdx::stop_token stop_token, Owner* owner)
    : d_(std::make_unique<Data>(http, event_loop, std::move(request),
                                std::move(stop_token), owner)) {}

template <typename NewOwner>
CurlHandle::CurlHandle(CurlHandle handle, NewOwner* owner)
    : CurlHandle(std::move(handle)) {
  d_->owner = owner;
}

CurlHandle::~CurlHandle() = default;

CurlHttpBodyGenerator::CurlHttpBodyGenerator(
    CurlHttpBodyGenerator&& other) noexcept
    : HttpBodyGenerator(std::move(other)),
      chunk_ready_(MoveEvent(&other.chunk_ready_, this)),
      body_ready_(MoveEvent(&other.body_ready_, this)),
      body_ready_fired_(other.body_ready_fired_),
      status_(other.status_),
      exception_ptr_(other.exception_ptr_),
      data_(std::move(other.data_)),
      handle_(std::move(other.handle_), this) {}

CurlHttpBodyGenerator::CurlHttpBodyGenerator(CurlHandle handle,
                                             std::string initial_chunk)
    : handle_(std::move(handle), this) {
  Check(event_assign(&chunk_ready_, handle_.d_->event_loop, -1, 0, OnChunkReady,
                     this));
  Check(event_assign(&body_ready_, handle_.d_->event_loop, -1, 0, OnBodyReady,
                     this));
  ReceivedData(std::move(initial_chunk));
}

CurlHttpBodyGenerator& CurlHttpBodyGenerator::operator=(
    CurlHttpBodyGenerator&& other) noexcept {
  static_cast<HttpBodyGenerator&>(*this) = std::move(other);
  MoveAssignEvent(&chunk_ready_, &other.chunk_ready_, this);
  MoveAssignEvent(&body_ready_, &other.body_ready_, this);
  body_ready_fired_ = other.body_ready_fired_;
  status_ = other.status_;
  exception_ptr_ = other.exception_ptr_;
  data_ = std::move(other.data_);
  handle_ = CurlHandle(std::move(other.handle_), this);
  return *this;
}

void CurlHttpBodyGenerator::OnChunkReady(evutil_socket_t, short, void* handle) {
  auto curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  std::string data = std::move(curl_http_body_generator->data_);
  curl_http_body_generator->data_.clear();
  if (curl_http_body_generator->status_ != -1 &&
      !curl_http_body_generator->body_ready_fired_) {
    curl_http_body_generator->body_ready_fired_ = true;
    evuser_trigger(&curl_http_body_generator->body_ready_);
  }
  curl_http_body_generator->ReceivedData(std::move(data));
}

void CurlHttpBodyGenerator::OnBodyReady(evutil_socket_t, short, void* handle) {
  auto curl_http_body_generator =
      reinterpret_cast<CurlHttpBodyGenerator*>(handle);
  if (curl_http_body_generator->exception_ptr_) {
    curl_http_body_generator->Close(curl_http_body_generator->exception_ptr_);
  } else {
    curl_http_body_generator->Close(curl_http_body_generator->status_);
  }
}

CurlHttpBodyGenerator::~CurlHttpBodyGenerator() {
  if (chunk_ready_.ev_base) {
    Check(event_del(&chunk_ready_));
    Check(event_del(&body_ready_));
  }
}

void CurlHttpBodyGenerator::Resume() {
  if (status_ == -1 && !exception_ptr_) {
    curl_easy_pause(handle_.d_->handle.get(), CURLPAUSE_RECV_CONT);
  }
}

CurlHttpOperation::CurlHttpOperation(CURLM* http, event_base* event_loop,
                                     Request<> request,
                                     stdx::stop_token stop_token)
    : headers_ready_(),
      headers_ready_event_posted_(),
      handle_(http, event_loop, std::move(request), std::move(stop_token),
              this) {
  Check(event_assign(&headers_ready_, handle_.d_->event_loop, -1, 0,
                     OnHeadersReady, this));
}

CurlHttpOperation::CurlHttpOperation(CurlHttpOperation&& other) noexcept
    : awaiting_coroutine_(std::exchange(other.awaiting_coroutine_, nullptr)),
      headers_ready_(MoveEvent(&other.headers_ready_, this)),
      exception_ptr_(other.exception_ptr_),
      headers_ready_event_posted_(other.headers_ready_event_posted_),
      status_(other.status_),
      headers_(std::move(other.headers_)),
      body_(std::move(other.body_)),
      no_body_(other.no_body_),
      handle_(std::move(other.handle_), this) {}

CurlHttpOperation::~CurlHttpOperation() {
  if (headers_ready_.ev_base) {
    event_del(&headers_ready_);
  }
}

void CurlHttpOperation::OnHeadersReady(evutil_socket_t fd, short event,
                                       void* handle) {
  auto http_operation = reinterpret_cast<CurlHttpOperation*>(handle);
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

Response<CurlHttpBodyGenerator> CurlHttpOperation::await_resume() {
  if (exception_ptr_) {
    std::rethrow_exception(exception_ptr_);
  }
  CurlHttpBodyGenerator body_generator(std::move(handle_), std::move(body_));
  if (no_body_) {
    body_generator.Close(status_);
  }
  return {.status = status_,
          .headers = std::move(headers_),
          .body = std::move(body_generator)};
}

CurlHttpImpl::CurlHttpImpl(event_base* event_loop)
    : curl_handle_(curl_multi_init()),
      event_loop_(event_loop),
      timeout_event_() {
  event_assign(&timeout_event_, event_loop, -1, 0, TimeoutEvent,
               curl_handle_.get());
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETFUNCTION,
                          SocketCallback));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERFUNCTION,
                          TimerCallback));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERDATA, this));
}

CurlHttpImpl::CurlHttpImpl(CurlHttpImpl&& other) noexcept
    : curl_handle_(std::move(other.curl_handle_)),
      event_loop_(other.event_loop_),
      timeout_event_(MoveEvent(&other.timeout_event_, curl_handle_.get())) {
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERDATA, this));
}

CurlHttpImpl& CurlHttpImpl::operator=(CurlHttpImpl&& other) noexcept {
  curl_handle_ = std::move(other.curl_handle_);
  event_loop_ = other.event_loop_;
  timeout_event_ = MoveEvent(&other.timeout_event_, curl_handle_.get());
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_SOCKETDATA, this));
  Check(curl_multi_setopt(curl_handle_.get(), CURLMOPT_TIMERDATA, this));
  return *this;
}

CurlHttpImpl::~CurlHttpImpl() {
  if (timeout_event_.ev_base) {
    event_del(&timeout_event_);
  }
}

void CurlHttpImpl::TimeoutEvent(evutil_socket_t fd, short event, void* handle) {
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
      CurlHandle::Data* data;
      Check(curl_easy_getinfo(handle, CURLINFO_PRIVATE, &data));
      if (std::holds_alternative<CurlHttpOperation*>(data->owner)) {
        auto operation = std::get<CurlHttpOperation*>(data->owner);
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
            operation->handle_.d_->event_loop, -1, EV_TIMEOUT,
            [](evutil_socket_t fd, short event, void* handle) {
              stdx::coroutine_handle<void>::from_address(handle).resume();
            },
            std::exchange(operation->awaiting_coroutine_, nullptr).address(),
            nullptr));
      } else if (std::holds_alternative<CurlHttpBodyGenerator*>(data->owner)) {
        auto curl_http_body_generator =
            std::get<CurlHttpBodyGenerator*>(data->owner);
        curl_http_body_generator->status_ =
            static_cast<int>(message->data.result);
        if (message->data.result != CURLE_OK) {
          curl_http_body_generator->exception_ptr_ = std::make_exception_ptr(
              HttpException(message->data.result,
                            curl_easy_strerror(message->data.result)));
        }
        if (!evuser_pending(&curl_http_body_generator->chunk_ready_, nullptr)) {
          curl_http_body_generator->body_ready_fired_ = true;
          evuser_trigger(&curl_http_body_generator->body_ready_);
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
  auto http = reinterpret_cast<CurlHttpImpl*>(userp);
  if (what == CURL_POLL_REMOVE) {
    auto data = reinterpret_cast<SocketData*>(socketp);
    if (data) {
      Check(event_del(&data->socket_event));
      delete data;
    }
  } else {
    auto data = reinterpret_cast<SocketData*>(socketp);
    if (!data) {
      data = new SocketData;
      Check(curl_multi_assign(http->curl_handle_.get(), socket, data));
    } else {
      Check(event_del(&data->socket_event));
    }
    Check(event_assign(&data->socket_event, http->event_loop_, socket,
                       ((what & CURL_POLL_IN) ? EV_READ : 0) |
                           ((what & CURL_POLL_OUT) ? EV_WRITE : 0) | EV_PERSIST,
                       SocketEvent, http->curl_handle_.get()));
    Check(event_add(&data->socket_event, nullptr));
  }
  return 0;
}

int CurlHttpImpl::TimerCallback(CURLM*, long timeout_ms, void* userp) {
  auto http = reinterpret_cast<CurlHttpImpl*>(userp);
  if (timeout_ms == -1) {
    Check(event_del(&http->timeout_event_));
  } else {
    timeval tv = {.tv_sec = timeout_ms / 1000,
                  .tv_usec = timeout_ms % 1000 * 1000};
    Check(event_add(&http->timeout_event_, &tv));
  }
  return 0;
}

CurlHttpOperation CurlHttpImpl::Fetch(Request<> request,
                                      stdx::stop_token token) const {
  return CurlHttpOperation(curl_handle_.get(), event_loop_, std::move(request),
                           std::move(token));
}

}  // namespace coro::http
