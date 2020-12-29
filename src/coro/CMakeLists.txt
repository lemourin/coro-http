add_subdirectory(stdx)
add_subdirectory(util)

add_library(coro OBJECT)

target_sources(coro PRIVATE
    task.h
    generator.h
    semaphore.h
    shared_promise.h
    interrupted_exception.h
)

target_link_libraries(coro PUBLIC coro-util event_core stdx)
target_include_directories(coro PUBLIC ..)

add_subdirectory(http)
