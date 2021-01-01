#!/usr/bin/env bash

set -e

git submodule update --init --recursive

cd $(dirname $0)/contrib/curl
git checkout .
git apply ../patches/curl-adjust-cmake-file.patch

cd ../libressl
./autogen.sh
