// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include "gtest/gtestutils.h"
#include "komodo_extern_globals.h"
#include "consensus/validation.h"
#include "consensus/upgrades.h"
#include "coincontrol.h"
#include "miner.h"
#include "key_io.h"
#include "pubkey.h"
#include "transaction_builder.h"
#include "wallet/walletmanager.h"
#include "zcash/Address.hpp"

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

static const std::string tSecretRegtestBlockConnect = "UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1";

// ContextualCheckShieldedInputs' duplicate-nullifier rejection (Consensus::CheckTxShieldedInputs,
// "bad-txns-sapling/ironwood-duplicate-nullifier") is exercised per-transaction by
// AcceptToMemoryPool, but ConnectBlock() runs it too, against a view that gets updated
// incrementally as each transaction in the block is processed (main.cpp's per-tx loop
// calls UpdateCoins - which marks nullifiers spent - right after checking that tx's
// shielded inputs, before moving to the next transaction). That's what actually catches
// a same-block double-spend: two different, individually-valid transactions in one block
// that both reveal the same nullifier. Had no test coverage anywhere in the suite - the
// only removed near-equivalent (see comment below) tested something else entirely (Alice
// spending an unconfirmed transfer to herself, which CWalletTx::IsTrusted() already
// refuses as untrusted 0-conf, not a same-block duplicate-nullifier scenario).
//
// Note: the spent note must come from an *earlier, already-connected* block - the
// Sapling/Ironwood frontier anchor only advances at block boundaries (pushed once after
// a block's whole transaction loop, not per-transaction), so a note shielded earlier in
// the *same* block has no recognized anchor yet to spend against. This is also why
// TestSpendInSameBlock/TestDoubleSpendInSameBlock (a related, unconfirmed-transfer
// scenario) were removed rather than adapted - this project has no 0-conf concept at all.
TEST(test_block, TestSaplingSameBlockDuplicateNullifierRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    // With no -ac_reward/-ac_end set, GetBlockSubsidy() falls back to a flat 10000
    // ARRRtoshis for every height (komodo_ac_block_subsidy()'s "older chains with no
    // explicit rewards" case) - too little for a coinbase that also needs to cover a
    // 40000 shield + this builder's 10000 default fee. ConnectBlock() rejects any
    // coinbase paying more than GetBlockSubsidy() ("bad-cb-amount"), so bump the
    // per-block reward up for the lifetime of this test.
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Block A: a plain coinbase paying our own keystore, so there's a transparent
    // output to shield from in block B.
    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    // Exactly shield-amount + default builder fee, so the shielding tx below has
    // zero leftover change and doesn't need a change address configured at all
    // (TransactionBuilder::Build() only requires one when change > 0). Must not
    // exceed the (now-bumped) block reward above, or ConnectBlock() rejects it.
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    // Sapling is active from height 1 in this test, so even a plain coinbase-only
    // block must carry the correct hashBlockCommitments (ConnectBlock, "bad-sapling-
    // root-in-block") - here that's just the still-untouched empty Sapling frontier.
    blockA.hashBlockCommitments = SaplingMerkleFrontier::empty_root();
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    // ConnectBlock(blockB, ...) below reads pindex->pprev->GetBlockHash() (main.cpp's
    // hashPrevBlock check) - a manually-built CBlockIndex has a null phashBlock by
    // default, which trips GetBlockHash()'s assert unless pointed at a real hash here.
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    // ContextualCheckInputs() -> GetSpendHeight() (main.cpp) looks up the current best
    // block in the *global* mapBlockIndex, not just the view - needed once blockB spends
    // coinbaseA's transparent output. Erased again on scope exit since indexA is a local
    // that won't outlive this test.
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    // ConnectBlock() only advances view.SetBestBlock() past its "if (fJustCheck) return
    // true" early-out (main.cpp) - with fJustCheck=true throughout this test, chaining a
    // second ConnectBlock() on top needs that marker moved forward by hand, or its
    // hashPrevBlock-vs-GetBestBlock() check rejects the next block outright.
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    // Block B: shield that coinbase output into Sapling.
    SaplingWallet saplingWallet;
    SaplingMerkleFrontier saplingFrontier;
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);

    auto sk_from = libzcash::SaplingSpendingKey::random();
    libzcash::SaplingFullViewingKey fvk_from;
    sk_from.expanded_spending_key().DeriveFVK(&fvk_from);
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    libzcash::SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    libzcash::SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.InitializeSapling(uint256());
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    ASSERT_TRUE(shieldBuilder.AddSaplingOutputRaw(pk, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    // Mirrors ConnectBlock's own sapling_frontier_tree.AppendBundle(...) - this is the
    // exact value it will independently compute and check hashBlockCommitments against.
    saplingFrontier.AppendBundle(shieldTx.GetSaplingBundle());
    blockB.hashBlockCommitments = saplingFrontier.root();
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    // Derive the note/anchor locally: block B is the first (and only, so far) block to
    // touch the Sapling frontier, so an independently-built frontier fed the same
    // outputs in the same order lands on the identical anchor ConnectBlock just pushed.
    saplingWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    auto vOutputs = shieldTx.GetSaplingOutputs();
    for (int j = 0; j < vOutputs.size(); j++) {
        saplingWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, j, &vOutputs[j], true);
    }
    int realOutputIndex = -1;
    std::optional<libzcash::SaplingNotePlaintext> maybe_pt;
    for (int j = 0; j < vOutputs.size(); j++) {
        maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], ivk);
        if (maybe_pt) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);
    auto cmu = uint256::FromRawBytes(vOutputs[realOutputIndex].cmu());
    auto note = maybe_pt.value().note(ivk).value();
    libzcash::MerklePath saplingMerklePath;
    ASSERT_TRUE(saplingWallet.GetMerklePathOfNote(shieldTx.GetHash(), realOutputIndex, saplingMerklePath));
    uint256 anchor;
    ASSERT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Two independent transactions, both spending the identical note.
    int32_t heightC = heightB + 1;
    auto spendBuilderA = TransactionBuilder(consensusParams, heightC);
    spendBuilderA.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilderA.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilderA.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilderA.AddSaplingOutputRaw(pk, 20000, {}));
    ASSERT_TRUE(spendBuilderA.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTxA = spendBuilderA.Build();
    ASSERT_TRUE(maybeSpendTxA.IsTx());
    auto spendTxA = maybeSpendTxA.GetTxOrThrow();

    auto spendBuilderB = TransactionBuilder(consensusParams, heightC);
    spendBuilderB.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilderB.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilderB.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilderB.AddSaplingOutputRaw(pk, 15000, {})); // different amount -> different tx
    ASSERT_TRUE(spendBuilderB.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTxB = spendBuilderB.Build();
    ASSERT_TRUE(maybeSpendTxB.IsTx());
    auto spendTxB = maybeSpendTxB.GetTxOrThrow();
    ASSERT_NE(spendTxA.GetHash(), spendTxB.GetHash());

    CMutableTransaction coinbaseCNew = CreateNewContextualCMutableTransaction(consensusParams, heightC);
    coinbaseCNew.vin.resize(1);
    coinbaseCNew.vin[0].prevout.SetNull();
    coinbaseCNew.vin[0].scriptSig = (CScript() << heightC << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseCNew.vout.resize(1);
    coinbaseCNew.vout[0].nValue = GetBlockSubsidy(heightC, consensusParams);
    coinbaseCNew.nExpiryHeight = 0;
    CBlock blockC;
    blockC.vtx.push_back(CTransaction(coinbaseCNew));
    blockC.vtx.push_back(spendTxA);
    blockC.vtx.push_back(spendTxB);
    CValidationState stateC;
    CBlockIndex indexC;
    indexC.pprev = &indexB;
    indexC.nHeight = heightC;
    EXPECT_FALSE(chain.ConnectBlock(blockC, stateC, &indexC, true, false));
    EXPECT_EQ(stateC.GetRejectReason(), "bad-txns-sapling-duplicate-nullifier");
}

TEST(test_block, TestIronwoodSameBlockDuplicateNullifierRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    // See the Sapling version of this test for why: with no -ac_reward/-ac_end set,
    // the default flat 10000-ARRRtoshi subsidy is too little for a coinbase that also
    // needs to cover a 40000 shield + this builder's 10000 default fee.
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    // Exactly shield-amount + default builder fee, so the shielding tx below has
    // zero leftover change and doesn't need a change address configured at all
    // (TransactionBuilder::Build() only requires one when change > 0). A coinbase
    // is free to pay itself less than the max subsidy - only overpayment (bad-cb-
    // amount) is rejected.
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    // Ironwood is active from height 1 in this test, so even a plain coinbase-only
    // block must carry the ZIP 244 block-commitments digest (ConnectBlock, "bad-block-
    // commitments-hash") rather than the simpler pre-Ironwood Sapling-root value. This
    // mirrors ConnectBlock's own computation (main.cpp, "Derive the various block
    // commitments") exactly, reading from the same CCoinsViewCache it will use
    // internally, before anything has mutated it for this block.
    {
        uint32_t branchIdA = CurrentEpochBranchId(heightA - 1, consensusParams);
        uint256 hashChainHistoryRootA = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdA);
        uint256 hashAuthDataRootA = blockA.BuildAuthDataMerkleTree();
        blockA.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootA, hashAuthDataRootA);
    }
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    // ConnectBlock(blockB, ...) below reads pindex->pprev->GetBlockHash() (main.cpp's
    // hashPrevBlock check) - a manually-built CBlockIndex has a null phashBlock by
    // default, which trips GetBlockHash()'s assert unless pointed at a real hash here.
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    // ContextualCheckInputs() -> GetSpendHeight() (main.cpp) looks up the current best
    // block in the *global* mapBlockIndex, not just the view - needed once blockB spends
    // coinbaseA's transparent output. Erased again on scope exit since indexA is a local
    // that won't outlive this test.
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    // ConnectBlock() only advances view.SetBestBlock() past its "if (fJustCheck) return
    // true" early-out (main.cpp) - with fJustCheck=true throughout this test, chaining a
    // second ConnectBlock() on top needs that marker moved forward by hand, or its
    // hashPrevBlock-vs-GetBestBlock() check rejects the next block outright.
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    IronwoodWallet ironwoodWallet;
    IronwoodMerkleFrontier ironwoodFrontier;
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto addr = fromWallet.GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate extsk;
    ASSERT_TRUE(fromWallet.GetIronwoodExtendedSpendingKey(addr, extsk));
    libzcash::IronwoodFullViewingKey fvk;
    ASSERT_TRUE(extsk.sk.DeriveFVK(&fvk));
    libzcash::IronwoodOutgoingViewingKey ovk;
    ASSERT_TRUE(fvk.DeriveOVK(&ovk));

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    shieldBuilder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(shieldBuilder.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    // ZIP 244's txid digest (used once Ironwood/NU5-style is active, as it is for every
    // block in this test) does not commit to scriptSig at all - unlike the legacy Sapling
    // txid hash. Without some other distinguishing field, this coinbase and coinbaseC
    // below (same value, same null prevout, same default locktime) hash identically and
    // trip ConnectBlock()'s BIP30 "tried to overwrite transaction" check.
    coinbaseBNew.nLockTime = heightB;
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    {
        uint32_t branchIdB = CurrentEpochBranchId(heightB - 1, consensusParams);
        uint256 hashChainHistoryRootB = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdB);
        uint256 hashAuthDataRootB = blockB.BuildAuthDataMerkleTree();
        blockB.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootB, hashAuthDataRootB);
    }
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    const auto& bundleDetails = shieldTx.GetIronwoodBundle().GetDetails();
    auto actions = bundleDetails.actions();
    uint256_t ovkBytes;
    std::copy(ovk.ovk.begin(), ovk.ovk.end(), ovkBytes.begin());
    int realActionIndex = -1;
    uint64_t noteValue = 0;
    uint256_t noteRho{}, noteRseed{};
    for (size_t i = 0; i < actions.size(); i++) {
        uint256_t ock;
        if (!ironwood::derive_ironwood_ock(actions[i], ovkBytes, ock)) {
            continue;
        }
        uint64_t testValue;
        std::array<uint8_t, 43> testAddress;
        std::array<uint8_t, 512> testMemo;
        uint256_t testRho, testRseed;
        if (ironwood::try_ironwood_decrypt_action_ock(
                actions[i], ock, testValue, testAddress, testMemo, testRho, testRseed)) {
            realActionIndex = (int)i;
            noteValue = testValue;
            noteRho = testRho;
            noteRseed = testRseed;
            break;
        }
    }
    ASSERT_NE(realActionIndex, -1);
    uint256 rho = uint256::FromRawBytes(noteRho);
    uint256 rseed = uint256::FromRawBytes(noteRseed);
    uint256 cmx = uint256::FromRawBytes(actions[realActionIndex].cmx());

    ironwoodWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, (int)j, &actions[j], true);
    }
    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(shieldTx.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilderA = TransactionBuilder(consensusParams, heightC);
    spendBuilderA.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilderA.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilderA.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilderA.AddIronwoodOutputRaw(addr, 20000, {}));
    ASSERT_TRUE(spendBuilderA.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTxA = spendBuilderA.Build();
    ASSERT_TRUE(maybeSpendTxA.IsTx());
    auto spendTxA = maybeSpendTxA.GetTxOrThrow();

    auto spendBuilderB = TransactionBuilder(consensusParams, heightC);
    spendBuilderB.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilderB.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilderB.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilderB.AddIronwoodOutputRaw(addr, 15000, {})); // different amount -> different tx
    ASSERT_TRUE(spendBuilderB.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTxB = spendBuilderB.Build();
    ASSERT_TRUE(maybeSpendTxB.IsTx());
    auto spendTxB = maybeSpendTxB.GetTxOrThrow();
    ASSERT_NE(spendTxA.GetHash(), spendTxB.GetHash());

    CMutableTransaction coinbaseCNew = CreateNewContextualCMutableTransaction(consensusParams, heightC);
    coinbaseCNew.vin.resize(1);
    coinbaseCNew.vin[0].prevout.SetNull();
    coinbaseCNew.vin[0].scriptSig = (CScript() << heightC << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseCNew.vout.resize(1);
    coinbaseCNew.vout[0].nValue = GetBlockSubsidy(heightC, consensusParams);
    coinbaseCNew.nExpiryHeight = 0;
    coinbaseCNew.nLockTime = heightC; // see coinbaseB's nLockTime comment above
    CBlock blockC;
    blockC.vtx.push_back(CTransaction(coinbaseCNew));
    blockC.vtx.push_back(spendTxA);
    blockC.vtx.push_back(spendTxB);
    CValidationState stateC;
    CBlockIndex indexC;
    indexC.pprev = &indexB;
    indexC.nHeight = heightC;
    EXPECT_FALSE(chain.ConnectBlock(blockC, stateC, &indexC, true, false));
    EXPECT_EQ(stateC.GetRejectReason(), "bad-txns-ironwood-duplicate-nullifier");
}

// AcceptToMemoryPool() (main.cpp) has its own, earlier duplicate-nullifier guard, ahead
// of and separate from Consensus::CheckTxShieldedInputs (which ConnectBlock and the
// mempool both also run): before doing any of the expensive proof/anchor checks, it
// checks the new tx's nullifiers against pool.nullifierExists() - the nullifiers of
// every other transaction *already sitting in the mempool*. That's a different code
// path from the same-block ConnectBlock tests above (which reject via the shared
// Consensus::CheckTxShieldedInputs check, "bad-txns-.../-duplicate-nullifier") and from
// CheckTransactionWithoutProofVerification's within-tx check ("bad-.../-nullifiers-
// duplicate") - so it gets its own coverage, with its own distinct reject reason.
TEST(test_mempool, TestSaplingMempoolDuplicateNullifierRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Block A: a plain coinbase paying our own keystore, so there's a transparent
    // output to shield from in block B.
    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    blockA.hashBlockCommitments = SaplingMerkleFrontier::empty_root();
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    // Block B: shield that coinbase output into Sapling.
    SaplingWallet saplingWallet;
    SaplingMerkleFrontier saplingFrontier;
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);

    auto sk_from = libzcash::SaplingSpendingKey::random();
    libzcash::SaplingFullViewingKey fvk_from;
    sk_from.expanded_spending_key().DeriveFVK(&fvk_from);
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    libzcash::SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    libzcash::SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.InitializeSapling(uint256());
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    ASSERT_TRUE(shieldBuilder.AddSaplingOutputRaw(pk, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    saplingFrontier.AppendBundle(shieldTx.GetSaplingBundle());
    blockB.hashBlockCommitments = saplingFrontier.root();
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    // Derive the note/anchor locally, exactly as the same-block ConnectBlock test does.
    saplingWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    auto vOutputs = shieldTx.GetSaplingOutputs();
    for (int j = 0; j < vOutputs.size(); j++) {
        saplingWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, j, &vOutputs[j], true);
    }
    int realOutputIndex = -1;
    std::optional<libzcash::SaplingNotePlaintext> maybe_pt;
    for (int j = 0; j < vOutputs.size(); j++) {
        maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], ivk);
        if (maybe_pt) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);
    auto cmu = uint256::FromRawBytes(vOutputs[realOutputIndex].cmu());
    auto note = maybe_pt.value().note(ivk).value();
    libzcash::MerklePath saplingMerklePath;
    ASSERT_TRUE(saplingWallet.GetMerklePathOfNote(shieldTx.GetHash(), realOutputIndex, saplingMerklePath));
    uint256 anchor;
    ASSERT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Two independent transactions, both spending the identical note.
    int32_t heightC = heightB + 1;
    auto spendBuilderA = TransactionBuilder(consensusParams, heightC);
    spendBuilderA.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilderA.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilderA.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilderA.AddSaplingOutputRaw(pk, 20000, {}));
    ASSERT_TRUE(spendBuilderA.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTxA = spendBuilderA.Build();
    ASSERT_TRUE(maybeSpendTxA.IsTx());
    auto spendTxA = maybeSpendTxA.GetTxOrThrow();

    auto spendBuilderB = TransactionBuilder(consensusParams, heightC);
    spendBuilderB.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilderB.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilderB.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilderB.AddSaplingOutputRaw(pk, 15000, {})); // different amount -> different tx
    ASSERT_TRUE(spendBuilderB.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTxB = spendBuilderB.Build();
    ASSERT_TRUE(maybeSpendTxB.IsTx());
    auto spendTxB = maybeSpendTxB.GetTxOrThrow();
    ASSERT_NE(spendTxA.GetHash(), spendTxB.GetHash());

    mempool.clear();
    CValidationState acceptStateA = chain.acceptTx(spendTxA);
    ASSERT_TRUE(acceptStateA.IsValid()) << acceptStateA.GetRejectReason();

    CValidationState acceptStateB = chain.acceptTx(spendTxB);
    EXPECT_FALSE(acceptStateB.IsValid());
    EXPECT_EQ(acceptStateB.GetRejectReason(), "bad-txns-duplicate-nullifier-requirements-not-met");
    mempool.clear();
}

TEST(test_mempool, TestIronwoodMempoolDuplicateNullifierRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    {
        uint32_t branchIdA = CurrentEpochBranchId(heightA - 1, consensusParams);
        uint256 hashChainHistoryRootA = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdA);
        uint256 hashAuthDataRootA = blockA.BuildAuthDataMerkleTree();
        blockA.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootA, hashAuthDataRootA);
    }
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    IronwoodWallet ironwoodWallet;
    IronwoodMerkleFrontier ironwoodFrontier;
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto addr = fromWallet.GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate extsk;
    ASSERT_TRUE(fromWallet.GetIronwoodExtendedSpendingKey(addr, extsk));
    libzcash::IronwoodFullViewingKey fvk;
    ASSERT_TRUE(extsk.sk.DeriveFVK(&fvk));
    libzcash::IronwoodOutgoingViewingKey ovk;
    ASSERT_TRUE(fvk.DeriveOVK(&ovk));

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    shieldBuilder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(shieldBuilder.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    coinbaseBNew.nLockTime = heightB; // see the same-block Ironwood test's nLockTime comment
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    {
        uint32_t branchIdB = CurrentEpochBranchId(heightB - 1, consensusParams);
        uint256 hashChainHistoryRootB = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdB);
        uint256 hashAuthDataRootB = blockB.BuildAuthDataMerkleTree();
        blockB.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootB, hashAuthDataRootB);
    }
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    const auto& bundleDetails = shieldTx.GetIronwoodBundle().GetDetails();
    auto actions = bundleDetails.actions();
    uint256_t ovkBytes;
    std::copy(ovk.ovk.begin(), ovk.ovk.end(), ovkBytes.begin());
    int realActionIndex = -1;
    uint64_t noteValue = 0;
    uint256_t noteRho{}, noteRseed{};
    for (size_t i = 0; i < actions.size(); i++) {
        uint256_t ock;
        if (!ironwood::derive_ironwood_ock(actions[i], ovkBytes, ock)) {
            continue;
        }
        uint64_t testValue;
        std::array<uint8_t, 43> testAddress;
        std::array<uint8_t, 512> testMemo;
        uint256_t testRho, testRseed;
        if (ironwood::try_ironwood_decrypt_action_ock(
                actions[i], ock, testValue, testAddress, testMemo, testRho, testRseed)) {
            realActionIndex = (int)i;
            noteValue = testValue;
            noteRho = testRho;
            noteRseed = testRseed;
            break;
        }
    }
    ASSERT_NE(realActionIndex, -1);
    uint256 rho = uint256::FromRawBytes(noteRho);
    uint256 rseed = uint256::FromRawBytes(noteRseed);
    uint256 cmx = uint256::FromRawBytes(actions[realActionIndex].cmx());

    ironwoodWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, (int)j, &actions[j], true);
    }
    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(shieldTx.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilderA = TransactionBuilder(consensusParams, heightC);
    spendBuilderA.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilderA.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilderA.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilderA.AddIronwoodOutputRaw(addr, 20000, {}));
    ASSERT_TRUE(spendBuilderA.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTxA = spendBuilderA.Build();
    ASSERT_TRUE(maybeSpendTxA.IsTx());
    auto spendTxA = maybeSpendTxA.GetTxOrThrow();

    auto spendBuilderB = TransactionBuilder(consensusParams, heightC);
    spendBuilderB.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilderB.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilderB.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilderB.AddIronwoodOutputRaw(addr, 15000, {})); // different amount -> different tx
    ASSERT_TRUE(spendBuilderB.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTxB = spendBuilderB.Build();
    ASSERT_TRUE(maybeSpendTxB.IsTx());
    auto spendTxB = maybeSpendTxB.GetTxOrThrow();
    ASSERT_NE(spendTxA.GetHash(), spendTxB.GetHash());

    mempool.clear();
    CValidationState acceptStateA = chain.acceptTx(spendTxA);
    ASSERT_TRUE(acceptStateA.IsValid()) << acceptStateA.GetRejectReason();

    CValidationState acceptStateB = chain.acceptTx(spendTxB);
    EXPECT_FALSE(acceptStateB.IsValid());
    EXPECT_EQ(acceptStateB.GetRejectReason(), "bad-txns-duplicate-nullifier-requirements-not-met");
    mempool.clear();
}

// AcceptToMemoryPool()/ConnectBlock() run every shielded tx's zk-proofs and signatures
// through the real Groth16/Halo2 batch validators (sapling::BatchValidator,
// ironwood::BatchValidator - see main.cpp's ContextualCheckShieldedInputs callers), not a
// stub. Every other test in this suite only ever exercises the *valid*-proof path (a
// TransactionBuilder-built spend either gets accepted or is rejected for some unrelated
// reason - duplicate nullifier, bad anchor, etc.) - none of them prove the validator is
// actually doing cryptographic work rather than rubber-stamping. These two tests take a
// real, otherwise well-formed and correctly-signed transaction and corrupt only the
// zk-proof bytes (via test-only Rust helpers that leave cv/anchor/nullifier/rk/signatures
// untouched), then confirm rejection is attributed specifically to bundle-authorization
// failure.
TEST(test_mempool, TestSaplingInvalidSpendProofRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    blockA.hashBlockCommitments = SaplingMerkleFrontier::empty_root();
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    SaplingWallet saplingWallet;
    SaplingMerkleFrontier saplingFrontier;
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);

    auto sk_from = libzcash::SaplingSpendingKey::random();
    libzcash::SaplingFullViewingKey fvk_from;
    sk_from.expanded_spending_key().DeriveFVK(&fvk_from);
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    libzcash::SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    libzcash::SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.InitializeSapling(uint256());
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    ASSERT_TRUE(shieldBuilder.AddSaplingOutputRaw(pk, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    saplingFrontier.AppendBundle(shieldTx.GetSaplingBundle());
    blockB.hashBlockCommitments = saplingFrontier.root();
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    saplingWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    auto vOutputs = shieldTx.GetSaplingOutputs();
    for (int j = 0; j < vOutputs.size(); j++) {
        saplingWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, j, &vOutputs[j], true);
    }
    int realOutputIndex = -1;
    std::optional<libzcash::SaplingNotePlaintext> maybe_pt;
    for (int j = 0; j < vOutputs.size(); j++) {
        maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], ivk);
        if (maybe_pt) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);
    auto cmu = uint256::FromRawBytes(vOutputs[realOutputIndex].cmu());
    auto note = maybe_pt.value().note(ivk).value();
    libzcash::MerklePath saplingMerklePath;
    ASSERT_TRUE(saplingWallet.GetMerklePathOfNote(shieldTx.GetHash(), realOutputIndex, saplingMerklePath));
    uint256 anchor;
    ASSERT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilder = TransactionBuilder(consensusParams, heightC);
    spendBuilder.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilder.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilder.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilder.AddSaplingOutputRaw(pk, 20000, {}));
    ASSERT_TRUE(spendBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTx = spendBuilder.Build();
    ASSERT_TRUE(maybeSpendTx.IsTx());
    auto spendTx = maybeSpendTx.GetTxOrThrow();
    ASSERT_EQ(spendTx.GetSaplingSpendsCount(), 1);

    // Replace only the spend's zk-proof with the shielding tx's own (real, validly-encoded,
    // but wrong-statement) output proof - cv/anchor/nullifier/rk/spend_auth_sig, the
    // transparent parts, and every other byte of the transaction are untouched. Swapping in
    // another real proof (rather than random bytes) matters: random bytes usually fail to
    // even decode as curve points, which sapling::BatchValidator::check_bundle rejects
    // synchronously as "bad-txns-sapling-bundle-invalid" before ever reaching the deferred,
    // pairing-check batch verification this test exists to exercise.
    auto wrongProof = shieldTx.GetSaplingOutputs()[0].zkproof();
    CMutableTransaction corruptedMtx(spendTx);
    sapling::test_only_replace_spend_proof(corruptedMtx.saplingBundle.GetDetailsMut(), 0, wrongProof);
    CTransaction corruptedSpendTx(corruptedMtx);

    mempool.clear();
    CValidationState acceptState = chain.acceptTx(corruptedSpendTx);
    EXPECT_FALSE(acceptState.IsValid());
    EXPECT_EQ(acceptState.GetRejectReason(), "bad-sapling-bundle-authorization");
    mempool.clear();

    // Same corrupted transaction via ConnectBlock() instead of the mempool - a distinct
    // code path (ConnectBlock sets up and runs its own real sapling::BatchValidator,
    // separately from AcceptToMemoryPool's) that a directly-mined or block-relayed
    // transaction bypassing the mempool entirely would go through.
    CMutableTransaction coinbaseCNew = CreateNewContextualCMutableTransaction(consensusParams, heightC);
    coinbaseCNew.vin.resize(1);
    coinbaseCNew.vin[0].prevout.SetNull();
    coinbaseCNew.vin[0].scriptSig = (CScript() << heightC << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseCNew.vout.resize(1);
    coinbaseCNew.vout[0].nValue = GetBlockSubsidy(heightC, consensusParams);
    coinbaseCNew.nExpiryHeight = 0;
    CBlock blockC;
    blockC.vtx.push_back(CTransaction(coinbaseCNew));
    blockC.vtx.push_back(corruptedSpendTx);
    // Mirrors ConnectBlock's own frontier-append - independently computed from the same
    // (corrupted) transaction, so it matches regardless of what was swapped.
    SaplingMerkleFrontier frontierClone(saplingFrontier);
    frontierClone.AppendBundle(corruptedSpendTx.GetSaplingBundle());
    blockC.hashBlockCommitments = frontierClone.root();
    CValidationState stateC;
    CBlockIndex indexC;
    indexC.pprev = &indexB;
    indexC.nHeight = heightC;
    EXPECT_FALSE(chain.ConnectBlock(blockC, stateC, &indexC, true, false));
    EXPECT_EQ(stateC.GetRejectReason(), "bad-sapling-bundle-authorization");
}

// Extends TestSaplingInvalidSpendProofRejected to every other publicly-exposed,
// circuit-bound component of a Sapling spend/output: nullifier, cv (value commitment),
// and rk (randomized spend-authorization key) are all public inputs to the Groth16
// circuits (rk is also the key spend_auth_sig is verified against), and cmu is a public
// input to the output circuit. Swapping any one of them - independent of a proof or
// signature ever being corrupted directly - should still be caught by the real batch
// validator, proving it actually binds the proof/signature to *this* nullifier/cv/rk/cmu
// and not just to "some valid-looking bundle."
TEST(test_mempool, TestSaplingBundleComponentSwapsRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    blockA.hashBlockCommitments = SaplingMerkleFrontier::empty_root();
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    SaplingWallet saplingWallet;
    SaplingMerkleFrontier saplingFrontier;
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);

    auto sk_from = libzcash::SaplingSpendingKey::random();
    libzcash::SaplingFullViewingKey fvk_from;
    sk_from.expanded_spending_key().DeriveFVK(&fvk_from);
    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    libzcash::SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    libzcash::SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.InitializeSapling(uint256());
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    ASSERT_TRUE(shieldBuilder.AddSaplingOutputRaw(pk, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    saplingFrontier.AppendBundle(shieldTx.GetSaplingBundle());
    blockB.hashBlockCommitments = saplingFrontier.root();
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    saplingWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    auto vOutputs = shieldTx.GetSaplingOutputs();
    for (int j = 0; j < vOutputs.size(); j++) {
        saplingWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, j, &vOutputs[j], true);
    }
    int realOutputIndex = -1;
    std::optional<libzcash::SaplingNotePlaintext> maybe_pt;
    for (int j = 0; j < vOutputs.size(); j++) {
        maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], ivk);
        if (maybe_pt) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);
    auto cmu = uint256::FromRawBytes(vOutputs[realOutputIndex].cmu());
    auto note = maybe_pt.value().note(ivk).value();
    libzcash::MerklePath saplingMerklePath;
    ASSERT_TRUE(saplingWallet.GetMerklePathOfNote(shieldTx.GetHash(), realOutputIndex, saplingMerklePath));
    uint256 anchor;
    ASSERT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilder = TransactionBuilder(consensusParams, heightC);
    spendBuilder.InitializeSapling(anchor);
    ASSERT_TRUE(spendBuilder.AddSaplingSpendRaw(
        SaplingOutPoint(shieldTx.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(spendBuilder.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(spendBuilder.AddSaplingOutputRaw(pk, 20000, {}));
    ASSERT_TRUE(spendBuilder.ConvertRawSaplingOutput(fvk_from.ovk));
    auto maybeSpendTx = spendBuilder.Build();
    ASSERT_TRUE(maybeSpendTx.IsTx());
    auto spendTx = maybeSpendTx.GetTxOrThrow();
    ASSERT_EQ(spendTx.GetSaplingSpendsCount(), 1);
    // 40000 spent - 20000 explicit output - 10000 default fee = 10000 change, auto-added
    // as a second output by TransactionBuilder::Build() since change > 0.
    ASSERT_EQ(spendTx.GetSaplingOutputsCount(), 2);

    // Each sub-case starts fresh from the same real, valid spendTx and swaps exactly one
    // component, expecting the real batch validator to reject it via the mempool - same
    // failure mode as a corrupted proof, since these fields are exactly what the proof
    // (and, for rk, the spend_auth_sig) are supposed to bind. Deliberately mempool-only,
    // not also ConnectBlock like TestSaplingInvalidSpendProofRejected: ConnectBlock
    // mutates the shared TestChain's real view (UpdateCoins marks the tx's nullifier
    // spent, PushAnchor advances the Sapling frontier) *before* it reaches the
    // batch-validate step these sub-cases are trying to isolate, and a fJustCheck=true
    // failure doesn't roll that back - so a second sub-case's ConnectBlock attempt against
    // the same chain fails for a contaminated reason (duplicate-nullifier, mismatched
    // frontier root), not the one under test. Coverage of ConnectBlock rejecting a bad
    // proof is established once, cleanly, by TestSaplingInvalidSpendProofRejected (a
    // single attempt against a chain used only once) - both call sites run the identical
    // sapling::BatchValidator, so that's sufficient without giving every sub-case here its
    // own from-scratch chain.
    auto expectRejected = [&](const char* label, auto&& corrupt) {
        SCOPED_TRACE(label);
        CMutableTransaction mtx(spendTx);
        corrupt(mtx);
        CTransaction corrupted(mtx);
        mempool.clear();
        CValidationState state = chain.acceptTx(corrupted);
        EXPECT_FALSE(state.IsValid());
        EXPECT_EQ(state.GetRejectReason(), "bad-sapling-bundle-authorization");
        mempool.clear();
    };

    expectRejected("spend nullifier swapped", [](CMutableTransaction& mtx) {
        sapling::test_only_replace_nullifier(
            mtx.saplingBundle.GetDetailsMut(), 0, GetRandHash().GetRawBytes());
    });
    expectRejected("spend cv swapped", [](CMutableTransaction& mtx) {
        sapling::test_only_replace_spend_cv(mtx.saplingBundle.GetDetailsMut(), 0);
    });
    expectRejected("spend rk swapped", [](CMutableTransaction& mtx) {
        sapling::test_only_replace_spend_rk(mtx.saplingBundle.GetDetailsMut(), 0);
    });
    expectRejected("output cmu swapped", [](CMutableTransaction& mtx) {
        sapling::test_only_replace_output_cmu(mtx.saplingBundle.GetDetailsMut(), 0);
    });
    expectRejected("output cv swapped", [](CMutableTransaction& mtx) {
        sapling::test_only_replace_output_cv(mtx.saplingBundle.GetDetailsMut(), 0);
    });
}

TEST(test_mempool, TestIronwoodInvalidBundleProofRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    {
        uint32_t branchIdA = CurrentEpochBranchId(heightA - 1, consensusParams);
        uint256 hashChainHistoryRootA = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdA);
        uint256 hashAuthDataRootA = blockA.BuildAuthDataMerkleTree();
        blockA.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootA, hashAuthDataRootA);
    }
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    IronwoodWallet ironwoodWallet;
    IronwoodMerkleFrontier ironwoodFrontier;
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto addr = fromWallet.GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate extsk;
    ASSERT_TRUE(fromWallet.GetIronwoodExtendedSpendingKey(addr, extsk));
    libzcash::IronwoodFullViewingKey fvk;
    ASSERT_TRUE(extsk.sk.DeriveFVK(&fvk));
    libzcash::IronwoodOutgoingViewingKey ovk;
    ASSERT_TRUE(fvk.DeriveOVK(&ovk));

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    shieldBuilder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(shieldBuilder.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    coinbaseBNew.nLockTime = heightB; // see the same-block Ironwood test's nLockTime comment
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    {
        uint32_t branchIdB = CurrentEpochBranchId(heightB - 1, consensusParams);
        uint256 hashChainHistoryRootB = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdB);
        uint256 hashAuthDataRootB = blockB.BuildAuthDataMerkleTree();
        blockB.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootB, hashAuthDataRootB);
    }
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    const auto& bundleDetails = shieldTx.GetIronwoodBundle().GetDetails();
    auto actions = bundleDetails.actions();
    uint256_t ovkBytes;
    std::copy(ovk.ovk.begin(), ovk.ovk.end(), ovkBytes.begin());
    int realActionIndex = -1;
    uint64_t noteValue = 0;
    uint256_t noteRho{}, noteRseed{};
    for (size_t i = 0; i < actions.size(); i++) {
        uint256_t ock;
        if (!ironwood::derive_ironwood_ock(actions[i], ovkBytes, ock)) {
            continue;
        }
        uint64_t testValue;
        std::array<uint8_t, 43> testAddress;
        std::array<uint8_t, 512> testMemo;
        uint256_t testRho, testRseed;
        if (ironwood::try_ironwood_decrypt_action_ock(
                actions[i], ock, testValue, testAddress, testMemo, testRho, testRseed)) {
            realActionIndex = (int)i;
            noteValue = testValue;
            noteRho = testRho;
            noteRseed = testRseed;
            break;
        }
    }
    ASSERT_NE(realActionIndex, -1);
    uint256 rho = uint256::FromRawBytes(noteRho);
    uint256 rseed = uint256::FromRawBytes(noteRseed);
    uint256 cmx = uint256::FromRawBytes(actions[realActionIndex].cmx());

    ironwoodWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, (int)j, &actions[j], true);
    }
    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(shieldTx.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilder = TransactionBuilder(consensusParams, heightC);
    spendBuilder.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilder.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilder.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilder.AddIronwoodOutputRaw(addr, 20000, {}));
    ASSERT_TRUE(spendBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTx = spendBuilder.Build();
    ASSERT_TRUE(maybeSpendTx.IsTx());
    auto spendTx = maybeSpendTx.GetTxOrThrow();

    // Corrupt the bundle's single Halo2 proof - every action, the binding signature, and
    // every other byte of the transaction are untouched.
    CMutableTransaction corruptedMtx(spendTx);
    ironwood_bundle::test_only_corrupt_proof(corruptedMtx.ironwoodBundle.GetDetailsMut());
    CTransaction corruptedSpendTx(corruptedMtx);

    mempool.clear();
    CValidationState acceptState = chain.acceptTx(corruptedSpendTx);
    EXPECT_FALSE(acceptState.IsValid());
    EXPECT_EQ(acceptState.GetRejectReason(), "bad-ironwood-bundle-authorization");
    mempool.clear();

    // Same corrupted transaction via ConnectBlock() instead of the mempool - a distinct
    // code path (ConnectBlock sets up and runs its own real ironwood::BatchValidator,
    // separately from AcceptToMemoryPool's) that a directly-mined or block-relayed
    // transaction bypassing the mempool entirely would go through.
    CMutableTransaction coinbaseCNew = CreateNewContextualCMutableTransaction(consensusParams, heightC);
    coinbaseCNew.vin.resize(1);
    coinbaseCNew.vin[0].prevout.SetNull();
    coinbaseCNew.vin[0].scriptSig = (CScript() << heightC << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseCNew.vout.resize(1);
    coinbaseCNew.vout[0].nValue = GetBlockSubsidy(heightC, consensusParams);
    coinbaseCNew.nExpiryHeight = 0;
    coinbaseCNew.nLockTime = heightC; // see the same-block Ironwood test's nLockTime comment
    CBlock blockC;
    blockC.vtx.push_back(CTransaction(coinbaseCNew));
    blockC.vtx.push_back(corruptedSpendTx);
    {
        uint32_t branchIdC = CurrentEpochBranchId(heightC - 1, consensusParams);
        uint256 hashChainHistoryRootC = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdC);
        uint256 hashAuthDataRootC = blockC.BuildAuthDataMerkleTree();
        blockC.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootC, hashAuthDataRootC);
    }
    CValidationState stateC;
    CBlockIndex indexC;
    indexC.pprev = &indexB;
    indexC.nHeight = heightC;
    EXPECT_FALSE(chain.ConnectBlock(blockC, stateC, &indexC, true, false));
    EXPECT_EQ(stateC.GetRejectReason(), "bad-ironwood-bundle-authorization");
}

// Ironwood analogue of TestSaplingBundleComponentSwapsRejected. Orchard/Ironwood unifies
// a spend and an output into a single Action, so there's one component set (rather than
// separate spend-side/output-side ones) to exercise: nullifier, cmx (note commitment),
// cv_net (value commitment), and rk are all public inputs to the action circuit (rk is
// also the key spend_auth_sig is verified against). Swapping any one of them - even with
// the aggregate Halo2 proof itself untouched - should still be caught by the real batch
// validator, the same way TestIronwoodInvalidBundleProofRejected's corrupted proof is.
TEST(test_mempool, TestIronwoodBundleComponentSwapsRejected)
{
    TestChain chain;
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    ASSERT_GT(chain.GetIndex()->nHeight, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    uint64_t savedReward0 = ASSETCHAINS_REWARD[0];
    ASSETCHAINS_REWARD[0] = 1000000;
    struct UpgradeReverter {
        uint64_t savedReward0;
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            ASSETCHAINS_REWARD[0] = savedReward0;
        }
    } upgradeReverter{savedReward0};
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtestBlockConnect);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    int32_t heightA = chain.GetIndex()->nHeight + 1;
    CMutableTransaction coinbaseANew = CreateNewContextualCMutableTransaction(consensusParams, heightA);
    coinbaseANew.vin.resize(1);
    coinbaseANew.vin[0].prevout.SetNull();
    coinbaseANew.vin[0].scriptSig = (CScript() << heightA << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseANew.vout.resize(1);
    coinbaseANew.vout[0].scriptPubKey = scriptPubKey;
    coinbaseANew.vout[0].nValue = 50000;
    coinbaseANew.nExpiryHeight = 0;
    CTransaction coinbaseA(coinbaseANew);
    CBlock blockA;
    blockA.vtx.push_back(coinbaseA);
    {
        uint32_t branchIdA = CurrentEpochBranchId(heightA - 1, consensusParams);
        uint256 hashChainHistoryRootA = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdA);
        uint256 hashAuthDataRootA = blockA.BuildAuthDataMerkleTree();
        blockA.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootA, hashAuthDataRootA);
    }
    CValidationState stateA;
    CBlockIndex indexA;
    indexA.pprev = chain.GetIndex();
    indexA.nHeight = heightA;
    uint256 hashA = blockA.GetHash();
    indexA.phashBlock = &hashA;
    mapBlockIndex[hashA] = &indexA;
    struct MapIndexEraserA {
        uint256 hash;
        ~MapIndexEraserA() { mapBlockIndex.erase(hash); }
    } eraseIndexA{hashA};
    ASSERT_TRUE(chain.ConnectBlock(blockA, stateA, &indexA, true, false)) << stateA.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashA);

    IronwoodWallet ironwoodWallet;
    IronwoodMerkleFrontier ironwoodFrontier;
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto addr = fromWallet.GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate extsk;
    ASSERT_TRUE(fromWallet.GetIronwoodExtendedSpendingKey(addr, extsk));
    libzcash::IronwoodFullViewingKey fvk;
    ASSERT_TRUE(extsk.sk.DeriveFVK(&fvk));
    libzcash::IronwoodOutgoingViewingKey ovk;
    ASSERT_TRUE(fvk.DeriveOVK(&ovk));

    int32_t heightB = heightA + 1;
    auto shieldBuilder = TransactionBuilder(consensusParams, heightB, &keystore);
    shieldBuilder.AddTransparentInput(COutPoint(coinbaseA.GetHash(), 0), scriptPubKey, coinbaseA.vout[0].nValue);
    shieldBuilder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(shieldBuilder.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(shieldBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeShieldTx = shieldBuilder.Build();
    ASSERT_TRUE(maybeShieldTx.IsTx());
    auto shieldTx = maybeShieldTx.GetTxOrThrow();

    CMutableTransaction coinbaseBNew = CreateNewContextualCMutableTransaction(consensusParams, heightB);
    coinbaseBNew.vin.resize(1);
    coinbaseBNew.vin[0].prevout.SetNull();
    coinbaseBNew.vin[0].scriptSig = (CScript() << heightB << CScriptNum(1)) + COINBASE_FLAGS;
    coinbaseBNew.vout.resize(1);
    coinbaseBNew.vout[0].nValue = GetBlockSubsidy(heightB, consensusParams);
    coinbaseBNew.nExpiryHeight = 0;
    coinbaseBNew.nLockTime = heightB; // see the same-block Ironwood test's nLockTime comment
    CBlock blockB;
    blockB.vtx.push_back(CTransaction(coinbaseBNew));
    blockB.vtx.push_back(shieldTx);
    {
        uint32_t branchIdB = CurrentEpochBranchId(heightB - 1, consensusParams);
        uint256 hashChainHistoryRootB = chain.GetCoinsViewCache()->GetHistoryRoot(branchIdB);
        uint256 hashAuthDataRootB = blockB.BuildAuthDataMerkleTree();
        blockB.hashBlockCommitments = DeriveBlockCommitmentsHash(hashChainHistoryRootB, hashAuthDataRootB);
    }
    CValidationState stateB;
    CBlockIndex indexB;
    indexB.pprev = &indexA;
    indexB.nHeight = heightB;
    uint256 hashB = blockB.GetHash();
    indexB.phashBlock = &hashB;
    mapBlockIndex[hashB] = &indexB;
    struct MapIndexEraserB {
        uint256 hash;
        ~MapIndexEraserB() { mapBlockIndex.erase(hash); }
    } eraseIndexB{hashB};
    ASSERT_TRUE(chain.ConnectBlock(blockB, stateB, &indexB, true, false)) << stateB.GetRejectReason();
    chain.GetCoinsViewCache()->SetBestBlock(hashB);

    const auto& bundleDetails = shieldTx.GetIronwoodBundle().GetDetails();
    auto actions = bundleDetails.actions();
    uint256_t ovkBytes;
    std::copy(ovk.ovk.begin(), ovk.ovk.end(), ovkBytes.begin());
    int realActionIndex = -1;
    uint64_t noteValue = 0;
    uint256_t noteRho{}, noteRseed{};
    for (size_t i = 0; i < actions.size(); i++) {
        uint256_t ock;
        if (!ironwood::derive_ironwood_ock(actions[i], ovkBytes, ock)) {
            continue;
        }
        uint64_t testValue;
        std::array<uint8_t, 43> testAddress;
        std::array<uint8_t, 512> testMemo;
        uint256_t testRho, testRseed;
        if (ironwood::try_ironwood_decrypt_action_ock(
                actions[i], ock, testValue, testAddress, testMemo, testRho, testRseed)) {
            realActionIndex = (int)i;
            noteValue = testValue;
            noteRho = testRho;
            noteRseed = testRseed;
            break;
        }
    }
    ASSERT_NE(realActionIndex, -1);
    uint256 rho = uint256::FromRawBytes(noteRho);
    uint256 rseed = uint256::FromRawBytes(noteRseed);
    uint256 cmx = uint256::FromRawBytes(actions[realActionIndex].cmx());

    ironwoodWallet.CreateEmptyPositionsForTxid(heightB, shieldTx.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(heightB, shieldTx.GetHash(), 0, (int)j, &actions[j], true);
    }
    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(shieldTx.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    int32_t heightC = heightB + 1;
    auto spendBuilder = TransactionBuilder(consensusParams, heightC);
    spendBuilder.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(spendBuilder.AddIronwoodSpendRaw(
        IronwoodOutPoint(shieldTx.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(spendBuilder.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(spendBuilder.AddIronwoodOutputRaw(addr, 20000, {}));
    ASSERT_TRUE(spendBuilder.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybeSpendTx = spendBuilder.Build();
    ASSERT_TRUE(maybeSpendTx.IsTx());
    auto spendTx = maybeSpendTx.GetTxOrThrow();

    // Deliberately mempool-only - see the Sapling version of this test (
    // TestSaplingBundleComponentSwapsRejected) for why repeating ConnectBlock across
    // many sub-cases against one shared TestChain isn't safe, and why
    // TestIronwoodInvalidBundleProofRejected's single ConnectBlock attempt already
    // establishes that ConnectBlock runs the identical ironwood::BatchValidator.
    auto expectRejected = [&](const char* label, auto&& corrupt) {
        SCOPED_TRACE(label);
        CMutableTransaction mtx(spendTx);
        corrupt(mtx);
        CTransaction corrupted(mtx);
        mempool.clear();
        CValidationState state = chain.acceptTx(corrupted);
        EXPECT_FALSE(state.IsValid());
        EXPECT_EQ(state.GetRejectReason(), "bad-ironwood-bundle-authorization");
        mempool.clear();
    };

    expectRejected("action nullifier swapped", [](CMutableTransaction& mtx) {
        ironwood_bundle::test_only_replace_nullifier(mtx.ironwoodBundle.GetDetailsMut(), 0);
    });
    expectRejected("action cmx swapped", [](CMutableTransaction& mtx) {
        ironwood_bundle::test_only_replace_cmx(mtx.ironwoodBundle.GetDetailsMut(), 0);
    });
    expectRejected("action cv swapped", [](CMutableTransaction& mtx) {
        ironwood_bundle::test_only_replace_cv(mtx.ironwoodBundle.GetDetailsMut(), 0);
    });
    expectRejected("action rk swapped", [](CMutableTransaction& mtx) {
        ironwood_bundle::test_only_replace_rk(mtx.ironwoodBundle.GetDetailsMut(), 0);
    });
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

// Phase 4 of the multiwallet effort: CWalletManager::LoadWallet() now
// registers a secondary wallet for ChainTip() notifications and catches it
// up to the current tip, instead of leaving it a permanently frozen
// snapshot. This drives a real TestChain rather than a synthetic
// CBlockIndex, because CWallet::ChainTip() (via IncrementSaplingWallet etc.)
// reads chainActive/pcoinsTip for real -- a fake index would crash inside
// that machinery rather than exercise it.
TEST(test_block, SecondaryWalletReceivesChainTipNotificationsAfterLoad)
{
    // TestChain doesn't reset bitdb itself: its own wallets are in-memory
    // TestWallet instances that don't care where their backing file
    // physically lands. CWalletManager::LoadWallet()'s file-exists check
    // does care (it resolves the path via the real GetDataDir(), independent
    // of whatever directory bitdb happens to still be bound to from an
    // earlier test) -- give it a fresh CDBEnv bound to this test's own
    // datadir first, the same pattern test_walletmanager.cpp's fixture uses.
    //
    // RAII, not a plain restore at the end of the test body: every ASSERT_*
    // below returns immediately on failure, which would otherwise skip the
    // restore and leave the global bitdb (and, if UnloadWallet's own
    // ASSERT_TRUE fired, a still-registered secondary wallet in
    // CWalletManager) installed as state for whatever test in this binary
    // runs next.
    struct GlobalStateCleanup {
        std::shared_ptr<CDBEnv> previousBitdb;
        // No-default-wallet redesign: this test now registers a
        // RegisterInitialWallet()-based "default_test.dat" (see below) so
        // "secondarytestwallet" is a genuine secondary rather than the
        // first-loaded-into-empty-registry wallet that would otherwise become
        // active. RegisterInitialWallet() never calls
        // RegisterValidationInterface() (production's equivalent, init.cpp,
        // does so separately, itself), so Reset() alone is safe for it --
        // unlike UnloadWallet()/FlushAndUnloadAllExceptActiveWallet(), it
        // doesn't need an UnregisterValidationInterface() call first, only
        // needs deleting directly (matching test_walletmanager.cpp's own
        // fixture, which owns the identical object lifetime).
        CWallet* defaultWallet = nullptr;
        ~GlobalStateCleanup() {
            // Opus-audit-caught: on the happy path the in-test UnloadWallet()
            // call below already removes "secondarytestwallet" (a
            // LoadWallet()-registered, and therefore validation-interface-
            // registered, wallet) before this destructor runs -- but on an
            // earlier ASSERT_* failure it wouldn't have, and Reset() alone
            // (unlike UnloadWallet()/FlushAndUnloadAllExceptActiveWallet())
            // does not call UnregisterValidationInterface() first. Matches
            // the other three CWalletManager-using gtest fixtures in this
            // tree (test_walletmanager.cpp, test_httprpc.cpp,
            // test_rpc_wallet_bitcoin.cpp), all of which call this before
            // Reset() for the same reason.
            CWalletManager::Get().FlushAndUnloadAllExceptActiveWallet();
            CWalletManager::Get().Reset();
            delete defaultWallet;
            bitdb->Flush(true);
            bitdb->Reset();
            bitdb = previousBitdb;
        }
    } cleanup{bitdb};
    bitdb = std::shared_ptr<CDBEnv>(new CDBEnv{});

    cleanup.defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", cleanup.defaultWallet);

    TestChain chain;
    // Must be set after TestChain's constructor, not before: it resets
    // chainName to the KMD default itself as part of its own setup (see the
    // comment in TestChain::TestChain()), which would otherwise silently
    // clobber this back before any block is generated.
    chainName = assetchain("TST");
    auto notary = std::make_shared<TestWallet>(chain.getNotaryKey(), "notary");
    chain.generateBlock(notary); // genesis
    chain.generateBlock(notary); // one real block on top of genesis

    bool fFirstRun;
    CWallet scratch("secondarytestwallet");
    ASSERT_EQ(DB_LOAD_OK, scratch.LoadWallet(fFirstRun));

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondarytestwallet", strError)) << strError;
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondarytestwallet");
    ASSERT_NE(nullptr, secondaryWallet);

    // The load-time catch-up (walletmanager.cpp) should already have run the
    // witness validate-or-rebuild step for both pools -- chainHeight itself
    // isn't set by that step (it's only ever set inside ChainTip() proper,
    // after these same calls run there too; init.cpp's own startup catch-up
    // for the default wallet doesn't set it either), so
    // saplingWalletPositionsValidated/ironwoodWalletPositionsValidated are
    // the right signal that the catch-up sequence actually ran end to end.
    EXPECT_TRUE(secondaryWallet->saplingWalletPositionsValidated);
    EXPECT_TRUE(secondaryWallet->ironwoodWalletPositionsValidated);

    // Advance the chain: this is where chainHeight becomes the right check
    // -- it's set inside ChainTip() itself, so this is the actual regression
    // guard for "does ChainTip() now fire at all for a registered secondary
    // wallet." Before Phase 4 this wallet was never registered, so
    // chainHeight would stay 0 forever regardless of how many more blocks
    // connect.
    chain.generateBlock(notary);
    EXPECT_EQ(chain.GetIndex()->nHeight, secondaryWallet->chainHeight);

    // Unload, then advance again. If UnregisterValidationInterface() were
    // missing from UnloadWallet(), this generateBlock() call would
    // dereference the just-freed `secondaryWallet` from inside ChainTip() on
    // the block-connection path -- there is no graceful assertion for a
    // use-after-free; the absence of a crash here is the actual check.
    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("secondarytestwallet", strError)) << strError;
    EXPECT_NO_THROW(chain.generateBlock(notary));

    // Cleanup runs via `cleanup`'s destructor above, on every exit path.
}