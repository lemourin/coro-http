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
target_include_directories(coro-http PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/../../)

include(embed_resource)

if(USE_OPENSSL)
    embed_resource(
        TARGET coro-http
        NAMESPACE coro::http
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/assets
        INPUT ../../../assets/cacert.pem
    )
    target_compile_definitions(coro-http PRIVATE USE_BUNDLED_CACERT)
endif()