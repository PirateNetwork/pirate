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
$0 [ --enable-lcov || --enable-tests || --disable-tests ] [ --disable-mining ] [ --enable-debug ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate and most of its transitive dependencies from
  source. MAKEARGS are applied to both dependencies and Pirate itself.
  The Pirate tests are not built by default. Pass --enable-tests to build
  them, or --disable-tests to be explicit about the default.
  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation, thus enabling "make cov" to work; this also builds the
  tests, since coverage is measured by running them.
  If --disable-mining is passed, Pirate is configured to not build any mining
  code. It must be passed after the test arguments, if present.
  If --enable-debug is passed, Pirate is built with debugging information. It
  must be passed after the previous arguments, if present.
  If --enable-system-command is passed, -blocknotify/-alertnotify are allowed
  to run their configured command. It must be passed after the previous
  arguments, if present.
EOF
    exit 0
fi

# If --enable-lcov is the first argument, enable lcov coverage support:
LCOV_ARG=''
HARDENING_ARG='--enable-hardening'
# Tests are off by default for this headless node build. --enable-tests opts
# back in, and --enable-lcov forces them on since "make cov" runs them.
TEST_ARG='--enable-tests=no'
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    LCOV_ARG='--enable-lcov'
    HARDENING_ARG='--disable-hardening'
    TEST_ARG='--enable-tests=yes'
    shift
elif [ "x${1:-}" = 'x--enable-tests' ]
then
    TEST_ARG='--enable-tests=yes'
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

# If --enable-debug is the next argument, enable debugging
DEBUGGING_ARG=''
if [ "x${1:-}" = 'x--enable-debug' ]
then
    DEBUG=1
    export DEBUG
    DEBUGGING_ARG='--enable-debug'
    shift
fi

if [[ -z "${VERBOSE-}" ]]; then
   VERBOSITY="--enable-silent-rules"
else
   VERBOSITY="--disable-silent-rules"
fi

# If --enable-system-command is the next argument, allow -blocknotify/
# -alertnotify to actually run their configured command (see
# util.cpp's runCommand(), gated behind this macro).
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    CXXFLAGS="${CXXFLAGS-} -DENABLE_SYSTEM_COMMAND"
    export CXXFLAGS
    shift
fi

PREFIX="$PWD/depends/$HOST"
ARTIFACTS_DIR="$PWD/artifacts"

HOST="$HOST" BUILD="$BUILD" "$MAKE" "$@" -C ./depends/ V=1 NO_QT=1

./autogen.sh

CONFIG_SITE="$PWD/depends/$HOST/share/config.site" ./configure --prefix="$PREFIX" "$HARDENING_ARG" "$LCOV_ARG" "$TEST_ARG" "$MINING_ARG" "$DEBUGGING_ARG" "$CONFIGURE_FLAGS" --with-gui=no

"$MAKE" "$@"

# --- Packaging (headless) -------------------------------------------------
# Stage the finished binaries into a flat, repo-local artifacts/ folder via a
# throwaway DESTDIR, re-rooting just the $PREFIX subtree. --prefix above points
# into the depends tree so the build can find its dependencies; that is not
# where a human wants the finished binaries.
STAGING_DIR="$(mktemp -d)"
"$MAKE" install DESTDIR="$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}/." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

# Strip only the artifacts/bin/ copies - the binaries a human or a downstream
# packaging step actually consumes - leaving the originals under src/ with
# whatever debug info this build produced.
STRIP="$(sed -n 's/^STRIP *= *//p' Makefile | head -1)"
if [ -n "$STRIP" ]; then
    for f in "$ARTIFACTS_DIR"/bin/*; do
        if [ -f "$f" ]; then
            "$STRIP" "$f" 2>/dev/null || true
        fi
    done
fi

# Package the headless daemon/CLI bundle (pirated, pirate-cli, and whichever of
# pirate-networking/pirate-tor/pirate-i2pd this build produced) into a .zip.
# build-deb.sh is intentionally not called here: it packages the Qt desktop app
# (requires pirate-qt plus a .desktop entry and icon) and has no meaning for a
# headless build.
./zcutil/build-zip.sh "$HOST" cli
