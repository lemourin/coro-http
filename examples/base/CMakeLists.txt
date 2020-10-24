add_executable(base)

target_sources(base PRIVATE main.cc)
target_link_libraries(base PRIVATE coro-http event_core)