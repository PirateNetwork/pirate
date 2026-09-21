# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# The Qt GUI. Source list transcribed from src/Makefile.qt.include. Qt6's CMake
# integration (AUTOMOC/AUTOUIC/AUTORCC) replaces autotools' hand-written
# moc/uic/rcc rules entirely -- no %.moc / ui_%.h / qrc_%.cpp recipes needed.

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
# .cpp files #include "ui_foo.h" without a path prefix even though the .ui
# lives in forms/ -- point AUTOUIC at it.
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/forms)

add_library(bitcoin_qt STATIC
  bantablemodel.cpp
  komodoaddressvalidator.cpp
  komodoamountfield.cpp
  pirateoceangui.cpp
  komodounits.cpp
  clientmodel.cpp
  csvmodelwriter.cpp
  guiutil.cpp
  intro.cpp
  modaloverlay.cpp
  networkstyle.cpp
  newseed.cpp
  newwallet.cpp
  restoreseed.cpp
  openwallet.cpp
  notificator.cpp
  optionsdialog.cpp
  optionsmodel.cpp
  peertablemodel.cpp
  platformstyle.cpp
  qvalidatedlineedit.cpp
  qvaluecombobox.cpp
  rpcconsole.cpp
  splashscreen.cpp
  trafficgraphwidget.cpp
  utilitydialog.cpp
  addressbookpage.cpp
  zaddressbookpage.cpp
  addresstablemodel.cpp
  zaddresstablemodel.cpp
  askpassphrasedialog.cpp
  coincontroldialog.cpp
  coincontroltreewidget.cpp
  editaddressdialog.cpp
  editzaddressdialog.cpp
  importkeydialog.cpp
  openphrasedialog.cpp
  openuridialog.cpp
  overviewpage.cpp
  paymentserver.cpp
  receivecoinsdialog.cpp
  receiverequestdialog.cpp
  recentrequeststablemodel.cpp
  zsendcoinsdialog.cpp
  zsigndialog.cpp
  zsendcoinsdialog_popup.cpp
  sendcoinsentry.cpp
  signverifymessagedialog.cpp
  transactiondesc.cpp
  transactiondescdialog.cpp
  transactionfilterproxy.cpp
  transactionrecord.cpp
  transactionrowdelegate.cpp
  transactiontablemodel.cpp
  transactionview.cpp
  unlocktimerdialog.cpp
  walletsdialog.cpp
  updatedialog.cpp
  verifypaymentdisclosuredialog.cpp
  walletframe.cpp
  walletmodel.cpp
  walletmodeltransaction.cpp
  walletmodelztransaction.cpp
  walletoptionspage.cpp
  walletview.cpp
  $<$<BOOL:${WIN32}>:winshutdownmonitor.cpp>
  # Pulled in directly (not just via bitcoin_server/bitcoin_wallet) because
  # nothing else in this target's own dependency graph references their RPC
  # command-table static initializers -- same reasoning Makefile.qt.include
  # documents for why libkomodoqt.a duplicates these objects.
  ../policy/policy.cpp
  ../wallet/rpcwallet.cpp
  ../rpc/server.cpp
  forms/addressbookpage.ui
  forms/askpassphrasedialog.ui
  forms/coincontroldialog.ui
  forms/editaddressdialog.ui
  forms/helpmessagedialog.ui
  forms/intro.ui
  forms/importSKdialog.ui
  forms/importVKdialog.ui
  forms/modaloverlay.ui
  forms/openphrasedialog.ui
  forms/openuridialog.ui
  forms/optionsdialog.ui
  forms/overviewpage.ui
  forms/receivecoinsdialog.ui
  forms/receiverequestdialog.ui
  forms/debugwindow.ui
  forms/zsendcoinsdialog.ui
  forms/zsigndialog.ui
  forms/zsendcoinsdialog_popup.ui
  forms/sendcoinsentry.ui
  forms/signverifymessagedialog.ui
  forms/transactiondescdialog.ui
  forms/verifypaymentdisclosuredialog.ui
  forms/zaddressbookpage.ui
  forms/editzaddressdialog.ui
  forms/newseed.ui
  forms/newwallet.ui
  forms/restoreseed.ui
  forms/openwallet.ui
  forms/unlocktimerdialog.ui
  forms/updatedialog.ui
  forms/walletoptionspage.ui
  komodo.qrc
)
target_include_directories(bitcoin_qt PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/forms
)
target_compile_definitions(bitcoin_qt PUBLIC QT_NO_KEYWORDS)
target_link_libraries(bitcoin_qt PUBLIC
  bitcoin_includes
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
)
if(WITH_DBUS)
  target_link_libraries(bitcoin_qt PUBLIC Qt6::DBus)
  target_compile_definitions(bitcoin_qt PUBLIC USE_DBUS)
endif()
add_dependencies(bitcoin_qt cxxbridge_gen)

# TODO(2b): translations. Makefile.qt.include builds qt/komodo_locale.qrc from
# 90 .ts files via lrelease+rcc; skipped for this bring-up (paymentserver.cpp
# loads translations at runtime via QTranslator, which just finds nothing
# without that resource -- no compile-time dependency).

#=============================
# pirate-qt executable
#=============================
# The static QPA platform plugin Q_IMPORT_PLUGIN(...) in komodo.cpp expects
# to find already linked in, one per platform (see the #if defined(QT_QPA_
# PLATFORM_XCB)/_WINDOWS/_COCOA chain there).
if(WIN32)
  set(QT_PLATFORM_PLUGIN Qt6::QWindowsIntegrationPlugin)
elseif(APPLE)
  set(QT_PLATFORM_PLUGIN Qt6::QCocoaIntegrationPlugin)
else()
  # QOffscreenIntegrationPlugin alongside XCB is purely additive -- XCB stays
  # the default for real displays, this just also makes `-platform offscreen`
  # available for headless runs (dev/CI debugging with no real display).
  set(QT_PLATFORM_PLUGIN Qt6::QXcbIntegrationPlugin Qt6::QOffscreenIntegrationPlugin)
endif()

add_executable(pirate-qt komodo.cpp)
target_add_windows_resource(pirate-qt res/pirate-qt-res.rc)
target_link_libraries(pirate-qt PRIVATE
  bitcoin_includes
  bitcoin_qt
  bitcoin_server
  $<$<BOOL:${ENABLE_WALLET}>:bitcoin_wallet>
  $<$<BOOL:${ENABLE_ZMQ}>:bitcoin_zmq>
  bitcoin_cli
  bitcoin_common
  bitcoin_util
  bitcoin_crypto
  univalue
  leveldb
  memenv
  bitcoin_cjson
  bitcoin_zcash
  bitcoin_tls
  librustzcash
  bitcoin_cxxbridge
  cryptoconditions
  secp256k1
  bitcoin_zcash_libs
  ${_boost_all}
  ${BDB_CXX_LIBRARY}
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
  ${QT_PLATFORM_PLUGIN}
  $<$<BOOL:${WITH_DBUS}>:Qt6::DBus>
  $<$<BOOL:${USE_QRCODE}>:PkgConfig::libqrencode>
  PkgConfig::libevent $<$<NOT:$<BOOL:${WIN32}>>:PkgConfig::libevent_pthreads>
  PkgConfig::libcurl PkgConfig::libarchive
  $<$<BOOL:${ENABLE_ZMQ}>:PkgConfig::libzmq>
  # See the comment at pirate-tx's link line (src/CMakeLists.txt): libssl/
  # libcrypto must come after everything that references OpenSSL symbols
  # for a single-pass linker.
  PkgConfig::libssl PkgConfig::libcrypto
  ${WINDOWS_SYSTEM_LIBS} ${SECURITY_FRAMEWORK}
)
