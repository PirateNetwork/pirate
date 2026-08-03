// Copyright (c) 2012-2020 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <net.h>
#include <netaddress.h>
#include <netbase.h>
#include <serialize.h>
#include <span.h>
#include <streams.h>
#include <util/strencodings.h>
#include <version.h>

#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

#include <ios>
#include <string>

// Covers CNetAddr serialization (legacy v1, v2/BIP155 addrv2 wire formats),
// reachability tracking (IsReachable/SetReachable), and local-address
// lifecycle management.
//
// test/net_tests.cpp (upstream Bitcoin Core, 2012-2020) targets a much newer
// net stack than this fork has (class-based gArgs, AdvertiseLocal(),
// PoissonNextSend(), SetAddrLocal(), and a differently-shaped CNode/CAddrDB
// API all don't exist here). Only the CNetAddr/reachability-focused cases,
// which exercise APIs this fork's netaddress.h/net.h actually have (this
// fork's BIP155/addrv2 support), are ported below; cnode_listen_port,
// caddrdb_read[_corrupted], cnode_simple_test,
// ipv4_peer_with_ipv6_addrMe_test, and PoissonNextSend were dropped as
// genuinely unportable without writing new production code.

class net_tests_bitcoin : public BitcoinBasicTestingSetup {};

static void ExpectThrowWithReason(CDataStream& s, CNetAddr& addr, const std::string& reason)
{
    try {
        s >> addr;
        FAIL() << "expected std::ios_base::failure containing \"" << reason << "\"";
    } catch (const std::ios_base::failure& e) {
        EXPECT_NE(std::string(e.what()).find(reason), std::string::npos) << e.what();
    }
}

TEST_F(net_tests_bitcoin, cnetaddr_basic)
{
    CNetAddr addr;

    // IPv4, INADDR_ANY
    ASSERT_TRUE(LookupHost("0.0.0.0", addr, false));
    ASSERT_TRUE(!addr.IsValid());
    ASSERT_TRUE(addr.IsIPv4());

    EXPECT_TRUE(addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "0.0.0.0");

    // IPv4, INADDR_NONE
    ASSERT_TRUE(LookupHost("255.255.255.255", addr, false));
    ASSERT_TRUE(!addr.IsValid());
    ASSERT_TRUE(addr.IsIPv4());

    EXPECT_TRUE(!addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "255.255.255.255");

    // IPv4, casual
    ASSERT_TRUE(LookupHost("12.34.56.78", addr, false));
    ASSERT_TRUE(addr.IsValid());
    ASSERT_TRUE(addr.IsIPv4());

    EXPECT_TRUE(!addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "12.34.56.78");

    // IPv6, in6addr_any
    ASSERT_TRUE(LookupHost("::", addr, false));
    ASSERT_TRUE(!addr.IsValid());
    ASSERT_TRUE(addr.IsIPv6());

    EXPECT_TRUE(addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "::");

    // IPv6, casual
    ASSERT_TRUE(LookupHost("1122:3344:5566:7788:9900:aabb:ccdd:eeff", addr, false));
    ASSERT_TRUE(addr.IsValid());
    ASSERT_TRUE(addr.IsIPv6());

    EXPECT_TRUE(!addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "1122:3344:5566:7788:9900:aabb:ccdd:eeff");

    // Note: unlike upstream Bitcoin Core, this fork's CNetAddr::SetTor() only
    // accepts TORv3 addresses -- TORv2 construction via SetSpecial() was
    // removed (TORv2 is still deserializable from the wire for backward
    // compat, see cnetaddr_unserialize_v2 below, just not constructible from
    // a ".onion" string anymore), so the upstream TORv2 SetSpecial() case is
    // dropped here.

    // TORv3
    const char* torv3_addr = "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion";
    ASSERT_TRUE(addr.SetSpecial(torv3_addr));
    ASSERT_TRUE(addr.IsValid());
    ASSERT_TRUE(addr.IsTor());

    EXPECT_TRUE(!addr.IsBindAny());
    EXPECT_TRUE(!addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), torv3_addr);

    // TORv3, broken, with wrong checksum
    EXPECT_TRUE(!addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscsad.onion"));

    // TORv3, broken, with wrong version
    EXPECT_TRUE(!addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscrye.onion"));

    // TORv3, malicious
    EXPECT_TRUE(!addr.SetSpecial(std::string{
        "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd\0wtf.onion", 66}));

    // TOR, bogus length
    EXPECT_TRUE(!addr.SetSpecial(std::string{"mfrggzak.onion"}));

    // TOR, invalid base32
    EXPECT_TRUE(!addr.SetSpecial(std::string{"mf*g zak.onion"}));

    // Internal
    addr.SetInternal("esffpp");
    ASSERT_TRUE(!addr.IsValid()); // "internal" is considered invalid
    ASSERT_TRUE(addr.IsInternal());

    EXPECT_TRUE(!addr.IsBindAny());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "esffpvrt3wpeaygy.internal");

    // Totally bogus
    EXPECT_TRUE(!addr.SetSpecial("totally bogus"));
}

TEST_F(net_tests_bitcoin, cnetaddr_serialize_v1)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);

    s << addr;
    EXPECT_EQ(HexStr(s), "00000000000000000000000000000000");
    s.clear();

    ASSERT_TRUE(LookupHost("1.2.3.4", addr, false));
    s << addr;
    EXPECT_EQ(HexStr(s), "00000000000000000000ffff01020304");
    s.clear();

    ASSERT_TRUE(LookupHost("1a1b:2a2b:3a3b:4a4b:5a5b:6a6b:7a7b:8a8b", addr, false));
    s << addr;
    EXPECT_EQ(HexStr(s), "1a1b2a2b3a3b4a4b5a5b6a6b7a7b8a8b");
    s.clear();

    // TORv2 SetSpecial() case dropped, see cnetaddr_basic.

    ASSERT_TRUE(addr.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    s << addr;
    EXPECT_EQ(HexStr(s), "00000000000000000000000000000000");
    s.clear();

    addr.SetInternal("a");
    s << addr;
    EXPECT_EQ(HexStr(s), "fd6b88c08724ca978112ca1bbdcafac2");
    s.clear();
}

TEST_F(net_tests_bitcoin, cnetaddr_serialize_v2)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    // Add ADDRV2_FORMAT to the version so that the CNetAddr
    // serialize method produces an address in v2 format.
    s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);

    s << addr;
    EXPECT_EQ(HexStr(s), "021000000000000000000000000000000000");
    s.clear();

    ASSERT_TRUE(LookupHost("1.2.3.4", addr, false));
    s << addr;
    EXPECT_EQ(HexStr(s), "010401020304");
    s.clear();

    ASSERT_TRUE(LookupHost("1a1b:2a2b:3a3b:4a4b:5a5b:6a6b:7a7b:8a8b", addr, false));
    s << addr;
    EXPECT_EQ(HexStr(s), "02101a1b2a2b3a3b4a4b5a5b6a6b7a7b8a8b");
    s.clear();

    // TORv2 SetSpecial() case dropped, see cnetaddr_basic.

    ASSERT_TRUE(addr.SetSpecial("kpgvmscirrdqpekbqjsvw5teanhatztpp2gl6eee4zkowvwfxwenqaid.onion"));
    s << addr;
    EXPECT_EQ(HexStr(s), "042053cd5648488c4707914182655b7664034e09e66f7e8cbf1084e654eb56c5bd88");
    s.clear();

    ASSERT_TRUE(addr.SetInternal("a"));
    s << addr;
    EXPECT_EQ(HexStr(s), "0210fd6b88c08724ca978112ca1bbdcafac2");
    s.clear();
}

TEST_F(net_tests_bitcoin, cnetaddr_unserialize_v2)
{
    CNetAddr addr;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    // Add ADDRV2_FORMAT to the version so that the CNetAddr
    // unserialize method expects an address in v2 format.
    s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);

    // Valid IPv4.
    s << MakeSpan(ParseHex("01"          // network type (IPv4)
                           "04"          // address length
                           "01020304")); // address
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsIPv4());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "1.2.3.4");
    ASSERT_TRUE(s.empty());

    // Invalid IPv4, valid length but address itself is shorter.
    s << MakeSpan(ParseHex("01"      // network type (IPv4)
                           "04"      // address length
                           "0102")); // address
    ExpectThrowWithReason(s, addr, "end of data");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv4, with bogus length.
    s << MakeSpan(ParseHex("01"          // network type (IPv4)
                           "05"          // address length
                           "01020304")); // address
    ExpectThrowWithReason(s, addr, "BIP155 IPv4 address with length 5 (should be 4)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv4, with extreme length.
    s << MakeSpan(ParseHex("01"          // network type (IPv4)
                           "fd0102"      // address length (513 as CompactSize)
                           "01020304")); // address
    ExpectThrowWithReason(s, addr, "Address too long: 513 > 512");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid IPv6.
    s << MakeSpan(ParseHex("02"                                  // network type (IPv6)
                           "10"                                  // address length
                           "0102030405060708090a0b0c0d0e0f10")); // address
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsIPv6());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "102:304:506:708:90a:b0c:d0e:f10");
    ASSERT_TRUE(s.empty());

    // Valid IPv6, contains embedded "internal".
    s << MakeSpan(ParseHex(
        "02"                                  // network type (IPv6)
        "10"                                  // address length
        "fd6b88c08724ca978112ca1bbdcafac2")); // address: 0xfd + sha256("bitcoin")[0:5] +
                                              // sha256(name)[0:10]
    s >> addr;
    EXPECT_TRUE(addr.IsInternal());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "zklycewkdo64v6wc.internal");
    ASSERT_TRUE(s.empty());

    // Invalid IPv6, with bogus length.
    s << MakeSpan(ParseHex("02"    // network type (IPv6)
                           "04"    // address length
                           "00")); // address
    ExpectThrowWithReason(s, addr, "BIP155 IPv6 address with length 4 (should be 16)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Invalid IPv6, contains embedded IPv4.
    s << MakeSpan(ParseHex("02"                                  // network type (IPv6)
                           "10"                                  // address length
                           "00000000000000000000ffff01020304")); // address
    s >> addr;
    EXPECT_TRUE(!addr.IsValid());
    ASSERT_TRUE(s.empty());

    // Invalid IPv6, contains embedded TORv2.
    s << MakeSpan(ParseHex("02"                                  // network type (IPv6)
                           "10"                                  // address length
                           "fd87d87eeb430102030405060708090a")); // address
    s >> addr;
    EXPECT_TRUE(!addr.IsValid());
    ASSERT_TRUE(s.empty());

    // Valid TORv2.
    s << MakeSpan(ParseHex("03"                      // network type (TORv2)
                           "0a"                      // address length
                           "f1f2f3f4f5f6f7f8f9fa")); // address
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsTor());
    EXPECT_TRUE(addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "6hzph5hv6337r6p2.onion");
    ASSERT_TRUE(s.empty());

    // Invalid TORv2, with bogus length.
    s << MakeSpan(ParseHex("03"    // network type (TORv2)
                           "07"    // address length
                           "00")); // address
    ExpectThrowWithReason(s, addr, "BIP155 TORv2 address with length 7 (should be 10)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid TORv3.
    s << MakeSpan(ParseHex("04"                               // network type (TORv3)
                           "20"                               // address length
                           "79bcc625184b05194975c28b66b66b04" // address
                           "69f7f6556fb1ac3189a79b40dda32f1f"
                           ));
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsTor());
    EXPECT_TRUE(!addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(),
              "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion");
    ASSERT_TRUE(s.empty());

    // Invalid TORv3, with bogus length.
    s << MakeSpan(ParseHex("04" // network type (TORv3)
                           "00" // address length
                           "00" // address
                           ));
    ExpectThrowWithReason(s, addr, "BIP155 TORv3 address with length 0 (should be 32)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid I2P.
    s << MakeSpan(ParseHex("05"                               // network type (I2P)
                           "20"                               // address length
                           "a2894dabaec08c0051a481a6dac88b64" // address
                           "f98232ae42d4b6fd2fa81952dfe36a87"));
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsI2P());
    EXPECT_TRUE(!addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(),
              "ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p");
    ASSERT_TRUE(s.empty());

    // Invalid I2P, with bogus length.
    s << MakeSpan(ParseHex("05" // network type (I2P)
                           "03" // address length
                           "00" // address
                           ));
    ExpectThrowWithReason(s, addr, "BIP155 I2P address with length 3 (should be 32)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Valid CJDNS.
    s << MakeSpan(ParseHex("06"                               // network type (CJDNS)
                           "10"                               // address length
                           "fc000001000200030004000500060007" // address
                           ));
    s >> addr;
    EXPECT_TRUE(addr.IsValid());
    EXPECT_TRUE(addr.IsCJDNS());
    EXPECT_TRUE(!addr.IsAddrV1Compatible());
    EXPECT_EQ(addr.ToString(), "fc00:1:2:3:4:5:6:7");
    ASSERT_TRUE(s.empty());

    // Invalid CJDNS, with bogus length.
    s << MakeSpan(ParseHex("06" // network type (CJDNS)
                           "01" // address length
                           "00" // address
                           ));
    ExpectThrowWithReason(s, addr, "BIP155 CJDNS address with length 1 (should be 16)");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Unknown, with extreme length.
    s << MakeSpan(ParseHex("aa"             // network type (unknown)
                           "fe00000002"     // address length (CompactSize's MAX_SIZE)
                           "01020304050607" // address
                           ));
    ExpectThrowWithReason(s, addr, "Address too long: 33554432 > 512");
    ASSERT_TRUE(!s.empty()); // The stream is not consumed on invalid input.
    s.clear();

    // Unknown, with reasonable length.
    s << MakeSpan(ParseHex("aa"       // network type (unknown)
                           "04"       // address length
                           "01020304" // address
                           ));
    s >> addr;
    EXPECT_TRUE(!addr.IsValid());
    ASSERT_TRUE(s.empty());

    // Unknown, with zero length.
    s << MakeSpan(ParseHex("aa" // network type (unknown)
                           "00" // address length
                           ""   // address
                           ));
    s >> addr;
    EXPECT_TRUE(!addr.IsValid());
    ASSERT_TRUE(s.empty());
}

TEST_F(net_tests_bitcoin, LimitedAndReachable_Network)
{
    EXPECT_EQ(IsReachable(NET_IPV4), true);
    EXPECT_EQ(IsReachable(NET_IPV6), true);
    EXPECT_EQ(IsReachable(NET_ONION), true);

    SetReachable(NET_IPV4, false);
    SetReachable(NET_IPV6, false);
    SetReachable(NET_ONION, false);

    EXPECT_EQ(IsReachable(NET_IPV4), false);
    EXPECT_EQ(IsReachable(NET_IPV6), false);
    EXPECT_EQ(IsReachable(NET_ONION), false);

    SetReachable(NET_IPV4, true);
    SetReachable(NET_IPV6, true);
    SetReachable(NET_ONION, true);

    EXPECT_EQ(IsReachable(NET_IPV4), true);
    EXPECT_EQ(IsReachable(NET_IPV6), true);
    EXPECT_EQ(IsReachable(NET_ONION), true);
}

TEST_F(net_tests_bitcoin, LimitedAndReachable_NetworkCaseUnroutableAndInternal)
{
    EXPECT_EQ(IsReachable(NET_UNROUTABLE), true);
    EXPECT_EQ(IsReachable(NET_INTERNAL), true);

    SetReachable(NET_UNROUTABLE, false);
    SetReachable(NET_INTERNAL, false);

    EXPECT_EQ(IsReachable(NET_UNROUTABLE), true); // Ignored for both networks
    EXPECT_EQ(IsReachable(NET_INTERNAL), true);
}

TEST_F(net_tests_bitcoin, GetReachabilityFrom_OnlySameNetworkOrUnreachable)
{
    // Regression test for a privacy leak, hardened in three stages:
    //
    // 1. GetReachabilityFrom() originally had no case for NET_I2P or
    //    NET_CJDNS in its outer switch(theirNet), so an I2P/CJDNS peer fell
    //    through to the same default branch used for unroutable/unknown
    //    peers, which scored our own IPv4/IPv6 address (REACH_IPV4/
    //    REACH_IPV6_WEAK) higher than our own I2P/CJDNS address. Since
    //    GetLocal()/GetLocalAddress() pick the highest-scoring local address
    //    for a given peer, and CNode::PushVersion() sends that address
    //    unconditionally to every peer in the VERSION handshake, a real IPv4
    //    address could be sent directly to an I2P peer.
    // 2. Fixing that to a lower-but-nonzero score wasn't enough either: a
    //    low score still gets returned by GetLocal() as a last-resort
    //    fallback when nothing better is available (see
    //    GetLocal_NeverFallsBackToCrossNetworkAddress below for the other
    //    half of this fix, in net.cpp's GetLocal(), which treats
    //    REACH_UNREACHABLE specifically as a hard skip).
    // 3. The final policy: every network (IPv4, IPv6, Tor, I2P, CJDNS,
    //    Teredo) only ever offers an address on that exact same network to a
    //    matching peer, or REACH_UNREACHABLE otherwise - no cross-network
    //    fallback at all, not even IPv4<->IPv6. Revealing an address on one
    //    network to a peer on a different network links whatever identity
    //    that network represents to the peer; for the anonymity networks
    //    that's a direct deanonymization risk, and for IPv4/IPv6 it isn't
    //    useful anyway since the peer can't dial an address on a network it
    //    doesn't speak. Checked in both directions: neither side may ever be
    //    offered the other's address unless the networks match exactly.
    CNetAddr torPeer, torOurs, i2pPeer, i2pOurs, ipv4Ours, ipv6Ours, ipv4Peer;
    ASSERT_TRUE(torPeer.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    ASSERT_TRUE(torOurs.SetSpecial("kpgvmscirrdqpekbqjsvw5teanhatztpp2gl6eee4zkowvwfxwenqaid.onion"));
    ASSERT_TRUE(i2pPeer.SetSpecial("ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p"));
    ASSERT_TRUE(i2pOurs.SetSpecial("ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p"));
    ASSERT_TRUE(LookupHost("12.34.56.78", ipv4Ours, false));
    ASSERT_TRUE(LookupHost("1122:3344:5566:7788:9900:aabb:ccdd:eeff", ipv6Ours, false));
    ASSERT_TRUE(LookupHost("87.65.43.21", ipv4Peer, false));

    EXPECT_EQ(ipv4Ours.GetReachabilityFrom(&torPeer), 0)
        << "a Tor peer must never be offered our IPv4 address";
    EXPECT_EQ(ipv6Ours.GetReachabilityFrom(&torPeer), 0)
        << "a Tor peer must never be offered our IPv6 address";
    EXPECT_GT(torOurs.GetReachabilityFrom(&torPeer), 0)
        << "a Tor peer may still be offered our own onion address";
    EXPECT_EQ(torOurs.GetReachabilityFrom(&ipv4Peer), 0)
        << "an IPv4 peer must never be offered our onion address";

    EXPECT_EQ(ipv4Ours.GetReachabilityFrom(&i2pPeer), 0)
        << "an I2P peer must never be offered our IPv4 address";
    EXPECT_EQ(ipv6Ours.GetReachabilityFrom(&i2pPeer), 0)
        << "an I2P peer must never be offered our IPv6 address";
    EXPECT_GT(i2pOurs.GetReachabilityFrom(&i2pPeer), 0)
        << "an I2P peer may still be offered our own I2P address";
    EXPECT_EQ(i2pOurs.GetReachabilityFrom(&ipv4Peer), 0)
        << "an IPv4 peer must never be offered our I2P address";

    // CJDNS has no SetSpecial()-style string constructor; build addresses via
    // the BIP155 wire format, same as cnetaddr_unserialize_v2 does above.
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);
    CNetAddr cjdnsPeer, cjdnsOurs;
    s << MakeSpan(ParseHex("06"                               // network type (CJDNS)
                           "10"                               // address length
                           "fc000001000200030004000500060007" // address
                           ));
    s >> cjdnsPeer;
    ASSERT_TRUE(cjdnsPeer.IsCJDNS());
    s << MakeSpan(ParseHex("06"
                           "10"
                           "fc000001000200030004000500060008"));
    s >> cjdnsOurs;
    ASSERT_TRUE(cjdnsOurs.IsCJDNS());

    EXPECT_EQ(ipv4Ours.GetReachabilityFrom(&cjdnsPeer), 0)
        << "a CJDNS peer must never be offered our IPv4 address";
    EXPECT_GT(cjdnsOurs.GetReachabilityFrom(&cjdnsPeer), 0)
        << "a CJDNS peer may still be offered our own CJDNS address";
    EXPECT_EQ(cjdnsOurs.GetReachabilityFrom(&ipv4Peer), 0)
        << "an IPv4 peer must never be offered our CJDNS address";

    // Cross-anonymity-network pairings must also be unreachable: a Tor peer
    // must never be offered our I2P/CJDNS address, and vice versa.
    EXPECT_EQ(i2pOurs.GetReachabilityFrom(&torPeer), 0)
        << "a Tor peer must never be offered our I2P address";
    EXPECT_EQ(cjdnsOurs.GetReachabilityFrom(&torPeer), 0)
        << "a Tor peer must never be offered our CJDNS address";
    EXPECT_EQ(torOurs.GetReachabilityFrom(&i2pPeer), 0)
        << "an I2P peer must never be offered our onion address";
    EXPECT_EQ(torOurs.GetReachabilityFrom(&cjdnsPeer), 0)
        << "a CJDNS peer must never be offered our onion address";

    // Sanity check: a same-network match still works, and IPv4/IPv6 no
    // longer fall back to each other either - the "own network or
    // unreachable" rule applies uniformly, not just to the anonymity
    // networks.
    EXPECT_GT(ipv4Ours.GetReachabilityFrom(&ipv4Peer), 0)
        << "an IPv4 peer must still be offered our IPv4 address";
    EXPECT_EQ(ipv6Ours.GetReachabilityFrom(&ipv4Peer), 0)
        << "an IPv4 peer must never be offered our IPv6 address";
}

static CNetAddr UtilBuildAddress(unsigned char p1, unsigned char p2, unsigned char p3, unsigned char p4)
{
    unsigned char ip[] = {p1, p2, p3, p4};

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sockaddr_in)); // initialize the memory block
    memcpy(&(sa.sin_addr), &ip, sizeof(ip));
    return CNetAddr(sa.sin_addr);
}

TEST_F(net_tests_bitcoin, GetLocal_NeverFallsBackToCrossNetworkAddress)
{
    // The other half of the fix above: GetReachabilityFrom() returning
    // REACH_UNREACHABLE only matters if GetLocal() (net.cpp) actually treats
    // it as a hard skip rather than just the bottom of a preference ordering.
    // Before this fix, GetLocal()'s loop started at nBestReachability = -1,
    // so even a REACH_UNREACHABLE (0) candidate would still "win" and be
    // returned whenever it was the only address configured - a low score
    // meant "least preferred", not "never". This test proves the actual,
    // end-to-end guarantee: with only an IPv4 address registered, a Tor/I2P
    // peer gets nothing back at all (GetLocal returns false), which is what
    // makes GetLocalAddress() fall back to the unspecified/unroutable
    // placeholder instead of ever handing out that IPv4 address.
    CService ipv4Local = CService(UtilBuildAddress(0x005, 0x001, 0x001, 0x001), 2000); // 5.1.1.1:2000
    SetReachable(NET_IPV4, true);
    ASSERT_TRUE(AddLocal(ipv4Local, LOCAL_MANUAL));

    CNetAddr torPeer, i2pPeer, ipv4Peer;
    ASSERT_TRUE(torPeer.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    ASSERT_TRUE(i2pPeer.SetSpecial("ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p"));
    ipv4Peer = UtilBuildAddress(0x006, 0x001, 0x001, 0x001);

    CService result;
    EXPECT_FALSE(GetLocal(result, &torPeer))
        << "a Tor peer must get nothing back, not a fallback IPv4 address";
    EXPECT_FALSE(GetLocal(result, &i2pPeer))
        << "an I2P peer must get nothing back, not a fallback IPv4 address";

    // Sanity check: the same address is still offered to a plain IPv4 peer -
    // this isn't a general "never return anything" regression.
    EXPECT_TRUE(GetLocal(result, &ipv4Peer));
    EXPECT_EQ(result, ipv4Local);

    RemoveLocal(ipv4Local);
}

TEST_F(net_tests_bitcoin, LimitedAndReachable_CNetAddr)
{
    CNetAddr addr = UtilBuildAddress(0x001, 0x001, 0x001, 0x001); // 1.1.1.1

    SetReachable(NET_IPV4, true);
    EXPECT_EQ(IsReachable(addr), true);

    SetReachable(NET_IPV4, false);
    EXPECT_EQ(IsReachable(addr), false);

    SetReachable(NET_IPV4, true); // have to reset this, because this is stateful.
}

TEST_F(net_tests_bitcoin, LocalAddress_BasicLifecycle)
{
    CService addr = CService(UtilBuildAddress(0x002, 0x001, 0x001, 0x001), 1000); // 2.1.1.1:1000

    SetReachable(NET_IPV4, true);

    EXPECT_EQ(IsLocal(addr), false);
    EXPECT_EQ(AddLocal(addr, 1000), true);
    EXPECT_EQ(IsLocal(addr), true);

    RemoveLocal(addr);
    EXPECT_EQ(IsLocal(addr), false);
}

TEST_F(net_tests_bitcoin, GetUnderTargetReachableNetworks_ReturnsUnreachedReachableNetworks)
{
    // Every network is reachable by default in this test binary, and with no
    // outbound connections yet (an empty count map), every interface network
    // still needs to reach MIN_OUTBOUND_PER_REACHABLE_NETWORK.
    std::map<Network, int> empty;
    std::set<Network> underTarget = GetUnderTargetReachableNetworks(empty);
    EXPECT_EQ(underTarget, (std::set<Network>{NET_IPV4, NET_IPV6, NET_ONION, NET_I2P, NET_CJDNS}));

    // Networks that already have enough outbound connections drop out of the set.
    std::map<Network, int> counts{
        {NET_IPV4, MIN_OUTBOUND_PER_REACHABLE_NETWORK},
        {NET_IPV6, MIN_OUTBOUND_PER_REACHABLE_NETWORK + 5},
        {NET_ONION, 0},
        {NET_I2P, MIN_OUTBOUND_PER_REACHABLE_NETWORK - 1},
        {NET_CJDNS, MIN_OUTBOUND_PER_REACHABLE_NETWORK},
    };
    underTarget = GetUnderTargetReachableNetworks(counts);
    EXPECT_EQ(underTarget, (std::set<Network>{NET_ONION, NET_I2P}));

    // Unreachable networks never appear, no matter their (irrelevant) count.
    SetReachable(NET_I2P, false);
    underTarget = GetUnderTargetReachableNetworks(counts);
    EXPECT_EQ(underTarget, (std::set<Network>{NET_ONION}));
    SetReachable(NET_I2P, true); // reset - global state shared with every other test in this binary
}

TEST_F(net_tests_bitcoin, ShouldSkipForNetworkDiversity_Behavior)
{
    const std::set<Network> underTarget{NET_ONION, NET_I2P};
    const int kBudget = 40;

    // Nothing to skip for once every reachable network has already met its target.
    EXPECT_FALSE(ShouldSkipForNetworkDiversity(NET_IPV4, {}, 0, kBudget));

    // A candidate on an under-target network is never skipped, at any point
    // in the search.
    EXPECT_FALSE(ShouldSkipForNetworkDiversity(NET_ONION, underTarget, 0, kBudget));
    EXPECT_FALSE(ShouldSkipForNetworkDiversity(NET_I2P, underTarget, kBudget - 1, kBudget));

    // A candidate on an already-satisfied network is skipped while there's
    // still search budget left, to give the under-target networks a chance...
    EXPECT_TRUE(ShouldSkipForNetworkDiversity(NET_IPV4, underTarget, 0, kBudget));
    EXPECT_TRUE(ShouldSkipForNetworkDiversity(NET_IPV4, underTarget, kBudget - 1, kBudget));

    // ...but accepted once the budget is exhausted, so an under-target
    // network with no addresses in addrman yet can never starve connections
    // entirely.
    EXPECT_FALSE(ShouldSkipForNetworkDiversity(NET_IPV4, underTarget, kBudget, kBudget));
    EXPECT_FALSE(ShouldSkipForNetworkDiversity(NET_IPV4, underTarget, kBudget + 1, kBudget));
}

// Regression test for CreateNodeFromAcceptedSocket's MAX_INBOUND_FROMIP
// check (net.cpp): it used to build a raw sockaddr for the *new* connection
// that was never actually populated from that connection's address (an
// uninitialized local variable compared via memcmp() against real peers'
// addresses), and even a correctly-populated version would have compared
// full IP:port instead of IP alone - every new inbound TCP connection gets a
// fresh ephemeral source port even from a repeat IP, so a port-inclusive
// comparison could never consider two real connections from the same source
// to match. Both bugs meant the per-IP inbound cap (added specifically to
// resist eclipse attacks) never actually triggered for a real repeat source.
TEST_F(net_tests_bitcoin, SameNetAddr_ComparesIPOnlyIgnoringPort)
{
    CNetAddr ipA, ipB;
    ASSERT_TRUE(LookupHost("203.0.113.5", ipA, false));
    ASSERT_TRUE(LookupHost("203.0.113.6", ipB, false));

    // Same IP, different ports (as every distinct inbound TCP connection
    // from one source naturally would be) must still count as the same source.
    EXPECT_TRUE(SameNetAddr(CService(ipA, 45452), CService(ipA, 55164)));
    EXPECT_TRUE(SameNetAddr(CService(ipA, 8233), CService(ipA, 8233)));

    // Different IPs must never be considered the same source, regardless of port.
    EXPECT_FALSE(SameNetAddr(CService(ipA, 45452), CService(ipB, 45452)));
    EXPECT_FALSE(SameNetAddr(CService(ipA, 8233), CService(ipB, 8233)));
}

// Regression test: fixing SameNetAddr (above) made MAX_INBOUND_FROMIP
// actually enforce per-source-IP counting for the first time - which
// exposed a second, latent bug. Tor forwards every inbound onion-service
// connection to our own loopback address, so once the per-IP cap actually
// worked, all real (and distinct) inbound Tor peers collided against each
// other as "the same source", capping the node to MAX_INBOUND_FROMIP
// inbound Tor peers total. Connections accepted on the dedicated
// Tor-forwarding listener (BindOnionListenPort) must be exempt from this
// cap; connections accepted anywhere else - including a plain loopback
// connection from some other local process - must still be capped, since
// only the dedicated listener can be trusted to mean "this is really Tor".
TEST_F(net_tests_bitcoin, ShouldRejectForInboundFromIPCap_ExemptsOnionListenerOnly)
{
    const int kCap = 2;

    // Under the cap: never rejected, Tor listener or not.
    EXPECT_FALSE(ShouldRejectForInboundFromIPCap(0, kCap, /* fOnionListener */ true));
    EXPECT_FALSE(ShouldRejectForInboundFromIPCap(0, kCap, /* fOnionListener */ false));

    // At/over the cap: a connection not accepted via the dedicated Tor
    // listener is rejected - this includes plain loopback connections from
    // some other local process, not just remote IPs.
    EXPECT_TRUE(ShouldRejectForInboundFromIPCap(kCap, kCap, /* fOnionListener */ false));
    EXPECT_TRUE(ShouldRejectForInboundFromIPCap(kCap + 5, kCap, /* fOnionListener */ false));

    // ...but connections accepted via the dedicated Tor listener are exempt,
    // however many are already connected.
    EXPECT_FALSE(ShouldRejectForInboundFromIPCap(kCap, kCap, /* fOnionListener */ true));
    EXPECT_FALSE(ShouldRejectForInboundFromIPCap(kCap + 5, kCap, /* fOnionListener */ true));
}

// Non-I2P connections must count toward the per-source-IP cap exactly as
// before - one local identity, one global per-remote-address total,
// regardless of whatever pool index/generation values happen to be passed
// (which for a non-I2P peer are always the "not pool" defaults anyway).
TEST_F(net_tests_bitcoin, ShouldCountTowardInboundFromIPCap_NonI2PAlwaysCounts)
{
    EXPECT_TRUE(ShouldCountTowardInboundFromIPCap(false, -1, 0, -1, 0));
    EXPECT_TRUE(ShouldCountTowardInboundFromIPCap(false, 3, 7, 5, 2));
}

// I2P connections must be scoped per local identity: an existing peer only
// counts against a new connection attempt if both are on the same pool slot
// (primary identity is idx -1) *and* the same generation of that slot - a
// remote peer already connected to our primary identity, or to a different
// pool slot, must not count against a new connection to some other local
// I2P identity, since that's exactly the multi-identity behavior the
// relay-pool design depends on.
TEST_F(net_tests_bitcoin, ShouldCountTowardInboundFromIPCap_I2PScopedPerIdentity)
{
    // Same identity (both primary, idx -1): counts.
    EXPECT_TRUE(ShouldCountTowardInboundFromIPCap(true, -1, 0, -1, 0));
    // Same pool slot, same generation: counts.
    EXPECT_TRUE(ShouldCountTowardInboundFromIPCap(true, 0, 4, 0, 4));

    // Existing peer on primary, new connection targeting a pool slot: does
    // not count against each other.
    EXPECT_FALSE(ShouldCountTowardInboundFromIPCap(true, -1, 0, 0, 0));
    EXPECT_FALSE(ShouldCountTowardInboundFromIPCap(true, 0, 0, -1, 0));

    // Different pool slots: does not count.
    EXPECT_FALSE(ShouldCountTowardInboundFromIPCap(true, 0, 0, 1, 0));

    // Same slot index but different generation - the old identity was
    // retired and the index reused for a brand new identity; a lingering
    // peer from the old generation must not count against the new one.
    EXPECT_FALSE(ShouldCountTowardInboundFromIPCap(true, 2, 1, 2, 2));
}

// A local (loopback) inbound connection that did NOT arrive via the
// dedicated Tor listener must be hard-rejected unless the operator opted in
// with -allowlocalip; a genuine Tor peer or a non-local address must
// never be rejected by this check regardless of that flag.
TEST_F(net_tests_bitcoin, ShouldRejectLocalNonOnionInbound_Behavior)
{
    CNetAddr loopback, nonLocal;
    ASSERT_TRUE(LookupHost("127.0.0.1", loopback, false));
    ASSERT_TRUE(LookupHost("203.0.113.5", nonLocal, false));

    // Local, not via the Tor listener, opt-in not set: rejected.
    EXPECT_TRUE(ShouldRejectLocalNonOnionInbound(loopback, /* fOnionListener */ false, /* fAllowLocalIp */ false));

    // Local, not via the Tor listener, but opted in: allowed.
    EXPECT_FALSE(ShouldRejectLocalNonOnionInbound(loopback, /* fOnionListener */ false, /* fAllowLocalIp */ true));

    // Local, but via the dedicated Tor listener: always allowed, opt-in or not.
    EXPECT_FALSE(ShouldRejectLocalNonOnionInbound(loopback, /* fOnionListener */ true, /* fAllowLocalIp */ false));
    EXPECT_FALSE(ShouldRejectLocalNonOnionInbound(loopback, /* fOnionListener */ true, /* fAllowLocalIp */ true));

    // Non-local address: never rejected by this check, regardless of the
    // other flags - that's what ShouldRejectForInboundFromIPCap is for.
    EXPECT_FALSE(ShouldRejectLocalNonOnionInbound(nonLocal, /* fOnionListener */ false, /* fAllowLocalIp */ false));
    EXPECT_FALSE(ShouldRejectLocalNonOnionInbound(nonLocal, /* fOnionListener */ true, /* fAllowLocalIp */ false));
}

TEST_F(net_tests_bitcoin, IsPrivacyPeer_Behavior)
{
    CNetAddr clearnet;
    ASSERT_TRUE(LookupHost("203.0.113.5", clearnet, false));

    CNetAddr tor;
    ASSERT_TRUE(tor.SetSpecial("pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
    ASSERT_TRUE(tor.IsTor());

    CNetAddr i2p;
    CDataStream s(SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    s << MakeSpan(ParseHex("05"                               // network type (I2P)
                           "20"                               // address length
                           "a2894dabaec08c0051a481a6dac88b64" // address
                           "f98232ae42d4b6fd2fa81952dfe36a87"));
    s >> i2p;
    ASSERT_TRUE(i2p.IsI2P());

    // Outbound Tor and outbound I2P both count as privacy peers; outbound
    // clearnet doesn't. Inbound Tor never counts - inbound Tor connections
    // always arrive over loopback (see ShouldRejectForInboundFromIPCap), so
    // an inbound connection's claimed Tor address can never be trusted as
    // real. Inbound I2P, unlike inbound Tor, DOES count - I2P inbound
    // connections carry the real remote address via SAM's STREAM ACCEPT,
    // so there's no loopback-forwarding ambiguity to worry about there.
    EXPECT_TRUE(IsPrivacyPeer(/* fInbound */ false, tor));
    EXPECT_TRUE(IsPrivacyPeer(/* fInbound */ false, i2p));
    EXPECT_FALSE(IsPrivacyPeer(/* fInbound */ false, clearnet));
    EXPECT_FALSE(IsPrivacyPeer(/* fInbound */ true, tor));
    EXPECT_TRUE(IsPrivacyPeer(/* fInbound */ true, i2p));
    EXPECT_FALSE(IsPrivacyPeer(/* fInbound */ true, clearnet));
}

// A transaction relayed on our own peer's behalf (received from another
// peer, fLocalOrigin=false) must never be restricted, regardless of
// anything else - only locally-originated transactions get the privacy
// treatment, and even then only when there's an eligible peer to send them
// to, or -privatetxrelayfallback is disabled (see net.h/net.cpp).
TEST_F(net_tests_bitcoin, ShouldRestrictRelayToPrivacyPeers_Behavior)
{
    EXPECT_FALSE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ false, /* fEnabled */ true, /* nPeers */ 5, /* fAllowFallback */ true));
    EXPECT_FALSE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ false, /* fEnabled */ true, /* nPeers */ 0, /* fAllowFallback */ false));
    EXPECT_FALSE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ true, /* fEnabled */ false, /* nPeers */ 5, /* fAllowFallback */ true));

    // At least one privacy peer connected: always restrict, regardless of
    // the fallback setting - there's no need to fall back to clearnet.
    EXPECT_TRUE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ true, /* fEnabled */ true, /* nPeers */ 1, /* fAllowFallback */ true));
    EXPECT_TRUE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ true, /* fEnabled */ true, /* nPeers */ 5, /* fAllowFallback */ false));

    // Zero privacy peers connected: fall open (don't restrict, so the
    // caller relays via all peers) only if fallback is allowed...
    EXPECT_FALSE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ true, /* fEnabled */ true, /* nPeers */ 0, /* fAllowFallback */ true));

    // ...but stay restricted (relay to nobody this round, rather than ever
    // touching a clearnet peer) when fallback has been explicitly disabled.
    EXPECT_TRUE(ShouldRestrictRelayToPrivacyPeers(/* fLocalOrigin */ true, /* fEnabled */ true, /* nPeers */ 0, /* fAllowFallback */ false));
}

// I2P relay pool: burn-after-use identities used only for relaying
// locally-originated transactions - see the "Rotating burn-after-use I2P
// identities for transaction relay" design. These tests cover the pure
// scheduling/selection algorithms in isolation; the actual pool lifecycle
// (real SAM sessions, sockets, threads) is exercised by running a node,
// not by gtest - see net.cpp for how these functions are used.

static int64_t AbsDiff(int64_t a, int64_t b)
{
    return a >= b ? a - b : b - a;
}

TEST_F(net_tests_bitcoin, ComputeI2PPoolStaggeredExpiry_Bootstrap)
{
    const int64_t nNow = 1000000;
    const int64_t nMin = nNow + I2P_POOL_MIN_LIFETIME;
    const int64_t nMax = nNow + I2P_POOL_MAX_LIFETIME;

    // No other identities yet (pool startup): result is unconstrained
    // except by the lifetime window itself, for any random draw.
    for (int64_t nRandSource : {(int64_t)0, (int64_t)10000, (int64_t)21599}) {
        int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, {}, nRandSource);
        EXPECT_GE(result, nMin);
        EXPECT_LE(result, nMax);
    }
}

TEST_F(net_tests_bitcoin, ComputeI2PPoolStaggeredExpiry_AlreadySafeIsUnmodified)
{
    const int64_t nNow = 1000000;
    const int64_t nMin = nNow + I2P_POOL_MIN_LIFETIME;

    // candidate = nMin + 10800; nearest neighbor is far enough away already.
    int64_t nOther = nMin;
    int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, {nOther}, /* nRandSource */ 10800);
    EXPECT_EQ(result, nMin + 10800);
}

TEST_F(net_tests_bitcoin, ComputeI2PPoolStaggeredExpiry_PushesAwayFromCollision)
{
    const int64_t nNow = 1000000;
    const int64_t nMin = nNow + I2P_POOL_MIN_LIFETIME;

    // Push downward: candidate (nMin+5000) collides with a neighbor just
    // above it; must be pushed below the neighbor by exactly the margin.
    {
        int64_t nOther = nMin + 5400; // distance from raw candidate = 400
        int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, {nOther}, /* nRandSource */ 5000);
        EXPECT_EQ(result, nOther - I2P_POOL_MIN_STAGGER);
        EXPECT_GE(AbsDiff(result, nOther), I2P_POOL_MIN_STAGGER);
    }

    // Push upward: candidate (nMin+10000) collides with a neighbor just
    // below it; must be pushed above the neighbor by exactly the margin.
    {
        int64_t nOther = nMin + 8400; // distance from raw candidate = 1600
        int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, {nOther}, /* nRandSource */ 10000);
        EXPECT_EQ(result, nOther + I2P_POOL_MIN_STAGGER);
        EXPECT_GE(AbsDiff(result, nOther), I2P_POOL_MIN_STAGGER);
    }
}

TEST_F(net_tests_bitcoin, ComputeI2PPoolStaggeredExpiry_LifetimeFloorTakesPriority)
{
    const int64_t nNow = 1000000;
    const int64_t nMin = nNow + I2P_POOL_MIN_LIFETIME;
    const int64_t nMax = nNow + I2P_POOL_MAX_LIFETIME;

    // Raw candidate lands exactly at the floor; a neighbor just above it
    // would normally push the candidate below the floor - the floor must
    // win instead (documented trade-off: see net.cpp).
    int64_t nOther = nMin + 1000;
    int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, {nOther}, /* nRandSource */ 0);
    EXPECT_EQ(result, nMin);
    EXPECT_GE(result, nMin);
    EXPECT_LE(result, nMax);
}

TEST_F(net_tests_bitcoin, ComputeI2PPoolStaggeredExpiry_MultipleNeighbors)
{
    const int64_t nNow = 1000000;
    const int64_t nMin = nNow + I2P_POOL_MIN_LIFETIME;

    // Candidate collides with the first neighbor in the list; after being
    // pushed away from it, must still respect the margin against the
    // second (unrelated) neighbor too.
    std::vector<int64_t> vOthers = {nMin + 5400, nMin + 50000};
    int64_t result = ComputeI2PPoolStaggeredExpiry(nNow, vOthers, /* nRandSource */ 5000);
    for (int64_t nOther : vOthers) {
        EXPECT_GE(AbsDiff(result, nOther), I2P_POOL_MIN_STAGGER)
            << "result " << result << " too close to neighbor " << nOther;
    }
}

TEST_F(net_tests_bitcoin, SelectI2PPoolIdentityForRelay_Behavior)
{
    using State = I2PPoolIdentityState;

    // Nothing usable at all.
    {
        std::vector<I2PPoolSlotSelectionInfo> vSlots = {
            {State::WARMING, 5, 0},
            {State::READY, 0, 0},   // READY but no peers - not eligible
            {State::ACTIVE, 0, 100}, // ACTIVE but no peers - not eligible
        };
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 0), -1);
    }

    // A single READY-with-peers slot is selected.
    {
        std::vector<I2PPoolSlotSelectionInfo> vSlots = {
            {State::WARMING, 5, 0},
            {State::READY, 3, 0},
        };
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 0), 1);
    }

    // Multiple READY-with-peers slots: round-robin via the counter.
    {
        std::vector<I2PPoolSlotSelectionInfo> vSlots = {
            {State::READY, 1, 0},
            {State::READY, 1, 0},
            {State::READY, 1, 0},
        };
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 0), 0);
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 1), 1);
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 2), 2);
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 3), 0); // wraps around
    }

    // READY-with-peers is always preferred over ACTIVE-with-peers, even if
    // the ACTIVE slot has an earlier (more urgent-looking) drain deadline.
    {
        std::vector<I2PPoolSlotSelectionInfo> vSlots = {
            {State::ACTIVE, 5, /* nDrainDeadline */ 1},
            {State::READY, 5, 0},
        };
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 0), 1);
    }

    // No READY-with-peers slot: degrade to the least-recently-activated
    // (smallest drain deadline) ACTIVE-with-peers slot.
    {
        std::vector<I2PPoolSlotSelectionInfo> vSlots = {
            {State::ACTIVE, 2, /* nDrainDeadline */ 500},
            {State::ACTIVE, 2, /* nDrainDeadline */ 100},
            {State::ACTIVE, 2, /* nDrainDeadline */ 300},
        };
        EXPECT_EQ(SelectI2PPoolIdentityForRelay(vSlots, 0), 1);
    }
}

TEST_F(net_tests_bitcoin, ShouldRetireI2PPoolIdentity_Behavior)
{
    using State = I2PPoolIdentityState;
    const int64_t nNow = 1000000;

    // Hard expiry is a backstop enforced regardless of state.
    EXPECT_TRUE(ShouldRetireI2PPoolIdentity(State::WARMING, /* nHardExpiryTime */ nNow - 1, /* nDrainDeadline */ 0, nNow));
    EXPECT_TRUE(ShouldRetireI2PPoolIdentity(State::READY, /* nHardExpiryTime */ nNow - 1, /* nDrainDeadline */ 0, nNow));
    EXPECT_FALSE(ShouldRetireI2PPoolIdentity(State::WARMING, /* nHardExpiryTime */ nNow + 1, /* nDrainDeadline */ 0, nNow));

    // Drain deadline only matters (and only retires) while ACTIVE.
    EXPECT_TRUE(ShouldRetireI2PPoolIdentity(State::ACTIVE, /* nHardExpiryTime */ nNow + 1000, /* nDrainDeadline */ nNow - 1, nNow));
    EXPECT_FALSE(ShouldRetireI2PPoolIdentity(State::ACTIVE, /* nHardExpiryTime */ nNow + 1000, /* nDrainDeadline */ nNow + 1, nNow));
    // A READY slot's stale/irrelevant drain deadline must never trigger
    // retirement on its own - only the hard expiry applies to it.
    EXPECT_FALSE(ShouldRetireI2PPoolIdentity(State::READY, /* nHardExpiryTime */ nNow + 1000, /* nDrainDeadline */ nNow - 1, nNow));
}

TEST_F(net_tests_bitcoin, ShouldPromoteI2PPoolIdentityToReady_Behavior)
{
    const int64_t nNow = 1000000;
    const int nMinPeers = 2;

    EXPECT_FALSE(ShouldPromoteI2PPoolIdentityToReady(/* nWarmupDeadline */ nNow + 1, /* nPeerCount */ 5, nNow, nMinPeers))
        << "warmup not yet elapsed, even with plenty of peers";
    EXPECT_FALSE(ShouldPromoteI2PPoolIdentityToReady(/* nWarmupDeadline */ nNow - 1, /* nPeerCount */ 1, nNow, nMinPeers))
        << "warmup elapsed but not enough peers yet";
    EXPECT_TRUE(ShouldPromoteI2PPoolIdentityToReady(/* nWarmupDeadline */ nNow - 1, /* nPeerCount */ 2, nNow, nMinPeers));
    EXPECT_TRUE(ShouldPromoteI2PPoolIdentityToReady(/* nWarmupDeadline */ nNow, /* nPeerCount */ 5, nNow, nMinPeers));
}

TEST_F(net_tests_bitcoin, ShouldDisconnectForI2PPoolRotation_Behavior)
{
    EXPECT_TRUE(ShouldDisconnectForI2PPoolRotation(/* nodeIdx */ 2, /* nodeGen */ 5, /* rotatingIdx */ 2, /* curGen */ 5));
    EXPECT_FALSE(ShouldDisconnectForI2PPoolRotation(/* nodeIdx */ 2, /* nodeGen */ 4, /* rotatingIdx */ 2, /* curGen */ 5))
        << "stale generation from before this slot's current lifetime must not match";
    EXPECT_FALSE(ShouldDisconnectForI2PPoolRotation(/* nodeIdx */ 1, /* nodeGen */ 5, /* rotatingIdx */ 2, /* curGen */ 5))
        << "different slot index must never match";
    EXPECT_FALSE(ShouldDisconnectForI2PPoolRotation(/* nodeIdx */ -1, /* nodeGen */ 0, /* rotatingIdx */ 2, /* curGen */ 5))
        << "non-pool (primary identity or non-I2P) connection must never match";
}
