
zcash_packages := libsodium rustcxx utfcpp tl_expected

qt_packages = qrencode

qt_linux_packages := qt expat libxcb xcb_proto libXau libXdmcp xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_cursor libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm
qt_android_packages := qt
qt_darwin_packages := qt
qt_mingw32_packages := qt

# Cross builds need a Qt built for the build machine to supply the host tools
# (moc, rcc, lrelease); qt.mk points at it with -qt-host-path. Consumed by
# depends/Makefile's `native_packages += $(qt_native_packages)`.
ifneq ($(host),$(build))
qt_native_packages := native_qt
endif

native_packages := native_ccache native_rust native_cxxbridge
$(host_arch)_$(host_os)_native_packages += native_b2

wallet_packages := bdb

tor_i2pd_packages := tor i2pd

packages := boost openssl libevent zeromq $(zcash_packages) zlib libarchive googletest libcurl
