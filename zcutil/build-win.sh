#!/usr/bin/env bash

HOST=x86_64-w64-mingw32
set -eu -o pipefail
set -x
cd "$(dirname "$(readlink -f "$0")")/.."
. ./zcutil/build-common.sh

WITH_SYSTEM_COMMAND=OFF
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    WITH_SYSTEM_COMMAND=ON
    shift
fi

pirate_depends "$HOST" "" "$@"
pirate_cmake_configure "$HOST" build RelWithDebInfo OFF \
    -DBUILD_GTEST=ON \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND"
pirate_cmake_build build "$@"
