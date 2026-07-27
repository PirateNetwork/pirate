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

static CNetAddr UtilBuildAddress(unsigned char p1, unsigned char p2, unsigned char p3, unsigned char p4)
{
    unsigned char ip[] = {p1, p2, p3, p4};

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sockaddr_in)); // initialize the memory block
    memcpy(&(sa.sin_addr), &ip, sizeof(ip));
    return CNetAddr(sa.sin_addr);
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
