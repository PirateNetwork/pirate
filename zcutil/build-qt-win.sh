#!/usr/bin/env bash

HOST=x86_64-w64-mingw32
PREFIX="$(pwd)/depends/$HOST"
ARTIFACTS_DIR="$(pwd)/artifacts"
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
pirate_cmake_configure "$HOST" build RelWithDebInfo ON \
    -DBUILD_GTEST=OFF \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build "$@"
cp build/src/qt/pirate-qt.exe build/src/qt/pirate-qt-win.exe
cp build/src/qt/pirate-qt-win.exe ../pirate-qt-win.exe

STAGING_DIR="$(mktemp -d)"
pirate_cmake_install build "$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}/." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

STRIP="$(pirate_strip_tool "$HOST")"
if [ -n "$STRIP" ]; then
    for f in "$ARTIFACTS_DIR"/bin/*; do
        if [ -f "$f" ]; then
            "$STRIP" "$f" 2>/dev/null || true
        fi
    done
fi
./zcutil/build-zip.sh "$HOST" both
