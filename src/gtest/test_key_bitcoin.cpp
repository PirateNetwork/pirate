// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"

#include "chainparams.h"
#include "key_io.h"
#include "script/script.h"
#include "uint256.h"
#include "util.h"
#include "util/strencodings.h"
#include "gtest/gtestutils.h"

#include "zcash/Address.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace libzcash;

// Named _bitcoin: gtest/test_keys.cpp already covers Sapling key/address
// encode-decode round trips (Keys.EncodeAndDecodeSapling); this file only
// ports key_test1, the raw secp256k1 WIF/sign/compact-recovery coverage that
// isn't duplicated anywhere else. The original file's zc_address_test (Sprout
// key/address creation) and zs_address_test (Sapling, duplicate of the above)
// were not ported: zc_address_test also no longer compiles as written (it
// passes the std::variant by value instead of by pointer to std::get_if in
// several spots) and Sprout note/address creation isn't something this fork
// does for new transactions.
//
// key_test1's original hardcoded WIF secrets/addresses/deterministic-signature
// hex were vanilla-Bitcoin-mainnet literals (base58 prefixes 128/0). This
// fork's actual MAIN params use different prefixes (SECRET_KEY=188,
// PUBKEY_ADDRESS=60, chainparams.cpp), so those literals don't decode under
// CBaseChainParams::MAIN and the address/signature values don't apply here -
// not a typo, just cross-fork incompatible. Rewritten to generate fresh raw
// key material and verify the same properties (WIF/address round-trip,
// compressed/uncompressed pairing, sign/verify, compact-sig recovery,
// same-key deterministic-signature equality) against this fork's own prefixes
// instead of reproducing another chain's literal test vectors.
class key_tests : public BitcoinBasicTestingSetup {};

static const std::string strAddressBad = "t1aMkLwU1LcMZYN7TgXUJAwzA1r44dbLkSp";

static void MakeKeyPair(CKey& uncompressed, CKey& compressed)
{
    CKey seed;
    seed.MakeNewKey(true);
    uncompressed.Set(seed.begin(), seed.end(), false);
    compressed.Set(seed.begin(), seed.end(), true);
}

TEST_F(key_tests, key_test1)
{
    CKey key1, key1C, key2, key2C;
    MakeKeyPair(key1, key1C);
    MakeKeyPair(key2, key2C);
    EXPECT_TRUE(key1.IsValid() && !key1.IsCompressed());
    EXPECT_TRUE(key2.IsValid() && !key2.IsCompressed());
    EXPECT_TRUE(key1C.IsValid() && key1C.IsCompressed());
    EXPECT_TRUE(key2C.IsValid() && key2C.IsCompressed());
    CKey bad_key = DecodeSecret(strAddressBad);
    EXPECT_TRUE(!bad_key.IsValid());

    // WIF round-trip, under this fork's own prefixes
    EXPECT_TRUE(DecodeSecret(EncodeSecret(key1)) == key1);
    EXPECT_TRUE(DecodeSecret(EncodeSecret(key2)) == key2);
    EXPECT_TRUE(DecodeSecret(EncodeSecret(key1C)) == key1C);
    EXPECT_TRUE(DecodeSecret(EncodeSecret(key2C)) == key2C);

    CPubKey pubkey1  = key1. GetPubKey();
    CPubKey pubkey2  = key2. GetPubKey();
    CPubKey pubkey1C = key1C.GetPubKey();
    CPubKey pubkey2C = key2C.GetPubKey();

    EXPECT_TRUE(key1.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key1.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey1));
    EXPECT_TRUE(key1C.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key1C.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key2.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key2.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(key2.VerifyPubKey(pubkey2));
    EXPECT_TRUE(!key2.VerifyPubKey(pubkey2C));

    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey1));
    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey1C));
    EXPECT_TRUE(!key2C.VerifyPubKey(pubkey2));
    EXPECT_TRUE(key2C.VerifyPubKey(pubkey2C));

    // Address round-trip, under this fork's own prefixes
    EXPECT_TRUE(DecodeDestination(EncodeDestination(pubkey1.GetID()))  == CTxDestination(pubkey1.GetID()));
    EXPECT_TRUE(DecodeDestination(EncodeDestination(pubkey2.GetID()))  == CTxDestination(pubkey2.GetID()));
    EXPECT_TRUE(DecodeDestination(EncodeDestination(pubkey1C.GetID())) == CTxDestination(pubkey1C.GetID()));
    EXPECT_TRUE(DecodeDestination(EncodeDestination(pubkey2C.GetID())) == CTxDestination(pubkey2C.GetID()));

    for (int n=0; n<16; n++)
    {
        std::string strMsg = strprintf("Very secret message %i: 11", n);
        uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());

        // normal signatures

        std::vector<unsigned char> sign1, sign2, sign1C, sign2C;

        EXPECT_TRUE(key1.Sign (hashMsg, sign1));
        EXPECT_TRUE(key2.Sign (hashMsg, sign2));
        EXPECT_TRUE(key1C.Sign(hashMsg, sign1C));
        EXPECT_TRUE(key2C.Sign(hashMsg, sign2C));

        EXPECT_TRUE( pubkey1.Verify(hashMsg, sign1));
        EXPECT_TRUE(!pubkey1.Verify(hashMsg, sign2));
        EXPECT_TRUE( pubkey1.Verify(hashMsg, sign1C));
        EXPECT_TRUE(!pubkey1.Verify(hashMsg, sign2C));

        EXPECT_TRUE(!pubkey2.Verify(hashMsg, sign1));
        EXPECT_TRUE( pubkey2.Verify(hashMsg, sign2));
        EXPECT_TRUE(!pubkey2.Verify(hashMsg, sign1C));
        EXPECT_TRUE( pubkey2.Verify(hashMsg, sign2C));

        EXPECT_TRUE( pubkey1C.Verify(hashMsg, sign1));
        EXPECT_TRUE(!pubkey1C.Verify(hashMsg, sign2));
        EXPECT_TRUE( pubkey1C.Verify(hashMsg, sign1C));
        EXPECT_TRUE(!pubkey1C.Verify(hashMsg, sign2C));

        EXPECT_TRUE(!pubkey2C.Verify(hashMsg, sign1));
        EXPECT_TRUE( pubkey2C.Verify(hashMsg, sign2));
        EXPECT_TRUE(!pubkey2C.Verify(hashMsg, sign1C));
        EXPECT_TRUE( pubkey2C.Verify(hashMsg, sign2C));

        // compact signatures (with key recovery)

        std::vector<unsigned char> csign1, csign2, csign1C, csign2C;

        EXPECT_TRUE(key1.SignCompact (hashMsg, csign1));
        EXPECT_TRUE(key2.SignCompact (hashMsg, csign2));
        EXPECT_TRUE(key1C.SignCompact(hashMsg, csign1C));
        EXPECT_TRUE(key2C.SignCompact(hashMsg, csign2C));

        CPubKey rkey1, rkey2, rkey1C, rkey2C;

        EXPECT_TRUE(rkey1.RecoverCompact (hashMsg, csign1));
        EXPECT_TRUE(rkey2.RecoverCompact (hashMsg, csign2));
        EXPECT_TRUE(rkey1C.RecoverCompact(hashMsg, csign1C));
        EXPECT_TRUE(rkey2C.RecoverCompact(hashMsg, csign2C));

        EXPECT_TRUE(rkey1  == pubkey1);
        EXPECT_TRUE(rkey2  == pubkey2);
        EXPECT_TRUE(rkey1C == pubkey1C);
        EXPECT_TRUE(rkey2C == pubkey2C);
    }

    // test deterministic signing: same underlying secret, compressed or not,
    // must produce the same RFC6979 signature (the flag only affects pubkey
    // serialization, not the scalar being signed with).
    std::vector<unsigned char> detsig, detsigc;
    std::string strMsg = "Very deterministic message";
    uint256 hashMsg = Hash(strMsg.begin(), strMsg.end());
    EXPECT_TRUE(key1.Sign(hashMsg, detsig));
    EXPECT_TRUE(key1C.Sign(hashMsg, detsigc));
    EXPECT_TRUE(detsig == detsigc);
    EXPECT_TRUE(key2.Sign(hashMsg, detsig));
    EXPECT_TRUE(key2C.Sign(hashMsg, detsigc));
    EXPECT_TRUE(detsig == detsigc);
    EXPECT_TRUE(key1.SignCompact(hashMsg, detsig));
    EXPECT_TRUE(key1C.SignCompact(hashMsg, detsigc));
    // Compact signatures encode a recovery id in the header byte that depends
    // on the pubkey's compression flag, so only the 64-byte R||S body must
    // match between the compressed and uncompressed variants.
    ASSERT_EQ(detsig.size(), 65u);
    ASSERT_EQ(detsigc.size(), 65u);
    EXPECT_TRUE(std::vector<unsigned char>(detsig.begin()+1, detsig.end()) ==
                std::vector<unsigned char>(detsigc.begin()+1, detsigc.end()));
    EXPECT_TRUE(key2.SignCompact(hashMsg, detsig));
    EXPECT_TRUE(key2C.SignCompact(hashMsg, detsigc));
    ASSERT_EQ(detsig.size(), 65u);
    ASSERT_EQ(detsigc.size(), 65u);
    EXPECT_TRUE(std::vector<unsigned char>(detsig.begin()+1, detsig.end()) ==
                std::vector<unsigned char>(detsigc.begin()+1, detsigc.end()));
}
