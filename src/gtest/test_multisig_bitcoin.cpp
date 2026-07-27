// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "consensus/upgrades.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "uint256.h"
#include "gtest/gtestutils.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet_ismine.h"
#endif

#include <gtest/gtest.h>

// Tests multisig script verification, IsStandard() classification, and
// signing. This fork allows up to x-of-9 multisig as standard (not
// upstream's x-of-3 limit). Covers the TRANSPARENT pool specifically, not
// Sapling/Ironwood/Sprout shielded logic.

using namespace std;

class multisig_tests_bitcoin : public BitcoinBasicTestingSetup {};

static CScript
sign_multisig(CScript scriptPubKey, vector<CKey> keys, CMutableTransaction transaction, int whichIn, uint32_t consensusBranchId)
{
    std::vector<CTxOut> allPrevOutputs;
    allPrevOutputs.resize(transaction.vin.size());
    PrecomputedTransactionData txdata(transaction, allPrevOutputs);
    uint256 hash = SignatureHash(scriptPubKey, transaction, whichIn, SIGHASH_ALL, 0, consensusBranchId, txdata);

    CScript result;
    result << OP_0; // CHECKMULTISIG bug workaround
    for (const CKey &key : keys)
    {
        vector<unsigned char> vchSig;
        EXPECT_TRUE(key.Sign(hash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);
        result << vchSig;
    }
    return result;
}

// Parameterized testing over consensus branch ids
TEST_F(multisig_tests_bitcoin, multisig_verify)
{
    for (int sample = 0; sample < static_cast<int>(Consensus::MAX_NETWORK_UPGRADES); sample++) {
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;
    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_STRICTENC;

    ScriptError err;
    CKey key[4];
    CAmount amount = 0;
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[0].nValue = 0;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[1].nValue = 0;
    txFrom.vout[2].scriptPubKey = escrow;
    txFrom.vout[2].nValue = 0;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    std::vector<CTxOut> allPrevOutputs0(txTo[0].vin.size());
    PrecomputedTransactionData txdata0(txTo[0], allPrevOutputs0);
    std::vector<CTxOut> allPrevOutputs1(txTo[1].vin.size());
    PrecomputedTransactionData txdata1(txTo[1], allPrevOutputs1);
    std::vector<CTxOut> allPrevOutputs2(txTo[2].vin.size());
    PrecomputedTransactionData txdata2(txTo[2], allPrevOutputs2);

    vector<CKey> keys;
    CScript s;

    // Test a AND b:
    keys.assign(1,key[0]);
    keys.push_back(key[1]);
    s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
    EXPECT_TRUE(VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], txdata0, 0, amount), consensusBranchId, &err));
    EXPECT_TRUE(err == SCRIPT_ERR_OK) << ScriptErrorString(err);

    for (int i = 0; i < 4; i++)
    {
        keys.assign(1,key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
        EXPECT_TRUE(!VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], txdata0, 0, amount), consensusBranchId, &err)) << strprintf("a&b 1: %d", i);
        EXPECT_TRUE(err == SCRIPT_ERR_INVALID_STACK_OPERATION) << ScriptErrorString(err);

        keys.assign(1,key[1]);
        keys.push_back(key[i]);
        s = sign_multisig(a_and_b, keys, txTo[0], 0, consensusBranchId);
        EXPECT_TRUE(!VerifyScript(s, a_and_b, flags, MutableTransactionSignatureChecker(&txTo[0], txdata0, 0, amount), consensusBranchId, &err)) << strprintf("a&b 2: %d", i);
        EXPECT_TRUE(err == SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);
    }

    // Test a OR b:
    for (int i = 0; i < 4; i++)
    {
        keys.assign(1,key[i]);
        s = sign_multisig(a_or_b, keys, txTo[1], 0, consensusBranchId);
        if (i == 0 || i == 1)
        {
            EXPECT_TRUE(VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], txdata1, 0, amount), consensusBranchId, &err)) << strprintf("a|b: %d", i);
            EXPECT_TRUE(err == SCRIPT_ERR_OK) << ScriptErrorString(err);
        }
        else
        {
            EXPECT_TRUE(!VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], txdata1, 0, amount), consensusBranchId, &err)) << strprintf("a|b: %d", i);
            EXPECT_TRUE(err == SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);
        }
    }
    s.clear();
    s << OP_0 << OP_1;
    EXPECT_TRUE(!VerifyScript(s, a_or_b, flags, MutableTransactionSignatureChecker(&txTo[1], txdata1, 0, amount), consensusBranchId, &err));
    EXPECT_TRUE(err == SCRIPT_ERR_SIG_DER) << ScriptErrorString(err);


    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
        {
            keys.assign(1,key[i]);
            keys.push_back(key[j]);
            s = sign_multisig(escrow, keys, txTo[2], 0, consensusBranchId);
            if (i < j && i < 3 && j < 3)
            {
                EXPECT_TRUE(VerifyScript(s, escrow, flags, MutableTransactionSignatureChecker(&txTo[2], txdata2, 0, amount), consensusBranchId, &err)) << strprintf("escrow 1: %d %d", i, j);
                EXPECT_TRUE(err == SCRIPT_ERR_OK) << ScriptErrorString(err);
            }
            else
            {
                EXPECT_TRUE(!VerifyScript(s, escrow, flags, MutableTransactionSignatureChecker(&txTo[2], txdata2, 0, amount), consensusBranchId, &err)) << strprintf("escrow 2: %d %d", i, j);
                EXPECT_TRUE(err == SCRIPT_ERR_EVAL_FALSE) << ScriptErrorString(err);
            }
        }
    }
}

TEST_F(multisig_tests_bitcoin, multisig_IsStandard)
{
    CKey key[4];
    for (int i = 0; i < 4; i++)
        key[i].MakeNewKey(true);

    txnouttype whichType;

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(a_and_b, whichType));

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(a_or_b, whichType));

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(escrow, whichType));

    // This fork's IsStandard() (script/standard.cpp) allows up to x-of-9
    // multisig as standard, not upstream Bitcoin's x-of-3 limit, so 1-of-4
    // is standard here.
    CScript one_of_four;
    one_of_four << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << ToByteVector(key[3].GetPubKey()) << OP_4 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(one_of_four, whichType));

    // Boundary of the x-of-9 standardness limit itself (script/standard.cpp:
    // `if (n < 1 || n > 9) return false;`): 9-of-9 must still be standard,
    // one more (10-of-10, below) must not - pins the exact cutoff rather
    // than just a value comfortably on each side of it.
    CScript nine_of_nine;
    nine_of_nine << OP_9;
    for (int i = 0; i < 9; i++)
        nine_of_nine << ToByteVector(key[i % 4].GetPubKey());
    nine_of_nine << OP_9 << OP_CHECKMULTISIG;
    EXPECT_TRUE(::IsStandard(nine_of_nine, whichType));

    CScript one_of_ten;
    one_of_ten << OP_1;
    for (int i = 0; i < 10; i++)
        one_of_ten << ToByteVector(key[i % 4].GetPubKey());
    one_of_ten << OP_10 << OP_CHECKMULTISIG;
    EXPECT_TRUE(!::IsStandard(one_of_ten, whichType));

    CScript malformed[6];
    malformed[0] << OP_3 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[1] << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;
    malformed[2] << OP_0 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;
    malformed[3] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_0 << OP_CHECKMULTISIG;
    malformed[4] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_CHECKMULTISIG;
    malformed[5] << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey());

    for (int i = 0; i < 6; i++)
        EXPECT_TRUE(!::IsStandard(malformed[i], whichType));
}

// Parameterized testing over consensus branch ids
TEST_F(multisig_tests_bitcoin, multisig_Sign)
{
    for (int sample = 0; sample < static_cast<int>(Consensus::MAX_NETWORK_UPGRADES); sample++) {
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    // Test SignSignature() (and therefore the version of Solver() that signs transactions)
    CBasicKeyStore keystore;
    CKey key[4];
    for (int i = 0; i < 4; i++)
    {
        key[i].MakeNewKey(true);
        keystore.AddKey(key[i]);
    }

    CScript a_and_b;
    a_and_b << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript a_or_b;
    a_or_b  << OP_1 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << OP_2 << OP_CHECKMULTISIG;

    CScript escrow;
    escrow << OP_2 << ToByteVector(key[0].GetPubKey()) << ToByteVector(key[1].GetPubKey()) << ToByteVector(key[2].GetPubKey()) << OP_3 << OP_CHECKMULTISIG;

    CMutableTransaction txFrom;  // Funding transaction
    txFrom.vout.resize(3);
    txFrom.vout[0].scriptPubKey = a_and_b;
    txFrom.vout[0].nValue = 0;
    txFrom.vout[1].scriptPubKey = a_or_b;
    txFrom.vout[1].nValue = 0;
    txFrom.vout[2].scriptPubKey = escrow;
    txFrom.vout[2].nValue = 0;

    CMutableTransaction txTo[3]; // Spending transaction
    for (int i = 0; i < 3; i++)
    {
        txTo[i].vin.resize(1);
        txTo[i].vout.resize(1);
        txTo[i].vin[0].prevout.n = i;
        txTo[i].vin[0].prevout.hash = txFrom.GetHash();
        txTo[i].vout[0].nValue = 1;
    }

    for (int i = 0; i < 3; i++)
    {
        std::vector<CTxOut> allPrevOutputs(txTo[i].vin.size());
        PrecomputedTransactionData txdata(txTo[i], allPrevOutputs);
        EXPECT_TRUE(SignSignature(keystore, CTransaction(txFrom), txTo[i], txdata, 0, SIGHASH_ALL, consensusBranchId)) << strprintf("SignSignature %d", i);
    }
    }
}
