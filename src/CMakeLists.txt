# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Milestone 2b (in progress): library and executable targets, transcribed from
# src/Makefile.am. Bring-up order: leaf libraries first.

#=============================
# Common include paths
#=============================
# Mirrors autotools BITCOIN_INCLUDES / BITCOIN_CONFIG_INCLUDES: every translation
# unit resolves "config/bitcoin-config.h", in-tree headers, the hand-written rust
# FFI headers and (once generated) the cxxbridge output.
add_library(bitcoin_includes INTERFACE)
set(CXXBRIDGE_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/rust/gen)

target_include_directories(bitcoin_includes INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}
  # Generated headers (embedded_binary_hashes.h, ...) land here. Note src/ is
  # searched first, so a stale generated header left in the source tree by an
  # old autotools build would shadow the fresh one - those are gitignored and
  # must not be present.
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/config
  ${CMAKE_CURRENT_SOURCE_DIR}/rust/include
  ${CXXBRIDGE_GEN_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/secp256k1/include
  ${CMAKE_CURRENT_SOURCE_DIR}/cc/includes
  ${CMAKE_CURRENT_SOURCE_DIR}/cryptoconditions/include
  ${CMAKE_CURRENT_SOURCE_DIR}/cryptoconditions/src
  ${CMAKE_CURRENT_SOURCE_DIR}/cryptoconditions/src/asn
  ${CMAKE_CURRENT_SOURCE_DIR}/univalue/include
  ${CMAKE_CURRENT_SOURCE_DIR}/leveldb/include
  ${CMAKE_CURRENT_SOURCE_DIR}/leveldb/helpers/memenv
)
target_link_libraries(bitcoin_includes INTERFACE
  core_interface
  $<TARGET_NAME_IF_EXISTS:Boost::headers>
)
if(Boost_INCLUDE_DIRS)
  target_include_directories(bitcoin_includes INTERFACE ${Boost_INCLUDE_DIRS})
endif()

#=============================
# Vendored third-party trees
#=============================
# RUST_TARGET: the triple cargo emits under target/<triple>/release/. Prefer the
# value depends computed, because rustc's triple is not always the depends host
# dir name -- on macOS the latter carries an OS version (x86_64-apple-darwin24.6.0)
# that rustc rejects outright. Fall back to the directory name for trees
# configured without the depends toolchain file.
get_filename_component(_depends_host "${DEPENDS_PREFIX}" NAME)
if(DEPENDS_RUST_TARGET)
  set(_default_rust_target "${DEPENDS_RUST_TARGET}")
else()
  set(_default_rust_target "${_depends_host}")
endif()
set(RUST_TARGET "${_default_rust_target}" CACHE STRING "Rust target triple for librustzcash")

add_subdirectory(leveldb)
add_subdirectory(univalue)
add_subdirectory(secp256k1)
add_subdirectory(cryptoconditions)
add_subdirectory(rust)

#=============================
# crypto: hash / PoW primitives
#=============================
add_library(bitcoin_crypto STATIC
  crypto/equihash.cpp
  crypto/hmac_sha256.cpp
  crypto/hmac_sha512.cpp
  crypto/ripemd160.cpp
  crypto/sha1.cpp
  crypto/sha3.cpp
  crypto/sha256.cpp
  crypto/sha512.cpp
)
target_link_libraries(bitcoin_crypto PRIVATE bitcoin_includes)
if(ENABLE_MINING)
  target_compile_definitions(bitcoin_crypto PRIVATE EQUIHASH_TROMP_ATOMIC)
endif()

#=============================
# util: shared by every executable
#=============================
add_library(bitcoin_util STATIC
  support/pagelocker.cpp
  chainparamsbase.cpp
  clientversion.cpp
  compat/glibc_sanity.cpp
  compat/glibcxx_sanity.cpp
  compat/strnlen.cpp
  random.cpp
  rpc/protocol.cpp
  support/cleanse.cpp
  uint256.cpp
  util.cpp
  utilmoneystr.cpp
  util/sock.cpp
  util/spanparsing.cpp
  util/strencodings.cpp
  util/string.cpp
  utiltime.cpp
  util/asmap.cpp
  util/readwritefile.cpp
)
target_link_libraries(bitcoin_util PRIVATE bitcoin_includes)

#=============================
# cli: pirate-cli RPC client
#=============================
add_library(bitcoin_cli STATIC
  rpc/client.cpp
)
target_link_libraries(bitcoin_cli PRIVATE bitcoin_includes)

#=============================
# Rust FFI: cxxbridge codegen + the prebuilt static libs
#=============================
find_program(CXXBRIDGE_BIN
  NAMES cxxbridge
  HINTS ${DEPENDS_PREFIX}/native/bin
  REQUIRED
)

# .rs bridge module -> (generated header, generated source). Matches the
# per-file rules in src/Makefile.am (the generic rule there only handles the
# three modules that sit directly under rust/src/).
set(_cxxbridge_map
  "crypto/blake2b.rs|blake2b.h|blake2b.cpp"
  "sapling_protocol/zip32.rs|sapling/zip32.h|sapling/zip32.cpp"
  "sprout_protocol/sprout.rs|sprout.h|sprout.cpp"
  "streams.rs|streams.h|streams.cpp"
  "history.rs|history.h|history.cpp"
  "bridge.rs|bridge.h|bridge.cpp"
  "sapling_protocol/sapling_keys_bridge.rs|sapling_keys_bridge.h|sapling_keys_bridge.cpp"
  "ironwood_protocol/ironwood_keys_bridge.rs|ironwood_keys_bridge.h|ironwood_keys_bridge.cpp"
  "seed_bridge.rs|seed_bridge.h|seed_bridge.cpp"
)
set(CXXBRIDGE_GEN_CPP "")
set(CXXBRIDGE_GEN_H "")
foreach(entry IN LISTS _cxxbridge_map)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 rs_rel)
  list(GET parts 1 h_rel)
  list(GET parts 2 cpp_rel)
  set(rs_in  ${CMAKE_CURRENT_SOURCE_DIR}/rust/src/${rs_rel})
  set(h_out  ${CXXBRIDGE_GEN_DIR}/include/rust/${h_rel})
  set(cpp_out ${CXXBRIDGE_GEN_DIR}/src/${cpp_rel})
  get_filename_component(h_dir ${h_out} DIRECTORY)
  get_filename_component(cpp_dir ${cpp_out} DIRECTORY)
  add_custom_command(
    OUTPUT ${h_out} ${cpp_out}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${h_dir} ${cpp_dir}
    COMMAND ${CXXBRIDGE_BIN} -i rust/cxx.h ${rs_in} --header -o ${h_out}
    COMMAND ${CXXBRIDGE_BIN} -i rust/cxx.h ${rs_in} -o ${cpp_out}
    DEPENDS ${rs_in} ${CXXBRIDGE_BIN}
    COMMENT "cxxbridge ${rs_rel}"
    VERBATIM
  )
  list(APPEND CXXBRIDGE_GEN_CPP ${cpp_out})
  list(APPEND CXXBRIDGE_GEN_H ${h_out})
endforeach()

# Anything that includes a rust/*.h must wait for the codegen above.
add_custom_target(cxxbridge_gen DEPENDS ${CXXBRIDGE_GEN_H} ${CXXBRIDGE_GEN_CPP})

add_library(bitcoin_cxxbridge STATIC
  ${CXXBRIDGE_GEN_CPP}
  zcash/cache.cpp
)
target_link_libraries(bitcoin_cxxbridge PRIVATE bitcoin_includes)
add_dependencies(bitcoin_cxxbridge cxxbridge_gen)

#=============================
# common: shared by the daemon and the non-server tools
#=============================
add_library(bitcoin_common STATIC
  amount.cpp
  arith_uint256.cpp
  base58.cpp
  bech32.cpp
  chainparams.cpp
  coins.cpp
  compressor.cpp
  consensus/upgrades.cpp
  core_read.cpp
  core_write.cpp
  hash.cpp
  hex.c
  importcoin.cpp
  key.cpp
  key_io.cpp
  keystore.cpp
  komodo_structs.cpp
  komodo.cpp
  komodo_bitcoind.cpp
  komodo_ccdata.cpp
  komodo_curve25519.cpp
  komodo_events.cpp
  komodo_gateway.cpp
  komodo_globals.cpp
  komodo_hardfork.cpp
  komodo_interest.cpp
  komodo_kv.cpp
  komodo_notary.cpp
  komodo_utils.cpp
  metrics.cpp
  primitives/block.cpp
  primitives/transaction.cpp
  primitives/transaction_info.cpp
  protocol.cpp
  pubkey.cpp
  scheduler.cpp
  script/cc.cpp
  script/interpreter.cpp
  script/script.cpp
  script/script_ext.cpp
  script/script_error.cpp
  script/sign.cpp
  script/standard.cpp
  streams_rust.cpp
  sync.cpp
  transaction_builder.cpp
  cc/CCtokenutils.cpp
  cc/CCutilbits.cpp
)
target_link_libraries(bitcoin_common PRIVATE bitcoin_includes)

#=============================
# cJSON (komodo CC / notarisation helpers)
#=============================
add_library(bitcoin_cjson STATIC
  cJSON.c
  komodo_cJSON.c
  komodo_cutils.cpp
)
target_link_libraries(bitcoin_cjson PRIVATE bitcoin_includes)
set_target_properties(bitcoin_cjson PROPERTIES POSITION_INDEPENDENT_CODE ON)

#=============================
# zcash protocol primitives
#=============================
add_library(bitcoin_zcash STATIC
  zcash/IncrementalMerkleTree.cpp
  zcash/NoteEncryption.cpp
  zcash/Address.cpp
  zcash/JoinSplit.cpp
  zcash/memo.cpp
  zcash/Note.cpp
  zcash/prf.cpp
  zcash/address/ironwood.cpp
  zcash/address/sapling.cpp
  zcash/address/sprout.cpp
  zcash/History.cpp
  zcash/util.cpp
  zcash/address/zip32.cpp
)
target_link_libraries(bitcoin_zcash PRIVATE bitcoin_includes)
target_compile_definitions(bitcoin_zcash PRIVATE
  BINARY_OUTPUT CURVE_ALT_BN128 BOOST_SPIRIT_THREADSAFE __STDC_FORMAT_MACROS
  STATIC MONTGOMERY_OUTPUT
)
target_compile_options(bitcoin_zcash PRIVATE -fwrapv -fno-strict-aliasing -fvisibility=hidden)

#=============================
# TLS transport
#=============================
add_library(bitcoin_tls STATIC
  tls/utiltls.cpp
  tls/tlsmanager.cpp
)
target_link_libraries(bitcoin_tls PRIVATE bitcoin_includes)
target_compile_definitions(bitcoin_tls PRIVATE
  BINARY_OUTPUT CURVE_ALT_BN128 BOOST_SPIRIT_THREADSAFE __STDC_FORMAT_MACROS
  STATIC MONTGOMERY_OUTPUT
)
target_compile_options(bitcoin_tls PRIVATE -fwrapv -fno-strict-aliasing -fvisibility=hidden)

#=============================
# server: daemon-only code (net, RPC, validation, CC contracts)
#=============================
add_library(bitcoin_server STATIC
  sendalert.cpp
  addrman.cpp
  addrdb.cpp
  alert.cpp
  asyncrpcoperation.cpp
  asyncrpcqueue.cpp
  bloom.cpp
  cc/eval.cpp
  cc/import.cpp
  cc/importgateway.cpp
  cc/CCassetsCore.cpp
  cc/CCcustom.cpp
  cc/CCtx.cpp
  cc/CCutils.cpp
  cc/CCtokens.cpp
  cc/assets.cpp
  cc/faucet.cpp
  cc/rewards.cpp
  cc/dice.cpp
  cc/lotto.cpp
  cc/fsm.cpp
  cc/heir.cpp
  cc/oracles.cpp
  cc/payments.cpp
  cc/gateways.cpp
  cc/channels.cpp
  cc/auction.cpp
  cc/betprotocol.cpp
  cc/cclib.cpp
  chain.cpp
  checkpoints.cpp
  fs.cpp
  crosschain.cpp
  deprecation.cpp
  httprpc.cpp
  httpserver.cpp
  embeddedprocess.cpp
  i2p.cpp
  init.cpp
  dbwrapper.cpp
  main.cpp
  merkleblock.cpp
  miner.cpp
  net.cpp
  netbase.cpp
  netaddress.cpp
  notaries_staked.cpp
  noui.cpp
  notarisationdb.cpp
  params.cpp
  paymentdisclosure.cpp
  policy/fees.cpp
  pow.cpp
  rest.cpp
  rpc/blockchain.cpp
  rpc/crosschain.cpp
  rpc/mining.cpp
  rpc/misc.cpp
  rpc/net.cpp
  rpc/notaries.cpp
  rpc/rawtransaction.cpp
  rpc/server.cpp
  script/serverchecker.cpp
  script/sigcache.cpp
  timedata.cpp
  torcontrol.cpp
  txdb.cpp
  txmempool.cpp
  validationinterface.cpp
)
target_link_libraries(bitcoin_server PRIVATE bitcoin_includes)
target_compile_definitions(bitcoin_server PRIVATE $<$<BOOL:${ENABLE_ZMQ}>:ENABLE_ZMQ=1>)
if(ENABLE_EMBEDDED_TOR)
  target_sources(bitcoin_server PRIVATE tor_process.cpp)
endif()
if(ENABLE_EMBEDDED_I2PD)
  target_sources(bitcoin_server PRIVATE i2pd_process.cpp)
endif()
if(ENABLE_EMBEDDED_TOR OR ENABLE_EMBEDDED_I2PD)
  # init.cpp calls Start/Stop/NotifyNetworkingWatchdog under the same guard.
  target_sources(bitcoin_server PRIVATE networking_watchdog.cpp)
endif()

#=============================
# wallet: only linked when ENABLE_WALLET
#=============================
if(ENABLE_WALLET)
  add_library(bitcoin_wallet STATIC
    utiltest.cpp
    zcbenchmarks.cpp
    wallet/asyncrpcoperation_mergetoaddress.cpp
    wallet/asyncrpcoperation_saplingconsolidation.cpp
    wallet/asyncrpcoperation_saplingconsolidation_address.cpp
    wallet/asyncrpcoperation_ironwoodconsolidation_address.cpp
    wallet/asyncrpcoperation_ironwoodconsolidation.cpp
    wallet/asyncrpcoperation_sweeptoaddress.cpp
    wallet/asyncrpcoperation_sendmany.cpp
    wallet/asyncrpcoperation_shieldcoinbase.cpp
    wallet/crypter.cpp
    wallet/db.cpp
    paymentdisclosure.cpp
    transaction_builder.cpp
    wallet/rpcdump.cpp
    cc/CCtokens.cpp
    cc/CCassetsCore.cpp
    cc/CCassetstx.cpp
    cc/CCtx.cpp
    wallet/rpcwallet.cpp
    wallet/rpcpiratewallet.cpp
    wallet/rpcmultiwallet.cpp
    wallet/wallet.cpp
    wallet/wallet_fees.cpp
    wallet/wallet_ismine.cpp
    wallet/walletdb.cpp
    wallet/walletmanager.cpp
    wallet/witness.cpp
    zcash/address/zip32.cpp
  )
  target_link_libraries(bitcoin_wallet PRIVATE bitcoin_includes)
endif()

#=============================
# ZMQ notifications
#=============================
if(ENABLE_ZMQ)
  add_library(bitcoin_zmq STATIC
    zmq/zmqabstractnotifier.cpp
    zmq/zmqnotificationinterface.cpp
    zmq/zmqpublishnotifier.cpp
  )
  target_link_libraries(bitcoin_zmq PRIVATE bitcoin_includes)
  target_compile_definitions(bitcoin_zmq PRIVATE ENABLE_ZMQ=1)
  if(WIN32)
    # depends builds libzmq static; without this zmq.h declares its API
    # __declspec(dllimport) and the static link fails. configure.ac added
    # -DZMQ_STATIC to ZMQ_CFLAGS for mingw for the same reason.
    target_compile_definitions(bitcoin_zmq PRIVATE ZMQ_STATIC)
  endif()
endif()

# Serialise the cxxbridge codegen before any C++ TU that pulls in rust/*.h.
foreach(_t bitcoin_common bitcoin_server bitcoin_wallet bitcoin_tls bitcoin_zcash bitcoin_util)
  if(TARGET ${_t})
    add_dependencies(${_t} cxxbridge_gen)
  endif()
endforeach()

#=============================
# External library dependencies (depends/ static .pc on Linux/BSD;
# find_library() directly on Windows/macOS -- see FindDepPackage.cmake for why)
#=============================
include(FindDepPackage)

# Windows needs its own system libs for a fully static link (autotools'
# configure.ac AC_CHECK_LIB list for *mingw*): the winsock/COM/shell/crypto
# base set every executable links, plus -luserenv for librustzcash.
if(WIN32)
  set(WINDOWS_SYSTEM_LIBS
    mingwthrd kernel32 user32 gdi32 comdlg32 winspool winmm shell32 comctl32
    ole32 oleaut32 uuid rpcrt4 advapi32 ws2_32 mswsock ssp iphlpapi shlwapi
  )
else()
  set(WINDOWS_SYSTEM_LIBS "")
endif()

find_dep_package(libevent          PKGCONFIG_NAME libevent
  LIBRARY_NAMES event HEADER event2/event.h
  EXTRA_LIBS ${WINDOWS_SYSTEM_LIBS})
# libevent's pthreads shim does not exist on Windows, where libevent uses the
# native threading API instead. configure.ac guarded both its pkg-config and
# fallback checks with `if test x$TARGET_OS != xwindows`.
if(NOT WIN32)
  find_dep_package(libevent_pthreads PKGCONFIG_NAME libevent_pthreads
    LIBRARY_NAMES event_pthreads HEADER event2/thread.h)
endif()
find_dep_package(libssl            PKGCONFIG_NAME libssl
  LIBRARY_NAMES ssl HEADER openssl/ssl.h)
find_dep_package(libcrypto         PKGCONFIG_NAME libcrypto
  LIBRARY_NAMES crypto HEADER openssl/crypto.h
  EXTRA_LIBS ${WINDOWS_SYSTEM_LIBS})
find_dep_package(libsodium         PKGCONFIG_NAME libsodium
  LIBRARY_NAMES sodium HEADER sodium.h)
# zlib is a depends package in its own right, and both libcurl (gzip transfer
# encoding) and libarchive link against it. On Linux find_dep_package defers to
# pkg-config, which resolves that transitively; on macOS and Windows it builds
# the imported target by hand and carries no dependencies but these, so libcurl
# there fails to link with undefined _zlibVersion/_inflate/_uncompress.
find_dep_package(zlib              PKGCONFIG_NAME zlib
  LIBRARY_NAMES z HEADER zlib.h)
# CURL_STATICLIB: curl.h otherwise declares its API __declspec(dllimport) on
# Windows (the linkage a *dynamic* libcurl.dll needs), and depends builds
# libcurl statically -- linking then fails with undefined __imp_curl_*
# references. libcurl.pc's Cflags already carries this; EXTRA_DEFS repeats it
# for the Windows/macOS path, which has no .pc file to read it from.
find_dep_package(libcurl           PKGCONFIG_NAME libcurl
  LIBRARY_NAMES curl HEADER curl/curl.h
  EXTRA_LIBS PkgConfig::zlib ${WINDOWS_SYSTEM_LIBS}
  EXTRA_DEFS CURL_STATICLIB)
# libarchive's archive_string.o calls iconv_open/iconv/iconv_close. On Linux
# those are in glibc; macOS keeps them in a separate libiconv that has to be
# named explicitly.
if(APPLE)
  set(_libarchive_extra_libs PkgConfig::zlib iconv)
else()
  set(_libarchive_extra_libs PkgConfig::zlib)
endif()
find_dep_package(libarchive        PKGCONFIG_NAME libarchive
  LIBRARY_NAMES archive HEADER archive.h
  EXTRA_LIBS ${_libarchive_extra_libs})
if(ENABLE_ZMQ)
  find_dep_package(libzmq          PKGCONFIG_NAME libzmq
    LIBRARY_NAMES zmq HEADER zmq.h
    EXTRA_LIBS ${WINDOWS_SYSTEM_LIBS})
endif()
if(USE_QRCODE)
  find_dep_package(libqrencode     PKGCONFIG_NAME libqrencode
    LIBRARY_NAMES qrencode HEADER qrencode.h)
endif()

find_library(BDB_CXX_LIBRARY NAMES db_cxx-6.2 db_cxx db_cxx-6.3
  PATHS ${DEPENDS_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)

# librustzcash (a Rust staticlib) needs these system libs, per configure.ac's
# RUST_LIBS: -ldl on Linux, -luserenv on Windows.
if(WIN32)
  set(RUST_SYSTEM_LIBS userenv)
elseif(APPLE)
  # No librt on macOS: clock_gettime and friends live in libSystem. configure.ac
  # only ever linked -lrt under --enable-glibc-back-compat, which defaults off.
  #
  # CoreFoundation is required by librustzcash itself: chrono's
  # offset::local::inner::current_zone calls CFTimeZoneCopySystem and friends,
  # so without it every executable fails to link with undefined _CF* symbols.
  set(RUST_SYSTEM_LIBS dl pthread m "-framework CoreFoundation")
else()
  set(RUST_SYSTEM_LIBS dl pthread m rt)
endif()

# Aggregate matching autotools' LIBZCASH_LIBS = boost_system + crypto + sodium + RUST_LIBS.
add_library(bitcoin_zcash_libs INTERFACE)
target_link_libraries(bitcoin_zcash_libs INTERFACE
  Boost::system PkgConfig::libcrypto PkgConfig::libsodium ${RUST_SYSTEM_LIBS}
)

# TARGET_DARWIN's RUST_DARWIN_FRAMEWORKS (Makefile.am): librustzcash needs
# the Security framework for keychain-adjacent APIs on macOS.
#
# Linked as a plain flag rather than looked up with find_library(): the depends
# toolchain sets CMAKE_FIND_ROOT_PATH_MODE_LIBRARY to ONLY, which confines
# find_library() to the depends prefix, and the SDK frameworks live outside it.
# This is what the autotools build passed, too.
if(APPLE)
  set(SECURITY_FRAMEWORK "-framework Security")
else()
  set(SECURITY_FRAMEWORK "")
endif()

#=============================
# Executables
#=============================
set(_boost_all
  Boost::system Boost::filesystem Boost::thread Boost::chrono Boost::program_options
)

# Windows resource scripts (icons/version info), one per executable, matching
# each *_SOURCES += *-res.rc in src/Makefile.am. Each .rc #includes
# clientversion.h, which guards its C++ includes behind
# `#if !defined(WINDRES_PREPROC)` because windres's preprocessor has no C++
# standard library include path and chokes on <string>. Makefile.am's .rc.o
# rule passed -DWINDRES_PREPROC to windres for exactly this reason; the CMake
# port dropped it, so windres fell through to the real includes and failed
# with "fatal error: string: No such file or directory".
function(target_add_windows_resource target rc_file)
  if(WIN32)
    target_sources(${target} PRIVATE ${rc_file})
    set_source_files_properties(${rc_file} PROPERTIES COMPILE_DEFINITIONS "WINDRES_PREPROC")
  endif()
endfunction()

add_executable(pirate-cli bitcoin-cli.cpp)
target_add_windows_resource(pirate-cli bitcoin-cli-res.rc)
target_link_libraries(pirate-cli PRIVATE
  bitcoin_includes
  bitcoin_cli
  bitcoin_util
  bitcoin_zcash
  bitcoin_tls
  librustzcash
  bitcoin_cxxbridge
  bitcoin_crypto
  univalue
  bitcoin_zcash_libs
  ${_boost_all}
  PkgConfig::libssl PkgConfig::libcrypto
  PkgConfig::libevent
  ${WINDOWS_SYSTEM_LIBS} ${SECURITY_FRAMEWORK}
)

add_executable(pirate-tx komodo-tx.cpp)
target_add_windows_resource(pirate-tx bitcoin-tx-res.rc)
target_link_libraries(pirate-tx PRIVATE
  bitcoin_includes
  bitcoin_common
  bitcoin_util
  bitcoin_zcash
  bitcoin_tls
  librustzcash
  bitcoin_cxxbridge
  cryptoconditions
  secp256k1
  univalue
  bitcoin_crypto
  bitcoin_zcash_libs
  ${_boost_all}
  PkgConfig::libcrypto
  PkgConfig::libcurl
  ${WINDOWS_SYSTEM_LIBS} ${SECURITY_FRAMEWORK}
)

add_executable(pirated bitcoind.cpp)
target_add_windows_resource(pirated bitcoind-res.rc)
target_link_libraries(pirated PRIVATE
  bitcoin_includes
  bitcoin_server
  $<$<BOOL:${ENABLE_WALLET}>:bitcoin_wallet>
  $<$<BOOL:${ENABLE_ZMQ}>:bitcoin_zmq>
  bitcoin_common
  bitcoin_cli
  bitcoin_util
  bitcoin_zcash
  bitcoin_tls
  librustzcash
  bitcoin_cxxbridge
  cryptoconditions
  bitcoin_cjson
  secp256k1
  univalue
  leveldb
  memenv
  bitcoin_crypto
  bitcoin_zcash_libs
  ${_boost_all}
  ${BDB_CXX_LIBRARY}
  PkgConfig::libssl PkgConfig::libcrypto
  PkgConfig::libevent $<$<NOT:$<BOOL:${WIN32}>>:PkgConfig::libevent_pthreads>
  PkgConfig::libcurl PkgConfig::libarchive
  $<$<BOOL:${ENABLE_ZMQ}>:PkgConfig::libzmq>
  ${WINDOWS_SYSTEM_LIBS} ${SECURITY_FRAMEWORK}
)

#=============================
# pirate-gtest unit tests
#=============================
if(BUILD_GTEST)
  find_library(GTEST_LIB NAMES gtest PATHS ${DEPENDS_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)
  find_library(GMOCK_LIB NAMES gmock PATHS ${DEPENDS_PREFIX}/lib NO_DEFAULT_PATH REQUIRED)

  # JSON test vectors -> C-array headers (Makefile.gtest.include %.json.h rule).
  set(_gtest_json
    test/data/base58_encode_decode.json
    test/data/base58_keys_invalid.json
    test/data/script_valid.json
    test/data/script_invalid.json
    test/data/sapling_key_components.json
    test/data/merkle_roots.json
    test/data/merkle_roots_empty.json
    test/data/merkle_serialization.json
    test/data/merkle_witness_serialization.json
    test/data/merkle_path.json
    test/data/merkle_commitments.json
    test/data/merkle_roots_sapling.json
    test/data/merkle_roots_empty_sapling.json
    test/data/merkle_serialization_sapling.json
    test/data/merkle_witness_serialization_sapling.json
    test/data/merkle_path_sapling.json
    test/data/merkle_commitments_sapling.json
    test/data/sighash.json
    test/data/tx_valid.json
    test/data/tx_invalid.json
  )
  set(_gtest_json_h "")
  foreach(_j IN LISTS _gtest_json)
    set(_j_in ${CMAKE_CURRENT_SOURCE_DIR}/${_j})
    set(_j_out ${CMAKE_CURRENT_BINARY_DIR}/${_j}.h)
    get_filename_component(_j_dir ${_j_out} DIRECTORY)
    add_custom_command(
      OUTPUT ${_j_out}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${_j_dir}
      COMMAND ${CMAKE_COMMAND} -DJSON_IN=${_j_in} -DHEADER_OUT=${_j_out}
              -P ${PROJECT_SOURCE_DIR}/cmake/JsonToHeader.cmake
      DEPENDS ${_j_in} ${PROJECT_SOURCE_DIR}/cmake/JsonToHeader.cmake
      COMMENT "json2h ${_j}"
      VERBATIM
    )
    list(APPEND _gtest_json_h ${_j_out})
  endforeach()

  set(GTEST_SOURCES
    gtest/gtestutils.cpp
    gtest/json_test_vectors.cpp
    gtest/main.cpp
    gtest/notarisation_test_data.cpp
    gtest/test_addrman.cpp
    gtest/test_alerts.cpp
    gtest/test_allocator_bitcoin.cpp
    gtest/test_arith_uint256.cpp
    gtest/test_base32.cpp
    gtest/test_base58.cpp
    gtest/test_base64.cpp
    gtest/test_bech32.cpp
    gtest/test_bip32.cpp
    gtest/test_block_connect.cpp
    gtest/test_block.cpp
    gtest/test_bloom_bitcoin.cpp
    gtest/test_buffered_file.cpp
    gtest/test_chainparams.cpp
    gtest/test_checkblock.cpp
    gtest/test_checktransaction.cpp
    gtest/test_coinimport.cpp
    gtest/test_coins.cpp
    gtest/test_compress.cpp
    gtest/test_convertbits.cpp
    gtest/test_crypto_bitcoin.cpp
    gtest/test_cryptoconditions.cpp
    gtest/test_dbwrapper.cpp
    gtest/test_deprecation.cpp
    gtest/test_dos_bitcoin.cpp
    gtest/test_embeddedprocess.cpp
    gtest/test_equihash_bitcoin.cpp
    gtest/test_equihash.cpp
    gtest/test_eval_notarisation.cpp
    gtest/test_events.cpp
    gtest/test_getarg_bitcoin.cpp
    gtest/test-gmp-arith.cpp
    gtest/test_haraka_removal.cpp
    gtest/test_hash_bitcoin.cpp
    gtest/test_hex.cpp
    gtest/test_httprpc.cpp
    gtest/test_i2p.cpp
    gtest/test_init.cpp
    gtest/test_joinsplit.cpp
    gtest/test_key_bitcoin.cpp
    gtest/test_keys.cpp
    gtest/test_keystore.cpp
    gtest/test_kmd_feat.cpp
    gtest/test_legacy_events.cpp
    gtest/test_libzcash_utils.cpp
    gtest/test_main_bitcoin.cpp
    gtest/test_mempool_bitcoin.cpp
    gtest/test_mempool.cpp
    gtest/test_merkletree.cpp
    gtest/test_metrics.cpp
    gtest/test_miner.cpp
    gtest/test_mruset.cpp
    gtest/test_multisig_bitcoin.cpp
    gtest/test_netbase_bitcoin.cpp
    gtest/test_netbase_tests.cpp
    gtest/test_net_bitcoin.cpp
    gtest/test_notary.cpp
    gtest/test_noteencryption.cpp
    gtest/test_oldhash_removal.cpp
    gtest/test_params.cpp
    gtest/test_parse_args.cpp
    gtest/test_parse_notarisation.cpp
    gtest/test_paymentdisclosure.cpp
    gtest/test_pedersen_hash.cpp
    gtest/test_pmt_bitcoin.cpp
    gtest/test_policyestimator.cpp
    gtest/test_pow_bitcoin.cpp
    gtest/test_pow.cpp
    gtest/test_prevector_bitcoin.cpp
    gtest/test_raii_event.cpp
    gtest/test_random.cpp
    gtest/test_reverselock.cpp
    gtest/test_rpc_bitcoin.cpp
    gtest/test_rpc.cpp
    gtest/test_rpc_wallet_bitcoin.cpp
    gtest/test_sanity.cpp
    gtest/test_sapling_note.cpp
    gtest/test_scheduler_bitcoin.cpp
    gtest/test_script_bitcoin.cpp
    gtest/test_scriptnum.cpp
    gtest/test_script_p2pkh.cpp
    gtest/test_script_P2SH_bitcoin.cpp
    gtest/test_script_standard_tests.cpp
    gtest/test_serialize_bitcoin.cpp
    gtest/test_sha256compress_bitcoin.cpp
    gtest/test_sha256_crypto.cpp
    gtest/test_sighash_bitcoin.cpp
    gtest/test_sigopcount.cpp
    gtest/test_skiplist.cpp
    gtest/test_tautology.cpp
    gtest/test_torcontrol.cpp
    gtest/test_transaction_bitcoin.cpp
    gtest/test_transaction_builder.cpp
    gtest/test_txdb_subtrees.cpp
    gtest/test_txid.cpp
    gtest/test_uint256_bitcoin.cpp
    gtest/test_univalue.cpp
    gtest/test_upgrades.cpp
    gtest/test_util_bitcoin.cpp
    gtest/test_zip32.cpp
    wallet/gtest/test_crypter.cpp
    wallet/gtest/test_wallet.cpp
    wallet/gtest/test_wallet_encryption.cpp
    wallet/gtest/test_walletmanager.cpp
    wallet/gtest/test_wallet_zkeys.cpp
  )

  add_executable(pirate-gtest ${GTEST_SOURCES} ${_gtest_json_h})
  target_add_windows_resource(pirate-gtest gtest/pirate-gtest-res.rc)
  add_dependencies(pirate-gtest cxxbridge_gen)
  target_include_directories(pirate-gtest PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(pirate-gtest PRIVATE
    bitcoin_includes
    bitcoin_server
    $<$<BOOL:${ENABLE_WALLET}>:bitcoin_wallet>
    $<$<BOOL:${ENABLE_ZMQ}>:bitcoin_zmq>
    bitcoin_common
    bitcoin_cli
    bitcoin_util
    bitcoin_zcash
    bitcoin_tls
    librustzcash
    bitcoin_cxxbridge
    cryptoconditions
    bitcoin_cjson
    secp256k1
    univalue
    leveldb
    memenv
    bitcoin_crypto
    bitcoin_zcash_libs
    ${_boost_all}
    ${BDB_CXX_LIBRARY}
    ${GTEST_LIB} ${GMOCK_LIB}
    PkgConfig::libssl PkgConfig::libcrypto
    PkgConfig::libevent $<$<NOT:$<BOOL:${WIN32}>>:PkgConfig::libevent_pthreads>
    PkgConfig::libcurl PkgConfig::libarchive
    $<$<BOOL:${ENABLE_ZMQ}>:PkgConfig::libzmq>
    ${WINDOWS_SYSTEM_LIBS} ${SECURITY_FRAMEWORK}
  )
  add_test(NAME pirate-gtest COMMAND pirate-gtest)
endif()

#=============================
# Qt GUI
#=============================
if(BUILD_GUI)
  add_subdirectory(qt)
endif()

#=============================
# pirate-networking watchdog + embedded binary hashes
#=============================
if(ENABLE_EMBEDDED_NETWORKING_WATCHDOG)
  # Deliberately links no bitcoin library - see pirate-networking.cpp for why it
  # has to keep working after pirated has already crashed.
  add_executable(pirate-networking pirate-networking.cpp)
  target_link_libraries(pirate-networking PRIVATE core_interface)
  set(_networking_bin $<TARGET_FILE:pirate-networking>)
else()
  set(_networking_bin "")
endif()

# tor_process.cpp / i2pd_process.cpp / networking_watchdog.cpp #include this.
# It records the SHA256 of exactly the binaries this build ships, so
# embeddedprocess.cpp's FindBinary() can refuse a substituted sibling. An empty
# hash silently disables that check for its binary, so the inputs must be right.
set(_ebh_out ${CMAKE_CURRENT_BINARY_DIR}/embedded_binary_hashes.h)
add_custom_command(
  OUTPUT ${_ebh_out}
  COMMAND ${CMAKE_COMMAND}
          -DTOR_BIN=${TOR_BINARY_PATH}
          -DI2PD_BIN=${I2PD_BINARY_PATH}
          -DNETWORKING_BIN=${_networking_bin}
          -DSTRIP_TOOL=${CMAKE_STRIP}
          -DHEADER_OUT=${_ebh_out}
          -P ${PROJECT_SOURCE_DIR}/cmake/EmbeddedBinaryHashes.cmake
  DEPENDS ${PROJECT_SOURCE_DIR}/cmake/EmbeddedBinaryHashes.cmake
          $<$<BOOL:${TOR_BINARY_PATH}>:${TOR_BINARY_PATH}>
          $<$<BOOL:${I2PD_BINARY_PATH}>:${I2PD_BINARY_PATH}>
          $<$<BOOL:${ENABLE_EMBEDDED_NETWORKING_WATCHDOG}>:pirate-networking>
  COMMENT "Generating embedded_binary_hashes.h"
  VERBATIM
)
add_custom_target(embedded_binary_hashes DEPENDS ${_ebh_out})
# bitcoin_server holds the three .cpp files that include the header.
add_dependencies(bitcoin_server embedded_binary_hashes)

#=============================
# Install rules (packaging: build-deb.sh/build-zip.sh/gitian stage from
# these via `cmake --install`, mirroring what autotools' `make install` put
# under $(bindir))
#=============================
foreach(_exe pirated pirate-cli pirate-tx)
  if(TARGET ${_exe})
    install(TARGETS ${_exe} RUNTIME DESTINATION bin)
  endif()
endforeach()
if(TARGET pirate-qt)
  install(TARGETS pirate-qt RUNTIME DESTINATION bin)
endif()
if(TARGET pirate-networking)
  install(TARGETS pirate-networking RUNTIME DESTINATION bin)
endif()
# The bundled daemons ship under pirate-* names so they cannot collide with a
# separately-installed system tor/i2pd - see doc/tor.md and doc/i2p.md.
if(TOR_BINARY_PATH)
  install(PROGRAMS ${TOR_BINARY_PATH} DESTINATION bin
          RENAME pirate-tor${_embedded_bin_suffix})
endif()
if(I2PD_BINARY_PATH)
  install(PROGRAMS ${I2PD_BINARY_PATH} DESTINATION bin
          RENAME pirate-i2pd${_embedded_bin_suffix})
endif()
