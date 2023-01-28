add_executable(client)
target_sources(client PRIVATE client.cc)
target_link_libraries(client PRIVATE coro-http)

add_executable(server)
target_sources(server PRIVATE server.cc)
target_link_libraries(server PRIVATE coro-http)
