package=qt
include packages/qt_details.mk
$(package)_version=$(qt_details_version)
$(package)_download_path=$(qt_details_download_path)
$(package)_file_name=$(qt_details_qtbase_file_name)
$(package)_sha256_hash=$(qt_details_qtbase_sha256_hash)
# Cross builds need host tools (moc, rcc, lrelease) that run on the build
# machine, which only a native Qt provides.
ifneq ($(host),$(build))
$(package)_dependencies = native_qt
endif
$(package)_linux_dependencies=freetype fontconfig libxcb libxkbcommon libxcb_util libxcb_util_cursor libxcb_util_render libxcb_util_keysyms libxcb_util_image libxcb_util_wm
$(package)_patches = dont_hardcode_pwd.patch
$(package)_patches += qtbase_avoid_qmain.patch
$(package)_patches += qtbase_skip_tools.patch
$(package)_patches += rcc_hardcode_timestamp.patch
$(package)_patches += qttools_skip_dependencies.patch
$(package)_patches += static_fixes.patch
$(package)_patches += fix-qbytearray-include.patch
$(package)_patches += fix_missed_headers.patch
$(package)_patches += cocoa_compat.patch
$(package)_patches += qtbase_plugins_cocoa.patch
$(package)_patches += fix-macos26-qyield.patch
$(package)_patches += fix_openbsd_network_kernel.patch
$(package)_patches += fix_openbsd_plugin_qelfparser.patch
$(package)_patches += fix-gcc16-qcompare.patch
$(package)_patches += fix-gcc16-sfinae-qregularexpression.patch
$(package)_patches += fix-gcc16-sfinae-qchar.patch
$(package)_patches += fix-gcc16-sfinae-qbitarray.patch
$(package)_patches += fix-gcc16-sfinae-qanystringview.patch

$(package)_qttranslations_file_name=$(qt_details_qttranslations_file_name)
$(package)_qttranslations_sha256_hash=$(qt_details_qttranslations_sha256_hash)

$(package)_qttools_file_name=$(qt_details_qttools_file_name)
$(package)_qttools_sha256_hash=$(qt_details_qttools_sha256_hash)

$(package)_top_download_path=$(qt_details_top_download_path)
$(package)_top_cmakelists_file_name=$(qt_details_top_cmakelists_file_name)
$(package)_top_cmakelists_sha256_hash=$(qt_details_top_cmakelists_sha256_hash)
$(package)_top_cmake_download_path=$(qt_details_top_cmake_download_path)
$(package)_top_cmake_ecmoptionaladdsubdirectory_file_name=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_file_name)
$(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash=$(qt_details_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)
$(package)_top_cmake_qttoplevelhelpers_file_name=$(qt_details_top_cmake_qttoplevelhelpers_file_name)
$(package)_top_cmake_qttoplevelhelpers_sha256_hash=$(qt_details_top_cmake_qttoplevelhelpers_sha256_hash)

define $(package)_set_vars
$(package)_config_opts_release = -release
$(package)_config_opts_debug = -debug
$(package)_config_opts = -no-egl
$(package)_config_opts += -no-eglfs
$(package)_config_opts += -no-evdev
$(package)_config_opts += -no-gif
$(package)_config_opts += -no-glib
$(package)_config_opts += -no-icu
$(package)_config_opts += -no-ico
$(package)_config_opts += -no-kms
$(package)_config_opts += -no-linuxfb
$(package)_config_opts += -no-libjpeg
$(package)_config_opts += -no-libproxy
$(package)_config_opts += -no-libudev
$(package)_config_opts += -no-mtdev
$(package)_config_opts += -no-opengl
$(package)_config_opts += -no-openssl
$(package)_config_opts += -no-openvg
$(package)_config_opts += -no-reduce-relocations
$(package)_config_opts += -no-schannel
$(package)_config_opts += -no-sctp
$(package)_config_opts += -no-securetransport
$(package)_config_opts += -no-system-proxies
$(package)_config_opts += -no-use-gold-linker
$(package)_config_opts += -no-zstd
$(package)_config_opts += -nomake examples
$(package)_config_opts += -nomake tests
$(package)_config_opts += -prefix $(host_prefix)
$(package)_config_opts += -qt-doubleconversion
$(package)_config_opts += -qt-harfbuzz
$(package)_config_opts += -qt-libpng
$(package)_config_opts += -qt-pcre
$(package)_config_opts += -qt-zlib
$(package)_config_opts += -static
$(package)_config_opts += -no-feature-backtrace
$(package)_config_opts += -no-feature-concurrent
$(package)_config_opts += -no-feature-dial
$(package)_config_opts += -no-feature-gssapi
$(package)_config_opts += -no-feature-http
$(package)_config_opts += -no-feature-image_heuristic_mask
$(package)_config_opts += -no-feature-keysequenceedit
$(package)_config_opts += -no-feature-lcdnumber
$(package)_config_opts += -no-feature-libresolv
$(package)_config_opts += -no-feature-libstdcpp_assertions
$(package)_config_opts += -no-feature-networkdiskcache
$(package)_config_opts += -no-feature-networkproxy
$(package)_config_opts += -no-feature-printsupport
$(package)_config_opts += -no-feature-sessionmanager
$(package)_config_opts += -no-feature-socks5
$(package)_config_opts += -no-feature-sql
$(package)_config_opts += -no-feature-textmarkdownreader
$(package)_config_opts += -no-feature-textmarkdownwriter
$(package)_config_opts += -no-feature-textodfwriter
$(package)_config_opts += -no-feature-topleveldomain
$(package)_config_opts += -no-feature-udpsocket
$(package)_config_opts += -no-feature-undocommand
$(package)_config_opts += -no-feature-undogroup
$(package)_config_opts += -no-feature-undostack
$(package)_config_opts += -no-feature-undoview
$(package)_config_opts += -no-feature-vnc
$(package)_config_opts += -no-feature-vulkan

# Core tools.
$(package)_config_opts += -no-feature-androiddeployqt
$(package)_config_opts += -no-feature-macdeployqt
$(package)_config_opts += -no-feature-qmake
$(package)_config_opts += -no-feature-windeployqt

# Qt Tools module -- only linguist (lrelease/lupdate/lconvert) is needed for
# translations; everything else that would drag in Clang or a full designer
# build is switched off. This applies to cross builds too: without it qttools
# looks for an LLVM install ("Could NOT find Clang") and pulls in the full
# designer/qdbusviewer set, which fails to configure.
$(package)_config_opts += -feature-linguist
$(package)_config_opts += -no-feature-assistant
$(package)_config_opts += -no-feature-clang
$(package)_config_opts += -no-feature-clangcpp
$(package)_config_opts += -no-feature-designer
$(package)_config_opts += -no-feature-pixeltool
$(package)_config_opts += -no-feature-qdoc
$(package)_config_opts += -no-feature-qtattributionsscanner
$(package)_config_opts += -no-feature-qtdiag
$(package)_config_opts += -no-feature-qtplugininfo

$(package)_config_opts_darwin = -no-dbus
$(package)_config_opts_darwin += -no-feature-printsupport
$(package)_config_opts_darwin += -no-feature-freetype
$(package)_config_opts_darwin += -no-pkg-config

$(package)_config_opts_linux = -fontconfig
$(package)_config_opts_linux += -no-feature-xlib
$(package)_config_opts_linux += -no-xcb-xlib
$(package)_config_opts_linux += -pkg-config
$(package)_config_opts_linux += -system-freetype
$(package)_config_opts_linux += -xcb

$(package)_config_opts_mingw32 = -no-dbus
$(package)_config_opts_mingw32 += -no-feature-freetype
$(package)_config_opts_mingw32 += -no-pkg-config

# CMake build options passed after the "--" separator to qtbase/configure.
$(package)_cmake_opts = -DCMAKE_PREFIX_PATH=$(host_prefix)
$(package)_cmake_opts += -DQT_FEATURE_cxx20=ON
$(package)_cmake_opts += -DQT_GENERATE_SBOM=OFF
$(package)_cmake_opts += -DQT_USE_DEFAULT_CMAKE_OPTIMIZATION_FLAGS=ON
$(package)_cmake_opts += -DCMAKE_C_FLAGS="$($(package)_cflags) $($(package)_cppflags) -ffile-prefix-map=$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_CXX_FLAGS="$($(package)_cxxflags) $($(package)_cppflags) -ffile-prefix-map=$($(package)_extract_dir)=/usr"
$(package)_cmake_opts += -DCMAKE_EXE_LINKER_FLAGS="$($(package)_ldflags)"
$(package)_cmake_opts += -DCMAKE_AR="$($(package)_ar)"
ifneq (,$(filter linux openbsd,$(host_os)))
# The `-dbus-runtime` configure option does not work; drive it via CMake.
# https://qt-project.atlassian.net/browse/QTBUG-144864
$(package)_cmake_opts += -DINPUT_dbus=runtime
endif

ifneq ($(host),$(build))
# Host tools come from the native_qt build rather than being built for the
# target, which could not run here.
$(package)_config_opts += -qt-host-path $(build_prefix)

# Without these CMake never learns it is cross-compiling: it would fall back to
# the build machine's compiler while using the target's ar.
$(package)_cmake_opts += -DCMAKE_SYSTEM_NAME=$($(host_os)_cmake_system_name)
$(package)_cmake_opts += -DCMAKE_SYSTEM_VERSION=$($(host_os)_cmake_system_version)
$(package)_cmake_opts += -DCMAKE_SYSTEM_PROCESSOR=$(host_arch)

# qtbase/configure is invoked directly rather than through $(package)_autoconf,
# which is the only thing that passes CC/CXX, so hand the cross compiler over
# explicitly.
$(package)_config_env = CC="$($(package)_cc)"
$(package)_config_env += CXX="$($(package)_cxx)"

# qtbase/configure does not forward CC/CXX from the environment into the CMake
# invocation it builds, so setting them above is not enough on its own: CMake
# falls back to the build machine's /usr/bin/c++ and then fails its own compiler
# test once CMAKE_SYSTEM_NAME says it is cross-compiling. Pass them as options
# too. Safe here because only cross hosts reach this block, and their CC/CXX are
# bare tool names -- the native ones embed flags (e.g. "gcc -m64") and could not
# be used this way.
$(package)_cmake_opts += -DCMAKE_C_COMPILER=$($(package)_cc)
$(package)_cmake_opts += -DCMAKE_CXX_COMPILER=$($(package)_cxx)

# Qt looks for these native packages unconditionally even when cross-compiling,
# which picks up build-machine copies that cannot be linked into a target
# build. Turn the searches off.
$(package)_cmake_opts += -DCMAKE_DISABLE_FIND_PACKAGE_Libb2=TRUE
$(package)_cmake_opts += -DCMAKE_DISABLE_FIND_PACKAGE_WrapSystemDoubleConversion=TRUE
$(package)_cmake_opts += -DCMAKE_DISABLE_FIND_PACKAGE_WrapSystemMd4c=TRUE
$(package)_cmake_opts += -DCMAKE_DISABLE_FIND_PACKAGE_WrapZSTD=TRUE
endif
endef

define $(package)_fetch_cmds
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_download_file),$($(package)_file_name),$($(package)_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttranslations_file_name),$($(package)_qttranslations_file_name),$($(package)_qttranslations_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_download_path),$($(package)_qttools_file_name),$($(package)_qttools_file_name),$($(package)_qttools_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_download_path),$($(package)_top_cmakelists_file_name),$($(package)_top_cmakelists_file_name)-$($(package)_version),$($(package)_top_cmakelists_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name),$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version),$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)) && \
$(call fetch_file,$(package),$($(package)_top_cmake_download_path),$($(package)_top_cmake_qttoplevelhelpers_file_name),$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version),$($(package)_top_cmake_qttoplevelhelpers_sha256_hash))
endef

define $(package)_extract_cmds
  mkdir -p $($(package)_extract_dir) && \
  echo "$($(package)_sha256_hash)  $($(package)_source)" > $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttranslations_sha256_hash)  $($(package)_source_dir)/$($(package)_qttranslations_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_qttools_sha256_hash)  $($(package)_source_dir)/$($(package)_qttools_file_name)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmakelists_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_ecmoptionaladdsubdirectory_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  echo "$($(package)_top_cmake_qttoplevelhelpers_sha256_hash)  $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version)" >> $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  $(build_SHA256SUM) -c $($(package)_extract_dir)/.$($(package)_file_name).hash && \
  mkdir qtbase && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source) -C qtbase && \
  mkdir qttranslations && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttranslations_file_name) -C qttranslations && \
  mkdir qttools && \
  tar --no-same-owner --strip-components=1 -xf $($(package)_source_dir)/$($(package)_qttools_file_name) -C qttools && \
  cp $($(package)_source_dir)/$($(package)_top_cmakelists_file_name)-$($(package)_version) ./$($(package)_top_cmakelists_file_name) && \
  mkdir -p cmake && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_ecmoptionaladdsubdirectory_file_name) && \
  cp $($(package)_source_dir)/$($(package)_top_cmake_qttoplevelhelpers_file_name)-$($(package)_version) cmake/$($(package)_top_cmake_qttoplevelhelpers_file_name)
endef

define $(package)_preprocess_cmds
  patch -p1 -i $($(package)_patch_dir)/cocoa_compat.patch && \
  patch -p1 -i $($(package)_patch_dir)/dont_hardcode_pwd.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_avoid_qmain.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_plugins_cocoa.patch && \
  patch -p1 -i $($(package)_patch_dir)/qtbase_skip_tools.patch && \
  patch -p1 -i $($(package)_patch_dir)/rcc_hardcode_timestamp.patch && \
  patch -p1 -i $($(package)_patch_dir)/static_fixes.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-gcc16-qcompare.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-gcc16-sfinae-qregularexpression.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-gcc16-sfinae-qchar.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-gcc16-sfinae-qbitarray.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-gcc16-sfinae-qanystringview.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-macos26-qyield.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix-qbytearray-include.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_openbsd_network_kernel.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_openbsd_plugin_qelfparser.patch && \
  patch -p1 -i $($(package)_patch_dir)/fix_missed_headers.patch
endef
$(package)_preprocess_cmds += && patch -p1 -i $($(package)_patch_dir)/qttools_skip_dependencies.patch

define $(package)_config_cmds
  export PKG_CONFIG_SYSROOT_DIR=/ && \
  export PKG_CONFIG_LIBDIR=$(host_prefix)/lib/pkgconfig && \
  export PKG_CONFIG_PATH=$(host_prefix)/share/pkgconfig && \
  cd qtbase && \
  ./configure -top-level $($(package)_config_opts) -- $($(package)_cmake_opts)
endef

define $(package)_build_cmds
  cmake --build . -- $(filter -j%,$(MAKEFLAGS))
endef

define $(package)_stage_cmds
  cmake --install . --prefix $($(package)_staging_prefix_dir) --strip
endef

define $(package)_postprocess_cmds
  rm -rf doc/
endef
