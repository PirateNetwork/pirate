TEMPLATE = app
TARGET = PirateOceanGUI
VERSION = 0.4.0.0

INCLUDEPATH += src src\qt src\libsnark src\protobuf src\secp256k1 src\secp256k1\include src\leveldb\include src\leveldb\helpers\memenv src\leveldb src\univalue\include src\libevent\include src\libevent\compat src\cryptoconditions\include src\cryptoconditions src\cryptoconditions\src\asn

MINIUPNPC_INCLUDE_PATH = src\miniupnpc

windows:INCLUDEPATH += depends\libgmp_6.1.1_msvc14\include
windows:INCLUDEPATH += depends\BDB_6.2.32\include depends\db-6.2.23\build_windows
windows:INCLUDEPATH += depends\libsodium-1.0.15-msvc\include
windows:INCLUDEPATH += depends\boost_1_65_1 depends\boost_1_65_1\boost
windows:INCLUDEPATH += depends\pthreads-master
windows:INCLUDEPATH += depends\openssl-1.1.0f-vs2015\include64 depends\openssl\crypto

windows:BOOST_LIB_PATH = depends\boost_1_65_1\lib64-msvc-14.0
windows:OPENSSL_LIB_PATH = depends\openssl-1.1.0f-vs2015\lib64
RUST_LIB_PATH = depends\librustzcash-master\target\debug

QT_VERSION = 0x050908
QT += network widgets

DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0 ENABLE_WALLET ENABLE_MINING
DEFINES += BOOST_THREAD_USE_LIB BOOST_HAS_PTHREADS
DEFINES += _TIMESPEC_DEFINED BINARY_OUTPUT MONTGOMERY_OUTPUT NO_PT_COMPRESSION
DEFINES += MULTICORE NO_PROCPS NO_GTEST NO_DOCS STATIC NO_SUPERCOP
DEFINES += _AMD64_ _MT __STDC_FORMAT_MACROS __amd64__ __x86_64__ HAVE_WORKING_BOOST_SLEEP_FOR
DEFINES += HAVE_CONFIG_H HAVE_DECL_STRNLEN ENABLE_MODULE_ECDH ENABLE_MODULE_RECOVERY
DEFINES += CURVE_ALT_BN128 _REENTRANT __USE_MINGW_ANSI_STDIO=1 LEVELDB_ATOMIC_PRESENT

windows:DEFINES += _WINDOWS
windows:DEFINES +=  OS_WIN LEVELDB_PLATFORM_WINDOWS

CONFIG += no_include_pwd thread no_batch

MINGW_THREAD_BUGFIX = 0

Release:DESTDIR = release
Release:OBJECTS_DIR = build_release
Release:MOC_DIR = build_release
Release:RCC_DIR = build_release
Release:UI_DIR = build_release

Debug:DESTDIR = debug
Debug:OBJECTS_DIR = build_debug
Debug:MOC_DIR = build_debug
Debug:RCC_DIR = build_debug
Debug:UI_DIR = build_debug

# Mac: compile for maximum compatibility (10.5, 32-bit)
macx:QMAKE_CXXFLAGS += -mmacosx-version-min=10.5 -arch x86_64 -isysroot /Developer/SDKs/MacOSX10.5.sdk

!windows:!macx {
     # Linux: static link
     LIBS += -Wl,-Bstatic
#     LIBS += -Wl,-Bdynamic
}

!windows {
# for extra security against potential buffer overflows: enable GCCs Stack Smashing Protection
QMAKE_CXXFLAGS *= -fstack-protector-all --param ssp-buffer-size=1
QMAKE_LFLAGS *= -fstack-protector-all --param ssp-buffer-size=1
}

USE_UPNP=0
# use: qmake "USE_UPNP=1" ( enabled by default; default)
#  or: qmake "USE_UPNP=0" (disabled by default)
#  or: qmake "USE_UPNP=-" (not supported)
# miniupnpc (http://miniupnp.free.fr/files/) must be installed for support
contains(USE_UPNP, -) {
    message(Building without UPNP support)
} else {
    message(Building with UPNP support)
    count(USE_UPNP, 0) {
        USE_UPNP=1
    }
    DEFINES += USE_UPNP=$$USE_UPNP MINIUPNP_STATICLIB STATICLIB
    INCLUDEPATH += $$MINIUPNPC_INCLUDE_PATH

#    SOURCES += src\miniupnpc\connecthostport.c \
#    src\miniupnpc\igd_desc_parse.c \
#    src\miniupnpc\minisoap.c \
#    src\miniupnpc\minissdpc.c \
#    src\miniupnpc\miniupnpc.c \
#    src\miniupnpc\miniwget.c \
#    src\miniupnpc\minixml.c \
#    src\miniupnpc\portlistingparse.c \
#    src\miniupnpc\receivedata.c \
#    src\miniupnpc\upnpcommands.c \
#    src\miniupnpc\upnpdev.c \
#    src\miniupnpc\upnperrors.c \
#    src\miniupnpc\upnpreplyparse.c

#    windows:LIBS += -liphlpapi
}

# use: qmake "USE_DBUS=1" or qmake "USE_DBUS=0"
linux:count(USE_DBUS, 0) {
    USE_DBUS=1
}

contains(USE_DBUS, 1) {
    message(Building with DBUS (Freedesktop notifications) support)
    DEFINES += USE_DBUS
    QT += dbus
}

windows {
    # make an educated guess about what the ranlib command is called
    isEmpty(QMAKE_RANLIB) {
        QMAKE_RANLIB = $$replace(QMAKE_STRIP, strip, ranlib)
    }
    LIBS += -lshlwapi
}

# regenerate build.h
!windows|contains(USE_BUILD_INFO, 1) {
    genbuild.depends = FORCE
    genbuild.commands = cd $$PWD; /bin/sh share/genbuild.sh $$OUT_PWD/build/build.h
    genbuild.target = $$OUT_PWD/build/build.h
    PRE_TARGETDEPS += $$OUT_PWD/build/build.h
    QMAKE_EXTRA_TARGETS += genbuild
    DEFINES += HAVE_BUILD_INFO
}

contains(USE_O3, 1) {
    message(Building O3 optimization flag)
    QMAKE_CXXFLAGS_RELEASE -= -O2
    QMAKE_CFLAGS_RELEASE -= -O2
    QMAKE_CXXFLAGS += -O3
    QMAKE_CFLAGS += -O3
}

*-g++-32 {
    message("32 platform, adding -msse2 flag")

    QMAKE_CXXFLAGS += -msse2
    QMAKE_CFLAGS += -msse2
}

linux:QMAKE_CXXFLAGS_WARN_ON = -fdiagnostics-show-option -Wall -Wextra -Wno-ignored-qualifiers -Wformat -Wformat-security -Wno-unused-parameter -Wstack-protector

# Input
DEPENDPATH += .
HEADERS += src\komodo_globals.h \
     src\qt\pirateoceangui.h \
     src\qt\transactiontablemodel.h \
     src\qt\addresstablemodel.h \
     src\qt\zaddresstablemodel.h \
     src\qt\optionsdialog.h \
     src\qt\coincontroldialog.h \
     src\qt\coincontroltreewidget.h \
     src\qt\sendcoinsdialog.h \
     src\qt\zsendcoinsdialog.h \
     src\qt\addressbookpage.h \
     src\qt\zaddressbookpage.h \
     src\qt\signverifymessagedialog.h \
     src\qt\editaddressdialog.h \
     src\qt\editzaddressdialog.h \
     src\qt\komodoaddressvalidator.h \
     src\qt\clientmodel.h \
     src\qt\guiutil.h \
     src\qt\optionsmodel.h \
     src\qt\trafficgraphwidget.h \
     src\qt\transactiondesc.h \
     src\qt\transactiondescdialog.h \
     src\qt\komodoamountfield.h \
     src\qt\transactionfilterproxy.h \
     src\qt\transactionview.h \
     src\qt\walletmodel.h \
     src\qt\overviewpage.h \
     src\qt\csvmodelwriter.h \
     src\qt\sendcoinsentry.h \
     src\qt\qvalidatedlineedit.h \
     src\qt\komodounits.h \
     src\qt\qvaluecombobox.h \
     src\qt\askpassphrasedialog.h \
     src\qt\intro.h \
     src\qt\splashscreen.h \
     src\qt\utilitydialog.h \
     src\qt\notificator.h \
     src\qt\modaloverlay.h \
     src\qt\openuridialog.h \
     src\qt\walletframe.h \
     src\qt\paymentserver.h \
     src\qt\rpcconsole.h \
     src\qt\bantablemodel.h \
     src\qt\peertablemodel.h \
     src\qt\recentrequeststablemodel.h \
     src\qt\walletview.h \
     src\qt\receivecoinsdialog.h \
     src\qt\receiverequestdialog.h \
     src\streams.h \
     src\komodo_bitcoind.h \
     src\komodo_utils.h \
     src\komodo_notary.h \
     src\komodo_gateway.h

SOURCES += \ #src\protobuf\google\protobuf\any.cc \
#    src\protobuf\google\protobuf\arena.cc \
#    src\protobuf\google\protobuf\descriptor.cc \
#    src\protobuf\google\protobuf\descriptor.pb.cc \
#    src\protobuf\google\protobuf\descriptor_database.cc \
#    src\protobuf\google\protobuf\dynamic_message.cc \
#    src\protobuf\google\protobuf\extension_set.cc \
#    src\protobuf\google\protobuf\extension_set_heavy.cc \
#    src\protobuf\google\protobuf\generated_message_reflection.cc \
#    src\protobuf\google\protobuf\generated_message_util.cc \
#    src\protobuf\google\protobuf\io\coded_stream.cc \
#    src\protobuf\google\protobuf\io\strtod.cc \
#    src\protobuf\google\protobuf\io\tokenizer.cc \
#    src\protobuf\google\protobuf\io\zero_copy_stream.cc \
#    src\protobuf\google\protobuf\io\zero_copy_stream_impl.cc \
#    src\protobuf\google\protobuf\io\zero_copy_stream_impl_lite.cc \
#    src\protobuf\google\protobuf\map_field.cc \
#    src\protobuf\google\protobuf\message.cc \
#    src\protobuf\google\protobuf\message_lite.cc \
#    src\protobuf\google\protobuf\reflection_ops.cc \
#    src\protobuf\google\protobuf\repeated_field.cc \
#    src\protobuf\google\protobuf\stubs\atomicops_internals_x86_msvc.cc \
#    src\protobuf\google\protobuf\stubs\common.cc \
#    src\protobuf\google\protobuf\stubs\int128.cc \
#    src\protobuf\google\protobuf\stubs\io_win32.cc \
#    src\protobuf\google\protobuf\stubs\once.cc \
#    src\protobuf\google\protobuf\stubs\status.cc \
#    src\protobuf\google\protobuf\stubs\stringpiece.cc \
#    src\protobuf\google\protobuf\stubs\stringprintf.cc \
#    src\protobuf\google\protobuf\stubs\structurally_valid.cc \
#    src\protobuf\google\protobuf\stubs\strutil.cc \
#    src\protobuf\google\protobuf\stubs\substitute.cc \
#    src\protobuf\google\protobuf\text_format.cc \
#    src\protobuf\google\protobuf\unknown_field_set.cc \
#    src\protobuf\google\protobuf\wire_format.cc \
#    src\protobuf\google\protobuf\wire_format_lite.cc \
    src\addrdb.cpp \
    src\addrman.cpp \
    src\alert.cpp \
    src\amount.cpp \
    src\arith_uint256.cpp \
    src\asyncrpcoperation.cpp \
    src\asyncrpcqueue.cpp \
    src\base58.cpp \
    src\bech32.cpp \
    src\bloom.cpp \
    src\chain.cpp \
    src\chainparamsbase.cpp \
    src\checkpoints.cpp \
    src\clientversion.cpp \
    src\coins.cpp \
    src\compat\glibc_sanity.cpp \
    src\compat\glibcxx_sanity.cpp \
    src\compressor.cpp \
    src\core_read.cpp \
    src\core_write.cpp \
    src\crosschain.cpp \
    src\crypto\equihash.cpp \
    src\crypto\haraka.cpp \
    src\crypto\haraka_portable.cpp \
    src\crypto\hmac_sha256.cpp \
    src\crypto\hmac_sha512.cpp \
    src\crypto\ripemd160.cpp \
    src\crypto\sha1.cpp \
    src\crypto\sha256.cpp \
    src\crypto\sha512.cpp \
    src\crypto\verus_hash.cpp \
    src\fs.cpp \
    src\hash.cpp \
    src\httprpc.cpp \
    src\httpserver.cpp \
    src\init.cpp \
    src\importcoin.cpp \
    src\key.cpp \
    src\keystore.cpp \
    src\key_io.cpp \
    src\leveldb\db\builder.cc \
    src\leveldb\db\db_impl.cc \
    src\leveldb\db\db_iter.cc \
    src\leveldb\db\dbformat.cc \
    src\leveldb\db\filename.cc \
    src\leveldb\db\log_reader.cc \
    src\leveldb\db\log_writer.cc \
    src\leveldb\db\memtable.cc \
    src\leveldb\db\table_cache.cc \
    src\leveldb\db\version_edit.cc \
    src\leveldb\db\version_set.cc \
    src\leveldb\db\write_batch.cc \
    src\leveldb\helpers\memenv\memenv.cc \
    src\leveldb\port\port_posix_sse.cc \
    src\leveldb\port\port_win.cc \
    src\leveldb\table\block.cc \
    src\leveldb\table\block_builder.cc \
    src\leveldb\table\filter_block.cc \
    src\leveldb\table\format.cc \
    src\leveldb\table\iterator.cc \
    src\leveldb\table\merger.cc \
    src\leveldb\table\table.cc \
    src\leveldb\table\table_builder.cc \
    src\leveldb\table\two_level_iterator.cc \
    src\leveldb\util\arena_leveldb.cc \
    src\leveldb\util\bloom.cc \
    src\leveldb\util\cache.cc \
    src\leveldb\util\coding.cc \
    src\leveldb\util\comparator.cc \
    src\leveldb\util\crc32c.cc \
    src\leveldb\util\env.cc \
    src\leveldb\util\env_win.cc \
    src\leveldb\util\filter_policy.cc \
    src\leveldb\util\hash.cc \
    src\leveldb\util\logging.cc \
    src\leveldb\util\options.cc \
    src\leveldb\util\status_leveldb.cc \
    src\dbwrapper.cpp \
    src\libevent\buffer.c \
    src\libevent\buffer_iocp.c \
    src\libevent\bufferevent.c \
    src\libevent\bufferevent_async.c \
    src\libevent\bufferevent_ratelim.c \
    src\libevent\bufferevent_sock.c \
    src\libevent\event.c \
    src\libevent\event_iocp.c \
    src\libevent\evmap.c \
    src\libevent\evthread.c \
    src\libevent\evthread_win32.c \
    src\libevent\evutil.c \
    src\libevent\evutil_rand.c \
    src\libevent\evutil_time.c \
    src\libevent\http.c \
    src\libevent\listener.c \
    src\libevent\log.c \
    src\libevent\signal.c \
    src\libevent\strlcpy.c \
    src\libevent\win32select.c \
    src\libsnark\algebra\curves\alt_bn128\alt_bn128_g1.cpp \
    src\libsnark\algebra\curves\alt_bn128\alt_bn128_g2.cpp \
    src\libsnark\algebra\curves\alt_bn128\alt_bn128_init.cpp \
    src\libsnark\algebra\curves\alt_bn128\alt_bn128_pairing.cpp \
    src\libsnark\algebra\curves\alt_bn128\alt_bn128_pp.cpp \
    src\libsnark\common\profiling.cpp \
    src\libsnark\common\utils.cpp \
    src\main.cpp \
    src\merkleblock.cpp \
    src\metrics.cpp \
    src\miner.cpp \
    src\net.cpp \
    src\netbase.cpp \
    src\notarisationdb.cpp \
    src\policy\fees.cpp \
    src\policy\policy.cpp \
    src\pow.cpp \
    src\primitives\block.cpp \
    src\primitives\nonce.cpp \
    src\primitives\transaction.cpp \
    src\protocol.cpp \
    src\pubkey.cpp \
    src\qt\addressbookpage.cpp \
    src\qt\zaddressbookpage.cpp \
    src\qt\addresstablemodel.cpp \
    src\qt\zaddresstablemodel.cpp \
    src\qt\askpassphrasedialog.cpp \
    src\qt\bantablemodel.cpp \
    src\qt\clientmodel.cpp \
    src\qt\coincontroldialog.cpp \
    src\qt\coincontroltreewidget.cpp \
    src\qt\csvmodelwriter.cpp \
    src\qt\editaddressdialog.cpp \
    src\qt\editzaddressdialog.cpp \
    src\qt\guiutil.cpp \
    src\qt\intro.cpp \
    src\qt\komodo.cpp \
    src\qt\komodoaddressvalidator.cpp \
    src\qt\komodoamountfield.cpp \
    src\qt\pirateoceangui.cpp \
    src\qt\komodounits.cpp \
    src\qt\modaloverlay.cpp \
    src\qt\networkstyle.cpp \
    src\qt\notificator.cpp \
    src\qt\openuridialog.cpp \
    src\qt\optionsdialog.cpp \
    src\qt\optionsmodel.cpp \
    src\qt\overviewpage.cpp \
    src\qt\paymentrequest.pb.cc \
    src\qt\paymentrequestplus.cpp \
    src\qt\paymentserver.cpp \
    src\qt\peertablemodel.cpp \
    src\qt\platformstyle.cpp \
    src\qt\qvalidatedlineedit.cpp \
    src\qt\qvaluecombobox.cpp \
    src\qt\receivecoinsdialog.cpp \
    src\qt\receiverequestdialog.cpp \
    src\qt\recentrequeststablemodel.cpp \
    src\qt\rpcconsole.cpp \
    src\qt\sendcoinsdialog.cpp \
    src\qt\zsendcoinsdialog.cpp \
    src\qt\sendcoinsentry.cpp \
    src\qt\signverifymessagedialog.cpp \
    src\qt\splashscreen.cpp \
    src\qt\trafficgraphwidget.cpp \
    src\qt\transactiondesc.cpp \
    src\qt\transactiondescdialog.cpp \
    src\qt\transactionfilterproxy.cpp \
    src\qt\transactionrecord.cpp \
    src\qt\transactiontablemodel.cpp \
    src\qt\transactionview.cpp \
    src\qt\utilitydialog.cpp \
    src\qt\walletframe.cpp \
    src\qt\walletmodel.cpp \
    src\qt\walletmodeltransaction.cpp \
    src\qt\walletmodelztransaction.cpp \
    src\qt\walletview.cpp \
    src\qt\winshutdownmonitor.cpp \
    src\random.cpp \
    src\rest.cpp \
    src\rpc\blockchain.cpp \
    src\rpc\client.cpp \
    src\rpc\crosschain.cpp \
    src\rpc\mining.cpp \
    src\rpc\misc.cpp \
    src\rpc\net.cpp \
    src\rpc\protocol_rpc.cpp \
    src\rpc\rawtransaction.cpp \
    src\rpc\server.cpp \
    src\scheduler.cpp \
    src\script\interpreter.cpp \
    src\script\script.cpp \
    src\script\script_error.cpp \
    src\script\script_ext.cpp \
    src\script\serverchecker.cpp \
    src\script\sign.cpp \
    src\script\standard.cpp \
    src\secp256k1\src\secp256k1.c \
    src\sendalert.cpp \
    src\support\cleanse.cpp \
    src\support\pagelocker.cpp \
    src\sync.cpp \
    src\sys\time.cpp \
    src\threadinterrupt.cpp \
    src\timedata.cpp \
    src\torcontrol.cpp \
    src\transaction_builder.cpp \
    src\txdb.cpp \
    src\txmempool.cpp \
    src\uint256.cpp \
    src\univalue\lib\univalue.cpp \
    src\univalue\lib\univalue_read.cpp \
    src\univalue\lib\univalue_write.cpp \
    src\util.cpp \
    src\utilmoneystr.cpp \
    src\utilstrencodings.cpp \
    src\utiltest.cpp \
    src\utiltime.cpp \
    src\validationinterface.cpp \
    src\versionbits.cpp \
    src\wallet\asyncrpcoperation_sendmany.cpp \
    src\wallet\crypter.cpp \
    src\wallet\db.cpp \
    src\wallet\rpcdump.cpp \
    src\wallet\rpcwallet.cpp \
    src\wallet\wallet.cpp \
    src\wallet\wallet_fees.cpp \
    src\wallet\wallet_ismine.cpp \
    src\wallet\walletdb.cpp \
    src\zcash\Address.cpp \
    src\zcash\IncrementalMerkleTree.cpp \
    src\zcash\JoinSplit.cpp \
    src\zcash\Note.cpp \
    src\zcash\NoteEncryption.cpp \
    src\zcash\prf.cpp \
    src\zcash\Proof.cpp \
    src\zcash\util_zcash.cpp \
    src\zcash\zip32.cpp \
    src\zcbenchmarks.cpp \
    src\chainparams.cpp \
    src\cc\assets.cpp \
    src\cc\auction.cpp \
    src\cc\betprotocol.cpp \
    src\cc\channels.cpp \
    src\cc\CCassetstx.cpp \
    src\cc\CCassetsCore.cpp \
    src\cc\CCcustom.cpp \
    src\cc\CCtx.cpp \
    src\cc\CCutils.cpp \
    src\cc\dice.cpp \
    src\cc\eval.cpp \
    src\cc\faucet.cpp \
    src\cc\fsm.cpp \
    src\cc\gateways.cpp \
    src\cc\heir.cpp \
    src\cc\import.cpp \
    src\cc\lotto.cpp \
    src\cc\oracles.cpp \
    src\cc\payments.cpp \
    src\cc\pegs.cpp \
    src\cc\prices.cpp \
    src\cc\rewards.cpp \
    src\cc\triggers.cpp \
    src\consensus\upgrades.cpp \
    src\deprecation.cpp \
    src\paymentdisclosure.cpp \
    src\paymentdisclosuredb.cpp \
    src\script\cc.cpp \
    src\wallet\asyncrpcoperation_mergetoaddress.cpp \
    src\wallet\asyncrpcoperation_shieldcoinbase.cpp \
    src\wallet\rpcdisclosure.cpp \
    src\cryptoconditions\src\cryptoconditions.cpp \
    src\cryptoconditions\src\asn\Condition.c	\
    src\cryptoconditions\src\asn\SimpleSha256Condition.c	\
    src\cryptoconditions\src\asn\CompoundSha256Condition.c	\
    src\cryptoconditions\src\asn\ConditionTypes.c	\
    src\cryptoconditions\src\asn\Fulfillment.c	\
    src\cryptoconditions\src\asn\PreimageFulfillment.c	\
    src\cryptoconditions\src\asn\PrefixFulfillment.c	\
    src\cryptoconditions\src\asn\ThresholdFulfillment.c	\
    src\cryptoconditions\src\asn\RsaSha256Fulfillment.c	\
    src\cryptoconditions\src\asn\Ed25519Sha512Fulfillment.c	\
    src\cryptoconditions\src\asn\Secp256k1Fulfillment.c	\
    src\cryptoconditions\src\asn\EvalFulfillment.c	\
    src\cryptoconditions\src\asn\PrefixFingerprintContents.c	\
    src\cryptoconditions\src\asn\ThresholdFingerprintContents.c	\
    src\cryptoconditions\src\asn\RsaFingerprintContents.c	\
    src\cryptoconditions\src\asn\Ed25519FingerprintContents.c	\
    src\cryptoconditions\src\asn\Secp256k1FingerprintContents.c \
    src\cryptoconditions\src\asn\INTEGER.c \
    src\cryptoconditions\src\asn\NativeEnumerated.c \
    src\cryptoconditions\src\asn\NativeInteger.c \
    src\cryptoconditions\src\asn\asn_SET_OF.c \
    src\cryptoconditions\src\asn\constr_CHOICE.c \
    src\cryptoconditions\src\asn\constr_SEQUENCE.c \
    src\cryptoconditions\src\asn\constr_SET_OF.c \
    src\cryptoconditions\src\asn\OCTET_STRING.c \
    src\cryptoconditions\src\asn\BIT_STRING.c \
    src\cryptoconditions\src\asn\asn_codecs_prim.c \
    src\cryptoconditions\src\asn\ber_tlv_length.c \
    src\cryptoconditions\src\asn\ber_tlv_tag.c \
    src\cryptoconditions\src\asn\ber_decoder.c \
    src\cryptoconditions\src\asn\der_encoder.c \
    src\cryptoconditions\src\asn\constr_TYPE.c \
    src\cryptoconditions\src\asn\constraints.c \
    src\cryptoconditions\src\asn\xer_support.c \
    src\cryptoconditions\src\asn\xer_decoder.c \
    src\cryptoconditions\src\asn\xer_encoder.c \
    src\cryptoconditions\src\asn\per_support.c \
    src\cryptoconditions\src\asn\per_decoder.c \
    src\cryptoconditions\src\asn\per_encoder.c \
    src\cryptoconditions\src\asn\per_opentype.c \
    src\cryptoconditions\src\cryptoconditions_utils.cpp \
    src\cryptoconditions\src\include\cryptoconditions_sha256.c \
    src\cryptoconditions\src\include\ed25519\src\keypair.cpp \
    src\cryptoconditions\src\include\ed25519\src\cryptoconditions_sign.cpp \
    src\cryptoconditions\src\include\ed25519\src\verify.cpp \
    src\cryptoconditions\src\include\ed25519\src\cryptoconditions_sha512.cpp \
    src\cryptoconditions\src\include\ed25519\src\ge.cpp \
    src\cryptoconditions\src\include\ed25519\src\sc.cpp \
    src\cryptoconditions\src\include\ed25519\src\fe.cpp

RESOURCES += \
    src\qt\komodo.qrc \
    src\qt\komodo_locale.qrc

FORMS += \
    src\qt\forms\coincontroldialog.ui \
    src\qt\forms\sendcoinsdialog.ui \
    src\qt\forms\zsendcoinsdialog.ui \
    src\qt\forms\addressbookpage.ui \
    src\qt\forms\zaddressbookpage.ui \
    src\qt\forms\signverifymessagedialog.ui \
    src\qt\forms\editaddressdialog.ui \
    src\qt\forms\editzaddressdialog.ui \
    src\qt\forms\transactiondescdialog.ui \
    src\qt\forms\overviewpage.ui \
    src\qt\forms\sendcoinsentry.ui \
    src\qt\forms\askpassphrasedialog.ui \
    src\qt\forms\debugwindow.ui \
    src\qt\forms\helpmessagedialog.ui \
    src\qt\forms\intro.ui \
    src\qt\forms\modaloverlay.ui \
    src\qt\forms\openuridialog.ui \
    src\qt\forms\receivecoinsdialog.ui \
    src\qt\forms\receiverequestdialog.ui \
    src\qt\forms\optionsdialog.ui

CODECFORTR = UTF-8

# for lrelease/lupdate
# also add new translations to komodo.qrc under translations/
TRANSLATIONS = $$files(src/qt/locale/komodo_*.ts)

isEmpty(QMAKE_LRELEASE) {
    windows:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]\\lrelease.exe
    else:QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
}
isEmpty(QM_DIR):QM_DIR = $$PWD/locale
# automatically build translations, so they can be included in resource file
TSQM.name = lrelease ${QMAKE_FILE_IN}
TSQM.input = TRANSLATIONS
TSQM.output = $$QM_DIR/${QMAKE_FILE_BASE}.qm
TSQM.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
TSQM.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += TSQM

# "Other files" to show in Qt Creator
OTHER_FILES += \
    doc/*.rst doc/*.txt doc/README README.md src/qt/res/komodo-qt-res.rc

# platform specific defaults, if not overridden on command line
isEmpty(BOOST_LIB_SUFFIX) {
    macx:BOOST_LIB_SUFFIX = -mt
    Release:windows:BOOST_LIB_SUFFIX = -vc140-mt-1_65_1
    Debug:windows:BOOST_LIB_SUFFIX = -vc140-mt-gd-1_65_1
}

isEmpty(BOOST_THREAD_LIB_SUFFIX) {
    windows:BOOST_THREAD_LIB_SUFFIX = $$BOOST_LIB_SUFFIX
    else:BOOST_THREAD_LIB_SUFFIX = $$BOOST_LIB_SUFFIX
}

windows:RC_FILE = src/qt/res/komodo-qt-res.rc

windows:!contains(MINGW_THREAD_BUGFIX, 0) {
    # At least qmake's win32-g++-cross profile is missing the -lmingwthrd
    # thread-safety flag. GCC has -mthreads to enable this, but it doesn't
    # work with static linking. -lmingwthrd must come BEFORE -lmingw, so
    # it is prepended to QMAKE_LIBS_QT_ENTRY.
    # It can be turned off with MINGW_THREAD_BUGFIX=0, just in case it causes
    # any problems on some untested qmake profile now or in the future.
    DEFINES += _MT BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN
    QMAKE_LIBS_QT_ENTRY = -lmingwthrd $$QMAKE_LIBS_QT_ENTRY
}

macx:HEADERS += macdockiconhandler.h macnotificationhandler.h
macx:OBJECTIVE_SOURCES += macdockiconhandler.mm macnotificationhandler.mm
macx:LIBS += -framework Foundation -framework ApplicationServices -framework AppKit
macx:DEFINES += MAC_OSX MSG_NOSIGNAL=0
macx:ICON = src/qt/res/icons/pirate.icns
macx:TARGET = "PirateOceanGUI"
macx:QMAKE_CFLAGS_THREAD += -pthread
macx:QMAKE_CXXFLAGS_THREAD += -pthread
macx:QMAKE_LFLAGS_THREAD += -pthread

# Set libraries and includes at end, to use platform-defined defaults if not overridden
LIBS += $$join(BOOST_LIB_PATH,,-L,) $$join(OPENSSL_LIB_PATH,,-L,) $$join(RUST_LIB_PATH,,-L,)
LIBS += $$BDB_LIB_SUFFIX
windows:LIBS += -lws2_32 -lshlwapi -lmswsock -lole32 -loleaut32 -luuid -lgdi32 -luser32 -luserenv
LIBS += -lboost_system$$BOOST_LIB_SUFFIX -lboost_filesystem$$BOOST_LIB_SUFFIX -lboost_program_options$$BOOST_LIB_SUFFIX -lboost_thread$$BOOST_THREAD_LIB_SUFFIX
windows:LIBS += -lboost_chrono$$BOOST_LIB_SUFFIX
LIBS += -lrustzcash

Release:LIBS += -ldepends\libgmp_6.1.1_msvc14\lib\x64\gmp
Release:LIBS += -ldepends\libsodium-1.0.15-msvc\x64\Release\v140\dynamic\libsodium
Release:LIBS += -ldepends\libcurl-master\lib\dll-release-x64\libcurl
Release:LIBS += -ldepends\db-6.2.23\build_windows\x64\Release\libdb62
Release:LIBS += -llibcryptoMD -llibsslMD
Release:LIBS += -ldepends\pthreads-master\dll\x64\Release\pthreads

Debug:LIBS += -ldepends\libgmp_6.1.1_msvc14\lib\x64\gmp
Debug:LIBS += -ldepends\libsodium-1.0.15-msvc\x64\Debug\v140\dynamic\libsodium
Debug:LIBS += -ldepends\libcurl-master\lib\dll-debug-x64\libcurl_debug
Debug:LIBS += -ldepends\db-6.2.23\build_windows\x64\Debug\libdb62d
Debug:LIBS += -llibcryptoMDd -llibsslMDd
Debug:LIBS += -ldepends\pthreads-master\dll\x64\Debug\pthreads

!windows:!macx {
    DEFINES += LINUX
    LIBS += -lrt -ldl
}

#QMAKE_CXXFLAGS += -O2 -bigobj -Zp8 -GS -wd4800 -wd4100 -wd4267 -wd4244 -wd4101 -w14100 -wd4146 -wd4189 -wd4018 -wd4290 -wd4334 -wd4996
#QMAKE_CFLAGS += -O2 -bigobj -Zp8 -GS -wd4800 -wd4100 -wd4267 -wd4244 -wd4101 -w14100 -wd4146 -wd4189 -wd4018 -wd4290 -wd4334 -wd4996

#QMAKE_CXXFLAGS_RELEASE += $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
#QMAKE_LFLAGS_RELEASE += $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO

system($$QMAKE_LRELEASE -silent $$_PRO_FILE_)
