ifeq ($(build_os),darwin)
	zcash_packages := libsnark libgmp libsodium utfcpp
else
	proton_packages := proton
	zcash_packages := libgmp libsodium utfcpp
endif

native_packages := native_ccache native_rust

wallet_packages=bdb

ifeq ($(host_os),linux)
	packages := boost openssl libevent zeromq $(zcash_packages) googletest libcurl #googlemock
else
	packages := boost openssl libevent zeromq $(zcash_packages) libcurl googletest #googlemock
endif
