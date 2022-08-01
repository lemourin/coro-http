#ifndef CORO_STDX_SOURCE_LOCATION
#define CORO_STDX_SOURCE_LOCATION

#include <string>

#ifdef SOURCE_LOCATION_SUPPORTED

#include <source_location>

namespace coro::stdx {

using ::std::source_location;

}

#else

#include <cstdint>

namespace coro::stdx {

class source_location {
 public:
#if not defined(__apple_build_version__) and defined(__clang__) and \
    (__clang_major__ >= 9)
  static constexpr source_location current(
      const char* file_name = __builtin_FILE(),
      const char* function_name = __builtin_FUNCTION(),
      const uint_least32_t line_number = __builtin_LINE(),
      const uint_least32_t column_offset = __builtin_COLUMN()) noexcept
#elif defined(__GNUC__) and \
    (__GNUC__ > 4 or (__GNUC__ == 4 and __GNUC_MINOR__ >= 8))
  static constexpr source_location current(
      const char* file_name = __builtin_FILE(),
      const char* function_name = __builtin_FUNCTION(),
      const uint_least32_t line_number = __builtin_LINE(),
      const uint_least32_t column_offset = 0) noexcept
#else
  static constexpr source_location current(
      const char* file_name = "unsupported",
      const char* function_name = "unsupported",
      const uint_least32_t line_number = 0,
      const uint_least32_t column_offset = 0) noexcept
#endif
  {
    return source_location(file_name, function_name, line_number,
                           column_offset);
  }

  source_location(const source_location&) = default;
  source_location(source_location&&) = default;

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