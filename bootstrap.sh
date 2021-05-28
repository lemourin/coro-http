#!/usr/bin/env bash

set -e

git submodule update --init --recursive

cd $(dirname $0)/contrib/libressl
./update.sh
