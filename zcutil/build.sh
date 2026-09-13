#!/usr/bin/env bash

set -eu -o pipefail

function cmd_pref() {
    if type -p "$2" > /dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}

# If a g-prefixed version of the command exists, use it preferentially.
function gprefix() {
    cmd_pref "$1" "g$2" "$2"
}

gprefix READLINK readlink
cd "$(dirname "$("$READLINK" -f "$0")")/.."
. ./zcutil/build-common.sh

# Allow user overrides to $MAKE. Typical usage for users who need it:
#   MAKE=gmake ./zcutil/build.sh -j$(nproc)
if [[ -z "${MAKE-}" ]]; then
    MAKE=make
fi

# Allow overrides to $BUILD and $HOST for porters. Most users will not need it.
#   BUILD=i686-pc-linux-gnu ./zcutil/build.sh
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
$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --enable-debug ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from
  source (headless: no Qt GUI). MAKEARGS are applied to the CMake build step.
  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation, thus enabling "cmake --build build --target ExperimentalCoverage" to work.
  If --disable-tests is passed instead, the Pirate tests are not built.
  If --disable-mining is passed, Pirate is configured to not build any mining
  code. It must be passed after the test arguments, if present.
  If --enable-debug is passed, Pirate is built with debugging information. It
  must be passed after the previous arguments, if present.
  If --enable-system-command is passed, -blocknotify/-alertnotify are allowed
  to run their configured command. It must be passed after the previous
  arguments, if present.
EOF
    exit 0
fi

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

if [ "x${1:-}" = 'x--enable-debug' ]
then
    BUILD_TYPE=Debug
    shift
fi

WITH_SYSTEM_COMMAND=OFF
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    WITH_SYSTEM_COMMAND=ON
    shift
fi

pirate_depends "$HOST" "$BUILD" NO_QT=1 "$@"
pirate_cmake_configure "$HOST" build "$BUILD_TYPE" OFF \
    -DBUILD_GTEST="$BUILD_GTEST" \
    -DENABLE_MINING="$ENABLE_MINING" \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND"
pirate_cmake_build build "$@"
