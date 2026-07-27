// Copyright (c) 2015 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "pow.h"
#include "util.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

// Isolated exact-value regression tests of the raw next-work-required retarget
// arithmetic and GetBlockProofEquivalentTime(), independent of any live chain
// state (contrast with gtest/test_pow.cpp's chain-driven integration tests).
//
// Named _bitcoin to avoid colliding with the pre-existing gtest/test_pow.cpp
// (PoW.DifficultyAveraging / PoW.MinDifficultyRules), which tests different code paths.
class pow_tests : public BitcoinBasicTestingSetup {};

/* Test calculation of next difficulty target with no constraints applying */
TEST_F(pow_tests, get_next_work)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1262149169; // NOTE: Not an actual block time
    int64_t nThisTime = 1262152739;  // Block #32255 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1d00ffff);
    EXPECT_EQ(0x1d011998u,
              CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the upper bound for next work */
TEST_F(pow_tests, get_next_work_pow_limit)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1231006505; // Block #0 of Bitcoin
    int64_t nThisTime = 1233061996;  // Block #2015 of Bitcoin
    arith_uint256 bnAvg;
    // TODO change once the harder genesis block is generated
    bnAvg.SetCompact(KOMODO_MINDIFF_NBITS);
    EXPECT_EQ(KOMODO_MINDIFF_NBITS,
        CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the lower bound for actual time taken */
TEST_F(pow_tests, get_next_work_lower_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1279296753; // NOTE: Not an actual block time
    int64_t nThisTime = 1279297671;  // Block #68543 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1c05a3f4);
    EXPECT_EQ(0x1c04bcebu,
              CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

/* Test the constraint on the upper bound for actual time taken */
TEST_F(pow_tests, get_next_work_upper_limit_actual)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    int64_t nLastRetargetTime = 1269205629; // NOTE: Not an actual block time
    int64_t nThisTime = 1269211443;  // Block #46367 of Bitcoin
    arith_uint256 bnAvg;
    bnAvg.SetCompact(0x1c387f6f);
    EXPECT_EQ(0x1c4a93bbu,
              CalculateNextWorkRequired(bnAvg, nThisTime, nLastRetargetTime, params));
}

TEST_F(pow_tests, GetBlockProofEquivalentTime_test)
{
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();

    std::vector<CBlockIndex> blocks(10000);
    for (int i = 0; i < 10000; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : NULL;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x207fffff; /* target 0x7fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    for (int j = 0; j < 1000; j++) {
        CBlockIndex *p1 = &blocks[GetRand(10000)];
        CBlockIndex *p2 = &blocks[GetRand(10000)];
        CBlockIndex *p3 = &blocks[GetRand(10000)];

        int64_t tdiff = GetBlockProofEquivalentTime(*p1, *p2, *p3, params);
        EXPECT_EQ(tdiff, p1->GetBlockTime() - p2->GetBlockTime());
    }
}
