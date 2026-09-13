# Copyright (c) 2026 Pirate Chain developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or https://opensource.org/license/mit/.

# Generate embedded_binary_hashes.h from the exact tor/i2pd/pirate-networking
# binaries this build produced.
#
# embeddedprocess.cpp's FindBinary() uses these to refuse a same-named sibling
# binary that does not match what this build shipped. An EMPTY hash disables
# that check for its binary, so a missing input must stay visible rather than
# silently degrading into "verification off".
#
# Binaries are hashed after stripping a throwaway copy, so the recorded digest
# matches the stripped copy that actually ships.
#
#   cmake -DTOR_BIN=<path> -DI2PD_BIN=<path> -DNETWORKING_BIN=<path> \
#         -DSTRIP_TOOL=<strip> -DHEADER_OUT=<file>.h \
#         -P EmbeddedBinaryHashes.cmake

function(stripped_sha256 _path _out)
  set(${_out} "" PARENT_SCOPE)
  if(NOT _path OR NOT EXISTS "${_path}")
    return()
  endif()
  string(RANDOM LENGTH 12 _rnd)
  set(_tmp "${CMAKE_CURRENT_BINARY_DIR}/.ebh_${_rnd}")
  file(COPY_FILE "${_path}" "${_tmp}" RESULT _copy_err)
  if(_copy_err)
    return()
  endif()
  if(STRIP_TOOL)
    execute_process(COMMAND "${STRIP_TOOL}" "${_tmp}"
                    RESULT_VARIABLE _ignored
                    OUTPUT_QUIET ERROR_QUIET)
  endif()
  file(SHA256 "${_tmp}" _hash)
  file(REMOVE "${_tmp}")
  set(${_out} "${_hash}" PARENT_SCOPE)
endfunction()

stripped_sha256("${TOR_BIN}" _tor_hash)
stripped_sha256("${I2PD_BIN}" _i2pd_hash)
stripped_sha256("${NETWORKING_BIN}" _net_hash)

set(_content
"// Auto-generated at build time from the tor/i2pd/pirate-networking binaries this build produced - do not edit.
#ifndef BITCOIN_EMBEDDED_BINARY_HASHES_H
#define BITCOIN_EMBEDDED_BINARY_HASHES_H
#define EMBEDDED_TOR_SHA256 \"${_tor_hash}\"
#define EMBEDDED_I2PD_SHA256 \"${_i2pd_hash}\"
#define EMBEDDED_PIRATE_NETWORKING_SHA256 \"${_net_hash}\"
#endif
")

# Only rewrite on change, so touching an input does not force a rebuild of every
# translation unit that includes this header.
if(EXISTS "${HEADER_OUT}")
  file(READ "${HEADER_OUT}" _existing)
  if(_existing STREQUAL _content)
    return()
  endif()
endif()
file(WRITE "${HEADER_OUT}" "${_content}")
