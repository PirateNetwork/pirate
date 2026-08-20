# Builds the native Linux x86_64 CLI and QT artifacts inside a pinned
# ubuntu:20.04 userspace, so binaries link against that release's glibc
# baseline instead of whatever glibc happens to ship on the GitHub-hosted
# ubuntu-latest runner image this week (see pirate_build_all.yml's
# linux-x86_64 job, which uses --target=binaries on this file).
#
# One build only: build-qt-linux.sh already produces both artifacts -
# artifacts/bin/pirate-cli-*.zip (via its own internal
# `build-zip.sh ... both` call) and artifacts/bin/pirate-qt-*.deb/.zip -
# there is no separate CLI-only build here (an earlier version of this file
# ran build.sh AND build-qt-linux.sh as two independent full compiles,
# discarding the CLI zip build-qt-linux.sh already makes for free).
#
# Unlike docker-engine-builds/treasure_chest/ubuntu20.04/Dockerfile (a
# standalone "fetch master and build" convenience image), this Dockerfile
# builds whatever source is in the build context - pass the actual checkout
# (e.g. `docker build -f zcutil/docker/ubuntu20.04-linux.Dockerfile .` from
# the repo root) so CI builds the commit under test, not master.
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

WORKDIR /pirate
COPY . .

# Real releases (tag pushes) use configure.ac's version as-is. Everything
# else (e.g. auto-release on master) appends a short git sha so repeated
# builds off the same configure.ac version don't collide - see
# pirate_build_all.yml's "Set version suffix" steps and zcutil/build-zip.sh,
# zcutil/build-deb.sh.
ARG APP_VERSION_SUFFIX=
ENV APP_VERSION_SUFFIX=${APP_VERSION_SUFFIX}

RUN ./zcutil/build-qt-linux.sh -j$(nproc)

# pirate-gtest (src/Makefile.gtest.include) is the only suite that needs to
# run, and it's already built as a normal bin_PROGRAMS target by the
# build-qt-linux.sh RUN above - so run the binary directly instead of `make
# check` (even scoped via TESTS=pirate-gtest, that still recurses into
# unrelated SUBDIRS like cryptoconditions/src/include/secp256k1, whose own
# check-TESTS then fails trying to build a pirate-gtest.log/.trs that has
# nothing to do with them - the TESTS override isn't subdir-scoped). It
# needs the Sapling/Sprout trusted-setup params for its joinsplit tests (see
# src/gtest/main.cpp). Runs here, inside the same pinned ubuntu:20.04
# userspace that built it, since the scratch export below can't hand the
# build tree back to the host. A test failure fails this RUN and thus the
# whole docker build.
RUN ./zcutil/fetch-params.sh
RUN ./src/pirate-gtest

RUN sed -n 's/^PACKAGE_VERSION *= *//p' Makefile | head -1 > /tmp/VERSION

# --target=binaries: build-qt-linux.sh already strips, versions, and
# packages everything into artifacts/bin/*.deb and artifacts/bin/*.zip
# (pirate-qt-*, pirate-cli-*) - nothing left to do but export them, plus the
# resolved version string for pirate_build_all.yml's release-tagging job
# output.
FROM scratch AS binaries
COPY --from=builder /tmp/VERSION VERSION
COPY --from=builder /pirate/artifacts/bin/*.deb /pirate/artifacts/bin/*.zip ./
