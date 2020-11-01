add_library(coro OBJECT)

set(coro-headers
    task.h
    wait_task.h
)

set(coro-sources
    wait_task.cpp
)

target_sources(coro PRIVATE ${coro-headers} ${coro-sources})

target_link_libraries(coro PUBLIC event_core)
target_include_directories(coro PUBLIC ..)

set_target_properties(coro
    PROPERTIES
        CXX_STANDARD 20
        LINKER_LANGUAGE CXX
)

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
            target_compile_options(coro PUBLIC /await)
        else()
            message(FATAL_ERROR "Coroutines not supported.")
        endif()
    endif()
else()
    check_cxx_compiler_flag("-fcoroutines" COROUTINES_SUPPORTED)
    if(COROUTINES_SUPPORTED)
        target_compile_options(coro PUBLIC -fcoroutines)
    else()
        check_cxx_compiler_flag("-fcoroutines-ts" COROUTINES_TS_SUPPORTED)
        if(COROUTINES_TS_SUPPORTED)
            target_compile_options(coro PUBLIC -fcoroutines-ts)
        else()
            message(FATAL_ERROR "Coroutines not supported.")
        endif()
    endif()
endif()

add_subdirectory(http)
