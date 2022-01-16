include(GoogleTest)

find_package(GTest REQUIRED)

add_executable(
    coro-http-test
    http_server_test.cc
)

target_link_libraries(coro-http-test GTest::gtest GTest::gtest_main coro-http)

gtest_discover_tests(coro-http-test)
