
#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include "testutils.h"
#include "komodo_extern_globals.h"
#include "consensus/validation.h"
#include <gtest/gtest.h>

TEST(PoW, DifficultyAveraging) {
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].SetHeight(i);
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower = i ? (CChainPower(&blocks[i]) + blocks[i - 1].chainPower) + GetBlockProof(blocks[i - 1]) : CChainPower(&blocks[i]);
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
        blocks[i].SetHeight(params.nPowAllowMinDifficultyBlocksAfterHeight.get() + i);
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
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
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }

    // difficulty should have decreased ( nBits increased )
    EXPECT_GT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

    // diffuculty should never decrease below minimum
    arith_uint256 minWork = UintToArith256(params.powLimit);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].nBits = minWork.GetCompact();
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), minWork.GetCompact());

    // space all blocks out so the median is under limits and difficulty should increase
    nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + (params.MinActualTimespan() / params.nPowAveragingWindow - 1);
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }

    // difficulty should have increased ( nBits decreased )
    EXPECT_LT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

}

TEST(PoW, TestStopAt)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    lastBlock = chain.generateBlock(); // now we should be above 1
    ASSERT_GT( chain.GetIndex()->GetHeight(), 1);
    CBlock block;
    CValidationState state;
    KOMODO_STOPAT = 1;
    EXPECT_FALSE( ConnectBlock(block, state, chain.GetIndex(), *chain.GetCoinsViewCache(), false, true) );
}


TEST(PoW, TestConnectWithoutChecks)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Add some transaction to a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    auto notaryPrevOut = notary->GetAvailable(100000);
    ASSERT_TRUE(notaryPrevOut.first.vout.size() > 0);
    CMutableTransaction tx;
    CTxIn notaryIn;
    notaryIn.prevout.hash = notaryPrevOut.first.GetHash();
    notaryIn.prevout.n = notaryPrevOut.second;
    tx.vin.push_back(notaryIn);
    CTxOut aliceOut;
    aliceOut.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceOut.nValue = 100000;
    tx.vout.push_back(aliceOut);
    CTxOut notaryOut;
    notaryOut.scriptPubKey = GetScriptForDestination(notary->GetPubKey());
    notaryOut.nValue = notaryPrevOut.first.vout[notaryPrevOut.second].nValue - 100000;
    tx.vout.push_back(notaryOut);
    // sign it
    uint256 hash = SignatureHash(notaryPrevOut.first.vout[notaryPrevOut.second].scriptPubKey, tx, 0, SIGHASH_ALL, 0, 0);
    tx.vin[0].scriptSig << notary->Sign(hash, SIGHASH_ALL);
    CTransaction fundAlice(tx);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual tx
    block.vtx.push_back(fundAlice);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto view = chain.GetCoinsViewCache();
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_TRUE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    if (!state.IsValid() )
        FAIL() << state.GetRejectReason();
}

TEST(PoW, TestSpendInSameBlock)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Add some transaction to a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    auto notaryPrevOut = notary->GetAvailable(100000);
    ASSERT_TRUE(notaryPrevOut.first.vout.size() > 0);
    CMutableTransaction tx;
    CTxIn notaryIn;
    notaryIn.prevout.hash = notaryPrevOut.first.GetHash();
    notaryIn.prevout.n = notaryPrevOut.second;
    tx.vin.push_back(notaryIn);
    CTxOut aliceOut;
    aliceOut.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceOut.nValue = 100000;
    tx.vout.push_back(aliceOut);
    CTxOut notaryOut;
    notaryOut.scriptPubKey = GetScriptForDestination(notary->GetPubKey());
    notaryOut.nValue = notaryPrevOut.first.vout[notaryPrevOut.second].nValue - 100000;
    tx.vout.push_back(notaryOut);
    // sign it
    uint256 hash = SignatureHash(notaryPrevOut.first.vout[notaryPrevOut.second].scriptPubKey, tx, 0, SIGHASH_ALL, 0, 0);
    tx.vin[0].scriptSig << notary->Sign(hash, SIGHASH_ALL);
    CTransaction fundAlice(tx);
    // now have Alice move some funds to Bob in the same block
    CMutableTransaction aliceToBobMutable;
    CTxIn aliceIn;
    aliceIn.prevout.hash = fundAlice.GetHash();
    aliceIn.prevout.n = 0;
    aliceToBobMutable.vin.push_back(aliceIn);
    CTxOut bobOut;
    bobOut.scriptPubKey = GetScriptForDestination(bob->GetPubKey());
    bobOut.nValue = 10000;
    aliceToBobMutable.vout.push_back(bobOut);
    CTxOut aliceRemainder;
    aliceRemainder.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder.nValue = aliceOut.nValue - 10000;
    aliceToBobMutable.vout.push_back(aliceRemainder);
    hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToBobMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToBobMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToBobTx(aliceToBobMutable);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual txs
    block.vtx.push_back(fundAlice);
    block.vtx.push_back(aliceToBobTx);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_TRUE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    if (!state.IsValid() )
        FAIL() << state.GetRejectReason();
}

TEST(PoW, TestDoubleSpendInSameBlock)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    auto charlie = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Add some transaction to a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    auto notaryPrevOut = notary->GetAvailable(100000);
    ASSERT_TRUE(notaryPrevOut.first.vout.size() > 0);
    CMutableTransaction tx;
    CTxIn notaryIn;
    notaryIn.prevout.hash = notaryPrevOut.first.GetHash();
    notaryIn.prevout.n = notaryPrevOut.second;
    tx.vin.push_back(notaryIn);
    CTxOut aliceOut;
    aliceOut.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceOut.nValue = 100000;
    tx.vout.push_back(aliceOut);
    CTxOut notaryOut;
    notaryOut.scriptPubKey = GetScriptForDestination(notary->GetPubKey());
    notaryOut.nValue = notaryPrevOut.first.vout[notaryPrevOut.second].nValue - 100000;
    tx.vout.push_back(notaryOut);
    // sign it
    uint256 hash = SignatureHash(notaryPrevOut.first.vout[notaryPrevOut.second].scriptPubKey, tx, 0, SIGHASH_ALL, 0, 0);
    tx.vin[0].scriptSig << notary->Sign(hash, SIGHASH_ALL);
    CTransaction fundAlice(tx);
    // now have Alice move some funds to Bob in the same block
    CMutableTransaction aliceToBobMutable;
    CTxIn aliceIn;
    aliceIn.prevout.hash = fundAlice.GetHash();
    aliceIn.prevout.n = 0;
    aliceToBobMutable.vin.push_back(aliceIn);
    CTxOut bobOut;
    bobOut.scriptPubKey = GetScriptForDestination(bob->GetPubKey());
    bobOut.nValue = 10000;
    aliceToBobMutable.vout.push_back(bobOut);
    CTxOut aliceRemainder;
    aliceRemainder.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder.nValue = aliceOut.nValue - 10000;
    aliceToBobMutable.vout.push_back(aliceRemainder);
    hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToBobMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToBobMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToBobTx(aliceToBobMutable);
    // alice attempts to double spend and send the same to charlie
    CMutableTransaction aliceToCharlieMutable;
    CTxIn aliceIn2;
    aliceIn2.prevout.hash = fundAlice.GetHash();
    aliceIn2.prevout.n = 0;
    aliceToCharlieMutable.vin.push_back(aliceIn2);
    CTxOut charlieOut;
    charlieOut.scriptPubKey = GetScriptForDestination(charlie->GetPubKey());
    charlieOut.nValue = 10000;
    aliceToCharlieMutable.vout.push_back(charlieOut);
    CTxOut aliceRemainder2;
    aliceRemainder2.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder2.nValue = aliceOut.nValue - 10000;
    aliceToCharlieMutable.vout.push_back(aliceRemainder2);
    hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToCharlieMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToCharlieMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToCharlieTx(aliceToCharlieMutable);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual txs
    block.vtx.push_back(fundAlice);
    block.vtx.push_back(aliceToBobTx);
    block.vtx.push_back(aliceToCharlieTx);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_FALSE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txns-inputs-missingorspent");
}

