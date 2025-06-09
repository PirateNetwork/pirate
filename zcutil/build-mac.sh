#!/usr/bin/env bash

set -eu -o pipefail

# Allow user overrides to $MAKE. Typical usage for users who need it:
#   MAKE=gmake ./zcutil/build.sh -j$(nproc)
if [[ -z "${MAKE-}" ]]; then
    MAKE=make
fi

# Allow overrides to $BUILD and $HOST for porters. Most users will not need it.
#   BUILD=i686-pc-linux-gnu ./zcutil/build.sh
if [[ -z "${BUILD-}" ]]; then
    BUILD="$(./depends/config.guess)"
fi
if [[ -z "${HOST-}" ]]; then
    HOST="$BUILD"
fi

# Allow users to set arbitrary compile flags. Most users will not need this.
if [[ -z "${CONFIGURE_FLAGS-}" ]]; then
    CONFIGURE_FLAGS=""
fi

if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:

$0 --help
  Show this help message and exit.

$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --disable-rust ] [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Pirate itself.

  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --disable-tests is passed instead, the Pirate tests are not built.

  If --disable-mining is passed, Pirate is configured to not build any mining
  code. It must be passed after the test arguments, if present.

  If --disable-rust is passed, Pirate is configured to not build any Rust language
  assets. It must be passed after test/mining arguments, if present.
EOF
    exit 0
fi

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--enable-hardening'
TEST_ARG=''
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    shift
elif [ "x${1:-}" = 'x--disable-tests' ]
then
    TEST_ARG='--enable-tests=no'
    shift
fi

# If --disable-mining is the next argument, disable mining code:
MINING_ARG=''
if [ "x${1:-}" = 'x--disable-mining' ]
then
    MINING_ARG='--enable-mining=no'
    shift
fi

# If --disable-rust is the next argument, disable Rust code:
RUST_ARG=''
if [ "x${1:-}" = 'x--disable-rust' ]
then
    RUST_ARG='--enable-rust=no'
    shift
fi

PREFIX="$(pwd)/depends/$HOST"

eval "$MAKE" --version
eval "$MAKE" "$@" -C ./depends/ V=1 NO_QT=1 NO_PROTON=1

# Detect architecture
ARCH=$(uname -m)
if [[ $ARCH == 'arm64' ]]; then
    # Add arm64 specific flags
    export RUSTFLAGS="-C link-arg=-undefined -C link-arg=dynamic_lookup"
    # Add target specification
    rustup target add aarch64-apple-darwin
fi

# Build the library
cd src/rust
cargo build --release
cd ../..

# Build the full node
./autogen.sh
CONFIG_SITE="$PREFIX/share/config.site" ./configure "$HARDENING_ARG" "$LCOV_ARG" "$TEST_ARG" "$MINING_ARG" "$RUST_ARG" $CONFIGURE_FLAGS CXXFLAGS='-g'
"$MAKE" "$@" V=1
