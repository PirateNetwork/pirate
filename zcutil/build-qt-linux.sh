#!/usr/bin/env bash

set -eu -o pipefail

function cmd_pref() {
    if type -p "$2" > /dev/null; then
        eval "$1=$2"
    else
        eval "$1=$3"
    fi
}

# If a g-prefixed version of the command exists, use it preferentially.
function gprefix() {
    cmd_pref "$1" "g$2" "$2"
}

gprefix READLINK readlink
cd "$(dirname "$("$READLINK" -f "$0")")/.."

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
$0 [ --enable-lcov || --disable-tests ] [ --disable-mining ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Pirate itself.
  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation, thus enabling "make cov" to work.
  If --disable-tests is passed instead, the Pirate tests are not built.
  If --disable-mining is passed, Pirate is configured to not build any mining
  code. It must be passed after the test arguments, if present.
  If --enable-system-command is passed, -blocknotify/-alertnotify are allowed
  to run their configured command. It must be passed after the previous
  arguments, if present.

  This build always enables debugging information (--enable-debug), for use
  with build-deb.sh/build-zip.sh's Linux packaging step.
EOF
    exit 0
fi

set -x

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
    TEST_ARG='--enable-tests=yes'
    shift
fi

# If --disable-mining is the next argument, disable mining code:
MINING_ARG=''
if [ "x${1:-}" = 'x--disable-mining' ]
then
    MINING_ARG='--enable-mining=no'
    shift
fi

# If --enable-system-command is the next argument, allow -blocknotify/
# -alertnotify to actually run their configured command (see
# util.cpp's runCommand(), gated behind this macro).
SYSTEM_COMMAND_CXXFLAGS=''
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    SYSTEM_COMMAND_CXXFLAGS='-DENABLE_SYSTEM_COMMAND'
    shift
fi

PREFIX="$(pwd)/depends/$BUILD/"
ARTIFACTS_DIR="$(pwd)/artifacts"

HOST="$HOST" BUILD="$BUILD" "$MAKE" "$@" -C ./depends/ V=1
./autogen.sh

DEBUG=1
export DEBUG
DEBUGGING_ARG='--enable-debug'

./configure --prefix="${PREFIX}" --with-gui=qt5 --disable-bip70 --enable-tests=yes --enable-wallet=yes "$DEBUGGING_ARG" "$HARDENING_ARG" "$LCOV_ARG" "$TEST_ARG" "$MINING_ARG" $CONFIGURE_FLAGS CXXFLAGS="-g0 $SYSTEM_COMMAND_CXXFLAGS"
# -Wunused -Wunreachable-code'

nice -n 20 "$MAKE" "$@"
#V=1

cp src/qt/pirate-qt ./pirate-qt-linux

# `--prefix` above points at the depends tree so the build itself can find its
# dependencies; that's not where a human wants the finished binaries, so stage
# a real `make install` through a throwaway DESTDIR and re-root just the
# `$PREFIX` subtree into a flat, repo-local artifacts/ folder.
STAGING_DIR="$(mktemp -d)"
"$MAKE" install DESTDIR="$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

# This build is always --enable-debug (see above), so everything under
# src/ is left with full debug info intact for local debugging. Strip only
# the artifacts/bin/ copies - the binaries a human or a downstream packaging
# step (e.g. build-deb.sh) actually consumes - not the originals.
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
