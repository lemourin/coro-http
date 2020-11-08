add_library(stdx)

set(stdx-headers
    stop_callback.h
    stop_source.h
    stop_token.h
)

set(stdx-sources
    stop_source.cc
    stop_token.cc
)

target_sources(stdx PRIVATE ${stdx-headers} ${stdx-sources})