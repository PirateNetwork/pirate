package=native_ccache
$(package)_version=3.7.4
$(package)_download_path=https://github.com/ccache/ccache/releases/download/v$($(package)_version)
$(package)_file_name=ccache-$($(package)_version).tar.xz
$(package)_sha256_hash=04c0af414b8cf89e541daed59735547fbfd323b1aaa983da0216f6b6731e6836

define $(package)_set_vars
$(package)_config_opts=
endef

# Adding verbose cd and ls to debug path issues on Windows for native_ccache
define $(package)_config_cmds
  echo "Current directory before cd:" && pwd && \
  echo "Listing current directory before cd:" && ls -la && \
  echo "Attempting to cd to $($(package)_build_dir) and configure" && \
  cd "$($(package)_build_dir)" && \
  echo "Current directory after cd:" && pwd && \
  echo "Listing current directory after cd:" && ls -la && \
  $($(package)_autoconf)
endef

define $(package)_build_cmds
  $(MAKE)
endef

define $(package)_stage_cmds
  $(MAKE) DESTDIR=$($(package)_staging_dir) install
endef

define $(package)_postprocess_cmds
  rm -rf lib include
endef
