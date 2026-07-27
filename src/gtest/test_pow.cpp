// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assetchain.h"
#include "chain.h"
#include "chainparams.h"
#include "komodo_extern_globals.h"
#include "pow.h"
#include "random.h"
#include <gtest/gtest.h>

// Chain-driven integration tests for the difficulty-averaging-window retarget
// algorithm, plus testnet minimum-difficulty and time-warp rules, exercised
// over a constructed chain of CBlockIndex entries.

TEST(PoW, DifficultyAveraging) {
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = i;
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= params.AveragingWindowTimespan();
    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += GetRand(params.nPowTargetSpacing/2) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
}

TEST(PoW, MinDifficultyRules) {
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    const uint32_t startTime = 1269211443;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    uint32_t nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + params.nPowTargetSpacing;
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].nHeight = params.nPowAllowMinDifficultyBlocksAfterHeight.value() + i;
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork 
                + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // Create a new block at the target spacing
    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.nPowTargetSpacing;

    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= params.AveragingWindowTimespan();
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block a bit, time warp protection should prevent any change
    next.nTime += params.nPowTargetSpacing * 5;

    // Result should be unchanged, modulo integer division precision loss
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block to a huge number. Result should be unchanged, time warp protection
    next.nTime = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // space all blocks out so the median is above the limits and difficulty should drop
    nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + ( params.MaxActualTimespan() / params.nPowAveragingWindow + 1);
        blocks[i].nTime = nextTime;
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork 
                + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // difficulty should have decreased ( nBits increased )
    EXPECT_GT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

    // diffuculty should never decrease below minimum
    arith_uint256 minWork = UintToArith256(params.powLimit);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].nBits = minWork.GetCompact();
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork 
                + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), minWork.GetCompact());

    // space all blocks out so the median is under limits and difficulty should increase
    nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + (params.MinActualTimespan() / params.nPowAveragingWindow - 1);
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].nChainWork = i ? blocks[i - 1].nChainWork 
                + GetBlockProof(blocks[i - 1]) : arith_uint256(0);
    }

    // difficulty should have increased ( nBits decreased )
    EXPECT_LT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

}

// Phase 3 consensus-rule audit: CheckProofOfWork() (pow.cpp) is the function
// that gates every block's validity against its claimed difficulty target -
// it had zero test coverage anywhere in the suite. This exercises the actual
// hash-vs-target comparison directly (not equihash-solution validity, which
// is a separate check via CheckEquihashSolution).
TEST(PoW, CheckProofOfWork) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();

    // A real chain symbol avoids the legacy KMD "isKMD() && height<=792000"
    // notary-priority bypass path in pow.cpp (a known non-issue on Pirate,
    // since chainName.isKMD() is always false here - see
    // treasurechest_attack_vectors.md's "Notes / non-issues" section) and the
    // height>34000 notary-election machinery entirely (skipped below 34000
    // regardless of chain identity).
    chainName = assetchain("TST");
    KOMODO_LOADINGBLOCKS = false;
    uint8_t pubkey33[33] = {0};
    int32_t height = 2;

    CBlockHeader header;
    header.nVersion = 4;
    header.nTime = (uint32_t)GetTime();
    header.nBits = UintToArith256(params.powLimit).GetCompact();

    // Search for a nonce whose header hash satisfies powLimit (the loosest
    // target this chain allows) - a real block would reach this via mining;
    // brute-forcing the (non-equihash-verified) header hash is equivalent for
    // exercising the actual hash-vs-target comparison this function performs.
    arith_uint256 target = UintToArith256(params.powLimit);
    bool found = false;
    for (uint32_t nonceTry = 0; nonceTry < 100000 && !found; nonceTry++) {
        header.nNonce = ArithToUint256(arith_uint256(nonceTry));
        if (UintToArith256(header.GetHash()) <= target) {
            found = true;
        }
    }
    ASSERT_TRUE(found) << "failed to find a header hash under powLimit within the search budget";

    EXPECT_TRUE(CheckProofOfWork(header, pubkey33, height, params));

    // Same header/nonce, but demand a target of 1 (i.e. hash must be <= 1) -
    // reuses the exact hash from the positive case above, only the claimed
    // target changes, isolating the hash-vs-target comparison itself.
    CBlockHeader tooHard = header;
    tooHard.nBits = arith_uint256(1).GetCompact();
    EXPECT_FALSE(CheckProofOfWork(tooHard, pubkey33, height, params));
}
