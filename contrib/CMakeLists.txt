set(CARES_SHARED OFF CACHE BOOL "build shared c-ares")
set(CARES_STATIC ON CACHE BOOL "build static c-ares")
unset(RANDOM_FILE CACHE)
add_subdirectory(c-ares EXCLUDE_FROM_ALL)

set(ENABLE_LIB_ONLY ON CACHE BOOL "build only nghttp2 lib")
set(ENABLE_STATIC_LIB ON CACHE BOOL "build static nghttp2")
set(ENABLE_SHARED_LIB OFF CACHE BOOL "build shared nghttp2")
add_subdirectory(nghttp2 EXCLUDE_FROM_ALL)

add_subdirectory(libressl EXCLUDE_FROM_ALL)

set(CMAKE_USE_OPENSSL ON CACHE BOOL "enable curl with openssl")
set(ENABLE_ARES ON CACHE BOOL "enable curl with c-ares")
set(USE_NGHTTP2 ON CACHE BOOL "enable curl with nghttp2")
add_subdirectory(curl EXCLUDE_FROM_ALL)
target_include_directories(libcurl PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/nghttp2/lib/includes
    ${CMAKE_CURRENT_BINARY_DIR}/nghttp2/lib/includes
)
target_link_libraries(libcurl c-ares nghttp2_static ssl)

set(EVENT__DISABLE_OPENSSL ON CACHE BOOL "build libevent without OpenSSL")
set(EVENT__DISABLE_MBEDTLS ON CACHE BOOL "build libevent without mbedTLS")
if(BUILD_SHARED_LIBS)
    set(EVENT__LIBRARY_TYPE SHARED CACHE BOOL "build shared libevent")
else()
    set(EVENT__LIBRARY_TYPE STATIC CACHE BOOL "build static libevent")
endif()
add_subdirectory(libevent EXCLUDE_FROM_ALL)
