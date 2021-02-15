TEMPLATE = app
TARGET = PirateOceanGUI
VERSION = 0.4.0.0



INCLUDEPATH += \
            $$PWD/src \
            $$PWD/src/qt \
            $$PWD/src/qt/include \
            $$PWD/src/config \
            $$PWD/src/secp256k1/include \
            $$PWD/src/cc/includes \
            $$PWD/src/cryptoconditions/include \
            $$PWD/src/cryptoconditions/src \
            $$PWD/src/cryptoconditions/src/asn \
            $$PWD/src/snark \
            $$PWD/src/snark/libsnark \
            $$PWD/src/univalue/include \
            $$PWD/src/leveldb/include \
            $$PWD/src/leveldb/helpers/memenv \
            $$PWD/src/leveldb \
#            $$PWD/depends/x86_64-unknown-linux-gnu/include \
#            $$PWD/depends/x86_64-unknown-linux-gnu/lib

MINIUPNPC_INCLUDE_PATH = src\miniupnpc

MOC_DIR = moc
OBJECTS_DIR = bin
UI_DIR = ui

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
Release:DIR = build_release
Release:RCC_DIR = build_release
Release:UI_DIR = build_release

Debug:DESTDIR = debug
Debug:OBJECTS_DIR = build_debug
Debug:DIR = build_debug
Debug:RCC_DIR = build_debug
Debug:UI_DIR = build_debug

# Mac: compile for maximum compatibility (10.5, 32-bit)
macx:QMAKE_CXXFLAGS += -mmacosx-version-min=10.5 -arch x86_64 -isysroot /Developer/SDKs/MacOSX10.5.sdk

#!windows:!macx {
#     # Linux: static link
#     LIBS += -Wl,-Bstatic
##     LIBS += -Wl,-Bdynamic
#}

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

HEADERS += \
     src/qt/addressbookpage.h \
     src/qt/addresstablemodel.h \
     src/qt/askpassphrasedialog.h \
     src/qt/bantablemodel.h \
     src/qt/callback.h \
     src/qt/clientmodel.h \
     src/qt/coincontroldialog.h \
     src/qt/coincontroltreewidget.h \
     src/qt/csvmodelwriter.h \
     src/qt/editaddressdialog.h \
     src/qt/editzaddressdialog.h \
     src/qt/guiconstants.h \
     src/qt/guiutil.h \
     src/qt/intro.h \
     src/qt/komodoaddressvalidator.h \
     src/qt/komodoamountfield.h \
     src/qt/komodounits.h \
     src/qt/macdockiconhandler.h \
     src/qt/macnotificationhandler.h \
     src/qt/modaloverlay.h \
     src/qt/networkstyle.h \
     src/qt/notificator.h \
     src/qt/openuridialog.h \
     src/qt/optionsdialog.h \
     src/qt/optionsmodel.h \
     src/qt/overviewpage.h \
     src/qt/paymentserver.h \
     src/qt/peertablemodel.h \
     src/qt/pirateoceangui.h \
     src/qt/platformstyle.h \
     src/qt/precompiled.h \
     src/qt/qvalidatedlineedit.h \
     src/qt/qvaluecombobox.h \
     src/qt/receivecoinsdialog.h \
     src/qt/receiverequestdialog.h \
     src/qt/recentrequeststablemodel.h \
     src/qt/rpcconsole.h \
     src/qt/sendcoinsdialog.h \
     src/qt/sendcoinsentry.h \
     src/qt/signverifymessagedialog.h \
     src/qt/splashscreen.h \
     src/qt/trafficgraphwidget.h \
     src/qt/transactiondesc.h \
     src/qt/transactiondescdialog.h \
     src/qt/transactionfilterproxy.h \
     src/qt/transactionrecord.h \
     src/qt/transactiontablemodel.h \
     src/qt/transactionview.h \
     src/qt/utilitydialog.h \
     src/qt/walletframe.h \
     src/qt/walletmodel.h \
     src/qt/walletmodeltransaction.h \
     src/qt/walletmodelztransaction.h \
     src/qt/walletview.h \
     src/qt/winshutdownmonitor.h \
     src/qt/zaddressbookpage.h \
     src/qt/zaddresstablemodel.h \
     src/qt/zsendcoinsdialog.h \

SOURCES += \
    src/qt/addressbookpage.cpp \
    src/qt/addresstablemodel.cpp \
    src/qt/askpassphrasedialog.cpp \
    src/qt/bantablemodel.cpp \
    src/qt/clientmodel.cpp \
    src/qt/coincontroldialog.cpp \
    src/qt/coincontroltreewidget.cpp \
    src/qt/csvmodelwriter.cpp \
    src/qt/editaddressdialog.cpp \
    src/qt/editzaddressdialog.cpp \
    src/qt/guiutil.cpp \
    src/qt/intro.cpp \
    src/qt/komodo.cpp \
    src/qt/komodoaddressvalidator.cpp \
    src/qt/komodoamountfield.cpp \
    src/qt/komodostrings.cpp \
    src/qt/komodounits.cpp \
    src/qt/modaloverlay.cpp \
    src/qt/networkstyle.cpp \
    src/qt/notificator.cpp \
    src/qt/openuridialog.cpp \
    src/qt/optionsdialog.cpp \
    src/qt/optionsmodel.cpp \
    src/qt/overviewpage.cpp \
#    src/qt/paymentrequestplus.cpp \
    src/qt/paymentserver.cpp \
    src/qt/peertablemodel.cpp \
    src/qt/pirateoceangui.cpp \
    src/qt/platformstyle.cpp \
    src/qt/qvalidatedlineedit.cpp \
    src/qt/qvaluecombobox.cpp \
    src/qt/receivecoinsdialog.cpp \
    src/qt/receiverequestdialog.cpp \
    src/qt/recentrequeststablemodel.cpp \
    src/qt/rpcconsole.cpp \
    src/qt/sendcoinsdialog.cpp \
    src/qt/sendcoinsentry.cpp \
    src/qt/signverifymessagedialog.cpp \
    src/qt/splashscreen.cpp \
    src/qt/trafficgraphwidget.cpp \
    src/qt/transactiondesc.cpp \
    src/qt/transactiondescdialog.cpp \
    src/qt/transactionfilterproxy.cpp \
    src/qt/transactionrecord.cpp \
    src/qt/transactiontablemodel.cpp \
    src/qt/transactionview.cpp \
    src/qt/utilitydialog.cpp \
    src/qt/walletframe.cpp \
    src/qt/walletmodel.cpp \
    src/qt/walletmodeltransaction.cpp \
    src/qt/walletmodelztransaction.cpp \
    src/qt/walletview.cpp \
    src/qt/winshutdownmonitor.cpp \
    src/qt/zaddressbookpage.cpp \
    src/qt/zaddresstablemodel.cpp \
    src/qt/zsendcoinsdialog.cpp \

RESOURCES += \
    src/qt/komodo.qrc \
    src/qt/komodo_locale.qrc

FORMS += \
    src/qt/forms/ImportSKdialog.ui \
    src/qt/forms/ImportVKdialog.ui \
    src/qt/forms/addressbookpage.ui \
    src/qt/forms/askpassphrasedialog.ui \
    src/qt/forms/coincontroldialog.ui \
    src/qt/forms/editaddressdialog.ui \
    src/qt/forms/helpmessagedialog.ui \
    src/qt/forms/intro.ui \
    src/qt/forms/importSKdialog.ui \
    src/qt/forms/importVKdialog.ui \
    src/qt/forms/modaloverlay.ui \
    src/qt/forms/openuridialog.ui \
    src/qt/forms/optionsdialog.ui \
    src/qt/forms/overviewpage.ui \
    src/qt/forms/receivecoinsdialog.ui \
    src/qt/forms/receiverequestdialog.ui \
    src/qt/forms/debugwindow.ui \
    src/qt/forms/sendcoinsdialog.ui \
    src/qt/forms/zsendcoinsdialog.ui \
    src/qt/forms/sendcoinsentry.ui \
    src/qt/forms/signverifymessagedialog.ui \
    src/qt/forms/transactiondescdialog.ui \
    src/qt/forms/zaddressbookpage.ui \
    src/qt/forms/editzaddressdialog.ui

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
#windows:LIBS += -lws2_32 -lshlwapi -lmswsock -lole32 -loleaut32 -luuid -lgdi32 -luser32 -luserenv
LIBS += -lboost_system$$BOOST_LIB_SUFFIX -lboost_filesystem$$BOOST_LIB_SUFFIX -lboost_program_options$$BOOST_LIB_SUFFIX -lboost_thread$$BOOST_THREAD_LIB_SUFFIX
#windows:LIBS += -lboost_chrono$$BOOST_LIB_SUFFIX




LIBS += -L$$PWD/src -lbitcoin_server
LIBS += -L$$PWD/src -lbitcoin_zmq
LIBS += -L$$PWD/src -lbitcoin_wallet
LIBS += -L$$PWD/src -lbitcoin_cli
LIBS += -L$$PWD/src -lbitcoin_common
LIBS += -L$$PWD/src -lbitcoin_util


LIBS += -L$$PWD/src/crypto -lverus_portable_crypto
LIBS += -L$$PWD/src/crypto -lverus_crypto
LIBS += -L$$PWD/src/crypto -lbitcoin_crypto

LIBS += -L$$PWD/src -lzcash

LIBS += -L$$PWD/src/cryptoconditions/.libs/ -lcryptoconditions_core
LIBS += -L$$PWD/src/secp256k1/.libs/ -lsecp256k1

LIBS += -L$$PWD/src/univalue/.libs -lunivalue
LIBS += -L$$PWD/src/leveldb/ -lleveldb
LIBS += -L$$PWD/src/leveldb/ -lmemenv

LIBS += -L$$PWD/src/snark/ -lsnark


LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb-6.2
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb_cxx-6.2
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb_cxx
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqpid-proton-cpp-static
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqpid-proton-static
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqpid-proton-core-static
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lrustzcash

#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lxcb-static

#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqtlibpng
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqtharfbuzz
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lXau
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lexpat
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lz
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lprotobuf
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lprotobuf-lite
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lqrencode
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lcurl
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgmock
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgtest
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lsodium
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgmp
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgmpxx
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lzmq
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -levent
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -levent_core
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -levent_extra
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -levent_pthreads
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lssl
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lcrypto
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_filesystem
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_prg_exec_monitor
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_program_options
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_system
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_test_exec_monitor
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_thread
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_unit_test_framework
LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lboost_chrono



LIBS += -lanl -lgomp

#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lrustzcash
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lsodium
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgmp
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lgmpxx
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -ldb_cxx
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -levent
#LIBS += -L$$PWD/depends/x86_64-unknown-linux-gnu/lib -lcrypto


#Release:LIBS += -ldepends\libgmp_6.1.1_msvc14\lib\x64\gmp
#Release:LIBS += -ldepends\libsodium-1.0.15-msvc\x64\Release\v140\dynamic\libsodium
#Release:LIBS += -ldepends\libcurl-master\lib\dll-release-x64\libcurl
#Release:LIBS += -ldepends\db-6.2.23\build_windows\x64\Release\libdb62
#Release:LIBS += -llibcryptoMD -llibsslMD
#Release:LIBS += -ldepends\pthreads-master\dll\x64\Release\pthreads

#Debug:LIBS += -ldepends\libgmp_6.1.1_msvc14\lib\x64\gmp
#Debug:LIBS += -ldepends\libsodium-1.0.15-msvc\x64\Debug\v140\dynamic\libsodium
#Debug:LIBS += -ldepends\libcurl-master\lib\dll-debug-x64\libcurl_debug
#Debug:LIBS += -ldepends\db-6.2.23\build_windows\x64\Debug\libdb62d
#Debug:LIBS += -llibcryptoMDd -llibsslMDd
#Debug:LIBS += -ldepends\pthreads-master\dll\x64\Debug\pthreads

!windows:!macx {
    DEFINES += LINUX
    LIBS += -lrt -ldl
}

#QMAKE_CXXFLAGS += -O2 -bigobj -Zp8 -GS -wd4800 -wd4100 -wd4267 -wd4244 -wd4101 -w14100 -wd4146 -wd4189 -wd4018 -wd4290 -wd4334 -wd4996
#QMAKE_CFLAGS += -O2 -bigobj -Zp8 -GS -wd4800 -wd4100 -wd4267 -wd4244 -wd4101 -w14100 -wd4146 -wd4189 -wd4018 -wd4290 -wd4334 -wd4996

QMAKE_CXXFLAGS_RELEASE += $$QMAKE_CFLAGS_RELEASE_WITH_DEBUGINFO
QMAKE_LFLAGS_RELEASE += $$QMAKE_LFLAGS_RELEASE_WITH_DEBUGINFO

system($$QMAKE_LRELEASE -silent $$_PRO_FILE_)
