// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "consensus/validation.h"
#include "main.h"
#include "keystore.h"
#include "key_io.h"
#include "zcash/Proof.hpp"

// Tests for CheckBlock() and ContextualCheckBlock(), the block-level structural
// and consensus-rule checks applied before a block is connected. Covers version
// checks, coinbase-height enforcement, and per-network-upgrade-era transaction
// acceptance/rejection across Sprout, Overwinter, Sapling, Blossom, and Ironwood
// rule branches.

static const std::string tSecretRegtest = "UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1";

class MockCValidationState : public CValidationState {
public:
    MOCK_METHOD5(DoS, bool(int level, bool ret,
             unsigned char chRejectCodeIn, std::string strRejectReasonIn,
             bool corruptionIn));
    MOCK_METHOD3(Invalid, bool(bool ret,
                 unsigned char _chRejectCode, std::string _strRejectReason));
    MOCK_METHOD1(Error, bool(std::string strRejectReasonIn));
    MOCK_CONST_METHOD0(IsValid, bool());
    MOCK_CONST_METHOD0(IsInvalid, bool());
    MOCK_CONST_METHOD0(IsError, bool());
    MOCK_CONST_METHOD1(IsInvalid, bool(int &nDoSOut));
    MOCK_CONST_METHOD0(CorruptionPossible, bool());
    MOCK_CONST_METHOD0(GetRejectCode, unsigned char());
    MOCK_CONST_METHOD0(GetRejectReason, std::string());
};

TEST(CheckBlock, VersionTooLow) {
    auto verifier = ProofVerifier::Strict();

    CBlock block;
    int32_t futureblock;
    block.nVersion = 1;

    MockCValidationState state;

    SelectParams(CBaseChainParams::MAIN);

    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "version-too-low", false)).Times(1);
    EXPECT_FALSE(CheckBlock(&futureblock, 1, NULL, block, state, verifier, false, false));
}


// Subclass of CTransaction which doesn't call UpdateHash when constructing
// from a CMutableTransaction.  This enables us to create a CTransaction
// with bad values which normally trigger an exception during construction.
class UNSAFE_CTransaction : public CTransaction {
    public:
        UNSAFE_CTransaction(const CMutableTransaction &tx) : CTransaction(tx, true) {}
};

// Test that a Sprout tx with negative version is still rejected
// by CheckBlock under Sprout consensus rules.
TEST(CheckBlock, BlockSproutRejectsBadVersion) {
    SelectParams(CBaseChainParams::MAIN);

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;
    mtx.vout.push_back(CTxOut(
        GetBlockSubsidy(1, Params().GetConsensus()),scriptPubKey));
    mtx.fOverwintered = false;
    mtx.nVersion = -1;
    mtx.nVersionGroupId = 0;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx {mtx};
    CBlock block;
    int32_t futureblock;
    block.vtx.push_back(tx);

    MockCValidationState state;
    CBlockIndex indexPrev {Params().GenesisBlock()};

    auto verifier = ProofVerifier::Strict();

    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    EXPECT_FALSE(CheckBlock(&futureblock, 1, &indexPrev, block, state, verifier, false, false));
}


// Phase 3 consensus-rule audit: ContextualCheckBlockHeader()'s "bad-diffbits",
// "time-too-old", and "time-too-new" checks had no test coverage anywhere -
// these gate every block header's proof-of-work target and timestamp, and
// are cheap to exercise directly (no transactions/proofs needed).
TEST(CheckBlock, ContextualCheckBlockHeaderDiffbitsAndTimestamps) {
    // A stale mock time left over from an earlier test would make GetTime()
    // return a frozen, possibly tiny value, which the "time-too-new" case
    // below relies on being real wall-clock time to land after the "time-too-old"
    // floor - see the same lesson documented in gtestutils.cpp's TestChain setup.
    SetMockTime(0);

    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CBlockIndex indexPrev{ Params().GenesisBlock() }; // nHeight stays 0, so the next header is height 1

    // This synthetic indexPrev/header pair isn't part of a real chainActive,
    // so MAIN's real checkpoint table would reject height 1 outright (below
    // its first checkpoint) via a path unrelated to what's under test here.
    fCheckpointsEnabled = false;

    {
        // Correct diffbits, timestamp just past the median-time-past floor: passes.
        // nVersion must be >= 4 (main.cpp's separate "bad-version" check) - unrelated
        // to diffbits/timestamps, but CBlockHeader's default nVersion is 0.
        CBlockHeader header;
        header.nVersion = 4;
        header.nTime = (uint32_t)indexPrev.GetMedianTimePast() + 1;
        header.nBits = GetNextWorkRequired(&indexPrev, &header, consensusParams);

        CValidationState state;
        EXPECT_TRUE(ContextualCheckBlockHeader(header, state, &indexPrev));
    }

    {
        // Same valid timestamp, wrong nBits.
        CBlockHeader header;
        header.nVersion = 4;
        header.nTime = (uint32_t)indexPrev.GetMedianTimePast() + 1;
        header.nBits = GetNextWorkRequired(&indexPrev, &header, consensusParams) + 1;

        CValidationState state;
        EXPECT_FALSE(ContextualCheckBlockHeader(header, state, &indexPrev));
        EXPECT_EQ(state.GetRejectReason(), "bad-diffbits");
    }

    {
        // Correct nBits for a timestamp at (not past) the median-time-past floor.
        CBlockHeader header;
        header.nVersion = 4;
        header.nTime = (uint32_t)indexPrev.GetMedianTimePast();
        header.nBits = GetNextWorkRequired(&indexPrev, &header, consensusParams);

        CValidationState state;
        EXPECT_FALSE(ContextualCheckBlockHeader(header, state, &indexPrev));
        EXPECT_EQ(state.GetRejectReason(), "time-too-old");
    }

    {
        // Correct nBits, timestamp far beyond the allowed future-drift window.
        CBlockHeader header;
        header.nVersion = 4;
        header.nTime = (uint32_t)(GetTime() + consensusParams.nMaxFutureBlockTime + 1000);
        header.nBits = GetNextWorkRequired(&indexPrev, &header, consensusParams);

        CValidationState state;
        EXPECT_FALSE(ContextualCheckBlockHeader(header, state, &indexPrev));
        EXPECT_EQ(state.GetRejectReason(), "time-too-new");
    }

    fCheckpointsEnabled = true;
}

class ContextualCheckBlockTest : public ::testing::Test {
protected:
    void SetUp() override {
        SelectParams(CBaseChainParams::MAIN);
    }

    void TearDown() override {
        // Revert to test default. No-op on mainnet params.
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }

    // Returns a valid but empty mutable transaction at block height 1.
    CMutableTransaction GetFirstBlockCoinbaseTx() {

        CBasicKeyStore keystore;
        CKey tsk = DecodeSecret(tSecretRegtest);
        EXPECT_TRUE(tsk.IsValid());
        keystore.AddKey(tsk);
        auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

        CMutableTransaction mtx;

        // No inputs.
        mtx.vin.resize(1);
        mtx.vin[0].prevout.SetNull();

        // Set height to 1.
        mtx.vin[0].scriptSig = CScript() << 1 << OP_0;

        // Give it a single zero-valued, always-valid output.
        mtx.vout.resize(1);
        mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
        mtx.vout[0].nValue = 0;

        // Give it a a vout
        //
        // Not using GetBlockSubsidy(1, ...) here: with no chain symbol configured
        // (as in this gtest binary), chainName.isKMD() is true, so GetBlockSubsidy
        // takes Komodo mainnet's literal height-1 "ICO allocation" branch (100,000,000
        // coins) - a value that exceeds the vendored Rust crate's hardcoded
        // MAX_MONEY (21,000,000 * COIN) and makes CTransaction construction throw.
        // A real Pirate daemon always configures a non-empty chain symbol, so this
        // branch never fires in production; the test only needs some valid reward.
        auto rewardScript = scriptPubKey;
        mtx.vout.push_back(CTxOut(10 * COIN, rewardScript));

        return mtx;
    }

    // Expects a height-1 block containing a given transaction to pass
    // ContextualCheckBlock. This is used in accepting (Sprout-Sprout,
    // Overwinter-Overwinter, ...) tests. You should not call it without
    // calling a SCOPED_TRACE macro first to usefully label any failures.
    void ExpectValidBlockFromTx(const CTransaction& tx) {
        // Create a block and add the transaction to it.
        CBlock block;
        block.vtx.push_back(tx);

        // Set Testing mode to true, so that we can use the
        // ContextualCheckBlock function without IsInitialBlockDownload()
        // being true.
        fTesting = true;

        // Set the previous block index to the genesis block.
        CBlockIndex indexPrev {Params().GenesisBlock()};

        // We now expect this to be a valid block.
        MockCValidationState state;
        EXPECT_TRUE(ContextualCheckBlock(0, block, state, &indexPrev));
    }

    // Expects a height-1 block containing a given transaction to fail
    // ContextualCheckBlock. This is used in rejecting (Sprout-Overwinter,
    // Overwinter-Sprout, ...) tests. You should not call it without
    // calling a SCOPED_TRACE macro first to usefully label any failures.
    void ExpectInvalidBlockFromTx(const CTransaction& tx, int level, std::string reason) {
        // Create a block and add the transaction to it.
        CBlock block;
        block.vtx.push_back(tx);

        // Set Testing mode to true, so that we can use the
        // ContextualCheckBlock function without IsInitialBlockDownload()
        // being true.
        fTesting = true;

        // Set the previous block index to the genesis block.
        CBlockIndex indexPrev {Params().GenesisBlock()};

        // We now expect this to be an invalid block, for the given reason.
        MockCValidationState state;
        EXPECT_CALL(state, DoS(level, false, REJECT_INVALID, reason, false)).Times(1);
        EXPECT_FALSE(ContextualCheckBlock(0, block, state, &indexPrev));
    }

};


TEST_F(ContextualCheckBlockTest, BadCoinbaseHeight) {

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Put a transaction in a block with no height in scriptSig
    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();
    mtx.vin[0].scriptSig = CScript() << OP_0;
    mtx.vout.pop_back(); // remove the FR output

    CBlock block;
    block.vtx.push_back(mtx);

    // Treating block as genesis should pass
    MockCValidationState state;
    EXPECT_TRUE(ContextualCheckBlock(0, block, state, NULL));


    // Give the transaction a vout. See the comment in GetFirstBlockCoinbaseTx()
    // for why GetBlockSubsidy(1, ...) isn't used here.
    mtx.vout.push_back(CTxOut(10 * COIN, scriptPubKey));

    // Treating block as non-genesis should fail
    CTransaction tx2 {mtx};
    block.vtx[0] = tx2;
    CBlock prev;
    CBlockIndex indexPrev {prev};
    indexPrev.nHeight = 0;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-height", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(0, block, state, &indexPrev));

    // Setting to an incorrect height should fail
    mtx.vin[0].scriptSig = CScript() << 2 << OP_0;
    CTransaction tx3 {mtx};
    block.vtx[0] = tx3;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-height", false)).Times(1);
    EXPECT_FALSE(ContextualCheckBlock(0, block, state, &indexPrev));

    // After correcting the scriptSig, should pass
    mtx.vin[0].scriptSig = CScript() << 1 << OP_0;
    CTransaction tx4 {mtx};
    block.vtx[0] = tx4;
    EXPECT_TRUE(ContextualCheckBlock(0, block, state, &indexPrev));
}

// TEST PLAN: first, check that each ruleset accepts its own transaction type.
// Currently (May 2018) this means we'll test Sprout-Sprout,
// Overwinter-Overwinter, and Sapling-Sapling.

// Test block evaluated under Sprout rules will accept Sprout transactions.
// This test assumes that mainnet Overwinter activation is at least height 2.
TEST_F(ContextualCheckBlockTest, BlockSproutRulesAcceptSproutTx) {
    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Make it a Sprout transaction w/o JoinSplits
    mtx.fOverwintered = false;
    mtx.nVersion = 1;

    SCOPED_TRACE("BlockSproutRulesAcceptSproutTx");
    ExpectValidBlockFromTx(CTransaction(mtx));
}


// Test block evaluated under Overwinter rules will accept Overwinter transactions.
TEST_F(ContextualCheckBlockTest, BlockOverwinterRulesAcceptOverwinterTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Make it an Overwinter transaction
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    SCOPED_TRACE("BlockOverwinterRulesAcceptOverwinterTx");
    ExpectValidBlockFromTx(CTransaction(mtx));
}


// Test that a block evaluated under Sapling rules can contain Sapling transactions.
TEST_F(ContextualCheckBlockTest, BlockSaplingRulesAcceptSaplingTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Make it a Sapling transaction
    mtx.fOverwintered = true;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;

    SCOPED_TRACE("BlockSaplingRulesAcceptSaplingTx");
    ExpectValidBlockFromTx(CTransaction(mtx));
}


// Test that a block evaluated under Blossom rules can contain Blossom transactions.
TEST_F(ContextualCheckBlockTest, BlockBlossomRulesAcceptBlossomTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Make it a Blossom transaction (using Sapling version/group id).
    mtx.fOverwintered = true;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;

    SCOPED_TRACE("BlockIronwoodRulesAcceptIronwoodTx");
    ExpectValidBlockFromTx(CTransaction(mtx));
}


// TEST PLAN: next, check that each ruleset will not accept other transaction
// types. Currently (February 2020) this means with each of four *branches* active
// (Sprout, Overwinter, Sapling, Blossom), we'll test that transactions for tx
// *versions* not valid on that branch are rejected.
//
// Note that Sapling and Blossom transactions use the same tx version and version
// group id, but different consensus branch ids. These tests use transactions with
// no inputs and only a transparent output, therefore they are not signed and do not
// depend on the consensus branch id. Testing that Sapling rejects transactions that
// are signed for Blossom, and vice versa, is outside the scope of these tests.
//
// TODO: Change the testing approach to not require O(branches * versions) code.


// Test that a block evaluated under Sprout rules cannot contain non-Sprout
// transactions which require Overwinter to be active.  This test assumes that
// mainnet Overwinter activation is at least height 2.
TEST_F(ContextualCheckBlockTest, BlockSproutRulesRejectOtherTx) {
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Make it an Overwinter transaction
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    {
        SCOPED_TRACE("BlockSproutRulesRejectOverwinterTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "tx-overwinter-not-active");
    }

    // Make it a Sapling transaction
    mtx.fOverwintered = true;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;

    {
        SCOPED_TRACE("BlockSproutRulesRejectSaplingTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "tx-overwinter-not-active");
    }

    TearDown();
};


// Test block evaluated under Overwinter rules cannot contain non-Overwinter
// transactions.
TEST_F(ContextualCheckBlockTest, BlockOverwinterRulesRejectOtherTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Set the version to Sprout+JoinSplit (but nJoinSplit will be 0).
    mtx.nVersion = 2;

    {
        SCOPED_TRACE("BlockOverwinterRulesRejectSproutTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "tx-overwintered-flag-not-set");
    }

    // Make it a Sapling transaction
    mtx.fOverwintered = true;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;

    {
        SCOPED_TRACE("BlockOverwinterRulesRejectSaplingTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "bad-overwinter-tx-version-group-id");
    }
}


// Test block evaluated under Sapling rules cannot contain non-Sapling transactions.
TEST_F(ContextualCheckBlockTest, BlockSaplingRulesRejectOtherTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Set the version to Sprout+JoinSplit (but nJoinSplit will be 0).
    mtx.nVersion = 2;

    {
        SCOPED_TRACE("BlockSaplingRulesRejectSproutTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "tx-overwintered-flag-not-set");
    }

    // Make it an Overwinter transaction
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    {
        SCOPED_TRACE("BlockSaplingRulesRejectOverwinterTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "bad-sapling-tx-version-group-id");
    }
}


// Test block evaluated under Ironwood rules cannot contain non-Ironwood transactions.
TEST_F(ContextualCheckBlockTest, BlockIronwoodRulesRejectOtherTx) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 1);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, 1);

    CMutableTransaction mtx = GetFirstBlockCoinbaseTx();

    // Set the version to Sprout+JoinSplit (but nJoinSplit will be 0).
    mtx.nVersion = 2;

    {
        SCOPED_TRACE("BlockIronwoodRulesRejectSproutTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "tx-overwintered-flag-not-set");
    }

    // Make it an Overwinter transaction
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;

    {
        SCOPED_TRACE("BlockIronwoodRulesRejectOverwinterTx");
        ExpectInvalidBlockFromTx(CTransaction(mtx), 100, "bad-ironwood-tx-version-group-id");
    }
}
