add_library(stdx)

target_sources(stdx PRIVATE
    stop_callback.h
    stop_source.h
    stop_token.h
    coroutine.h
    concepts.h
    any_invocable.h
    stop_source.cc
    stop_token.cc
)
target_compile_features(stdx PUBLIC cxx_std_20)
target_include_directories(stdx PUBLIC ../../)

include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

if(MSVC)
    set(COROUTINE_FLAGS "/std:c++latest")
    set(EXPERIMENTAL_COROUTINE_FLAGS "/await")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
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
    target_compile_options(stdx PUBLIC ${COROUTINE_FLAGS})
    target_compile_definitions(stdx PUBLIC COROUTINE_SUPPORTED)
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
        target_compile_options(stdx PUBLIC ${EXPERIMENTAL_COROUTINE_FLAGS})
        target_compile_definitions(stdx PUBLIC EXPERIMENTAL_COROUTINE_SUPPORTED)
    else()
        message(FATAL_ERROR "Coroutines not supported.")
    endif()
endif()
