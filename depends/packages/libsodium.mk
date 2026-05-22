package=libsodium
$(package)_version=1.0.22
$(package)_download_path=https://download.libsodium.org/libsodium/releases
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
$(package)_dependencies=
$(package)_config_opts=

define $(package)_set_vars
  $(package)_build_env=DO_NOT_UPDATE_CONFIG_SCRIPTS=1
  ifeq ($(build_os),darwin)
  $(package)_build_env+=MACOSX_DEPLOYMENT_TARGET="$(OSX_MIN_VERSION)"
  $(package)_cc=clang
  $(package)_cxx=clang++
  endif
endef

define $(package)_preprocess_cmds
  cd $($(package)_build_subdir); DO_NOT_UPDATE_CONFIG_SCRIPTS=1 ./autogen.sh && \
  cp -f $(BASEDIR)/config.guess $(BASEDIR)/config.sub build-aux
endef

define $(package)_config_cmds
  $($(package)_autoconf) --enable-static --disable-shared
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
