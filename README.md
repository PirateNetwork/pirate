![Pirate cli build - Ubuntu 18.04](https://github.com/PirateNetwork/pirate/workflows/Pirate%20cli%20build%20-%20Ubuntu%2018.04/badge.svg)\
![Pirate cli build - Ubuntu 20.04](https://github.com/PirateNetwork/pirate/workflows/Pirate%20cli%20build%20-%20Ubuntu%2020.04/badge.svg)\
![Pirate cli build - Windows cross compile 18.04](https://github.com/PirateNetwork/pirate/workflows/Pirate%20cli%20build%20-%20Windows%20cross%20compile%2018.04/badge.svg)\
![Pirate cli build - Windows cross compile 20.04](https://github.com/PirateNetwork/pirate/workflows/Pirate%20cli%20build%20-%20Windows%20cross%20compile%2020.04/badge.svg)\
![Pirate cli build - MacOS 10.15 Catalina](https://github.com/PirateNetwork/pirate/workflows/Pirate%20cli%20build%20-%20MacOS%2010.15%20Catalina/badge.svg)\
![Pirate Logo](https://i.ibb.co/F7Dgnxy/Pirate-Logo-Wordmark-Gold.png "Pirate Chain Logo")


## Pirate Chain - offline

This is the official Pirate Chain sourcecode repository based on https://github.com/jl777/komodo.

## Development Resources

- Pirate Chain Website: [https://pirate.black](https://pirate.black/)
- Komodo Platform: [https://komodoplatform.com](https://komodoplatform.com/)
- Pirate Blockexplorer: [https://explorer.pirate.black](https://pirate.black/)
- Pirate Discord: [https://pirate.black/discord](https://pirate.black/discord)
- BTT ANN: [https://bitcointalk.org/index.php?topic=4979549.0](https://bitcointalk.org/index.php?topic=4979549.0/)
- Mail: [marketing@pirate.black](mailto:marketing@pirate.black)
- Support: [https://pirate.black/discord](https://pirate.black/discord)
- API references & Dev Documentation: [https://docs.komodoplatform.com](https://docs.komodoplatform.com/)
- Blog: [https://pirate.black/blog](https://pirate.black/blog/)
- Whitepaper: [Pirate Chain Whitepaper](https://pirate.black/whitepaper)

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

## Getting started
Build the code as described below. To see instructions on how to construct and send an offline transaction look
at README_offline_transaction_signing.md

A list of outstanding improvements is included in README_todo.md

### Dependencies

```shell
#The following packages are needed:
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool libncurses-dev unzip git python zlib1g-dev wget bsdmainutils automake libboost-all-dev libssl-dev libprotobuf-dev protobuf-compiler libqrencode-dev libdb++-dev ntp ntpdate nano software-properties-common curl libevent-dev libcurl4-gnutls-dev cmake clang libsodium-dev -y
```

### Build Pirate

This software is based on zcash and considered experimental and is continuously undergoing heavy development.

The dev branch is considered the bleeding edge codebase while the master-branch is considered tested (unit tests, runtime tests, functionality). At no point of time do the Pirate developers take any responsibility for any damage out of the usage of this software.
Pirate builds for all operating systems out of the same codebase. Follow the OS specific instructions from below.

#### Linux
```shell
git clone https://github.com/PirateNetwork/pirate --branch master
cd pirate
# This step is not required for when using the Qt GUI
./zcutil/fetch-params.sh

# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available

#For CLI binaries
./zcutil/build.sh -j8

#For qt GUI binaries
./zcutil/build-qt-linux.sh -j8.
```

#### OSX
Ensure you have [brew](https://brew.sh) and the command line tools installed (comes automatically with XCode) and run:
```shell
brew update
brew upgrade
brew tap discoteq/discoteq; brew install flock
brew install autoconf autogen automake gcc@8 binutilsprotobuf coreutils wget python3
git clone https://github.com/PirateNetwork/pirate --branch master
cd pirate
# This step is not required for when using the Qt GUI
./zcutil/fetch-params.sh

# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available

#For CLI binaries
./zcutil/build-mac.sh -j8

#For qt GUI binaries
./zcutil/build-qt-mac.sh -j8
```

#### Windows
Use a debian cross-compilation setup with mingw for windows and run:
```shell
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python python-zmq zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl cmake mingw-w64
curl https://sh.rustup.rs -sSf | sh
source $HOME/.cargo/env
rustup target add x86_64-pc-windows-gnu

sudo update-alternatives --config x86_64-w64-mingw32-gcc
# (configure to use POSIX variant)
sudo update-alternatives --config x86_64-w64-mingw32-g++
# (configure to use POSIX variant)

git clone https://github.com/PirateNetwork/pirate --branch master
cd pirate
# This step is not required for when using the Qt GUI
./zcutil/fetch-params.sh

# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available

#For CLI binaries
./zcutil/build-win.sh -j8

#For qt GUI binaries
./zcutil/build-qt-win.sh -j8
```
**Pirate is experimental and a work-in-progress.** Use at your own risk.

To run the Pirate GUI wallet:

**Linux**
`pirate-qt-linux`

**OSX**
`pirate-qt-mac`

**Windows**
`pirate-qt-win.exe`


To run the daemon for Pirate Chain:  
`pirated`
both pirated and pirate-cli are located in the src directory after successfully building  

To reset the Pirate Chain blockchain change into the *~/.komodo/PIRATE* data directory and delete the corresponding files by running `rm -rf blocks chainstate debug.log komodostate db.log` and restart daemon

To initiate a bootstrap download on the GUI wallet add bootstrap=1 to the PIRATE.conf file.


**Pirate is based on Komodo which is unfinished and highly experimental.** Use at your own risk.

License
-------
For license information see the file [COPYING](COPYING).


Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
