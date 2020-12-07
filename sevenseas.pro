#-------------------------------------------------
#
# Project created by QtCreator 2018-10-05T09:54:45
#
#-------------------------------------------------

QT       += core gui network

#CONFIG += precompile_header

#PRECOMPILED_HEADER = src/qt/precompiled.h

QT += widgets
QT += websockets

TARGET = PirateOcean

TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
#DEFINES += \
#    QT_DEPRECATED_WARNINGS

INCLUDEPATH += \
            $$PWD/src \
            $$PWD/src/qt \
            $$PWD/src/compat \
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
            $$PWD/depends/x86_64-unknown-linux-gnu/include \
            $$PWD/depends/x86_64-unknown-linux-gnu/lib



#RESOURCES     = src/qt/application.qrc

MOC_DIR = src/qt/bin
OBJECTS_DIR = src/qt/bin
UI_DIR = src/qt

CONFIG += cxx11_futurev Q_CLANG_QDOC

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
src/qt/paymentrequestplus.cpp \
src/qt/paymentserver.cpp \
src/qt/peertablemodel.cpp \
src/qt/pirateoceangui.cpp \
src/qt/platformstyle.cpp \
src/qt/qrc_komodo_locale.cpp \
src/qt/qrc_komodo.cpp \
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

HEADERS += \
    src/qt/addressbookpage.h \
    src/qt/zaddressbookpage.h \
    src/qt/addresstablemodel.h \
    src/qt/zaddresstablemodel.h \
    src/qt/askpassphrasedialog.h \
    src/qt/bantablemodel.h \
    src/qt/komodoaddressvalidator.h \
    src/qt/komodoamountfield.h \
    src/qt/pirateoceangui.h \
    src/qt/komodounits.h \
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
    src/qt/macdockiconhandler.h \
    src/qt/macnotificationhandler.h \
    src/qt/modaloverlay.h \
    src/qt/networkstyle.h \
    src/qt/notificator.h \
    src/qt/openuridialog.h \
    src/qt/optionsdialog.h \
    src/qt/optionsmodel.h \
    src/qt/overviewpage.h \
    src/qt/paymentrequestplus.h \
    src/qt/paymentserver.h \
    src/qt/peertablemodel.h \
    src/qt/platformstyle.h \
    src/qt/qvalidatedlineedit.h \
    src/qt/qvaluecombobox.h \
    src/qt/receivecoinsdialog.h \
    src/qt/receiverequestdialog.h \
    src/qt/recentrequeststablemodel.h \
    src/qt/rpcconsole.h \
    src/qt/sendcoinsdialog.h \
    src/qt/zsendcoinsdialog.h \
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
    src/qt/walletview.h \
    src/qt/winshutdownmonitor.h \

FORMS += \
    src/qt/forms/addressbookpage.ui \
    src/qt/forms/askpassphrasedialog.ui \
    src/qt/forms/coincontroldialog.ui \
    src/qt/forms/editaddressdialog.ui \
    src/qt/forms/helpmessagedialog.ui \
    src/qt/forms/intro.ui \
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


TRANSLATIONS += \
    src/qt/locale/komodo_af.ts \
    src/qt/locale/komodo_af_ZA.ts \
    src/qt/locale/komodo_ar.ts \
    src/qt/locale/komodo_be_BY.ts \
    src/qt/locale/komodo_bg_BG.ts \
    src/qt/locale/komodo_bg.ts \
    src/qt/locale/komodo_ca_ES.ts \
    src/qt/locale/komodo_ca.ts \
    src/qt/locale/komodo_ca@valencia.ts \
    src/qt/locale/komodo_cs.ts \
    src/qt/locale/komodo_cy.ts \
    src/qt/locale/komodo_da.ts \
    src/qt/locale/komodo_de.ts \
    src/qt/locale/komodo_el_GR.ts \
    src/qt/locale/komodo_el.ts \
    src/qt/locale/komodo_en_GB.ts \
    src/qt/locale/komodo_en.ts \
    src/qt/locale/komodo_eo.ts \
    src/qt/locale/komodo_es_AR.ts \
    src/qt/locale/komodo_es_CL.ts \
    src/qt/locale/komodo_es_CO.ts \
    src/qt/locale/komodo_es_DO.ts \
    src/qt/locale/komodo_es_ES.ts \
    src/qt/locale/komodo_es_MX.ts \
    src/qt/locale/komodo_es.ts \
    src/qt/locale/komodo_es_UY.ts \
    src/qt/locale/komodo_es_VE.ts \
    src/qt/locale/komodo_et_EE.ts \
    src/qt/locale/komodo_et.ts \
    src/qt/locale/komodo_eu_ES.ts \
    src/qt/locale/komodo_fa_IR.ts \
    src/qt/locale/komodo_fa.ts \
    src/qt/locale/komodo_fi.ts \
    src/qt/locale/komodo_fr_CA.ts \
    src/qt/locale/komodo_fr_FR.ts \
    src/qt/locale/komodo_fr.ts \
    src/qt/locale/komodo_gl.ts \
    src/qt/locale/komodo_he.ts \
    src/qt/locale/komodo_hi_IN.ts \
    src/qt/locale/komodo_hr.ts \
    src/qt/locale/komodo_hu.ts \
    src/qt/locale/komodo_id_ID.ts \
    src/qt/locale/komodo_it_IT.ts \
    src/qt/locale/komodo_it.ts \
    src/qt/locale/komodo_ja.ts \
    src/qt/locale/komodo_ka.ts \
    src/qt/locale/komodo_kk_KZ.ts \
    src/qt/locale/komodo_ko_KR.ts \
    src/qt/locale/komodo_ku_IQ.ts \
    src/qt/locale/komodo_ky.ts \
    src/qt/locale/komodo_la.ts \
    src/qt/locale/komodo_lt.ts \
    src/qt/locale/komodo_lv_LV.ts \
    src/qt/locale/komodo_mk_MK.ts \
    src/qt/locale/komodo_mn.ts \
    src/qt/locale/komodo_ms_MY.ts \
    src/qt/locale/komodo_nb.ts \
    src/qt/locale/komodo_ne.ts \
    src/qt/locale/komodo_nl.ts \
    src/qt/locale/komodo_pam.ts \
    src/qt/locale/komodo_pl.ts \
    src/qt/locale/komodo_pt_BR.ts \
    src/qt/locale/komodo_pt_PT.ts \
    src/qt/locale/komodo_ro_RO.ts \
    src/qt/locale/komodo_ro.ts \
    src/qt/locale/komodo_ru_RU.ts \
    src/qt/locale/komodo_ru.ts \
    src/qt/locale/komodo_sk.ts \
    src/qt/locale/komodo_sl_SI.ts \
    src/qt/locale/komodo_sq.ts \
    src/qt/locale/komodo_sr@latin.ts \
    src/qt/locale/komodo_sr.ts \
    src/qt/locale/komodo_sv.ts \
    src/qt/locale/komodo_ta.ts \
    src/qt/locale/komodo_th_TH.ts \
    src/qt/locale/komodo_tr_TR.ts \
    src/qt/locale/komodo_tr.ts \
    src/qt/locale/komodo_uk.ts \
    src/qt/locale/komodo_ur_PK.ts \
    src/qt/locale/komodo_uz@Cyrl.ts \
    src/qt/locale/komodo_vi.ts \
    src/qt/locale/komodo_vi_VN.ts \
    src/qt/locale/komodo_zh_CN.ts \
    src/qt/locale/komodo_zh_HK.ts \
    src/qt/locale/komodo_zh.ts \
    src/qt/locale/komodo_zh_TW.ts

include(src/qt/singleapplication/singleapplication.pri)
DEFINES += QAPPLICATION_CLASS=QApplication

#QMAKE_INFO_PLIST = res/Info.plist

#win32: RC_ICONS = res/icon.ico
#ICON = res/logo.icns

#libsodium.target = $$PWD/res/libsodium.a
#libsodium.commands = res/libsodium/buildlibsodium.sh

#QMAKE_EXTRA_TARGETS += src/leveldb/libleveldb.a
#QMAKE_CLEAN += src/leveldb/libsodium.a

## Default rules for deployment.
#qnx: target.path = /tmp/$${TARGET}/bin
#else: unix:!android: target.path = /opt/$${TARGET}/bin
#!isEmpty(target.path): INSTALLS += target

#win32:CONFIG(release, debug|release): LIBS += -L$$PWD/res/ -llibsodium
#else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/res/ -llibsodiumd
#else:unix: LIBS += -L$$PWD/res/ -lsodium

#INCLUDEPATH += $$PWD/res
#DEPENDPATH += $$PWD/res

#win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/res/liblibsodium.a
#else:win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/res/liblibsodium.a
#else:win32:!win32-g++:CONFIG(release, debug|release): PRE_TARGETDEPS += $$PWD/res/libsodium.lib
#else:win32:!win32-g++:CONFIG(debug, debug|release): PRE_TARGETDEPS += $$PWD/res/libsodiumd.lib
#else:unix:

#LIBBITCOIN_WALLET = libbitcoin_wallet.a
#LIBBITCOIN_COMMON = libbitcoin_common.a
#LIBBITCOIN_CLI=libbitcoin_cli.a
#LIBBITCOIN_UTIL=libbitcoin_util.a
#LIBBITCOIN_CRYPTO=crypto/libbitcoin_crypto.a
#LIBBITCOINQT=qt/libkomodoqt.a
#LIBVERUS_CRYPTO=crypto/libverus_crypto.a
#LIBVERUS_PORTABLE_CRYPTO=crypto/libverus_portable_crypto.a
#LIBSECP256K1=secp256k1/libsecp256k1.la
#LIBCRYPTOCONDITIONS=cryptoconditions/libcryptoconditions_core.la
#LIBSNARK=snark/libsnark.a
#LIBUNIVALUE=univalue/libunivalue.la
#LIBZCASH=libzcash.a

#LIBBITCOIN_ZMQ=libbitcoin_zmq.a
#LIBBITCOIN_PROTON=libbitcoin_proton.a
#LIBZCASH_CONSENSUS=libzcashconsensus.la
#LIBBITCOIN_WALLET=libbitcoin_wallet.a


PRE_TARGETDEPS += /
    $$PWD/src/libbitcoin_cli.a
    $$PWD/src/libbitcoin_common.a
    $$PWD/src/libbitcoin_util.a
    $$PWD/src/libzcashconsensus.la
    $$PWD/src/libbitcoin_crypto.a

DISTFILES +=
