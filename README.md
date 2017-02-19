ZERO 1.0.6
===========

What is ZERO?
--------------

[ZERO](https://github.com/zerocurrency/zero) is is a fork of Zcash that is a fork of Bitcoin.

This software is the ZERO client. It downloads and stores the entire history
of ZERO transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Security Warnings
-----------------

See important security warnings in
[doc/security-warnings.md](doc/security-warnings.md).

**ZERO is unfinished and highly experimental.** Use at your own risk.

Building
--------

Currently only Linux is officially supported.
Requires at least 8GB RAM.


sudo apt-get install \

      build-essential pkg-config libc6-dev m4 g++-multilib \

      autoconf libtool ncurses-dev unzip git python \

      zlib1g-dev wget bsdmainutils automake


git clone https://github.com/zerocurrency/zero.git

cd zero

git checkout master

./zcutil/fetch-params.sh

./zcutil/build.sh -j$(nproc)

mkdir -p ~/.zero

echo "rpcuser=username" > ~/.zero/zero.conf

echo "rpcpassword=`head -c 32 /dev/urandom | base64`" >> ~/.zero/zero.conf

echo "addnode=35.164.216.74" >> ~/.zero/zero.conf

echo "addnode=35.165.120.254" >> ~/.zero/zero.conf

echo "rpcport=23800" >> ~/.zero/zero.conf

echo 'gen=1' >> ~/.zero/zero.conf

echo "genproclimit=$(nproc)" >> ~/.zero/zero.conf

echo 'equihashsolver=tromp' >> ~/.zero/zero.conf




