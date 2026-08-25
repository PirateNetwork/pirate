#!/bin/bash
#
# Cross-compiles Pirate's daemon/CLI (pirated.exe, pirate-cli.exe, pirate-tx.exe)
# for Windows (x86_64-w64-mingw32) from Linux, statically linked, no GUI.
#
# Usage: build-win.sh [ --enable-system-command ] [ MAKEARGS... ]
#   If --enable-system-command is passed, -blocknotify/-alertnotify are
#   allowed to run their configured command.
HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
PREFIX="$(pwd)/depends/$HOST"

set -eu -o pipefail

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

# If --enable-system-command is the next argument, allow -blocknotify/
# -alertnotify to actually run their configured command (see
# util.cpp's runCommand(), gated behind this macro).
SYSTEM_COMMAND_CXXFLAGS=''
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    SYSTEM_COMMAND_CXXFLAGS='-DENABLE_SYSTEM_COMMAND'
    shift
fi

cd depends/ && make HOST=$HOST V=1
cd ../

./autogen.sh

CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site \
    CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -pthread $SYSTEM_COMMAND_CXXFLAGS" \
    ./configure --prefix="${PREFIX}" --host=x86_64-w64-mingw32 --enable-static --disable-shared \
    --with-gui=no --disable-bip70 --enable-tests=yes

sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure
cd src/
CC="${CC}" CXX="${CXX}" make "$@" V=1
