#!/usr/bin/env bash

set -eu -o pipefail
if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:
$0 --help
  Show this help message and exit.
$0 [ --enable-lcov ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from source for
  aarch64 Linux (headless: no Qt GUI -- see build-qt-aarch64.sh for the GUI
  build). If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation.
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

pirate_depends "$HOST" "$BUILD" NO_QT=1 "$@"
pirate_cmake_configure "$HOST" build "$BUILD_TYPE" OFF \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND"
pirate_cmake_build build "$@"
