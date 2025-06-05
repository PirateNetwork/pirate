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

CPPFLAGS="-I$PREFIX/include $ARCH_FLAGS" LDFLAGS="-L$PREFIX/lib $ARCH_FLAGS -Wl,-no_pie" \
CXXFLAGS="$ARCH_FLAGS -I$PREFIX/include -fwrapv -fno-strict-aliasing \
-Wno-deprecated-declarations -Wno-deprecated-builtins -Wno-enum-constexpr-conversion \
-Wno-unknown-warning-option -Werror -Wno-error=attributes -g" \
./configure --prefix="${PREFIX}" --disable-bip70 --with-gui=qt5 "$HARDENING_ARG" "$LCOV_ARG" "$DEBUGGING_ARG"

make "$@" NO_GTEST=0 STATIC=1

cp src/qt/komodo-qt "$mydir"/pirate-qt-mac

#Package as App bundle in a dmg
./makeReleaseMac.sh
