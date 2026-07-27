// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include "gtest/gtestutils.h"
#include "komodo_extern_globals.h"
#include "consensus/validation.h"
#include "coincontrol.h"
#include "miner.h"

#include <thread>
#include <gtest/gtest.h>

// Integration-style tests for ConnectBlock()-level block-connection logic,
// driving a real TestChain to generate and connect blocks and checking
// consensus outcomes (e.g. coinbase/subsidy caps) that only surface once a
// block is actually connected to the active chain.

// NB! first generateBlock call changes IsInitialBlockDownload() to false globally (!), affects other tests

// header_size_is_expected duplicated BlockTests.HeaderSizeIsExpected (gtest/test_block.cpp) - removed.

TEST(test_block, TestStopAt)
{
    TestChain chain;
    // chainName defaults to KMD (empty symbol); GetBlockSubsidy() special-cases KMD
    // mainnet height 1 as a 100,000,000-coin "ICO allocation" coinbase, which exceeds
    // the vendored Rust crate's hardcoded 21,000,000*COIN cap and makes the coinbase
    // CTransaction unconstructable. A real Pirate daemon always configures a chain
    // symbol, so this branch never fires in production - use a non-KMD identity here.
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    ASSERT_GT( chain.GetIndex()->nHeight, 0 );
    lastBlock = chain.generateBlock(notary); // now we should be above 1
    ASSERT_GT( chain.GetIndex()->nHeight, 1);
    CBlock block;
    CValidationState state;
    KOMODO_STOPAT = 1;
    EXPECT_FALSE( chain.ConnectBlock(block, state, chain.GetIndex(), false, true) );
    KOMODO_STOPAT = 0; // to not stop other tests
}

TEST(test_block, TestConnectWithoutChecks)
{
    TestChain chain;
    // See TestStopAt's comment: KMD-default chainName at height 1 produces an
    // unconstructable coinbase under the current Rust digest FFI.
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    auto alice = std::make_shared<TestWallet>("alice");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    ASSERT_GT( chain.GetIndex()->nHeight, 0 );
    // Add some transaction to a block
    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    TransactionInProcess fundAlice = notary->CreateSpendTransaction(alice, 100000);
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
    block.vtx.push_back(fundAlice.transaction);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_TRUE( chain.ConnectBlock(block, state, &newIndex, true, false) );
    if (!state.IsValid() )
        FAIL() << state.GetRejectReason();
}

// dos_vulnerability_analysis.md / Phase 3 consensus-rule audit: ConnectBlock()'s
// "bad-cb-amount" check (ConnectBlock, guarding block.vtx[0].GetValueOut() against
// blockReward+KOMODO_EXTRASATOSHI) is the direct inflation-limit backstop - a
// coinbase that pays itself more than the computed subsidy must be rejected. Had
// no test coverage anywhere in the suite prior to this.
TEST(test_block, TestCoinbaseOverpayRejected)
{
    TestChain chain;
    // See TestStopAt's comment: KMD-default chainName at height 1 produces an
    // unconstructable coinbase under the current Rust digest FFI.
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    ASSERT_GT( chain.GetIndex()->nHeight, 0 );

    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    // Overpay by a full coin relative to the correct subsidy for newHeight.
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight, consensusParams) + COIN;
    txNew.nExpiryHeight = 0;

    CBlock block;
    block.vtx.push_back(CTransaction(txNew));

    CValidationState state;
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    newIndex.nHeight = newHeight; // ConnectBlock() reads pindex->nHeight directly, not derived from pprev.
    EXPECT_FALSE( chain.ConnectBlock(block, state, &newIndex, true, false) );
    EXPECT_EQ(state.GetRejectReason(), "bad-cb-amount");
}

// consensus_logic_audit.md CL-01 / Phase 3 consensus-rule audit: ConnectBlock()'s
// ZIP-209-style turnstile check rejects a block whose cumulative shielded-pool
// value has gone negative (main.cpp, right after the block-index nChainSupply
// bookkeeping). pindex->nChainSaplingValue/nChainIronwoodValue are computed
// upstream of ConnectBlock (in AddToBlockIndex/ReceivedBlockTransactions, not
// here), so a direct ConnectBlock()-level test can't derive a violation from
// real spends/outputs - it sets the already-computed cumulative field directly
// on a synthetic CBlockIndex, which is exactly what ConnectBlock reads. This
// check had no test coverage at all prior to this (the CL-01 audit finding
// itself turned out to be stale/pointed at the wrong line - the real fix
// already existed - but the fix was unverified).
TEST(test_block, TestTurnstileSaplingViolationRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    ASSERT_GT( chain.GetIndex()->nHeight, 0 );

    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight, consensusParams); // correctly paid, doesn't trip bad-cb-amount
    txNew.nExpiryHeight = 0;

    CBlock block;
    block.vtx.push_back(CTransaction(txNew));

    CValidationState state;
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    newIndex.nHeight = newHeight;
    newIndex.nChainSaplingValue = -1; // simulates the cumulative Sapling pool having gone negative
    EXPECT_FALSE( chain.ConnectBlock(block, state, &newIndex, true, false) );
    EXPECT_EQ(state.GetRejectReason(), "turnstile-violation-sapling-shielded-pool");
}

TEST(test_block, TestTurnstileIronwoodViolationRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    ASSERT_GT( chain.GetIndex()->nHeight, 0 );

    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight, consensusParams); // correctly paid, doesn't trip bad-cb-amount
    txNew.nExpiryHeight = 0;

    CBlock block;
    block.vtx.push_back(CTransaction(txNew));

    CValidationState state;
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    newIndex.nHeight = newHeight;
    newIndex.nChainSaplingValue = 0; // must not itself trip the Sapling check
    newIndex.nChainIronwoodValue = -1; // simulates the cumulative Ironwood pool having gone negative
    EXPECT_FALSE( chain.ConnectBlock(block, state, &newIndex, true, false) );
    EXPECT_EQ(state.GetRejectReason(), "turnstile-violation-ironwood-shielded-pool");
}

// TestSpendInSameBlock and TestDoubleSpendInSameBlock removed: both required
// Alice to spend a same-block, unconfirmed transfer she did not send herself,
// which CWalletTx::IsTrusted() (wallet.cpp) correctly refuses to treat as
// spendable 0-conf. This project does not support 0-conf transactions.

bool CalcPoW(CBlock *pblock);

TEST(test_block, TestProcessBlock)
{
    TestChain chain;
    // See TestStopAt's comment: KMD-default chainName at height 1 produces an
    // unconstructable coinbase under the current Rust digest FFI.
    chainName = assetchain("TST");
    EXPECT_EQ(chain.GetIndex()->nHeight, 0);
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    auto alice = std::make_shared<TestWallet>("alice");
    auto bob = std::make_shared<TestWallet>("bob");
    auto charlie = std::make_shared<TestWallet>("charlie");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // gives notary everything
    EXPECT_EQ(chain.GetIndex()->nHeight, 1);
    chain.IncrementChainTime();
    // add a transaction to the mempool
    TransactionInProcess fundAlice = notary->CreateSpendTransaction(alice, 100000);
    EXPECT_TRUE( chain.acceptTx(fundAlice.transaction).IsValid() );
    // construct the block
    CBlock block;
    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    CValidationState state;
    // no transactions
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-blk-length");
    EXPECT_EQ(chain.GetIndex()->nHeight, 1);
    // add first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // no PoW, no merkle root should fail on merkle error
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txnmrklroot");
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
    // finish constructing the block
    block.nBits = GetNextWorkRequired( chain.GetIndex(), &block, Params().GetConsensus());
    block.nTime = GetTime();
    block.hashPrevBlock = lastBlock->GetHash();
    block.hashMerkleRoot = block.BuildMerkleTree();
    // Add the PoW
    EXPECT_TRUE(CalcPoW(&block));
    state = CValidationState();
    EXPECT_TRUE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    if (!state.IsValid())
        FAIL() << state.GetRejectReason();
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
}

TEST(test_block, TestProcessBadBlock)
{
    TestChain chain;
    // See TestStopAt's comment: KMD-default chainName at height 1 produces an
    // unconstructable coinbase under the current Rust digest FFI.
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    auto alice = std::make_shared<TestWallet>("alice");
    auto bob = std::make_shared<TestWallet>("bob");
    auto charlie = std::make_shared<TestWallet>("charlie");
    std::shared_ptr<CBlock> lastBlock = chain.generateBlock(notary); // genesis block
    // add a transaction to the mempool
    TransactionInProcess fundAlice = notary->CreateSpendTransaction(alice, 100000);
    EXPECT_TRUE( chain.acceptTx(fundAlice.transaction).IsValid() );
    // construct the block
    CBlock block;
    int32_t newHeight = chain.GetIndex()->nHeight + 1;
    CValidationState state;
    // no transactions
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-blk-length");
    // add first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // Add no PoW, should fail on merkle error
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txnmrklroot");
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
}