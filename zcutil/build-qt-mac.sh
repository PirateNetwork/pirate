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
ARTIFACTS_DIR="$mydir/artifacts"

make "$@" -C ./depends/ V=1

./autogen.sh

# Detect architecture for the Rust target.
ARCH=$(uname -m)

if command -v rustup >/dev/null 2>&1; then
    if [ "$ARCH" = "arm64" ]; then
        rustup target add aarch64-apple-darwin
    fi
    export RUSTC="$(rustup which rustc)"
    export CARGO="$(rustup which cargo)"
fi

CONFIG_SITE="$PREFIX/share/config.site" \
CXXFLAGS="-fwrapv -fno-strict-aliasing \
-Wno-deprecated-declarations -Wno-deprecated-builtins -Wno-enum-constexpr-conversion \
-Wno-unknown-warning-option -Wno-error=attributes -g" \
./configure --prefix="${PREFIX}" --disable-bip70 --with-gui=qt5 --enable-tests=no "$HARDENING_ARG" "$LCOV_ARG" "$DEBUGGING_ARG"

make "$@" NO_GTEST=0 STATIC=1

cp src/qt/pirate-qt "$mydir"/pirate-qt-mac
# Strip symbols to reduce release size.
strip -x "$mydir"/pirate-qt-mac

# `--prefix` above points at the depends tree so the build itself can find its
# dependencies; that's not where a human wants the finished binaries, so stage
# a real `make install` through a throwaway DESTDIR and re-root just the
# `$PREFIX` subtree into a flat, repo-local artifacts/ folder.
STAGING_DIR="$(mktemp -d)"
make install DESTDIR="$STAGING_DIR"
rm -rf "$ARTIFACTS_DIR"
mkdir -p "$ARTIFACTS_DIR"
cp -a "${STAGING_DIR}${PREFIX}/." "$ARTIFACTS_DIR/"
rm -rf "$STAGING_DIR"

# CXXFLAGS above always includes -g, so everything under src/ (including
# pirate-qt-mac, stripped separately above) is left with full debug info
# intact for local debugging. Strip only the artifacts/bin/ copies - the
# binaries a human or a downstream packaging step actually consumes - not
# the originals.
STRIP="$(sed -n 's/^STRIP *= *//p' Makefile | head -1)"
if [ -n "$STRIP" ]; then
    for f in "$ARTIFACTS_DIR"/bin/*; do
        if [ -f "$f" ]; then
            "$STRIP" "$f" 2>/dev/null || true
        fi
    done
fi

# makeReleaseMac.sh bundles daemon binaries by bare name out of $mydir (like
# pirate-qt-mac above); the embedded tor/i2pd daemons and their crash-safety
# watchdog only exist under artifacts/bin/ at this point, so pull them out to
# where it expects to find them, if this build produced them.
for bin in pirate-tor pirate-i2pd pirate-networking; do
    if [ -f "$ARTIFACTS_DIR/bin/$bin" ]; then
        cp "$ARTIFACTS_DIR/bin/$bin" "$mydir/$bin"
    fi
done

# Read the version ./configure already derived from configure.ac's
# _CLIENT_VERSION_* macros (e.g. "6.0.0-rc2"), rather than hand-maintaining
# a second copy of it here - same approach as build-deb.sh/build-zip.sh.
# Exported so makeReleaseMac.sh can fill it into zcutil/res/Info.plist's
# RELEASE_VERSION placeholders (CFBundleShortVersionString/CFBundleVersion).
APP_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)"
# CI sets this on non-tag builds (e.g. a short git sha) so repeated builds
# off the same configure.ac version don't collide - see pirate_build_all.yml.
if [ -n "${APP_VERSION_SUFFIX:-}" ]; then
    APP_VERSION="${APP_VERSION}-${APP_VERSION_SUFFIX}"
fi
export APP_VERSION

#Package as App bundle in a dmg
./makeReleaseMac.sh

# makeReleaseMac.sh always writes an unversioned pirate-qt-mac.dmg in the
# repo root; give it the same <name>-<arch>-<os>-v<version> naming and
# artifacts/bin/ location as every other build-qt-*.sh's packaging output.
if [ -n "$APP_VERSION" ] && [ -f "$mydir/pirate-qt-mac.dmg" ]; then
    case "$TRIPLET" in
        *-apple-darwin*) PLATFORM="${TRIPLET%%-*}-macos" ;;
        *) PLATFORM="$TRIPLET" ;;
    esac
    mkdir -p "$ARTIFACTS_DIR/bin"
    mv "$mydir/pirate-qt-mac.dmg" "$ARTIFACTS_DIR/bin/pirate-qt-${PLATFORM}-v${APP_VERSION}.dmg"
fi

# The Qt package is the .dmg app bundle above, not a flat zip of raw
# binaries - only build the CLI bundle here.
./zcutil/build-zip.sh "$TRIPLET" cli
