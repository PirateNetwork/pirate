#include <gtest/gtest.h>

#include "test/data/sapling_key_components.json.h"

#include "keystore.h"
#include "random.h"
#ifdef ENABLE_WALLET
#include "wallet/crypter.h"
#endif
//#include "util/test.h"
#include "zcash/Address.hpp"

#include "json_test_vectors.h"

#define MAKE_STRING(x) std::string((x), (x)+sizeof(x))

using namespace libzcash;

const uint32_t SLIP44_TESTNET_TYPE = 1;

// TEST(KeystoreTests, StoreAndRetrieveMnemonicSeed) {
//     CBasicKeyStore keyStore;

//     // When we haven't set a seed, we shouldn't get one
//     EXPECT_FALSE(keyStore.HaveMnemonicSeed());
//     auto seedOut = keyStore.GetMnemonicSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     // Generate a random seed
//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);

//     // We should be able to set and retrieve the seed
//     ASSERT_TRUE(keyStore.SetMnemonicSeed(seed));
//     EXPECT_TRUE(keyStore.HaveMnemonicSeed());
//     seedOut = keyStore.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // Generate another random seed
//     auto seed2 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     EXPECT_NE(seed, seed2);

//     // We should not be able to set and retrieve a different seed
//     EXPECT_FALSE(keyStore.SetMnemonicSeed(seed2));
//     seedOut = keyStore.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());
// }

// TEST(KeystoreTests, DecodeInvalidMnemonic) {
//     SecureString mnemonic("\xff");
//     EXPECT_FALSE(MnemonicSeed::ForPhrase(Language::English, mnemonic).has_value());
// }

// TEST(KeystoreTests, DeserializeMnemonic) {
//     CDataStream ss0(SER_NETWORK, CLIENT_VERSION);
//     ss0 << (uint32_t)English;
//     ss0 << SecureString("abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon art");
//     EXPECT_NO_THROW(MnemonicSeed::Read(ss0));

//     CDataStream ss(SER_NETWORK, CLIENT_VERSION);
//     ss << (uint32_t)English;
//     ss << SecureString("\xff");
//     EXPECT_THROW(MnemonicSeed::Read(ss), std::ios_base::failure);
// }

// TEST(KeystoreTests, StoreAndRetrieveLegacyHDSeed) {
//     CBasicKeyStore keyStore;

//     // When we haven't set a seed, we shouldn't get one
//     std::optional<HDSeed> seedOut = keyStore.GetLegacyHDSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     // Generate a random seed
//     // (We use MnemonicSeed purely to generate a seed, and then drop the mnemonic part.)
//     HDSeed seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);

//     // We should be able to set and retrieve the seed
//     ASSERT_TRUE(keyStore.SetLegacyHDSeed(seed));
//     seedOut = keyStore.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // Generate another random seed
//     HDSeed seed2 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     EXPECT_NE(seed, seed2);

//     // We should not be able to set and retrieve a different seed
//     EXPECT_FALSE(keyStore.SetLegacyHDSeed(seed2));
//     seedOut = keyStore.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());
// }

// TEST(KeystoreTests, SaplingKeys) {
//     // ["sk, ask, nsk, ovk, ak, nk, ivk, default_d, default_pk_d, note_v, note_r, note_cm, note_pos, note_nf"],
//     UniValue sapling_keys = read_json(MAKE_STRING(json_tests::sapling_key_components));

//     // Skipping over comments in sapling_key_components.json file
//     for (size_t i = 2; i < 12; i++) {
//         uint256 skSeed, ask, nsk, ovk, ak, nk, ivk;
//         skSeed.SetHex(sapling_keys[i][0].getValStr());
//         ask.SetHex(sapling_keys[i][1].getValStr());
//         nsk.SetHex(sapling_keys[i][2].getValStr());
//         ovk.SetHex(sapling_keys[i][3].getValStr());
//         ak.SetHex(sapling_keys[i][4].getValStr());
//         nk.SetHex(sapling_keys[i][5].getValStr());
//         ivk.SetHex(sapling_keys[i][6].getValStr());

//         libzcash::diversifier_t default_d;
//         std::copy_n(ParseHex(sapling_keys[i][7].getValStr()).begin(), 11, default_d.begin());

//         uint256 default_pk_d;
//         default_pk_d.SetHex(sapling_keys[i][8].getValStr());

//         auto sk = libzcash::SaplingSpendingKey(skSeed);

//         // Check that expanded spending key from primitives and from sk are the same
//         auto exp_sk_2 = libzcash::SaplingExpandedSpendingKey(ask, nsk, ovk);
//         auto exp_sk = sk.expanded_spending_key();
//         EXPECT_EQ(exp_sk, exp_sk_2);

//         // Check that full viewing key derived from sk and expanded sk are the same
//         auto full_viewing_key = sk.full_viewing_key();
//         EXPECT_EQ(full_viewing_key, exp_sk.full_viewing_key());

//         // Check that full viewing key from primitives and from sk are the same
//         auto full_viewing_key_2 = libzcash::SaplingFullViewingKey(ak, nk, ovk);
//         EXPECT_EQ(full_viewing_key, full_viewing_key_2);

//         // Check that incoming viewing key from primitives and from sk are the same
//         auto in_viewing_key = full_viewing_key.in_viewing_key();
//         auto in_viewing_key_2 = libzcash::SaplingIncomingViewingKey(ivk);
//         EXPECT_EQ(in_viewing_key, in_viewing_key_2);

//         // Check that the default address from primitives and from sk method are the same
//         auto default_addr = sk.default_address();
//         auto addrOpt2 = in_viewing_key.address(default_d);
//         EXPECT_TRUE(addrOpt2);
//         auto default_addr_2 = addrOpt2.value();
//         EXPECT_EQ(default_addr, default_addr_2);

//         auto default_addr_3 = libzcash::SaplingPaymentAddress(default_d, default_pk_d);
//         EXPECT_EQ(default_addr_2, default_addr_3);
//         EXPECT_EQ(default_addr, default_addr_3);
//     }
// }

// TEST(KeystoreTests, StoreAndRetrieveSpendingKey) {
//     CBasicKeyStore keyStore;
//     libzcash::SproutSpendingKey skOut;

//     std::set<libzcash::SproutPaymentAddress> addrs;
//     keyStore.GetSproutPaymentAddresses(addrs);
//     EXPECT_EQ(0, addrs.size());

//     auto sk = libzcash::SproutSpendingKey::random();
//     auto addr = sk.address();

//     // Sanity-check: we can't get a key we haven't added
//     EXPECT_FALSE(keyStore.HaveSproutSpendingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutSpendingKey(addr, skOut));

//     keyStore.AddSproutSpendingKey(sk);
//     EXPECT_TRUE(keyStore.HaveSproutSpendingKey(addr));
//     EXPECT_TRUE(keyStore.GetSproutSpendingKey(addr, skOut));
//     EXPECT_EQ(sk, skOut);

//     keyStore.GetSproutPaymentAddresses(addrs);
//     EXPECT_EQ(1, addrs.size());
//     EXPECT_EQ(1, addrs.count(addr));
// }

// TEST(KeystoreTests, StoreAndRetrieveNoteDecryptor) {
//     CBasicKeyStore keyStore;
//     ZCNoteDecryption decOut;

//     auto sk = libzcash::SproutSpendingKey::random();
//     auto addr = sk.address();

//     EXPECT_FALSE(keyStore.GetNoteDecryptor(addr, decOut));

//     keyStore.AddSproutSpendingKey(sk);
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk.receiving_key()), decOut);
// }

// TEST(KeystoreTests, StoreAndRetrieveViewingKey) {
//     CBasicKeyStore keyStore;
//     libzcash::SproutViewingKey vkOut;
//     libzcash::SproutSpendingKey skOut;
//     ZCNoteDecryption decOut;

//     auto sk = libzcash::SproutSpendingKey::random();
//     auto vk = sk.viewing_key();
//     auto addr = sk.address();

//     // Sanity-check: we can't get a viewing key we haven't added
//     EXPECT_FALSE(keyStore.HaveSproutViewingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutViewingKey(addr, vkOut));

//     // and we shouldn't have a spending key or decryptor either
//     EXPECT_FALSE(keyStore.HaveSproutSpendingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutSpendingKey(addr, skOut));
//     EXPECT_FALSE(keyStore.GetNoteDecryptor(addr, decOut));

//     // and we can't find it in our list of addresses
//     std::set<libzcash::SproutPaymentAddress> addresses;
//     keyStore.GetSproutPaymentAddresses(addresses);
//     EXPECT_FALSE(addresses.count(addr));

//     keyStore.AddSproutViewingKey(vk);
//     EXPECT_TRUE(keyStore.HaveSproutViewingKey(addr));
//     EXPECT_TRUE(keyStore.GetSproutViewingKey(addr, vkOut));
//     EXPECT_EQ(vk, vkOut);

//     // We should still not have the spending key...
//     EXPECT_FALSE(keyStore.HaveSproutSpendingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutSpendingKey(addr, skOut));

//     // ... but we should have a decryptor
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk.receiving_key()), decOut);

//     // ... and we should find it in our list of addresses
//     addresses.clear();
//     keyStore.GetSproutPaymentAddresses(addresses);
//     EXPECT_TRUE(addresses.count(addr));

//     keyStore.RemoveSproutViewingKey(vk);
//     EXPECT_FALSE(keyStore.HaveSproutViewingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutViewingKey(addr, vkOut));
//     EXPECT_FALSE(keyStore.HaveSproutSpendingKey(addr));
//     EXPECT_FALSE(keyStore.GetSproutSpendingKey(addr, skOut));
//     addresses.clear();
//     keyStore.GetSproutPaymentAddresses(addresses);
//     EXPECT_FALSE(addresses.count(addr));

//     // We still have a decryptor because those are cached in memory
//     // (and also we only remove viewing keys when adding a spending key)
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk.receiving_key()), decOut);
// }

// // Sapling
// TEST(KeystoreTests, StoreAndRetrieveSaplingSpendingKey) {
//     CBasicKeyStore keyStore;
//     libzcash::SaplingExtendedSpendingKey skOut;
//     libzcash::SaplingExtendedFullViewingKey extfvkOut;
//     libzcash::SaplingIncomingViewingKey ivkOut;

//     auto sk = GetTestMasterSaplingSpendingKey();
//     auto extfvk = sk.ToXFVK();
//     auto ivk = extfvk.ToIncomingViewingKey();
//     auto addr = sk.ToXFVK().DefaultAddress();

//     // Sanity-check: we can't get a key we haven't added
//     EXPECT_FALSE(keyStore.HaveSaplingSpendingKey(extfvk));
//     EXPECT_FALSE(keyStore.GetSaplingSpendingKey(extfvk, skOut));
//     // Sanity-check: we can't get a full viewing key we haven't added
//     EXPECT_FALSE(keyStore.HaveSaplingFullViewingKey(ivk));
//     EXPECT_FALSE(keyStore.GetSaplingFullViewingKey(ivk, extfvkOut));

//     // When we specify the default address, we get the full mapping
//     keyStore.AddSaplingSpendingKey(sk);
//     EXPECT_TRUE(keyStore.HaveSaplingSpendingKey(extfvk));
//     EXPECT_TRUE(keyStore.GetSaplingSpendingKey(extfvk, skOut));
//     EXPECT_TRUE(keyStore.HaveSaplingFullViewingKey(ivk));
//     EXPECT_TRUE(keyStore.GetSaplingFullViewingKey(ivk, extfvkOut));

//     // We can't get an incoming viewing key for an address we haven't added
//     EXPECT_FALSE(keyStore.HaveSaplingIncomingViewingKey(addr));
//     EXPECT_FALSE(keyStore.GetSaplingIncomingViewingKey(addr, ivkOut));

//     keyStore.AddSaplingPaymentAddress(ivk, addr);
//     EXPECT_TRUE(keyStore.HaveSaplingIncomingViewingKey(addr));
//     EXPECT_TRUE(keyStore.GetSaplingIncomingViewingKey(addr, ivkOut));
//     EXPECT_EQ(sk, skOut);
//     EXPECT_EQ(extfvk, extfvkOut);
//     EXPECT_EQ(ivk, ivkOut);
// }

// TEST(KeystoreTests, StoreAndRetrieveSaplingFullViewingKey) {
//     CBasicKeyStore keyStore;
//     libzcash::SaplingExtendedSpendingKey skOut;
//     libzcash::SaplingExtendedFullViewingKey extfvkOut;
//     libzcash::SaplingIncomingViewingKey ivkOut;

//     auto sk = GetTestMasterSaplingSpendingKey();
//     auto extfvk = sk.ToXFVK();
//     auto ivk = extfvk.ToIncomingViewingKey();
//     auto addr = sk.ToXFVK().DefaultAddress();

//     // Sanity-check: we can't get a full viewing key we haven't added
//     EXPECT_FALSE(keyStore.HaveSaplingFullViewingKey(ivk));
//     EXPECT_FALSE(keyStore.GetSaplingFullViewingKey(ivk, extfvkOut));

//     // and we shouldn't have a spending key or incoming viewing key either
//     EXPECT_FALSE(keyStore.HaveSaplingSpendingKey(extfvk));
//     EXPECT_FALSE(keyStore.GetSaplingSpendingKey(extfvk, skOut));
//     EXPECT_FALSE(keyStore.HaveSaplingIncomingViewingKey(addr));
//     EXPECT_FALSE(keyStore.GetSaplingIncomingViewingKey(addr, ivkOut));

//     // and we can't find the default address in our list of addresses
//     std::set<libzcash::SaplingPaymentAddress> addresses;
//     keyStore.GetSaplingPaymentAddresses(addresses);
//     EXPECT_FALSE(addresses.count(addr));

//     // When we add the full viewing key, we should have it
//     keyStore.AddSaplingFullViewingKey(extfvk);
//     EXPECT_TRUE(keyStore.HaveSaplingFullViewingKey(ivk));
//     EXPECT_TRUE(keyStore.GetSaplingFullViewingKey(ivk, extfvkOut));
//     EXPECT_EQ(extfvk, extfvkOut);

//     // We should still not have the spending key or
//     // be able to retrieve the IVK by the default address...
//     EXPECT_FALSE(keyStore.HaveSaplingSpendingKey(extfvk));
//     EXPECT_FALSE(keyStore.GetSaplingSpendingKey(extfvk, skOut));
//     EXPECT_FALSE(keyStore.HaveSaplingIncomingViewingKey(addr));

//     // The IVK must be manually associated with the address...
//     keyStore.AddSaplingPaymentAddress(ivk, addr);
//     EXPECT_TRUE(keyStore.HaveSaplingIncomingViewingKey(addr));
//     EXPECT_TRUE(keyStore.GetSaplingIncomingViewingKey(addr, ivkOut));
//     EXPECT_EQ(ivk, ivkOut);

//     // ... and we should find the default address in our list of addresses
//     addresses.clear();
//     keyStore.GetSaplingPaymentAddresses(addresses);
//     EXPECT_TRUE(addresses.count(addr));
// }

// #ifdef ENABLE_WALLET
// class TestCCryptoKeyStore : public CCryptoKeyStore
// {
// public:
//     bool EncryptKeys(CKeyingMaterial& vMasterKeyIn) { return CCryptoKeyStore::EncryptKeys(vMasterKeyIn); }
//     bool Unlock(const CKeyingMaterial& vMasterKeyIn) { return CCryptoKeyStore::Unlock(vMasterKeyIn); }
// };

// TEST(KeystoreTests, StoreAndRetrieveMnemonicSeedInEncryptedStore) {
//     TestCCryptoKeyStore keyStore;
//     CKeyingMaterial vMasterKey(32, 0);
//     GetRandBytes(vMasterKey.data(), 32);

//     // 1) Test adding a seed to an unencrypted key store, then encrypting it
//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     EXPECT_FALSE(keyStore.HaveMnemonicSeed());
//     auto seedOut = keyStore.GetMnemonicSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     ASSERT_TRUE(keyStore.SetMnemonicSeed(seed));
//     EXPECT_TRUE(keyStore.HaveMnemonicSeed());
//     seedOut = keyStore.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     ASSERT_TRUE(keyStore.EncryptKeys(vMasterKey));
//     seedOut = keyStore.GetMnemonicSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     // Unlocking with a random key causes sporadic failures, since we currently
//     // don't use an authenticated encryption scheme for CCryptoKeyStore.

//     // Currently, DecryptMnemonicSeed tests if a key is invalid by looking at
//     // the return value of CBCDecrypt. If keyStore.Unlock is called with an
//     // invalid key, there's roughly a 257/65536 chance that the padding check
//     // in CBCDecrypt will pass, in which case DecryptMnemonicSeed then calls
//     // the deserialization code in mnemonic.h with random data.

//     // Unlocking with vMasterKey should succeed
//     ASSERT_TRUE(keyStore.Unlock(vMasterKey));
//     seedOut = keyStore.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // 2) Test replacing the seed in an already-encrypted key store fails
//     auto seed2 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     EXPECT_FALSE(keyStore.SetMnemonicSeed(seed2));
//     EXPECT_TRUE(keyStore.HaveMnemonicSeed());
//     seedOut = keyStore.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // 3) Test adding a new seed to an already-encrypted key store
//     TestCCryptoKeyStore keyStore2;

//     // Add a Sprout address so the wallet has something to test when decrypting
//     ASSERT_TRUE(keyStore2.AddSproutSpendingKey(libzcash::SproutSpendingKey::random()));

//     ASSERT_TRUE(keyStore2.EncryptKeys(vMasterKey));
//     ASSERT_TRUE(keyStore2.Unlock(vMasterKey));

//     EXPECT_FALSE(keyStore2.HaveMnemonicSeed());
//     seedOut = keyStore2.GetMnemonicSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     auto seed3 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     ASSERT_TRUE(keyStore2.SetMnemonicSeed(seed3));
//     EXPECT_TRUE(keyStore2.HaveMnemonicSeed());
//     seedOut = keyStore2.GetMnemonicSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed3, seedOut.value());
// }

// TEST(KeystoreTests, StoreAndRetrieveLegacyHDSeedInEncryptedStore) {
//     TestCCryptoKeyStore keyStore;
//     CKeyingMaterial vMasterKey(32, 0);
//     GetRandBytes(vMasterKey.data(), 32);

//     // 1) Test adding a seed to an unencrypted key store, then encrypting it
//     // We use a mnemonic seed, then disregard the mnemonic itself.
//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     auto seedOut = keyStore.GetLegacyHDSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     ASSERT_TRUE(keyStore.SetLegacyHDSeed(seed));
//     seedOut = keyStore.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());

//     ASSERT_TRUE(keyStore.EncryptKeys(vMasterKey));
//     seedOut = keyStore.GetLegacyHDSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     // Unlocking with a random key should fail
//     CKeyingMaterial vRandomKey(32, 0);
//     GetRandBytes(vRandomKey.data(), 32);
//     EXPECT_FALSE(keyStore.Unlock(vRandomKey));

//     // Unlocking with a slightly-modified vMasterKey should fail
//     CKeyingMaterial vModifiedKey(vMasterKey);
//     vModifiedKey[0] += 1;
//     EXPECT_FALSE(keyStore.Unlock(vModifiedKey));

//     // Unlocking with vMasterKey should succeed
//     ASSERT_TRUE(keyStore.Unlock(vMasterKey));
//     seedOut = keyStore.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // 2) Test replacing the seed in an already-encrypted key store fails
//     auto seed2 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     EXPECT_FALSE(keyStore.SetLegacyHDSeed(seed2));
//     seedOut = keyStore.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed, seedOut.value());

//     // 3) Test adding a new seed to an already-encrypted key store
//     TestCCryptoKeyStore keyStore2;

//     // Add a Sprout address so the wallet has something to test when decrypting
//     ASSERT_TRUE(keyStore2.AddSproutSpendingKey(libzcash::SproutSpendingKey::random()));

//     ASSERT_TRUE(keyStore2.EncryptKeys(vMasterKey));
//     ASSERT_TRUE(keyStore2.Unlock(vMasterKey));

//     seedOut = keyStore2.GetLegacyHDSeed();
//     EXPECT_FALSE(seedOut.has_value());

//     auto seed3 = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     ASSERT_TRUE(keyStore2.SetLegacyHDSeed(seed3));
//     seedOut = keyStore2.GetLegacyHDSeed();
//     ASSERT_TRUE(seedOut.has_value());
//     EXPECT_EQ(seed3, seedOut.value());
// }

// TEST(KeystoreTests, StoreAndRetrieveSpendingKeyInEncryptedStore) {
//     TestCCryptoKeyStore keyStore;
//     uint256 r {GetRandHash()};
//     CKeyingMaterial vMasterKey (r.begin(), r.end());
//     libzcash::SproutSpendingKey keyOut;
//     ZCNoteDecryption decOut;
//     std::set<libzcash::SproutPaymentAddress> addrs;

//     // 1) Test adding a key to an unencrypted key store, then encrypting it
//     auto sk = libzcash::SproutSpendingKey::random();
//     auto addr = sk.address();
//     EXPECT_FALSE(keyStore.GetNoteDecryptor(addr, decOut));

//     keyStore.AddSproutSpendingKey(sk);
//     ASSERT_TRUE(keyStore.HaveSproutSpendingKey(addr));
//     ASSERT_TRUE(keyStore.GetSproutSpendingKey(addr, keyOut));
//     ASSERT_EQ(sk, keyOut);
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk.receiving_key()), decOut);

//     ASSERT_TRUE(keyStore.EncryptKeys(vMasterKey));
//     ASSERT_TRUE(keyStore.HaveSproutSpendingKey(addr));
//     ASSERT_FALSE(keyStore.GetSproutSpendingKey(addr, keyOut));
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk.receiving_key()), decOut);

//     // Unlocking with a random key should fail
//     uint256 r2 {GetRandHash()};
//     CKeyingMaterial vRandomKey (r2.begin(), r2.end());
//     EXPECT_FALSE(keyStore.Unlock(vRandomKey));

//     // Unlocking with a slightly-modified vMasterKey should fail
//     CKeyingMaterial vModifiedKey (r.begin(), r.end());
//     vModifiedKey[0] += 1;
//     EXPECT_FALSE(keyStore.Unlock(vModifiedKey));

//     // Unlocking with vMasterKey should succeed
//     ASSERT_TRUE(keyStore.Unlock(vMasterKey));
//     ASSERT_TRUE(keyStore.GetSproutSpendingKey(addr, keyOut));
//     ASSERT_EQ(sk, keyOut);

//     keyStore.GetSproutPaymentAddresses(addrs);
//     ASSERT_EQ(1, addrs.size());
//     ASSERT_EQ(1, addrs.count(addr));

//     // 2) Test adding a spending key to an already-encrypted key store
//     auto sk2 = libzcash::SproutSpendingKey::random();
//     auto addr2 = sk2.address();
//     EXPECT_FALSE(keyStore.GetNoteDecryptor(addr2, decOut));

//     keyStore.AddSproutSpendingKey(sk2);
//     ASSERT_TRUE(keyStore.HaveSproutSpendingKey(addr2));
//     ASSERT_TRUE(keyStore.GetSproutSpendingKey(addr2, keyOut));
//     ASSERT_EQ(sk2, keyOut);
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr2, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk2.receiving_key()), decOut);

//     ASSERT_TRUE(keyStore.Lock());
//     ASSERT_TRUE(keyStore.HaveSproutSpendingKey(addr2));
//     ASSERT_FALSE(keyStore.GetSproutSpendingKey(addr2, keyOut));
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr2, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk2.receiving_key()), decOut);

//     ASSERT_TRUE(keyStore.Unlock(vMasterKey));
//     ASSERT_TRUE(keyStore.GetSproutSpendingKey(addr2, keyOut));
//     ASSERT_EQ(sk2, keyOut);
//     EXPECT_TRUE(keyStore.GetNoteDecryptor(addr2, decOut));
//     EXPECT_EQ(ZCNoteDecryption(sk2.receiving_key()), decOut);

//     keyStore.GetSproutPaymentAddresses(addrs);
//     ASSERT_EQ(2, addrs.size());
//     ASSERT_EQ(1, addrs.count(addr));
//     ASSERT_EQ(1, addrs.count(addr2));
// }

// TEST(KeystoreTests, StoreAndRetrieveUFVK) {
//     SelectParams(CBaseChainParams::TESTNET);
//     CBasicKeyStore keyStore;

//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     auto usk = ZcashdUnifiedSpendingKey::ForAccount(seed, SLIP44_TESTNET_TYPE, 0);
//     EXPECT_TRUE(usk.has_value());

//     auto ufvk = usk.value().ToFullViewingKey();
//     auto zufvk = ZcashdUnifiedFullViewingKey::FromUnifiedFullViewingKey(Params(), ufvk);
//     auto ufvkid = zufvk.GetKeyID();
//     EXPECT_FALSE(keyStore.GetUnifiedFullViewingKey(ufvkid).has_value());

//     EXPECT_TRUE(keyStore.AddUnifiedFullViewingKey(zufvk));
//     EXPECT_EQ(keyStore.GetUnifiedFullViewingKey(ufvkid).value(), zufvk);

//     auto addrPair = std::get<std::pair<UnifiedAddress, diversifier_index_t>>(zufvk.FindAddress(diversifier_index_t(0), {ReceiverType::Sapling}));
//     EXPECT_TRUE(addrPair.first.GetSaplingReceiver().has_value());
//     auto saplingReceiver = addrPair.first.GetSaplingReceiver().value();

//     // We detect this even though we haven't added the Sapling address, because
//     // we trial-decrypt diversifiers (which also means we learn the index).
//     auto ufvkmetaUnadded = keyStore.GetUFVKMetadataForReceiver(saplingReceiver);
//     EXPECT_TRUE(ufvkmetaUnadded.has_value());
//     EXPECT_EQ(ufvkmetaUnadded.value().GetUFVKId(), ufvkid);
//     EXPECT_EQ(ufvkmetaUnadded.value().GetDiversifierIndex(), addrPair.second);

//     // Adding the Sapling addr -> ivk map entry causes us to find the same UFVK,
//     // and since we trial-decrypt with both external and internal IVKs to
//     // verify whether it's an internal address, we learn the index.
//     auto saplingIvk = zufvk.GetSaplingKey().value().ToIncomingViewingKey();
//     keyStore.AddSaplingPaymentAddress(saplingIvk, saplingReceiver);

//     auto ufvkmeta = keyStore.GetUFVKMetadataForReceiver(saplingReceiver);
//     EXPECT_TRUE(ufvkmeta.has_value());
//     EXPECT_EQ(ufvkmeta.value().GetUFVKId(), ufvkid);
//     EXPECT_EQ(ufvkmeta.value().GetDiversifierIndex(), addrPair.second);
// }

// TEST(KeystoreTests, StoreAndRetrieveUFVKByOrchard) {
//     SelectParams(CBaseChainParams::TESTNET);
//     CBasicKeyStore keyStore;

//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     auto usk = ZcashdUnifiedSpendingKey::ForAccount(seed, SLIP44_TESTNET_TYPE, 0);
//     EXPECT_TRUE(usk.has_value());

//     auto ufvk = usk.value().ToFullViewingKey();
//     auto zufvk = ZcashdUnifiedFullViewingKey::FromUnifiedFullViewingKey(Params(), ufvk);
//     auto ufvkid = zufvk.GetKeyID();
//     EXPECT_FALSE(keyStore.GetUnifiedFullViewingKey(ufvkid).has_value());

//     EXPECT_TRUE(keyStore.AddUnifiedFullViewingKey(zufvk));
//     EXPECT_EQ(keyStore.GetUnifiedFullViewingKey(ufvkid).value(), zufvk);

//     auto addrPair = std::get<std::pair<UnifiedAddress, diversifier_index_t>>(zufvk.FindAddress(diversifier_index_t(0), {ReceiverType::Orchard}));
//     EXPECT_TRUE(addrPair.first.GetOrchardReceiver().has_value());
//     auto orchardReceiver = addrPair.first.GetOrchardReceiver().value();

//     // We don't store Orchard addresses in CBasicKeyStore (the addr -> ivk
//     // mapping is stored in the Rust wallet), but we still detect this because
//     // we trial-decrypt diversifiers (which also means we learn the index).
//     auto ufvkmetaUnadded = keyStore.GetUFVKMetadataForReceiver(orchardReceiver);
//     EXPECT_TRUE(ufvkmetaUnadded.has_value());
//     EXPECT_EQ(ufvkmetaUnadded.value().GetUFVKId(), ufvkid);
//     EXPECT_EQ(ufvkmetaUnadded.value().GetDiversifierIndex(), addrPair.second);
// }

// TEST(KeystoreTests, AddTransparentReceiverForUnifiedAddress) {
//     SelectParams(CBaseChainParams::TESTNET);
//     CBasicKeyStore keyStore;

//     auto seed = MnemonicSeed::Random(SLIP44_TESTNET_TYPE);
//     auto usk = ZcashdUnifiedSpendingKey::ForAccount(seed, SLIP44_TESTNET_TYPE, 0);
//     EXPECT_TRUE(usk.has_value());

//     auto ufvk = usk.value().ToFullViewingKey();
//     auto zufvk = ZcashdUnifiedFullViewingKey::FromUnifiedFullViewingKey(Params(), ufvk);
//     auto ufvkid = zufvk.GetKeyID();
//     auto addrPair = std::get<std::pair<UnifiedAddress, diversifier_index_t>>(zufvk.FindAddress(diversifier_index_t(0), {ReceiverType::P2PKH, ReceiverType::Sapling}));
//     EXPECT_TRUE(addrPair.first.GetP2PKHReceiver().has_value());
//     auto ufvkmeta = keyStore.GetUFVKMetadataForReceiver(addrPair.first.GetP2PKHReceiver().value());
//     EXPECT_FALSE(ufvkmeta.has_value());

//     keyStore.AddTransparentReceiverForUnifiedAddress(ufvkid, addrPair.second, addrPair.first);

//     ufvkmeta = keyStore.GetUFVKMetadataForReceiver(addrPair.first.GetP2PKHReceiver().value());
//     EXPECT_TRUE(ufvkmeta.has_value());
//     EXPECT_EQ(ufvkmeta.value().GetUFVKId(), ufvkid);
// }


// #endif
