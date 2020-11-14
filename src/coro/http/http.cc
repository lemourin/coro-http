#include "http.h"

namespace coro::http {

HttpException::HttpException(int status, std::string_view message)
    : status_(status), message_(message) {}

int HttpException::status() const noexcept { return status_; }

const char* HttpException::what() const noexcept { return message_.c_str(); }

}  // namespace coro::http