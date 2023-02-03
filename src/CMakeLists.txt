include(embed_resource)
include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

add_library(coro-http)

target_sources(coro-http PRIVATE
    coro/mutex.cc
    coro/util/event_loop.cc
    coro/util/thread_pool.cc
    coro/stdx/stop_source.cc
    coro/stdx/stop_token.cc
    coro/stdx/source_location.cc
    coro/stdx/stacktrace.cc
    coro/http/http.cc
    coro/http/http_server_context.cc
    coro/http/curl_http.cc
    coro/http/http_parse.cc
    coro/http/cache_http.cc
    coro/http/http_exception.cc
)

target_sources(coro-http
    INTERFACE FILE_SET public_headers TYPE HEADERS FILES
        coro/task.h
        coro/generator.h
        coro/promise.h
        coro/shared_promise.h
        coro/interrupted_exception.h
        coro/when_all.h
        coro/mutex.h
        coro/exception.h
        coro/util/event_loop.h
        coro/util/thread_pool.h
        coro/util/raii_utils.h
        coro/util/stop_token_or.h
        coro/util/regex.h
        coro/util/function_traits.h
        coro/util/type_list.h
        coro/util/lru_cache.h
        coro/http/http_body_generator.h
        coro/http/http_parse.h
        coro/http/curl_http.h
        coro/http/http_server.h
        coro/http/http_exception.h
        coro/http/http.h
        coro/http/cache_http.h
        coro/http/http_server_context.h
        coro/stdx/coroutine.h
        coro/stdx/stop_callback.h
        coro/stdx/concepts.h
        coro/stdx/stacktrace.h
        coro/stdx/stop_token.h
        coro/stdx/any_invocable.h
        coro/stdx/source_location.h
        coro/stdx/stop_source.h
)

find_package(Threads REQUIRED)

target_link_libraries(coro-http PRIVATE libevent::core libevent::extra Threads::Threads CURL::libcurl Boost::regex)

if (NOT WIN32)
    target_link_libraries(coro-http PRIVATE libevent::pthreads)
endif()

target_include_directories(coro-http PRIVATE . ${CMAKE_CURRENT_BINARY_DIR})
target_compile_features(coro-http PUBLIC cxx_std_20)

if(TARGET boost_stacktrace)
    target_link_libraries(coro-http PRIVATE $<$<CONFIG:Debug>:boost_stacktrace>)
    target_compile_definitions(coro-http PRIVATE $<$<CONFIG:Debug>:HAVE_BOOST_STACKTRACE>)
endif()

if((CMAKE_SYSTEM_NAME STREQUAL "WindowsStore") OR ANDROID)
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

check_cxx_source_compiles(
    "
    #include <source_location>
    using std::source_location;
    int main() {}
    "
    SOURCE_LOCATION_SUPPORTED
)
if(SOURCE_LOCATION_SUPPORTED)
    target_compile_definitions(coro-http PUBLIC SOURCE_LOCATION_SUPPORTED)
else()
    check_c_source_compiles(
        "int main() { return __builtin_FILE() == 0; }"
        HAVE_BUILTIN_FILE
    )
    check_c_source_compiles(
        "int main() { return __builtin_FUNCTION() == 0; }"
        HAVE_BUILTIN_FUNCTION
    )
    check_c_source_compiles(
        "int main() { return __builtin_LINE(); }"
        HAVE_BUILTIN_LINE
    )
    check_c_source_compiles(
        "int main() { return __builtin_COLUMN(); }"
        HAVE_BUILTIN_COLUMN
    )
    if(HAVE_BUILTIN_FILE)
        target_compile_definitions(coro-http PUBLIC HAVE_BUILTIN_FILE)
    endif()
    if(HAVE_BUILTIN_FUNCTION)
        target_compile_definitions(coro-http PUBLIC HAVE_BUILTIN_FUNCTION)
    endif()
    if(HAVE_BUILTIN_LINE)
        target_compile_definitions(coro-http PUBLIC HAVE_BUILTIN_LINE)
    endif()
    if(HAVE_BUILTIN_COLUMN)
        target_compile_definitions(coro-http PUBLIC HAVE_BUILTIN_COLUMN)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS coro-http EXPORT coro-http FILE_SET public_headers)
install(EXPORT coro-http NAMESPACE coro:: DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME})
configure_package_config_file(
        ../cmake/coro-http-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/coro-http-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}
)
install(
    FILES
        ../cmake/embed_resource.cmake
        ../cmake/embed_resource_header.cmake
        ../cmake/embed_resource_impl.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/cmake/coro-http-config.cmake
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/cmake/${PROJECT_NAME}
)
