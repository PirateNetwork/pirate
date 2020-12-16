// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_POLICY_POLICY_H
#define KOMODO_POLICY_POLICY_H

#if defined(linux) || defined(__linux)
#include <sys/types.h>
#endif

#if defined(_WINDOWS) || defined(_WIN32)
#if defined(__MINGW__) || defined(__MINGW32__) || defined(__MINGW64__)
/** x86_64-w64-mingw32-gcc detect */
#include <sys/types.h>
#endif
#endif

#include "consensus/consensus.h"
#include "script/interpreter.h"
#include "script/standard.h"

#include <string>

extern unsigned int nBytesPerSigOp;

/** Default for -bytespersigop */
static const unsigned int DEFAULT_BYTES_PER_SIGOP = 20;
/** Default for -maxmempool, maximum megabytes of mempool memory usage */
static const unsigned int DEFAULT_MAX_MEMPOOL_SIZE = 300;

/** Compute the virtual transaction size (weight reinterpreted as bytes). */
int64_t GetVirtualTransactionSize(int64_t nWeight, int64_t nSigOpCost);
int64_t GetVirtualTransactionSize(const CTransaction& tx, int64_t nSigOpCost = 0);

#endif // KOMODO_POLICY_POLICY_H
