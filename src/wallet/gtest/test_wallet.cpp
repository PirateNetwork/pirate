// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include "base58.h"
#include "chainparams.h"
#include "key_io.h"
#include "main.h"
#include "primitives/block.h"
#include "random.h"
#include "transaction_builder.h"
#include "utiltest.h"
#include "gtest/gtestutils.h"
#include "wallet/wallet.h"
#include "zcash/JoinSplit.hpp"
#include "zcash/Note.hpp"
#include "zcash/NoteEncryption.hpp"

#include <boost/filesystem.hpp>

// Primary CWallet-internals test file: exercises note tracking, note
// selection, and balance accounting (e.g. GetFilteredNotes and friends)
// for both the Sprout and Sapling shielded pools.

using ::testing::Return;

extern ZCJoinSplit* params;

ACTION(ThrowLogicError) {
    throw std::logic_error("Boom");
}

static const std::string tSecretRegtest = "UuRoAgHmjHZqexxVAPjzW8N6hr3o7aETZqCZon2m8EYAmjmdTcj1";



// template void CWallet::SetBestChainINTERNAL<MockWalletDB>(
//         MockWalletDB& walletdb, const CBlockLocator& loc, const int& height);




// Wallet Used for all tests
//TestWallet* pTestWallet = nullptr;

// Create a CCoinsViewCache to hold the coins
CCoinsView baseView;
CCoinsViewCache view(&baseView);

//Coinbase address for regtest
CScript scriptPubKey; 

//Sapling Address for regtest
libzcash::SaplingPaymentAddress saplingAddress;

//Ironwood Address for regtest
libzcash::IronwoodPaymentAddress ironwoodAddress;

//Create a Sapling Wallet
SaplingWallet saplingWallet;

// Create a Ironwood Wallet
IronwoodWallet ironwoodWallet;

CTransaction CreateCoinBaseTransaction(int nHeight) {

    auto consensusParams = Params().GetConsensus();

     // Create coinbase tx
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, nHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 50000 * COIN; // 50 PRT
    txNew.nExpiryHeight = 0;
    CTransaction coinbaseTx(txNew);
    return coinbaseTx;

}


TEST(WalletTests, SetupDatadirLocationRunAsFirstTest) {
    // Get temporary and unique path for file.
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
}


TEST (WalletTests, SetupKeysRunAsSecondTest) {
    SelectParams(CBaseChainParams::REGTEST);

    //Create New Wallet
    pTestWallet = new TestWallet("testwallet");

    {
        LOCK(pTestWallet->cs_wallet);

        // Setup a coinbase Address
        CBasicKeyStore keystore;
        CKey tsk = DecodeSecret(tSecretRegtest);
        EXPECT_TRUE(tsk.IsValid());
        pTestWallet->AddKey(tsk);
        scriptPubKey = GetScriptForDestination(tsk.GetPubKey().GetID());

        // Load the all-zeroes seed
        CKeyingMaterial rawSeed(32, 0);
        HDSeed seed(rawSeed);
        pTestWallet->LoadHDSeed(seed);

        // Generate a Sapling spending key and address
        saplingAddress = pTestWallet->GenerateNewSaplingZKey();

        // Generate a Ironwood spending key and address
        ironwoodAddress = pTestWallet->GenerateNewIronwoodZKey();
    }
}

TEST (WalletTests, IntializeSaplingIronwoodWalletsRunAsThirdTest) {

    SaplingMerkleFrontier saplingFrontierTree;
    EXPECT_NO_THROW(saplingWallet.InitNoteCommitmentTree(saplingFrontierTree));

    IronwoodMerkleFrontier ironwoodFrontierTree;
    EXPECT_NO_THROW(ironwoodWallet.InitNoteCommitmentTree(ironwoodFrontierTree));
}

TEST(WalletTests, AddCoinbaseTransactionsToWallet) {
    SelectParams(CBaseChainParams::REGTEST);


    int testHeight;
    //Create 5 coinbase transactions and add them to the wallet
    for (testHeight = 1; testHeight <= 5; testHeight++) {
        auto coinbaseTx = CreateCoinBaseTransaction(testHeight);
        std::vector<CTransaction> vtx;
        std::vector<CTransaction> addedVtx;
        std::set<libzcash::SaplingPaymentAddress> saplingAddrs;
        std::set<libzcash::IronwoodPaymentAddress> ironwoodAddrs;
        vtx.push_back(coinbaseTx);
        CBlock block;
        pTestWallet->AddToWalletIfInvolvingMe(vtx, addedVtx, &block, testHeight, true, saplingAddrs, ironwoodAddrs,true);

        // Update Coins
        UpdateCoins(coinbaseTx, view, testHeight);

        
    }
}


// The block that used to follow here (~1680 lines) was an entirely
// commented-out legacy suite ported from upstream zcashd, covering
// FindMySaplingNotes/FindMySproutNotes, per-pool note-locking, nullifier
// bookkeeping, and conflict detection. It predated the Ironwood pool and used
// CWallet/CWalletTx APIs (raw CWallet instances, note.nullifier(sk),
// AddToWallet(wtx, fUpdate, pwalletdb, ...), BuildWitnessCache) that no
// longer exist in this fork, so none of it could compile, let alone run.
//
// LockNote/IsLockedNote/UnlockNote for all three pools are real, currently
// live CWallet methods (wallet.h) with no coverage anywhere else in this
// suite; they're ported below against the current API, using JSOutPoint/
// SaplingOutPoint/IronwoodOutPoint directly (locking is a pure set-membership
// mechanism keyed by outpoint - it doesn't require a note that actually
// exists in the wallet). GetFilteredNotes gets a basic smoke test.
//
// Reconstructing FindMySaplingNotes/FindMyIronwoodNotes-style positive
// coverage (a real shielded note actually landing in the wallet, complete
// with a fake-mined block, Merkle witness, and nullifier map lookup) would
// require rebuilding the entire note-mining scaffold the old code used
// (TransactionBuilder + fake CBlockIndex + IncrementNoteWitnesses); that's
// out of scope for this pass and remains a known gap - GetFilteredNotes is
// only exercised here against an empty/unfunded shielded balance.

TEST(WalletTests, SproutNoteLocking) {
    JSOutPoint jsoutpt {GetRandHash(), 0, 0};
    JSOutPoint jsoutpt2 {GetRandHash(), 0, 1};

    // Test selective locking
    pTestWallet->LockNote(jsoutpt);
    EXPECT_TRUE(pTestWallet->IsLockedNote(jsoutpt));
    EXPECT_FALSE(pTestWallet->IsLockedNote(jsoutpt2));

    // Test selective unlocking
    pTestWallet->UnlockNote(jsoutpt);
    EXPECT_FALSE(pTestWallet->IsLockedNote(jsoutpt));

    // Test multiple locking
    pTestWallet->LockNote(jsoutpt);
    pTestWallet->LockNote(jsoutpt2);
    EXPECT_TRUE(pTestWallet->IsLockedNote(jsoutpt));
    EXPECT_TRUE(pTestWallet->IsLockedNote(jsoutpt2));

    // Test list
    auto v = pTestWallet->ListLockedSproutNotes();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_TRUE(std::find(v.begin(), v.end(), jsoutpt) != v.end());
    EXPECT_TRUE(std::find(v.begin(), v.end(), jsoutpt2) != v.end());

    // Test unlock all
    pTestWallet->UnlockAllSproutNotes();
    EXPECT_FALSE(pTestWallet->IsLockedNote(jsoutpt));
    EXPECT_FALSE(pTestWallet->IsLockedNote(jsoutpt2));
}

TEST(WalletTests, SaplingNoteLocking) {
    SaplingOutPoint sop1 {GetRandHash(), 1};
    SaplingOutPoint sop2 {GetRandHash(), 2};

    // Test selective locking
    pTestWallet->LockNote(sop1);
    EXPECT_TRUE(pTestWallet->IsLockedNote(sop1));
    EXPECT_FALSE(pTestWallet->IsLockedNote(sop2));

    // Test selective unlocking
    pTestWallet->UnlockNote(sop1);
    EXPECT_FALSE(pTestWallet->IsLockedNote(sop1));

    // Test multiple locking
    pTestWallet->LockNote(sop1);
    pTestWallet->LockNote(sop2);
    EXPECT_TRUE(pTestWallet->IsLockedNote(sop1));
    EXPECT_TRUE(pTestWallet->IsLockedNote(sop2));

    // Test list
    auto v = pTestWallet->ListLockedSaplingNotes();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_TRUE(std::find(v.begin(), v.end(), sop1) != v.end());
    EXPECT_TRUE(std::find(v.begin(), v.end(), sop2) != v.end());

    // Test unlock all
    pTestWallet->UnlockAllSaplingNotes();
    EXPECT_FALSE(pTestWallet->IsLockedNote(sop1));
    EXPECT_FALSE(pTestWallet->IsLockedNote(sop2));
}

// Ironwood counterpart to SaplingNoteLocking above - this pool didn't exist
// when the old commented-out suite was written, so there was never an
// Ironwood version of this test to port.
TEST(WalletTests, IronwoodNoteLocking) {
    IronwoodOutPoint iop1 {GetRandHash(), 1};
    IronwoodOutPoint iop2 {GetRandHash(), 2};

    // Test selective locking
    pTestWallet->LockNote(iop1);
    EXPECT_TRUE(pTestWallet->IsLockedNote(iop1));
    EXPECT_FALSE(pTestWallet->IsLockedNote(iop2));

    // Test selective unlocking
    pTestWallet->UnlockNote(iop1);
    EXPECT_FALSE(pTestWallet->IsLockedNote(iop1));

    // Test multiple locking
    pTestWallet->LockNote(iop1);
    pTestWallet->LockNote(iop2);
    EXPECT_TRUE(pTestWallet->IsLockedNote(iop1));
    EXPECT_TRUE(pTestWallet->IsLockedNote(iop2));

    // Test list
    auto v = pTestWallet->ListLockedIronwoodNotes();
    EXPECT_EQ(v.size(), 2u);
    EXPECT_TRUE(std::find(v.begin(), v.end(), iop1) != v.end());
    EXPECT_TRUE(std::find(v.begin(), v.end(), iop2) != v.end());

    // Test unlock all
    pTestWallet->UnlockAllIronwoodNotes();
    EXPECT_FALSE(pTestWallet->IsLockedNote(iop1));
    EXPECT_FALSE(pTestWallet->IsLockedNote(iop2));
}

// Smoke test only (see file-level comment above): saplingAddress/
// ironwoodAddress (from SetupKeysRunAsSecondTest) are real addresses in
// pTestWallet, but the wallet has never received a shielded note paid to
// either of them - only transparent coinbase outputs were added in
// AddCoinbaseTransactionsToWallet. Confirms GetFilteredNotes runs against a
// live wallet+address without crashing and correctly reports zero notes.
TEST(WalletTests, GetFilteredNotesEmptyForUnfundedShieldedAddresses) {
    std::set<libzcash::PaymentAddress> filterAddresses;
    filterAddresses.insert(saplingAddress);
    filterAddresses.insert(ironwoodAddress);

    std::vector<SaplingNoteEntry> saplingEntries;
    std::vector<IronwoodNoteEntry> ironwoodEntries;
    pTestWallet->GetFilteredNotes(saplingEntries, ironwoodEntries, filterAddresses);

    EXPECT_EQ(saplingEntries.size(), 0u);
    EXPECT_EQ(ironwoodEntries.size(), 0u);
}
