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
  port/port_posix.cc
)
target_include_directories(leveldb PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/helpers/memenv
)
target_compile_definitions(leveldb PRIVATE
  OS_LINUX
  LEVELDB_PLATFORM_POSIX
  LEVELDB_ATOMIC_PRESENT
  __STDC_LIMIT_MACROS
)
target_link_libraries(leveldb PRIVATE core_interface Threads::Threads)
target_include_directories(leveldb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# CRC32 hardware-acceleration TU, kept separate so only it gets -msse4.2.
add_library(leveldb_sse42 STATIC port/port_posix_sse.cc)
target_include_directories(leveldb_sse42 PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_compile_definitions(leveldb_sse42 PRIVATE
  OS_LINUX
  LEVELDB_PLATFORM_POSIX
  LEVELDB_PLATFORM_POSIX_SSE
  LEVELDB_ATOMIC_PRESENT
  __STDC_LIMIT_MACROS
)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
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
  OS_LINUX
  LEVELDB_PLATFORM_POSIX
  LEVELDB_ATOMIC_PRESENT
  __STDC_LIMIT_MACROS
)
target_link_libraries(memenv PRIVATE core_interface)
