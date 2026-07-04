#!/bin/bash
mydir="$PWD"
set -eu -o pipefail

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov ] [ --enable-debug ] [ MAKEARGS... ]
  Build Komodo and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Komodo itself.
  If --enable-lcov is passed, Komodo is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --enable-debug is passed, Komodo is built with debugging information. It
  must be passed after the previous arguments, if present.
EOF
    exit 0
fi

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--disable-hardening'
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
fi

# If --enable-debug is the next argument, enable debugging
DEBUGGING_ARG=''
if [ "x${1:-}" = 'x--enable-debug' ]
then
    DEBUG=1
    export DEBUG
    DEBUGGING_ARG='--enable-debug'
    shift
fi

TRIPLET=`./depends/config.guess`
PREFIX="$(pwd)/depends/$TRIPLET"
ARTIFACTS_DIR="$mydir/artifacts"

make "$@" -C ./depends/ V=1

./autogen.sh

# Detect architecture and set appropriate flags
ARCH=$(uname -m)
if [ "$ARCH" = "arm64" ]; then
    # Apple Silicon (M1/M2)
    ARCH_FLAGS="-arch arm64"
else
    # Intel x86_64
    ARCH_FLAGS="-arch x86_64"
fi

if command -v rustup >/dev/null 2>&1; then
    if [ "$ARCH" = "arm64" ]; then
        rustup target add aarch64-apple-darwin
    fi
    export RUSTC="$(rustup which rustc)"
    export CARGO="$(rustup which cargo)"
fi

CPPFLAGS="-I$PREFIX/include $ARCH_FLAGS" LDFLAGS="-L$PREFIX/lib $ARCH_FLAGS -Wl,-no_pie" \
CXXFLAGS="$ARCH_FLAGS -I$PREFIX/include -fwrapv -fno-strict-aliasing \
-Wno-deprecated-declarations -Wno-deprecated-builtins -Wno-enum-constexpr-conversion \
-Wno-unknown-warning-option -Werror -Wno-error=attributes -g" \
./configure --prefix="${PREFIX}" --disable-bip70 --with-gui=qt5 --enable-tests=no "$HARDENING_ARG" "$LCOV_ARG" "$DEBUGGING_ARG"

make "$@" NO_GTEST=0 STATIC=1

cp src/qt/pirate-qt "$mydir"/pirate-qt-mac
# Strip symbols to reduce release size.
strip -x "$mydir"/pirate-qt-mac

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

#Package as App bundle in a dmg
./makeReleaseMac.sh
