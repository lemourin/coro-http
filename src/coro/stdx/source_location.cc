#include "coro/stdx/source_location.h"

#include <sstream>

namespace coro {

std::string ToString(const stdx::source_location& location) {
  std::stringstream stream;
  stream << location.file_name() << '(' << location.line();
  if (location.column() > 0) {
    stream << ':' << location.column();
  }
  stream << "): " << location.function_name();
  return std::move(stream).str();
}

}  // namespace coro