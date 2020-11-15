add_library(coro-http)

set(coro-http-headers
    http.h
    curl_http.h
    http_server.h
)

set(coro-http-sources
    curl_http.cc
)

target_sources(coro-http PRIVATE
    ${coro-http-headers}
    ${coro-http-sources}
)

target_link_libraries(coro-http PUBLIC libcurl event_core event_extra coro)