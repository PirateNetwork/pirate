#!/bin/bash

#sudo apt-get install gcc-aarch64-linux-gnu
#sudo apt-get install g++-aarch64-linux-gnu



set -eu -o pipefail

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov ] [ MAKEARGS... ]
  Build Zcash and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Zcash itself. If
  --enable-lcov is passed, Zcash is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
EOF
    exit 0
fi

set -x
cd "$(dirname "$(readlink -f "$0")")/.."

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--disable-hardening'
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
fi

# BUG: parameterize the platform/host directory:
HOST=aarch64-linux-gnu
BUILD=x86_64-unknown-linux-gnu
PREFIX="$(pwd)/depends/$HOST/"
ARTIFACTS_DIR="$(pwd)/artifacts"

HOST="$HOST" BUILD="$BUILD" make "$@" -C ./depends/ V=1
./autogen.sh
CONFIG_SITE="$(pwd)/depends/$HOST/share/config.site" ./configure --prefix="${PREFIX}" --host="$HOST" --build="$BUILD" --with-gui=qt5 --disable-bip70 --enable-tests=no --enable-online-rust=yes "$HARDENING_ARG" "$LCOV_ARG" CXXFLAGS='-fwrapv -fno-strict-aliasing -g'

make "$@" V=1

cp src/qt/pirate-qt ./pirate-qt-arm

# `--prefix` above points at the depends tree so the build itself can find its
# dependencies; that's not where a human wants the finished binaries, so stage
# a real `make install` through a throwaway DESTDIR and re-root just the
# `$PREFIX` subtree into a flat, repo-local artifacts/ folder.
STAGING_DIR="$(mktemp -d)"
make install DESTDIR="$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

# CXXFLAGS above includes -g, so everything under src/ is left with full
# debug info intact for local debugging. Strip only the artifacts/bin/
# copies - the binaries a human or a downstream packaging step (e.g.
# build-deb.sh) actually consumes - not the originals.
STRIP="$(sed -n 's/^STRIP *= *//p' Makefile | head -1)"
if [ -n "$STRIP" ]; then
    for f in "$ARTIFACTS_DIR"/bin/*; do
        if [ -f "$f" ]; then
            "$STRIP" "$f" 2>/dev/null || true
        fi
    done
fi

./zcutil/build-deb.sh "$HOST"
./zcutil/build-zip.sh "$HOST" both
