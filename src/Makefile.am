DIST_SUBDIRS = secp256k1 univalue
SUBDIRS = cryptoconditions
AM_LDFLAGS = $(PTHREAD_CFLAGS) $(LIBTOOL_LDFLAGS) $(SAN_LDFLAGS) $(HARDENED_LDFLAGS)
AM_CXXFLAGS = $(SAN_CXXFLAGS) $(HARDENED_CXXFLAGS) $(ERROR_CXXFLAGS)
AM_CPPFLAGS = $(HARDENED_CPPFLAGS)
EXTRA_LIBRARIES =

if ARCH_ARM
PLATFORM_VARIANT = armv8.1-a+crypto
else
PLATFORM_VARIANT = x86-64
endif

BITCOIN_CONFIG_INCLUDES=-I$(builddir)/config
BITCOIN_INCLUDES=-I$(builddir) -I$(builddir)/obj $(BDB_CPPFLAGS) $(BOOST_CPPFLAGS) $(LEVELDB_CPPFLAGS) $(CRYPTO_CFLAGS) $(SSL_CFLAGS)

BITCOIN_CONFIG_INCLUDES += -I$(srcdir)/rust/include
BITCOIN_CONFIG_INCLUDES += -I$(srcdir)/rust/gen/include
BITCOIN_INCLUDES += -I$(srcdir)/rust/include
BITCOIN_INCLUDES += -I$(srcdir)/rust/gen/include
BITCOIN_INCLUDES += -I$(srcdir)/secp256k1/include
BITCOIN_INCLUDES += -I$(srcdir)/cc/includes
BITCOIN_INCLUDES += -I$(srcdir)/cryptoconditions/include
BITCOIN_INCLUDES += -I$(srcdir)/cryptoconditions/src
BITCOIN_INCLUDES += -I$(srcdir)/cryptoconditions/src/asn
BITCOIN_INCLUDES += -I$(srcdir)/snark
BITCOIN_INCLUDES += -I$(srcdir)/snark/libsnark
BITCOIN_INCLUDES += -I$(srcdir)/univalue/include

# if TARGET_WINDOWS
# LIBBITCOIN_SERVER=libbitcoin_server.a -lcurl -larchive
# endif
# if TARGET_DARWIN
# LIBBITCOIN_SERVER=libbitcoin_server.a -lcurl -larchive
# endif
# if TARGET_LINUX
# LIBBITCOIN_SERVER=libbitcoin_server.a -lcurl -larchive
# endif
# if ARCH_ARM
LIBBITCOIN_SERVER=libbitcoin_server.a -lcurl -larchive
# endif

LIBBITCOIN_WALLET=libbitcoin_wallet.a
LIBBITCOIN_COMMON=libbitcoin_common.a
LIBBITCOIN_CLI=libbitcoin_cli.a
LIBBITCOIN_UTIL=libbitcoin_util.a
LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
LIBBITCOINQT=qt/libkomodoqt.a
LIBRUSTZCASH=$(top_builddir)/target/$(RUST_TARGET)/release/librustzcash.la
LIBSECP256K1=secp256k1/libsecp256k1.la
LIBCRYPTOCONDITIONS=cryptoconditions/libcryptoconditions_core.a
LIBSNARK=snark/libsnark.a
LIBUNIVALUE=univalue/libunivalue.la
LIBCC=libcc.a
LIBZCASH=libzcash.a
LIBTLS=libtls.a
LIBCJSON=libcjson.a
LIBCXXBRIDGE=libcxxbridge.a

if ENABLE_ZMQ
LIBBITCOIN_ZMQ=libbitcoin_zmq.a
endif
if ENABLE_PROTON
LIBBITCOIN_PROTON=libbitcoin_proton.a
endif
if BUILD_BITCOIN_LIBS
LIBZCASH_CONSENSUS=libzcashconsensus.la
endif
# if ENABLE_WALLET
# LIBBITCOIN_WALLET=libbitcoin_wallet.a
# endif

CXXBRIDGE_RS = \
  rust/src/sapling/spec.rs \
  rust/src/sapling/zip32.rs \
  rust/src/streams.rs \
  rust/src/bridge.rs

CXXBRIDGE_H = \
  rust/gen/include/rust/sapling/spec.h \
  rust/gen/include/rust/sapling/zip32.h \
  rust/gen/include/rust/streams.h \
  rust/gen/include/rust/bridge.h

CXXBRIDGE_CPP = \
  rust/gen/src/sapling/spec.cpp \
  rust/gen/src/sapling/zip32.cpp \
  rust/gen/src/streams.cpp \
  rust/gen/src/bridge.cpp

# We add a rust/cxx.h include to indicate that we provide this (via the rustcxx depends
# package), so that cxxbridge doesn't include it within the generated headers and code.
CXXBRIDGE_OPTS = -i rust/cxx.h

$(CXXBRIDGE_RS): ;
$(CXXBRIDGE_H) $(CXXBRIDGE_CPP): $(CXXBRIDGE_RS)
	@$(MKDIR_P) $(@D)
	$(AM_V_GEN)$(CXXBRIDGE) $(CXXBRIDGE_OPTS) $(basename $(patsubst rust/gen/include/rust/%,rust/src/%,$(patsubst rust/gen/src/%,rust/src/%,$@))).rs -o $@

# We pass through CC etc. flags so they are available to Rust dependencies that internally
# compile C or C++ code with the `cc` crate.
#
# We depend on the secp256k1 crate for some logic on the Rust side of the FFI. This crate
# is a wrapper around libsecp256k1, which we already vendor in our code; the crate vendors
# its own copy with non-colliding symbols. To ensure that we only use a single version of
# libsecp256k1, we disable symbol renaming in the secp256k1-sys crate  so it links to the
# same library as the C++ code.
# - Note that this does not prevent the secp256k1-sys vendored code from being built; this
#   requires https://github.com/rust-bitcoin/rust-secp256k1/issues/380 to be addressed.
RUST_ENV_VARS = \
  RUSTC="$(RUSTC)" \
  RUSTFLAGS="--cfg=rust_secp_no_symbol_renaming -L native=$(abs_top_srcdir)/src/secp256k1/.libs" \
  CC="$(CC)" \
  CFLAGS="$(CFLAGS)" \
  CXX="$(CXX)" \
  CXXFLAGS="$(CXXFLAGS)" \
  TERM=dumb
RUST_BUILD_OPTS = --release --target $(RUST_TARGET) --manifest-path $(top_srcdir)/Cargo.toml

rust_verbose = $(rust_verbose_@AM_V@)
rust_verbose_ = $(rust_verbose_@AM_DEFAULT_V@)
rust_verbose_0 = @echo "  RUST     $@";

cargo_verbose = $(cargo_verbose_@AM_V@)
cargo_verbose_ = $(cargo_verbose_@AM_DEFAULT_V@)
cargo_verbose_1 = --verbose

if ENABLE_ONLINE_RUST
# Ensure that .cargo/config does not exist
CARGO_CONFIGURED = $(top_srcdir)/.cargo/.configured-for-online
$(CARGO_CONFIGURED):
	$(AM_V_at)rm -f $(top_srcdir)/.cargo/.configured-for-offline $(top_srcdir)/.cargo/config
	$(AM_V_at)touch $@

else
# Enable dependency vendoring
RUST_BUILD_OPTS += --offline

CARGO_CONFIGURED = $(top_srcdir)/.cargo/.configured-for-offline
$(CARGO_CONFIGURED): $(top_srcdir)/.cargo/config.offline
	$(AM_V_at)rm -f $(top_srcdir)/.cargo/.configured-for-online
	$(AM_V_at)cp $< $(top_srcdir)/.cargo/config
	$(AM_V_at)echo "directory = \"$(RUST_VENDORED_SOURCES)\"" >>$(top_srcdir)/.cargo/config
	$(AM_V_at)touch $@
endif

cargo-build: $(CARGO_CONFIGURED) $(LIBSECP256K1)
	$(rust_verbose)$(RUST_ENV_VARS) $(CARGO) build $(RUST_BUILD_OPTS) $(cargo_verbose)

cargo-build-lib: cargo-build

cargo-build-bins: cargo-build

$(LIBRUSTZCASH): cargo-build-lib
	$(AM_V_at)$(MKDIR_P) $(@D)/.libs
	$(AM_V_at)echo '# $(@F) - a libtool library file' >$@
	$(AM_V_at)echo "# Generated by src/Makefile.am for libtool" >>$@
	$(AM_V_at)echo "dlname=''" >>$@
	$(AM_V_at)echo "library_names=''" >>$@
	$(AM_V_at)echo "old_library='../$(basename $(@F)).a'" >>$@
	$(AM_V_at)echo "inherited_linker_flags=''" >>$@
	$(AM_V_at)echo "installed=no" >>$@
	$(AM_V_at)echo "shouldnotlink=no" >>$@

$(LIBSECP256K1): $(wildcard secp256k1/src/*) $(wildcard secp256k1/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F) OPTFLAGS="-O2 -march=$(PLATFORM_VARIANT) -g "

LIBSNARK_CXXFLAGS = $(AM_CXXFLAGS) $(PIC_FLAGS) -DBINARY_OUTPUT -DNO_PT_COMPRESSION=1 -fstack-protector-all
LIBSNARK_CONFIG_FLAGS = CURVE=ALT_BN128 NO_PROCPS=1 NO_DOCS=1 STATIC=1 NO_SUPERCOP=1 FEATUREFLAGS=-DMONTGOMERY_OUTPUT NO_COPY_DEPINST=1 NO_COMPILE_LIBGTEST=1
if HAVE_OPENMP
LIBSNARK_CONFIG_FLAGS += MULTICORE=1
endif
if TARGET_DARWIN
LIBSNARK_CONFIG_FLAGS += PLATFORM=darwin
endif

$(LIBSNARK): $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="-O2 -march=$(PLATFORM_VARIANT)"

libsnark-tests: $(wildcard snark/src/*)
	$(AM_V_at) CC="$(CC)" CXX="$(CXX)" AR="$(AR)" CXXFLAGS="$(LIBSNARK_CXXFLAGS)" $(MAKE) $(AM_MAKEFLAGS) -C snark/ check DEPINST="$(LIBSNARK_DEPINST)" $(LIBSNARK_CONFIG_FLAGS) OPTFLAGS="-O2 -march=$(PLATFORM_VARIANT)"

$(LIBUNIVALUE): $(wildcard univalue/lib/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F) OPTFLAGS="-O2 -march=$(PLATFORM_VARIANT) -g "

$(LIBCRYPTOCONDITIONS): $(wildcard cryptoconditions/src/*) $(wildcard cryptoconditions/include/*)
	$(AM_V_at)$(MAKE) $(AM_MAKEFLAGS) -C $(@D) $(@F) OPTFLAGS="-O2 -march=$(PLATFORM_VARIANT) -g "

#%.o: %.c
#	$(CC) -c -o $@ $<

#$(LIBCJSON): cJSON.o komodo_cJSON.o komodo_cutils.o
#	$(AR) cr $(LIBCJSON) $^

# libcjson build
LIBCJSON=libcjson.a
libcjson_a_SOURCES = cJSON.c \
  komodo_cJSON.c komodo_cutils.cpp
libcjson_a_CPPFLAGS=-fPIC
EXTRA_LIBRARIES += $(LIBCJSON)

# Make is not made aware of per-object dependencies to avoid limiting building parallelization
# But to build the less dependent modules first, we manually select their order here:
EXTRA_LIBRARIES += \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_CLI) \
	$(LIBCXXBRIDGE) \
  $(LIBZCASH) \
	$(LIBTLS)

if ENABLE_WALLET
BITCOIN_INCLUDES += $(BDB_CPPFLAGS)
EXTRA_LIBRARIES += $(LIBBITCOIN_WALLET)
endif
if ENABLE_ZMQ
EXTRA_LIBRARIES += $(LIBBITCOIN_ZMQ)
endif
if ENABLE_PROTON
EXTRA_LIBRARIES += $(LIBBITCOIN_PROTON)
endif

lib_LTLIBRARIES = $(LIBZCASH_CONSENSUS) $(LIBCC)

bin_PROGRAMS =
noinst_PROGRAMS =
TESTS =

#if BUILD_BITCOIND
  bin_PROGRAMS += pirated
#endif

if BUILD_BITCOIN_UTILS
  bin_PROGRAMS += pirate-cli pirate-tx
endif
if ENABLE_WALLET
  bin_PROGRAMS += wallet-utility
endif


LIBZCASH_H = \
  zcash/IncrementalMerkleTree.hpp \
  zcash/NoteEncryption.hpp \
  zcash/Address.hpp \
  zcash/JoinSplit.hpp \
  zcash/Note.hpp \
  zcash/prf.h \
  zcash/Proof.hpp \
	zcash/address/sapling.hpp \
	zcash/address/sprout.hpp \
  zcash/util.h \
  zcash/Zcash.h \
  zcash/address/zip32.h

.PHONY: FORCE collate-libsnark check-symbols check-security
# bitcoin core #
BITCOIN_CORE_H = \
  addressindex.h \
  spentindex.h \
  addrman.h \
	attributes.h \
	addrdb.h \
  alert.h \
  amount.h \
  amqp/amqpabstractnotifier.h \
  amqp/amqpconfig.h \
  amqp/amqpnotificationinterface.h \
  amqp/amqppublishnotifier.h \
  amqp/amqpsender.h \
  arith_uint256.h \
  asyncrpcoperation.h \
  asyncrpcqueue.h \
  base58.h \
  bech32.h \
  bloom.h \
  cc/eval.h \
  chain.h \
  chainparams.h \
  chainparamsbase.h \
  chainparamsseeds.h \
  checkpoints.h \
  checkqueue.h \
  clientversion.h \
  coincontrol.h \
  coins.h \
  compat.h \
  compat/byteswap.h \
  compat/endian.h \
  compat/sanity.h \
  compressor.h \
  consensus/consensus.h \
  consensus/params.h \
  consensus/upgrades.h \
  consensus/validation.h \
  core_io.h \
  core_memusage.h \
	cuckoocache.h \
  deprecation.h \
  fs.h \
  hash.h \
  httprpc.h \
  httpserver.h \
	i2p.h \
  init.h \
  key.h \
  key_io.h \
  keystore.h \
  dbwrapper.h \
  limitedmap.h \
  main.h \
  memusage.h \
  merkleblock.h \
  metrics.h \
  miner.h \
  mruset.h \
  net.h \
  netbase.h \
	netaddress.h \
	netmessagemaker.h \
  notaries_staked.h \
  noui.h \
	params.h \
  paymentdisclosure.h \
  paymentdisclosuredb.h \
  policy/fees.h \
  pow.h \
  prevector.h \
	span.h \
  primitives/block.h \
	primitives/sapling.h \
  primitives/transaction.h \
  protocol.h \
  pubkey.h \
  random.h \
	util/readwritefile.h \
  reverselock.h \
  rpc/client.h \
  rpc/protocol.h \
  rpc/server.h \
  rpc/register.h \
  scheduler.h \
  script/interpreter.h \
  script/script.h \
  script/script_error.h \
  script/serverchecker.h \
  script/sign.h \
  script/standard.h \
  serialize.h \
  streams.h \
	streams_rust.h \
  support/allocators/secure.h \
  support/allocators/zeroafterfree.h \
  support/cleanse.h \
  support/events.h \
  support/pagelocker.h \
  sync.h \
  threadsafety.h \
  timedata.h \
  tinyformat.h \
  torcontrol.h \
  transaction_builder.h \
  txdb.h \
  txmempool.h \
  ui_interface.h \
  util/asmap.h \
  uint256.h \
  uint252.h \
  undo.h \
  util.h \
  utilmoneystr.h \
	util/sock.h \
	util/spanparsing.h \
  util/strencodings.h \
	util/string.h \
  utiltime.h \
  validationinterface.h \
  version.h \
  wallet/asyncrpcoperation_mergetoaddress.h \
	wallet/asyncrpcoperation_saplingconsolidation.h \
	wallet/asyncrpcoperation_sweeptoaddress.h \
  wallet/asyncrpcoperation_sendmany.h \
  wallet/asyncrpcoperation_shieldcoinbase.h \
  wallet/crypter.h \
  wallet/db.h \
  wallet/rpcwallet.h \
	wallet/rpcpiratewallet.h \
	wallet/sapling.h \
  wallet/wallet.h \
	wallet/wallet_fees.h \
  wallet/wallet_ismine.h \
  wallet/walletdb.h \
  wallet/witness.h \
  zmq/zmqabstractnotifier.h \
  zmq/zmqconfig.h \
  zmq/zmqnotificationinterface.h \
  zmq/zmqpublishnotifier.h

	LIBTLS_H = \
	    tls/utiltls.h

obj/build.h: FORCE
	@$(MKDIR_P) $(builddir)/obj
	@$(top_srcdir)/share/genbuild.sh $(abs_top_builddir)/src/obj/build.h \
	$(abs_top_srcdir)
libbitcoin_util_a-clientversion.$(OBJEXT): obj/build.h

# server: zcashd
libbitcoin_server_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libbitcoin_server_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_server_a_CPPFLAGS += -fPIC
libbitcoin_server_a_SOURCES = \
  sendalert.cpp \
  addrman.cpp \
	addrdb.cpp \
  alert.cpp \
  alertkeys.h \
  asyncrpcoperation.cpp \
  asyncrpcqueue.cpp \
  bloom.cpp \
  cc/eval.cpp \
  cc/import.cpp \
  cc/importgateway.cpp \
  cc/CCassetsCore.cpp \
  cc/CCcustom.cpp \
  cc/CCtx.cpp \
  cc/CCutils.cpp \
  cc/CCtokens.cpp \
  cc/assets.cpp \
  cc/faucet.cpp \
  cc/rewards.cpp \
  cc/dice.cpp \
  cc/lotto.cpp \
  cc/fsm.cpp \
  cc/heir.cpp \
  cc/oracles.cpp \
  cc/payments.cpp \
  cc/gateways.cpp \
  cc/channels.cpp \
  cc/auction.cpp \
  cc/betprotocol.cpp \
  chain.cpp \
  checkpoints.cpp \
  fs.cpp \
  crosschain.cpp \
  deprecation.cpp \
  httprpc.cpp \
  httpserver.cpp \
	i2p.cpp \
  init.cpp \
  dbwrapper.cpp \
  main.cpp \
  merkleblock.cpp \
  metrics.h \
  miner.cpp \
  net.cpp \
	netbase.cpp \
	netaddress.cpp \
  notaries_staked.cpp \
  noui.cpp \
  notarisationdb.cpp \
	params.cpp \
  paymentdisclosure.cpp \
  paymentdisclosuredb.cpp \
  policy/fees.cpp \
  pow.cpp \
  rest.cpp \
  rpc/blockchain.cpp \
  rpc/crosschain.cpp \
  rpc/mining.cpp \
  rpc/misc.cpp \
  rpc/net.cpp \
  rpc/rawtransaction.cpp \
  rpc/server.cpp \
  script/serverchecker.cpp \
  script/sigcache.cpp \
  timedata.cpp \
  torcontrol.cpp \
  txdb.cpp \
  txmempool.cpp \
  validationinterface.cpp \
	cc/cclib.cpp \
	mini-gmp.c \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H) \
	$(LIBTLS_H)

libcxxbridge_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS) $(EVENT_PTHREADS_CFLAGS)
libcxxbridge_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libcxxbridge_a_SOURCES = \
  $(CXXBRIDGE_CPP) \
  $(CXXBRIDGE_H) \
  zcash/cache.cpp \
  zcash/cache.h

if ENABLE_ZMQ
libbitcoin_zmq_a_CPPFLAGS = $(BITCOIN_INCLUDES) $(ZMQ_CFLAGS)
libbitcoin_zmq_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_zmq_a_SOURCES = \
	zmq/zmqabstractnotifier.cpp \
	zmq/zmqnotificationinterface.cpp \
	zmq/zmqpublishnotifier.cpp
endif

if ENABLE_PROTON
libbitcoin_proton_a_CPPFLAGS = $(BITCOIN_INCLUDES)
libbitcoin_proton_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_proton_a_SOURCES = \
  amqp/amqpabstractnotifier.cpp \
  amqp/amqpnotificationinterface.cpp \
  amqp/amqppublishnotifier.cpp
endif

# wallet: zcashd, but only linked when wallet enabled
libbitcoin_wallet_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_wallet_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_wallet_a_SOURCES = \
  utiltest.cpp \
  utiltest.h \
  zcbenchmarks.cpp \
  zcbenchmarks.h \
  wallet/asyncrpcoperation_mergetoaddress.cpp \
	wallet/asyncrpcoperation_saplingconsolidation.cpp \
	wallet/asyncrpcoperation_sweeptoaddress.cpp \
  wallet/asyncrpcoperation_sendmany.cpp \
  wallet/asyncrpcoperation_shieldcoinbase.cpp \
  wallet/crypter.cpp \
  wallet/db.cpp \
  paymentdisclosure.cpp \
  paymentdisclosuredb.cpp \
  transaction_builder.cpp \
  wallet/rpcdisclosure.cpp \
  wallet/rpcdump.cpp \
  cc/CCtokens.cpp \
  cc/CCassetsCore.cpp \
  cc/CCassetstx.cpp \
  cc/CCtx.cpp \
  wallet/rpcwallet.cpp \
	wallet/rpcpiratewallet.cpp \
  wallet/wallet.cpp \
	wallet/wallet_fees.cpp \
  wallet/wallet_ismine.cpp \
  wallet/walletdb.cpp \
  wallet/witness.cpp \
  zcash/address/zip32.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

# a shared library for cryptoconditions
libcc_a_SOURCES = cc/cclib.cpp
libcc_a_CXXFLAGS = -DBUILD_CUSTOMCC -I../secp256k1/include -I../depends/$(shell echo `../depends/config.guess`/include) -I./univalue/include -I./cryptoconditions/include -I./cryptoconditions/src -I./cryptoconditions/src/asn -I. -I./cc -I$(srcdir)/rust/include -I$(srcdir)/rust/gen/include
libcc_a_LDFLAGS = -version-info 0:0:0

# crypto primitives library
crypto_libbitcoin_crypto_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_CONFIG_INCLUDES)
crypto_libbitcoin_crypto_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
crypto_libbitcoin_crypto_a_SOURCES = \
  crypto/common.h \
  crypto/equihash.cpp \
  crypto/equihash.h \
  crypto/equihash.tcc \
  crypto/hmac_sha256.cpp \
  crypto/hmac_sha256.h \
  crypto/hmac_sha512.cpp \
  crypto/hmac_sha512.h \
  crypto/ripemd160.cpp \
  crypto/ripemd160.h \
  crypto/sha1.cpp \
  crypto/sha1.h \
	crypto/sha3.cpp \
  crypto/sha3.h \
  crypto/sha256.cpp \
  crypto/sha256.h \
  crypto/sha512.cpp \
  crypto/sha512.h

if EXPERIMENTAL_ASM
  crypto_libbitcoin_crypto_a_SOURCES += crypto/sha256_sse4.cpp
endif

if ENABLE_MINING
EQUIHASH_TROMP_SOURCES = \
	pow/tromp/equi_miner.h \
	pow/tromp/equi.h \
	pow/tromp/osx_barrier.h

crypto_libbitcoin_crypto_a_CPPFLAGS += \
	-DEQUIHASH_TROMP_ATOMIC
crypto_libbitcoin_crypto_a_SOURCES += \
	${EQUIHASH_TROMP_SOURCES}
endif

# common: shared between zcashd and non-server tools
libbitcoin_common_a_CPPFLAGS = -fPIC $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_common_a_CXXFLAGS = -fPIC $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_common_a_SOURCES = \
  amount.cpp \
  arith_uint256.cpp \
  base58.cpp \
  bech32.cpp \
  chainparams.cpp \
  coins.cpp \
  compressor.cpp \
  consensus/upgrades.cpp \
  core_read.cpp \
  core_write.cpp \
  hash.cpp \
  hex.c \
  importcoin.cpp \
  key.cpp \
  key_io.cpp \
  keystore.cpp \
  komodo_structs.cpp \
  komodo.cpp \
  komodo_bitcoind.cpp \
  komodo_ccdata.cpp \
  komodo_curve25519.cpp \
  komodo_events.cpp \
  komodo_gateway.cpp \
  komodo_globals.cpp \
  komodo_hardfork.cpp \
  komodo_interest.cpp \
  komodo_kv.cpp \
  komodo_notary.cpp \
  komodo_utils.cpp \
  metrics.cpp \
  primitives/block.cpp \
  primitives/transaction.cpp \
  protocol.cpp \
  pubkey.cpp \
  scheduler.cpp \
  script/cc.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  script/script_ext.cpp \
  script/script_error.cpp \
  script/sign.cpp \
  script/standard.cpp \
	streams_rust.cpp \
	sync.cpp \
  transaction_builder.cpp \
  cc/CCtokenutils.cpp \
  cc/CCutilbits.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

# util: shared between all executables.
# This library *must* be included to make sure that the glibc
# backward-compatibility objects and their sanity checks are linked.
libbitcoin_util_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_util_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_util_a_SOURCES = \
  support/pagelocker.cpp \
  chainparamsbase.cpp \
  clientversion.cpp \
  compat/glibc_sanity.cpp \
  compat/glibcxx_sanity.cpp \
  compat/strnlen.cpp \
  random.cpp \
  rpc/protocol.cpp \
  support/cleanse.cpp \
  uint256.cpp \
  util.cpp \
  utilmoneystr.cpp \
	util/sock.cpp \
	util/spanparsing.cpp \
  util/strencodings.cpp \
	util/string.cpp \
	utiltime.cpp \
  util/asmap.cpp \
	util/readwritefile.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

if GLIBC_BACK_COMPAT
libbitcoin_util_a_SOURCES += compat/glibc_compat.cpp
endif


# cli: zcash-cli
libbitcoin_cli_a_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
libbitcoin_cli_a_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
libbitcoin_cli_a_SOURCES = \
  rpc/client.cpp \
  $(BITCOIN_CORE_H) \
  $(LIBZCASH_H)

nodist_libbitcoin_util_a_SOURCES = $(srcdir)/obj/build.h
#

# bitcoind binary #
pirated_SOURCES = bitcoind.cpp
pirated_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
pirated_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pirated_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
pirated_SOURCES += bitcoind-res.rc
endif

pirated_LDADD = \
  $(LIBBITCOIN_SERVER) \
  $(LIBBITCOIN_COMMON) \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_UTIL) \
  $(LIBBITCOIN_ZMQ) \
  $(LIBBITCOIN_PROTON) \
  $(LIBBITCOIN_CRYPTO)

if ENABLE_WALLET
pirated_LDADD += $(LIBBITCOIN_WALLET)
endif

pirated_LDADD += \
  $(LIBZCASH) \
	$(LIBTLS) \
	$(LIBRUSTZCASH) \
	$(LIBCXXBRIDGE) \
  $(LIBSNARK) \
  $(LIBLEVELDB) \
  $(LIBMEMENV) \
  $(LIBSECP256K1) \
  $(LIBCRYPTOCONDITIONS) \
	$(LIBCJSON) \
  $(BOOST_LIBS) \
  $(BDB_LIBS) \
  $(SSL_LIBS) \
  $(CRYPTO_LIBS) \
  $(EVENT_PTHREADS_LIBS) \
  $(EVENT_LIBS) \
  $(ZMQ_LIBS) \
  $(PROTON_LIBS) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS) \
  $(LIBSECP256K1) \
  $(LIBCRYPTOCONDITIONS)

# if TARGET_DARWIN
# pirated_LDADD += libcc.dylib $(LIBSECP256K1)
# endif
# if TARGET_WINDOWS
# pirated_LDADD += libcc.dll $(LIBSECP256K1)
# endif
# if TARGET_LINUX
# pirated_LDADD += libcc.so $(LIBSECP256K1)
# endif

if ENABLE_PROTON
pirated_LDADD += $(LIBBITCOIN_PROTON) $(PROTON_LIBS)
endif

# [+] Decker: use static linking for libstdc++.6.dylib, libgomp.1.dylib, libgcc_s.1.dylib
if TARGET_DARWIN
pirated_LDFLAGS += -static-libgcc
endif

# bitcoin-cli binary #
pirate_cli_SOURCES = bitcoin-cli.cpp
pirate_cli_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES) $(EVENT_CFLAGS)
pirate_cli_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pirate_cli_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)
if TARGET_DARWIN
pirate_cli_LDFLAGS += -static-libgcc
endif

# wallet-utility binary #
if ENABLE_WALLET
wallet_utility_SOURCES = wallet-utility.cpp
wallet_utility_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
wallet_utility_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
wallet_utility_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
wallet_utility_SOURCES += wallet-utility-res.rc
endif

endif

if TARGET_WINDOWS
pirate_cli_SOURCES += bitcoin-cli-res.rc
endif

pirate_cli_LDADD = \
	$(LIBBITCOIN_CLI) \
	$(LIBUNIVALUE) \
	$(LIBBITCOIN_UTIL) \
	$(BOOST_LIBS) \
	$(SSL_LIBS) \
	$(CRYPTO_LIBS) \
	$(EVENT_LIBS) \
	$(LIBZCASH) \
	$(LIBTLS) \
	$(LIBRUSTZCASH) \
	$(LIBCXXBRIDGE) \
	$(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS)

if ENABLE_WALLET
wallet_utility_LDADD = \
	libbitcoin_wallet.a \
	$(LIBBITCOIN_COMMON) \
	$(LIBBITCOIN_CRYPTO) \
	$(LIBSECP256K1) \
	$(LIBBITCOIN_UTIL) \
	$(BOOST_LIBS) \
	$(BDB_LIBS) \
	$(CRYPTO_LIBS) \
	$(LIBZCASH) \
	$(LIBRUSTZCASH) \
	$(LIBCXXBRIDGE) \
	$(LIBSNARK) \
	$(LIBZCASH_LIBS)\
  $(LIBCRYPTOCONDITIONS)
endif

# zcash-tx binary #
pirate_tx_SOURCES = komodo-tx.cpp
pirate_tx_CPPFLAGS = $(AM_CPPFLAGS) $(BITCOIN_INCLUDES)
pirate_tx_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)
pirate_tx_LDFLAGS = $(RELDFLAGS) $(AM_LDFLAGS) $(LIBTOOL_APP_LDFLAGS)

if TARGET_WINDOWS
pirate_tx_SOURCES += bitcoin-tx-res.rc
endif

# FIXME: Is libzcash needed for zcash_tx?
pirate_tx_LDADD = \
  $(LIBUNIVALUE) \
  $(LIBBITCOIN_COMMON) \
  $(LIBBITCOIN_UTIL) \
  $(LIBSECP256K1) \
  $(LIBZCASH) \
	$(LIBTLS) \
	$(LIBRUSTZCASH) \
	$(LIBCXXBRIDGE) \
  $(LIBSNARK) \
  $(LIBBITCOIN_CRYPTO) \
  $(LIBZCASH_LIBS) \
  $(LIBCRYPTOCONDITIONS)

pirate_tx_LDADD += $(BOOST_LIBS) $(CRYPTO_LIBS)
#

# zcash protocol primitives #
libzcash_a_SOURCES = \
	zcash/IncrementalMerkleTree.cpp \
	zcash/NoteEncryption.cpp \
	zcash/Address.cpp \
	zcash/JoinSplit.cpp \
	zcash/Proof.cpp \
	zcash/Note.cpp \
	zcash/prf.cpp \
	zcash/address/sapling.cpp \
	zcash/address/sprout.cpp \
	zcash/util.cpp \
  zcash/address/zip32.cpp \
	zcash/circuit/commitment.tcc \
	zcash/circuit/gadget.tcc \
	zcash/circuit/merkle.tcc \
	zcash/circuit/note.tcc \
	zcash/circuit/prfs.tcc \
	zcash/circuit/utils.tcc

libzcash_a_CPPFLAGS = -DMULTICORE -fopenmp -fPIC -DBINARY_OUTPUT -DCURVE_ALT_BN128 \
	-DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS $(HARDENED_CPPFLAGS) \
	$(HARDENED_CXXFLAGS) $(HARDENED_LDFLAGS) -pipe $(SAN_LDFLAGS) \
	-Wstack-protector $(SAN_CXXFLAGS) -fstack-protector-all -fPIE -fvisibility=hidden \
	-DSTATIC $(BITCOIN_INCLUDES)
libzcash_a_CXXFLAGS = $(SAN_CXXFLAGS) $(HARDENED_CXXFLAGS) -fwrapv -fno-strict-aliasing
libzcash_a_LDFLAGS = $(SAN_LDFLAGS) $(HARDENED_LDFLAGS)
libzcash_a_CPPFLAGS += -DMONTGOMERY_OUTPUT

# zcashconsensus library #
if BUILD_BITCOIN_LIBS
include_HEADERS = script/zcashconsensus.h
libzcashconsensus_la_SOURCES = \
  crypto/equihash.cpp \
  crypto/hmac_sha512.cpp \
  crypto/ripemd160.cpp \
  crypto/sha1.cpp \
	crypto/sha3.cpp \
  crypto/sha256.cpp \
  crypto/sha512.cpp \
  hash.cpp \
  primitives/transaction.cpp \
  pubkey.cpp \
  script/zcashconsensus.cpp \
  script/interpreter.cpp \
  script/script.cpp \
  uint256.cpp \
  utilstrencodings.cpp

if GLIBC_BACK_COMPAT
libzcashconsensus_la_SOURCES += compat/glibc_compat.cpp
endif

libzcashconsensus_la_LDFLAGS = $(AM_LDFLAGS) -no-undefined $(RELDFLAGS)
libzcashconsensus_la_LIBADD = $(LIBSECP256K1)
libzcashconsensus_la_CPPFLAGS = $(AM_CPPFLAGS) -I$(builddir)/obj  -I$(srcdir)/rust/include -I$(srcdir)/rust/gen/include -I$(srcdir)/secp256k1/include -I$(srcdir)/cryptoconditions/include -DBUILD_BITCOIN_INTERNAL
libzcashconsensus_la_CXXFLAGS = $(AM_CXXFLAGS) $(PIE_FLAGS)

endif
#

libtls_a_SOURCES = \
    tls/utiltls.cpp\
    tls/tlsmanager.cpp

libtls_a_CPPFLAGS = -DMULTICORE -fopenmp -fPIC -DBINARY_OUTPUT -DCURVE_ALT_BN128 -DBOOST_SPIRIT_THREADSAFE -DHAVE_BUILD_INFO -D__STDC_FORMAT_MACROS $(HARDENED_CPPFLAGS) -pipe -O2 -g  -Wstack-protector -fstack-protector-all -fPIE -fvisibility=hidden -DSTATIC $(BITCOIN_INCLUDES) -I$(srcdir)/rust/include -I$(srcdir)/rust/gen/include

libtls_a_CXXFLAGS = $(HARDENED_CXXFLAGS) -fwrapv -fno-strict-aliasing

libtls_a_LDFLAGS = $(HARDENED_LDFLAGS)

libtls_a_CPPFLAGS += -DMONTGOMERY_OUTPUT

BUILT_SOURCES = $(CXXBRIDGE_H)

CLEANFILES = leveldb/libleveldb.a leveldb/libmemenv.a *.gcda *.gcno */*.gcno wallet/*/*.gcno

DISTCLEANFILES = obj/build.h

EXTRA_DIST = leveldb snark rust

clean-local:
	-$(MAKE) -C leveldb clean
	-$(MAKE) -C secp256k1 clean
	-$(MAKE) -C snark clean
	-$(MAKE) -C univalue clean
	-rm -f leveldb/*/*.gcno leveldb/helpers/memenv/*.gcno
	-rm -f config.h
	-$(RM) *.a
	-$(RM) crypto/*.a
	-$(RM) cryptoconditions/.libs/*.a

clean-all: clean-local
	-$(MAKE) -C snark clean-all
	-$(MAKE) -C univalue clean-all

.rc.o:
	@test -f $(WINDRES)
	$(AM_V_GEN) $(WINDRES) -DWINDRES_PREPROC -i $< -o $@

.mm.o:
	$(AM_V_CXX) $(OBJCXX) $(DEFS) $(DEFAULT_INCLUDES) $(INCLUDES) $(AM_CPPFLAGS) \
	  $(CPPFLAGS) $(AM_CXXFLAGS) $(AM_CXXFLAGS) $(PIE_FLAGS) $(CXXFLAGS) -c -o $@ $<

check-symbols: $(bin_PROGRAMS)
if GLIBC_BACK_COMPAT
	@echo "Checking glibc back compat of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) CPPFILT=$(CPPFILT) $(top_srcdir)/contrib/devtools/symbol-check.py $(bin_PROGRAMS)
endif

check-security: $(bin_PROGRAMS)
if HARDEN
	@echo "Checking binary security of [$(bin_PROGRAMS)]..."
	$(AM_V_at) READELF=$(READELF) OBJDUMP=$(OBJDUMP) $(top_srcdir)/contrib/devtools/security-check.py $(bin_PROGRAMS)
endif

%.pb.cc %.pb.h: %.proto
	@test -f $(PROTOC)
	$(AM_V_GEN) $(PROTOC) --cpp_out=$(@D) --proto_path=$(abspath $(<D) $<)

if EMBEDDED_LEVELDB
include Makefile.leveldb.include
endif

if ENABLE_TESTS
include Makefile.ktest.include
#include Makefile.test.include # bitcoin tests
#include Makefile.gtest.include # zcash tests
endif


if ENABLE_QT
include Makefile.qt.include
endif
