# Building Pirate on macOS

Install Xcode Command Line Tools, Homebrew, and Rust:

```shell
xcode-select --install
brew install autoconf automake libtool coreutils bison pkgconf python wget curl
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
. "$HOME/.cargo/env"
```

Clone the repository:

```shell
git clone https://github.com/PirateNetwork/pirate.git
cd pirate
```

Build CLI binaries:

```shell
CONFIGURE_FLAGS=--disable-hardening ./zcutil/build-mac.sh -j$(sysctl -n hw.ncpu)
```

For the Qt GUI and DMG, also install Qt 5 and `create-dmg`:

```shell
brew install qt@5 create-dmg
export PATH="$(brew --prefix qt@5)/bin:$PATH"
./zcutil/build-qt-mac.sh -j$(sysctl -n hw.ncpu)
```

Build outputs:

- CLI: `src/pirated`, `src/pirate-cli`, `src/pirate-tx`
- Qt: `pirate-qt-mac` and `pirate-qt-mac.dmg`

The macOS scripts build for the native architecture of the machine running the build. Apple Silicon builds arm64 binaries, and Intel builds x86_64 binaries.

Run this before starting the node if the Sapling parameters are not already installed:

```shell
./zcutil/fetch-params.sh
```

Create `PIRATE.conf` in the macOS data directory:

```shell
mkdir -p ~/Library/Application\ Support/Komodo/PIRATE
touch ~/Library/Application\ Support/Komodo/PIRATE/PIRATE.conf
nano ~/Library/Application\ Support/Komodo/PIRATE/PIRATE.conf
```

Example `PIRATE.conf`:

```shell
rpcuser=dontuseweakusernameoryougetrobbed
rpcpassword=dontuseweakpasswordoryougetrobbed
txindex=1
addnode=144.202.71.190
```
