package=utfcpp
$(package)_version=4.0.4
# Fetch the same checksum-pinned archive directly, without the GitHub redirect.
$(package)_download_path=https://codeload.github.com/nemtrif/$(package)/tar.gz/refs/tags
$(package)_file_name=$(package)-$($(package)_version).tar.gz
$(package)_download_file=v$($(package)_version)
$(package)_sha256_hash=7c8a403d0c575d52473c8644cd9eb46c6ba028d2549bc3e0cdc2d45f5cfd78a0

define $(package)_stage_cmds
  mkdir -p $($(package)_staging_dir)$(host_prefix)/include && \
  cp -a ./source $($(package)_staging_dir)$(host_prefix)/include/utf8cpp
endef
