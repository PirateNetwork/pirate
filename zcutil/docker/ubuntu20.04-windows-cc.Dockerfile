# Cross-compiles the Windows x86_64 CLI and QT artifacts inside a pinned
# ubuntu:20.04 userspace - see pirate_build_all.yml's windows-x86_64-cross
# job (--target=binaries) and ubuntu20.04-linux.Dockerfile's header comment
# for why pinning matters, why this builds the passed-in checkout instead of
# doing its own git clone (unlike
# docker-engine-builds/treasure_chest/ubuntu20.04_windows_cc/Dockerfile, a
# standalone "fetch master and build" convenience image), and why there's
# only one build here (build-qt-win.sh already produces both the CLI zip and
# the QT zip).
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
    mingw-w64 \
    zip \
    && rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/x86_64-w64-mingw32-gcc x86_64-w64-mingw32-gcc /usr/bin/x86_64-w64-mingw32-gcc-posix 90
RUN update-alternatives --install /usr/bin/x86_64-w64-mingw32-g++ x86_64-w64-mingw32-g++ /usr/bin/x86_64-w64-mingw32-g++-posix 90

WORKDIR /pirate
COPY . .

# Real releases (tag pushes) use configure.ac's version as-is. Everything
# else (e.g. auto-release on master) appends a short git sha so repeated
# builds off the same configure.ac version don't collide - see
# pirate_build_all.yml's "Set version suffix" steps and zcutil/build-zip.sh.
ARG APP_VERSION_SUFFIX=
ENV APP_VERSION_SUFFIX=${APP_VERSION_SUFFIX}

RUN ./zcutil/build-qt-win.sh -j$(nproc)
RUN BASE_VERSION="$(sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1)" && \
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
