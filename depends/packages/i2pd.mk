package=i2pd
$(package)_version=2.60.0
$(package)_download_path=https://github.com/PurpleI2P/i2pd/archive/refs/tags
$(package)_download_file=$($(package)_version).tar.gz
$(package)_file_name=i2pd-$($(package)_version).tar.gz
$(package)_sha256_hash=ef32100c5ffdf4d23dfe78a2f6c08f65574fd79f992eb2ac8cfea0b6440deabd
$(package)_dependencies=boost openssl zlib

# i2pd's CMakeLists.txt lives under build/, with the project's own source tree
# as a sibling directory, so it must be pointed at explicitly. Building with
# `cmake -S build -B .` puts the generated Makefile at the top of the extracted
# tree, which lets the rest of this package definition follow the same
# config/build/stage shape every autotools-based depends package uses.
#
# $(package)_cc/_cxx can be a compound value (e.g. "gcc -m64" for 32/64-bit
# toggling on x86 linux hosts, see depends/hosts/linux.mk) - CMake's
# CMAKE_C_COMPILER/CMAKE_CXX_COMPILER must be a bare executable, so the first
# word is used for the compiler and any remaining words are folded into flags.
define $(package)_config_cmds
  cmake -S build -B . \
    -DCMAKE_INSTALL_PREFIX=$(host_prefix) \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(word 1,$($(package)_cc))" \
    -DCMAKE_CXX_COMPILER="$(word 1,$($(package)_cxx))" \
    -DCMAKE_C_FLAGS="$(wordlist 2,99,$($(package)_cc)) $($(package)_cflags) $($(package)_cppflags)" \
    -DCMAKE_CXX_FLAGS="$(wordlist 2,99,$($(package)_cxx)) $($(package)_cxxflags) $($(package)_cppflags)" \
    -DCMAKE_EXE_LINKER_FLAGS="$($(package)_ldflags)" \
    -DCMAKE_PREFIX_PATH=$(host_prefix) \
    -DCMAKE_FIND_ROOT_PATH=$(host_prefix) \
    -DBoost_NO_SYSTEM_PATHS=ON \
    -DOPENSSL_ROOT_DIR=$(host_prefix) \
    -DBUILD_TESTING=OFF \
    -DWITH_LIBRARY=OFF \
    -DWITH_BINARY=ON \
    -DWITH_STATIC=ON \
    -DWITH_UPNP=OFF \
    -DWITH_HARDENING=ON \
    -DWITH_GIT_VERSION=OFF
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
