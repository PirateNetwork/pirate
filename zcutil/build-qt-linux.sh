#!/usr/bin/env bash

set -eu -o pipefail
function cmd_pref() {
    if type -p "$2" > /dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}
function gprefix() {
    cmd_pref "$1" "g$2" "$2"
}
gprefix READLINK readlink
cd "$(dirname "$("$READLINK" -f "$0")")/.."
. ./zcutil/build-common.sh

if [[ -z "${MAKE-}" ]]; then
    MAKE=make
fi
if [[ -z "${BUILD-}" ]]; then
    BUILD="$(./depends/config.guess)"
fi
if [[ -z "${HOST-}" ]]; then
    HOST="$BUILD"
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:
$0 --help
  Show this help message and exit.
$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate (with the Qt GUI) and most of its transitive dependencies from
  source, then package a .deb and .zip into ./artifacts.
  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation.
  If --disable-tests is passed instead, the Pirate tests are not built.
  If --disable-mining is passed, Pirate is configured to not build any mining
  code. It must be passed after the test arguments, if present.
  If --enable-system-command is passed, -blocknotify/-alertnotify are allowed
  to run their configured command. It must be passed after the previous
  arguments, if present.
  This build always enables debugging information, for use with
  build-deb.sh/build-zip.sh's Linux packaging step.
EOF
    exit 0
fi
set -x

# RelWithDebInfo, matching every other build script: it keeps the debug info
# the packaging step below wants while staying optimised. A full Debug build
# also defines DEBUG, turns on assertions, and (via ProcessConfigurations.cmake)
# adds -ftrapv, which none of these belong in a shipped artifact.
BUILD_TYPE=RelWithDebInfo
BUILD_GTEST=ON
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    BUILD_TYPE=Coverage
    shift
elif [ "x${1:-}" = 'x--disable-tests' ]
then
    BUILD_GTEST=OFF
    shift
fi

ENABLE_MINING=ON
if [ "x${1:-}" = 'x--disable-mining' ]
then
    ENABLE_MINING=OFF
    shift
fi

WITH_SYSTEM_COMMAND=OFF
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    WITH_SYSTEM_COMMAND=ON
    shift
fi

PREFIX="$(pwd)/depends/$BUILD"
ARTIFACTS_DIR="$(pwd)/artifacts"

pirate_depends "$HOST" "$BUILD" "$@"
pirate_cmake_configure "$HOST" build "$BUILD_TYPE" ON \
    -DBUILD_GTEST="$BUILD_GTEST" \
    -DENABLE_MINING="$ENABLE_MINING" \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
nice -n 20 cmake --build build "$@"
cp build/src/qt/pirate-qt ./pirate-qt-linux

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
