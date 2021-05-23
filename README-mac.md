
You will need Apple's Xcode (at least version 7, preferably 8.x) and the Xcode Command Line Tools:

https://itunes.apple.com/us/app/xcode/id497799835?mt=12

And Homebrew:

http://brew.sh/

Use the brewfile to install the necessary packages:

```shell
brew bundle
```

or 

```shell
brew tap discoteq/discoteq; brew install flock autoconf autogen automake gcc@8 binutils protobuf coreutils wget
```

Get all that installed, then run:

```shell
git clone https://github.com/PirateNetwork/pirate.git
cd pirate
./zcutil/build-mac.sh
./zcutil/fetch-params.sh
```

To build a distributable version of pirate then run the makeReleaseMac.sh script after building. This will fix the dependency references and move the komodod and komodo-cli binaries to the kmd/mac/pirate-cli directory along with the 6 libraries required for it to work properly.

When you are done building, you need to create `PIRATE.conf` the Mac way. 

```shell
mkdir ~/Library/Application\ Support/Komodo/PIRATE
touch ~/Library/Application\ Support/Komodo/PIRATE/PIRATE.conf
nano ~/Library/Application\ Support/Komodo/PIRATE/PIRATE.conf
```

Add the following lines to the PIRATE.conf file:

```shell
rpcuser=dontuseweakusernameoryougetrobbed
rpcpassword=dontuseweakpasswordoryougetrobbed
txindex=1
addnode=144.202.71.190
```

Happy Building
