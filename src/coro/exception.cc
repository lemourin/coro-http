#include "coro/exception.h"

#ifdef HAVE_BOOST_STACKTRACE
#include <boost/stacktrace.hpp>
#endif

namespace coro {

namespace {
std::string GetStackTrace() {
#ifdef HAVE_BOOST_STACKTRACE
  return boost::stacktrace::to_string(
      boost::stacktrace::stacktrace(/*skip=*/5, /*max_depth=*/128));
#else
  return "";
#endif
}
}  // namespace

Exception::Exception() : stacktrace_(GetStackTrace()) {}

}  // namespace coro