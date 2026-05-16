| Target | CLI | QT |
| --- | --- | --- |
| Linux x86_64 | [![CLI Linux x86_64](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=CLI%20%28Linux%20x86_64%29&label=CLI)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) | [![QT Linux x86_64](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=QT%20%28Linux%20x86_64%29&label=QT)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) |
| Windows x86_64 cross | [![CLI Windows x86_64 cross](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=CLI%20%28Windows%20x86_64%20cross%29&label=CLI)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) | [![QT Windows x86_64 cross](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=QT%20%28Windows%20x86_64%20cross%29&label=QT)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) |
| Linux AArch64 cross | [![CLI Linux AArch64 cross](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=CLI%20%28Linux%20AArch64%20cross%29&label=CLI)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) | [![QT Linux AArch64 cross](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=QT%20%28Linux%20AArch64%20cross%29&label=QT)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) |
| macOS | [![CLI macOS](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=CLI%20%28macOS%29&label=CLI)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) | [![QT macOS](https://img.shields.io/github/check-runs/PirateNetwork/pirate/master?nameFilter=QT%20%28macOS%29&label=QT)](https://github.com/PirateNetwork/pirate/actions/workflows/pirate_build_all.yml) |

![Pirate Logo](https://i.ibb.co/F7Dgnxy/Pirate-Logo-Wordmark-Gold.png "Pirate Chain Logo")

## Pirate Chain

This is the official Pirate Chain sourcecode repository based on https://github.com/jl777/komodo.

## Development Resources

- Pirate Chain Website: [https://piratechain.com](https://piratechain.com/)
- Komodo Platform: [https://komodoplatform.com](https://komodoplatform.com/)
- Pirate Blockexplorer: [https://explorer.piratechain.com](https://piratechain.com/)
- Pirate Discord: [https://piratechain.com/discord](https://piratechain.com/discord)
- BTT ANN: [https://bitcointalk.org/index.php?topic=4979549.0](https://bitcointalk.org/index.php?topic=4979549.0/)
- Mail: [business@piratechain.com](mailto:business@piratechain.com)
- Support: [https://piratechain.com/discord](https://piratechain.com/discord)
- API references & Dev Documentation: [https://docs.piratechain.com](https://docs.piratechain.com/)
- Blog: [https://piratechain.com/blog](https://piratechain.com/blog/)
- Whitepaper: [Pirate Chain Whitepaper](https://piratechain.com/whitepaper)

## Komodo Platform Technologies Integrated In Pirate Chain

- Delayed Proof of Work (dPoW) - Additional security layer and Komodos own consensus algorithm  
- zk-SNARKs - Komodo Platform's privacy technology for shielded transactions  


## Tech Specification
- Max Supply: 200 million ARRR
- Block Time: 60s
- Block Reward: 256 ARRR
- Mining Algorithm: Equihash 200,9

## About this Project
Pirate Chain (ARRR) is a 100% private send cryptocurrency. It uses a privacy protocol that cannot be compromised by other users activity on the network. Most privacy coins are riddled with holes created by optional privacy. Pirate Chain uses zk-SNARKs to shield 100% of the peer to peer transactions on the blockchain making for highly anonymous and private transactions.

## Signed Releases
A Signature file is included in all releases designated as signed in the releases sections of this repository.

Verify the hashes specified in the signatures-vX.X.X.zip of each file by running:
```shell
sha256sum -c sha256sum-vX.Y.Z.txt
```

Verify the signatures specified in the signatures-vX.X.X.zip of each file by running:
```shell
1. First, import the public key (Available on GitHub at https://github.com/piratenetwork/pirate/blob/master/public_key.asc)
gpg --import public_key.asc

2. Verify signature
gpg --verify <filename.sig> <downloaded-filename-to-verify>
```

## Getting started
Build the code as described below. To see instructions on how to construct and send an offline transaction look
at README_offline_transaction_signing.md

A list of outstanding improvements is included in README_todo.md

## Build Pirate

The dev branch is considered the bleeding edge codebase while the master-branch is considered tested (unit tests, runtime tests, functionality). At no point of time do the Pirate developers take any responsibility for any damage out of the usage of this software.
Pirate builds for all operating systems out of the same codebase. Follow the OS specific instructions from below.

Clone the repository first:

```shell
git clone https://github.com/PirateNetwork/pirate --branch master
cd pirate
```

Build jobs use `-j` to set parallelism. On Linux hosts you can use `-j$(nproc)`. On macOS you can use `-j$(sysctl -n hw.ncpu)`.

`./zcutil/fetch-params.sh` is not required to compile. Run it before running the node or tests if the Sapling parameters are not already installed.

### Linux x86_64

Install dependencies on Ubuntu 24.04/22.04:

```shell
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  pkg-config \
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
  liblz4-dev \
  zip
```

For Qt GUI builds, also install:

```shell
sudo apt-get install -y \
  dpkg-dev \
  qtbase5-dev \
  qttools5-dev \
  qttools5-dev-tools
```

Build CLI binaries:

```shell
./zcutil/build.sh -j$(nproc)
```

Build the Qt GUI:

```shell
./zcutil/build-qt-linux.sh -j$(nproc)
```

Outputs:

- CLI: `src/pirated`, `src/pirate-cli`, `src/pirate-tx`
- Qt: `pirate-qt-linux`

### Windows x86_64 Cross-Compile

Use an Ubuntu/Debian cross-compilation host.

Install dependencies:

```shell
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  pkg-config \
  m4 \
  autoconf \
  libtool \
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
  liblz4-dev \
  zip \
  gcc-mingw-w64-x86-64-posix \
  g++-mingw-w64-x86-64-posix \
  binutils-mingw-w64-x86-64
```

If your distro only provides `x86_64-w64-mingw32-*` binaries, add the `x86_64-w64-mingw64-*` aliases used by the CLI build:

```shell
for tool in ar ranlib strip nm gcc g++ windres dlltool objcopy; do
  if ! command -v x86_64-w64-mingw64-$tool >/dev/null 2>&1; then
    if command -v x86_64-w64-mingw32-$tool >/dev/null 2>&1; then
      sudo ln -sf "$(command -v x86_64-w64-mingw32-$tool)" "/usr/bin/x86_64-w64-mingw64-$tool"
    fi
  fi
done
```

Build CLI binaries:

```shell
./zcutil/build-win.sh -j$(nproc)
```

Build the Qt GUI:

```shell
./zcutil/build-qt-win.sh -j$(nproc)
```

Outputs:

- CLI: `src/pirated.exe`, `src/pirate-cli.exe`, `src/pirate-tx.exe`
- Qt: `pirate-qt-win.exe`

### Linux AArch64 Cross-Compile

Use an Ubuntu/Debian x86_64 host to cross-compile Linux arm64/AArch64 binaries.

Install dependencies:

```shell
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  pkg-config \
  m4 \
  autoconf \
  libtool \
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
  liblz4-dev \
  zip \
  dpkg-dev \
  gcc-aarch64-linux-gnu \
  g++-aarch64-linux-gnu \
  binutils-aarch64-linux-gnu
```

Create the host alias expected by the dependency build:

```shell
ln -sfn aarch64-linux-gnu depends/aarch64-unknown-linux-gnu
```

Build CLI binaries:

```shell
./zcutil/build-arm.sh -j$(nproc)
```

Build the Qt GUI:

```shell
./zcutil/build-qt-arm.sh -j$(nproc)
```

Outputs:

- CLI: `src/pirated`, `src/pirate-cli`, `src/pirate-tx`
- Qt: `pirate-qt-arm`

### macOS

Install Xcode Command Line Tools, Homebrew, and Rust:

```shell
xcode-select --install
brew install autoconf automake libtool coreutils bison pkgconf python wget curl
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
. "$HOME/.cargo/env"
```

For Qt GUI builds, also install:

```shell
brew install qt@5 create-dmg
export PATH="$(brew --prefix qt@5)/bin:$PATH"
```

Build CLI binaries:

```shell
CONFIGURE_FLAGS=--disable-hardening ./zcutil/build-mac.sh -j$(sysctl -n hw.ncpu)
```

Build the Qt GUI and DMG:

```shell
./zcutil/build-qt-mac.sh -j$(sysctl -n hw.ncpu)
```

Outputs:

- CLI: `src/pirated`, `src/pirate-cli`, `src/pirate-tx`
- Qt: `pirate-qt-mac` and `pirate-qt-mac.dmg`

The macOS scripts build for the native architecture of the machine running the build: Apple Silicon builds arm64 binaries, and Intel builds x86_64 binaries. The current GitHub Actions `macos-latest` runner is Apple Silicon/arm64.

**Pirate is experimental and a work-in-progress.** Use at your own risk.

To run the Pirate GUI wallet:

**Linux**
`pirate-qt-linux`

**Linux AArch64**
`pirate-qt-arm`

**macOS**
`pirate-qt-mac`

**Windows**
`pirate-qt-win.exe`


To run the daemon for Pirate Chain:  
`pirated`
`pirated`, `pirate-cli`, and `pirate-tx` are located in the `src` directory after successfully building CLI targets.

To reset the Pirate Chain blockchain change into the *~/.komodo/PIRATE* data directory and delete the corresponding files by running `rm -rf blocks chainstate debug.log komodostate db.log` and restart daemon

To initiate a bootstrap download on the GUI wallet add bootstrap=1 to the PIRATE.conf file.


**Pirate is based on Komodo which is unfinished and highly experimental.** Use at your own risk.

License
-------
For license information see the file [COPYING](COPYING).


Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
