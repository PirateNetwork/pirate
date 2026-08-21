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
    cmake \
    m4 \
    g++-multilib \
    autoconf \
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

WORKDIR /pirate
COPY . .
RUN ln -sfn aarch64-linux-gnu depends/aarch64-unknown-linux-gnu

# Real releases (tag pushes) use configure.ac's version as-is. Everything
# else (e.g. auto-release on master) appends a short git sha so repeated
# builds off the same configure.ac version don't collide - see
# pirate_build_all.yml's "Set version suffix" steps and zcutil/build-zip.sh,
# zcutil/build-deb.sh.
ARG APP_VERSION_SUFFIX=
ENV APP_VERSION_SUFFIX=${APP_VERSION_SUFFIX}

RUN ./zcutil/build-qt-aarch64.sh -j$(nproc)
RUN BASE_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)" && \
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
