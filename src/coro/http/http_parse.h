#ifndef CORO_CLOUDSTORAGE_HTTP_PARSE_H
#define CORO_CLOUDSTORAGE_HTTP_PARSE_H

#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace coro::http {

enum class Method;

struct Range {
  int64_t start;
  std::optional<int64_t> end;
};

struct Uri {
  std::optional<std::string> scheme;
  std::optional<std::string> userinfo;
  std::optional<std::string> host;
  std::optional<int> port;
  std::optional<std::string> path;
  std::optional<std::string> fragment;
  std::optional<std::string> query;
};

Uri ParseUri(std::string_view uri);
std::unordered_map<std::string, std::string> ParseQuery(std::string_view query);
std::string DecodeUri(std::string_view uri);
std::string EncodeUri(std::string_view uri);
std::string EncodeUriPath(std::string_view uri);

time_t timegm(const std::tm&);
std::tm gmtime(time_t time);

template <typename List = std::initializer_list<
              std::pair<std::string_view, std::string_view>>>
std::string FormDataToString(const List& params) {
  std::string result;
  bool first = false;
  for (const auto& [key, value] : params) {
    if (first) {
      result += "&";
    }
    result += EncodeUri(key) + "=" + EncodeUri(value);
    first = true;
  }
  return result;
}

Range ParseRange(std::string);
std::string ToLowerCase(std::string);
std::string GetExtension(std::string_view filename);
std::string GetMimeType(std::string_view extension);
std::string MimeTypeToExtension(std::string_view mime_type);
std::string ToBase64(std::string_view);
std::string FromBase64(std::string_view);
int64_t ParseTime(std::string_view);
std::string_view ToStatusString(int http_code);
Method ToMethod(std::string_view method);
std::pair<std::string, std::string> ToRangeHeader(const Range&);

template <typename Collection>
std::optional<std::string> GetHeader(const Collection& collection,
                                     std::string requested_header) {
  for (const auto& [header, value] : collection) {
    if (ToLowerCase(header) == ToLowerCase(requested_header)) {
      return value;
    }
  }
  return std::nullopt;
}

template <typename Container>
bool HasHeader(const Container& container, std::string_view key,
               std::string_view value) {
  for (const auto& [ckey, cvalue] : container) {
    if (ToLowerCase(ckey) == ToLowerCase(std::string(key))) {
      if (cvalue.find(value) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace coro::http

#endif  // CORO_CLOUDSTORAGE_HTTP_PARSE_H
