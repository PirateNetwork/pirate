#!/usr/bin/env bash
#
# Packages the already-built pirate-qt bundle - pirate-qt itself, plus
# whichever of pirate-tor/pirate-i2pd/pirate-networking this build produced
# (see doc/tor.md, doc/i2p.md; each is conditional on how the build was
# configured) - from artifacts/bin/ into a .deb, using the control file,
# icon, and .desktop entry under zcutil/deb/.
#
# Called automatically at the end of build-qt-linux.sh and build-qt-aarch64.sh
# (passing their own $HOST), after their `make install` step has staged
# those binaries into artifacts/bin/ - not meant to be run standalone
# against a checkout that hasn't been built yet.
#
# Usage: build-deb.sh <host-triplet>   (e.g. x86_64-unknown-linux-gnu, aarch64-linux-gnu)
# The GNU config triplet, not a dpkg architecture name (that's derived
# below, since dpkg-deb's Architecture: field can't hold a full triplet) and
# not what ends up in the output filename either - see PLATFORM below.

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

# A build host without Debian packaging tools (e.g. macOS, used for
# build-qt-mac.sh) should not fail the whole build over a step it never
# needed - warn and skip instead.
if ! type -p dpkg-deb > /dev/null; then
    echo "warning: dpkg-deb not found, skipping .deb packaging" >&2
    exit 0
fi

ARTIFACTS_DIR="$(pwd)/artifacts"
BIN_DIR="$ARTIFACTS_DIR/bin"
if [ ! -f "$BIN_DIR/pirate-qt" ]; then
    echo "warning: $BIN_DIR/pirate-qt not found (did the build/install step run first?), skipping .deb packaging" >&2
    exit 0
fi

case "$HOST" in
    x86_64-*) CONTROL_TEMPLATE="zcutil/deb/control_amd64" ;;
    aarch64-*) CONTROL_TEMPLATE="zcutil/deb/control_aarch64" ;;
    *)
        echo "warning: no .deb control template for host triplet '$HOST', skipping .deb packaging" >&2
        exit 0
        ;;
esac

# Read the version ./configure already derived from configure.ac's
# _CLIENT_VERSION_* macros (e.g. "6.0.0-rc2"), rather than hand-maintaining
# a second copy of it here.
APP_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)"
if [ -z "$APP_VERSION" ]; then
    echo "warning: could not determine PACKAGE_VERSION from ./Makefile (did ./configure run first?), skipping .deb packaging" >&2
    exit 0
fi
# CI sets this on non-tag builds (e.g. a short git sha) so repeated builds
# off the same configure.ac version don't collide - see pirate_build_all.yml.
if [ -n "${APP_VERSION_SUFFIX:-}" ]; then
    APP_VERSION="${APP_VERSION}-${APP_VERSION_SUFFIX}"
fi

PKG_ROOT="$(mktemp -d)"
trap 'rm -rf "$PKG_ROOT"' EXIT

mkdir -p "$PKG_ROOT/DEBIAN" "$PKG_ROOT/usr/local/bin" "$PKG_ROOT/usr/share/pixmaps" "$PKG_ROOT/usr/share/applications"

sed "s/RELEASE_VERSION/$APP_VERSION/g" "$CONTROL_TEMPLATE" > "$PKG_ROOT/DEBIAN/control"

cp "$BIN_DIR/pirate-qt" "$PKG_ROOT/usr/local/bin/pirate-qt"
chmod +x "$PKG_ROOT/usr/local/bin/pirate-qt"

for bin in pirate-tor pirate-i2pd pirate-networking; do
    if [ -f "$BIN_DIR/$bin" ]; then
        cp "$BIN_DIR/$bin" "$PKG_ROOT/usr/local/bin/$bin"
        chmod +x "$PKG_ROOT/usr/local/bin/$bin"
    fi
done

cp zcutil/deb/pirate.xpm "$PKG_ROOT/usr/share/pixmaps/"
cp zcutil/deb/desktopentry "$PKG_ROOT/usr/share/applications/pirate-qt.desktop"

# These scripts only ever target Linux, so the vendor/libc part of the
# triplet (e.g. "unknown-linux-gnu", "linux-gnu") is noise in a filename -
# just the architecture plus a plain "linux" suffix is enough to tell
# x86_64-unknown-linux-gnu and aarch64-linux-gnu apart.
PLATFORM="${HOST%%-*}-linux"

OUTPUT_DEB="$BIN_DIR/pirate-qt-${PLATFORM}-v${APP_VERSION}.deb"
rm -f "$OUTPUT_DEB"

# --root-owner-group (dpkg >= 1.19.0) avoids needing an actual root/fakeroot
# to get conventional root:root ownership inside the package; fall back to a
# plain build (owned by whichever user is running this script) on an older
# dpkg that doesn't recognize the flag.
if ! dpkg-deb --build --root-owner-group "$PKG_ROOT" "$OUTPUT_DEB" 2>/dev/null; then
    dpkg-deb --build "$PKG_ROOT" "$OUTPUT_DEB"
fi

echo "Wrote $OUTPUT_DEB"
