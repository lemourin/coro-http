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

}  // namespace

Exception::Exception(stdx::source_location location)
    : stacktrace_(GetStackTrace()), source_location_(std::move(location)) {}

std::string GetHtmlStacktrace(std::string_view stacktrace) {
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

}  // namespace coro