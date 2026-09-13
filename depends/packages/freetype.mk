package=freetype
$(package)_version=2.11.1
$(package)_download_path=https://download.savannah.gnu.org/releases/$(package)
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=f8db94d307e9c54961b39a1cc799a67d46681480696ed72ecf78d4473770f09b

define $(package)_set_vars
  $(package)_config_opts=--without-zlib --without-png --without-harfbuzz --without-bzip2 --without-brotli
  $(package)_config_opts += --enable-static --disable-shared --enable-option-checking
  $(package)_config_opts_linux=--with-pic
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
  rm -f lib/*.la
endef
