#!/bin/bash

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

# Read the version ./configure already derived from configure.ac's
# _CLIENT_VERSION_* macros (e.g. "6.0.0-rc2"), rather than hand-maintaining
# a second copy of it here - same approach as build-deb.sh/build-zip.sh.
# -v/--version below can still override this.
APP_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)"
export APP_VERSION

# Accept the variables as command line arguments as well
POSITIONAL=()
while [[ $# -gt 0 ]]
do
key="$1"

case $key in
    -v|--version)
    APP_VERSION="$2"
    shift # past argument
    shift # past value
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

if [ -z "$APP_VERSION" ]; then
    echo "APP_VERSION is not set (could not read PACKAGE_VERSION from ./Makefile - did ./configure run first? or pass -v/--version explicitly)" >&2
    exit 1
fi

GPG_ARGS=(--batch)
if [ -n "${PIRATE_GPG_PASSPHRASE:-}" ]; then
  GPG_ARGS+=(--pinentry-mode loopback --passphrase "$PIRATE_GPG_PASSPHRASE")
fi

# Store the hash and signatures here
rm -rf release/signatures
mkdir -p release/signatures

cd artifacts/release

# Remove previous signatures/hashes
rm -f sha256sum-v$APP_VERSION.txt
rm -f signatures-v$APP_VERSION.tar.gz

# sha256sum the binaries
sha256sum *$APP_VERSION* > sha256sum-v$APP_VERSION.txt

for i in $( ls pirate*-v$APP_VERSION* sha256sum-v$APP_VERSION* ); do
  echo "Signing" $i
  gpg "${GPG_ARGS[@]}" --output ../../release/signatures/$i.sig --detach-sig $i
done

mv sha256sum-v$APP_VERSION.txt ../../release/signatures/
cp ../../zcutil/res/SIGNATURES_README ../../release/signatures/README

cd ../../release/signatures
#tar -czf signatures-v$APP_VERSION.tar.gz *
zip signatures-v$APP_VERSION.zip *
mv signatures-v$APP_VERSION.zip ../../artifacts/release
