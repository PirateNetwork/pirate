// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "key.h"
#include "key_io.h"
#include "util.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "zcash/Address.hpp"

#include <boost/filesystem.hpp>

// wallet_encryption_audit.md P5/P6/C3/P3: regression tests for on-disk
// wallet-at-rest fixes that don't fit test_wallet_zkeys.cpp's z-key focus:
//  - P5: destdata (per-address labels) round-trips through the encrypted
//    `cdestdata` form instead of leaking in a `destdata` DB key.
//  - P6: the HD chain (seed fingerprint, account counters) round-trips
//    through the encrypted `chdchain` form.
//  - C3 (witness-tree integrity): a tampered encrypted note-commitment-tree
//    blob is detected and rejected, not silently accepted.
//  - P3: the `zkeymeta` purge-routing bug (Sprout metadata records were
//    misrouted during the legacy-record sweep, so they were never erased).

static boost::filesystem::path MakeTempDataDir()
{
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    return pathTemp;
}

TEST(WalletEncryptionTests, DestDataRoundTripsThroughEncryption)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_destdata_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    CKey key;
    key.MakeNewKey(true);
    CTxDestination dest = key.GetPubKey().GetID();

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    wallet.Unlock(strWalletPass);

    // Written while encrypted+unlocked: must take the WriteCryptedDestData path.
    ASSERT_TRUE(wallet.AddDestData(dest, "label", "my saved address"));

    // Reload from disk into a fresh wallet object.
    CWallet wallet2("wallet_destdata_test.dat");
    ASSERT_EQ(DB_LOAD_CRYPTED, wallet2.InitalizeCryptedLoad());
    wallet2.SetDBCrypted();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadCryptedSeedFromDB());
    ASSERT_TRUE(wallet2.OpenWallet(strWalletPass));
    HDSeed seed2;
    wallet2.GetHDSeed(seed2);
    wallet2.seedEncyptionFP = seed2.EncryptionFingerprint();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));
    wallet2.Unlock(strWalletPass);

    std::string value;
    ASSERT_TRUE(wallet2.GetDestData(dest, "label", &value));
    EXPECT_EQ(value, "my saved address");
}

TEST(WalletEncryptionTests, HDChainRoundTripsThroughEncryption)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_hdchain_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    wallet.Unlock(strWalletPass);

    // Deriving a new Sapling key advances/persists the HD chain while the
    // wallet is encrypted+unlocked, exercising the encrypted chdchain path.
    wallet.GenerateNewSaplingZKey();
    uint256 expectedSeedFp = wallet.GetHDChain().seedFp;
    ASSERT_FALSE(expectedSeedFp.IsNull());

    CWallet wallet2("wallet_hdchain_test.dat");
    ASSERT_EQ(DB_LOAD_CRYPTED, wallet2.InitalizeCryptedLoad());
    wallet2.SetDBCrypted();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadCryptedSeedFromDB());
    ASSERT_TRUE(wallet2.OpenWallet(strWalletPass));
    HDSeed seed2;
    wallet2.GetHDSeed(seed2);
    wallet2.seedEncyptionFP = seed2.EncryptionFingerprint();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));

    EXPECT_EQ(wallet2.GetHDChain().seedFp, expectedSeedFp);
}

TEST(WalletEncryptionTests, WitnessTreeTamperIsDetected)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_witnesstree_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    wallet.Unlock(strWalletPass);

    // Stand-in for a serialized note-commitment tree: content doesn't matter,
    // only that chash is bound to it via HashWithFP, matching how the real
    // csaplingwitnessenc record is produced (chash = HashWithFP(serialized tree)).
    // (CHDChain is used as the payload type purely because SerializeForEncryptionInput<T>
    // is a template defined in wallet.cpp - only instantiations already used there,
    // like this one for the HD chain, are linkable from this test binary.)
    CHDChain fakeTree;
    fakeTree.seedFp = GetRandHash();
    CKeyingMaterial vchSecret = wallet.SerializeForEncryptionInput(fakeTree);
    uint256 chash = wallet.HashWithFP(vchSecret);

    std::vector<unsigned char> vchCryptedSecret;
    ASSERT_TRUE(wallet.EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret));

    // Untampered: decrypts and verifies.
    CKeyingMaterial decrypted;
    ASSERT_TRUE(wallet.DecryptSaplingWitnessTree(chash, vchCryptedSecret, decrypted));
    EXPECT_EQ(decrypted, vchSecret);

    // Tampered ciphertext: must fail closed (decrypt error, or a plaintext
    // that no longer matches chash), never silently accepted.
    std::vector<unsigned char> tampered = vchCryptedSecret;
    ASSERT_FALSE(tampered.empty());
    tampered[0] ^= 0xFF;
    CKeyingMaterial tamperedResult;
    EXPECT_FALSE(wallet.DecryptSaplingWitnessTree(chash, tampered, tamperedResult));
}

TEST(WalletEncryptionTests, ZkeymetaPurgeRoutingErasesRecord)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_zkeymeta_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    libzcash::SproutSpendingKey sk = libzcash::SproutSpendingKey::random();
    libzcash::SproutPaymentAddress addr = sk.address();
    libzcash::ReceivingKey rk = sk.receiving_key();
    std::vector<unsigned char> vchCryptedSecret = {0x01, 0x02, 0x03};
    CKeyMetadata keyMeta(GetTime());

    {
        CWalletDB walletdb("wallet_zkeymeta_test.dat");
        ASSERT_TRUE(walletdb.WriteCryptedZKey(addr, rk, vchCryptedSecret, keyMeta));
    }

    // Before the fix, a "zkeymeta" record's address was pushed into the
    // czkey list instead of the zkeymeta list, so the zkeymeta erase loop in
    // ZapOldRecords() never fired and the record survived indefinitely.
    {
        CWalletDB walletdb("wallet_zkeymeta_test.dat");
        std::vector<uint256> vArcSproutNullifier;
        std::vector<libzcash::SproutViewingKey> vSproutViewingKeys;
        std::vector<libzcash::SproutPaymentAddress> vSproutPaymentAddresses;
        std::vector<libzcash::SproutPaymentAddress> vCSproutPaymentAddresses;
        std::vector<libzcash::SproutPaymentAddress> vSproutMetaData;
        ASSERT_EQ(DB_LOAD_OK, walletdb.FindOldRecordsToZap(&wallet, vArcSproutNullifier,
            vSproutViewingKeys, vSproutPaymentAddresses, vCSproutPaymentAddresses, vSproutMetaData));
        ASSERT_EQ(vSproutMetaData.size(), 1u);
        EXPECT_EQ(vSproutMetaData[0], addr);
    }

    ASSERT_EQ(DB_LOAD_OK, wallet.ZapOldRecords());

    // Re-scan: the zkeymeta record must actually be gone now.
    {
        CWalletDB walletdb("wallet_zkeymeta_test.dat");
        std::vector<uint256> vArcSproutNullifier;
        std::vector<libzcash::SproutViewingKey> vSproutViewingKeys;
        std::vector<libzcash::SproutPaymentAddress> vSproutPaymentAddresses;
        std::vector<libzcash::SproutPaymentAddress> vCSproutPaymentAddresses;
        std::vector<libzcash::SproutPaymentAddress> vSproutMetaData;
        ASSERT_EQ(DB_LOAD_OK, walletdb.FindOldRecordsToZap(&wallet, vArcSproutNullifier,
            vSproutViewingKeys, vSproutPaymentAddresses, vCSproutPaymentAddresses, vSproutMetaData));
        EXPECT_EQ(vSproutMetaData.size(), 0u);
        EXPECT_EQ(vCSproutPaymentAddresses.size(), 0u);
    }
}
