#!/usr/bin/env bash

set -eu -o pipefail
if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:
$0 --help
  Show this help message and exit.
$0 [ --enable-lcov ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate (with the Qt GUI) for aarch64 Linux and package a .deb and
  .zip into ./artifacts. If --enable-lcov is passed, Pirate is configured to
  add coverage instrumentation.
  If --enable-system-command is passed, -blocknotify/-alertnotify are
  allowed to run their configured command. It must be passed after
  --enable-lcov, if present.
EOF
    exit 0
fi
set -x
cd "$(dirname "$(readlink -f "$0")")/.."
. ./zcutil/build-common.sh

HOST=aarch64-linux-gnu
BUILD=x86_64-unknown-linux-gnu
PREFIX="$(pwd)/depends/$HOST"
ARTIFACTS_DIR="$(pwd)/artifacts"

BUILD_TYPE=RelWithDebInfo
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    BUILD_TYPE=Coverage
    shift
fi

WITH_SYSTEM_COMMAND=OFF
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    WITH_SYSTEM_COMMAND=ON
    shift
fi

pirate_depends "$HOST" "$BUILD" "$@"
pirate_cmake_configure "$HOST" build "$BUILD_TYPE" ON \
    -DBUILD_GTEST=OFF \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build "$@"
cp build/src/qt/pirate-qt ./pirate-qt-arm

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
./zcutil/build-deb.sh "$HOST"
./zcutil/build-zip.sh "$HOST" both
