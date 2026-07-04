package=tor
$(package)_version=0.4.9.11
$(package)_download_path=https://dist.torproject.org
$(package)_file_name=tor-$($(package)_version).tar.gz
$(package)_sha256_hash=2e6c1720118c812acf0079fd47cf91b6bfaba5d766c321c4d3d2a28d6a11a8ed
$(package)_dependencies=openssl libevent zlib

define $(package)_set_vars
  $(package)_config_opts=--disable-unittests --disable-system-torrc --disable-asciidoc
  $(package)_config_opts+=--disable-tool-name-check --disable-dependency-tracking --enable-option-checking
  $(package)_config_opts+=--enable-static-tor
  $(package)_config_opts+=--with-openssl-dir=$(host_prefix)
  $(package)_config_opts+=--with-libevent-dir=$(host_prefix)
  $(package)_config_opts+=--with-zlib-dir=$(host_prefix)
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf share/man share/doc
endef
