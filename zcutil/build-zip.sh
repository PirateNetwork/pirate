#!/usr/bin/env bash
#
# Packages the already-built binaries from artifacts/bin/ into .zip
# archives per platform:
#   pirate-cli-<arch>-<os>-v<version>.zip - pirated, pirate-cli,
#     pirate-networking, pirate-tor, pirate-i2pd
#   pirate-qt-<arch>-<os>-v<version>.zip  - pirate-qt, pirate-networking,
#     pirate-tor, pirate-i2pd
# (each with a .exe suffix instead, on Windows)
#
# pirate-networking/pirate-tor/pirate-i2pd are each conditional on how the
# build was configured (--disable-embedded-onion-routing), so a missing one
# is omitted rather than failing the whole archive - see build-deb.sh, which
# has the same tolerance.
#
# Called automatically at the end of build-qt-linux.sh, build-qt-aarch64.sh,
# build-qt-win.sh (all: both), and build-qt-mac.sh (cli only - the Qt
# package there is the .dmg app bundle build-qt-mac.sh/makeReleaseMac.sh
# already produce, not a flat zip of raw binaries), each passing their own
# $HOST, after their `make install` step has staged (and stripped) those
# binaries into artifacts/bin/ - not meant to be run standalone against a
# checkout that hasn't been built yet.
#
# Usage: build-zip.sh <host-triplet> <cli|qt|both>
# host-triplet e.g. x86_64-unknown-linux-gnu, aarch64-linux-gnu,
# x86_64-w64-mingw32, x86_64-apple-darwin23.4.0 - only the architecture
# (first component) and a recognized OS suffix are used; the vendor/libc
# part is noise in a filename (see PLATFORM below).

set -eu -o pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <host-triplet> <cli|qt|both>" >&2
    exit 1
fi
HOST="$1"
WHICH="$2"
case "$WHICH" in
    cli|qt|both) ;;
    *)
        echo "Usage: $0 <host-triplet> <cli|qt|both>" >&2
        exit 1
        ;;
esac

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

# A build host without `zip` should not fail the whole build over a step it
# never needed - warn and skip instead.
if ! type -p zip > /dev/null; then
    echo "warning: zip not found, skipping .zip packaging" >&2
    exit 0
fi

ARTIFACTS_DIR="$(pwd)/artifacts"
BIN_DIR="$ARTIFACTS_DIR/bin"

# Read the version ./configure already derived from configure.ac's
# _CLIENT_VERSION_* macros (e.g. "6.0.0-rc2"), rather than hand-maintaining
# a second copy of it here - same approach as build-deb.sh.
APP_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)"
if [ -z "$APP_VERSION" ]; then
    echo "warning: could not determine PACKAGE_VERSION from ./Makefile (did ./configure run first?), skipping .zip packaging" >&2
    exit 0
fi
# CI sets this on non-tag builds (e.g. a short git sha) so repeated builds
# off the same configure.ac version don't collide - see pirate_build_all.yml.
if [ -n "${APP_VERSION_SUFFIX:-}" ]; then
    APP_VERSION="${APP_VERSION}-${APP_VERSION_SUFFIX}"
fi

case "$HOST" in
    *-w64-mingw32*) PLATFORM="${HOST%%-*}-windows" ;;
    *-apple-darwin*) PLATFORM="${HOST%%-*}-macos" ;;
    *) PLATFORM="${HOST%%-*}-linux" ;;
esac

# package <package-base-name> <binary>...
# Bundles whichever of the named binaries actually exist in $BIN_DIR (or
# $BIN_DIR/<name>.exe, on Windows) into
# <package-base-name>-$PLATFORM-v$APP_VERSION.zip, wrapped in a same-named
# top-level directory (so extracting it doesn't scatter files into whatever
# directory the user happened to extract into).
package() {
    local pkg_name="$1"
    shift
    local archive_name="${pkg_name}-${PLATFORM}-v${APP_VERSION}"
    local staging_dir
    staging_dir="$(mktemp -d)"
    local pkg_dir="$staging_dir/$archive_name"
    mkdir -p "$pkg_dir"

    local installed_any=0
    local bin src
    for bin in "$@"; do
        src=""
        if [ -f "$BIN_DIR/$bin" ]; then
            src="$BIN_DIR/$bin"
        elif [ -f "$BIN_DIR/$bin.exe" ]; then
            src="$BIN_DIR/$bin.exe"
            bin="$bin.exe"
        fi
        if [ -n "$src" ]; then
            cp "$src" "$pkg_dir/$bin"
            installed_any=1
        else
            echo "warning: $BIN_DIR/$bin not found, omitting from $archive_name.zip" >&2
        fi
    done

    if [ "$installed_any" -eq 0 ]; then
        echo "warning: none of the expected binaries were found for $archive_name, skipping" >&2
        rm -rf "$staging_dir"
        return
    fi

    local output_zip="$BIN_DIR/${archive_name}.zip"
    rm -f "$output_zip"
    (cd "$staging_dir" && zip -rq "$output_zip" "$archive_name")
    rm -rf "$staging_dir"
    echo "Wrote $output_zip"
}

if [ "$WHICH" = "cli" ] || [ "$WHICH" = "both" ]; then
    package pirate-cli pirated pirate-cli pirate-networking pirate-tor pirate-i2pd
fi
if [ "$WHICH" = "qt" ] || [ "$WHICH" = "both" ]; then
    package pirate-qt pirate-qt pirate-networking pirate-tor pirate-i2pd
fi
