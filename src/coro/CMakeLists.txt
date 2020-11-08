add_subdirectory(stdx)

add_library(coro OBJECT)

set(coro-headers
    task.h
    wait_task.h
)

set(coro-sources
    wait_task.cc
)

target_sources(coro PRIVATE ${coro-headers} ${coro-sources})

target_link_libraries(coro PUBLIC event_core stdx)
target_include_directories(coro PUBLIC ..)

add_subdirectory(http)
