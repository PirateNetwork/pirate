package=libcurl
$(package)_version=7.69.1
$(package)_dependencies=openssl
$(package)_download_path=https://curl.haxx.se/download
$(package)_file_name=curl-$($(package)_version).tar.xz
$(package)_sha256_hash=03c7d5e6697f7b7e40ada1b2256e565a555657398e6c1fcfa4cb251ccd819d4f
$(package)_config_opts_linux=--disable-shared --enable-static --prefix=$(host_prefix) --host=$(HOST)
$(package)_config_opts_mingw32=--enable-mingw --disable-shared --enable-static --prefix=$(host_prefix) --host=x86_64-w64-mingw32
$(package)_config_opts_darwin=--disable-shared --enable-static --prefix=$(host_prefix)
$(package)_cflags_darwin=-mmacosx-version-min=10.9
$(package)_conf_tool=./configure

ifeq ($(build_os),darwin)
define $(package)_set_vars
  $(package)_build_env=MACOSX_DEPLOYMENT_TARGET="10.9"
endef
endif

ifeq ($(build_os),linux)
define $(package)_set_vars
  $(package)_config_env=LD_LIBRARY_PATH="$(host_prefix)/lib" PKG_CONFIG_LIBDIR="$(host_prefix)/lib/pkgconfig" CPPFLAGS="-I$(host_prefix)/include" LDFLAGS="-L$(host_prefix)/lib"
endef
endif


define $(package)_config_cmds
  echo '=== config for $(package):' && \
  echo '$($(package)_config_env) $($(package)_conf_tool) $($(package)_config_opts)' && \
  echo '=== ' && \
  $($(package)_config_env) $($(package)_conf_tool) $($(package)_config_opts)
endef

ifeq ($(build_os),darwin)
define $(package)_build_cmds
  $(MAKE) CPPFLAGS="-I$(host_prefix)/include -fPIC" CFLAGS='-mmacosx-version-min=10.9'
endef
else
define $(package)_build_cmds
  $(MAKE)
endef
endif

define $(package)_stage_cmds
  echo 'Staging dir: $($(package)_staging_dir)$(host_prefix)/' && \
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
