add_library(coro-util)

set_target_properties(coro-util PROPERTIES LINKER_LANGUAGE CXX)
target_sources(coro-util PRIVATE
    function_traits.h
    type_list.h
    raii_utils.h
    for_each.h
    lru_cache.h
    event_loop.h
    event_loop.cc
    stop_token_or.h
    thread_pool.h
    thread_pool.cc
)
target_link_libraries(coro-util PUBLIC stdx event_core)