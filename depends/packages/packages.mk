ifeq ($(build_os),darwin)
	zcash_packages := libgmp libsodium utfcpp rustcxx libsnark
else
	zcash_packages := libgmp libsodium utfcpp rustcxx
endif

qt_native_packages = native_protobuf
qt_packages = qrencode protobuf

qt_linux_packages := qt expat libxcb xcb_proto libXau xproto freetype fontconfig
qt_android_packages := qt

qt_darwin_packages := qt
qt_mingw32_packages := qt

native_packages := native_ccache native_rust native_cxxbridge

wallet_packages := bdb

packages := boost openssl libevent zeromq $(zcash_packages) zlib libarchive googletest libcurl
