#!/bin/bash
HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
PREFIX="$(pwd)/depends/$HOST"

set -eu -o pipefail

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

cd depends/ && make HOST=$HOST V=1
cd ../

./autogen.sh

CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site \
    CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -fopenmp -pthread -g0 -O2" \
    ./configure --prefix="${PREFIX}" --host=x86_64-w64-mingw32 --enable-static --disable-shared \
    --with-gui=qt5 --disable-bip70 --enable-tests=no

sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure
cd src/
CC="${CC}" CXX="${CXX}" make "$@" V=1

mv qt/komodo-qt.exe qt/pirate-qt-win.exe
cp qt/pirate-qt-win.exe ../pirate-qt-win.exe
