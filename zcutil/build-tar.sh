#!/usr/bin/env bash
#
# Packages the already-built binaries from artifacts/bin/ into two .tar.gz
# archives per platform:
#   pirate-cli-<arch>-linux-v<version>.tar.gz - pirated, pirate-cli,
#     pirate-networking, pirate-tor, pirate-i2pd
#   pirate-qt-<arch>-linux-v<version>.tar.gz  - pirate-qt,
#     pirate-networking, pirate-tor, pirate-i2pd
#
# pirate-networking/pirate-tor/pirate-i2pd are each conditional on how the
# build was configured (--disable-embedded-onion-routing), so a missing one
# is omitted rather than failing the whole archive - see build-deb.sh, which
# has the same tolerance.
#
# Called automatically at the end of build-qt-linux.sh and build-qt-arm.sh
# (passing their own $HOST), after their `make install` step has staged
# (and stripped) those binaries into artifacts/bin/ - not meant to be run
# standalone against a checkout that hasn't been built yet.
#
# Usage: build-tar.sh <host-triplet>   (e.g. x86_64-unknown-linux-gnu, aarch64-linux-gnu)
# These scripts only ever target Linux, so the vendor/libc part of the
# triplet is noise in a filename - just the architecture plus a plain
# "linux" suffix (see PLATFORM below) is enough to tell them apart.

set -eu -o pipefail

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <host-triplet>" >&2
    exit 1
fi
HOST="$1"

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

ARTIFACTS_DIR="$(pwd)/artifacts"
BIN_DIR="$ARTIFACTS_DIR/bin"

# Read the version ./configure already derived from configure.ac's
# _CLIENT_VERSION_* macros (e.g. "6.0.0-rc2"), rather than hand-maintaining
# a second copy of it here - same approach as build-deb.sh.
APP_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)"
if [ -z "$APP_VERSION" ]; then
    echo "warning: could not determine PACKAGE_VERSION from ./Makefile (did ./configure run first?), skipping .tar.gz packaging" >&2
    exit 0
fi

PLATFORM="${HOST%%-*}-linux"

# package <package-base-name> <binary>...
# Bundles whichever of the named binaries actually exist in $BIN_DIR into
# <package-base-name>-$PLATFORM-v$APP_VERSION.tar.gz, wrapped in a
# same-named top-level directory (so extracting it doesn't scatter files
# into whatever directory the user happened to extract into).
package() {
    local pkg_name="$1"
    shift
    local archive_name="${pkg_name}-${PLATFORM}-v${APP_VERSION}"
    local staging_dir
    staging_dir="$(mktemp -d)"
    local pkg_dir="$staging_dir/$archive_name"
    mkdir -p "$pkg_dir"

    local installed_any=0
    local bin
    for bin in "$@"; do
        if [ -f "$BIN_DIR/$bin" ]; then
            cp "$BIN_DIR/$bin" "$pkg_dir/$bin"
            installed_any=1
        else
            echo "warning: $BIN_DIR/$bin not found, omitting from $archive_name.tar.gz" >&2
        fi
    done

    if [ "$installed_any" -eq 0 ]; then
        echo "warning: none of the expected binaries were found for $archive_name, skipping" >&2
        rm -rf "$staging_dir"
        return
    fi

    local output_tar="$BIN_DIR/${archive_name}.tar.gz"
    rm -f "$output_tar"
    tar -czf "$output_tar" -C "$staging_dir" "$archive_name"
    rm -rf "$staging_dir"
    echo "Wrote $output_tar"
}

package pirate-cli pirated pirate-cli pirate-networking pirate-tor pirate-i2pd
package pirate-qt pirate-qt pirate-networking pirate-tor pirate-i2pd
