#!/usr/bin/env bash

set -e

git submodule update --init --recursive

cd $(dirname $0)/contrib/curl
git checkout .
git apply ../patches/curl-adjust-cmake-file.patch

cd ../libevent
git checkout .
git apply ../patches/libevent-don-t-override-CMAKE_CONFIGURATION_TYPES.patch

cd ../libressl
./autogen.sh
