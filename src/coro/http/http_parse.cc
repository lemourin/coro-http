#include "coro/http/http_parse.h"

#include <event2/http.h>
#include <event2/keyvalq_struct.h>

#include <array>
#include <cmath>
#include <memory>
#include <regex>
#include <sstream>
#include <utility>

#include "coro/http/http.h"
#include "coro/util/raii_utils.h"

namespace coro::http {

namespace {

struct EvHttpUriDeleter {
  void operator()(evhttp_uri* uri) const { evhttp_uri_free(uri); }
};

const std::unordered_map<std::string, std::string> kMimeType = {  // NOLINT
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

}  // namespace

Uri ParseUri(std::string_view url_view) {
  std::unique_ptr<evhttp_uri, EvHttpUriDeleter> uri(evhttp_uri_parse_with_flags(
      std::string(url_view).c_str(), EVHTTP_URI_NONCONFORMANT));
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
  auto* it = keyvalq.tqh_first;
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
  auto scope_guard = util::AtScopeExit([&] {
    free(decoded);  // NOLINT
  });
  return decoded;
}

std::string EncodeUri(std::string_view uri) {
  char* encoded = evhttp_encode_uri(std::string(uri).c_str());
  if (!encoded) {
    throw HttpException(-1, "evhttp_encode_uri failed");
  }
  auto scope_guard = util::AtScopeExit([&] {
    free(encoded);  // NOLINT
  });
  return encoded;
}

std::string EncodeUriPath(std::string_view uri) {
  std::string result;
  size_t it = 0;
  while (it < uri.size()) {
    auto next = uri.find_first_of('/', it);
    result += EncodeUri(std::string_view(
        uri.data() + it, (next == std::string::npos ? uri.size() : next) - it));
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

Range ParseRange(std::string_view str) {
  std::regex regex(R"(bytes=(\d+)-(\d*))");
  std::match_results<std::string_view::const_iterator> match;
  if (std::regex_match(str.begin(), str.end(), match, regex)) {
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
    c = static_cast<char>(std::tolower(c));
  }
  return result;
}

std::string TrimWhitespace(std::string_view str) {
  int it1 = 0;
  while (it1 < static_cast<int>(str.size()) && std::isspace(str[it1])) {
    it1++;
  }
  int it2 = static_cast<int>(str.size()) - 1;
  while (it2 > it1 && std::isspace(str[it2])) {
    it2--;
  }
  return std::string(str.begin() + it1, str.begin() + it2 + 1);
}

std::string GetExtension(std::string_view filename) {
  auto index = filename.find_last_of('.');
  if (index == std::string_view::npos) {
    return "";
  } else {
    return std::string(filename.begin() + index + 1, filename.end());
  }
}

std::string GetMimeType(std::string_view extension) {
  auto it = kMimeType.find(ToLowerCase(std::string(extension)));
  if (it == std::end(kMimeType)) {
    return "application/octet-stream";
  } else {
    return it->second;
  }
}

std::string MimeTypeToExtension(std::string_view mime_type) {
  for (const auto& [key, value] : kMimeType) {
    if (value == mime_type) {
      return key;
    }
  }
  return "bin";
}

std::string ToBase64(std::string_view in) {
  const char* base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  int val = 0;
  int valb = -6;
  for (uint8_t c : in) {
    val = (val << 8) + c;
    valb += 8;
    while (valb >= 0) {
      out.push_back(base64_chars[(val >> valb) & 0x3F]);
      valb -= 6;
    }
  }
  if (valb > -6) {
    out.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
  }
  while (out.size() % 4) {
    out.push_back('=');
  }
  return out;
}

std::string FromBase64(std::string_view in) {
  const std::array<uint8_t, 80> lookup = {
      62,  255, 62,  255, 63,  52,  53, 54, 55, 56, 57, 58, 59, 60, 61, 255,
      255, 0,   255, 255, 255, 255, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
      10,  11,  12,  13,  14,  15,  16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
      255, 255, 255, 255, 63,  255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
      36,  37,  38,  39,  40,  41,  42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

  std::string out;
  int val = 0;
  int valb = -8;
  for (uint8_t c : in) {
    if (c < '+' || c > 'z') {
      break;
    }
    c -= '+';
    if (lookup[c] >= 64) {
      break;
    }
    val = (val << 6) + lookup[c];
    valb += 6;
    if (valb >= 0) {
      out.push_back(static_cast<char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return out;
}

std::tm gmtime(time_t time) {
  auto leap_year = [](int year) {
    return !(year % 4) && ((year % 100) || !(year % 400));
  };
  auto year_size = [&](int year) { return leap_year(year) ? 366 : 365; };

  const std::array<const std::array<int, 12>, 2> ytab{
      {{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
       {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}}};
  auto year = 1970;
  auto dayclock = time % (24 * 60 * 60);
  auto dayno = time / (24 * 60 * 60);
  std::tm tmbuf = {};

  tmbuf.tm_sec = static_cast<int>(dayclock % 60);
  tmbuf.tm_min = static_cast<int>((dayclock % 3600) / 60);
  tmbuf.tm_hour = static_cast<int>(dayclock / 3600);
  tmbuf.tm_wday = static_cast<int>((dayno + 4) % 7);
  while (dayno >= year_size(year)) {
    dayno -= year_size(year);
    year++;
  }
  tmbuf.tm_year = year - 1900;
  tmbuf.tm_yday = static_cast<int>(dayno);
  while (dayno >= ytab[leap_year(year)][tmbuf.tm_mon]) {
    dayno -= ytab[leap_year(year)][tmbuf.tm_mon];
    tmbuf.tm_mon++;
  }
  tmbuf.tm_mday = static_cast<int>(dayno + 1);
  return tmbuf;
}

time_t timegm(const std::tm& t) {
  const int month_count = 12;
  const std::array<int, 12> days = {0,   31,  59,  90,  120, 151,
                                    181, 212, 243, 273, 304, 334};
  int year = 1900 + t.tm_year + t.tm_mon / month_count;
  time_t result = (year - 1970) * 365 + days[t.tm_mon % month_count];

  result += (year - 1968) / 4;
  result -= (year - 1900) / 100;
  result += (year - 1600) / 400;
  if ((year % 4) == 0 && ((year % 100) != 0 || (year % 400) == 0) &&
      (t.tm_mon % month_count) < 2) {
    result--;
  }
  result += t.tm_mday - 1;
  result *= 24;
  result += t.tm_hour;
  result *= 60;
  result += t.tm_min;
  result *= 60;
  result += t.tm_sec;
  if (t.tm_isdst == 1) {
    result -= 3600;
  }
  return result;
}

int64_t ParseTime(std::string_view str) {
  const uint32_t SIZE = 6;
  std::array<char, SIZE + 1> buffer = {};
  float sec;
  std::tm time = {};
#ifdef _MSC_VER
  int cnt = sscanf_s(std::string(str).c_str(), "%d-%d-%dT%d:%d:%f%6s",
                     &time.tm_year, &time.tm_mon, &time.tm_mday, &time.tm_hour,
                     &time.tm_min, &sec, buffer, SIZE + 1);
#else
  // NOLINTNEXTLINE
  int cnt = sscanf(std::string(str).c_str(), "%d-%d-%dT%d:%d:%f%6s",
                   &time.tm_year, &time.tm_mon, &time.tm_mday, &time.tm_hour,
                   &time.tm_min, &sec, buffer.data());
#endif
  if (cnt == 7) {
    time.tm_year -= 1900;
    time.tm_mon--;
    time.tm_sec = static_cast<int>(std::lround(sec));
    if (std::string_view(buffer.begin()) != "Z") {
      int offset_hour;
      int offset_minute;
#ifdef _MSC_VER
      cnt = sscanf_s(buffer, "%d:%d", &offset_hour, &offset_minute);
#else
      // NOLINTNEXTLINE
      cnt = sscanf(buffer.data(), "%d:%d", &offset_hour, &offset_minute);
#endif
      if (cnt == 2) {
        time.tm_hour -= offset_hour;
        time.tm_min -= offset_minute;
      }
    }
    return timegm(time);
  }
  throw std::invalid_argument("can't parse time");
}

Method ToMethod(std::string_view method) {
  if (method == "GET") {
    return Method::kGet;
  } else if (method == "POST") {
    return Method::kPost;
  } else if (method == "PUT") {
    return Method::kPut;
  } else if (method == "OPTIONS") {
    return Method::kOptions;
  } else if (method == "HEAD") {
    return Method::kHead;
  } else if (method == "PATCH") {
    return Method::kPatch;
  } else if (method == "DELETE") {
    return Method::kDelete;
  } else if (method == "PROPFIND") {
    return Method::kPropfind;
  } else if (method == "PROPPATCH") {
    return Method::kProppatch;
  } else if (method == "MKCOL") {
    return Method::kMkcol;
  } else if (method == "MOVE") {
    return Method::kMove;
  } else {
    throw HttpException(HttpException::kUnknown, "unknown http method");
  }
}

std::string_view ToStatusString(int http_code) {
  switch (http_code) {
    case 100:
      return "Continue";
    case 101:
      return "Switching Protocol";
    case 102:
      return "Processing";
    case 103:
      return "Early Hints";
    case 200:
      return "OK";
    case 201:
      return "Created";
    case 202:
      return "Accepted";
    case 203:
      return "Non-Authoritative Information";
    case 204:
      return "No Content";
    case 205:
      return "Reset Content";
    case 206:
      return "Partial Content";
    case 207:
      return "Multi-Status";
    case 208:
      return "Already Reported";
    case 300:
      return "Multiple Choice";
    case 302:
      return "Found";
    case 303:
      return "See Other";
    case 304:
      return "Not Modified";
    case 307:
      return "Temporary Redirect";
    case 308:
      return "Permanent Redirect";
    case 400:
      return "Bad Request";
    case 401:
      return "Unauthorized";
    case 403:
      return "Forbidden";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 406:
      return "Not Acceptable";
    case 408:
      return "Request Timeout";
    case 409:
      return "Conflict";
    case 410:
      return "Gone";
    case 411:
      return "Length Required";
    case 412:
      return "Precondition Failed";
    case 413:
      return "Payload Too Large";
    case 414:
      return "URI Too Long";
    case 415:
      return "Unsupported Media Type";
    case 416:
      return "Range Not Satisfiable";
    case 417:
      return "Expectation Failed";
    case 418:
      return "I'm a teapot";
    case 421:
      return "Misdirected Request";
    case 422:
      return "Unprocessable Entity";
    case 423:
      return "Locked";
    case 424:
      return "Failed Dependency";
    case 425:
      return "Too Early";
    case 426:
      return "Update Required";
    case 428:
      return "Precondition Required";
    case 429:
      return "Too Many Requests";
    case 431:
      return "Request Header Fields Too Large";
    case 451:
      return "Unavailable For Legal Reasons";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    case 502:
      return "Bad Gateway";
    case 503:
      return "Service Unavailable";
    case 504:
      return "Gateway Timeout";
    case 505:
      return "HTTP Version Not Supported";
    case 506:
      return "Variant also Negotiates";
    case 507:
      return "Insufficient Storage";
    case 508:
      return "Loop Detected";
    case 510:
      return "Not Extended";
    case 511:
      return "Network Authentication Required";
    default:
      throw HttpException(http_code, "unknown http code");
  }
}

std::pair<std::string, std::string> ToRangeHeader(const Range& range) {
  std::stringstream range_header;
  range_header << "bytes=" << range.start << "-";
  if (range.end) {
    range_header << *range.end;
  }
  return {"Range", std::move(range_header).str()};
}

}  // namespace coro::http
