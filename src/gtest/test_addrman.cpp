// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "addrman.h"
#include <boost/filesystem.hpp>
#include <atomic>
#include <boost/thread.hpp>
#include <chrono>
#include <future>
#include <memory>
#include <set>
#include <string>
#include <thread>

#include "hash.h"
#include "random.h"
#include "util/asmap.h"

#include "net.h"
#include "netbase.h"
#include "gtest/gtestutils.h"
#include "pubkey.h"
#include "rpc/server.h"
#include "chainparams.h"
#include "streams.h"
#include "tinyformat.h"
#include "util/strencodings.h"
#include "version.h"

// Tests for CAddrMan, the peer address database backing GetAddr() replies and
// outbound peer selection: Add/Select/Find/Create/Delete, ASMAP-based bucket
// grouping (autonomous-system-aware address diversity so a single network
// can't dominate the tried/new tables), and ASMAP serialization round-tripping.

#define NODE_NONE 0

// https://stackoverflow.com/questions/16491675/how-to-send-custom-message-in-google-c-testing-framework/29155677
#define GTEST_COUT_NOCOLOR std::cerr << "[          ] [ INFO ] "
namespace testing
{
    namespace internal
    {
    enum GTestColor {
        COLOR_DEFAULT,
        COLOR_RED,
        COLOR_GREEN,
        COLOR_YELLOW
    };

    extern void ColoredPrintf(GTestColor color, const char* fmt, ...);
    }
}
#define PRINTF(...)  do { testing::internal::ColoredPrintf(testing::internal::COLOR_GREEN, "[          ] "); testing::internal::ColoredPrintf(testing::internal::COLOR_YELLOW, __VA_ARGS__); } while(0)

// C++ stream interface
class TestCout : public std::stringstream
{
    public:
        ~TestCout()
        {
            PRINTF("%s",str().c_str());
        }
};

#define GTEST_COUT_COLOR TestCout()

using namespace std;

/* xxd -i est-komodo/data/asmap.raw | sed 's/unsigned char/static unsigned const char/g' */
static unsigned const char asmap_raw[] = {
    0xfb, 0x03, 0xec, 0x0f, 0xb0, 0x3f, 0xc0, 0xfe, 0x00, 0xfb, 0x03, 0xec,
    0x0f, 0xb0, 0x3f, 0xc0, 0xfe, 0x00, 0xfb, 0x03, 0xec, 0x0f, 0xb0, 0xff,
    0xff, 0xfe, 0xff, 0xed, 0xb0, 0xff, 0xd4, 0x86, 0xe6, 0x28, 0x29, 0x00,
    0x00, 0x40, 0x00, 0x00, 0x40, 0x00, 0x40, 0x99, 0x01, 0x00, 0x80, 0x01,
    0x80, 0x04, 0x00, 0x00, 0x05, 0x00, 0x06, 0x00, 0x1c, 0xf0, 0x39
};
unsigned int asmap_raw_len = 59;

class CAddrManTest : public CAddrMan
{
    private:
        uint64_t state;
        bool deterministic;
    public:

        explicit CAddrManTest(bool makeDeterministic = true,
            std::vector<bool> asmap = std::vector<bool>())
        {
            if (makeDeterministic) {
                //  Set addrman addr placement to be deterministic.
                MakeDeterministic();
            }
            deterministic = makeDeterministic;
            m_asmap = asmap;
            state = 1;
        }

        void PrintInternals()
        {
            GTEST_COUT_NOCOLOR << "mapInfo.size() = " << mapInfo.size() << std::endl;
            GTEST_COUT_NOCOLOR << "nNew = " << nNew << std::endl;
        }

        //! Ensure that bucket placement is always the same for testing purposes.
        void MakeDeterministic()
        {
            nKey.SetNull();
            seed_insecure_rand(true);
        }

        //! Number of RandomInt() calls so far (each one reads the RNG).
        int nRandomCalls = 0;

        int RandomInt(int nMax)
        {
            nRandomCalls++;
            state = (CHashWriter(SER_GETHASH, 0) << state).GetHash().GetCheapHash();
            return (unsigned int)(state % nMax);
        }

        CAddrInfo* Find(const CNetAddr& addr, int* pnId = NULL)
        {
            return CAddrMan::Find(addr, pnId);
        }

        // Direct access to GetAddr_, bypassing GetAddr()'s 23%-of-total
        // sampling cap - that cap makes it impractical to reliably exercise
        // GetAddr_'s per-address filtering logic (what this test file's
        // addrman_getaddr_v1_excludes_incompatible_networks needs) without a
        // huge and slow addrman, since the sampled subset's exact membership
        // otherwise depends on the internal random shuffle.
        void GetAddrRaw(std::vector<CAddress>& vAddr, bool wants_addrv2)
        {
            CAddrMan::GetAddr_(vAddr, wants_addrv2);
        }

        CAddrInfo* Create(const CAddress& addr, const CNetAddr& addrSource, int* pnId = NULL)
        {
            return CAddrMan::Create(addr, addrSource, pnId);
        }

        void Delete(int nId)
        {
            CAddrMan::Delete(nId);
        }

        // Used to test deserialization
        std::pair<int, int> GetBucketAndEntry(const CAddress& addr)
        {
            // LOCK(cs);
            int nId = mapAddr[addr];
            for (int bucket = 0; bucket < ADDRMAN_NEW_BUCKET_COUNT; ++bucket) {
                for (int entry = 0; entry < ADDRMAN_BUCKET_SIZE; ++entry) {
                    if (nId == vvNew[bucket][entry]) {
                        return std::pair<int, int>(bucket, entry);
                    }
                }
            }
            return std::pair<int, int>(-1, -1);
        }

        //! CAddrManTest is a friend of CAddrMan, so tests can hold any CAddrMan's
        //! lock (including the global addrman) to simulate it being busy.
        static CCriticalSection& GetLock(CAddrMan& am)
        {
            return am.cs;
        }

        //! Whether an entry returned by Select()/SelectCandidates() lives in the tried table.
        static bool IsInTried(const CAddrInfo& info)
        {
            return info.fInTried;
        }

        void Clear()
        {
            CAddrMan::Clear();
            if (deterministic) {
                nKey.SetNull();
                seed_insecure_rand(true);
            }
        }
};

static CNetAddr ResolveIP(const std::string& ip)
{
        vector<CNetAddr> vIPs;
        CNetAddr addr;
        if (LookupHost(ip.c_str(), vIPs, 256, false)) {
                addr = vIPs[0];
        } else
        {
            // it was BOOST_CHECK_MESSAGE, but we can't use ASSERT or EXPECT outside a test
            GTEST_COUT_COLOR << strprintf("failed to resolve: %s", ip) << std::endl;
        }
        return addr;
}

static CService ResolveService(const std::string& ip, const int port = 0)
{
    CService serv;
    if (!Lookup(ip.c_str(), serv, port, false))
        GTEST_COUT_COLOR << strprintf("failed to resolve: %s:%i", ip, port) << std::endl;
    return serv;
}

static std::vector<bool> FromBytes(const unsigned char* source, int vector_size) {
    std::vector<bool> result(vector_size);
    for (int byte_i = 0; byte_i < vector_size / 8; ++byte_i) {
        unsigned char cur_byte = source[byte_i];
        for (int bit_i = 0; bit_i < 8; ++bit_i) {
            result[byte_i * 8 + bit_i] = (cur_byte >> bit_i) & 1;
        }
    }
    return result;
}

namespace TestAddrmanTests {

    TEST(TestAddrmanTests, display_constants) {

        // Not actually the test, just used to display constants
        GTEST_COUT_COLOR << "ADDRMAN_NEW_BUCKET_COUNT = " << ADDRMAN_NEW_BUCKET_COUNT << std::endl;
        GTEST_COUT_COLOR << "ADDRMAN_TRIED_BUCKET_COUNT = " << ADDRMAN_TRIED_BUCKET_COUNT << std::endl;
        GTEST_COUT_COLOR << "ADDRMAN_BUCKET_SIZE = " << ADDRMAN_BUCKET_SIZE << std::endl;

    }

    TEST(TestAddrmanTests, addrman_simple) {

        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CNetAddr test_addr;
        // Test 1: Does Addrman respond correctly when empty.
        ASSERT_TRUE(addrman.size() == 0);
        CAddrInfo addr_null = addrman.Select();
        ASSERT_TRUE(addr_null.ToString() == "[::]:0");

        // Test 2: Does Addrman::Add work as expected.
        LookupHost("250.1.1.1", test_addr, false);
        CService addr1 = CService(test_addr, 8333);
        // Realistic (non-zero) nTime - an unset nTime defaults to 0, which
        // IsTerrible() treats as "never seen" and Select_() now actively
        // skips over (see addrman.cpp), unlike production addresses which
        // always have a sane nTime by the time they reach addrman.
        CAddress addr1_ca(addr1, NODE_NONE);
        addr1_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr1_ca, source);
        ASSERT_TRUE(addrman.size() == 1);
        CAddrInfo addr_ret1 = addrman.Select();
        ASSERT_TRUE(addr_ret1.ToString() == "250.1.1.1:8333");

        // Test 3: Does IP address deduplication work correctly.
        //  Expected dup IP should not be added.
        LookupHost("250.1.1.1", test_addr, false);
        CService addr1_dup = CService(test_addr, 8333);
        addrman.Add(CAddress(addr1_dup, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 1);

        // Test 5: New table has one addr and we add a diff addr we should
        //  have two addrs.
        LookupHost("250.1.1.2", test_addr, false);
        CService addr2 = CService(test_addr, 8333);
        addrman.Add(CAddress(addr2, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 2);

        // Test 6: AddrMan::Clear() should empty the new table.
        addrman.Clear();
        ASSERT_TRUE(addrman.size() == 0);
        CAddrInfo addr_null2 = addrman.Select();
        ASSERT_TRUE(addr_null2.ToString() == "[::]:0");

    }

    TEST(TestAddrmanTests, addrman_ports) {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;
        // Test 7; Addr with same IP but diff port does not replace existing addr.
        LookupHost("250.1.1.1", test_addr, false);
        CService addr1 = CService(test_addr, 8333);
        // Realistic nTime - see addrman_simple for why an unset (0) one
        // now matters with Select_()'s terrible-address skip.
        CAddress addr1_ca(addr1, NODE_NONE);
        addr1_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr1_ca, source);
        ASSERT_TRUE(addrman.size() == 1);

        LookupHost("250.1.1.1", test_addr, false);
        CService addr1_port = CService(test_addr, 8334);
        addrman.Add(CAddress(addr1_port, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 1);
        CAddrInfo addr_ret2 = addrman.Select();
        ASSERT_TRUE(addr_ret2.ToString() == "250.1.1.1:8333");

        // Test 8: Add same IP but diff port to tried table, it doesn't get added.
        //  Perhaps this is not ideal behavior but it is the current behavior.
        addrman.Good(CAddress(addr1_port, NODE_NONE));
        ASSERT_TRUE(addrman.size() == 1);
        bool newOnly = true;
        CAddrInfo addr_ret3 = addrman.Select(newOnly);
        ASSERT_TRUE(addr_ret3.ToString() == "250.1.1.1:8333");

    }

    TEST(TestAddrmanTests, addrman_select) {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CNetAddr test_addr;
        // Test 9: Select from new with 1 addr in new.
        LookupHost("250.1.1.1", test_addr, false);
        CService addr1 = CService(test_addr, 8333);
        // Realistic nTime - see addrman_simple for why an unset (0) one
        // now matters with Select_()'s terrible-address skip.
        CAddress addr1_ca(addr1, NODE_NONE);
        addr1_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr1_ca, source);
        ASSERT_TRUE(addrman.size() == 1);

        bool newOnly = true;
        CAddrInfo addr_ret1 = addrman.Select(newOnly);
        ASSERT_TRUE(addr_ret1.ToString() == "250.1.1.1:8333");

        // Test 10: move addr to tried, select from new expected nothing returned.
        addrman.Good(CAddress(addr1, NODE_NONE));
        ASSERT_TRUE(addrman.size() == 1);
        CAddrInfo addr_ret2 = addrman.Select(newOnly);
        ASSERT_TRUE(addr_ret2.ToString() == "[::]:0");

        CAddrInfo addr_ret3 = addrman.Select();
        ASSERT_TRUE(addr_ret3.ToString() == "250.1.1.1:8333");

        ASSERT_TRUE(addrman.size() == 1);


        // Add three addresses to new table.
        // Realistic nTime throughout (see Test 9 above) - both for
        // correctness-realism and so Select()'s terrible-address skip
        // doesn't burn its retry budget on every single one of the 1000
        // Select() calls below.
        LookupHost("250.3.1.1", test_addr, false);
        CService addr2 = CService(test_addr, 8333);
        LookupHost("250.3.2.2", test_addr, false);
        CService addr3 = CService(test_addr, 9999);
        LookupHost("250.3.3.3", test_addr, false);
        CService addr4 = CService(test_addr, 9999);

        LookupHost("250.4.1.1", test_addr, false);
        CAddress addr2_ca(addr2, NODE_NONE);
        addr2_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr2_ca, CService(test_addr, 8333));
        LookupHost("250.4.2.2", test_addr, false);
        CAddress addr3_ca(addr3, NODE_NONE);
        addr3_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr3_ca, CService(test_addr, 8333));
        LookupHost("250.4.3.3", test_addr, false);
        CAddress addr4_ca(addr4, NODE_NONE);
        addr4_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr4_ca, CService(test_addr, 8333));

        // Add three addresses to tried table.
        LookupHost("250.3.4.4", test_addr, false);
        CService addr5 = CService(test_addr, 8333);
        LookupHost("250.3.5.5", test_addr, false);
        CService addr6 = CService(test_addr, 7777);
        LookupHost("250.3.6.6", test_addr, false);
        CService addr7 = CService(test_addr, 8333);

        LookupHost("250.4.4.4", test_addr, false);
        CAddress addr5_ca(addr5, NODE_NONE);
        addr5_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr5_ca, CService(test_addr, 8333));
        addrman.Good(CAddress(addr5, NODE_NONE));
        LookupHost("250.4.5.5", test_addr, false);
        CAddress addr6_ca(addr6, NODE_NONE);
        addr6_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr6_ca, CService(test_addr, 8333));
        addrman.Good(CAddress(addr6, NODE_NONE));
        LookupHost("250.4.6.6", test_addr, false);
        CAddress addr7_ca(addr7, NODE_NONE);
        addr7_ca.nTime = (unsigned int)GetTime();
        addrman.Add(addr7_ca, CService(test_addr, 8333));
        addrman.Good(CAddress(addr7, NODE_NONE));

        // Test 11: 6 addrs + 1 addr from last test = 7.
        ASSERT_TRUE(addrman.size() == 7);

        int triedAddr = 0;
        int newAddr = 0;
        for (int i = 0; i<1000; i++) {
            auto addr = addrman.Select().ToString();

            if (addr == "250.3.1.1:8333")
                newAddr++;
            if (addr == "250.3.2.2:9999")
                newAddr++;
            if (addr == "250.3.3.3:9999")
                newAddr++;
            if (addr == "250.1.1.1:8333")
                triedAddr++;
            if (addr == "250.3.4.4:8333")
                triedAddr++;
            if (addr == "250.3.5.5:7777")
                triedAddr++;
            if (addr == "250.3.6.6:8333")
                triedAddr++;
        }

        if (triedAddr > 400) triedAddr = 400;
        if (newAddr > 400) newAddr = 400;

        // Test 12: Select pulls from new and tried regardless of port number.
        // addrman.Select should pull 50% from tried and 50% from new
        // triedAddr and newAddr should be close to 500 each but using 400 to allow
        // for random chance
        ASSERT_TRUE(triedAddr == 400);
        ASSERT_TRUE(newAddr == 400);

    }

    TEST(TestAddrmanTests, addrman_new_collisions)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;

        int addrSize = 0;
        for (int i = 1; i < 18; i++) {

            LookupHost(("250.1.2." + std::to_string(i)).c_str(), test_addr, false);
            // CService addr = CService(test_addr, 8333);
            CAddress addr = CAddress(CService(test_addr, 8333), NODE_NONE);
            addr.nTime = GetTime();


            bool added = addrman.Add(addr, source);
            addrSize++;
            //Test 13: No collision in new table yet.
            if (i == 12) {
              addrSize--;
            } //asmap bucket position collision at 250.1.2.12

            ASSERT_TRUE(addrman.size() == addrSize);
        }

        ASSERT_TRUE(addrman.size() == 16);

        //Test 14: new table collision!
        LookupHost("250.1.2.17", test_addr, false);
        CService addr1 = CService(test_addr, 8333);
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 16);

        LookupHost("250.1.2.18", test_addr, false);
        CService addr2 = CService(test_addr, 8333);
        addrman.Add(CAddress(addr2, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 17);
    }

    TEST(TestAddrmanTests, addrman_tried_collisions)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;

        for (unsigned int i = 1; i < 80; i++) {
            LookupHost(("250.1.1." + boost::to_string(i)).c_str(), test_addr, false);
            CService addr = CService(test_addr, 8333);
            addrman.Add(CAddress(addr, NODE_NONE), source);
            addrman.Good(CAddress(addr, NODE_NONE));

            //Test 15: No collision in tried table yet.
            // GTEST_COUT << addrman.size() << std::endl;
            ASSERT_TRUE(addrman.size() == i);
        }

        //Test 16: tried table collision!
        LookupHost("250.1.1.79", test_addr, false);
        CService addr1 = CService(test_addr, 8333);
        addrman.Add(CAddress(addr1, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 79);

        LookupHost("250.1.1.80", test_addr, false);
        CService addr2 = CService(test_addr, 8333);
        addrman.Add(CAddress(addr2, NODE_NONE), source);
        ASSERT_TRUE(addrman.size() == 80);
    }

    TEST(TestAddrmanTests, addrman_find)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;
        LookupHost("250.1.2.1", test_addr, false);
        CAddress addr1 = CAddress(CService(test_addr, 8333), NODE_NONE);
        LookupHost("250.1.2.1", test_addr, false);
        CAddress addr2 = CAddress(CService(test_addr, 9999), NODE_NONE);
        LookupHost("250.255.2.1", test_addr, false);
        CAddress addr3 = CAddress(CService(test_addr, 8333), NODE_NONE);

        CNetAddr source1;
        LookupHost("252.2.2.1", source1, false);

        CNetAddr source2;
        LookupHost("252.2.2.2", source2, false);

        addrman.Add(addr1, source1);
        addrman.Add(addr2, source2);
        addrman.Add(addr3, source1);

        // Test 17: ensure Find returns an IP matching what we searched on.
        CAddrInfo* info1 = addrman.Find(addr1);
        ASSERT_TRUE(info1);
        if (info1)
            ASSERT_TRUE(info1->ToString() == "250.1.2.1:8333");

        // Test 18; Find does not discriminate by port number.
        CAddrInfo* info2 = addrman.Find(addr2);
        ASSERT_TRUE(info2);
        if (info2)
            ASSERT_TRUE(info2->ToString() == info1->ToString());

        // Test 19: Find returns another IP matching what we searched on.
        CAddrInfo* info3 = addrman.Find(addr3);
        ASSERT_TRUE(info3);
        if (info3)
            ASSERT_TRUE(info3->ToString() == "250.255.2.1:8333");
    }

    TEST(TestAddrmanTests, addrman_create)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;
        LookupHost("250.1.2.1", test_addr, false);
        CAddress addr1 = CAddress(CService(test_addr, 8333), NODE_NONE);

        CNetAddr source1;
        LookupHost("252.1.2.1", source1, false);

        int nId;
        CAddrInfo* pinfo = addrman.Create(addr1, source1, &nId);

        // Test 20: The result should be the same as the input addr.
        ASSERT_TRUE(pinfo->ToString() == "250.1.2.1:8333");

        CAddrInfo* info2 = addrman.Find(addr1);
        ASSERT_TRUE(info2->ToString() == "250.1.2.1:8333");
    }


    TEST(TestAddrmanTests, addrman_delete)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        ASSERT_TRUE(addrman.size() == 0);

        CNetAddr test_addr;
        LookupHost("250.1.2.1", test_addr, false);
        CAddress addr1 = CAddress(CService(test_addr, 8333), NODE_NONE);

        CNetAddr source1;
        LookupHost("252.1.2.1", source1, false);

        int nId;
        addrman.Create(addr1, source1, &nId);

        // Test 21: Delete should actually delete the addr.
        ASSERT_TRUE(addrman.size() == 1);
        addrman.Delete(nId);
        ASSERT_TRUE(addrman.size() == 0);
        CAddrInfo* info2 = addrman.Find(addr1);
        ASSERT_TRUE(info2 == NULL);
    }

    TEST(TestAddrmanTests, addrman_getaddr)
    {
        CAddrManTest addrman;

        // Set addrman addr placement to be deterministic.
        addrman.MakeDeterministic();

        // Test 22: Sanity check, GetAddr should never return anything if addrman
        //  is empty.
        ASSERT_TRUE(addrman.size() == 0);
        vector<CAddress> vAddr1 = addrman.GetAddr();
        ASSERT_TRUE(vAddr1.size() == 0);

        CNetAddr test_addr;

        LookupHost("250.250.2.1", test_addr, false);
        CAddress addr1 = CAddress(CService(test_addr, 8333), NODE_NONE);
        addr1.nTime = GetTime(); // Set time so isTerrible = false
        LookupHost("250.251.2.2", test_addr, false);
        CAddress addr2 = CAddress(CService(test_addr, 9999), NODE_NONE);
        addr2.nTime = GetTime();
        LookupHost("250.251.2.3", test_addr, false);
        CAddress addr3 = CAddress(CService(test_addr, 8333), NODE_NONE);
        addr3.nTime = GetTime();
        LookupHost("250.251.2.4", test_addr, false);
        CAddress addr4 = CAddress(CService(test_addr, 8333), NODE_NONE);
        addr4.nTime = GetTime();
        LookupHost("250.251.2.5", test_addr, false);
        CAddress addr5 = CAddress(CService(test_addr, 8333), NODE_NONE);
        addr5.nTime = GetTime();

        CNetAddr source1;
        LookupHost("252.1.2.1", source1, false);
        CNetAddr source2;
        LookupHost("252.2.3.3", source2, false);

        // Test 23: Ensure GetAddr works with new addresses.
        addrman.Add(addr1, source1);
        addrman.Add(addr2, source2);
        addrman.Add(addr3, source1);
        addrman.Add(addr4, source2);
        addrman.Add(addr5, source1);

        // GetAddr returns 23% of addresses, 23% of 5 is 1 rounded down.
        ASSERT_TRUE(addrman.GetAddr().size() == 1);

        // Test 24: Ensure GetAddr works with new and tried addresses.
        addrman.Good(CAddress(addr1, NODE_NONE));
        addrman.Good(CAddress(addr2, NODE_NONE));
        ASSERT_TRUE(addrman.GetAddr().size() == 1);

        // Test 25: Ensure GetAddr still returns 23% when addrman has many addrs.
        for (unsigned int i = 1; i < (8 * 256); i++) {
            int octet1 = i % 256;
            int octet2 = (i / 256) % 256;
            int octet3 = (i / (256 * 2)) % 256;
            string strAddr = boost::to_string(octet1) + "." + boost::to_string(octet2) + "." + boost::to_string(octet3) + ".23";

            LookupHost(strAddr.c_str(), test_addr, false);
            CAddress addr = CAddress(CService(test_addr, 8333), NODE_NONE);

            // Ensure that for all addrs in addrman, isTerrible == false.
            addr.nTime = GetTime();

            CNetAddr source;
            LookupHost(strAddr.c_str(), source, false);
            addrman.Add(addr, source);

            if (i % 8 == 0)
                addrman.Good(addr);
        }
        vector<CAddress> vAddr = addrman.GetAddr();

        size_t percent23 = (addrman.size() * 23) / 100;
        ASSERT_TRUE(vAddr.size() == percent23);
        ASSERT_TRUE(vAddr.size() == 460);
        // (Addrman.size() < number of addresses added) due to address collisons.
        ASSERT_TRUE(addrman.size() == 2001);
    }

    TEST(TestAddrmanTests, caddrinfo_get_tried_bucket_legacy)
    {
        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.1.1", 8333), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.1.1", 9999), NODE_NONE);

        CNetAddr source1 = ResolveIP("250.1.1.1");

        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        std::vector<bool> asmap; // use /16

        ASSERT_EQ(info1.GetTriedBucket(nKey1, asmap), 40);

        // Test: Make sure key actually randomizes bucket placement. A fail on
        //  this test could be a security issue.
        ASSERT_TRUE(info1.GetTriedBucket(nKey1, asmap) != info1.GetTriedBucket(nKey2, asmap));

        // Test: Two addresses with same IP but different ports can map to
        //  different buckets because they have different keys.
        CAddrInfo info2 = CAddrInfo(addr2, source1);

        ASSERT_TRUE(info1.GetKey() != info2.GetKey());
        ASSERT_TRUE(info1.GetTriedBucket(nKey1, asmap) != info2.GetTriedBucket(nKey1, asmap));

        std::set<int> buckets;
        for (int i = 0; i < 255; i++) {
            CAddrInfo infoi = CAddrInfo(
                CAddress(ResolveService("250.1.1." + boost::to_string(i)), NODE_NONE),
                ResolveIP("250.1.1." + boost::to_string(i)));
            int bucket = infoi.GetTriedBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the same /16 prefix should
        // never get more than 8 buckets with legacy grouping
        ASSERT_EQ(buckets.size(), 8U);

        buckets.clear();
        for (int j = 0; j < 255; j++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("250." + boost::to_string(j) + ".1.1"), NODE_NONE),
                ResolveIP("250." + boost::to_string(j) + ".1.1"));
            int bucket = infoj.GetTriedBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different /16 prefix should map to more than
        // 8 buckets with legacy grouping
        ASSERT_EQ(buckets.size(), 160U);
    }

    TEST(TestAddrmanTests, caddrinfo_get_new_bucket_legacy)
    {
        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);

        CNetAddr source1 = ResolveIP("250.1.2.1");

        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        std::vector<bool> asmap; // use /16

        // Test: Make sure the buckets are what we expect
        ASSERT_EQ(info1.GetNewBucket(nKey1, asmap), 786);
        ASSERT_EQ(info1.GetNewBucket(nKey1, source1, asmap), 786);

        // Test: Make sure key actually randomizes bucket placement. A fail on
        //  this test could be a security issue.
        ASSERT_TRUE(info1.GetNewBucket(nKey1, asmap) != info1.GetNewBucket(nKey2, asmap));

        // Test: Ports should not affect bucket placement in the addr
        CAddrInfo info2 = CAddrInfo(addr2, source1);
        ASSERT_TRUE(info1.GetKey() != info2.GetKey());
        ASSERT_EQ(info1.GetNewBucket(nKey1, asmap), info2.GetNewBucket(nKey1, asmap));

        std::set<int> buckets;
        for (int i = 0; i < 255; i++) {
            CAddrInfo infoi = CAddrInfo(
                CAddress(ResolveService("250.1.1." + boost::to_string(i)), NODE_NONE),
                ResolveIP("250.1.1." + boost::to_string(i)));
            int bucket = infoi.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the same group (\16 prefix for IPv4) should
        //  always map to the same bucket.
        ASSERT_EQ(buckets.size(), 1U);

        buckets.clear();
        for (int j = 0; j < 4 * 255; j++) {
            CAddrInfo infoj = CAddrInfo(CAddress(
                                            ResolveService(
                                                boost::to_string(250 + (j / 255)) + "." + boost::to_string(j % 256) + ".1.1"), NODE_NONE),
                ResolveIP("251.4.1.1"));
            int bucket = infoj.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the same source groups should map to NO MORE
        //  than 64 buckets.
        ASSERT_TRUE(buckets.size() <= 64);

        buckets.clear();
        for (int p = 0; p < 255; p++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("250.1.1.1"), NODE_NONE),
                ResolveIP("250." + boost::to_string(p) + ".1.1"));
            int bucket = infoj.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different source groups should map to MORE
        //  than 64 buckets.
        ASSERT_TRUE(buckets.size() > 64);

    }

    // The following three test cases use asmap_raw[] from asmap.raw file
    // We use an artificial minimal mock mapping
    // 250.0.0.0/8 AS1000
    // 101.1.0.0/16 AS1
    // 101.2.0.0/16 AS2
    // 101.3.0.0/16 AS3
    // 101.4.0.0/16 AS4
    // 101.5.0.0/16 AS5
    // 101.6.0.0/16 AS6
    // 101.7.0.0/16 AS7
    // 101.8.0.0/16 AS8

    TEST(TestAddrmanTests, caddrinfo_get_tried_bucket)
    {
        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.1.1", 8333), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.1.1", 9999), NODE_NONE);

        CNetAddr source1 = ResolveIP("250.1.1.1");


        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        std::vector<bool> asmap = FromBytes(asmap_raw, sizeof(asmap_raw) * 8);

        ASSERT_EQ(info1.GetTriedBucket(nKey1, asmap), 236);

        // Test: Make sure key actually randomizes bucket placement. A fail on
        //  this test could be a security issue.
        ASSERT_TRUE(info1.GetTriedBucket(nKey1, asmap) != info1.GetTriedBucket(nKey2, asmap));

        // Test: Two addresses with same IP but different ports can map to
        //  different buckets because they have different keys.
        CAddrInfo info2 = CAddrInfo(addr2, source1);

        ASSERT_TRUE(info1.GetKey() != info2.GetKey());
        ASSERT_TRUE(info1.GetTriedBucket(nKey1, asmap) != info2.GetTriedBucket(nKey1, asmap));

        std::set<int> buckets;
        for (int j = 0; j < 255; j++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("101." + boost::to_string(j) + ".1.1"), NODE_NONE),
                ResolveIP("101." + boost::to_string(j) + ".1.1"));
            int bucket = infoj.GetTriedBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different /16 prefix MAY map to more than
        // 8 buckets.
        ASSERT_TRUE(buckets.size() > 8);

        buckets.clear();
        for (int j = 0; j < 255; j++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("250." + boost::to_string(j) + ".1.1"), NODE_NONE),
                ResolveIP("250." + boost::to_string(j) + ".1.1"));
            int bucket = infoj.GetTriedBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different /16 prefix MAY NOT map to more than
        // 8 buckets.
        ASSERT_TRUE(buckets.size() == 8);
    }

    TEST(TestAddrmanTests, caddrinfo_get_new_bucket)
    {
        CAddrManTest addrman;

        CAddress addr1 = CAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);

        CNetAddr source1 = ResolveIP("250.1.2.1");

        CAddrInfo info1 = CAddrInfo(addr1, source1);

        uint256 nKey1 = (uint256)(CHashWriter(SER_GETHASH, 0) << 1).GetHash();
        uint256 nKey2 = (uint256)(CHashWriter(SER_GETHASH, 0) << 2).GetHash();

        std::vector<bool> asmap = FromBytes(asmap_raw, sizeof(asmap_raw) * 8);

        // Test: Make sure the buckets are what we expect
        ASSERT_EQ(info1.GetNewBucket(nKey1, asmap), 795);
        ASSERT_EQ(info1.GetNewBucket(nKey1, source1, asmap), 795);

        // Test: Make sure key actually randomizes bucket placement. A fail on
        //  this test could be a security issue.
        ASSERT_TRUE(info1.GetNewBucket(nKey1, asmap) != info1.GetNewBucket(nKey2, asmap));

        // Test: Ports should not affect bucket placement in the addr
        CAddrInfo info2 = CAddrInfo(addr2, source1);
        ASSERT_TRUE(info1.GetKey() != info2.GetKey());
        ASSERT_EQ(info1.GetNewBucket(nKey1, asmap), info2.GetNewBucket(nKey1, asmap));

        std::set<int> buckets;
        for (int i = 0; i < 255; i++) {
            CAddrInfo infoi = CAddrInfo(
                CAddress(ResolveService("250.1.1." + boost::to_string(i)), NODE_NONE),
                ResolveIP("250.1.1." + boost::to_string(i)));
            int bucket = infoi.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the same /16 prefix
        // usually map to the same bucket.
        ASSERT_EQ(buckets.size(), 1U);

        buckets.clear();
        for (int j = 0; j < 4 * 255; j++) {
            CAddrInfo infoj = CAddrInfo(CAddress(
                                            ResolveService(
                                                boost::to_string(250 + (j / 255)) + "." + boost::to_string(j % 256) + ".1.1"), NODE_NONE),
                ResolveIP("251.4.1.1"));
            int bucket = infoj.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the same source /16 prefix should not map to more
        // than 64 buckets.
        ASSERT_TRUE(buckets.size() <= 64);

        buckets.clear();
        for (int p = 0; p < 255; p++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("250.1.1.1"), NODE_NONE),
                ResolveIP("101." + boost::to_string(p) + ".1.1"));
            int bucket = infoj.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different source /16 prefixes usually map to MORE
        // than 1 bucket.
        ASSERT_TRUE(buckets.size() > 1);

        buckets.clear();
        for (int p = 0; p < 255; p++) {
            CAddrInfo infoj = CAddrInfo(
                CAddress(ResolveService("250.1.1.1"), NODE_NONE),
                ResolveIP("250." + boost::to_string(p) + ".1.1"));
            int bucket = infoj.GetNewBucket(nKey1, asmap);
            buckets.insert(bucket);
        }
        // Test: IP addresses in the different source /16 prefixes sometimes map to NO MORE
        // than 1 bucket.
        ASSERT_TRUE(buckets.size() == 1);
    }

    TEST(TestAddrmanTests, addrman_serialization)
    {
        std::vector<bool> asmap1 = FromBytes(asmap_raw, sizeof(asmap_raw) * 8);

        CAddrManTest addrman_asmap1(true, asmap1);
        CAddrManTest addrman_asmap1_dup(true, asmap1);
        CAddrManTest addrman_noasmap;
        CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);

        CAddress addr = CAddress(ResolveService("250.1.1.1"), NODE_NONE);
        CNetAddr default_source;

        addrman_asmap1.Add(addr, default_source);

        stream << addrman_asmap1;
        // serizalizing/deserializing addrman with the same asmap
        stream >> addrman_asmap1_dup;

        std::pair<int, int> bucketAndEntry_asmap1 = addrman_asmap1.GetBucketAndEntry(addr);
        std::pair<int, int> bucketAndEntry_asmap1_dup = addrman_asmap1_dup.GetBucketAndEntry(addr);
        ASSERT_TRUE(bucketAndEntry_asmap1.second != -1);
        ASSERT_TRUE(bucketAndEntry_asmap1_dup.second != -1);

        ASSERT_TRUE(bucketAndEntry_asmap1.first == bucketAndEntry_asmap1_dup.first);
        ASSERT_TRUE(bucketAndEntry_asmap1.second == bucketAndEntry_asmap1_dup.second);

        // deserializing asmaped peers.dat to non-asmaped addrman
        stream << addrman_asmap1;
        stream >> addrman_noasmap;
        std::pair<int, int> bucketAndEntry_noasmap = addrman_noasmap.GetBucketAndEntry(addr);
        ASSERT_TRUE(bucketAndEntry_noasmap.second != -1);
        ASSERT_TRUE(bucketAndEntry_asmap1.first != bucketAndEntry_noasmap.first);
        ASSERT_TRUE(bucketAndEntry_asmap1.second != bucketAndEntry_noasmap.second);

        // deserializing non-asmaped peers.dat to asmaped addrman
        addrman_asmap1.Clear();
        addrman_noasmap.Clear();
        addrman_noasmap.Add(addr, default_source);
        // GTEST_COUT_COLOR << addr.ToString() << " - " << default_source.ToString() << " - " << addrman_noasmap.size() << std::endl;
        // addrman_noasmap.PrintInternals();
        stream << addrman_noasmap;
        // std::string strHex = HexStr(stream.begin(), stream.end());
        // GTEST_COUT_COLOR << strHex << std::endl;

        stream >> addrman_asmap1;
        std::pair<int, int> bucketAndEntry_asmap1_deser = addrman_asmap1.GetBucketAndEntry(addr);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser.second != -1);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser.first != bucketAndEntry_noasmap.first);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser.first == bucketAndEntry_asmap1_dup.first);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser.second == bucketAndEntry_asmap1_dup.second);

        // used to map to different buckets, now maps to the same bucket.
        addrman_asmap1.Clear();
        addrman_noasmap.Clear();
        CAddress addr1 = CAddress(ResolveService("250.1.1.1"), NODE_NONE);
        CAddress addr2 = CAddress(ResolveService("250.2.1.1"), NODE_NONE);
        addrman_noasmap.Add(addr, default_source);
        addrman_noasmap.Add(addr2, default_source);
        std::pair<int, int> bucketAndEntry_noasmap_addr1 = addrman_noasmap.GetBucketAndEntry(addr1);
        std::pair<int, int> bucketAndEntry_noasmap_addr2 = addrman_noasmap.GetBucketAndEntry(addr2);
        ASSERT_TRUE(bucketAndEntry_noasmap_addr1.first != bucketAndEntry_noasmap_addr2.first);
        ASSERT_TRUE(bucketAndEntry_noasmap_addr1.second != bucketAndEntry_noasmap_addr2.second);
        stream << addrman_noasmap;
        stream >> addrman_asmap1;
        std::pair<int, int> bucketAndEntry_asmap1_deser_addr1 = addrman_asmap1.GetBucketAndEntry(addr1);
        std::pair<int, int> bucketAndEntry_asmap1_deser_addr2 = addrman_asmap1.GetBucketAndEntry(addr2);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser_addr1.first == bucketAndEntry_asmap1_deser_addr2.first);
        ASSERT_TRUE(bucketAndEntry_asmap1_deser_addr1.second != bucketAndEntry_asmap1_deser_addr2.second);
    }

    // Regression test for a connectivity bug found during a networking
    // review: Select_()'s reachability deprioritization was inverted -
    // `if (info.IsReachableNetwork())` reduced the acceptance weight of
    // *reachable* candidates by 4x, leaving addresses on networks we can't
    // actually dial (e.g. stored Tor addresses with no proxy configured) at
    // full weight. The bug didn't cause bad connection attempts (net.cpp's
    // ThreadOpenConnections has its own IsReachable() guard before dialing
    // anything Select() returns), but it meant Select() - and therefore
    // ThreadOpenConnections's bounded 100-attempt search - spent a
    // disproportionate share of its budget on addresses that can never be
    // used, directly degrading how fast the node finds a usable peer.
    // Covers both the "tried" and "new" table code paths, since the bug was
    // duplicated identically in each.
    TEST(TestAddrmanTests, addrman_select_deprioritizes_unreachable_network)
    {
        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CNetAddr reachableIp;
        LookupHost("250.1.1.1", reachableIp, false);
        CService reachableAddr(reachableIp, 8233);

        CNetAddr unreachableOnion;
        ASSERT_TRUE(unreachableOnion.SetSpecial(
            "pg6mmjiyjmcrsslvykfwnntlaru7p5svn6y2ymmju6nubxndf4pscryd.onion"));
        CService unreachableAddr(unreachableOnion, 8233);

        // Simulate a node with no Tor proxy configured: onion addresses are
        // stored (gossip doesn't care whether we can use them) but not
        // currently reachable. Restore afterwards - this is process-global
        // state shared with every other test in this binary.
        SetReachable(NET_ONION, false);
        struct ReachabilityGuard {
            ~ReachabilityGuard() { SetReachable(NET_ONION, true); }
        } reachabilityGuard;

        const int kIterations = 500;

        auto countSelections = [&](bool newOnlyForSelect) {
            // One addrman instance reused across every draw: CAddrManTest's
            // deterministic RandomInt() is a hash chain seeded from a fixed
            // starting state in its constructor (regardless of whether
            // MakeDeterministic() is called), so recreating the object each
            // iteration would replay the exact same "random" decision every
            // time - not a statistical sample at all, just one outcome
            // repeated kIterations times. Select() itself doesn't mutate the
            // entries it scores (no Attempt() call in there), so reusing one
            // instance doesn't bias GetChance()/IsJustTried() between draws.
            CAddrManTest addrman;
            // Give both a realistic (non-zero, non-stale) nTime - a real
            // addrman entry's nTime is essentially never 0 in practice
            // (set by sanitization on ADDR receipt, or GetTime() at every
            // other insertion site), and leaving it unset here would make
            // both addresses IsTerrible() under GetChance()'s staleness
            // penalty, swamping the reachability signal this test is
            // actually isolating.
            CAddress reachableCAddr(reachableAddr, NODE_NONE);
            reachableCAddr.nTime = (unsigned int)GetTime();
            CAddress unreachableCAddr(unreachableAddr, NODE_NONE);
            unreachableCAddr.nTime = (unsigned int)GetTime();
            addrman.Add(reachableCAddr, source);
            addrman.Add(unreachableCAddr, source);
            if (!newOnlyForSelect) {
                addrman.Good(reachableAddr);
                addrman.Good(unreachableAddr);
            }

            int reachableCount = 0;
            int unreachableCount = 0;
            for (int i = 0; i < kIterations; i++) {
                CAddrInfo picked = addrman.Select(newOnlyForSelect);
                if (picked.ToString() == reachableAddr.ToString()) {
                    reachableCount++;
                } else if (picked.ToString() == unreachableAddr.ToString()) {
                    unreachableCount++;
                }
            }
            return std::make_pair(reachableCount, unreachableCount);
        };

        {
            const auto counts = countSelections(/*newOnlyForSelect=*/false);
            EXPECT_GT(counts.first, counts.second)
                << "Select() (tried table) must prefer the reachable-network address: "
                << "reachable picked " << counts.first << " times, unreachable picked "
                << counts.second << " times";
        }
        {
            const auto counts = countSelections(/*newOnlyForSelect=*/true);
            EXPECT_GT(counts.first, counts.second)
                << "Select(newOnly=true) (new table) must prefer the reachable-network "
                << "address: reachable picked " << counts.first << " times, unreachable "
                << "picked " << counts.second << " times";
        }
    }

    // Regression test: GetChance() previously gave a "terrible" address
    // (IsTerrible() == true - never seen within ADDRMAN_HORIZON_DAYS, a
    // zero/bogus nTime, or repeated failures with no success) the exact
    // same selection weight as a fresh, never-tried address. Select()
    // itself never calls IsTerrible() at all, so nothing stopped an
    // ancient or bogus-timestamp entry from being handed out and dialed
    // just as readily as a good one - only GetChance()'s weighting can
    // fix this, since it's the only signal Select() actually consults.
    TEST(TestAddrmanTests, addrman_select_deprioritizes_terrible_address)
    {
        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CNetAddr freshIp;
        LookupHost("250.1.1.1", freshIp, false);
        CAddress freshAddr(CService(freshIp, 8233), NODE_NONE);
        freshAddr.nTime = (unsigned int)GetTime();

        CNetAddr terribleIp;
        LookupHost("250.1.1.2", terribleIp, false);
        CAddress terribleAddr(CService(terribleIp, 8233), NODE_NONE);
        // Well beyond ADDRMAN_HORIZON_DAYS (30) - IsTerrible() must be true.
        terribleAddr.nTime = (unsigned int)(GetTime() - 400 * 24 * 60 * 60);

        const int kIterations = 500;

        CAddrManTest addrman;
        addrman.Add(freshAddr, source);
        addrman.Add(terribleAddr, source);

        int freshCount = 0;
        int terribleCount = 0;
        for (int i = 0; i < kIterations; i++) {
            CAddrInfo picked = addrman.Select(/*newOnly=*/true);
            if (picked.ToString() == CService(freshIp, 8233).ToString()) {
                freshCount++;
            } else if (picked.ToString() == CService(terribleIp, 8233).ToString()) {
                terribleCount++;
            }
        }

        EXPECT_GT(freshCount, terribleCount)
            << "Select() must strongly prefer a fresh address over a terrible one: "
            << "fresh picked " << freshCount << " times, terrible picked "
            << terribleCount << " times";
    }

    // SweepTerrible() must actually remove terrible entries from the table
    // (not just deprioritize them at selection time - see the two tests
    // above), leaving good entries untouched, so bad addresses don't sit
    // in addrman indefinitely waiting for a lazy, collision-triggered
    // eviction that may never come for a sparsely-populated bucket.
    TEST(TestAddrmanTests, addrman_sweep_terrible_removes_only_terrible)
    {
        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CNetAddr freshIp;
        LookupHost("250.1.1.1", freshIp, false);
        CAddress freshAddr(CService(freshIp, 8233), NODE_NONE);
        freshAddr.nTime = (unsigned int)GetTime();

        CNetAddr terribleIp;
        LookupHost("250.1.1.2", terribleIp, false);
        CAddress terribleAddr(CService(terribleIp, 8233), NODE_NONE);
        terribleAddr.nTime = (unsigned int)(GetTime() - 400 * 24 * 60 * 60);

        CAddrManTest addrman;
        addrman.Add(freshAddr, source);
        addrman.Add(terribleAddr, source);
        ASSERT_EQ(addrman.size(), 2);

        addrman.SweepTerrible();

        EXPECT_EQ(addrman.size(), 1)
            << "SweepTerrible() should have removed exactly the terrible address";

        int nId = -1;
        CAddrInfo* pFresh = addrman.Find(freshAddr, &nId);
        EXPECT_NE(pFresh, nullptr) << "the fresh address must survive the sweep";

        CAddrInfo* pTerrible = addrman.Find(terribleAddr, &nId);
        EXPECT_EQ(pTerrible, nullptr) << "the terrible address must be gone after the sweep";
    }

    // Regression test for a privacy/waste bug: GetAddr_()'s legacy
    // (non-ADDRv2) branch used to push every non-terrible address
    // unconditionally, regardless of network, relying entirely on
    // CService::Serialize() to silently zero out Tor v3/I2P/CJDNS addresses
    // on the wire (see CNetAddr::IsAddrV1Compatible()/SerializeV1Array()).
    // That meant legacy peers burned slots in their capped GetAddr response
    // on useless 0.0.0.0-equivalent placeholder entries instead of getting
    // real addresses. The addrv2 branch already filtered by network; the
    // legacy branch now does too, via IsAddrV1Compatible().
    TEST(TestAddrmanTests, addrman_getaddr_v1_excludes_incompatible_networks)
    {
        CAddrManTest addrman;
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        // A generous, mostly-onion pool so the legacy branch would almost
        // certainly have included at least one V1-incompatible address
        // before the fix (verified directly by reverting the fix locally).
        // Built via the raw BIP155 wire format (network id 0x04 = TORv3,
        // length 0x20 = 32 bytes), same as test_net_bitcoin.cpp's
        // cnetaddr_unserialize_v2 - CNetAddr::SetTor() is private and only
        // accepts real checksummed ".onion" strings, so this is the
        // straightforward way to generate many distinct valid TORv3
        // addresses without needing 30 real onion hostnames.
        for (int i = 1; i <= 30; i++) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);
            s << MakeSpan(ParseHex(strprintf("0420%064x", i)));
            CNetAddr onion;
            s >> onion;
            ASSERT_TRUE(onion.IsTor()) << "iteration " << i;
            CAddress addr(CService(onion, 8233), NODE_NONE);
            addr.nTime = GetTime();
            addrman.Add(addr, source);
        }
        for (int i = 1; i <= 10; i++) {
            std::string strAddr = strprintf("250.%d.%d.23", i, i);
            CNetAddr ipv4;
            LookupHost(strAddr.c_str(), ipv4, false);
            CAddress addr(CService(ipv4, 8233), NODE_NONE);
            addr.nTime = GetTime();
            addrman.Add(addr, source);
        }

        std::vector<CAddress> vAddrLegacy;
        addrman.GetAddrRaw(vAddrLegacy, /*wants_addrv2=*/false);
        ASSERT_GT(vAddrLegacy.size(), 0u) << "test setup didn't produce any sampled addresses";
        for (const CAddress& addr : vAddrLegacy) {
            EXPECT_TRUE(addr.IsAddrV1Compatible())
                << "legacy GetAddr() response must never include a V1-incompatible "
                   "address (got " << addr.ToString() << ")";
        }
    }

    static CAddress MakeI2PAddress(int i, int64_t nTime)
    {
        CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
        s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);
        // BIP155 network id 0x05 = I2P, length 0x20 = 32 bytes
        s << MakeSpan(ParseHex(strprintf("0520%064x", i)));
        CNetAddr i2p;
        s >> i2p;
        EXPECT_TRUE(i2p.IsI2P()) << "iteration " << i;
        CAddress addr(CService(i2p, 45452), NODE_NONE);
        addr.nTime = nTime;
        return addr;
    }

    // SelectCandidates() is what the I2P dialer threads use to find dial candidates in a
    // single pass under one addrman lock acquisition, instead of calling Select() (over
    // every network) many times and discarding almost everything it returns.
    TEST(TestAddrmanTests, addrman_select_candidates_filters_network_and_limit)
    {
        CAddrManTest addrman;
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        size_t nI2P = 0, nOnion = 0;
        for (int i = 1; i <= 12; i++) {
            if (addrman.Add(MakeI2PAddress(i, GetTime()), source))
                nI2P++;
        }
        for (int i = 1; i <= 5; i++) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s.SetVersion(s.GetVersion() | ADDRV2_FORMAT);
            s << MakeSpan(ParseHex(strprintf("0420%064x", i)));
            CNetAddr onion;
            s >> onion;
            CAddress addr(CService(onion, 8233), NODE_NONE);
            addr.nTime = GetTime();
            if (addrman.Add(addr, source))
                nOnion++;
        }
        for (int i = 1; i <= 8; i++) {
            CNetAddr ipv4;
            LookupHost(strprintf("250.%d.%d.23", i, i).c_str(), ipv4, false);
            CAddress addr(CService(ipv4, 8233), NODE_NONE);
            addr.nTime = GetTime();
            addrman.Add(addr, source);
        }
        ASSERT_GT(nI2P, 5u) << "test setup didn't add enough I2P addresses";
        ASSERT_GT(nOnion, 0u);

        // Every I2P entry, and only I2P entries, each exactly once.
        std::vector<CAddrInfo> all = addrman.SelectCandidates(NET_I2P, 50);
        EXPECT_EQ(all.size(), nI2P);
        std::set<CService> seen;
        for (const CAddrInfo& info : all) {
            EXPECT_EQ(info.GetNetwork(), NET_I2P);
            seen.insert(info);
        }
        EXPECT_EQ(seen.size(), all.size()) << "candidates must be distinct";

        // nMax caps the result without changing what qualifies.
        std::vector<CAddrInfo> limited = addrman.SelectCandidates(NET_I2P, 5);
        EXPECT_EQ(limited.size(), 5u);
        std::set<CService> seenLimited;
        for (const CAddrInfo& info : limited) {
            EXPECT_EQ(info.GetNetwork(), NET_I2P);
            seenLimited.insert(info);
        }
        EXPECT_EQ(seenLimited.size(), limited.size());

        EXPECT_TRUE(addrman.SelectCandidates(NET_I2P, 0).empty());
        EXPECT_EQ(addrman.SelectCandidates(NET_ONION, 50).size(), nOnion);
        EXPECT_TRUE(addrman.SelectCandidates(NET_IPV6, 50).empty()) << "no IPv6 entries were added";
    }

    // Terrible entries (here: not seen in longer than the addrman horizon) are the ones
    // Select() already steers away from, so they are not worth handing to a dialer either.
    TEST(TestAddrmanTests, addrman_select_candidates_excludes_terrible)
    {
        CAddrManTest addrman;
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        CAddress good = MakeI2PAddress(1, GetTime());
        CAddress stale = MakeI2PAddress(2, GetTime() - int64_t(60) * 24 * 60 * 60);
        ASSERT_TRUE(addrman.Add(good, source));
        ASSERT_TRUE(addrman.Add(stale, source));

        std::vector<CAddrInfo> candidates = addrman.SelectCandidates(NET_I2P, 50);
        ASSERT_EQ(candidates.size(), 1u);
        EXPECT_TRUE(static_cast<CService>(candidates[0]) == static_cast<CService>(good));
    }

    // Select() runs with the addrman lock held, so it must not sleep: with a real (non-null)
    // nKey it used to MilliSleep(100) every 1000 probes while looking for an occupied slot,
    // and a sparse table needs thousands of probes. A single tried entry in the 16384-slot
    // (256 buckets x 64) tried table costs roughly sixteen sleeps per call - long enough, across
    // the dialer threads looping on Select(), to starve every other addrman user for minutes.
    TEST(TestAddrmanTests, addrman_select_sparse_table_does_not_sleep_under_lock)
    {
        // A real (non-null) nKey and random probing are what the old sleep depended on; put the
        // process-wide insecure RNG back to its fixed initial state afterwards for other tests.
        seed_insecure_rand(false);
        struct RngRestorer {
            ~RngRestorer() { seed_insecure_rand(true); }
        } rngRestorer;
        CAddrManTest addrman(/* makeDeterministic */ false);

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);
        // A handful of tried entries among thousands of slots, like the node this was found on
        // (3 tried entries): each Select() has to probe for an occupied slot.
        for (int i = 1; i <= 3; i++) {
            CNetAddr ipv4;
            LookupHost(strprintf("250.%d.1.1", i).c_str(), ipv4, false);
            CAddress addr(CService(ipv4, 8233), NODE_NONE);
            addr.nTime = GetTime();
            ASSERT_TRUE(addrman.Add(addr, source));
            addrman.Good(addr); // move it to the tried table
        }

        const int64_t nStart = GetTimeMillis();
        int nCalls = 0;
        for (; nCalls < 20; nCalls++) {
            CAddrInfo selected = addrman.Select();
            ASSERT_TRUE(selected.IsValid()) << "iteration " << nCalls;
            if (GetTimeMillis() - nStart > 1500)
                break; // already failing; don't spend minutes proving it
        }
        EXPECT_LT(GetTimeMillis() - nStart, 1500)
            << "Select() on a sparse table is sleeping while holding the addrman lock ("
            << nCalls + 1 << " calls)";
    }

    // Like Select(), which picks the tried table (peers we have connected to before) or the new
    // table with equal probability, candidates are drawn from the two tables with equal
    // probability - known-good peers are not diluted by whatever has merely been announced to us,
    // but they also don't monopolize the front of the list (every dialer, and every I2P pool
    // identity, would otherwise start from the same few tried peers).
    TEST(TestAddrmanTests, addrman_select_candidates_draws_tried_and_new_evenly)
    {
        CAddrManTest addrman;
        addrman.MakeDeterministic();

        CNetAddr source;
        LookupHost("252.2.2.2", source, false);

        std::vector<CAddress> vAdded;
        for (int i = 1; i <= 12; i++) {
            CAddress addr = MakeI2PAddress(i, GetTime());
            if (addrman.Add(addr, source))
                vAdded.push_back(addr);
        }
        ASSERT_GE(vAdded.size(), 8u);
        for (size_t i = 0; i < 4; i++)
            addrman.Good(vAdded[i]); // promote to the tried table

        int nFirstTried = 0;
        const int kRuns = 200;
        for (int run = 0; run < kRuns; run++) {
            // nMax == 1 exposes which table the first draw came from.
            std::vector<CAddrInfo> first = addrman.SelectCandidates(NET_I2P, 1);
            ASSERT_EQ(first.size(), 1u);
            nFirstTried += CAddrManTest::IsInTried(first[0]) ? 1 : 0;

            // Asking for everything returns every entry exactly once, whichever table it is in.
            std::vector<CAddrInfo> all = addrman.SelectCandidates(NET_I2P, 50);
            ASSERT_EQ(all.size(), vAdded.size());
            std::set<CService> seen;
            for (const CAddrInfo& info : all)
                seen.insert(info);
            ASSERT_EQ(seen.size(), all.size());
        }
        // 50/50 per draw: both tables must lead a substantial share of the time.
        EXPECT_GT(nFirstTried, kRuns / 4) << "tried entries are almost never drawn first";
        EXPECT_LT(nFirstTried, kRuns * 3 / 4) << "tried entries lead almost every list";
    }

    // SelectCandidates() runs with the addrman lock held on a table remote peers can fill with
    // entries, and every RandomInt() call reads the system RNG - so the number of draws must be
    // bounded by nMax, not by the size of the table (a full shuffle of every matching entry made it
    // grow with the table).
    TEST(TestAddrmanTests, addrman_select_candidates_random_draws_bounded_by_nmax)
    {
        CAddrManTest addrman;
        addrman.MakeDeterministic();

        // Vary the source group: entries are bucketed by (address group, source group), and one
        // bucket holds only 64 of them.
        size_t nAdded = 0;
        for (int i = 1; i <= 600; i++) {
            CNetAddr entrySource;
            LookupHost(strprintf("250.%d.1.1", i % 100 + 1).c_str(), entrySource, false);
            if (addrman.Add(MakeI2PAddress(i, GetTime()), entrySource))
                nAdded++;
        }
        ASSERT_GT(nAdded, 300u) << "test setup didn't add enough I2P addresses";

        const size_t nMax = 5;
        addrman.nRandomCalls = 0;
        std::vector<CAddrInfo> candidates = addrman.SelectCandidates(NET_I2P, nMax);
        ASSERT_EQ(candidates.size(), nMax);
        // At most nMax swaps per list plus one table choice per candidate.
        EXPECT_LE(addrman.nRandomCalls, static_cast<int>(3 * nMax))
            << "random draws grew with the table size (" << nAdded << " entries)";
    }

    // ---- addrman use while holding cs_main -------------------------------------------
    //
    // AddressCurrentlyConnected() (addrman.Add() + addrman.Connected()) is called when a
    // peer completes its handshake (VERACK) and when it is disconnected (FinalizeNode).
    // Both used to happen with cs_main held, so whenever the network threads kept the address
    // manager lock busy (looping on Select()) the caller sat on cs_main until it got the lock -
    // up to two minutes on a live node - and every RPC and block-processing thread queued
    // behind it. These tests hold the global addrman lock on a helper thread, drive the real
    // code path on another, and require that cs_main is not held while it waits.

    class AddrmanNodeLockTests : public BitcoinTestingSetup {};

    // Holds the global addrman's lock on a helper thread until Release() or destruction.
    class GlobalAddrmanLockHolder
    {
    public:
        GlobalAddrmanLockHolder()
        {
            m_release_future = m_release.get_future().share();
            m_thread = std::thread([this]() {
                LOCK(CAddrManTest::GetLock(addrman));
                m_locked.set_value();
                m_release_future.wait();
            });
            m_locked.get_future().wait();
        }
        void Release()
        {
            if (!m_released) {
                m_released = true;
                m_release.set_value();
            }
        }
        ~GlobalAddrmanLockHolder()
        {
            Release();
            m_thread.join();
        }
    private:
        std::promise<void> m_locked;
        std::promise<void> m_release;
        std::shared_future<void> m_release_future;
        std::thread m_thread;
        bool m_released = false;
    };

    // A fresh routable test address on every call, each in its own /16. The lock tests add their
    // node's address to the process-wide addrman (which the fixture never clears) and check that
    // it was added. A fixed address would already be present when the test runs again in the same
    // process (--gtest_repeat), and merely-distinct addresses in one /16 would all compete for the
    // same 64-slot new-table bucket - addrman buckets by (address group, source group), and the
    // source here is the peer's own address - so once it filled up Add() would silently refuse
    // further ones. Distinct groups land in different buckets. (Good for 250 calls per process.)
    static std::string UniqueTestAddress()
    {
        static std::atomic<int> nCounter(0);
        const int n = nCounter++;
        return strprintf("250.%d.77.1", n % 250 + 1);
    }

    static CDataStream MakeVersionPayload()
    {
        CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
        const uint64_t nServices = NODE_NETWORK;
        const int64_t nTime = GetTime();
        const CAddress addrMe, addrFrom;
        const uint64_t nNonce = 0x1234567; // != nLocalHostNonce, or the handler treats it as a self-connection
        const std::string strSubVer = "/gtest:0.0.0/";
        const int nStartingHeight = 0;
        const bool fRelay = true;
        payload << PROTOCOL_VERSION << nServices << nTime << addrMe << addrFrom << nNonce
                << strSubVer << nStartingHeight << fRelay;
        return payload;
    }

    // True if another thread can take cs_main within timeoutMs. The caller is blocked on the
    // addrman lock at that point, so any failure to get cs_main means the caller holds it.
    static bool CsMainAcquirableWithin(int timeoutMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            {
                TRY_LOCK(cs_main, lock);
                if (lock)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    // Wait until `flag` is set, then give the thread that set it time to run into the lock
    // it is expected to block on.
    static void WaitForThreadToBlock(const std::atomic<bool>& flag)
    {
        for (int i = 0; i < 500 && !flag; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Completes VERSION for a fresh inbound test node. For an inbound peer whose addrFrom
    // differs from its own address the VERSION handler does not touch addrman (the outbound
    // path's addrman.Good() calls, which would block on the held lock too early, are skipped),
    // so this can run with or without the addrman lock held.
    static void SendVersion(CNode* node)
    {
        CDataStream payload = MakeVersionPayload();
        InjectMessage(node, NetMsgType::VERSION, payload);
    }

    static void SendVerack(CNode* node)
    {
        CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
        InjectMessage(node, NetMsgType::VERACK, payload);
    }

    TEST_F(AddrmanNodeLockTests, verack_handler_releases_cs_main_before_updating_addrman)
    {
        CService service;
        ASSERT_TRUE(Lookup(UniqueTestAddress().c_str(), service, 8233, false));
        CAddress addr(service, NODE_NETWORK);
        CNode node(INVALID_SOCKET, addr, "", true);
        node.fNetworkNode = true;
        const size_t nBefore = addrman.size();

        SendVersion(&node);
        ASSERT_NE(node.nVersion, 0) << "version message was not accepted";
        ASSERT_EQ(addrman.size(), nBefore) << "the VERSION handler unexpectedly updated addrman";

        {
            GlobalAddrmanLockHolder holder;
            std::atomic<bool> started(false);
            std::thread injector([&]() {
                started = true;
                SendVerack(&node);
            });
            WaitForThreadToBlock(started);

            EXPECT_TRUE(CsMainAcquirableWithin(1000))
                << "the VERACK handler is holding cs_main while it waits for the addrman lock";

            holder.Release();
            injector.join();
        }

        // Guards against a vacuous pass: the blocked call must actually have been the addrman update.
        EXPECT_GT(addrman.size(), nBefore);
    }

    TEST_F(AddrmanNodeLockTests, finalize_node_releases_cs_main_before_updating_addrman)
    {
        CService service;
        ASSERT_TRUE(Lookup(UniqueTestAddress().c_str(), service, 8233, false));
        CAddress addr(service, NODE_NETWORK);
        std::unique_ptr<CNode> node(new CNode(INVALID_SOCKET, addr, "", true));
        node->fNetworkNode = true;
        const size_t nBefore = addrman.size();

        // Complete the handshake first (nothing is blocking addrman yet) so that disconnecting
        // the node takes the "connected, not misbehaving" path that updates addrman.
        SendVersion(node.get());
        SendVerack(node.get());
        ASSERT_GT(addrman.size(), nBefore) << "handshake did not mark the node as connected";
        ASSERT_EQ(GetMisbehavior(node->GetId()), 0);

        {
            GlobalAddrmanLockHolder holder;
            std::atomic<bool> started(false);
            // ~CNode() calls FinalizeNode() via the node signals.
            std::thread destroyer([&]() {
                started = true;
                node.reset();
            });
            WaitForThreadToBlock(started);

            EXPECT_TRUE(CsMainAcquirableWithin(1000))
                << "FinalizeNode is holding cs_main while it waits for the addrman lock";

            holder.Release();
            destroyer.join();
        }
    }

    // GetAllPeers() iterated mapInfo without taking the addrman lock, racing with every thread
    // that adds, promotes or removes entries.
    TEST_F(AddrmanNodeLockTests, get_all_peers_takes_the_addrman_lock)
    {
        std::atomic<bool> started(false), finished(false);
        std::thread reader;
        {
            GlobalAddrmanLockHolder holder;
            reader = std::thread([&]() {
                started = true;
                std::map<std::string, int64_t> info;
                addrman.GetAllPeers(info);
                finished = true;
            });
            WaitForThreadToBlock(started);
            EXPECT_FALSE(finished) << "GetAllPeers() completed while another thread held the addrman lock";
            holder.Release();
        }
        reader.join();
        EXPECT_TRUE(finished);
    }

    // With GetAllPeers() locking addrman, getpeerlist must not hold cs_main while it waits.
    TEST_F(AddrmanNodeLockTests, getpeerlist_does_not_hold_cs_main_while_waiting_for_addrman)
    {
        std::atomic<bool> started(false), finished(false);
        std::thread rpc;
        {
            GlobalAddrmanLockHolder holder;
            rpc = std::thread([&]() {
                started = true;
                UniValue result = getpeerlist(UniValue(UniValue::VARR), false, CPubKey());
                EXPECT_TRUE(result.isArray());
                finished = true;
            });
            WaitForThreadToBlock(started);

            EXPECT_FALSE(finished) << "getpeerlist finished without waiting for the addrman lock";
            EXPECT_TRUE(CsMainAcquirableWithin(1000))
                << "getpeerlist is holding cs_main while it waits for the addrman lock";
            holder.Release();
        }
        rpc.join();
        EXPECT_TRUE(finished);
    }

}
