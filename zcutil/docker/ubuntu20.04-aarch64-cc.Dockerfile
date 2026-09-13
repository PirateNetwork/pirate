# Cross-compiles the Linux AArch64 CLI and QT artifacts inside a pinned
# ubuntu:20.04 userspace - see pirate_build_all.yml's linux-aarch64-cross
# job (--target=binaries) and ubuntu20.04-linux.Dockerfile's header comment
# for why pinning matters, why this builds the passed-in checkout instead of
# doing its own git clone (unlike
# docker-engine-builds/treasure_chest/ubuntu20.04_aarch64_cc/Dockerfile, a
# standalone "fetch master and build" convenience image), and why there's
# only one build here (build-qt-aarch64.sh already produces both the CLI zip
# and the QT deb/zip).
#
# The aarch64-unknown-linux-gnu Rust cross-linker/ar config already lives in
# .cargo/persistent-config (checked into this repo), so unlike the
# docker-engine-builds image, no extra .cargo/config needs to be written
# here.
FROM ubuntu:20.04 AS builder
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
    zip \
    && rm -rf /var/lib/apt/lists/*
RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
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

WORKDIR /pirate
COPY . .
RUN ln -sfn aarch64-linux-gnu depends/aarch64-unknown-linux-gnu

# Real releases (tag pushes) use CMakeLists.txt's version as-is. Everything
# else (e.g. auto-release on master) appends a short git sha so repeated
# builds off the same CMakeLists.txt version don't collide - see
# pirate_build_all.yml's "Set version suffix" steps and zcutil/build-zip.sh,
# zcutil/build-deb.sh.
ARG APP_VERSION_SUFFIX=
ENV APP_VERSION_SUFFIX=${APP_VERSION_SUFFIX}

RUN ./zcutil/build-qt-aarch64.sh -j$(nproc)
RUN . ./zcutil/build-common.sh && BASE_VERSION="$(pirate_version)" && \
    if [ -n "${APP_VERSION_SUFFIX:-}" ]; then \
        printf '%s-%s' "$BASE_VERSION" "$APP_VERSION_SUFFIX" > /tmp/VERSION; \
    else \
        printf '%s' "$BASE_VERSION" > /tmp/VERSION; \
    fi

# --target=binaries: build-qt-aarch64.sh already strips (with the correct
# aarch64-linux-gnu-strip), versions, and packages everything into
# artifacts/bin/*.deb and artifacts/bin/*.zip (pirate-qt-*, pirate-cli-*) -
# nothing left to do but export them, plus the resolved version string for
# pirate_build_all.yml's release-tagging job output.
FROM scratch AS binaries
COPY --from=builder /tmp/VERSION VERSION
COPY --from=builder /pirate/artifacts/bin/*.deb /pirate/artifacts/bin/*.zip ./
