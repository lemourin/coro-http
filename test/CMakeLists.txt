include(GoogleTest)

find_package(GTest REQUIRED)

add_executable(
    coro-http-test
    http_server_test.cc
)

target_link_libraries(coro-http-test GTest::gtest_main GTest::gtest coro-http)

gtest_discover_tests(coro-http-test)
