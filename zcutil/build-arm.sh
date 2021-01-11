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
PREFIX="$(pwd)/depends/aarch64-linux-gnu/"

HOST=aarch64-linux-gnu BUILD=x86_64-unknown-linux-gnu make "$@" -C ./depends/ V=1 NO_QT=1
./autogen.sh
CONFIG_SITE="$(pwd)/depends/aarch64-linux-gnu/share/config.site" ./configure --prefix="${PREFIX}" --host=aarch64-linux-gnu --build=x86_64-unknown-linux-gnu --with-gui=no "$HARDENING_ARG" "$LCOV_ARG" CXXFLAGS='-fwrapv -fno-strict-aliasing -g'

# #BUILD CCLIB
#
# WD=$PWD
# cd src/cc
# echo $PWD
# ./makecustom
# cd $WD

make "$@" V=1
