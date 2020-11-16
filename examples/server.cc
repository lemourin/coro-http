#include <coro/generator.h>
#include <coro/http/curl_http.h>
#include <coro/http/http_server.h>

#include <csignal>
#include <memory>

constexpr const char *kUrl =
    R"(https://r2---sn-oxup5-3ufs.googlevideo.com/videoplayback?expire=1605567894&ei=NrGyX4LjLtnByQXB-J3oDg&ip=87.205.168.166&id=o-AH3xEeRFz46rKjehFu9hzz7iWAOACnUcXwMskmgX2V86&itag=248&aitags=133%2C134%2C135%2C136%2C137%2C160%2C242%2C243%2C244%2C247%2C248%2C278%2C394%2C395%2C396%2C397%2C398%2C399&source=youtube&requiressl=yes&mh=kN&mm=31%2C29&mn=sn-oxup5-3ufs%2Csn-f5f7lne7&ms=au%2Crdu&mv=m&mvi=2&pl=19&initcwndbps=1065000&vprv=1&mime=video%2Fwebm&gir=yes&clen=1139885028&dur=3588.933&lmt=1575425961931478&mt=1605546183&fvip=2&keepalive=yes&c=WEB&txp=5535432&sparams=expire%2Cei%2Cip%2Cid%2Caitags%2Csource%2Crequiressl%2Cvprv%2Cmime%2Cgir%2Cclen%2Cdur%2Clmt&lsparams=mh%2Cmm%2Cmn%2Cms%2Cmv%2Cmvi%2Cpl%2Cinitcwndbps&lsig=AG3C_xAwRAIgA_5V1vBMK3wfTU-aBliPFKNjbbC2LdKvRTmRvhUQH4MCIE5NG5u5kXGVD7GywuBX03oc8Z6wKl7SWYfAbM0PTvLt&sig=AOq0QJ8wRgIhAI3lTHTFYcs-re8JbpvZax3ttWoAHtoLqt0O8Nk-cnxSAiEA1-oFnPFIri8QqnuvdbVurdPGB0-bvCyj_sih6me2KjU=&ratebypass=yes)";

template <typename T, typename Deleter>
auto MakePointer(T *ptr, Deleter &&deleter) {
  return std::unique_ptr<T, Deleter>(ptr, std::forward<Deleter>(deleter));
}

int main() {
#ifdef _WIN32
  WORD version_requested = MAKEWORD(2, 2);
  WSADATA wsa_data;

  (void)WSAStartup(version_requested, &wsa_data);
#endif

#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif

  auto base = MakePointer(event_base_new(), event_base_free);
  coro::http::CurlHttp http(base.get());
  coro::http::HttpServer http_server(
      base.get(),
      [&http](const coro::http::Request &request,
              coro::stdx::stop_token stop_token)
          -> coro::Task<coro::http::Response<
              std::unique_ptr<coro::http::CurlHttpBodyGenerator>>> {
        std::unordered_multimap<std::string, std::string> headers;
        auto range_it = request.headers.find("Range");
        if (range_it != std::end(request.headers)) {
          headers.emplace(*range_it);
        }
        auto pipe = co_await http.Fetch(
            coro::http::Request{.url = kUrl, .headers = std::move(headers)},
            stop_token);
        co_return coro::http::Response<
            std::unique_ptr<coro::http::CurlHttpBodyGenerator>>{
            .status = pipe.status,
            .headers = pipe.headers,
            .body = std::move(pipe.body)};
      });

  event_base_dispatch(base.get());
  return 0;
}