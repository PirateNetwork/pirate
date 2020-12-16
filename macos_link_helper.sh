#!/bin/bash

# Link helper script for MacOS (should be placed in src and launched after zcutil/build-mac.sh -j4)

rm qt/komodo-qt

sed -i -e "s/\/\* #undef QT_QPA_PLATFORM_COCOA \*\//#define QT_QPA_PLATFORM_COCOA 1/" config/bitcoin-config.h
sed -i -e "s/\/\* #undef QT_STATICPLUGIN \*\//#define QT_STATICPLUGIN 1/" config/bitcoin-config.h
#make -j4

../libtool  --tag CXX  --mode=link g++-8 -g -O2 -D_THREAD_SAFE -pthread -fopenmp -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/share/../lib -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -arch x86_64 -Wl,-no_pie -Wl,-headerpad_max_install_names -Wl,-dead_strip -o qt/komodo-qt qt/komodo_qt-komodo.o qt/komodo_qt-macdockiconhandler.o  libbitcoin_server.a -lcurl qt/libkomodoqt.a libbitcoin_zmq.a -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lzmq -lstdc++ -lpthread libbitcoin_wallet.a  libbitcoin_cli.a libbitcoin_common.a libbitcoin_util.a  crypto/libbitcoin_crypto.a univalue/libunivalue.la ./leveldb/libleveldb.a  ./leveldb/libmemenv.a -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/share/../lib -lboost_system-mt -lboost_filesystem-mt -lboost_program_options-mt -lboost_thread-mt -lboost_chrono-mt -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lQt5Network -lQt5Core -framework DiskArbitration -framework IOKit -lm -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -lz -lqtpcre2 -framework Security -framework CoreFoundation -framework CoreServices -framework SystemConfiguration -lz -lQt5Widgets -lQt5Gui -framework DiskArbitration -framework IOKit -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -framework CoreGraphics -framework OpenGL -framework AGL -lqtlibpng -lqtharfbuzz -lQt5Core -lm -lz -lqtpcre2 -framework AppKit -lz -framework Carbon -framework OpenGL -framework AGL -lQt5Gui -lQt5Core -framework DiskArbitration -framework IOKit -lm -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -lz -lqtpcre2 -framework AppKit -framework CoreGraphics -framework Foundation -framework OpenGL -framework AGL -lqtlibpng -lqtharfbuzz -framework ApplicationServices -lz -lQt5Core -framework DiskArbitration -framework IOKit -lm -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -lz -lqtpcre2 -framework Foundation -framework ApplicationServices -framework AppKit -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lQt5DBus -lQt5Core -framework DiskArbitration -framework IOKit -lm -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -lz -lqtpcre2 -lQt5Core -framework DiskArbitration -framework IOKit -lm -framework Foundation -framework CoreServices -framework AppKit -framework ApplicationServices -framework CoreFoundation -lz -lqtpcre2  -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lqrencode -lpthread  -ldb_cxx-6.2 -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lssl -lcrypto -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lcrypto  secp256k1/libsecp256k1.la -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -levent_pthreads -levent -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -levent crypto/libverus_crypto.a crypto/libverus_portable_crypto.a libzcash.a -lcurl snark/libsnark.a -lgmp -lgmpxx -lboost_system-mt -lcrypto -lsodium -lrustzcash -ldl cryptoconditions/libcryptoconditions_core.la \
-L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/plugins/platforms -lqcocoa -L/Users/decker/KomodoOcean/depends/x86_64-apple-darwin18.5.0/lib -lQt5AccessibilitySupport \
-lQt5AccessibilitySupport \
-lQt5CglSupport \
-lQt5ClipboardSupport \
-lQt5Core \
-lQt5DBus \
-lQt5DeviceDiscoverySupport \
-lQt5EventDispatcherSupport \
-lQt5FbSupport \
-lQt5FontDatabaseSupport \
-lQt5GraphicsSupport \
-lQt5Gui \
-lQt5Network \
-lQt5OpenGL \
-lQt5PlatformCompositorSupport \
-lQt5PrintSupport \
-lQt5Test \
-lQt5ThemeSupport \
-lQt5Widgets \
-lc++ \
-static-libgcc

