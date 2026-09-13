# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Vendored UniValue. Source list from src/univalue/Makefile.am.

add_library(univalue STATIC
  lib/univalue.cpp
  lib/univalue_get.cpp
  lib/univalue_read.cpp
  lib/univalue_write.cpp
)
target_include_directories(univalue PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(univalue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(univalue PRIVATE core_interface)
set_target_properties(univalue PROPERTIES POSITION_INDEPENDENT_CODE ON)
