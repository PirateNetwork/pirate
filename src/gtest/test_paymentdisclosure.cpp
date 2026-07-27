// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key_io.h"
#include "main.h"
#include "pubkey.h"
#include "random.h"
#include "transaction_builder.h"
#include "txmempool.h"
#include "wallet/wallet.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

#include "paymentdisclosure.h"
#include "gtest/gtestutils.h"
#include <rust/bridge.h>

// Payment disclosure tests: generating and independently verifying a
// disclosure that proves a shielded output's value/address without revealing
// the spending key. Covers both pools in parallel - SaplingGenerateAndVerify
// and IronwoodGenerateAndVerify build near-identical scenarios for their
// respective pool, and UnifiedVerifyDispatchesByType checks the shared
// VerifyPaymentDisclosure entry point dispatches by disclosure type. Good
// reference for how a dual-pool test pair should be structured.

using namespace libzcash;

static const std::string tSecretRegtest = "UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1";

// Inserts tx directly into the mempool, bypassing full policy/consensus
// checks: these tests exercise GenerateSaplingDisclosure/VerifySaplingDisclosure
// (and their Ironwood counterparts), which only need GetTransaction() to find
// the transaction via mempool.lookup(); they are not testing mempool-acceptance
// policy itself.
static void InsertIntoMempool(const CTransaction& tx, uint32_t consensusBranchId, bool spendsCoinbase)
{
    CTxMemPoolEntry entry(tx, /*fee=*/1000, GetTime(), /*priority=*/0.0, /*height=*/1,
                           /*poolHasNoInputsOf=*/true, spendsCoinbase, consensusBranchId);
    mempool.addUnchecked(tx.GetHash(), entry);
}

TEST(paymentdisclosure, SaplingGenerateAndVerify)
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

    // Transparent key that funds the shielding transaction.
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

    // Wallet holding the sending (OVK-bearing) Sapling key. This is the wallet
    // GenerateSaplingDisclosure searches to find the OVK that encrypted the output.
    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto fromAddr = fromWallet.GenerateNewSaplingZKey();
    libzcash::SaplingExtendedSpendingKey fromExtsk;
    ASSERT_TRUE(fromWallet.GetSaplingExtendedSpendingKey(fromAddr, fromExtsk));

    // Destination address, not tracked by fromWallet. The 96-byte expsk (ask/nsk/ovk
    // only) doesn't carry dk, so it can't derive the ZIP 32 default diversifier (needs
    // the dk-keyed FF1 permutation) - treat the random key's seed as a ZIP 32 master
    // seed instead (bip39Enabled=false so it's used directly) to get a real dk.
    auto recipientSk = SaplingSpendingKey::random();
    RawHDSeed recipientRawSeed(recipientSk.begin(), recipientSk.end());
    auto recipientExtsk = libzcash::SaplingExtendedSpendingKey::Master(HDSeed(recipientRawSeed), false);
    SaplingPaymentAddress recipientAddr = recipientExtsk.DefaultAddress();

    auto builder = TransactionBuilder(consensusParams, 1, &keystore);
    builder.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder.InitializeSapling(uint256());
    ASSERT_TRUE(builder.AddSaplingOutputRaw(recipientAddr, 40000, {}));
    ASSERT_TRUE(builder.ConvertRawSaplingOutput(fromExtsk.expsk.ovk));
    auto maybeTx = builder.Build();
    ASSERT_TRUE(maybeTx.IsTx());
    auto tx = maybeTx.GetTxOrThrow();

    // The Sapling builder pads to a privacy-floor minimum of 2 outputs
    // (MIN_SHIELDED_OUTPUTS) and shuffles real and dummy outputs together, so the
    // real one isn't necessarily at index 0 - find it via trial decryption.
    ASSERT_EQ(tx.GetSaplingOutputsCount(), 2);
    libzcash::SaplingIncomingViewingKey recipientIvk;
    ASSERT_TRUE(recipientExtsk.ToXFVK().fvk.DeriveIVK(&recipientIvk));
    auto vOutputs = tx.GetSaplingOutputs();
    int realOutputIndex = -1;
    for (int j = 0; j < (int)vOutputs.size(); j++) {
        if (libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], recipientIvk)) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);

    CheckTransationResults results = ContextualCheckTransactionSingleThreaded(tx, 1, false);
    EXPECT_TRUE(results.validationPassed);

    InsertIntoMempool(tx, consensusId, /*spendsCoinbase=*/false);
    struct MempoolReverter {
        const CTransaction& tx;
        ~MempoolReverter() {
            std::list<CTransaction> removed;
            mempool.remove(tx, removed, true);
        }
    } mempoolReverter{tx};

    // Generate the disclosure from the sending wallet.
    std::string disclosure = GenerateSaplingDisclosure(&fromWallet, tx.GetHash(), realOutputIndex);
    ASSERT_FALSE(disclosure.empty());

    // Verify it independently of the wallet.
    SaplingDisclosureVerificationResult result = VerifySaplingDisclosure(disclosure);
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.txid, tx.GetHash());
    EXPECT_EQ(result.outputIndex, realOutputIndex);
    EXPECT_EQ(result.value, 40000);
    EXPECT_EQ(result.address, EncodePaymentAddress(recipientAddr));

    // A garbage disclosure string should fail cleanly, not throw.
    SaplingDisclosureVerificationResult badResult = VerifySaplingDisclosure("not-a-real-disclosure");
    EXPECT_FALSE(badResult.success);
    EXPECT_FALSE(badResult.error.empty());
}

TEST(paymentdisclosure, IronwoodGenerateAndVerify)
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

    // Wallet holding the sending (OVK-bearing) Ironwood key.
    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto fromAddr = fromWallet.GenerateNewIronwoodZKey();
    libzcash::IronwoodExtendedSpendingKeyPirate fromExtsk;
    ASSERT_TRUE(fromWallet.GetIronwoodExtendedSpendingKey(fromAddr, fromExtsk));
    IronwoodFullViewingKey fromFvk;
    ASSERT_TRUE(fromExtsk.sk.DeriveFVK(&fromFvk));
    IronwoodOutgoingViewingKey fromOvk;
    ASSERT_TRUE(fromFvk.DeriveOVK(&fromOvk));

    // Destination address, derived from a different seed, not tracked by fromWallet.
    CKeyingMaterial recipientRawSeed(32, 7);
    HDSeed recipientSeed(recipientRawSeed);
    auto recipientExtsk = IronwoodExtendedSpendingKeyPirate::Master(recipientSeed);
    IronwoodPaymentAddress recipientAddr;
    ASSERT_TRUE(recipientExtsk.sk.DeriveDefaultAddress(&recipientAddr));

    auto builder = TransactionBuilder(consensusParams, 1, &keystore);
    builder.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder.InitializeIronwood(/*spendsEnabled=*/false, /*outputsEnabled=*/true, uint256());
    ASSERT_TRUE(builder.AddIronwoodOutputRaw(recipientAddr, 40000, {}));
    ASSERT_TRUE(builder.ConvertRawIronwoodOutput(fromOvk.ovk));
    auto maybeTx = builder.Build();
    ASSERT_TRUE(maybeTx.IsTx());
    auto tx = maybeTx.GetTxOrThrow();

    // The vendored orchard crate (which backs Ironwood) pads a bundle to at least
    // 2 actions whenever any real action exists, mirroring Sapling's
    // MIN_SHIELDED_OUTPUTS rule. Real vs. dummy actions are shuffled, so find the
    // real one via trial decryption rather than assuming index 0.
    const auto& bundleDetails = tx.GetIronwoodBundle().GetDetails();
    ASSERT_EQ(bundleDetails.num_actions(), 2);
    auto actions = bundleDetails.actions();
    uint256_t fromOvkBytes;
    std::copy(fromOvk.ovk.begin(), fromOvk.ovk.end(), fromOvkBytes.begin());
    int realActionIndex = -1;
    for (size_t i = 0; i < actions.size(); i++) {
        uint256_t ock;
        if (!ironwood::derive_ironwood_ock(actions[i], fromOvkBytes, ock)) {
            continue;
        }
        uint64_t testValue;
        std::array<uint8_t, 43> testAddress;
        std::array<uint8_t, 512> testMemo;
        uint256_t testRho, testRseed;
        if (ironwood::try_ironwood_decrypt_action_ock(
                actions[i], ock, testValue, testAddress, testMemo, testRho, testRseed)) {
            realActionIndex = (int)i;
            break;
        }
    }
    ASSERT_NE(realActionIndex, -1);

    InsertIntoMempool(tx, consensusId, /*spendsCoinbase=*/false);
    struct MempoolReverter {
        const CTransaction& tx;
        ~MempoolReverter() {
            std::list<CTransaction> removed;
            mempool.remove(tx, removed, true);
        }
    } mempoolReverter{tx};

    std::string disclosure = GenerateIronwoodDisclosure(&fromWallet, tx.GetHash(), realActionIndex);
    ASSERT_FALSE(disclosure.empty());

    IronwoodDisclosureVerificationResult result = VerifyIronwoodDisclosure(disclosure);
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.txid, tx.GetHash());
    EXPECT_EQ(result.actionIndex, realActionIndex);
    EXPECT_EQ(result.value, 40000);
    EXPECT_EQ(result.address, EncodePaymentAddress(recipientAddr));

    IronwoodDisclosureVerificationResult badResult = VerifyIronwoodDisclosure("not-a-real-disclosure");
    EXPECT_FALSE(badResult.success);
    EXPECT_FALSE(badResult.error.empty());
}

TEST(paymentdisclosure, UnifiedVerifyDispatchesByType)
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

    CWallet fromWallet;
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    fromWallet.LoadHDSeed(seed);
    auto fromAddr = fromWallet.GenerateNewSaplingZKey();
    libzcash::SaplingExtendedSpendingKey fromExtsk;
    ASSERT_TRUE(fromWallet.GetSaplingExtendedSpendingKey(fromAddr, fromExtsk));

    auto recipientSk = SaplingSpendingKey::random();
    RawHDSeed recipientRawSeed(recipientSk.begin(), recipientSk.end());
    auto recipientExtsk = libzcash::SaplingExtendedSpendingKey::Master(HDSeed(recipientRawSeed), false);
    SaplingPaymentAddress recipientAddr = recipientExtsk.DefaultAddress();

    auto builder = TransactionBuilder(consensusParams, 1, &keystore);
    builder.AddTransparentInput(COutPoint(coinbaseTx.GetHash(), 0), scriptPubKey, 50000);
    builder.InitializeSapling(uint256());
    ASSERT_TRUE(builder.AddSaplingOutputRaw(recipientAddr, 40000, {}));
    ASSERT_TRUE(builder.ConvertRawSaplingOutput(fromExtsk.expsk.ovk));
    auto maybeTx = builder.Build();
    ASSERT_TRUE(maybeTx.IsTx());
    auto tx = maybeTx.GetTxOrThrow();

    // The Sapling builder pads to a privacy-floor minimum of 2 outputs
    // (MIN_SHIELDED_OUTPUTS) and shuffles real and dummy outputs together, so the
    // real one isn't necessarily at index 0 - find it via trial decryption.
    libzcash::SaplingIncomingViewingKey recipientIvk;
    ASSERT_TRUE(recipientExtsk.ToXFVK().fvk.DeriveIVK(&recipientIvk));
    auto vOutputs = tx.GetSaplingOutputs();
    int realOutputIndex = -1;
    for (int j = 0; j < (int)vOutputs.size(); j++) {
        if (libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[j], recipientIvk)) {
            realOutputIndex = j;
            break;
        }
    }
    ASSERT_NE(realOutputIndex, -1);

    InsertIntoMempool(tx, consensusId, /*spendsCoinbase=*/false);
    struct MempoolReverter {
        const CTransaction& tx;
        ~MempoolReverter() {
            std::list<CTransaction> removed;
            mempool.remove(tx, removed, true);
        }
    } mempoolReverter{tx};

    std::string disclosure = GenerateSaplingDisclosure(&fromWallet, tx.GetHash(), realOutputIndex);
    ASSERT_FALSE(disclosure.empty());

    UnifiedDisclosureVerificationResult result = VerifyPaymentDisclosure(disclosure);
    EXPECT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.disclosureType, "Sapling");
    EXPECT_EQ(result.value, 40000);

    UnifiedDisclosureVerificationResult badResult = VerifyPaymentDisclosure("not-a-real-disclosure");
    EXPECT_FALSE(badResult.success);
    EXPECT_FALSE(badResult.error.empty());
}
