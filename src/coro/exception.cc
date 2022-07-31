#include "coro/exception.h"

#ifdef HAVE_BOOST_STACKTRACE
#include <boost/stacktrace.hpp>
#endif

#include <sstream>

namespace coro {

namespace {
std::string GetStackTrace() {
#ifdef HAVE_BOOST_STACKTRACE
  return boost::stacktrace::to_string(
      boost::stacktrace::stacktrace(/*skip=*/0, /*max_depth=*/128));
#else
  return "";
#endif
}

std::string GetFormattedStacktrace(std::string_view stacktrace) {
  std::stringstream stream;
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
}

}  // namespace

Exception::Exception() : stacktrace_(GetStackTrace()) {}

std::string Exception::html_stacktrace() const {
  return GetFormattedStacktrace(stacktrace_);
}

}  // namespace coro