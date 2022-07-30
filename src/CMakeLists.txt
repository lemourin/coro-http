include(embed_resource)
include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

add_library(coro-http)

target_sources(coro-http PRIVATE
    coro/task.h
    coro/generator.h
    coro/promise.h
    coro/shared_promise.h
    coro/interrupted_exception.h
    coro/when_all.h
    coro/mutex.h
    coro/mutex.cc
    coro/util/function_traits.h
    coro/util/type_list.h
    coro/util/raii_utils.h
    coro/util/lru_cache.h
    coro/util/event_loop.h
    coro/util/event_loop.cc
    coro/util/stop_token_or.h
    coro/util/thread_pool.h
    coro/util/thread_pool.cc
    coro/util/regex.h
    coro/stdx/stop_callback.h
    coro/stdx/stop_source.h
    coro/stdx/stop_token.h
    coro/stdx/coroutine.h
    coro/stdx/concepts.h
    coro/stdx/stop_source.cc
    coro/stdx/stop_token.cc
    coro/stdx/any_invocable.h
    coro/http/http.h
    coro/http/http.cc
    coro/http/curl_http.h
    coro/http/http_server.h
    coro/http/http_server_context.h
    coro/http/http_server_context.cc
    coro/http/http_parse.h
    coro/http/curl_http.cc
    coro/http/http_parse.cc
    coro/http/cache_http.h
    coro/http/cache_http.cc
    coro/http/http_body_generator.h
    coro/http/http_exception.h
    coro/http/http_exception.cc
    coro/exception.h
    coro/exception.cc)

find_package(Threads REQUIRED)

target_link_libraries(coro-http PRIVATE libevent::core libevent::extra Threads::Threads CURL::libcurl Boost::regex)

if (NOT WIN32)
    target_link_libraries(coro-http PRIVATE libevent::pthreads)
endif()

target_include_directories(coro-http PUBLIC .)
target_include_directories(coro-http PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(coro-http PUBLIC cxx_std_20)

if(TARGET boost_stacktrace)
    target_link_libraries(coro-http PRIVATE $<$<CONFIG:Debug>:boost_stacktrace>)
    target_compile_definitions(coro-http PRIVATE $<$<CONFIG:Debug>:HAVE_BOOST_STACKTRACE>)
endif()

if(USE_OPENSSL OR ANDROID)
    embed_resource(
        TARGET coro-http
        NAMESPACE coro::http
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/coro/http/assets
        INPUT ../assets/cacert.pem
    )
    target_compile_definitions(coro-http PRIVATE USE_BUNDLED_CACERT)
endif()

if(MSVC)
    set(COROUTINE_FLAGS "/std:c++20")
    set(EXPERIMENTAL_COROUTINE_FLAGS "/await")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(COROUTINE_FLAGS "-std=c++20")
    set(EXPERIMENTAL_COROUTINE_FLAGS "-fcoroutines-ts")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(COROUTINE_FLAGS "-fcoroutines")
endif()

set(CMAKE_REQUIRED_FLAGS "${COROUTINE_FLAGS}")
check_cxx_source_compiles(
    "
    #include <version>
    #if defined(__clang__) && !defined(__cpp_impl_coroutine)
    #define __cpp_impl_coroutine 1
    #endif
    #if defined(_MSC_VER) && !defined(__cpp_lib_coroutine)
    #define __cpp_lib_coroutine 1
    #endif
    #include <coroutine>
    using std::coroutine_handle;
    int main() {}
    "
    COROUTINE_SUPPORTED
)

if(COROUTINE_SUPPORTED)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(coro-http PUBLIC ${COROUTINE_FLAGS})
    endif()
    target_compile_definitions(coro-http PUBLIC COROUTINE_SUPPORTED)
else()
    set(CMAKE_REQUIRED_FLAGS "${EXPERIMENTAL_COROUTINE_FLAGS}")
    check_cxx_source_compiles(
        "
        #include <experimental/coroutine>
        using std::experimental::coroutine_handle;
        int main() {}
        "
        EXPERIMENTAL_COROUTINE_SUPPORTED
    )
    if(EXPERIMENTAL_COROUTINE_SUPPORTED)
        target_compile_options(coro-http PUBLIC ${EXPERIMENTAL_COROUTINE_FLAGS})
        target_compile_definitions(coro-http PUBLIC EXPERIMENTAL_COROUTINE_SUPPORTED)
    else()
        message(FATAL_ERROR "Coroutines not supported.")
    endif()
endif()
