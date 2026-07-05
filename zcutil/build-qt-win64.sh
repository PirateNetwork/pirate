#!/bin/bash
HOST=x86_64-w64-mingw32
CXX=x86_64-w64-mingw32-g++-posix
CC=x86_64-w64-mingw32-gcc-posix
PREFIX="$(pwd)/depends/$HOST"
ARTIFACTS_DIR="$(pwd)/artifacts"

set -eu -o pipefail

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

cd depends/ && make HOST=$HOST V=1
cd ../

./autogen.sh

CONFIG_SITE=$PWD/depends/x86_64-w64-mingw32/share/config.site \
    CXXFLAGS="-DPTW32_STATIC_LIB -DCURL_STATICLIB -DCURVE_ALT_BN128 -pthread" \
    ./configure --prefix="${PREFIX}" --host=x86_64-w64-mingw32 --enable-static --disable-shared \
    --with-gui=qt5 --disable-bip70 --enable-tests=no

sed -i 's/-lboost_system-mt /-lboost_system-mt-s /' configure
cd src/
CC="${CC}" CXX="${CXX}" make "$@" V=1

cp qt/pirate-qt.exe qt/pirate-qt-win.exe
cp qt/pirate-qt-win.exe ../pirate-qt-win.exe

cd ..

# `--prefix` above points at the depends tree so the build itself can find its
# dependencies; that's not where a human wants the finished binaries, so stage
# a real `make install` through a throwaway DESTDIR and re-root just the
# `$PREFIX` subtree into a flat, repo-local artifacts/ folder.
STAGING_DIR="$(mktemp -d)"
make install DESTDIR="$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}/." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"
