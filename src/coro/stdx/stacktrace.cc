#include "coro/stdx/stacktrace.h"

#include <sstream>

#ifdef HAVE_BOOST_STACKTRACE
#include <boost/stacktrace.hpp>
#endif

namespace coro {

namespace stdx {

struct stacktrace::Impl {
#ifdef HAVE_BOOST_STACKTRACE
  boost::stacktrace::stacktrace stacktrace;
#endif
};

stacktrace::stacktrace(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

stacktrace::stacktrace(const stacktrace& other)
    : impl_(std::make_unique<Impl>(*other.impl_)) {}

stacktrace::stacktrace(stacktrace&& other) noexcept
    : impl_(std::move(other.impl_)) {}

stacktrace::~stacktrace() = default;

stacktrace& stacktrace::operator=(const stacktrace& other) {
  impl_ = std::make_unique<Impl>(*other.impl_);
  return *this;
}

stacktrace& stacktrace::operator=(stacktrace&& other) noexcept {
  impl_ = std::move(other.impl_);
  return *this;
}

bool stacktrace::empty() const noexcept {
#ifdef HAVE_BOOST_STACKTRACE
  return impl_->stacktrace.empty();
#else
  return true;
#endif
}

stacktrace stacktrace::current() noexcept {
#ifdef HAVE_BOOST_STACKTRACE
  return stacktrace(std::make_unique<Impl>(
      Impl{.stacktrace = boost::stacktrace::stacktrace()}));
#else
  return stacktrace(std::make_unique<Impl>());
#endif
}

}  // namespace stdx

std::string GetHtmlStacktrace(const stdx::stacktrace& d) {
#ifdef HAVE_BOOST_STACKTRACE
  std::stringstream stream;
  boost::stacktrace::stacktrace p;
  std::string stacktrace = ToString(d);
  for (int i = 0; i < stacktrace.size();) {
    if (i + 1 < stacktrace.size() && stacktrace.substr(i, 2) == "\r\n") {
      i += 2;
      stream << "<br>";
    } else if (stacktrace[i] == '\n') {
      i++;
      stream << "<br>";
    } else {
      i++;
      stream << stacktrace[i];
    }
  }
  return std::move(stream).str();
#else
  return "";
#endif
}

std::string ToString(const stdx::stacktrace& d) {
#ifdef HAVE_BOOST_STACKTRACE
  return boost::stacktrace::to_string(GetImpl(&d)->stacktrace);
#else
  return "";
#endif
}

}  // namespace coro