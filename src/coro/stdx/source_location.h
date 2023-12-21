#ifndef CORO_STDX_SOURCE_LOCATION
#define CORO_STDX_SOURCE_LOCATION

#include <string>

#ifdef CORO_HTTP_SOURCE_LOCATION_SUPPORTED

#include <source_location>

namespace coro::stdx {

using ::std::source_location;

}  // namespace coro::stdx

#else

#include <cstdint>

namespace coro::stdx {

class source_location {
 public:
  static constexpr source_location current(const char* file_name =
#ifdef CORO_HTTP_HAVE_BUILTIN_FILE
                                               __builtin_FILE()
#else
                                               "unknown"
#endif
                                               ,
                                           const char* function_name =
#ifdef CORO_HTTP_HAVE_BUILTIN_FUNCTION
                                               __builtin_FUNCTION()
#else
                                               "unknown"
#endif
                                               ,
                                           uint_least32_t line_number =
#ifdef CORO_HTTP_HAVE_BUILTIN_LINE
                                               __builtin_LINE()
#else
                                               0
#endif
                                               ,
                                           uint_least32_t column_offset =
#ifdef CORO_HTTP_HAVE_BUILTIN_COLUMN
                                               __builtin_COLUMN()
#else
                                               0
#endif
                                               ) noexcept {
    return source_location(file_name, function_name, line_number,
                           column_offset);
  }

  source_location(const source_location&) = default;
  source_location(source_location&&) = default;

  source_location& operator=(const source_location&) = default;
  source_location& operator=(source_location&&) = default;

  constexpr const char* file_name() const noexcept { return file_name_; }

  constexpr const char* function_name() const noexcept {
    return function_name_;
  }

  constexpr uint_least32_t line() const noexcept { return line_number_; }

  constexpr uint_least32_t column() const noexcept { return column_offset_; }

 private:
  constexpr source_location(const char* file_name, const char* function_name,
                            uint_least32_t line_number,
                            uint_least32_t column_offset) noexcept
      : file_name_(file_name),
        function_name_(function_name),
        line_number_(line_number),
        column_offset_(column_offset) {}

  const char* file_name_;
  const char* function_name_;
  uint_least32_t line_number_;
  uint_least32_t column_offset_;
};

}  // namespace coro::stdx
#endif

namespace coro {

std::string ToString(const stdx::source_location& location);

}  // namespace coro

#endif  // CORO_STDX_SOURCE_LOCATION