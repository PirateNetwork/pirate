// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef BITCOIN_RANDOM_H
#define BITCOIN_RANDOM_H

#include "crypto/chacha20.h"
#include "crypto/common.h"
#include "uint256.h"

#include <functional>
#include <stdint.h>

/**
 * Functions to gather random data via the libsodium CSPRNG
 */
void GetRandBytes(unsigned char* buf, size_t num);
uint64_t GetRand(uint64_t nMax);
int GetRandInt(int nMax);
uint256 GetRandHash();

/**
 * Identity function for MappedShuffle, so that elements retain their original order.
 */
 int GenIdentity(int n);

/**
 * Rearranges the elements in the range [first,first+len) randomly, assuming
 * that gen is a uniform random number generator. Follows the same algorithm as
 * std::shuffle in C++11 (a Durstenfeld shuffle).
 *
 * The elements in the range [mapFirst,mapFirst+len) are rearranged according to
 * the same permutation, enabling the permutation to be tracked by the caller.
 *
 * gen takes an integer n and produces a uniform random output in [0,n).
 */
template <typename RandomAccessIterator, typename MapRandomAccessIterator>
void MappedShuffle(RandomAccessIterator first,
                   MapRandomAccessIterator mapFirst,
                   size_t len,
                   std::function<int(int)> gen)
{
    for (size_t i = len-1; i > 0; --i) {
        auto r = gen(i+1);
        assert(r >= 0);
        assert(r <= i);
        std::swap(first[i], first[r]);
        std::swap(mapFirst[i], mapFirst[r]);
    }
}

/**
 * Fast randomness source. This is seeded once with secure random data, but
 * is completely deterministic and insecure after that.
 * This class is not thread-safe.
 */
class FastRandomContext {
private:
    bool requires_seed;
    ChaCha20 rng;

    unsigned char bytebuf[64];
    int bytebuf_size;

    uint64_t bitbuf;
    int bitbuf_size;

    void RandomSeed();

    void FillByteBuffer()
    {
        if (requires_seed) {
            RandomSeed();
        }
        rng.Output(bytebuf, sizeof(bytebuf));
        bytebuf_size = sizeof(bytebuf);
    }

    void FillBitBuffer()
    {
        bitbuf = rand64();
        bitbuf_size = 64;
    }

public:
    explicit FastRandomContext(bool fDeterministic = false);

    /** Initialize with explicit seed (only for testing) */
    explicit FastRandomContext(const uint256& seed);

    /** Generate a random 64-bit integer. */
    uint64_t rand64()
    {
        if (bytebuf_size < 8) FillByteBuffer();
        uint64_t ret = ReadLE64(bytebuf + 64 - bytebuf_size);
        bytebuf_size -= 8;
        return ret;
    }

    /** Generate a random (bits)-bit integer. */
    uint64_t randbits(int bits) {
        if (bits == 0) {
            return 0;
        } else if (bits > 32) {
            return rand64() >> (64 - bits);
        } else {
            if (bitbuf_size < bits) FillBitBuffer();
            uint64_t ret = bitbuf & (~(uint64_t)0 >> (64 - bits));
            bitbuf >>= bits;
            bitbuf_size -= bits;
            return ret;
        }
    }

    /** Generate a random integer in the range [0..range). */
    uint64_t randrange(uint64_t range)
    {
        --range;
        int bits = CountBits(range);
        while (true) {
            uint64_t ret = randbits(bits);
            if (ret <= range) return ret;
        }
    }

    /** Generate random bytes. */
    std::vector<unsigned char> randbytes(size_t len);

    /** Generate a random 32-bit integer. */
    uint32_t rand32() { return randbits(32); }

    /** generate a random uint256. */
    uint256 rand256();

    /** Generate a random boolean. */
    bool randbool() { return randbits(1); }
};

/**
 * Seed insecure_rand using the random pool.
 * @param Deterministic Use a deterministic seed
 */
void seed_insecure_rand(bool fDeterministic = false);

/**
 * MWC RNG of George Marsaglia
 * This is intended to be fast. It has a period of 2^59.3, though the
 * least significant 16 bits only have a period of about 2^30.1.
 *
 * @return random value
 */
extern uint32_t insecure_rand_Rz;
extern uint32_t insecure_rand_Rw;
static inline uint32_t insecure_rand(void)
{
    insecure_rand_Rz = 36969 * (insecure_rand_Rz & 65535) + (insecure_rand_Rz >> 16);
    insecure_rand_Rw = 18000 * (insecure_rand_Rw & 65535) + (insecure_rand_Rw >> 16);
    return (insecure_rand_Rw << 16) + insecure_rand_Rz;
}

#endif // BITCOIN_RANDOM_H
