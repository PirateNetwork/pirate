# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Resolves one of depends/'s built third-party libraries into a
# `PkgConfig::<prefix>`-named IMPORTED target, the same target name
# `pkg_check_modules(<prefix> ... IMPORTED_TARGET ...)` would produce -- so
# every call site in src/CMakeLists.txt can say `PkgConfig::libssl` etc
# regardless of platform.
#
# On Linux/BSD this just defers to pkg-config (depends' toolchain.cmake
# points PKG_CONFIG_PATH/LIBDIR at the depends prefix already).
#
# On Windows/macOS this does NOT use pkg-config at all: autotools'
# configure.ac deliberately disables it for MinGW ("pkgconfig does more harm
# than good with MinGW"), and depends doesn't guarantee a matching
# cross pkg-config setup on those hosts either. Instead, resolve the library
# and header directly with find_library()/find_path() against the depends
# prefix and any EXTRA_LIBS the caller names (the system libraries a static
# build of that dependency itself needs, e.g. -lz for a static libcurl).
#
# Usage:
#   find_dep_package(<prefix> PKGCONFIG_NAME <pc-name> LIBRARY_NAMES <names...>
#                     [HEADER <path/to/header.h>] [EXTRA_LIBS <names...>])
function(find_dep_package prefix)
  cmake_parse_arguments(FDP "" "PKGCONFIG_NAME;HEADER" "LIBRARY_NAMES;EXTRA_LIBS" ${ARGN})

  if(CMAKE_SYSTEM_NAME MATCHES "^(Linux|FreeBSD|OpenBSD)$")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(${prefix} REQUIRED IMPORTED_TARGET ${FDP_PKGCONFIG_NAME})
    return()
  endif()

  find_library(${prefix}_LIBRARY NAMES ${FDP_LIBRARY_NAMES}
    PATHS ${DEPENDS_PREFIX}/lib NO_DEFAULT_PATH REQUIRED
  )
  if(FDP_HEADER)
    find_path(${prefix}_INCLUDE_DIR NAMES ${FDP_HEADER}
      PATHS ${DEPENDS_PREFIX}/include NO_DEFAULT_PATH REQUIRED
    )
  else()
    set(${prefix}_INCLUDE_DIR ${DEPENDS_PREFIX}/include)
  endif()

  add_library(PkgConfig::${prefix} STATIC IMPORTED GLOBAL)
  set_target_properties(PkgConfig::${prefix} PROPERTIES
    IMPORTED_LOCATION ${${prefix}_LIBRARY}
    INTERFACE_INCLUDE_DIRECTORIES ${${prefix}_INCLUDE_DIR}
  )
  if(FDP_EXTRA_LIBS)
    target_link_libraries(PkgConfig::${prefix} INTERFACE ${FDP_EXTRA_LIBS})
  endif()
endfunction()
