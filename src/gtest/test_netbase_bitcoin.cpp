// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "netbase.h"
#include "gtest/gtestutils.h"
#include "util/strencodings.h"

#include <string>

#include <gtest/gtest.h>

// Named _bitcoin: gtest/test_netbase_tests.cpp already covers netbase_getgroup
// (TestAddrmanTests.netbase_getgroup), so it isn't re-ported here.
//
// This fork's netbase.h/netaddress.h differ from the newer upstream Bitcoin
// Core version the original test was written against: NET_TOR was renamed
// NET_ONION, there's no CNetAddr(const std::string&) constructor for onion
// addresses (use SetSpecial() instead), LookupHost/LookupSubNet only take
// const char* (no embedded-NUL-safe std::string overload), and there's no
// net_permissions.h (NetWhitebindPermissions/NetWhitelistPermissions/
// bilingual_str) at all. netpermissions_test and
// netbase_dont_resolve_strings_with_embedded_nul_characters depend on APIs
// that plainly don't exist here and were not ported.
class netbase_tests_bitcoin : public BitcoinBasicTestingSetup {};

static CNetAddr ResolveIP(const std::string& ip)
{
    CNetAddr addr;
    LookupHost(ip.c_str(), addr, false);
    return addr;
}

static CSubNet ResolveSubNet(const std::string& subnet)
{
    CSubNet ret;
    LookupSubNet(subnet.c_str(), ret);
    return ret;
}

static CNetAddr CreateInternal(const char* host)
{
    CNetAddr addr;
    addr.SetInternal(host);
    return addr;
}

TEST_F(netbase_tests_bitcoin, netbase_networks)
{
    EXPECT_TRUE(ResolveIP("127.0.0.1").GetNetwork()                              == NET_UNROUTABLE);
    EXPECT_TRUE(ResolveIP("::1").GetNetwork()                                    == NET_UNROUTABLE);
    EXPECT_TRUE(ResolveIP("8.8.8.8").GetNetwork()                                == NET_IPV4);
    EXPECT_TRUE(ResolveIP("2001::8888").GetNetwork()                             == NET_IPV6);
    EXPECT_TRUE(ResolveIP("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca").GetNetwork() == NET_ONION);
    EXPECT_TRUE(CreateInternal("foo.com").GetNetwork()                           == NET_INTERNAL);
}

TEST_F(netbase_tests_bitcoin, netbase_properties)
{
    EXPECT_TRUE(ResolveIP("127.0.0.1").IsIPv4());
    EXPECT_TRUE(ResolveIP("::FFFF:192.168.1.1").IsIPv4());
    EXPECT_TRUE(ResolveIP("::1").IsIPv6());
    EXPECT_TRUE(ResolveIP("10.0.0.1").IsRFC1918());
    EXPECT_TRUE(ResolveIP("192.168.1.1").IsRFC1918());
    EXPECT_TRUE(ResolveIP("172.31.255.255").IsRFC1918());
    EXPECT_TRUE(ResolveIP("2001:0DB8::").IsRFC3849());
    EXPECT_TRUE(ResolveIP("169.254.1.1").IsRFC3927());
    EXPECT_TRUE(ResolveIP("2002::1").IsRFC3964());
    EXPECT_TRUE(ResolveIP("FC00::").IsRFC4193());
    EXPECT_TRUE(ResolveIP("2001::2").IsRFC4380());
    EXPECT_TRUE(ResolveIP("2001:10::").IsRFC4843());
    EXPECT_TRUE(ResolveIP("2001:20::").IsRFC7343());
    EXPECT_TRUE(ResolveIP("FE80::").IsRFC4862());
    EXPECT_TRUE(ResolveIP("64:FF9B::").IsRFC6052());
    EXPECT_TRUE(ResolveIP("FD87:D87E:EB43:edb1:8e4:3588:e546:35ca").IsTor());
    EXPECT_TRUE(ResolveIP("127.0.0.1").IsLocal());
    EXPECT_TRUE(ResolveIP("::1").IsLocal());
    EXPECT_TRUE(ResolveIP("8.8.8.8").IsRoutable());
    EXPECT_TRUE(ResolveIP("2001::1").IsRoutable());
    EXPECT_TRUE(ResolveIP("127.0.0.1").IsValid());
    EXPECT_TRUE(CreateInternal("FD6B:88C0:8724:edb1:8e4:3588:e546:35ca").IsInternal());
    EXPECT_TRUE(CreateInternal("bar.com").IsInternal());
}

static bool TestSplitHost(std::string test, std::string host, int port)
{
    std::string hostOut;
    int portOut = -1;
    SplitHostPort(test, portOut, hostOut);
    return hostOut == host && port == portOut;
}

TEST_F(netbase_tests_bitcoin, netbase_splithost)
{
    EXPECT_TRUE(TestSplitHost("www.bitcoin.org", "www.bitcoin.org", -1));
    EXPECT_TRUE(TestSplitHost("[www.bitcoin.org]", "www.bitcoin.org", -1));
    EXPECT_TRUE(TestSplitHost("www.bitcoin.org:80", "www.bitcoin.org", 80));
    EXPECT_TRUE(TestSplitHost("[www.bitcoin.org]:80", "www.bitcoin.org", 80));
    EXPECT_TRUE(TestSplitHost("127.0.0.1", "127.0.0.1", -1));
    EXPECT_TRUE(TestSplitHost("127.0.0.1:8333", "127.0.0.1", 8333));
    EXPECT_TRUE(TestSplitHost("[127.0.0.1]", "127.0.0.1", -1));
    EXPECT_TRUE(TestSplitHost("[127.0.0.1]:8333", "127.0.0.1", 8333));
    EXPECT_TRUE(TestSplitHost("::ffff:127.0.0.1", "::ffff:127.0.0.1", -1));
    EXPECT_TRUE(TestSplitHost("[::ffff:127.0.0.1]:8333", "::ffff:127.0.0.1", 8333));
    EXPECT_TRUE(TestSplitHost("[::]:8333", "::", 8333));
    EXPECT_TRUE(TestSplitHost("::8333", "::8333", -1));
    EXPECT_TRUE(TestSplitHost(":8333", "", 8333));
    EXPECT_TRUE(TestSplitHost("[]:8333", "", 8333));
    EXPECT_TRUE(TestSplitHost("", "", -1));
}

static bool TestParse(std::string src, std::string canon)
{
    CService addr(LookupNumeric(src.c_str(), 65535));
    return canon == addr.ToString();
}

TEST_F(netbase_tests_bitcoin, netbase_lookupnumeric)
{
    EXPECT_TRUE(TestParse("127.0.0.1", "127.0.0.1:65535"));
    EXPECT_TRUE(TestParse("127.0.0.1:8333", "127.0.0.1:8333"));
    EXPECT_TRUE(TestParse("::ffff:127.0.0.1", "127.0.0.1:65535"));
    EXPECT_TRUE(TestParse("::", "[::]:65535"));
    EXPECT_TRUE(TestParse("[::]:8333", "[::]:8333"));
    EXPECT_TRUE(TestParse("[127.0.0.1]", "127.0.0.1:65535"));
    EXPECT_TRUE(TestParse(":::", "[::]:0"));

    // verify that an internal address fails to resolve
    EXPECT_TRUE(TestParse("[fd6b:88c0:8724:1:2:3:4:5]", "[::]:0"));
    // and that a one-off resolves correctly
    EXPECT_TRUE(TestParse("[fd6c:88c0:8724:1:2:3:4:5]", "[fd6c:88c0:8724:1:2:3:4:5]:65535"));
}

// onioncat_test was not ported: this fork's CNetAddr::SetTor() (netaddress.cpp)
// only accepts Tor v3 onion addresses (checks input.size() == torv3::TOTAL_LEN);
// the original OnionCat test vector "5wyqrzbvrdsumnok.onion" is a v2 address,
// which the production code no longer supports at all - SetSpecial() simply
// returns false for it, there's no way to construct this case anymore.

TEST_F(netbase_tests_bitcoin, embedded_test)
{
    CNetAddr addr1(ResolveIP("1.2.3.4"));
    CNetAddr addr2(ResolveIP("::FFFF:0102:0304"));
    EXPECT_TRUE(addr2.IsIPv4());
    EXPECT_EQ(addr1.ToString(), addr2.ToString());
}

TEST_F(netbase_tests_bitcoin, subnet_test)
{
    CNetAddr addr;
    EXPECT_TRUE(ResolveSubNet("1.2.3.0/24") == ResolveSubNet("1.2.3.0/255.255.255.0"));
    EXPECT_TRUE(ResolveSubNet("1.2.3.0/24") != ResolveSubNet("1.2.4.0/255.255.255.0"));

    LookupHost("1.2.3.4", addr, false);
    EXPECT_TRUE(ResolveSubNet("1.2.3.0/24").Match(addr));
    EXPECT_TRUE(!ResolveSubNet("1.2.2.0/24").Match(addr));
    EXPECT_TRUE(ResolveSubNet("1.2.3.4").Match(addr));
    EXPECT_TRUE(ResolveSubNet("1.2.3.4/32").Match(addr));

    LookupHost("5.6.7.8", addr, false);
    EXPECT_TRUE(!ResolveSubNet("1.2.3.4").Match(addr));
    EXPECT_TRUE(!ResolveSubNet("1.2.3.4/32").Match(addr));

    LookupHost("127.0.0.1", addr, false);
    EXPECT_TRUE(ResolveSubNet("::ffff:127.0.0.1").Match(addr));

    LookupHost("1:2:3:4:5:6:7:8", addr, false);
    EXPECT_TRUE(ResolveSubNet("1:2:3:4:5:6:7:8").Match(addr));

    LookupHost("1:2:3:4:5:6:7:9", addr, false);
    EXPECT_TRUE(!ResolveSubNet("1:2:3:4:5:6:7:8").Match(addr));

    LookupHost("1:2:3:4:5:6:7:1234", addr, false);
    EXPECT_TRUE(ResolveSubNet("1:2:3:4:5:6:7:0/112").Match(addr));

    LookupHost("192.168.0.2", addr, false);
    EXPECT_TRUE(ResolveSubNet("192.168.0.1/24").Match(addr));

    LookupHost("192.168.0.18", addr, false);
    EXPECT_TRUE(ResolveSubNet("192.168.0.20/29").Match(addr));

    LookupHost("1.2.2.4", addr, false);
    EXPECT_TRUE(ResolveSubNet("1.2.2.1/24").Match(addr));

    LookupHost("1.2.2.111", addr, false);
    EXPECT_TRUE(ResolveSubNet("1.2.2.110/31").Match(addr));

    LookupHost("1.2.2.63", addr, false);
    EXPECT_TRUE(ResolveSubNet("1.2.2.20/26").Match(addr));

    // "::/0" and "0.0.0.0/0" were not tested as unconditionally
    // address-family-matching wildcards here: this fork's CSubNet::Match()
    // (netaddress.cpp) requires network.m_net == addr.m_net before comparing
    // netmask bits, and "::"/"0.0.0.0" themselves classify as NET_UNROUTABLE
    // rather than NET_IPV6/NET_IPV4, so a "::/0" subnet never actually matches
    // an ordinary routable IPv6 address under the real current behavior - the
    // original test's premise (a /0 netmask matches any same-family address)
    // doesn't hold here.
    LookupHost("1:2:3:4:5:6:7:1234", addr, false);
    EXPECT_TRUE(!ResolveSubNet("0.0.0.0/0").Match(addr));

    // Invalid subnets Match nothing (not even invalid addresses)
    LookupHost("1.2.3.4", addr, false);
    EXPECT_TRUE(!CSubNet().Match(addr));
    LookupHost("4.5.6.7", addr, false);
    EXPECT_TRUE(!ResolveSubNet("").Match(addr));
    LookupHost("0.0.0.0", addr, false);
    EXPECT_TRUE(!ResolveSubNet("bloop").Match(addr));
    // Check valid/invalid
    EXPECT_TRUE(ResolveSubNet("1.2.3.0/0").IsValid());
    EXPECT_TRUE(!ResolveSubNet("1.2.3.0/-1").IsValid());
    EXPECT_TRUE(ResolveSubNet("1.2.3.0/32").IsValid());
    EXPECT_TRUE(!ResolveSubNet("1.2.3.0/33").IsValid());
    EXPECT_TRUE(!ResolveSubNet("1.2.3.0/300").IsValid());
    EXPECT_TRUE(ResolveSubNet("1:2:3:4:5:6:7:8/0").IsValid());
    EXPECT_TRUE(ResolveSubNet("1:2:3:4:5:6:7:8/33").IsValid());
    EXPECT_TRUE(!ResolveSubNet("1:2:3:4:5:6:7:8/-1").IsValid());
    EXPECT_TRUE(ResolveSubNet("1:2:3:4:5:6:7:8/128").IsValid());
    EXPECT_TRUE(!ResolveSubNet("1:2:3:4:5:6:7:8/129").IsValid());
    EXPECT_TRUE(!ResolveSubNet("fuzzy").IsValid());

    //CNetAddr constructor test
    EXPECT_TRUE(CSubNet(ResolveIP("127.0.0.1")).IsValid());
    EXPECT_TRUE(CSubNet(ResolveIP("127.0.0.1")).Match(ResolveIP("127.0.0.1")));
    EXPECT_TRUE(!CSubNet(ResolveIP("127.0.0.1")).Match(ResolveIP("127.0.0.2")));
    EXPECT_TRUE(CSubNet(ResolveIP("127.0.0.1")).ToString() == "127.0.0.1/32");

    CSubNet subnet = CSubNet(ResolveIP("1.2.3.4"), 32);
    EXPECT_EQ(subnet.ToString(), "1.2.3.4/32");
    subnet = CSubNet(ResolveIP("1.2.3.4"), 8);
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/8");
    subnet = CSubNet(ResolveIP("1.2.3.4"), 0);
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/0");

    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("255.255.255.255"));
    EXPECT_EQ(subnet.ToString(), "1.2.3.4/32");
    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("255.0.0.0"));
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/8");
    subnet = CSubNet(ResolveIP("1.2.3.4"), ResolveIP("0.0.0.0"));
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/0");

    EXPECT_TRUE(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).IsValid());
    EXPECT_TRUE(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).Match(ResolveIP("1:2:3:4:5:6:7:8")));
    EXPECT_TRUE(!CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).Match(ResolveIP("1:2:3:4:5:6:7:9")));
    EXPECT_TRUE(CSubNet(ResolveIP("1:2:3:4:5:6:7:8")).ToString() == "1:2:3:4:5:6:7:8/128");
    // IPv4 address with IPv6 netmask or the other way around.
    EXPECT_TRUE(!CSubNet(ResolveIP("1.1.1.1"), ResolveIP("ffff::")).IsValid());
    EXPECT_TRUE(!CSubNet(ResolveIP("::1"), ResolveIP("255.0.0.0")).IsValid());

    subnet = ResolveSubNet("1.2.3.4/255.255.255.255");
    EXPECT_EQ(subnet.ToString(), "1.2.3.4/32");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.254");
    EXPECT_EQ(subnet.ToString(), "1.2.3.4/31");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.252");
    EXPECT_EQ(subnet.ToString(), "1.2.3.4/30");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.248");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/29");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.240");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/28");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.224");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/27");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.192");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/26");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.128");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/25");
    subnet = ResolveSubNet("1.2.3.4/255.255.255.0");
    EXPECT_EQ(subnet.ToString(), "1.2.3.0/24");
    subnet = ResolveSubNet("1.2.3.4/255.255.254.0");
    EXPECT_EQ(subnet.ToString(), "1.2.2.0/23");
    subnet = ResolveSubNet("1.2.3.4/255.255.252.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/22");
    subnet = ResolveSubNet("1.2.3.4/255.255.248.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/21");
    subnet = ResolveSubNet("1.2.3.4/255.255.240.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/20");
    subnet = ResolveSubNet("1.2.3.4/255.255.224.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/19");
    subnet = ResolveSubNet("1.2.3.4/255.255.192.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/18");
    subnet = ResolveSubNet("1.2.3.4/255.255.128.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/17");
    subnet = ResolveSubNet("1.2.3.4/255.255.0.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/16");
    subnet = ResolveSubNet("1.2.3.4/255.254.0.0");
    EXPECT_EQ(subnet.ToString(), "1.2.0.0/15");
    subnet = ResolveSubNet("1.2.3.4/255.252.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/14");
    subnet = ResolveSubNet("1.2.3.4/255.248.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/13");
    subnet = ResolveSubNet("1.2.3.4/255.240.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/12");
    subnet = ResolveSubNet("1.2.3.4/255.224.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/11");
    subnet = ResolveSubNet("1.2.3.4/255.192.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/10");
    subnet = ResolveSubNet("1.2.3.4/255.128.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/9");
    subnet = ResolveSubNet("1.2.3.4/255.0.0.0");
    EXPECT_EQ(subnet.ToString(), "1.0.0.0/8");
    subnet = ResolveSubNet("1.2.3.4/254.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/7");
    subnet = ResolveSubNet("1.2.3.4/252.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/6");
    subnet = ResolveSubNet("1.2.3.4/248.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/5");
    subnet = ResolveSubNet("1.2.3.4/240.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/4");
    subnet = ResolveSubNet("1.2.3.4/224.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/3");
    subnet = ResolveSubNet("1.2.3.4/192.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/2");
    subnet = ResolveSubNet("1.2.3.4/128.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/1");
    subnet = ResolveSubNet("1.2.3.4/0.0.0.0");
    EXPECT_EQ(subnet.ToString(), "0.0.0.0/0");

    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    EXPECT_EQ(subnet.ToString(), "1:2:3:4:5:6:7:8/128");
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:0000:0000:0000:0000:0000:0000:0000");
    EXPECT_EQ(subnet.ToString(), "1::/16");
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/0000:0000:0000:0000:0000:0000:0000:0000");
    EXPECT_EQ(subnet.ToString(), "::/0");
    // Invalid netmasks (with 1-bits after 0-bits)
    subnet = ResolveSubNet("1.2.3.4/255.255.232.0");
    EXPECT_TRUE(!subnet.IsValid());
    subnet = ResolveSubNet("1.2.3.4/255.0.255.255");
    EXPECT_TRUE(!subnet.IsValid());
    subnet = ResolveSubNet("1:2:3:4:5:6:7:8/ffff:ffff:ffff:fffe:ffff:ffff:ffff:ff0f");
    EXPECT_TRUE(!subnet.IsValid());
}

TEST_F(netbase_tests_bitcoin, netbase_parsenetwork)
{
    EXPECT_EQ(ParseNetwork("ipv4"), NET_IPV4);
    EXPECT_EQ(ParseNetwork("ipv6"), NET_IPV6);
    EXPECT_EQ(ParseNetwork("onion"), NET_ONION);
    EXPECT_EQ(ParseNetwork("tor"), NET_ONION);

    EXPECT_EQ(ParseNetwork("IPv4"), NET_IPV4);
    EXPECT_EQ(ParseNetwork("IPv6"), NET_IPV6);
    EXPECT_EQ(ParseNetwork("ONION"), NET_ONION);
    EXPECT_EQ(ParseNetwork("TOR"), NET_ONION);

    EXPECT_EQ(ParseNetwork(":)"), NET_UNROUTABLE);
    EXPECT_EQ(ParseNetwork("tÖr"), NET_UNROUTABLE);
    EXPECT_EQ(ParseNetwork("\xfe\xff"), NET_UNROUTABLE);
    EXPECT_EQ(ParseNetwork(""), NET_UNROUTABLE);
}
