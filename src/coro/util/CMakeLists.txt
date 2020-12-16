add_library(coro-util OBJECT)

set(coro-util-headers
    function_traits.h
    type_list.h
    wrap.h
    make_pointer.h
    for_each.h
    lru_cache.h
)

set_target_properties(coro-util PROPERTIES LINKER_LANGUAGE CXX)
target_sources(coro-util PRIVATE ${coro-util-headers})