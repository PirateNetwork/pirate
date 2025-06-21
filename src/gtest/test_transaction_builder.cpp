#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/consensus.h"
#include "consensus/validation.h"
#include "key_io.h"
#include "main.h"
#include "pubkey.h"
#include "transaction_builder.h"
#include "zcash/Address.hpp"
#include "gtest/gtestutils.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

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

    // Create wallets for Sapling and Orchard
    SaplingWallet saplingWallet;
    OrchardWallet orchardWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    OrchardMerkleFrontier orchardFrontier;

    // Initialize the Sapling and Orchard wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    orchardWallet.InitNoteCommitmentTree(orchardFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx, view, 1);

    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto fvk_from = sk_from.full_viewing_key();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pk = *ivk.address(d);

    libzcash::SaplingExtendedSpendingKey extsk;
    extsk.expsk = expsk;

    // Create a shielding transaction from transparent to Sapling
    // 0.0005 t-ARRR in, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder1 = TransactionBuilder(consensusParams, 1, &keystore);
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
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 1);
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

    // Update the wallets with the new transaction
    if (tx1.GetSaplingBundle().IsPresent()) {
        saplingWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vOutputs = tx1.GetSaplingOutputs();
        for (int j = 0; j < vOutputs.size(); j++) {
            saplingWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vOutputs[j], true);
        }
    }
    if (tx1.GetOrchardBundle().IsPresent()) {
        orchardWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetOrchardBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            orchardWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created
    auto vOutputs = tx1.GetSaplingOutputs();
    auto encCiphertext = vOutputs[0].enc_ciphertext();
    auto outCiphertext = vOutputs[0].out_ciphertext();
    auto ephemeralKey = uint256::FromRawBytes(vOutputs[0].ephemeral_key());
    auto cmu = uint256::FromRawBytes(vOutputs[0].cmu());
    auto cv = uint256::FromRawBytes(vOutputs[0].cv());

    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(consensusParams,2,encCiphertext, ivk, ephemeralKey, cmu);
    ASSERT_EQ(static_cast<bool>(maybe_pt), true);
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), 0, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Create a Sapling-only transaction
    // 0.0004 z-ARRR in, 0.00025 z-ARRR out, 0.0001 t-ARRR fee, 0.00005 z-ARRR change
    auto builder2 = TransactionBuilder(consensusParams, 2);
    EXPECT_TRUE(builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor));  

    // Check that trying to add a different anchor fails
    EXPECT_FALSE(builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, uint256()));

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

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
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
    auto fvk_from = sk_from.full_viewing_key();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pk = *ivk.address(d);

    libzcash::SaplingExtendedSpendingKey extsk;
    extsk.expsk = expsk;

    // Create a shielding transaction from transparent to Sapling
    // Fail if there is only a Sapling output
    // 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder1 = TransactionBuilder(consensusParams, 3, &keystore);
    builder1.AddSaplingOutputRaw(pk, 40000, {});
    builder1.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx1 = builder1.Build();
    EXPECT_FALSE(maybe_tx1.IsTx());

    // Create a shielding transaction from transparent to Sapling
    // Fail if Sapling output > Transparent Input
    // 0.00025 z-ARRR, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder2 = TransactionBuilder(consensusParams, 3, &keystore);
    builder2.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 25000);
    builder2.AddSaplingOutputRaw(pk, 40000, {});
    builder2.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx2 = builder2.Build();
    EXPECT_FALSE(maybe_tx2.IsTx());

    // Create a shielding transaction from transparent to Sapling
    // Succeed with valid inputs and outputs
    // 0.0005 z-ARRR, 0.0004 z-ARRR out, 0.0001 t-ARRR fee
    auto builder3 = TransactionBuilder(consensusParams, 3, &keystore);
    builder3.AddTransparentInput(COutPoint(coinbaseTx1.GetHash(),0), scriptPubKey, 25000);
    builder3.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 25000);
    builder3.AddSaplingOutputRaw(pk, 40000, {});
    builder3.ConvertRawSaplingOutput(fvk_from.ovk);
    auto maybe_tx3 = builder3.Build();
    EXPECT_TRUE(maybe_tx3.IsTx());

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, ChangeOutput)
{

    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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

    // Create wallets for Sapling and Orchard
    SaplingWallet saplingWallet;
    OrchardWallet orchardWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    OrchardMerkleFrontier orchardFrontier;

    // Initialize the Sapling and Orchard wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    orchardWallet.InitNoteCommitmentTree(orchardFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx1, view, 1);
    UpdateCoins(coinbaseTx2, view, 2);
    
    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto fvk_from = sk_from.full_viewing_key();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pk = *ivk.address(d);

    libzcash::diversifier_t change_d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto change_pk = *ivk.address(change_d);

    libzcash::SaplingExtendedSpendingKey extsk;
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
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 1);
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
    if (tx1.GetOrchardBundle().IsPresent()) {
        orchardWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetOrchardBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            orchardWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created
    auto vOutputs = tx1.GetSaplingOutputs();
    auto encCiphertext = vOutputs[0].enc_ciphertext();
    auto outCiphertext = vOutputs[0].out_ciphertext();
    auto ephemeralKey = uint256::FromRawBytes(vOutputs[0].ephemeral_key());
    auto cmu = uint256::FromRawBytes(vOutputs[0].cmu());
    auto cv = uint256::FromRawBytes(vOutputs[0].cv());

    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(consensusParams,2,encCiphertext, ivk, ephemeralKey, cmu);
    ASSERT_EQ(static_cast<bool>(maybe_pt), true);
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), 0, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));


    // Change to the same address as the first Sapling spend
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, SetFee)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
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

    // Create wallets for Sapling and Orchard
    SaplingWallet saplingWallet;
    OrchardWallet orchardWallet;

    // Create Frontier Trees
    SaplingMerkleFrontier saplingFrontier;
    OrchardMerkleFrontier orchardFrontier;

    // Initialize the Sapling and Orchard wallets with the frontiers
    saplingWallet.InitNoteCommitmentTree(saplingFrontier);
    orchardWallet.InitNoteCommitmentTree(orchardFrontier);

    // Initialize the view with the coinbase transaction
    UpdateCoins(coinbaseTx1, view, 1);
    UpdateCoins(coinbaseTx2, view, 2);
    
    // Create a Sapling spending key and full viewing key
    auto sk_from = libzcash::SaplingSpendingKey::random();
    auto fvk_from = sk_from.full_viewing_key();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto fvk = sk.full_viewing_key();
    auto ivk = fvk.in_viewing_key();
    libzcash::diversifier_t d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto pk = *ivk.address(d);

    libzcash::diversifier_t change_d = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    auto change_pk = *ivk.address(change_d);

    libzcash::SaplingExtendedSpendingKey extsk;
    extsk.expsk = expsk;

    // No Fee Shileding Transaction
    // 0.0005 t-ARRR in, 0.0005 z-ARRR out
    auto builder1 = TransactionBuilder(consensusParams, 3, &keystore);
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
    EXPECT_EQ(tx1.GetSaplingOutputsCount(), 1);
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
    if (tx1.GetOrchardBundle().IsPresent()) {
        orchardWallet.CreateEmptyPositionsForTxid(2, tx1.GetHash());
        auto vActions = tx1.GetOrchardBundle().GetDetails().actions();
        for (int j = 0; j < vActions.size(); j++) {
            orchardWallet.AppendNoteCommitment(2, tx1.GetHash(), 0, j, &vActions[j], true);
        }
    }

    // Prepare to spend the note that was just created
    auto vOutputs = tx1.GetSaplingOutputs();
    auto encCiphertext = vOutputs[0].enc_ciphertext();
    auto outCiphertext = vOutputs[0].out_ciphertext();
    auto ephemeralKey = uint256::FromRawBytes(vOutputs[0].ephemeral_key());
    auto cmu = uint256::FromRawBytes(vOutputs[0].cmu());
    auto cv = uint256::FromRawBytes(vOutputs[0].cv());

    auto maybe_pt = libzcash::SaplingNotePlaintext::decrypt(consensusParams,2,encCiphertext, ivk, ephemeralKey, cmu);
    ASSERT_EQ(static_cast<bool>(maybe_pt), true);
    auto maybe_note = maybe_pt.value().note(ivk);
    ASSERT_EQ(static_cast<bool>(maybe_note), true);
    auto note = maybe_note.value();

    // Get Merkle path for the note
    libzcash::MerklePath saplingMerklePath;
    EXPECT_TRUE(saplingWallet.GetMerklePathOfNote(tx1.GetHash(), 0, saplingMerklePath));

    // Get the Merkle root anchor for the note
    uint256 anchor;
    EXPECT_TRUE(saplingWallet.GetPathRootWithCMU(saplingMerklePath, cmu, anchor));

    // Default fee
    // Change to the same address as the first Sapling spend
    {
        // Create a Sapling-only transaction
        // 0.0005 t-ARRR in, 0.0005 z-ARRR in, 0.0006 z-ARRR out, 0.0001 t-ARRR fee, 0.00003 z-ARRR change
        auto builder2 = TransactionBuilder(consensusParams, 2,&keystore);
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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
        builder2.AddTransparentInput(COutPoint(coinbaseTx2.GetHash(),0), scriptPubKey, 50000);
        builder2.AddSaplingSpendRaw(SaplingOutPoint(tx1.GetHash(), 0),pk , note.value(), note.rcm(), saplingMerklePath, anchor);  
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

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(TransactionBuilder, CheckSaplingTxVersion)
{
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    auto consensusParams = Params().GetConsensus();

    auto sk = libzcash::SaplingSpendingKey::random();
    auto expsk = sk.expanded_spending_key();
    auto pk = sk.default_address();

    // Cannot add Sapling outputs to a non-Sapling transaction
    auto builder = TransactionBuilder(consensusParams, 1);
    try {
        builder.AddSaplingOutputRaw(pk, 12345, {});
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Sapling output to pre-Sapling transaction"));
    } catch(...) {
        FAIL() << "Expected std::runtime_error";
    }

    // Cannot add Sapling spends to a non-Sapling transaction
    libzcash::MerklePath saplingMerklePath;
    try {
        builder.AddSaplingSpendRaw(SaplingOutPoint(), pk, 10000, uint256(),saplingMerklePath, uint256());
    } catch (std::runtime_error const & err) {
        EXPECT_EQ(err.what(), std::string("TransactionBuilder cannot add Sapling spend to pre-Sapling transaction"));
    } catch(...) {
        FAIL() << "Expected std::runtime_error";
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}
