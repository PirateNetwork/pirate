// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//

#include "consensus/upgrades.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"

#include "gtest/gtestutils.h"

#include <stdint.h>

#include <gtest/gtest.h>

// Tests this internal-to-main.cpp method:
extern bool AddOrphanTx(const CTransaction& tx, NodeId peer);
extern void EraseOrphansFor(NodeId peer);
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};
extern std::map<uint256, COrphanTx> mapOrphanTransactions;
extern std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;

static CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

class DoS_tests_bitcoin : public BitcoinTestingSetup {};

TEST_F(DoS_tests_bitcoin, DoS_banning)
{
    CNode::ClearBanned();
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    // This fork's default -banscore threshold is 101 (main.cpp GetArg("-banscore", 101)),
    // not 100 as the original test assumed - misbehaving exactly 100 falls short of it.
    Misbehaving(dummyNode1.GetId(), 101); // Should get banned
    SendMessages(&dummyNode1, false);
    EXPECT_TRUE(CNode::IsBanned(addr1));
    EXPECT_TRUE(!CNode::IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002));
    CNode dummyNode2(INVALID_SOCKET, addr2, "", true);
    dummyNode2.nVersion = 1;
    Misbehaving(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2, false);
    EXPECT_TRUE(!CNode::IsBanned(addr2)); // 2 not banned yet...
    EXPECT_TRUE(CNode::IsBanned(addr1));  // ... but 1 still should be
    // 51, not 50: this fork's default -banscore threshold is 101 (see DoS_banning's
    // top comment), so 50+50=100 falls one point short of tripping the ban.
    Misbehaving(dummyNode2.GetId(), 51);
    SendMessages(&dummyNode2, false);
    EXPECT_TRUE(CNode::IsBanned(addr2));
}

TEST_F(DoS_tests_bitcoin, DoS_banscore)
{
    CNode::ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001));
    CNode dummyNode1(INVALID_SOCKET, addr1, "", true);
    dummyNode1.nVersion = 1;
    Misbehaving(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1, false);
    EXPECT_TRUE(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1, false);
    EXPECT_TRUE(!CNode::IsBanned(addr1));
    Misbehaving(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1, false);
    EXPECT_TRUE(CNode::IsBanned(addr1));
    mapArgs.erase("-banscore");
}

TEST_F(DoS_tests_bitcoin, DoS_bantime)
{
    CNode::ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = 1;

    // See DoS_banning: this fork's default -banscore threshold is 101, not 100.
    Misbehaving(dummyNode.GetId(), 101);
    SendMessages(&dummyNode, false);
    EXPECT_TRUE(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    EXPECT_TRUE(CNode::IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    EXPECT_TRUE(!CNode::IsBanned(addr));
}

static CTransaction RandomOrphan()
{
    std::map<uint256, COrphanTx>::iterator it;
    it = mapOrphanTransactions.lower_bound(GetRandHash());
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();
    return it->second.tx;
}

// Parameterized testing over consensus branch ids
TEST_F(DoS_tests_bitcoin, DoS_mapOrphans)
{
    for (int sample = 0; sample < static_cast<int>(Consensus::MAX_NETWORK_UPGRADES); sample++) {
    uint32_t consensusBranchId = NetworkUpgradeInfo[sample].nBranchId;

    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        std::vector<CTxOut> allPrevOutputs;
        PrecomputedTransactionData txdata(CTransaction(tx), allPrevOutputs);
        SignSignature(keystore, txPrev, tx, txdata, 0, SIGHASH_ALL, consensusBranchId);

        AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        std::vector<CTxOut> allPrevOutputs;
        PrecomputedTransactionData txdata(CTransaction(tx), allPrevOutputs);
        SignSignature(keystore, txPrev, tx, txdata, 0, SIGHASH_ALL, consensusBranchId);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        EXPECT_TRUE(!AddOrphanTx(tx, i));
    }

    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        size_t sizeBefore = mapOrphanTransactions.size();
        EraseOrphansFor(i);
        EXPECT_TRUE(mapOrphanTransactions.size() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    LimitOrphanTxSize(40);
    EXPECT_TRUE(mapOrphanTransactions.size() <= 40);
    LimitOrphanTxSize(10);
    EXPECT_TRUE(mapOrphanTransactions.size() <= 10);
    LimitOrphanTxSize(0);
    EXPECT_TRUE(mapOrphanTransactions.empty());
    EXPECT_TRUE(mapOrphanTransactionsByPrev.empty());
    }
}
