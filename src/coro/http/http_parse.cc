#include "http_parse.h"

#include <coro/http/http.h>
#include <coro/util/make_pointer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <regex>

namespace coro::http {

namespace {

std::optional<std::string> ToOptional(const char* str) {
  if (str) {
    return str;
  } else {
    return std::nullopt;
  }
}

std::optional<int> ToOptional(int value) {
  if (value != -1) {
    return value;
  } else {
    return std::nullopt;
  }
}

}  // namespace

Uri ParseUri(std::string_view url_view) {
  auto uri = util::MakePointer(evhttp_uri_parse(std::string(url_view).c_str()),
                               evhttp_uri_free);
  return Uri{.scheme = ToOptional(evhttp_uri_get_scheme(uri.get())),
             .userinfo = ToOptional(evhttp_uri_get_userinfo(uri.get())),
             .host = ToOptional(evhttp_uri_get_host(uri.get())),
             .port = ToOptional(evhttp_uri_get_port(uri.get())),
             .path = ToOptional(evhttp_uri_get_path(uri.get())),
             .query = ToOptional(evhttp_uri_get_query(uri.get()))};
}

std::unordered_map<std::string, std::string> ParseQuery(
    std::string_view query) {
  evkeyvalq keyvalq;
  if (evhttp_parse_query_str(std::string(query).c_str(), &keyvalq) != 0) {
    throw HttpException(-1, "evhttp_parse_query_str failed");
  }
  std::unordered_map<std::string, std::string> result;
  auto it = keyvalq.tqh_first;
  while (it != nullptr) {
    result.emplace(it->key, it->value);
    it = it->next.tqe_next;
  }
  evhttp_clear_headers(&keyvalq);
  return result;
}

std::string EncodeUri(std::string_view uri) {
  char* encoded = evhttp_encode_uri(std::string(uri).c_str());
  if (!encoded) {
    throw HttpException(-1, "evhttp_encode_uri failed");
  }
  std::string ret_str = encoded;
  free(encoded);
  return ret_str;
}

std::string FormDataToString(
    const std::initializer_list<std::pair<std::string_view, std::string_view>>&
        params) {
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

Range ParseRange(std::string str) {
  std::regex regex(R"(bytes=(\d+)-(\d*))");
  std::smatch match;
  if (std::regex_match(str, match, regex)) {
    return Range{.start = std::stoll(match[1].str()),
                 .end = match[2].str().empty()
                            ? std::nullopt
                            : std::make_optional(std::stoll(match[2].str()))};
  } else {
    return Range{};
  }
}

}  // namespace coro::http