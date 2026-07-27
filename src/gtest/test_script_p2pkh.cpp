// Copyright (c) 2012-2015 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/script.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

// Tests CScript::IsPayToPublicKeyHash() pattern matching against well-formed
// and malformed scripts. Covers the TRANSPARENT pool specifically.

class script_P2PKH_tests : public BitcoinBasicTestingSetup {};

TEST_F(script_P2PKH_tests, IsPayToPublicKeyHash)
{
    // Test CScript::IsPayToPublicKeyHash()
    uint160 dummy;
    CScript p2pkh;
    p2pkh << OP_DUP << OP_HASH160 << ToByteVector(dummy) << OP_EQUALVERIFY << OP_CHECKSIG;
    EXPECT_TRUE(p2pkh.IsPayToPublicKeyHash());

    static const unsigned char direct[] = {
        OP_DUP, OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUALVERIFY, OP_CHECKSIG
    };
    EXPECT_TRUE(CScript(direct, direct+sizeof(direct)).IsPayToPublicKeyHash());

    static const unsigned char notp2pkh1[] = {
        OP_DUP, OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUALVERIFY, OP_CHECKSIG, OP_CHECKSIG
    };
    EXPECT_FALSE(CScript(notp2pkh1, notp2pkh1+sizeof(notp2pkh1)).IsPayToPublicKeyHash());

    static const unsigned char p2sh[] = {
        OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUAL
    };
    EXPECT_FALSE(CScript(p2sh, p2sh+sizeof(p2sh)).IsPayToPublicKeyHash());

    static const unsigned char extra[] = {
        OP_DUP, OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUALVERIFY, OP_CHECKSIG, OP_CHECKSIG
    };
    EXPECT_FALSE(CScript(extra, extra+sizeof(extra)).IsPayToPublicKeyHash());

    static const unsigned char missing[] = {
        OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, OP_EQUALVERIFY, OP_CHECKSIG, OP_RETURN
    };
    EXPECT_FALSE(CScript(missing, missing+sizeof(missing)).IsPayToPublicKeyHash());

    static const unsigned char missing2[] = {
        OP_DUP, OP_HASH160, 20, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
    };
    EXPECT_FALSE(CScript(missing2, missing2+sizeof(missing2)).IsPayToPublicKeyHash());

    static const unsigned char tooshort[] = {
        OP_DUP, OP_HASH160, 2, 0,0, OP_EQUALVERIFY, OP_CHECKSIG
    };
    EXPECT_FALSE(CScript(tooshort, tooshort+sizeof(tooshort)).IsPayToPublicKeyHash());
}
