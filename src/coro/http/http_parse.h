#ifndef CORO_CLOUDSTORAGE_HTTP_PARSE_H
#define CORO_CLOUDSTORAGE_HTTP_PARSE_H

#include <event2/http.h>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace coro::http {

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
std::string EncodeUri(std::string_view uri);

template <typename Map = std::unordered_map<std::string, std::string>>
std::string FormDataToString(const Map& map) {
  std::string result;
  bool first = false;
  for (const auto& [key, value] : map) {
    if (first) {
      result += "&";
    }
    result += EncodeUri(key) + "=" + EncodeUri(value);
    first = true;
  }
  return result;
}

}  // namespace coro::http

#endif  // CORO_CLOUDSTORAGE_HTTP_PARSE_H
