ZERO 1.0.12
=============

What is ZERO?
--------------

[ZERO](https://github.com/tearodactyl/zero) is is a fork of Zcash that in turn is a fork of Bitcoin.

This software is the ZERO client. It downloads and stores the entire history
of ZERO transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Announcement
-----------------
Launch date: 2017-02-19
https://bitcointalk.org/index.php?topic=1796036.0


Security Warnings
-----------------

See important security warnings in
[doc/security-warnings.md](doc/security-warnings.md).

**ZERO is unfinished and highly experimental.** Use at your own risk.

Deprecation Policy
------------------

This release is considered deprecated 16 weeks after the release day. There
is an automatic deprecation shutdown feature which will halt the node some
time after this 16 week time period. The automatic feature is based on block
height and can be explicitly disabled.

Building
--------

Currently only Linux is officially supported.
Requires at least 8GB RAM.

### Install packages (needs to be done once only at start)
```
sudo apt-get install \
      build-essential pkg-config libc6-dev m4 g++-multilib \
      autoconf libtool ncurses-dev unzip git python python-zmq \
      zlib1g-dev wget bsdmainutils automake cmake
```

### Obtain the ZERO software from GitHub
```
git clone https://github.com/backendmaster/zero.git
cd zero
git checkout master
```

### Download cryptographic keys (needs to be done once only)
```
./zcutil/fetch-params.sh
```

### Build the source code to produce binary executables:
```
./zcutil/build.sh --disable-rust -j$(nproc)
```

### Create a ZERO configuration file
```
mkdir -p ~/.zero
echo "rpcuser=username" > ~/.zero/zero.conf
echo "rpcpassword=`head -c 32 /dev/urandom | base64`" >> ~/.zero/zero.conf
echo "rpcport=23800" >> ~/.zero/zero.conf
```

### Seeder Nodes
As of 07/12/2017 the following seeder nodes work:
```
addnode=zeropool.cloud:23801
addnode=139.162.188.122:23801
addnode=188.166.2.55:23801
addnode=94.176.235.178:23801
addnode=2a01:4f8:a0:8298::2:23801
addnode=2a02:168:5829:0:b486:978b:2017:dd2:23801
addnode=213.239.212.246:23801
addnode=213.32.78.132:23801
addnode=64.237.50.236:23801
addnode=139.162.188.122:23801
```

Check the thread for seeder node updates.
The way you add seeder nodes to the config file:
```
echo "addnode=zeropool.cloud" >> ~/.zero/zero.conf
```

### Enable CPU mining (optional)
```
echo 'gen=1' >> ~/.zero/zero.conf
echo "genproclimit=1" >> ~/.zero/zero.conf
echo 'equihashsolver=tromp' >> ~/.zero/zero.conf
```

When mining you're helping to strengthen the network and contributing to a social good.
GPU mining is now possible, please see the announcement thread for more details.


Running & Using ZERO
--------------------

After all the building steps you will have ZERO ready for use in folder `zero/src`. The two important binary executables are `zcashd` and `zcash-cli`.

Your wallet will be created (on first zcashd start) here: ~/.zero/wallet.zero
Please backup your wallet periodically and keep it safe.

The usage is currently the same as for ZCash. For more information see the [ZCash User Guide](https://github.com/zcash/zcash/wiki/1.0-User-Guide#running-zcash).


Donations
--------------------
Donations for running nodes and for development are welcomed here:
t1gUHkWqcC9ruk6iGkeDKnxtPAsrgm8AGVt

