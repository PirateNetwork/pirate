# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.
#
# Shared CMake build driver, sourced (not executed) by zcutil/build*.sh.
# Replaces the ./autogen.sh && ./configure && make sequence every one of
# those scripts used to run individually.

# pirate_depends HOST [BUILD] [DEPENDS_MAKEARGS...]
# Builds depends/ for HOST, producing depends/$HOST/toolchain.cmake.
pirate_depends() {
    local host="$1"; shift
    local build="${1:-}"; [ $# -gt 0 ] && shift
    if [ -n "$build" ]; then
        HOST="$host" BUILD="$build" "${MAKE:-make}" "$@" -C ./depends/ V=1
    else
        HOST="$host" "${MAKE:-make}" "$@" -C ./depends/ V=1
    fi
}

# pirate_cmake_configure HOST BUILD_DIR CMAKE_BUILD_TYPE WITH_GUI [extra -D args...]
pirate_cmake_configure() {
    local host="$1" build_dir="$2" build_type="$3" with_gui="$4"; shift 4
    cmake -S . -B "$build_dir" \
        --toolchain "depends/$host/toolchain.cmake" \
        -DCMAKE_BUILD_TYPE="$build_type" \
        -DBUILD_GUI="$with_gui" \
        "$@"
}

# pirate_cmake_build BUILD_DIR [MAKEARGS...]
pirate_cmake_build() {
    local build_dir="$1"; shift
    cmake --build "$build_dir" "$@"
}

# pirate_cmake_install BUILD_DIR STAGING_DIR
pirate_cmake_install() {
    local build_dir="$1" staging_dir="$2"
    DESTDIR="$staging_dir" cmake --install "$build_dir"
}

# Prints "MAJOR.MINOR.REVISION" (the client version), read directly from the
# root CMakeLists.txt -- the CMake-build equivalent of the old
# `sed -n 's/^PACKAGE_VERSION *= *//p' Makefile` pattern (there is no
# generated top-level Makefile to read that from any more).
pirate_version() {
    local major minor revision
    major="$(sed -n 's/^set(CLIENT_VERSION_MAJOR \([0-9]*\))$/\1/p' CMakeLists.txt | head -1)"
    minor="$(sed -n 's/^set(CLIENT_VERSION_MINOR \([0-9]*\))$/\1/p' CMakeLists.txt | head -1)"
    revision="$(sed -n 's/^set(CLIENT_VERSION_REVISION \([0-9]*\))$/\1/p' CMakeLists.txt | head -1)"
    if [ -z "$major" ] || [ -z "$minor" ] || [ -z "$revision" ]; then
        return 1
    fi
    echo "$major.$minor.$revision"
}

# Prints the strip tool this HOST's depends toolchain.cmake sets, the
# equivalent of the old `sed -n 's/^STRIP *= *//p' Makefile` pattern (CMake
# doesn't cache CMAKE_STRIP the way autotools' generated Makefile did, so
# read it from the source of truth instead: the generated toolchain file).
pirate_strip_tool() {
    sed -n 's/^set(CMAKE_STRIP "\(.*\)")$/\1/p' "depends/$1/toolchain.cmake" 2>/dev/null | head -1
}
