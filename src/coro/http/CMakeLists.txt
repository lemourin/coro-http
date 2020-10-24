add_library(coro-http)

set(coro-http-headers
    http.h
    curl_http.h
)

set(coro-http-sources
    http.cc
    curl_http.cc
)

target_sources(coro-http PRIVATE
    ${coro-http-headers}
    ${coro-http-sources}
)

target_include_directories(coro-http PUBLIC ../..)

target_link_libraries(coro-http PUBLIC libcurl event_core coro)
target_compile_features(coro-http PUBLIC cxx_std_20)