add_subdirectory(curl)

set(EVENT__DISABLE_OPENSSL ON)
set(EVENT__DISABLE_MBEDTLS ON)
set(EVENT__LIBRARY_TYPE STATIC)
add_subdirectory(libevent)