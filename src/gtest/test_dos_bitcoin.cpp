// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//

#include "addrman.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "hash.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "pow.h"
#include "primitives/block.h"
#include "protocol.h"
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

// InjectMessage()/GetMisbehavior() are shared helpers declared in
// gtest/gtestutils.h (also used by other message-handler regression tests).

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

// Regression coverage for treasurechest_attack_vectors.md #6 / dos_vulnerability_analysis.md
// VULN-05: INV declaring a count above MAX_INV_SZ must be rejected (Misbehaving+20)
// before the oversized vector is allocated/decoded.
TEST_F(DoS_tests_bitcoin, DoS_InvSizeCap)
{
    CAddress addr(ip(0xa0b0c001));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, MAX_INV_SZ + 1);
    InjectMessage(&dummyNode, NetMsgType::INV, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 20);
}

// Same fix, GETDATA handler (treasurechest_attack_vectors.md #6).
TEST_F(DoS_tests_bitcoin, DoS_GetDataSizeCap)
{
    CAddress addr(ip(0xa0b0c002));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, MAX_INV_SZ + 1);
    InjectMessage(&dummyNode, NetMsgType::GETDATA, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 20);
}

// Same fix, ADDR handler (cap is 1000, not MAX_INV_SZ).
TEST_F(DoS_tests_bitcoin, DoS_AddrSizeCap)
{
    CAddress addr(ip(0xa0b0c003));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, 1001);
    InjectMessage(&dummyNode, NetMsgType::ADDR, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 20);
}

// Regression test found during a networking review: the ADDRV2 handler used
// to gate storage on IsReachable(addr) ("Do not store addresses outside our
// network"), so an IPv4-only node (Tor/I2P unreachable) discarded every
// Tor/I2P/CJDNS address it was ever gossiped, on receipt, before it ever
// reached addrman - meaning such a node could never learn, and therefore
// could never re-relay, a single address on those networks. That defeats
// address propagation for them entirely: addrman.Select()/GetAddr() already
// filter by reachability at use time (see GetReachabilityFrom() in
// netaddress.cpp), so gating storage the same way here was pure data loss,
// not a meaningful protection. Simulate an IPv4-only node (onion explicitly
// marked unreachable) receiving a real onion address via ADDRV2 and confirm
// it's actually stored.
TEST_F(DoS_tests_bitcoin, AddrHandler_StoresNonReachableNetworkAddresses)
{
    SetReachable(NET_ONION, false);
    struct ReachabilityGuard {
        ~ReachabilityGuard() { SetReachable(NET_ONION, true); }
    } reachabilityGuard;

    CNetAddr onion;
    ASSERT_TRUE(onion.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    ASSERT_FALSE(IsReachable(onion)) << "test setup: onion must be unreachable for this test to mean anything";

    CAddress onionAddr(CService(onion, 8233), NODE_NETWORK);
    onionAddr.nTime = GetTime();

    CAddress peerAddr(ip(0xa0b0c010));
    CNode dummyNode(INVALID_SOCKET, peerAddr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    WriteCompactSize(payload, 1);
    payload << onionAddr;
    InjectMessage(&dummyNode, NetMsgType::ADDRV2, payload);

    std::map<std::string, int64_t> peers;
    addrman.GetAllPeers(peers);
    EXPECT_TRUE(peers.count(onionAddr.ToStringIPPort()) > 0)
        << "an unreachable-network address must still be stored, so it can later be "
           "relayed to a peer who can actually use it";
}

// treasurechest_attack_vectors.md #6 / VULN-04: GETBLOCKS locator over
// MAX_LOCATOR_SZ=101 entries must be rejected (Misbehaving+20).
TEST_F(DoS_tests_bitcoin, DoS_GetBlocksLocatorCap)
{
    CAddress addr(ip(0xa0b0c004));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CBlockLocator locator(std::vector<uint256>(102, GetRandHash()));
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETBLOCKS, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 20);
}

// Same fix, GETHEADERS handler.
TEST_F(DoS_tests_bitcoin, DoS_GetHeadersLocatorCap)
{
    CAddress addr(ip(0xa0b0c005));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    CBlockLocator locator(std::vector<uint256>(102, GetRandHash()));
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETHEADERS, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 20);
}

// VULN-04: GETBLOCKS is rate-limited to one per second per peer. A second
// request inside the window must be silently dropped (no nLastGetBlocksRecv
// update); a request outside the window must be processed (update allowed).
TEST_F(DoS_tests_bitcoin, DoS_GetBlocksRateLimit)
{
    CAddress addr(ip(0xa0b0c006));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    SetMockTime(1000);
    dummyNode.nLastGetBlocksRecv = 1000; // as if a request just landed this second

    CBlockLocator locator(std::vector<uint256>(1, GetRandHash()));
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETBLOCKS, payload);
    // Still within the 1-second window: rate-limited, no update.
    EXPECT_EQ(dummyNode.nLastGetBlocksRecv, 1000);

    SetMockTime(1002);
    CDataStream payload2(SER_NETWORK, PROTOCOL_VERSION);
    payload2 << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETBLOCKS, payload2);
    // Outside the window: processed, field updates.
    EXPECT_EQ(dummyNode.nLastGetBlocksRecv, 1002);
    SetMockTime(0);
}

// Same rate-limit fix, GETHEADERS handler.
TEST_F(DoS_tests_bitcoin, DoS_GetHeadersRateLimit)
{
    CAddress addr(ip(0xa0b0c007));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    SetMockTime(2000);
    dummyNode.nLastGetHeadersRecv = 2000;

    CBlockLocator locator(std::vector<uint256>(1, GetRandHash()));
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETHEADERS, payload);
    EXPECT_EQ(dummyNode.nLastGetHeadersRecv, 2000);

    SetMockTime(2002);
    CDataStream payload2(SER_NETWORK, PROTOCOL_VERSION);
    payload2 << locator << uint256();
    InjectMessage(&dummyNode, NetMsgType::GETHEADERS, payload2);
    EXPECT_EQ(dummyNode.nLastGetHeadersRecv, 2002);
    SetMockTime(0);
}

// treasurechest_attack_vectors.md #5: any std::ios_base::failure thrown while
// parsing a message's payload (e.g. a declared element count with no backing
// bytes) must trip Misbehaving(+10) via ProcessMessages()'s catch block, so a
// peer can't stream parse-error traffic indefinitely to evade banning.
TEST_F(DoS_tests_bitcoin, DoS_MisbehavingOnMalformedDeserialize)
{
    CAddress addr(ip(0xa0b0c008));
    CNode dummyNode(INVALID_SOCKET, addr, "", true);
    dummyNode.nVersion = PROTOCOL_VERSION;

    // Declares one CInv entry (well under MAX_INV_SZ) but provides none of
    // its 36 payload bytes, so "vRecv >> vInv[n]" throws "end of data".
    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    WriteCompactSize(payload, 1);
    InjectMessage(&dummyNode, NetMsgType::GETDATA, payload);
    EXPECT_EQ(GetMisbehavior(dummyNode.GetId()), 10);
}

// treasurechest_attack_vectors.md #9: mapRelay must not grow unbounded within
// its 15-minute expiry window; a hard MAX_RELAY_ENTRIES=15000 cap evicts the
// oldest entry on insert once full.
TEST_F(DoS_tests_bitcoin, DoS_MapRelayCap)
{
    {
        LOCK(cs_mapRelay);
        mapRelay.clear();
        vRelayExpiration.clear();
    }

    SetMockTime(GetTime());
    const size_t kOverCap = 15010; // MAX_RELAY_ENTRIES(15000) + margin
    for (size_t i = 0; i < kOverCap; i++) {
        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = (CAmount)i;
        RelayTransaction(CTransaction(tx));
    }

    LOCK(cs_mapRelay);
    EXPECT_LE(mapRelay.size(), (size_t)15000);
    SetMockTime(0);
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

// treasurechest_attack_vectors.md #3 / VULN-03: a single peer may not fill
// the orphan pool with more than DEFAULT_MAX_ORPHAN_PER_PEER (5) entries.
// DoS_mapOrphans above never actually triggers this cap (each of its 50
// same-peer-independent orphans uses its own peer id 0..49); this test
// drives every AddOrphanTx call from the SAME peer id specifically to
// exercise the per-peer limit.
TEST_F(DoS_tests_bitcoin, DoS_OrphanPerPeerCap)
{
    LimitOrphanTxSize(0); // start from an empty pool
    ASSERT_TRUE(mapOrphanTransactions.empty());

    const NodeId peer = 777;
    unsigned int accepted = 0;
    for (int i = 0; i < 10; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = GetRandHash();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;

        if (AddOrphanTx(tx, peer))
            accepted++;
    }
    EXPECT_EQ(accepted, DEFAULT_MAX_ORPHAN_PER_PEER);
    EXPECT_EQ(mapOrphanTransactions.size(), (size_t)DEFAULT_MAX_ORPHAN_PER_PEER);

    EraseOrphansFor(peer);
    EXPECT_TRUE(mapOrphanTransactions.empty());
}
