.NOTPARALLEL :

SOURCES_PATH ?= $(BASEDIR)/sources
BASE_CACHE ?= $(BASEDIR)/built
SDK_PATH ?= $(BASEDIR)/SDKs
NO_QT ?=
NO_WALLET ?=
NO_UPNP ?=
FALLBACK_DOWNLOAD_PATH ?= https://github.com/PirateNetwork/Dependencies/raw/main/Treasure_Chest

BUILD ?= $(shell ./config.guess)
HOST ?= $(BUILD)
PATCHES_PATH = $(BASEDIR)/patches
BASEDIR = $(CURDIR)
HASH_LENGTH:=11
DOWNLOAD_CONNECT_TIMEOUT:=10
DOWNLOAD_RETRIES:=3
CRATE_REGISTRY:=vendored-sources
CRATE_ARCHIVE = $(SOURCES_PATH)/vendored-crates.tar.gz

host:=$(BUILD)
ifneq ($(HOST),)
host:=$(HOST)
host_toolchain:=$(HOST)-
endif

ifneq ($(DEBUG),)
release_type=debug
else
release_type=release
endif

base_build_dir=$(BASEDIR)/work/build
base_staging_dir=$(BASEDIR)/work/staging
base_download_dir=$(BASEDIR)/work/download
canonical_host:=$(shell ./config.sub $(HOST))
build:=$(shell ./config.sub $(BUILD))

build_arch =$(firstword $(subst -, ,$(build)))
build_vendor=$(word 2,$(subst -, ,$(build)))
full_build_os:=$(subst $(build_arch)-$(build_vendor)-,,$(build))
build_os:=$(findstring linux,$(full_build_os))
build_os+=$(findstring darwin,$(full_build_os))
build_os:=$(strip $(build_os))
ifeq ($(build_os),)
build_os=$(full_build_os)
endif

host_arch=$(firstword $(subst -, ,$(canonical_host)))
host_vendor=$(word 2,$(subst -, ,$(canonical_host)))
full_host_os:=$(subst $(host_arch)-$(host_vendor)-,,$(canonical_host))
host_os:=$(findstring linux,$(full_host_os))
host_os+=$(findstring darwin,$(full_host_os))
host_os+=$(findstring mingw32,$(full_host_os))
host_os:=$(strip $(host_os))
ifeq ($(host_os),)
host_os=$(full_host_os)
endif

$(host_arch)_$(host_os)_prefix=$(BASEDIR)/$(host)
$(host_arch)_$(host_os)_host=$(host)
host_prefix=$($(host_arch)_$(host_os)_prefix)
build_prefix=$(host_prefix)/native
build_host=$(build)

AT_$(V):=
AT_:=@
AT:=$(AT_$(V))

all: install

include hosts/$(host_os).mk
include hosts/default.mk
include builders/$(build_os).mk
include builders/default.mk
include packages/packages.mk

qt_packages_$(NO_QT) = $(qt_packages) $(qt_$(host_os)_packages) $(qt_$(host_arch)_$(host_os)_packages)
wallet_packages_$(NO_WALLET) = $(wallet_packages)

packages += $($(host_arch)_$(host_os)_packages) $($(host_os)_packages) $(qt_packages_) $(rust_packages) $(wallet_packages_)
native_packages += $($(host_arch)_$(host_os)_native_packages) $($(host_os)_native_packages)

ifneq ($(qt_packages_),)
native_packages += $(qt_native_packages)
endif

all_packages = $(packages) $(native_packages)

meta_depends = Makefile funcs.mk builders/default.mk hosts/default.mk hosts/$(host_os).mk builders/$(build_os).mk cargo-checksum.sh

$(host_arch)_$(host_os)_native_toolchain?=$($(host_os)_native_toolchain)

include funcs.mk

toolchain_path=$($($(host_arch)_$(host_os)_native_toolchain)_prefixbin)
final_build_id_long+=$(shell $(build_SHA256SUM) config.site.in)
final_build_id+=$(shell echo -n $(final_build_id_long) | $(build_SHA256SUM) | cut -c-$(HASH_LENGTH))
$(host_prefix)/.stamp_$(final_build_id): $(native_packages) $(packages)
	$(AT)rm -rf $(@D)
	$(AT)mkdir -p $(@D)
	$(AT)echo copying packages: $^
	$(AT)echo to: $(@D)
	$(AT)cd $(@D); $(foreach package,$^, tar xf $($(package)_cached); )
	$(AT)if test -f "$(CRATE_ARCHIVE)"; \
	then echo Extracting pre-vendored crates from $(CRATE_ARCHIVE)...; \
		tar xf $(CRATE_ARCHIVE) -C $(@D); \
	else echo Vendoring crates...; \
		$(@D)/native/bin/cargo vendor --locked --manifest-path $(BASEDIR)/../Cargo.toml $(@D)/$(CRATE_REGISTRY); \
	fi
	$(AT)touch $@

$(host_prefix)/share/config.site : config.site.in $(host_prefix)/.stamp_$(final_build_id)
	$(AT)@mkdir -p $(@D)
	$(AT)sed -e 's|@HOST@|$(host)|' \
            -e 's|@CC@|$(toolchain_path)$(host_CC)|' \
            -e 's|@CXX@|$(toolchain_path)$(host_CXX)|' \
            -e 's|@AR@|$(toolchain_path)$(host_AR)|' \
            -e 's|@RANLIB@|$(toolchain_path)$(host_RANLIB)|' \
            -e 's|@NM@|$(toolchain_path)$(host_NM)|' \
            -e 's|@STRIP@|$(toolchain_path)$(host_STRIP)|' \
            -e 's|@build_os@|$(build_os)|' \
            -e 's|@host_os@|$(host_os)|' \
            -e 's|@CFLAGS@|$(strip $(host_CFLAGS) $(host_$(release_type)_CFLAGS))|' \
            -e 's|@CXXFLAGS@|$(strip $(host_CXXFLAGS) $(host_$(release_type)_CXXFLAGS))|' \
            -e 's|@CPPFLAGS@|$(strip $(host_CPPFLAGS) $(host_$(release_type)_CPPFLAGS))|' \
            -e 's|@LDFLAGS@|$(strip $(host_LDFLAGS) $(host_$(release_type)_LDFLAGS))|' \
            -e 's|@rust_target@|$(call rust_target,native_rust,$(canonical_host),$(host_os))|' \
            -e 's|@no_qt@|$(NO_QT)|' \
            -e 's|@no_wallet@|$(NO_WALLET)|' \
            -e 's|@debug@|$(DEBUG)|' \
            $< > $@
	$(AT)touch $@


define check_or_remove_cached
  mkdir -p $(BASE_CACHE)/$(host)/$(package) && cd $(BASE_CACHE)/$(host)/$(package); \
  $(build_SHA256SUM) -c $($(package)_cached_checksum) >/dev/null 2>/dev/null || \
  ( rm -f $($(package)_cached_checksum); \
    if test -f "$($(package)_cached)"; then echo "Checksum mismatch for $(package). Forcing rebuild.."; rm -f $($(package)_cached_checksum) $($(package)_cached); fi )
endef

define check_or_remove_sources
  mkdir -p $($(package)_source_dir); cd $($(package)_source_dir); \
  $(build_SHA256SUM) -c $($(package)_fetched) >/dev/null 2>/dev/null || \
    ( if test -f "$($(package)_all_sources)"; then echo "Checksum missing or mismatched for $(package) source. Forcing re-download."; fi; \
      rm -f "$($(package)_all_sources)" "$($(1)_fetched)")
endef

check-packages:
	@$(foreach package,$(all_packages),$(call check_or_remove_cached,$(package));)
check-sources:
	@$(foreach package,$(all_packages),$(call check_or_remove_sources,$(package));)

$(host_prefix)/share/config.site: check-packages

check-packages: check-sources

install: check-packages $(host_prefix)/share/config.site

crates_download_dir=$(base_download_dir)/crates
download-crates: native_rust
	$(AT)echo Vendoring crates...
	$(AT)mkdir -p $(SOURCES_PATH)
	$(AT)rm -rf $(crates_download_dir)
	$(AT)mkdir -p $(crates_download_dir)
	$(AT)tar xf $(native_rust_cached) -C $(crates_download_dir)
	$(AT)$(crates_download_dir)/native/bin/cargo vendor --locked --manifest-path $(BASEDIR)/../Cargo.toml $(crates_download_dir)/$(CRATE_REGISTRY)
	$(AT)cd $(crates_download_dir); find $(CRATE_REGISTRY) | sort | tar --no-recursion -czf $(CRATE_ARCHIVE) -T -
	$(AT)rm -rf $(crates_download_dir)

download-one: check-sources $(all_sources)

download-osx:
	@$(MAKE) -s HOST=x86_64-apple-darwin11 download-one
download-linux:
	@$(MAKE) -s HOST=x86_64-unknown-linux-gnu download-one
download-win:
	@$(MAKE) -s HOST=x86_64-w64-mingw32 download-one
download: download-osx download-linux download-win

.PHONY: install cached download-one download-osx download-linux download-win download check-packages check-sources
