# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

include_guard(GLOBAL)

# Runs the platform feature checks that autotools' configure.ac previously did
# and writes src/config/bitcoin-config.h from cmake/bitcoin-config.h.cmake.in.

include(CheckIncludeFileCXX)
include(CheckCXXSymbolExists)
include(CheckCXXSourceCompiles)
include(TestBigEndian)

function(generate_config_header output_file)
  set(CMAKE_REQUIRED_QUIET TRUE)

  # --- Headers -------------------------------------------------------------
  foreach(pair IN ITEMS
    "byteswap.h=HAVE_BYTESWAP_H"
    "endian.h=HAVE_ENDIAN_H"
    "sys/endian.h=HAVE_SYS_ENDIAN_H"
    "dlfcn.h=HAVE_DLFCN_H"
    "inttypes.h=HAVE_INTTYPES_H"
    "stdint.h=HAVE_STDINT_H"
    "stdio.h=HAVE_STDIO_H"
    "stdlib.h=HAVE_STDLIB_H"
    "strings.h=HAVE_STRINGS_H"
    "string.h=HAVE_STRING_H"
    "sys/prctl.h=HAVE_SYS_PRCTL_H"
    "sys/select.h=HAVE_SYS_SELECT_H"
    "sys/stat.h=HAVE_SYS_STAT_H"
    "sys/types.h=HAVE_SYS_TYPES_H"
    "unistd.h=HAVE_UNISTD_H"
  )
    string(REPLACE "=" ";" kv "${pair}")
    list(GET kv 0 header)
    list(GET kv 1 var)
    check_include_file_cxx("${header}" ${var})
  endforeach()

  # --- Declarations (HAVE_DECL_*, always 0/1) -----------------------------
  set(_decl_headers "#include <cstring>\n#include <cstdlib>")
  if(HAVE_ENDIAN_H)
    string(APPEND _decl_headers "\n#include <endian.h>")
  endif()
  if(HAVE_SYS_ENDIAN_H)
    string(APPEND _decl_headers "\n#include <sys/endian.h>")
  endif()
  if(HAVE_BYTESWAP_H)
    string(APPEND _decl_headers "\n#include <byteswap.h>")
  endif()

  # Mirror autoconf's AC_CHECK_DECL: the name counts as "declared" whether it
  # is a real declaration or just a function-like macro (glibc defines be16toh,
  # bswap_16, ... as macros), so only reference it when it is *not* a macro.
  foreach(decl
    be16toh be32toh be64toh
    htobe16 htobe32 htobe64
    htole16 htole32 htole64
    le16toh le32toh le64toh
    bswap_16 bswap_32 bswap_64
    strerror_r strnlen
  )
    string(TOUPPER "HAVE_DECL_${decl}" var)
    check_cxx_source_compiles("
      ${_decl_headers}
      int main() {
      #ifndef ${decl}
        (void) ${decl};
      #endif
        return 0;
      }
    " ${var})
    if(${var})
      set(${var} 1 PARENT_SCOPE)
    else()
      set(${var} 0 PARENT_SCOPE)
    endif()
  endforeach()

  # --- Functions / symbols ----------------------------------------------
  check_cxx_symbol_exists(strerror_r "cstring" HAVE_STRERROR_R)

  check_cxx_source_compiles("
    #include <sys/socket.h>
    int main() { return MSG_NOSIGNAL; }
  " HAVE_MSG_NOSIGNAL)

  # GNU (char*) vs XSI (int) strerror_r.
  check_cxx_source_compiles("
    #include <cstring>
    int main() { char buf[100]; char* p = strerror_r(0, buf, sizeof(buf)); return !p; }
  " STRERROR_R_CHAR_P)

  # --- Compiler attributes --------------------------------------------------
  check_cxx_source_compiles("
    int foo(void) __attribute__((visibility(\"default\")));
    int foo(void) { return 0; }
    int main() { return foo(); }
  " HAVE_FUNC_ATTRIBUTE_VISIBILITY)
  set(HAVE_VISIBILITY_ATTRIBUTE ${HAVE_FUNC_ATTRIBUTE_VISIBILITY})

  # --- Threads ------------------------------------------------------------
  if(TARGET Threads::Threads)
    set(HAVE_PTHREAD 1)
    set(CMAKE_REQUIRED_LIBRARIES Threads::Threads)
    check_cxx_source_compiles("
      #include <pthread.h>
      int main() { return PTHREAD_PRIO_INHERIT; }
    " HAVE_PTHREAD_PRIO_INHERIT)
    unset(CMAKE_REQUIRED_LIBRARIES)
  endif()

  # --- Endianness -------------------------------------------------------
  test_big_endian(WORDS_BIGENDIAN)

  set(STDC_HEADERS 1)

  # --- Propagate everything the template references ----------------------
  foreach(v
    HAVE_BYTESWAP_H HAVE_ENDIAN_H HAVE_SYS_ENDIAN_H HAVE_DLFCN_H HAVE_INTTYPES_H
    HAVE_STDINT_H HAVE_STDIO_H HAVE_STDLIB_H HAVE_STRINGS_H HAVE_STRING_H
    HAVE_SYS_PRCTL_H HAVE_SYS_SELECT_H HAVE_SYS_STAT_H HAVE_SYS_TYPES_H HAVE_UNISTD_H
    HAVE_STRERROR_R HAVE_MSG_NOSIGNAL STRERROR_R_CHAR_P
    HAVE_FUNC_ATTRIBUTE_VISIBILITY HAVE_VISIBILITY_ATTRIBUTE
    HAVE_PTHREAD HAVE_PTHREAD_PRIO_INHERIT WORDS_BIGENDIAN STDC_HEADERS
  )
    set(${v} "${${v}}" PARENT_SCOPE)
  endforeach()

  configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/bitcoin-config.h.cmake.in
    ${output_file}
    @ONLY
  )
  message(STATUS "Generated ${output_file}")
endfunction()
