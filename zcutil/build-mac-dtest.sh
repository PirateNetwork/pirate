#!/bin/bash
set -eu -o pipefail

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Pirate itself.
EOF
    exit 0
fi

TRIPLET=`./depends/config.guess`
PREFIX="$(pwd)/depends/$TRIPLET"

make "$@" -C ./depends/ V=1 NO_QT=1

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

CPPFLAGS="-I$PREFIX/include $ARCH_FLAGS -DTESTMODE" LDFLAGS="-L$PREFIX/lib $ARCH_FLAGS -Wl,-no_pie" \
CXXFLAGS="$ARCH_FLAGS -I$PREFIX/include -fwrapv -fno-strict-aliasing \
-Wno-deprecated-declarations -Wno-deprecated-builtins -Wno-enum-constexpr-conversion \
-Wno-unknown-warning-option -Werror -Wno-error=attributes -g" \
./configure --prefix="${PREFIX}" --with-gui=no --disable-hardening --enable-debug

make "$@" V=1 NO_GTEST=0 STATIC=1
