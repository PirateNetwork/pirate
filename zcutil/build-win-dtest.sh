#!/bin/bash
#
# Cross-compiles a debug/TESTMODE build of Pirate's daemon/CLI for Windows
# (x86_64-w64-mingw32) from Linux, for local dtest use - not a release build.
#
# Usage: build-win-dtest.sh [ --enable-system-command ] [ MAKEARGS... ]
#   If --enable-system-command is passed, -blocknotify/-alertnotify are
#   allowed to run their configured command.
export HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix

set -eu -o pipefail
set -x

UTIL_DIR="$(dirname "$(readlink -f "$0")")"
BASE_DIR="$(dirname "$(readlink -f "$UTIL_DIR")")"
PREFIX="$BASE_DIR/depends/$HOST"

# If --enable-system-command is the next argument, allow -blocknotify/
# -alertnotify to actually run their configured command (see
# util.cpp's runCommand(), gated behind this macro).
SYSTEM_COMMAND_CXXFLAGS=''
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    SYSTEM_COMMAND_CXXFLAGS='-DENABLE_SYSTEM_COMMAND'
    shift
fi

cd $BASE_DIR/depends
make HOST=$HOST NO_QT=1 "$@"
cd $BASE_DIR

./autogen.sh
CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -pthread $SYSTEM_COMMAND_CXXFLAGS" CPPFLAGS=-DTESTMODE ./configure --prefix="${PREFIX}" --host=x86_64-w64-mingw32 --enable-static --disable-shared
sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure
cd src/
CC="${CC} -g " CXX="${CXX} -g " make V=1  pirated.exe pirate-cli.exe pirate-tx.exe
