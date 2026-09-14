# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Vendored LevelDB. Source list transcribed from src/Makefile.leveldb.include.

add_library(leveldb STATIC
  db/builder.cc
  db/c.cc
  db/dbformat.cc
  db/db_impl.cc
  db/db_iter.cc
  db/dumpfile.cc
  db/filename.cc
  db/log_reader.cc
  db/log_writer.cc
  db/memtable.cc
  db/repair.cc
  db/table_cache.cc
  db/version_edit.cc
  db/version_set.cc
  db/write_batch.cc
  table/block_builder.cc
  table/block.cc
  table/filter_block.cc
  table/format.cc
  table/iterator.cc
  table/merger.cc
  table/table_builder.cc
  table/table.cc
  table/two_level_iterator.cc
  util/arena.cc
  util/bloom.cc
  util/cache.cc
  util/coding.cc
  util/comparator.cc
  util/crc32c.cc
  util/env.cc
  util/env_posix.cc
  util/filter_policy.cc
  util/hash.cc
  util/histogram.cc
  util/logging.cc
  util/options.cc
  util/status.cc
)
# Platform port implementation, matching Makefile.leveldb.include: env_posix.cc
# builds everywhere, but the port layer is one or the other, and Windows adds
# env_win.cc. port.h includes port_win.h under LEVELDB_PLATFORM_WINDOWS, so
# compiling port_posix.cc there pulls in port_posix.h and its <endian.h>.
if(WIN32)
  target_sources(leveldb PRIVATE port/port_win.cc util/env_win.cc)
else()
  target_sources(leveldb PRIVATE port/port_posix.cc)
endif()

target_include_directories(leveldb PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/helpers/memenv
)
# Platform selection for leveldb's port layer, mirroring configure.ac's
# LEVELDB_TARGET_FLAGS and Makefile.leveldb.include. port_posix.h keys its
# endian header off these: OS_MACOSX picks machine/endian.h, and everything
# else falls through to <endian.h>, which does not exist on macOS. Linux
# deliberately defines no OS_* macro, exactly as the autotools build did.
set(LEVELDB_PLATFORM_DEFS LEVELDB_ATOMIC_PRESENT __STDC_LIMIT_MACROS)
if(APPLE)
  list(APPEND LEVELDB_PLATFORM_DEFS OS_MACOSX LEVELDB_PLATFORM_POSIX)
elseif(WIN32)
  list(APPEND LEVELDB_PLATFORM_DEFS LEVELDB_PLATFORM_WINDOWS __USE_MINGW_ANSI_STDIO=1)
else()
  list(APPEND LEVELDB_PLATFORM_DEFS LEVELDB_PLATFORM_POSIX)
endif()

target_compile_definitions(leveldb PRIVATE
  ${LEVELDB_PLATFORM_DEFS}
)
target_link_libraries(leveldb PRIVATE core_interface Threads::Threads)
target_include_directories(leveldb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# CRC32 hardware-acceleration TU, kept separate so only it gets -msse4.2.
add_library(leveldb_sse42 STATIC port/port_posix_sse.cc)
target_include_directories(leveldb_sse42 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# port_posix_sse.cc keys entirely off LEVELDB_PLATFORM_POSIX_SSE: define it and
# the file uses _mm_crc32_* unconditionally, which does not exist off x86. Feature
# -test the intrinsics rather than matching on CMAKE_SYSTEM_PROCESSOR, mirroring
# configure.ac's ENABLE_HWCRC32 check, and gate both the define and the flag on
# the result as Makefile.leveldb.include did. Without the define the file falls
# back to a portable CRC32 implementation.
include(CheckSourceCompilesWithFlags)
check_cxx_source_compiles_with_flags("
  #include <stdint.h>
  #include <nmmintrin.h>
  int main() {
    uint64_t l = 0;
    l = _mm_crc32_u8(l, 0);
    l = _mm_crc32_u32(l, 0);
    l = _mm_crc32_u64(l, 0);
    return (int)l;
  }
  " HAVE_HWCRC32 CXXFLAGS -msse4.2
)

target_compile_definitions(leveldb_sse42 PRIVATE ${LEVELDB_PLATFORM_DEFS})
if(HAVE_HWCRC32)
  target_compile_definitions(leveldb_sse42 PRIVATE LEVELDB_PLATFORM_POSIX_SSE)
  target_compile_options(leveldb_sse42 PRIVATE -msse4.2)
endif()
target_link_libraries(leveldb_sse42 PRIVATE core_interface)
target_link_libraries(leveldb PRIVATE leveldb_sse42)

add_library(memenv STATIC helpers/memenv/memenv.cc)
target_include_directories(memenv PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/helpers/memenv
)
target_compile_definitions(memenv PRIVATE
  ${LEVELDB_PLATFORM_DEFS}
)
target_link_libraries(memenv PRIVATE core_interface)
