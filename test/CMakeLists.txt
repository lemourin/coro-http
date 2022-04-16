include(GoogleTest)

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

add_executable(
    coro-http-test
    test_main.cc
    http_server_test.cc
)

target_link_libraries(coro-http-test GTest::gtest coro-http)
if(NOT WIN32)
    target_link_libraries(coro-http-test libevent::pthreads)
endif()

gtest_discover_tests(coro-http-test)
