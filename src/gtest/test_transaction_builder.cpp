// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key_io.h"
#include "main.h"
#include "pubkey.h"
#include "transaction_builder.h"
#include "wallet/wallet.h"
#include "zcash/Address.hpp"
#include "gtest/gtestutils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// TransactionBuilder tests. Most assertions here are transparent
// input/output/fee/change-address cases plus one CheckSaplingTxVersion case
// guarding Sapling spend/output rejection on a pre-Sapling transaction.
// TransactionBuilder.Invoke and TransactionBuilder.IronwoodShieldAndSpend
// cover the Sapling and Ironwood shield-then-spend builder paths respectively.

using libzcash::SaplingIncomingViewingKey;
using libzcash::SaplingPaymentAddress;

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

TEST(TransactionBuilder, Invoke)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();
    auto consensusId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Create coinbase tx
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew.nExpiryHeight = 0;
    txNew.vout[0].nValue = 50000; // Add 50000 ARRRtoshis to the coinbase output for testing
    CTransaction coinbaseTx(txNew);

    // Create a CCoinsViewCache to hold the coins
    CCoinsView baseView;
    CCoinsViewCache view(&baseView);

    // Create wallets for Sapling and Ironwood
    SaplingWallet saplingWallet;
    IronwoodWallet ironwoodWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    IronwoodMerkleFrontier ironwoodFrontier;

    // Initialize the Sapling and Ironwood wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx, view, 1);

    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto expsk_from = sk_from.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk_from;
    expsk_from.DeriveFVK(&fvk_from);

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    // depth/parentFVKTag/childIndex are plain POD fields with no default member
    // initializer; leaving them uninitialized produces a garbage childIndex that
    // the Rust deserializer now rejects ("Unsupported child index in encoding").
    // Only .expsk matters for ConvertRawSaplingSpend's signing authority, so
    // zero the rest to a valid (masterlike) shape.
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    // Create a shielding transaction from transparent to Sapling
    // 0.0005 t-ARRR in, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
    builder1.InitializeSapling(uint256());
    builder1.AddTransparentInput(COutPoint(coinbaseTx.GetHash(),0), scriptPubKey, 50000);
    builder1.AddSaplingOutputRaw(pk, 40000, {});
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    ASSERT_EQ(maybe_tx1.IsTx(), true);
    auto tx1 = maybe_tx1.GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.vjoinsplit.size(), 0);
    EXPECT_EQ(tx1.GetSaplingSpendsCount(), 0);
    // MIN_SHIELDED_OUTPUTS pads a bundle with >=1 real spend/output to at least 2 outputs.
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 2);
    EXPECT_EQ(tx1.GetValueBalanceSapling(), -40000);

    CValidationState state;

    // Check that the transaction is valid without proof verification
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx1, state));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Check that the transaction is valid in the current context
    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx1, 1, false);
    EXPECT_TRUE(results0.validationPassed);

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx1;
    std::vector<std::vector<const CTransaction*>> vvtx1;
    vtx1.push_back(&tx1);
    vvtx1.push_back(vtx1);

    //Shielded Bundle Contextual Check
    CheckTransationResults results1 = ContextualCheckTransactionShieldedBundles(vvtx1[0], &view, consensusId, false, 0);
    EXPECT_TRUE(results1.validationPassed);

    // Update Coins
    UpdateCoins(tx1, view, 1);

    // UpdateCoins doesn't push anchors on its own (see main.cpp's ConnectBlock,
    // which does this separately) - without this, ContextualCheckShieldedInputs
    // can never recognize an anchor computed from a spend built on tx1's outputs.
    if (tx1.GetSaplingBundle().IsPresent()) {
        saplingFrontier.AppendBundle(tx1.GetSaplingBundle());
        view.PushAnchor(saplingFrontier);
    }

    // Update the wallets with the new transaction
    if (tx1.GetSaplingBundle().IsPresent()) {
        saplingWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vOutputs = tx1.GetSaplingOutputs();
        for (int j = 0; j < vOutputs.size(); j++) {
            saplingWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vOutputs[j], true);
        }
    }
    if (tx1.GetIronwoodBundle().IsPresent()) {
        ironwoodWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetIronwoodBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            ironwoodWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created. The Sapling builder pads to a
    // privacy-floor minimum of 2 outputs (MIN_SHIELDED_OUTPUTS) and shuffles real and
    // dummy outputs together, so the real one isn't necessarily at index 0 - find it by
    // trial decryption instead.
    auto vOutputs = tx1.GetSaplingOutputs();
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

    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), realOutputIndex, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Create a Sapling-only transaction
    // 0.0004 z-ARRR in, 0.00025 z-ARRR out, 0.0001 t-ARRR fee, 0.00005 z-ARRR change
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.InitializeSapling(anchor);
    EXPECT_TRUE(builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor));

    // Check that trying to add a different anchor fails
    EXPECT_FALSE(builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, uint256()));

    // Convert the Sapling spend with the extended spending key
    EXPECT_TRUE(builder2.ConvertRawSaplingSpend(extsk));    

    // Add a Sapling output
    builder2.AddSaplingOutputRaw(pk, 25000, {});
    builder2.ConvertRawSaplingOutput(fvk_from.ovk);

    // Build the transaction
    auto maybe_tx2 = builder2.Build();
    ASSERT_EQ(maybe_tx2.IsTx(), true);
    auto tx2 = maybe_tx2.GetTxOrThrow();

    EXPECT_EQ(tx2.vin.size(), 0);
    EXPECT_EQ(tx2.vout.size(), 0);
    EXPECT_EQ(tx2.vjoinsplit.size(), 0);
    EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
    EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
    EXPECT_EQ(tx2.GetValueBalanceSapling(), 10000);

    // Check that the transaction is valid without proof verification
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx2, state));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Check that the transaction is valid in the current context
    CheckTransationResults results2 = ContextualCheckTransactionSingleThreaded(tx2, 3, false);
    EXPECT_TRUE(results2.validationPassed);

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx2;
    std::vector<std::vector<const CTransaction*>> vvtx2;
    vtx2.push_back(&tx2);
    vvtx2.push_back(vtx2);

    //Shielded Bundle Contextual Check
    CheckTransationResults results3 = ContextualCheckTransactionShieldedBundles(vvtx2[0], &view, consensusId, false, 0);
    EXPECT_TRUE(results3.validationPassed);
}

// Phase 4 protocol-coverage audit: AddIronwoodSpendRaw/ConvertRawIronwoodSpend
// (and, less critically, InitializeIronwood/AddIronwoodOutputRaw/
// ConvertRawIronwoodOutput) had zero direct coverage anywhere in the gtest
// suite prior to this - only Sapling's builder path was exercised here, and
// Ironwood's output (not spend) side got incidental exercise via
// gtest/test_paymentdisclosure.cpp. This mirrors the Sapling shield-then-spend
// flow in TransactionBuilder.Invoke above, but for the Ironwood pool end to end.
TEST(TransactionBuilder, IronwoodShieldAndSpend)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();
    auto consensusId = NetworkUpgradeInfo[Consensus::UPGRADE_IRONWOOD].nBranchId;

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

    IronwoodWallet ironwoodWallet;
    IronwoodMerkleFrontier ironwoodFrontier;
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    // Wallet holding the Ironwood key we'll shield to, then spend from.
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

    // Shielding transaction: transparent -> Ironwood output only.
    // 0.0005 t-ARRR in, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
    builder1.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder1.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(builder1.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(builder1.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybe_tx1 = builder1.Build();
    ASSERT_EQ(maybe_tx1.IsTx(), true);
    auto tx1 = maybe_tx1.GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.GetIronwoodActionsCount(), 2); // real + dummy, per Orchard's min-2-actions padding
    EXPECT_EQ(tx1.GetValueBalanceIronwood(), -40000);

    CValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx1, state));
    EXPECT_EQ(state.GetRejectReason(), "");
    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx1, 1, false);
    EXPECT_TRUE(results0.validationPassed);

    // Find the real (non-dummy) action via trial decryption using our own OVK,
    // exactly as gtest/test_paymentdisclosure.cpp's IronwoodGenerateAndVerify does.
    const auto& bundleDetails = tx1.GetIronwoodBundle().GetDetails();
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
    EXPECT_EQ(noteValue, 40000);
    uint256 rho = uint256::FromRawBytes(noteRho);
    uint256 rseed = uint256::FromRawBytes(noteRseed);
    uint256 cmx = uint256::FromRawBytes(actions[realActionIndex].cmx());

    // Populate the wallet's note-commitment tree with tx1's actions (mirrors
    // the Sapling equivalent in TransactionBuilder.Invoke above).
    ironwoodWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, (int)j, &actions[j], true);
    }

    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(tx1.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    // Ironwood-only transaction: spend the note just created, send some of it
    // to a different address, and let the builder auto-generate change.
    // 0.0004 z-ARRR in, 0.00025 z-ARRR out, 0.0001 z-ARRR fee, 0.00005 z-ARRR change
    CKeyingMaterial recipientRawSeed(32, 7);
    HDSeed recipientSeed(recipientRawSeed);
    auto recipientExtsk = libzcash::IronwoodExtendedSpendingKeyPirate::Master(recipientSeed);
    libzcash::IronwoodPaymentAddress recipientAddr;
    ASSERT_TRUE(recipientExtsk.sk.DeriveDefaultAddress(&recipientAddr));

    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    EXPECT_TRUE(builder2.AddIronwoodSpendRaw(
        IronwoodOutPoint(tx1.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));

    // Check that trying to add a different anchor fails, mirroring the Sapling case above.
    EXPECT_FALSE(builder2.AddIronwoodSpendRaw(
        IronwoodOutPoint(tx1.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, uint256()));

    EXPECT_TRUE(builder2.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(builder2.AddIronwoodOutputRaw(recipientAddr, 25000, {}));
    ASSERT_TRUE(builder2.ConvertRawIronwoodOutput(ovk.ovk));

    auto maybe_tx2 = builder2.Build();
    ASSERT_EQ(maybe_tx2.IsTx(), true);
    auto tx2 = maybe_tx2.GetTxOrThrow();

    EXPECT_EQ(tx2.vin.size(), 0);
    EXPECT_EQ(tx2.vout.size(), 0);
    EXPECT_EQ(tx2.GetValueBalanceIronwood(), 10000); // 40000 spent - (25000 output + 5000 auto-change)

    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx2, state));
    EXPECT_EQ(state.GetRejectReason(), "");
    CheckTransationResults results2 = ContextualCheckTransactionSingleThreaded(tx2, 3, false);
    EXPECT_TRUE(results2.validationPassed);
}

// CheckTransactionWithoutProofVerification's own std::set-based duplicate-nullifier
// check (main.cpp, "bad-spend-description-nullifiers-duplicate") has no direct test
// coverage anywhere in the suite. Neither the TransactionBuilder nor the underlying
// (unmodified upstream) sapling-crypto builder rejects adding the same note twice -
// that's a consensus-level rule, not a builder-level one - so this exercises the
// actual guard: spend the identical, genuinely-anchored note twice within one
// transaction (mirroring TransactionBuilder.Invoke's shield-then-spend setup) and
// confirm CheckTransaction rejects the resulting two-spends-same-nullifier bundle.
TEST(TransactionBuilder, SaplingDuplicateNullifierWithinTxRejected)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

    SaplingWallet saplingWallet;
    SaplingMerkleFrontier saplingFrontier;
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);

    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto expsk_from = sk_from.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk_from;
    expsk_from.DeriveFVK(&fvk_from);

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    // Shielding transaction: transparent -> Sapling output only.
    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
    builder1.InitializeSapling(uint256());
    builder1.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder1.AddSaplingOutputRaw(pk, 40000, {});
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    ASSERT_EQ(maybe_tx1.IsTx(), true);
    auto tx1 = maybe_tx1.GetTxOrThrow();

    saplingWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
    auto vOutputs = tx1.GetSaplingOutputs();
    for (int j = 0; j < vOutputs.size(); j++) {
        saplingWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vOutputs[j], true);
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
    ASSERT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), realOutputIndex, saplingMerklePath));
    uint256 anchor;
    ASSERT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Spend the exact same real, anchored note twice in one transaction.
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.InitializeSapling(anchor);
    ASSERT_TRUE(builder2.AddSaplingSpendRaw(
        SaplingOutPoint(tx1.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(builder2.AddSaplingSpendRaw(
        SaplingOutPoint(tx1.GetHash(), realOutputIndex), pk, note.value(), note.rcm(), saplingMerklePath, anchor));
    ASSERT_TRUE(builder2.ConvertRawSaplingSpend(extsk));
    ASSERT_TRUE(builder2.AddSaplingOutputRaw(pk, 25000, {}));
    ASSERT_TRUE(builder2.ConvertRawSaplingOutput(fvk_from.ovk));

    auto maybe_tx2 = builder2.Build();
    ASSERT_EQ(maybe_tx2.IsTx(), true);
    auto tx2 = maybe_tx2.GetTxOrThrow();
    EXPECT_EQ(tx2.GetSaplingSpendsCount(), 2);

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(0, tx2, state));
    EXPECT_EQ(state.GetRejectReason(), "bad-spend-description-nullifiers-duplicate");
}

TEST(TransactionBuilder, IronwoodDuplicateNullifierWithinTxRejected)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    ASSERT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000;
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);

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

    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
    builder1.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder1.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(builder1.AddIronwoodOutputRaw(addr, 40000, {}));
    ASSERT_TRUE(builder1.ConvertRawIronwoodOutput(ovk.ovk));
    auto maybe_tx1 = builder1.Build();
    ASSERT_EQ(maybe_tx1.IsTx(), true);
    auto tx1 = maybe_tx1.GetTxOrThrow();

    const auto& bundleDetails = tx1.GetIronwoodBundle().GetDetails();
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

    ironwoodWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
    for (size_t j = 0; j < actions.size(); j++) {
        ironwoodWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, (int)j, &actions[j], true);
    }

    libzcash::MerklePath ironwoodMerklePath;
    ASSERT_TRUE(ironwoodWallet.GetMerklePathOfNote(tx1.GetHash(), realActionIndex, ironwoodMerklePath));
    uint256 anchor;
    ASSERT_TRUE(ironwoodWallet.GetPathRootWithCMU(ironwoodMerklePath, cmx, anchor));

    // Spend the exact same real, anchored note twice in one transaction.
    auto builder2 = TransactionBuilder(consensusParams, 2);
    builder2.InitializeIronwood(/*spendsEnabled=*/true, /*outputsEnabled=*/true, anchor);
    ASSERT_TRUE(builder2.AddIronwoodSpendRaw(
        IronwoodOutPoint(tx1.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(builder2.AddIronwoodSpendRaw(
        IronwoodOutPoint(tx1.GetHash(), realActionIndex), addr, noteValue, rho, rseed, ironwoodMerklePath, anchor));
    ASSERT_TRUE(builder2.ConvertRawIronwoodSpend(extsk));
    ASSERT_TRUE(builder2.AddIronwoodOutputRaw(addr, 25000, {}));
    ASSERT_TRUE(builder2.ConvertRawIronwoodOutput(ovk.ovk));

    auto maybe_tx2 = builder2.Build();
    ASSERT_EQ(maybe_tx2.IsTx(), true);
    auto tx2 = maybe_tx2.GetTxOrThrow();

    CValidationState state;
    EXPECT_FALSE(CheckTransactionWithoutProofVerification(0, tx2, state));
    EXPECT_EQ(state.GetRejectReason(), "bad-ironwood-nullifiers-duplicate");
}

TEST(TransactionBuilder, ThrowsOnTransparentInputWithoutKeyStore)
{
    auto consensusParams = Params().GetConsensus();

    auto builder = TransactionBuilder(consensusParams, 1);
    ASSERT_THROW(builder.AddTransparentInput(COutPoint(), CScript(), 1), std::runtime_error);
}

TEST(TransactionBuilder, RejectsInvalidTransparentOutput)
{
    auto consensusParams = Params().GetConsensus();

    // Default CTxDestination type is an invalid address
    CTxDestination taddr;
    auto builder = TransactionBuilder(consensusParams, 1);
    EXPECT_FALSE(builder.AddTransparentOutput(taddr, 50));
}

TEST(TransactionBuilder, RejectsInvalidTransparentChangeAddress)
{
    auto consensusParams = Params().GetConsensus();

    // Default CTxDestination type is an invalid address
    CTxDestination taddr;
    auto builder = TransactionBuilder(consensusParams, 1);
    EXPECT_FALSE(builder.SendChangeTo(taddr));
}

TEST(TransactionBuilder, FailsWithNegativeChange)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();
    auto consensusId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

    // Create coinbase txs
    CMutableTransaction txNew1 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew1.vin.resize(1);
    txNew1.vin[0].prevout.SetNull();
    txNew1.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew1.vout.resize(1);
    txNew1.vout[0].scriptPubKey = scriptPubKey;
    txNew1.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew1.nExpiryHeight = 0;
    txNew1.vout[0].nValue = 25000; // Add 25000 ARRRtoshis to the coinbase output for testing
    CTransaction coinbaseTx1(txNew1);

    CMutableTransaction txNew2 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew2.vin.resize(1);
    txNew2.vin[0].prevout.SetNull();
    txNew2.vin[0].scriptSig = (CScript() << 2 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew2.vout.resize(1);
    txNew2.vout[0].scriptPubKey = scriptPubKey;
    txNew2.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew2.nExpiryHeight = 0;
    txNew2.vout[0].nValue = 25000; // Add 25000 ARRRtosis to the coinbase output for testing
    CTransaction coinbaseTx2(txNew2);

    // Create a CCoinsViewCache to hold the coins
    CCoinsView baseView;
    CCoinsViewCache view(&baseView);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx1, view, 1);
    UpdateCoins(coinbaseTx2, view, 2);

    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto expsk_from = sk_from.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk_from;
    expsk_from.DeriveFVK(&fvk_from);

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    // depth/parentFVKTag/childIndex are plain POD fields with no default member
    // initializer; leaving them uninitialized produces a garbage childIndex that
    // the Rust deserializer now rejects ("Unsupported child index in encoding").
    // Only .expsk matters for ConvertRawSaplingSpend's signing authority, so
    // zero the rest to a valid (masterlike) shape.
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    // Create a shielding transaction from transparent to Sapling
    // Fail if there is only a Sapling output
    // 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder1 = TransactionBuilder(consensusParams, 3, &keystore);
    builder1.InitializeSapling(uint256());
    builder1.AddSaplingOutputRaw(pk, 40000, {});
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    EXPECT_FALSE(maybe_tx1.IsTx());

    // Create a shielding transaction from transparent to Sapling
    // Fail if Sapling output > Transparent Input
    // 0.00025 z-ARRR, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder2 = TransactionBuilder(consensusParams, 3, &keystore);
    builder2.InitializeSapling(uint256());
    builder2.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 25000);
    builder2.AddSaplingOutputRaw(pk, 40000, {});
    builder2.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx2 = builder2.Build();
    EXPECT_FALSE(maybe_tx2.IsTx());

    // Create a shielding transaction from transparent to Sapling
    // Succeed with valid inputs and outputs
    // 0.0005 z-ARRR, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder3 = TransactionBuilder(consensusParams, 3, &keystore);
    builder3.InitializeSapling(uint256());
    builder3.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 25000);
    builder3.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 25000);
    builder3.AddSaplingOutputRaw(pk, 40000, {});
    builder3.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx3 = builder3.Build();
    EXPECT_TRUE(maybe_tx3.IsTx());
}

TEST(TransactionBuilder, ChangeOutput)
{

    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();
    auto consensusId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto tkeyid = tsk.GetPubKey().GetID();
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());
    CTxDestination taddr = tkeyid;

    // Create coinbase txs
    CMutableTransaction txNew1 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew1.vin.resize(1);
    txNew1.vin[0].prevout.SetNull();
    txNew1.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew1.vout.resize(1);
    txNew1.vout[0].scriptPubKey = scriptPubKey;
    txNew1.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew1.nExpiryHeight = 0;
    txNew1.vout[0].nValue = 50000; // Add 50000 ARRRtoshis to the coinbase output for testing
    CTransaction coinbaseTx1(txNew1);

    CMutableTransaction txNew2 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew2.vin.resize(1);
    txNew2.vin[0].prevout.SetNull();
    txNew2.vin[0].scriptSig = (CScript() << 2 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew2.vout.resize(1);
    txNew2.vout[0].scriptPubKey = scriptPubKey;
    txNew2.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew2.nExpiryHeight = 0;
    txNew2.vout[0].nValue = 25000; // Add 25000 ARRRtosis to the coinbase output for testing
    CTransaction coinbaseTx2(txNew2);

    // Create a CCoinsViewCache to hold the coins
    CCoinsView baseView;
    CCoinsViewCache view(&baseView);

    // Create wallets for Sapling and Ironwood
    SaplingWallet saplingWallet;
    IronwoodWallet ironwoodWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    IronwoodMerkleFrontier ironwoodFrontier;

    // Initialize the Sapling and Ironwood wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx1, view, 1);
    UpdateCoins(coinbaseTx2, view, 2);
    
    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto expsk_from = sk_from.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk_from;
    expsk_from.DeriveFVK(&fvk_from);

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    libzcash::diversifier_t change_d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress change_pk;
    ASSERT_TRUE(ivk.DeriveAddress(&change_pk, change_d));

    // depth/parentFVKTag/childIndex are plain POD fields with no default member
    // initializer; leaving them uninitialized produces a garbage childIndex that
    // the Rust deserializer now rejects ("Unsupported child index in encoding").
    // Only .expsk matters for ConvertRawSaplingSpend's signing authority, so
    // zero the rest to a valid (masterlike) shape.
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    // No change address and no Sapling spends
    {
        auto builder = TransactionBuilder(consensusParams, 3, &keystore);
        builder.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 25000);
        auto maybe_tx = builder.Build();
        EXPECT_FALSE(maybe_tx.IsTx());
    }

    // No Fee Shileding Transaction
    // 0.0005 t-ARRR in, 0.0005 z-ARRR out
    auto builder1 = TransactionBuilder(consensusParams, 3, &keystore);
    builder1.InitializeSapling(uint256());
    builder1.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 50000);
    builder1.AddSaplingOutputRaw(pk, 50000, {});
    builder1.SetFee(0);
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    EXPECT_TRUE(maybe_tx1.IsTx());
    auto tx1 = maybe_tx1.GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.vjoinsplit.size(), 0);
    EXPECT_EQ(tx1.GetSaplingSpendsCount(), 0);
    // MIN_SHIELDED_OUTPUTS pads a bundle with >=1 real spend/output to at least 2 outputs.
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 2);
    EXPECT_EQ(tx1.GetValueBalanceSapling(), -50000);

    CValidationState state;

    // Check that the transaction is valid without proof verification
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx1, state));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Check that the transaction is valid in the current context
    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx1, 1, false);
    EXPECT_TRUE(results0.validationPassed);

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx1;
    std::vector<std::vector<const CTransaction*>> vvtx1;
    vtx1.push_back(&tx1);
    vvtx1.push_back(vtx1);

    //Shielded Bundle Contextual Check
    CheckTransationResults results1 = ContextualCheckTransactionShieldedBundles(vvtx1[0], &view, consensusId, false, 0);
    EXPECT_TRUE(results1.validationPassed);

    // Update Coins
    UpdateCoins(tx1, view, 1);

    // Update the wallets with the new transaction
    if (tx1.GetSaplingBundle().IsPresent()) {
        saplingWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vOutputs = tx1.GetSaplingOutputs();
        for (int j = 0; j < vOutputs.size(); j++) {
            saplingWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vOutputs[j], true);
        }
    }
    if (tx1.GetIronwoodBundle().IsPresent()) {
        ironwoodWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetIronwoodBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            ironwoodWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created. The Sapling builder pads to a
    // privacy-floor minimum of 2 outputs (MIN_SHIELDED_OUTPUTS) and shuffles real and
    // dummy outputs together, so the real one isn't necessarily at index 0 - find it by
    // trial decryption instead.
    auto vOutputs = tx1.GetSaplingOutputs();
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

    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), realOutputIndex, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));


    // Change to the same address as the first Sapling spend
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        ASSERT_EQ(maybe_tx2.IsTx(), true);
        auto tx2 = maybe_tx2.GetTxOrThrow();

        EXPECT_EQ(tx2.vin.size(), 1);
        EXPECT_EQ(tx2.vout.size(), 0);
        EXPECT_EQ(tx2.vjoinsplit.size(), 0);
        EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
        EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
        EXPECT_EQ(tx2.GetValueBalanceSapling(), -40000);
    }

    // Change to transparent change address
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 t-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2, &keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        // Set the change address to the t-address
        builder2.SendChangeTo(taddr);

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        ASSERT_EQ(maybe_tx2.IsTx(), true);
        auto tx2 = maybe_tx2.GetTxOrThrow();

        EXPECT_EQ(tx2.vin.size(), 1);
        EXPECT_EQ(tx2.vout.size(), 1);
        EXPECT_EQ(tx2.vjoinsplit.size(), 0);
        EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
        EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
        EXPECT_EQ(tx2.GetValueBalanceSapling(), -10000);
    }

    // Change to sapling change address
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2, &keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        // Set the change address to the t-address
        builder2.SendChangeTo(change_pk,fvk_from.ovk);

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        ASSERT_EQ(maybe_tx2.IsTx(), true);
        auto tx2 = maybe_tx2.GetTxOrThrow();

        EXPECT_EQ(tx2.vin.size(), 1);
        EXPECT_EQ(tx2.vout.size(), 0);
        EXPECT_EQ(tx2.vjoinsplit.size(), 0);
        EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
        EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
        EXPECT_EQ(tx2.GetValueBalanceSapling(), -40000);
    }
}

TEST(TransactionBuilder, SetFee)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();
    auto consensusId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;

    CBasicKeyStore keystore;
    CKey tsk = DecodeSecret(tSecretRegtest);
    EXPECT_TRUE(tsk.IsValid());
    keystore.AddKey(tsk);
    auto tkeyid = tsk.GetPubKey().GetID();
    auto scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());
    CTxDestination taddr = tkeyid;

    // Create coinbase txs
    CMutableTransaction txNew1 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew1.vin.resize(1);
    txNew1.vin[0].prevout.SetNull();
    txNew1.vin[0].scriptSig = (CScript() << 1 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew1.vout.resize(1);
    txNew1.vout[0].scriptPubKey = scriptPubKey;
    txNew1.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew1.nExpiryHeight = 0;
    txNew1.vout[0].nValue = 50000; // Add 50000 ARRRtoshis to the coinbase output for testing
    CTransaction coinbaseTx1(txNew1);

    CMutableTransaction txNew2 = CreateNewContextualCMutableTransaction(consensusParams, 1);
    txNew2.vin.resize(1);
    txNew2.vin[0].prevout.SetNull();
    txNew2.vin[0].scriptSig = (CScript() << 2 << CScriptNum(1)) + COINBASE_FLAGS;
    txNew2.vout.resize(1);
    txNew2.vout[0].scriptPubKey = scriptPubKey;
    txNew2.vout[0].nValue = GetBlockSubsidy(1,consensusParams);
    txNew2.nExpiryHeight = 0;
    txNew2.vout[0].nValue = 25000; // Add 25000 ARRRtosis to the coinbase output for testing
    CTransaction coinbaseTx2(txNew2);

    // Create a CCoinsViewCache to hold the coins
    CCoinsView baseView;
    CCoinsViewCache view(&baseView);

    // Create wallets for Sapling and Ironwood
    SaplingWallet saplingWallet;
    IronwoodWallet ironwoodWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    IronwoodMerkleFrontier ironwoodFrontier;

    // Initialize the Sapling and Ironwood wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx1, view, 1);
    UpdateCoins(coinbaseTx2, view, 2);
    
    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto expsk_from = sk_from.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk_from;
    expsk_from.DeriveFVK(&fvk_from);

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    libzcash::diversifier_t change_d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    SaplingPaymentAddress change_pk;
    ASSERT_TRUE(ivk.DeriveAddress(&change_pk, change_d));

    // depth/parentFVKTag/childIndex are plain POD fields with no default member
    // initializer; leaving them uninitialized produces a garbage childIndex that
    // the Rust deserializer now rejects ("Unsupported child index in encoding").
    // Only .expsk matters for ConvertRawSaplingSpend's signing authority, so
    // zero the rest to a valid (masterlike) shape.
    libzcash::SaplingExtendedSpendingKey extsk = {};
    extsk.expsk = expsk;

    // No Fee Shileding Transaction
    // 0.0005 t-ARRR in, 0.0005 z-ARRR out
    auto builder1 = TransactionBuilder(consensusParams, 3, &keystore);
    builder1.InitializeSapling(uint256());
    builder1.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 50000);
    builder1.AddSaplingOutputRaw(pk, 50000, {});
    builder1.SetFee(0);
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    EXPECT_TRUE(maybe_tx1.IsTx());
    auto tx1 = maybe_tx1.GetTxOrThrow();

    EXPECT_EQ(tx1.vin.size(), 1);
    EXPECT_EQ(tx1.vout.size(), 0);
    EXPECT_EQ(tx1.vjoinsplit.size(), 0);
    EXPECT_EQ(tx1.GetSaplingSpendsCount(), 0);
    // MIN_SHIELDED_OUTPUTS pads a bundle with >=1 real spend/output to at least 2 outputs.
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 2);
    EXPECT_EQ(tx1.GetValueBalanceSapling(), -50000);
    
    CValidationState state;

    // Check that the transaction is valid without proof verification
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx1, state));
    EXPECT_EQ(state.GetRejectReason(), "");

    // Check that the transaction is valid in the current context
    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx1, 1, false);
    EXPECT_TRUE(results0.validationPassed);

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx1;
    std::vector<std::vector<const CTransaction*>> vvtx1;
    vtx1.push_back(&tx1);
    vvtx1.push_back(vtx1);

    //Shielded Bundle Contextual Check
    CheckTransationResults results1 = ContextualCheckTransactionShieldedBundles(vvtx1[0], &view, consensusId, false, 0);
    EXPECT_TRUE(results1.validationPassed);

    // Update Coins
    UpdateCoins(tx1, view, 1);


    // Update the wallets with the new transaction
    if (tx1.GetSaplingBundle().IsPresent()) {
        saplingWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vOutputs = tx1.GetSaplingOutputs();
        for (int j = 0; j < vOutputs.size(); j++) {
            saplingWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vOutputs[j], true);
        }
    }
    if (tx1.GetIronwoodBundle().IsPresent()) {
        ironwoodWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetIronwoodBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            ironwoodWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created. The Sapling builder pads to a
    // privacy-floor minimum of 2 outputs (MIN_SHIELDED_OUTPUTS) and shuffles real and
    // dummy outputs together, so the real one isn't necessarily at index 0 - find it by
    // trial decryption instead.
    auto vOutputs = tx1.GetSaplingOutputs();
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

    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), realOutputIndex, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Default fee
    // Change to the same address as the first Sapling spend
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        ASSERT_EQ(maybe_tx2.IsTx(), true);
        auto tx2 = maybe_tx2.GetTxOrThrow();

        EXPECT_EQ(tx2.vin.size(), 1);
        EXPECT_EQ(tx2.vout.size(), 0);
        EXPECT_EQ(tx2.vjoinsplit.size(), 0);
        EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
        EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
        EXPECT_EQ(tx2.GetValueBalanceSapling(), -40000);
    }

    // Configured fee - Transacion in balance
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0002 t-ARRR fee, 0.00002 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        builder2.SetFee(20000); // Set fee to 0.0002 ARRR

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        ASSERT_EQ(maybe_tx2.IsTx(), true);
        auto tx2 = maybe_tx2.GetTxOrThrow();

        EXPECT_EQ(tx2.vin.size(), 1);
        EXPECT_EQ(tx2.vout.size(), 0);
        EXPECT_EQ(tx2.vjoinsplit.size(), 0);
        EXPECT_EQ(tx2.GetSaplingSpendsCount(), 1);
        EXPECT_EQ(tx2.GetSaplingOutputsCount(), 2);
        EXPECT_EQ(tx2.GetValueBalanceSapling(), -30000);
    }

    // Configured fee - Transacion out of balance
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0005 t-ARRR fee
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.InitializeSapling(anchor);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), realOutputIndex),pk , note.value(), note.rcm(), saplingMerklePath, anchor);
        builder2.ConvertRawSaplingSpend(extsk);

        // Add a Sapling output
        builder2.AddSaplingOutputRaw(pk, 60000, {});
        builder2.ConvertRawSaplingOutput(fvk_from.ovk);

        // Set fee to 0.0006 ARRR
        builder2.SetFee(60000); 

        // Build the transaction
        auto maybe_tx2 = builder2.Build();
        EXPECT_FALSE(maybe_tx2.IsTx());

    }
}

TEST(TransactionBuilder, CheckSaplingTxVersion)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    libzcash::SaplingFullViewingKey fvk;
    expsk.DeriveFVK(&fvk);
    SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    libzcash::SaplingPaymentAddress pk;
    ASSERT_TRUE(ivk.DeriveAddress(&pk, d));

    // Cannot add Sapling outputs to a non-Sapling transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    // EXPECT_THROW fails the test outright if AddSaplingOutputRaw doesn't
    // throw at all, unlike a bare try/catch (which would silently pass with
    // zero assertions executed if a future refactor changed the guard from
    // throw to a bool return).
    EXPECT_THROW(builder.AddSaplingOutputRaw(pk, 12345, {}), std::runtime_error);
    try {
        builder.AddSaplingOutputRaw(pk, 12345, {});
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Sapling output to pre-Sapling transaction"));
    }

    // Cannot add Sapling spends to a non-Sapling transaction
    libzcash::MerklePath saplingMerklePath;
    EXPECT_THROW(builder.AddSaplingSpendRaw(SaplingOutPoint(), pk, 10000, uint256(), saplingMerklePath, uint256()), std::runtime_error);
    try {
        builder.AddSaplingSpendRaw(SaplingOutPoint(), pk, 10000, uint256(),saplingMerklePath, uint256());
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction"));
    }
}

// Ironwood counterpart to CheckSaplingTxVersion above: confirms
// AddIronwoodOutputRaw/AddIronwoodSpendRaw reject a transaction built at a
// height where Sapling is active but Ironwood is not, mirroring the guard
// TransactionBuilder.IronwoodShieldAndSpend only ever exercises on the happy
// (Ironwood-active) path.
TEST(TransactionBuilder, CheckIronwoodTxVersion)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    struct UpgradeReverter {
        ~UpgradeReverter() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    } upgradeReverter;
    auto consensusParams = Params().GetConsensus();

    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    auto m = libzcash::IronwoodExtendedSpendingKeyPirate::Master(seed);
    auto skOpt = m.Derive(Params().BIP44CoinType(), 0);
    ASSERT_TRUE(skOpt.has_value());
    libzcash::IronwoodPaymentAddress addr;
    ASSERT_TRUE(skOpt.value().sk.DeriveDefaultAddress(&addr));

    // Cannot add Ironwood outputs to a pre-Ironwood transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    EXPECT_THROW(builder.AddIronwoodOutputRaw(addr, 12345, {}), std::runtime_error);
    try {
        builder.AddIronwoodOutputRaw(addr, 12345, {});
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Ironwood Output before Ironwood activation"));
    }

    // Cannot add Ironwood spends to a pre-Ironwood transaction
    libzcash::MerklePath ironwoodMerklePath;
    EXPECT_THROW(builder.AddIronwoodSpendRaw(IronwoodOutPoint(), addr, 10000, uint256(), uint256(), ironwoodMerklePath, uint256()), std::runtime_error);
    try {
        builder.AddIronwoodSpendRaw(IronwoodOutPoint(), addr, 10000, uint256(), uint256(), ironwoodMerklePath, uint256());
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Ironwood Spend before Ironwood activation"));
    }
}
