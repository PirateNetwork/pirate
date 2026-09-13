#!/usr/bin/env bash

mydir="$PWD"
set -eu -o pipefail
if [ "x$*" = 'x--help' ]
then
    cat <<EOF
Usage:
$0 --help
  Show this help message and exit.
$0 [ --enable-lcov ] [ --enable-debug ] [ --enable-system-command ] [ MAKEARGS... ]
  Build Pirate (with the Qt GUI) for macOS and package a .dmg + .zip into
  ./artifacts.
  If --enable-lcov is passed, Pirate is configured to add coverage
  instrumentation.
  If --enable-debug is passed, Pirate is built with debugging information. It
  must be passed after the previous arguments, if present.
  If --enable-system-command is passed, -blocknotify/-alertnotify are allowed
  to run their configured command. It must be passed after the previous
  arguments, if present.
EOF
    exit 0
fi
. ./zcutil/build-common.sh

BUILD_TYPE=RelWithDebInfo
if [ "x${1:-}" = 'x--enable-lcov' ]
then
    BUILD_TYPE=Coverage
    shift
fi

if [ "x${1:-}" = 'x--enable-debug' ]
then
    BUILD_TYPE=Debug
    shift
fi

WITH_SYSTEM_COMMAND=OFF
if [ "x${1:-}" = 'x--enable-system-command' ]
then
    WITH_SYSTEM_COMMAND=ON
    shift
fi

TRIPLET=$(./depends/config.guess)
PREFIX="$(pwd)/depends/$TRIPLET"
ARTIFACTS_DIR="$mydir/artifacts"

pirate_depends "$TRIPLET" "" "$@"

ARCH=$(uname -m)
if command -v rustup >/dev/null 2>&1; then
    if [ "$ARCH" = "arm64" ]; then
        rustup target add aarch64-apple-darwin
    fi
fi

pirate_cmake_configure "$TRIPLET" build "$BUILD_TYPE" ON \
    -DBUILD_GTEST=OFF \
    -DWITH_SYSTEM_COMMAND="$WITH_SYSTEM_COMMAND" \
    -DCMAKE_INSTALL_PREFIX="$PREFIX"
cmake --build build "$@"
cp build/src/qt/pirate-qt "$mydir"/pirate-qt-mac
strip -x "$mydir"/pirate-qt-mac

STAGING_DIR="$(mktemp -d)"
pirate_cmake_install build "$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}/." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

STRIP="$(pirate_strip_tool "$TRIPLET")"
if [ -n "$STRIP" ]; then
    for f in "$ARTIFACTS_DIR"/bin/*; do
        if [ -f "$f" ]; then
            "$STRIP" "$f" 2>/dev/null || true
        fi
    done
fi
for bin in pirate-tor pirate-i2pd pirate-networking; do
    if [ -f "$ARTIFACTS_DIR/bin/$bin" ]; then
        cp "$ARTIFACTS_DIR/bin/$bin" "$mydir/$bin"
    fi
done

APP_VERSION="$(pirate_version || echo '')"
if [ -n "${APP_VERSION_SUFFIX:-}" ]; then
    APP_VERSION="${APP_VERSION}-${APP_VERSION_SUFFIX}"
fi
export APP_VERSION
./makeReleaseMac.sh
if [ -n "$APP_VERSION" ] && [ -f "$mydir/pirate-qt-mac.dmg" ]; then
    case "$TRIPLET" in
        *-apple-darwin*) PLATFORM="${TRIPLET%%-*}-macos" ;;
        *) PLATFORM="$TRIPLET" ;;
    esac
    mkdir -p "$ARTIFACTS_DIR/bin"
    mv "$mydir/pirate-qt-mac.dmg" "$ARTIFACTS_DIR/bin/pirate-qt-${PLATFORM}-v${APP_VERSION}.dmg"
fi
./zcutil/build-zip.sh "$TRIPLET" cli
