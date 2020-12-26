#include "http_parse.h"

#include <coro/http/http.h>
#include <coro/util/make_pointer.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <cmath>
#include <regex>

namespace coro::http {

namespace {

const std::unordered_map<std::string, std::string> kMimeType = {
    {"aac", "audio/aac"},           {"avi", "video/x-msvideo"},
    {"gif", "image/gif"},           {"jpeg", "image/jpeg"},
    {"jpg", "image/jpeg"},          {"mpeg", "video/mpeg"},
    {"oga", "audio/ogg"},           {"ogv", "video/ogg"},
    {"png", "image/png"},           {"svg", "image/svg+xml"},
    {"tif", "image/tiff"},          {"tiff", "image/tiff"},
    {"wav", "audio-x/wav"},         {"weba", "audio/webm"},
    {"webm", "video/webm"},         {"webp", "image/webp"},
    {"3gp", "video/3gpp"},          {"3g2", "video/3gpp2"},
    {"mp4", "video/mp4"},           {"mkv", "video/webm"},
    {"mp3", "audio/mpeg"},          {"flac", "audio/flac"},
    {"txt", "text/plain"},          {"pdf", "application/pdf"},
    {"mpd", "application/dash+xml"}};

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

time_t timegm(const std::tm& t) {
  const int month_count = 12;
  const int days[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  int year = 1900 + t.tm_year + t.tm_mon / month_count;
  time_t result = (year - 1970) * 365 + days[t.tm_mon % month_count];

  result += (year - 1968) / 4;
  result -= (year - 1900) / 100;
  result += (year - 1600) / 400;
  if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
      (t.tm_mon % month_count) < 2)
    result--;
  result += t.tm_mday - 1;
  result *= 24;
  result += t.tm_hour;
  result *= 60;
  result += t.tm_min;
  result *= 60;
  result += t.tm_sec;
  if (t.tm_isdst == 1) result -= 3600;
  return result;
}

}  // namespace

Uri ParseUri(std::string_view url_view) {
  auto uri = util::MakePointer(
      evhttp_uri_parse_with_flags(std::string(url_view).c_str(),
                                  EVHTTP_URI_NONCONFORMANT),
      evhttp_uri_free);
  if (!uri) {
    throw HttpException(-1, "evhttp_uri_parse failed");
  }
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

std::string DecodeUri(std::string_view uri) {
  char* decoded = evhttp_uridecode(std::string(uri).c_str(), 1, nullptr);
  if (!decoded) {
    throw HttpException(-1, "evhttp_decode_uri failed");
  }
  std::string ret_str = decoded;
  free(decoded);
  return ret_str;
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

std::string EncodeUriPath(std::string_view uri) {
  std::string result;
  size_t it = 0;
  while (it < uri.size()) {
    auto next = uri.find_first_of('/', it);
    result += EncodeUri(std::string_view(
        uri.begin() + it,
        uri.begin() + (next == std::string_view::npos ? uri.size() : next)));
    if (next == std::string_view::npos) {
      break;
    } else {
      result += '/';
      it = next + 1;
    }
  }
  return result;
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

std::string ToLowerCase(std::string result) {
  for (char& c : result) {
    c = std::tolower(c);
  }
  return result;
}

std::string GetExtension(std::string_view filename) {
  auto index = filename.find_last_of(".");
  if (index == std::string_view::npos) {
    return "";
  } else {
    return std::string(filename.begin() + index + 1, filename.end());
  }
}

std::string GetMimeType(std::string_view extension) {
  auto it = kMimeType.find(ToLowerCase(std::string(extension)));
  if (it == std::end(kMimeType))
    return "application/octet-stream";
  else
    return it->second;
}

std::string ToBase64(std::string_view in) {
  const char* base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0, valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  while (out.size() % 4) out.push_back('=');
  return out;
}

std::string FromBase64(std::string_view in) {
  const uint8_t lookup[] = {
      62,  255, 62,  255, 63,  52,  53, 54, 55, 56, 57, 58, 59, 60, 61, 255,
      255, 0,   255, 255, 255, 255, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
      10,  11,  12,  13,  14,  15,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
      255, 255, 255, 255, 63,  255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
      36,  37,  38,  39,  40,  41,  42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

  std::string out;
  int val = 0, valb = -8;
  for (uint8_t c : in) {
    if (c < '+' || c > 'z') break;
    c -= '+';
    if (lookup[c] >= 64) break;
    val = (val << 6) + lookup[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(char((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

int64_t ParseTime(std::string_view str) {
  const uint32_t SIZE = 6;
  char buffer[SIZE + 1] = {};
  float sec;
  std::tm time = {};
  if (sscanf(std::string(str).c_str(), "%d-%d-%dT%d:%d:%f%6s", &time.tm_year,
             &time.tm_mon, &time.tm_mday, &time.tm_hour, &time.tm_min, &sec,
             buffer) == 7) {
    time.tm_year -= 1900;
    time.tm_mon--;
    time.tm_sec = std::lround(sec);
    if (buffer != std::string("Z")) {
      int offset_hour, offset_minute;
      if (sscanf(buffer, "%d:%d", &offset_hour, &offset_minute) == 2) {
        time.tm_hour -= offset_hour;
        time.tm_min -= offset_minute;
      }
    }
    return timegm(time);
  }
  throw std::invalid_argument("can't parse time");
}

}  // namespace coro::http