# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# libsecp256k1 is a vendored autotools fork (komodo's, with the MuSig/recovery
# modules). Rather than re-transcribe its generated-context build, drive its
# existing ./configure && make. Sub-configure args match configure.ac's
# ac_configure_args for AC_CONFIG_SUBDIRS.

include(ExternalProject)

set(_secp_lib ${CMAKE_CURRENT_SOURCE_DIR}/.libs/libsecp256k1.a)

ExternalProject_Add(secp256k1_ep
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}
  BUILD_IN_SOURCE   1
  # configure is generated, not committed (see .gitignore), so autogen.sh has
  # to run first. The retired top-level autogen.sh used to cover both this and
  # cryptoconditions via autoreconf recursing through AC_CONFIG_SUBDIRS.
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR} ./autogen.sh
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure
                      --disable-shared --with-pic --with-bignum=no
                      --enable-module-recovery
                      --disable-tests --disable-benchmark --disable-exhaustive-tests
                      CC=${CMAKE_C_COMPILER}
                      "CFLAGS=${CMAKE_C_FLAGS}"
  BUILD_COMMAND     $(MAKE) libsecp256k1.la
  INSTALL_COMMAND   ""
  BUILD_BYPRODUCTS  ${_secp_lib}
  LOG_CONFIGURE     1
  LOG_BUILD         1
)

add_library(secp256k1 STATIC IMPORTED GLOBAL)
set_target_properties(secp256k1 PROPERTIES IMPORTED_LOCATION ${_secp_lib})
target_include_directories(secp256k1 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_dependencies(secp256k1 secp256k1_ep)
