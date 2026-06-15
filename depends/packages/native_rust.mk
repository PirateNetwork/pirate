package=native_rust
$(package)_version=1.96.0
$(package)_download_path=https://static.rust-lang.org/dist

# 1. Define all platform-specific file names and hashes first
$(package)_file_name_linux=rust-$($(package)_version)-x86_64-unknown-linux-gnu.tar.gz
$(package)_sha256_hash_linux=c1130e4f7976f230766ab062b105b1fb050d6a78177db2246a5878fd6a589680
$(package)_file_name_darwin=rust-$($(package)_version)-x86_64-apple-darwin.tar.gz
$(package)_sha256_hash_darwin=ddaf6a98ccc500891a74bcb95807a04c89a0d1ad7b9c58bd7116620ff6f903a8
$(package)_file_name_freebsd=rust-$($(package)_version)-x86_64-unknown-freebsd.tar.gz
$(package)_sha256_hash_freebsd=5457c15df17ff963b582b95c55fae3bc3736468e4df765182c75c19b1b6e8e74
$(package)_file_name_aarch64_linux=rust-$($(package)_version)-aarch64-unknown-linux-gnu.tar.gz
$(package)_sha256_hash_aarch64_linux=20d5ebe3916fe489891fc577574e47fc679cdf62080c1bb1be6b6905ff4e275b
$(package)_file_name_windows=rust-$($(package)_version)-x86_64-pc-windows-gnu.tar.gz
$(package)_sha256_hash_windows=f9d9411035190563fc08a461188ea90cd778bb1c36d534469b5772612d6daad5

# 2. Determine _file_name_to_use and _sha256_hash_to_use based on (stripped) build_os
_tmp_build_os := $(strip $(build_os))
ifeq ($(_tmp_build_os),mingw32)
  _file_name_to_use := $($(package)_file_name_windows)
  _sha256_hash_to_use := $($(package)_sha256_hash_windows)
else ifeq ($(_tmp_build_os),linux)
  ifeq ($(strip $(build_arch)),aarch64)
    _file_name_to_use := $($(package)_file_name_aarch64_linux)
    _sha256_hash_to_use := $($(package)_sha256_hash_aarch64_linux)
  else
    _file_name_to_use := $($(package)_file_name_linux)
    _sha256_hash_to_use := $($(package)_sha256_hash_linux)
  endif
else ifeq ($(_tmp_build_os),darwin)
  _file_name_to_use := $($(package)_file_name_darwin)
  _sha256_hash_to_use := $($(package)_sha256_hash_darwin)
else ifeq ($(_tmp_build_os),freebsd)
  _file_name_to_use := $($(package)_file_name_freebsd)
  _sha256_hash_to_use := $($(package)_sha256_hash_freebsd)
else
  # Fallback, with a warning or error if possible, for now default to linux x86
  _file_name_to_use := $($(package)_file_name_linux)
  _sha256_hash_to_use := $($(package)_sha256_hash_linux)
endif

# 3. Assign to the main package variables, stripping results
$(package)_file_name = $(strip $(_file_name_to_use))
$(package)_download_file = $(strip $(_file_name_to_use))
$(package)_sha256_hash = $(strip $(_sha256_hash_to_use))

# --- Original Rust target mappings and std sha256 hashes (keep these as they are) ---
$(package)_rust_target_x86_64-pc-linux-gnu=x86_64-unknown-linux-gnu
$(package)_rust_target_x86_64-w64-mingw32=x86_64-pc-windows-gnu
$(package)_rust_target_x86_64-w64-mingw64=x86_64-pc-windows-gnu

$(package)_rust_std_sha256_hash_aarch64-unknown-linux-gnu=66ad5d73e79dd44b93c260ee61752abce3ce5ccb5031832beaccd1c248b88586
$(package)_rust_std_sha256_hash_aarch64-apple-darwin=a5c160197236f68cc8627a573545fd883d4d98856fb654a6d6aa5883ff1bdcc7
$(package)_rust_std_sha256_hash_arm-unknown-linux-gnueabihf=2ad60ba83eac16934d35cbd468abb8721b10a37554cab02ad4990b9e54ec5db3
$(package)_rust_std_sha256_hash_x86_64-apple-darwin=c5dfa11ccc724faec277e420ff6b33cfa6567b9ac6fa9e5d712a19c662d8c36c
$(package)_rust_std_sha256_hash_x86_64-pc-windows-gnu=6951de999a0926aa8e35046017473a1912274cc34e800887eb3bfba4ddae12c9
$(package)_rust_std_sha256_hash_x86_64-unknown-freebsd=90979e87d60185944eef415230b904253abdcd36a4ea603479a3991ffb185807
$(package)_rust_std_sha256_hash_x86_64-unknown-linux-gnu=36e577b66f7b2f8fc6493f97f81329e5f6e1514360d0c6c31d5d8463184e6773
$(package)_rust_std_sha256_hash_x86_64-w64-mingw64=6951de999a0926aa8e35046017473a1912274cc34e800887eb3bfba4ddae12c9
# --- End of original Rust target mappings ---

define rust_target
$(if $($(1)_rust_target_$(2)),$($(1)_rust_target_$(2)),$(if $(findstring darwin,$(3)),$(if $(findstring aarch64,$(2)),aarch64-apple-darwin,$(if $(findstring arm,$(2)),aarch64-apple-darwin,x86_64-apple-darwin)),$(if $(findstring freebsd,$(3)),x86_64-unknown-freebsd,$(2))))
endef

define $(package)_set_vars
$(package)_stage_opts=--disable-ldconfig
$(package)_stage_build_opts=--without=rust-docs-json-preview,rust-docs
endef

# --- Conditional logic for cross-compilation vs native ---
ifneq ($(canonical_host),$(build))
  $(package)_rust_target=$(call rust_target,$(package),$(canonical_host),$(host_os))
  $(package)_download_file=rust-std-$($(package)_version)-$($(package)_rust_target).tar.gz
  $(package)_rust_std_sha256_hash=$($(package)_rust_std_sha256_hash_$($(package)_rust_target))
  $(package)_build_subdir=buildos
  $(package)_extra_sources = $($(package)_file_name)

  define $(package)_fetch_cmds
    $(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_download_file),$($(package)_rust_std_sha256_hash)) && \
    $(call fetch_file,$(package),$($(package)_download_path),$($(package)_extra_sources),$($(package)_extra_sources),$($(package)_sha256_hash))
  endef

  define $(package)_extract_cmds
    mkdir -p $($(package)_extract_dir) && \
    mkdir -p $($(package)_extract_dir)/$(canonical_host) && \
    mkdir -p $($(package)_extract_dir)/buildos && \
    cd $($(package)_extract_dir) && \
    echo "$($(package)_rust_std_sha256_hash)  $($(package)_source_dir)/$($(package)_download_file)" > .$($(package)_download_file).hash && \
    echo "$($(package)_sha256_hash)  $($(package)_source_dir)/$($(package)_extra_sources)" > .$($(package)_extra_sources).hash && \
    $(build_SHA256SUM) -c .$($(package)_download_file).hash && \
    $(build_SHA256SUM) -c .$($(package)_extra_sources).hash && \
    tar --strip-components=1 -xf $($(package)_source_dir)/$($(package)_download_file) -C $(canonical_host) && \
    tar --strip-components=1 -xf $($(package)_source_dir)/$($(package)_extra_sources) -C buildos
  endef

  define $(package)_stage_cmds
    bash ./install.sh --destdir=$($(package)_staging_dir) --prefix=$(build_prefix) $($(package)_stage_opts) $($(package)_stage_build_opts) && \
    ../$(canonical_host)/install.sh --destdir=$($(package)_staging_dir) --prefix=$(build_prefix) $($(package)_stage_opts)
  endef
else # Native build
  # For native builds, _extra_sources isn't strictly needed by fetch_cmds the same way,
  # as only one rust toolchain (primary) is downloaded by default fetch_cmds in funcs.mk
  # $(package)_extra_sources is not used by default $(package)_extract_cmds from funcs.mk
  define $(package)_stage_cmds # Simpler for native
    bash ./install.sh --destdir=$($(package)_staging_dir) --prefix=$(build_prefix) $($(package)_stage_opts) $($(package)_stage_build_opts)
  endef
endif

$(package)_pre_configure:
	# Copy rustc and cargo to a directory that is in PATH
	mkdir -p $(build_dir)/bin
	cp $(native_rust_toolchain_dir)/bin/rustc $(build_dir)/bin/
	cp $(native_rust_toolchain_dir)/bin/cargo $(build_dir)/bin/

	# Install the rust-src component
	$(native_rust_toolchain_dir)/bin/rustup component add rust-src --toolchain $(native_rust_toolchain)

	# Install required targets
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) wasm32-unknown-unknown
ifeq ($(build_os),linux)
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) x86_64-unknown-linux-gnu
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) aarch64-linux-gnu
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) arm-linux-gnueabihf
else ifeq ($(build_os),darwin)
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) x86_64-apple-darwin
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) aarch64-apple-darwin
else ifeq ($(build_os),mingw32)
	$(native_rust_toolchain_dir)/bin/rustup target add --toolchain $(native_rust_toolchain) x86_64-w64-mingw64
endif
