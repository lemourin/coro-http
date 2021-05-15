#!/usr/bin/env bash

set -e

git submodule update --init --recursive

cd $(dirname $0)/contrib/curl
git checkout .
git apply ../patches/CMake-add-CURL_ENABLE_EXPORT_TARGET-option.patch

cd ../libressl
./update.sh
