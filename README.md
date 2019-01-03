![alt text](https://github.com/zerocurrencycoin/Zero/blob/master/art/zero%203d%20mountain.png?raw=true)

[ZERO](https://zerocurrency.io) - [Isfjorden:2.0.1](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1)

||FAST|| ||DECENTRALISED|| ||ANONYMOUS|| ||SECURE|| ||ASIC RESISTANT||  - LAUNCE DATE: 2017-02-19

GENESIS BLOCK - 19th Feb 2017 11:26:40 - 068cbb5db6bc11be5b93479ea4df41fa7e012e92ca8603c315f9b1a2202205c6

Download the latest version here - [ZERO - Latest Version - Isfjorden:2.0.1](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1)

------------------------------------------

❓ What is ZERO?
--------------

[ZERO](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1) is a revolutionary cryptocurrency and transaction platform based on Zcash.

[ZERO](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1) offers total payment confidentiality, while still maintaining a decentralised network using a public blockchain.

[ZERO](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1) combines Bitcoin’s security with Zcash’s anonymity and privacy.

[ZERO](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1) stands out from the competition as a fully working product that has already
implemented a set of special features not found in any other cryptocurrency.

Our main focus as a team and community is to remain as transparent as we can possibly be and to maintain an interactive relationship with everyone involved. We are fully open about the project, listening to all suggestions from investors, miners and supporters.

This software is the [ZERO](https://github.com/zerocurrencycoin/Zero/releases/tag/v2.0.1) node. It downloads and stores the entire history of ZERO's transactions, about 1.2GB at this point.
Depending on the speed of your computer and network connection, the synchronization process could take several hours.

------------------------------------------

![alt text](https://github.com/zerocurrencycoin/Zero/blob/master/art/zero-logo-shade-black_256x256.png)


💫 ZERO CORE FEATURES
-------------------

||ZERO COST||
--------------
Zero’s transaction fees are 0.0001 ZER, when the networks capacity is high. When Zero’s network is below full capacity the fee reduces to 0.


||NO PREMINE||
--------------
0% advantage for early adopters. No Pre-Mined Zero, therefore people that buy or mine today have no disadvantages.


||SECURE - ZERO BACKDOORS||
----------------------------
Zero implements zk-SNARKs, which is concluded to be a state of the art privacy technology, according to a consensus of a significant number of nonbiased parties.


||ASIC RESISTANT||
----------------------------
Zero is ASIC resistant due to increased difficulty settings in its equihash algorithm, keeping specialized hardware (ASICS) out of the way.


||SHIELDED TRANSACTIONS||
----------------------------
Zero has a feature called shielded transactions that ensures full anonymity when sending funds.


||VERY HIGH MINING PROFITABILITY||
------------------------------------------
Zero uses an alternative set of parameters taken from the equihash algorithm, this needs atleast 4GB/8GB of RAM. Zero is one of the most profitable cryptocurrency’s to mine.


||DECENTRALISED PAYMENTS||
----------------------------
Zero is founded on a decentralised platform, there are no borders. Zero facilitates lightning fast and anonymous money transfers worldwide, without any restrictions or interference from 3rd parties.


||INFLATION||
--------------
Zero has a small inflation that degrades over time. With a stable supply of 7776 Zero per day.

------------------------------------------

![alt text](https://github.com/zerocurrencycoin/Zero/blob/master/art/tech%20improv%20zero.jfif)


![alt text](https://github.com/zerocurrencycoin/Zero/blob/master/art/algo%20zer%20improv.jfif)

------------------------------------------


🔢 Development Fund Breakdown (Per Block)
------------------------------------------
Development Fund Details 0.81 ZER / Block (~7.5%)

0.324 ZER/Block (40%) Exchange Listing Funding.

0.243 ZER/Block (30%) Marketing Funding.

0.162 ZER/Block (20%) Dedicated to Radical New Capabilities.

0.081 ZER/Block (10%) Dedicated to Maintaining & Updating Blockchain.

Development Fund Total - 0.81 ZER / Block

Total ZER per day = 583.2


📄 White Paper - Extended
-----------------------

http://docdro.id/n9j4sJr


📣 Announcements
-----------------
https://bitcointalk.org/index.php?topic=1796036.0

https://bitcointalk.org/index.php?topic=3310714.0


🔒 Security Warnings
-----------------
See important security warnings on the
[Security Information page](https://z.cash/support/security/).


📒 Deprecation Policy
------------------
Disabledeprecation flag has been removed. Old nodes will automatically be shut down and must be upgraded upon reaching the deprecation block height, which will occur approximately 32 weeks (7/1/2019) from the release of v2.0.1.


🔧 Building
--------
Currently only Linux build is officially supported.  8GB RAM is recommended.

### Install packages (needs to be done only once)
```
sudo apt-get install \
      build-essential pkg-config libc6-dev m4 g++-multilib \
      autoconf libtool ncurses-dev unzip git python python-zmq \
      zlib1g-dev wget bsdmainutils automake cmake curl
```

### Obtain the ZERO software from GitHub
```
git clone https://github.com/zerocurrencycoin/zero.git
cd zero
git checkout master
```

### Download cryptographic keys (needs to be done only once)
```
./zcutil/fetch-params.sh
```

### Build the source code to produce binary executables:
```
./zcutil/build.sh -j$(nproc)
```
On a typical laptop -j3 works fine, while retaining some UI interactivity
```
./zcutil/build.sh -j3
```

### Create a ZERO configuration file
```
mkdir -p ~/.zero
echo "rpcuser=YOUR_USER" > ~/.zero/zero.conf
echo "rpcpassword=`head -c 32 /dev/urandom | base64`" >> ~/.zero/zero.conf
echo "rpcport=23800" >> ~/.zero/zero.conf
```

### Seeder Nodes
As of 26/11/2018 the following seeder nodes are up and run a recent Linux version:
```
addnode=34.236.37.74
addnode=178.128.42.10
addnode=86.26.174.151
addnode=46.101.66.152:33801
addnode=zseed1.cryptonode.cloud
addnode=zseed2.cryptonode.cloud
addnode=zeroseed.cryptoforge.cc
```

### Enable CPU mining (optional)
```
echo 'gen=1' >> ~/.zero/zero.conf
echo "genproclimit=1" >> ~/.zero/zero.conf
echo 'equihashsolver=tromp' >> ~/.zero/zero.conf
```

A sample of the current zero.conf
```
./contrib/zero.conf
```
A sample demonstrating a large number of command line options
```
./contrib/debian/examples/zero.conf
```

🔩 Running & Using ZERO
--------------------
After successfully building, the ZERO binaries are stored in `./src`. The two important binaries are `zerod` and `zero-cli`.

Your wallet will be created (on first zerod run) in: ~/.zero/wallet.zero
Please backup your wallet often and keep it safe.

The usage is currently the same as ZCash. For more information see the [ZCash User Guide](https://github.com/zcash/zcash/wiki/1.0-User-Guide#running-zcash).

📜 License
-------
For license information see the file [COPYING](COPYING).
