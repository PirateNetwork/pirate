#include <gtest/gtest.h>

#include "zcash/Address.hpp"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "util.h"

#include <boost/filesystem.hpp>

/**
 * This test covers Sapling methods on CWallet
 * GenerateNewSaplingZKey()
 * AddSaplingZKey()
 * AddSaplingIncomingViewingKey()
 * LoadSaplingZKey()
 * LoadSaplingIncomingViewingKey()
 * LoadSaplingZKeyMetadata()
 */
TEST(wallet_zkeys_tests, StoreAndLoadSaplingZkeys) {
    SelectParams(CBaseChainParams::MAIN);

    CWallet wallet;

    // wallet should be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    wallet.GetSaplingPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // No HD seed in the wallet
    EXPECT_ANY_THROW(wallet.GenerateNewSaplingZKey());

    // Load the all-zeroes seed
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    wallet.LoadHDSeed(seed);

    // Now this call succeeds
    auto address = wallet.GenerateNewSaplingZKey();

    // wallet should have one key
    wallet.GetSaplingPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // verify wallet has incoming viewing key for the address
    ASSERT_TRUE(wallet.HaveSaplingIncomingViewingKey(address));

    // manually add new spending key to wallet
    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);
    auto sk = m.Derive(0);
    ASSERT_TRUE(wallet.AddSaplingZKey(sk));

    // verify wallet did add it
    auto extfvk = sk.ToXFVK();
    ASSERT_TRUE(wallet.HaveSaplingSpendingKey(extfvk));

    // verify spending key stored correctly
    libzcash::SaplingExtendedSpendingKey keyOut;
    wallet.GetSaplingSpendingKey(extfvk, keyOut);
    ASSERT_EQ(sk, keyOut);

    // verify there are two keys
    wallet.GetSaplingPaymentAddresses(addrs);
    EXPECT_EQ(2, addrs.size());
    EXPECT_EQ(1, addrs.count(address));
    EXPECT_EQ(1, addrs.count(sk.DefaultAddress()));

    // Generate a diversified address different to the default
    // If we can't get an early diversified address, we are very unlucky
    blob88 diversifier;
    diversifier.begin()[0] = 10;
    auto dpa = sk.ToXFVK().Address(diversifier).value().second;
    libzcash::SaplingFullViewingKey fvk;
    sk.expsk.DeriveFVK(&fvk);

    // verify wallet only has the default address
    EXPECT_TRUE(wallet.HaveSaplingIncomingViewingKey(sk.DefaultAddress()));
    EXPECT_FALSE(wallet.HaveSaplingIncomingViewingKey(dpa));

    // manually add a diversified address
    libzcash::SaplingIncomingViewingKey ivk;
    fvk.DeriveIVK(&ivk);
    EXPECT_TRUE(wallet.AddSaplingIncomingViewingKey(ivk, dpa));

    // verify wallet did add it
    EXPECT_TRUE(wallet.HaveSaplingIncomingViewingKey(sk.DefaultAddress()));
    EXPECT_TRUE(wallet.HaveSaplingIncomingViewingKey(dpa));

    // verify that resets nTimeFirstKey, since there is no birthday info for watch-only keys
    EXPECT_EQ(wallet.nTimeFirstKey, 1);

    // Load a third key into the wallet
    auto sk2 = m.Derive(1);
    ASSERT_TRUE(wallet.LoadSaplingZKey(sk2));

    // attach metadata to this third key
    libzcash::SaplingIncomingViewingKey ivk2;
    libzcash::SaplingFullViewingKey fvk2;
    sk2.expsk.DeriveFVK(&fvk2);
    fvk2.DeriveIVK(&ivk2);
    int64_t now = GetTime();
    CKeyMetadata meta(now);
    ASSERT_TRUE(wallet.LoadSaplingZKeyMetadata(ivk2, meta));

    // check metadata is the same
    ASSERT_EQ(wallet.mapSaplingSpendingKeyMetadata[ivk2].nCreateTime, now);

    // Load a diversified address for the third key into the wallet
    auto dpa2 = sk2.ToXFVK().Address(diversifier).value().second;
    EXPECT_TRUE(wallet.HaveSaplingIncomingViewingKey(sk2.DefaultAddress()));
    EXPECT_FALSE(wallet.HaveSaplingIncomingViewingKey(dpa2));
    EXPECT_TRUE(wallet.LoadSaplingPaymentAddress(dpa2, ivk2));
    EXPECT_TRUE(wallet.HaveSaplingIncomingViewingKey(dpa2));
}

/**
 * This test covers methods on CWalletDB to load/save crypted sapling z keys.
 */
TEST(wallet_zkeys_tests, WriteCryptedSaplingZkeyDirectToDb) {
    SelectParams(CBaseChainParams::TESTNET);

    // Get temporary and unique path for file.
    // Note: / operator to append paths
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    bool fFirstRun;
    CWallet wallet("wallet_crypted_sapling.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

     // No default CPubKey set
    ASSERT_TRUE(fFirstRun);

    ASSERT_FALSE(wallet.HaveHDSeed());
    wallet.GenerateNewSeed();

    // wallet should be empty
    std::set<libzcash::SaplingPaymentAddress> addrs;
    wallet.GetSaplingPaymentAddresses(addrs);
    ASSERT_EQ(0, addrs.size());

    // Add random key to the wallet
    auto address = wallet.GenerateNewSaplingZKey();

    // wallet should have one key
    wallet.GetSaplingPaymentAddresses(addrs);
    ASSERT_EQ(1, addrs.size());

    // Generate a diversified address different to the default
    // If we can't get an early diversified address, we are very unlucky
    libzcash::SaplingExtendedSpendingKey extsk;
    EXPECT_TRUE(wallet.GetSaplingExtendedSpendingKey(address, extsk));
    blob88 diversifier;
    diversifier.begin()[0] = 10;
    auto dpa = extsk.ToXFVK().Address(diversifier).value().second;

    // Add diversified address to the wallet
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingFullViewingKey ivk_fvk;
    extsk.expsk.DeriveFVK(&ivk_fvk);
    ivk_fvk.DeriveIVK(&ivk);
    EXPECT_TRUE(wallet.AddSaplingIncomingViewingKey(ivk, dpa));

    // encrypt wallet
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));

    // adding a new key will fail as the wallet is locked
    EXPECT_ANY_THROW(wallet.GenerateNewSaplingZKey());

    // unlock wallet and then add
    wallet.Unlock(strWalletPass);
    auto address2 = wallet.GenerateNewSaplingZKey();

    // Create a new wallet from the existing wallet path
    CWallet wallet2("wallet_crypted_sapling.dat");
    ASSERT_EQ(DB_LOAD_CRYPTED, wallet2.InitalizeCryptedLoad());
    wallet2.SetDBCrypted();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadCryptedSeedFromDB());
    ASSERT_TRUE(wallet2.OpenWallet(strWalletPass));
    HDSeed seed2;
    wallet2.GetHDSeed(seed2);
    wallet2.seedEncyptionFP = seed2.EncryptionFingerprint();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));
    wallet2.Lock();

    // Confirm it's not the same as the other wallet
    ASSERT_TRUE(&wallet != &wallet2);
    ASSERT_TRUE(wallet2.HaveHDSeed());

    // wallet should have three addresses
    wallet2.GetSaplingPaymentAddresses(addrs);
    ASSERT_EQ(3, addrs.size());

    //check we have entries for our payment addresses
    ASSERT_TRUE(addrs.count(address));
    ASSERT_TRUE(addrs.count(address2));
    ASSERT_TRUE(addrs.count(dpa));

    // spending key is crypted, so we can't extract valid payment address
    libzcash::SaplingExtendedSpendingKey keyOut;
    EXPECT_FALSE(wallet2.GetSaplingExtendedSpendingKey(address, keyOut));
    
    EXPECT_ANY_THROW(keyOut.DefaultAddress());
    // ASSERT_FALSE(address == keyOut.DefaultAddress());

    // address -> ivk mapping is not crypted
    libzcash::SaplingIncomingViewingKey ivkOut;
    EXPECT_TRUE(wallet2.GetSaplingIncomingViewingKey(dpa, ivkOut));
    EXPECT_EQ(ivk, ivkOut);

   // unlock wallet to get spending keys and verify payment addresses
      wallet2.Unlock(strWalletPass);

    EXPECT_TRUE(wallet2.GetSaplingExtendedSpendingKey(address, keyOut));
    ASSERT_EQ(address, keyOut.DefaultAddress());

    EXPECT_TRUE(wallet2.GetSaplingExtendedSpendingKey(address2, keyOut));
    ASSERT_EQ(address2, keyOut.DefaultAddress());
}

TEST(wallet_zkeys_tests, StoreAndLoadIronwoodKeys) {
    SelectParams(CBaseChainParams::MAIN);

    CWallet wallet;

    // wallet should be empty
    std::set<libzcash::IronwoodPaymentAddress> ironwoodAddrs;
    wallet.GetIronwoodPaymentAddresses(ironwoodAddrs);
    ASSERT_EQ(0, ironwoodAddrs.size());

    // No HD seed in the wallet
    EXPECT_ANY_THROW(wallet.GenerateNewIronwoodZKey());

    // Load the all-zeroes seed
    CKeyingMaterial rawSeed(32, 0);
    HDSeed seed(rawSeed);
    wallet.LoadHDSeed(seed);

    // Now this call succeeds
    auto ironwoodAddr = wallet.GenerateNewIronwoodZKey();

    // wallet should have one key
    wallet.GetIronwoodPaymentAddresses(ironwoodAddrs);
    ASSERT_EQ(1, ironwoodAddrs.size());
    ASSERT_TRUE(ironwoodAddrs.count(ironwoodAddr));

    // verify wallet has incoming viewing key for the address
    ASSERT_TRUE(wallet.HaveIronwoodIncomingViewingKey(ironwoodAddr));

    // manually add new spending key to wallet
    uint32_t bip44CoinType = Params().BIP44CoinType();
    auto ironwoodSkOpt = libzcash::IronwoodExtendedSpendingKeyPirate::Master(seed).Derive(bip44CoinType,0);
    ASSERT_TRUE(ironwoodSkOpt.has_value());

    auto ironwoodSk = ironwoodSkOpt.value();
    ASSERT_TRUE(wallet.AddIronwoodZKey(ironwoodSk));

    // verify wallet did add it
    auto ironwoodFvkOpt = ironwoodSk.GetXFVK();
    ASSERT_TRUE(ironwoodFvkOpt.has_value());

    auto ironwoodFvk = ironwoodFvkOpt.value();
    ASSERT_TRUE(wallet.HaveIronwoodSpendingKey(ironwoodFvk));

    // verify spending key stored correctly
    libzcash::IronwoodExtendedSpendingKeyPirate keyOut;
    wallet.GetIronwoodSpendingKey(ironwoodFvk, keyOut);
    ASSERT_EQ(ironwoodSk, keyOut);
}

TEST(wallet_zkeys_tests, EncryptAndUnlockIronwoodKeys) {
    SelectParams(CBaseChainParams::TESTNET);

    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();

    bool fFirstRun;
    CWallet wallet("wallet_crypted_ironwood.dat");
    ASSERT_EQ(DB_LOAD_OK, wallet.LoadWallet(fFirstRun));

    ASSERT_FALSE(wallet.HaveHDSeed());
    wallet.GenerateNewSeed();

    // wallet should be empty
    std::set<libzcash::IronwoodPaymentAddress> ironwoodAddrs;
    wallet.GetIronwoodPaymentAddresses(ironwoodAddrs);
    ASSERT_EQ(0, ironwoodAddrs.size());

    // Add random key to the wallet
    auto ironwoodAddr = wallet.GenerateNewIronwoodZKey();

    // wallet should have one key
    wallet.GetIronwoodPaymentAddresses(ironwoodAddrs);
    ASSERT_EQ(1, ironwoodAddrs.size());

    // encrypt wallet
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = "hello";
    ASSERT_TRUE(wallet.EncryptWallet(strWalletPass));

    // adding a new key will fail as the wallet is locked
    EXPECT_ANY_THROW(wallet.GenerateNewIronwoodZKey());

    // unlock wallet and then add
    wallet.Unlock(strWalletPass);
    auto ironwoodAddr2 = wallet.GenerateNewIronwoodZKey();

    // Create a new wallet from the existing wallet path
    CWallet wallet2("wallet_crypted_ironwood.dat");
    ASSERT_EQ(DB_LOAD_CRYPTED, wallet2.InitalizeCryptedLoad());
    wallet2.SetDBCrypted();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadCryptedSeedFromDB());
    ASSERT_TRUE(wallet2.OpenWallet(strWalletPass));
    HDSeed seed2;
    wallet2.GetHDSeed(seed2);
    wallet2.seedEncyptionFP = seed2.EncryptionFingerprint();
    ASSERT_EQ(DB_LOAD_OK, wallet2.LoadWallet(fFirstRun));
    wallet2.Lock();

    // Confirm it's not the same as the other wallet
    ASSERT_TRUE(&wallet != &wallet2);
    ASSERT_TRUE(wallet2.HaveHDSeed());

    // wallet should have two addresses
    wallet2.GetIronwoodPaymentAddresses(ironwoodAddrs);
    ASSERT_EQ(2, ironwoodAddrs.size());
    ASSERT_TRUE(ironwoodAddrs.count(ironwoodAddr));
    ASSERT_TRUE(ironwoodAddrs.count(ironwoodAddr2));

    // spending key is crypted, so we can't extract valid spending key
    libzcash::IronwoodExtendedSpendingKeyPirate keyOut;
    EXPECT_FALSE(wallet2.GetIronwoodExtendedSpendingKey(ironwoodAddr, keyOut));

    libzcash::IronwoodPaymentAddress defaultAddr;
    ASSERT_TRUE(keyOut.sk.DeriveDefaultAddress(&defaultAddr));
    ASSERT_FALSE(ironwoodAddr == defaultAddr);

    // unlock wallet to get spending keys and verify payment addresses
    wallet2.Unlock(strWalletPass);

    EXPECT_TRUE(wallet2.GetIronwoodExtendedSpendingKey(ironwoodAddr, keyOut));
    ASSERT_TRUE(keyOut.sk.DeriveDefaultAddress(&defaultAddr));

    ASSERT_EQ(ironwoodAddr, defaultAddr);
}