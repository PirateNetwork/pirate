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

// Phase 5 follow-up: the consolidation/sweep/fee/pruning/change-address
// settings added in Phase 5 were originally always written in plaintext,
// regardless of whether the wallet itself was encrypted -- an acquired
// encrypted wallet.dat still fully disclosed them, address-filter lists and
// sweep/change addresses especially. This is the same class of fix as
// DestDataRoundTripsThroughEncryption/HDChainRoundTripsThroughEncryption
// above, applied uniformly to every one of those settings via
// CWallet::WriteEncryptableSetting()/CWalletDB::ReadKeyValue()'s "c"-prefixed
// branches. Covers one setting from each affected shape (plain scalar, a
// vector<string> address-filter list, and the sweep-address string with its
// cross-pool mutual-exclusivity clearing) rather than all ~26 individually --
// they all go through the same generic template, so one of each is
// representative, not exhaustive.
TEST(WalletEncryptionTests, ConfigSettingsRoundTripThroughEncryptionAndPlaintextIsErased)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_configsettings_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    // Sanity check while still unencrypted: this is the plaintext path every
    // wallet used unconditionally before this fix.
    wallet.SetSaplingConsolidationTargetQty(77);
    {
        CWalletDB walletdb("wallet_configsettings_test.dat");
        EXPECT_TRUE(walletdb.SettingExists("targetsaplingconsolidationqty"));
    }

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    wallet.Unlock(strWalletPass);

    // Encrypting migrates the value set above (CWallet::EncryptWallet() calls
    // MigrateSettingsToEncrypted()) -- the plaintext record must be gone and
    // the encrypted one present immediately, without any further Set*() call.
    {
        CWalletDB walletdb("wallet_configsettings_test.dat");
        EXPECT_FALSE(walletdb.SettingExists("targetsaplingconsolidationqty"));
        EXPECT_TRUE(walletdb.SettingExists("ctargetsaplingconsolidationqty"));
    }

    // Now exercise the write-while-encrypted path directly for the other two
    // shapes, same as the migration above already implicitly covers for the
    // first setting.
    wallet.SetSaplingConsolidationAddresses({"zs1exampleaddressone", "zs1exampleaddresstwo"});
    wallet.SetSaplingSweepAddress("zs1sweepaddressexample");

    {
        CWalletDB walletdb("wallet_configsettings_test.dat");
        EXPECT_FALSE(walletdb.SettingExists("saplingconsolidationaddresses"));
        EXPECT_TRUE(walletdb.SettingExists("csaplingconsolidationaddresses"));
        EXPECT_FALSE(walletdb.SettingExists("saplingsweepaddress"));
        EXPECT_TRUE(walletdb.SettingExists("csaplingsweepaddress"));
        // Cross-pool mutual exclusivity (SetSaplingSweepAddress clears the
        // Ironwood slot too) must hold in encrypted form as well: no leftover
        // plaintext, and the encrypted Ironwood record decrypts to empty.
        EXPECT_FALSE(walletdb.SettingExists("ironwoodsweepaddress"));
        EXPECT_TRUE(walletdb.SettingExists("cironwoodsweepaddress"));
    }

    // Reload from disk into a fresh wallet object -- proves the encrypted
    // records actually persisted and decrypt correctly, not just that the
    // in-memory object still remembers what it wrote.
    CWallet wallet2("wallet_configsettings_test.dat");
    ASSERT_EQ(DB_LOAD_CRYPTED, wallet2.InitalizeCryptedLoad());
    wallet2.SetDBCrypted();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadCryptedSeedFromDB());
    ASSERT_TRUE(wallet2.OpenWallet(strWalletPass));
    HDSeed seed2;
    wallet2.GetHDSeed(seed2);
    wallet2.seedEncyptionFP = seed2.EncryptionFingerprint();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));

    EXPECT_EQ(77, wallet2.targetSaplingConsolidationQty);
    ASSERT_EQ(2u, wallet2.saplingConsolidationAddresses.size());
    EXPECT_EQ("zs1exampleaddressone", wallet2.saplingConsolidationAddresses[0]);
    EXPECT_EQ("zs1exampleaddresstwo", wallet2.saplingConsolidationAddresses[1]);
    EXPECT_EQ("zs1sweepaddressexample", wallet2.saplingSweepAddress);
    EXPECT_TRUE(wallet2.ironwoodSweepAddress.empty());
}

// Audit follow-up on the fix above: chash must be bound to the record's own
// key name, not just its serialized content. These settings have tiny value
// domains (a bool is one byte; an empty string and an empty address list
// both serialize to a single zero byte), so a content-only chash would make
// two different settings' encrypted records byte-identical whenever their
// values happen to match -- letting an attacker holding only the file (no
// passphrase) read low-entropy settings straight off which records are
// equal, and letting one record's ciphertext be copied onto a different
// same-shaped key without detection. Covers both halves: equal plaintext
// values under different keys must NOT produce identical ciphertext, and
// swapping one setting's genuine (chash, ciphertext) pair onto a different
// key of the same C++ type must be rejected on load, not silently accepted.
TEST(WalletEncryptionTests, ConfigSettingsChashIsBoundToKeyNotJustContent)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_configsettings_keybinding_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    wallet.Unlock(strWalletPass);

    // Two different bool settings, both set to the identical value `true`.
    wallet.SetSaplingConsolidationEnabled(true);
    wallet.SetIronwoodConsolidationEnabled(true);

    CWalletDB walletdb("wallet_configsettings_keybinding_test.dat");
    std::pair<uint256, std::vector<unsigned char>> saplingRecord, ironwoodRecord;
    ASSERT_TRUE(walletdb.ReadSetting("csaplingconsolidationenabled", saplingRecord));
    ASSERT_TRUE(walletdb.ReadSetting("cironwoodconsolidationenabled", ironwoodRecord));

    // Same plaintext value, different keys -- chash (and therefore the AES
    // IV and ciphertext) must differ. Equal here would mean the encryption
    // leaks equality between settings to anyone holding just the file.
    EXPECT_NE(saplingRecord.first, ironwoodRecord.first) << "chash must not be identical across different settings with equal values";
    EXPECT_NE(saplingRecord.second, ironwoodRecord.second) << "ciphertext must not be identical across different settings with equal values";

    // Swap: try to decrypt the Sapling record's genuine (chash, ciphertext)
    // as if it were the Ironwood record. Must fail closed, not succeed with
    // the wrong value silently applied.
    bool crossDecoded = false;
    EXPECT_FALSE(wallet.DecryptIronwoodConsolidationEnabled("cironwoodconsolidationenabled", saplingRecord.first, saplingRecord.second, crossDecoded))
        << "a genuine record for one setting must not decrypt successfully under a different setting's key";
}

// Audit follow-up: WriteEncryptableSetting() must fail closed (persist
// nothing, in either form) when the wallet is encrypted but locked, rather
// than falling back to plaintext -- that fallback is exactly the leak this
// whole feature exists to close.
TEST(WalletEncryptionTests, ConfigSettingWriteFailsClosedWhenWalletIsLocked)
{
    SelectParams(CBaseChainParams::TESTNET);
    MakeTempDataDir();

    bool fFirstRun;
    CWallet wallet("wallet_configsettings_locked_test.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));
    wallet.GenerateNewSeed();

    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    int defaultSweepInterval = wallet.sweepInterval;
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));
    // EncryptWallet() leaves the wallet locked (see its Lock()/Unlock()/
    // NewKeyPool()/Lock() sequence) -- no explicit Lock() call needed here.
    // It also calls MigrateSettingsToEncrypted() internally (while briefly
    // unlocked, mid-transition), which persists every setting's current
    // value including ones still at their compiled-in default -- so
    // "csweepinterval" already exists at this point, encoding
    // defaultSweepInterval. That's expected; the actual thing under test is
    // that the locked SetSweepInterval() call below cannot change it.
    ASSERT_TRUE(wallet.IsLocked());

    wallet.SetSweepInterval(4321);

    CWalletDB walletdb("wallet_configsettings_locked_test.dat");
    EXPECT_FALSE(walletdb.SettingExists("sweepinterval"))
        << "must not fall back to writing plaintext into an encrypted wallet's file";

    std::pair<uint256, std::vector<unsigned char>> record;
    ASSERT_TRUE(walletdb.ReadSetting("csweepinterval", record));
    // Decryption itself requires an unlocked wallet -- unlock only to verify
    // what's actually on disk; the locked SetSweepInterval() call above is
    // what's under test, not this read-back.
    wallet.Unlock(strWalletPass);
    int decoded = -1;
    EXPECT_TRUE(wallet.DecryptSweepInterval("csweepinterval", record.first, record.second, decoded));
    EXPECT_EQ(defaultSweepInterval, decoded)
        << "the locked-wallet Set call must not have changed the persisted value to 4321";
}
