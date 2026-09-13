# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# cryptoconditions is a vendored autotools fork with generated ASN.1 sources.
# Drive its existing ./configure && make rather than re-transcribe the ASN.1
# codegen.

include(ExternalProject)

set(_cc_lib ${CMAKE_CURRENT_SOURCE_DIR}/libcryptoconditions_core.a)

ExternalProject_Add(cryptoconditions_ep
  SOURCE_DIR        ${CMAKE_CURRENT_SOURCE_DIR}
  BUILD_IN_SOURCE   1
  # configure is generated, not committed, so autogen.sh has to run first --
  # see the same note in src/secp256k1/CMakeLists.txt.
  CONFIGURE_COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_SOURCE_DIR} ./autogen.sh
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/configure
                      --disable-shared --with-pic
                      CC=${CMAKE_C_COMPILER}
                      "CFLAGS=${CMAKE_C_FLAGS}"
  BUILD_COMMAND     $(MAKE) libcryptoconditions_core.a
  INSTALL_COMMAND   ""
  BUILD_BYPRODUCTS  ${_cc_lib}
  LOG_CONFIGURE     1
  LOG_BUILD         1
)

add_library(cryptoconditions STATIC IMPORTED GLOBAL)
set_target_properties(cryptoconditions PROPERTIES IMPORTED_LOCATION ${_cc_lib})
target_include_directories(cryptoconditions INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/src/asn
)
add_dependencies(cryptoconditions cryptoconditions_ep)
