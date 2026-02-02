package=zlib
$(package)_version=1.3
$(package)_download_path=https://github.com/PirateNetwork/Dependencies/raw/main/Treasure_Chest
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_sha256_hash=ff0ba4c292013dbc27530b3a81e1f9a813cd39de01ca5e0f8bf355702efa593e

define $(package)_set_vars
$(package)_config_opts= CC="$($(package)_cc)"
$(package)_config_opts+=CFLAGS="$($(package)_cflags) $($(package)_cppflags) -fPIC"
$(package)_config_opts+=RANLIB="$($(package)_ranlib)"
$(package)_config_opts+=AR="$($(package)_ar)"
$(package)_config_opts_darwin+=AR="$($(package)_libtool)"
$(package)_config_opts_darwin+=ARFLAGS="-o"
$(package)_config_opts_android+=CHOST=$(host)
$(package)_cppflags_darwin+=-DHAVE_STDIO_H
endef

# zlib has its own custom configure script that takes in options like CC,
# CFLAGS, RANLIB, AR, and ARFLAGS from the environment rather than from
# command-line arguments.
define $(package)_config_cmds
  env $($(package)_config_opts) ./configure --static --prefix=$(host_prefix) && \
  if [ "$(host_os)" = "darwin" ]; then \
    if [ -f zconf.h ]; then \
      if ! grep -q '^#undef NO_FDOPEN' zconf.h; then \
        printf '\n#undef NO_FDOPEN\n' >> zconf.h; \
      fi; \
      if ! grep -q '^#define HAVE_STDIO_H' zconf.h; then \
        printf '\n#define HAVE_STDIO_H 1\n' >> zconf.h; \
      fi; \
    fi; \
    if [ -f zutil.h ]; then \
      if ! grep -q 'PIRATE_UNDEF_FDOPEN' zutil.h; then \
        printf '\n#ifdef __APPLE__\n/* PIRATE_UNDEF_FDOPEN: avoid fdopen macro collision with Xcode headers. */\n#ifdef fdopen\n#undef fdopen\n#endif\n#endif\n' >> zutil.h; \
      fi; \
    fi; \
  fi
endef

define $(package)_build_cmds
  $(MAKE) libz.a
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef
