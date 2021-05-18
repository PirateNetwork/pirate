package=libgmp

$(package)_version=6.2.1
$(package)_download_path=https://ftp.gnu.org/gnu/gmp
$(package)_file_name=gmp-$($(package)_version).tar.bz2
$(package)_sha256_hash=eae9326beb4158c386e39a356818031bd28f3124cf915f8c5b1dc4c7a36b4d7c
#a8109865f2893f1373b0a8ed5ff7429de8db696fc451b1036bd7bdf95bbeffd6
$(package)_dependencies=
$(package)_config_opts=--enable-cxx --disable-shared

define $(package)_config_cmds
  $($(package)_autoconf) --host=$(host) --build=$(build)
endef

ifeq ($(build_os),darwin)
define $(package)_build_cmds
	$(MAKE)
endef
else
define $(package)_build_cmds
  $(MAKE) CPPFLAGS='-fPIC'
endef
endif

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install ; echo '=== staging find for $(package):' ; find $($(package)_staging_dir)
endef
