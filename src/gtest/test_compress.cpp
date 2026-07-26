// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "compressor.h"
#include "util.h"
#include "gtest/gtestutils.h"

#include <stdint.h>

#include <gtest/gtest.h>

// amounts 0.00000001 .. 0.00100000
#define NUM_MULTIPLES_UNIT 100000

// amounts 0.01 .. 100.00
#define NUM_MULTIPLES_CENT 10000

// amounts 1 .. 10000
#define NUM_MULTIPLES_1BTC 10000

// amounts 50 .. 21000000
#define NUM_MULTIPLES_50BTC 420000

class compress_tests : public BitcoinBasicTestingSetup {};

static bool TestEncode(uint64_t in) {
    return in == CTxOutCompressor::DecompressAmount(CTxOutCompressor::CompressAmount(in));
}

static bool TestDecode(uint64_t in) {
    return in == CTxOutCompressor::CompressAmount(CTxOutCompressor::DecompressAmount(in));
}

static bool TestPair(uint64_t dec, uint64_t enc) {
    return CTxOutCompressor::CompressAmount(dec) == enc &&
           CTxOutCompressor::DecompressAmount(enc) == dec;
}

TEST_F(compress_tests, compress_amounts)
{
    EXPECT_TRUE(TestPair(            0,       0x0));
    EXPECT_TRUE(TestPair(            1,       0x1));
    EXPECT_TRUE(TestPair(         CENT,       0x7));
    EXPECT_TRUE(TestPair(         COIN,       0x9));
    EXPECT_TRUE(TestPair(      50*COIN,      0x32));
    // MAX_MONEY on this fork is 200,000,000*COIN (komodo_globals.cpp), not the
    // vanilla 21,000,000*COIN the original Bitcoin/Zcash test vector assumed -
    // the compressed representation differs accordingly (CompressAmount strips
    // MAX_MONEY's 8 trailing zero digits, capping the exponent at e=9, so it
    // compresses to plain 1 + (n-1)*10 + 9 = 200000000 = 0xbebc200).
    EXPECT_TRUE(TestPair(MAX_MONEY, 0xbebc200));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_UNIT; i++)
        EXPECT_TRUE(TestEncode(i));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_CENT; i++)
        EXPECT_TRUE(TestEncode(i * CENT));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_1BTC; i++)
        EXPECT_TRUE(TestEncode(i * COIN));

    for (uint64_t i = 1; i <= NUM_MULTIPLES_50BTC; i++)
        EXPECT_TRUE(TestEncode(i * 50 * COIN));

    for (uint64_t i = 0; i < 100000; i++)
        EXPECT_TRUE(TestDecode(i));
}
