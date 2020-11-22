add_library(stdx)

set(stdx-headers
    stop_callback.h
    stop_source.h
    stop_token.h
    coroutine.h
)

set(stdx-sources
    stop_source.cc
    stop_token.cc
)

target_sources(stdx PRIVATE ${stdx-headers} ${stdx-sources})
target_compile_features(stdx PUBLIC cxx_std_20)

include(CheckCXXCompilerFlag)

if(MSVC)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "/std:c++latest")
    check_cxx_source_compiles(
        "
        #include <version>
        #ifndef __cpp_lib_coroutine
            #error \"coroutines not supported\"
        #endif
        int main() {}
        "
        COROUTINES_SUPPORTED
    )
    if(NOT COROUTINES_SUPPORTED)
        check_cxx_compiler_flag("/await" COROUTINES_TS_SUPPORTED)
        if(COROUTINES_TS_SUPPORTED)
            target_compile_options(stdx PUBLIC /await)
        else()
            message(FATAL_ERROR "Coroutines not supported.")
        endif()
    endif()
else()
    check_cxx_compiler_flag("-fcoroutines" COROUTINES_SUPPORTED)
    if(COROUTINES_SUPPORTED)
        target_compile_options(stdx PUBLIC -fcoroutines)
    else()
        check_cxx_compiler_flag("-fcoroutines-ts" COROUTINES_TS_SUPPORTED)
        if(COROUTINES_TS_SUPPORTED)
            target_compile_options(stdx PUBLIC -fcoroutines-ts)
        else()
            message(FATAL_ERROR "Coroutines not supported.")
        endif()
    endif()
endif()
