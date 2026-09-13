# Cross-compiles the Windows x86_64 CLI and QT artifacts. See
# pirate_build_all.yml's windows-x86_64-cross job (--target=binaries) and
# ubuntu20.04-linux.Dockerfile's header comment for why this builds the
# passed-in checkout instead of doing its own git clone (unlike
# docker-engine-builds/treasure_chest/ubuntu20.04_windows_cc/Dockerfile, a
# standalone "fetch master and build" convenience image), and why there's
# only one build here (build-qt-win.sh already produces both the CLI zip and
# the QT zip).
# Unlike the Linux and AArch64 images, this one is NOT pinned to 20.04. That
# pin exists to fix the glibc baseline the released *Linux* binaries link
# against; everything this image emits is a Windows PE binary built against
# the mingw runtime, so the host's glibc is irrelevant here.
#
# 20.04's mingw is GCC 9.3, which ICEs building Qt 6.8.4's bundled PCRE2:
#   pcre2_compile.c: in function 'pcre2_compile_16':
#   internal compiler error: in i386_pe_seh_unwind_emit, at config/i386/winnt.c:1258
# 24.04 ships mingw GCC 13.2.
FROM ubuntu:24.04 AS builder
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get upgrade -y && apt-get install -y \
    build-essential \
    pkg-config \
    m4 \
    g++-multilib \
    autoconf \
    automake \
    libtool \
    libncurses-dev \
    unzip \
    git \
    python3 \
    python3-zmq \
    zlib1g-dev \
    wget \
    libcurl4-gnutls-dev \
    bsdmainutils \
    curl \
    libsodium-dev \
    bison \
    mingw-w64 \
    zip \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 20.04's apt cmake is 3.16.3. Qt 6.8 requires 3.21+ (qtbase's
# QtCMakeVersionHelpers.cmake hard-errors below that) and this project's
# top-level CMakeLists.txt requires 3.22+, so install an upstream build.
# The base image stays on 20.04 deliberately: it pins the glibc baseline the
# released binaries link against.
ARG CMAKE_VERSION=3.27.7
ARG CMAKE_SHA256=a8c92ecb139bcc7a1f92a8108179bd1d021bdb158a5ee759cba6d60010b83ae9
RUN wget -q "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && echo "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" | sha256sum -c - \
    && tar -xzf "cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" -C /opt \
    && rm "cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && ln -sf "/opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/cmake" /usr/local/bin/cmake \
    && ln -sf "/opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/ctest" /usr/local/bin/ctest \
    && cmake --version

RUN update-alternatives --install /usr/bin/x86_64-w64-mingw32-gcc x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix 90
RUN update-alternatives --install /usr/bin/x86_64-w64-mingw32-g++ x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix 90

WORKDIR /pirate
COPY . .

# Real releases (tag pushes) use CMakeLists.txt's version as-is. Everything
# else (e.g. auto-release on master) appends a short git sha so repeated
# builds off the same CMakeLists.txt version don't collide - see
# pirate_build_all.yml's "Set version suffix" steps and zcutil/build-zip.sh.
ARG APP_VERSION_SUFFIX=
ENV APP_VERSION_SUFFIX=${APP_VERSION_SUFFIX}

RUN ./zcutil/build-qt-win.sh -j$(nproc)
RUN . ./zcutil/build-common.sh && BASE_VERSION="$(pirate_version)" && \
    if [ -n "${APP_VERSION_SUFFIX:-}" ]; then \
        printf '%s-%s' "$BASE_VERSION" "$APP_VERSION_SUFFIX" > /tmp/VERSION; \
    else \
        printf '%s' "$BASE_VERSION" > /tmp/VERSION; \
    fi

# --target=binaries: build-qt-win.sh already strips, versions, and packages
# everything into artifacts/bin/*.zip (pirate-qt-*, pirate-cli-*) - no .deb
# for a Windows target, nothing left to do but export them, plus the
# resolved version string for pirate_build_all.yml's release-tagging job
# output.
FROM scratch AS binaries
COPY --from=builder /tmp/VERSION VERSION
COPY --from=builder /pirate/artifacts/bin/*.zip ./
