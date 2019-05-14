[![Build Status](https://travis-ci.org/KomodoPlatform/komodo.svg?branch=dev)](https://travis-ci.org/KomodoPlatform/komodo)
---
![Pirate Logo](https://i.ibb.co/F7Dgnxy/Pirate-Logo-Wordmark-Gold.png "PirateChain Logo")


## PirateChain

This is the official PirateChain sourcecode repository based on https://github.com/jl777/komodo.

## Development Resources

- PirateChain Website: [https://pirate.black](https://pirate.black/)
- Komodo Platform: [https://komodoplatform.com](https://komodoplatform.com/)
- Pirate Blockexplorer: [https://explorer.pirate.black](https://pirate.black/)
- Pirate Discord: [https://pirate.black/discord](https://pirate.black/discord)
- BTT ANN: [https://bitcointalk.org/index.php?topic=4979549.0](https://bitcointalk.org/index.php?topic=4979549.0/)
- Mail: [marketing@pirate.black](mailto:marketing@pirate.black)
- Support: [https://pirate.black/discord](https://pirate.black/discord)
- API references & Dev Documentation: [https://docs.komodoplatform.com](https://docs.komodoplatform.com/)
- Blog: [https://pirate.black/blog](https://pirate.black/blog/)
- Whitepaper: [PirateChain Whitepaper](https://pirate.black/whitepaper)

## Komodo Platform Technologies Integrated In PirateChain

- Delayed Proof of Work (dPoW) - Additional security layer and Komodos own consensus algorithm  
- zk-SNARKs - Komodo Platform's privacy technology for shielded transactions  


## Tech Specification
- Max Supply: 200 million ARRR
- Block Time: 60s
- Block Reward: 256 KMD
- Mining Algorithm: Equihash 200,9

## About this Project
PirateChain (ARRR) is a 100% private send cryptocurrency. It uses a privacy protocol that cannot be compromised by other users activity on the network. Most privacy coins are riddled with holes created by optional privacy. PirateChain uses zk-SNARKs to shield 100% of the peer to peer transactions on the blockchain making for highly anonymous and private transactions.

## Getting started

### Dependencies

```shell
#The following packages are needed:
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python python-zmq zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl
```

### Build Komodo

This software is based on zcash and considered experimental and is continously undergoing heavy development.

The dev branch is considered the bleeding edge codebase while the master-branch is considered tested (unit tests, runtime tests, functionality). At no point of time do the Komodo Platform developers take any responsbility for any damage out of the usage of this software. 
Komodo builds for all operating systems out of the same codebase. Follow the OS specific instructions from below.

#### Linux
```shell
git clone https://github.com/mrmlynch/pirate --branch dev --single-branch
cd pirate
./zcutil/fetch-params.sh
# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available
./zcutil/build.sh -j8
#This can take some time.
```

#### OSX
Ensure you have [brew](https://brew.sh) and the command line tools installed (comes automatically with XCode) and run:
```shell
brew update && brew install gcc@6
git clone https://github.com/mrmlynch/pirate --branch dev --single-branch
cd pirate
./zcutil/fetch-params.sh
# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available
./zcutil/build-mac.sh -j8
#This can take some time.
```

#### Windows
Use a debian cross-compilation setup with mingw for windows and run:
```shell
sudo apt-get install build-essential pkg-config libc6-dev m4 g++-multilib autoconf libtool ncurses-dev unzip git python python-zmq zlib1g-dev wget libcurl4-gnutls-dev bsdmainutils automake curl cmake mingw-w64
curl https://sh.rustup.rs -sSf | sh
source $HOME/.cargo/env
rustup target add x86_64-pc-windows-gnu
git clone https://github.com/mrmlynch/pirate --branch dev --single-branch
cd komodo
./zcutil/fetch-params.sh
# -j8 = using 8 threads for the compilation - replace 8 with number of threads you want to use; -j$(nproc) for all threads available
./zcutil/build-win.sh -j8
#This can take some time.
```
**pirate is experimental and a work-in-progress.** Use at your own risk.

To reset the PirateChain blockchain change into the *~/.komodo/PIRATE* data directory and delete the corresponding files by running `rm -rf blocks chainstate debug.log komodostate db.log` and restart daemon



**Pirate is based on Komodo which is unfinished and highly experimental.** Use at your own risk.

License
-------
For license information see the file [COPYING](COPYING).


Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
