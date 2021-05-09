add_library(coro-http)

target_sources(coro-http PRIVATE
    http.h
    curl_http.h
    http_server.h
    http_server.cc
    http_parse.h
    curl_http.cc
    http_parse.cc
    cache_http.h
)

target_link_libraries(coro-http PUBLIC libcurl event_core event_extra coro)