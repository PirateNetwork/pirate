package=native_ccache
$(package)_version=3.7.4
$(package)_download_path=https://github.com/ccache/ccache/releases/download/v$($(package)_version)
$(package)_file_name=ccache-$($(package)_version).tar.xz
$(package)_sha256_hash=04c0af414b8cf89e541daed59735547fbfd323b1aaa983da0216f6b6731e6836

define $(package)_set_vars
$(package)_config_opts=
$(package)_config_opts_mingw32=--host=x86_64-w64-mingw32
$(package)_build_opts_mingw32=CC="$(build_CC)" CXX="$(build_CXX)"
endef

define $(package)_preprocess_cmds
  cp $(BASEDIR)/config.guess $(BASEDIR)/config.sub . && \
  $(if $(findstring mingw32,$(host_os)), \
    autoreconf -i \
  )
endef

define $(package)_config_cmds
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE) $($(package)_build_opts_$(host_os))
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf lib include
endef
