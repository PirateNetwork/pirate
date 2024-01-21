
zcash_packages := libsodium utfcpp rustcxx libgmp

qt_native_packages = native_protobuf
qt_packages = qrencode protobuf

qt_linux_packages := qt expat libxcb xcb_proto libXau xproto freetype fontconfig libxkbcommon libxcb_util libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm
qt_android_packages := qt
qt_darwin_packages := qt
qt_mingw32_packages := qt

native_packages := native_ccache native_rust native_cxxbridge
$(host_arch)_$(host_os)_native_packages += native_b2

wallet_packages := bdb

packages := boost openssl libevent zeromq $(zcash_packages) zlib libarchive googletest libcurl
