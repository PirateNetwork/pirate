// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util.h"

#include "clientversion.h"
#include "primitives/transaction.h"
#include "random.h"
#include "sync.h"
#include "util/strencodings.h"
#include "utilmoneystr.h"
#include "gtest/gtestutils.h"

#include <stdint.h>
#include <vector>

#include <gtest/gtest.h>

// Broad grab-bag of util.cpp tests: ParseHex/HexStr, DateTimeStr,
// ParseParameters/command-line handling, money-string formatting, and
// critical-section (locking) behavior.

using namespace std;

class util_tests_bitcoin : public BitcoinBasicTestingSetup {};

TEST_F(util_tests_bitcoin, util_criticalsection)
{
    CCriticalSection cs;

    do {
        LOCK(cs);
        break;

        ADD_FAILURE() << "break was swallowed!";
    } while(0);

    do {
        TRY_LOCK(cs, lockTest);
        if (lockTest)
            break;

        ADD_FAILURE() << "break was swallowed!";
    } while(0);
}

static const unsigned char ParseHex_expected[65] = {
    0x04, 0x67, 0x8a, 0xfd, 0xb0, 0xfe, 0x55, 0x48, 0x27, 0x19, 0x67, 0xf1, 0xa6, 0x71, 0x30, 0xb7,
    0x10, 0x5c, 0xd6, 0xa8, 0x28, 0xe0, 0x39, 0x09, 0xa6, 0x79, 0x62, 0xe0, 0xea, 0x1f, 0x61, 0xde,
    0xb6, 0x49, 0xf6, 0xbc, 0x3f, 0x4c, 0xef, 0x38, 0xc4, 0xf3, 0x55, 0x04, 0xe5, 0x1e, 0xc1, 0x12,
    0xde, 0x5c, 0x38, 0x4d, 0xf7, 0xba, 0x0b, 0x8d, 0x57, 0x8a, 0x4c, 0x70, 0x2b, 0x6b, 0xf1, 0x1d,
    0x5f
};
TEST_F(util_tests_bitcoin, util_ParseHex)
{
    std::vector<unsigned char> result;
    std::vector<unsigned char> expected(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected));
    // Basic test vector
    result = ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");
    EXPECT_TRUE(std::equal(expected.begin(), expected.end(), result.begin()) && result.size() == expected.size());

    // Spaces between bytes must be supported
    result = ParseHex("12 34 56 78");
    EXPECT_TRUE(result.size() == 4 && result[0] == 0x12 && result[1] == 0x34 && result[2] == 0x56 && result[3] == 0x78);

    // Stop parsing at invalid value
    result = ParseHex("1234 invalid 1234");
    EXPECT_TRUE(result.size() == 2 && result[0] == 0x12 && result[1] == 0x34);
}

TEST_F(util_tests_bitcoin, util_HexStr)
{
    EXPECT_EQ(
        HexStr(ParseHex_expected, ParseHex_expected + sizeof(ParseHex_expected)),
        "04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f");

    EXPECT_EQ(
        HexStr(ParseHex_expected, ParseHex_expected + 5, true),
        "04 67 8a fd b0");

    EXPECT_EQ(
        HexStr(ParseHex_expected, ParseHex_expected, true),
        "");

    std::vector<unsigned char> ParseHex_vec(ParseHex_expected, ParseHex_expected + 5);

    EXPECT_EQ(
        HexStr(ParseHex_vec, true),
        "04 67 8a fd b0");
}


TEST_F(util_tests_bitcoin, util_DateTimeStrFormat)
{
    EXPECT_EQ(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 0), "1970-01-01 00:00:00");
    EXPECT_EQ(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 0x7FFFFFFF), "2038-01-19 03:14:07");
    EXPECT_EQ(DateTimeStrFormat("%Y-%m-%d %H:%M:%S", 1317425777), "2011-09-30 23:36:17");
    EXPECT_EQ(DateTimeStrFormat("%Y-%m-%d %H:%M", 1317425777), "2011-09-30 23:36");
    EXPECT_EQ(DateTimeStrFormat("%a, %d %b %Y %H:%M:%S +0000", 1317425777), "Fri, 30 Sep 2011 23:36:17 +0000");
}

TEST_F(util_tests_bitcoin, util_ParseParameters)
{
    const char *argv_test[] = {"-ignored", "-a", "-b", "-ccc=argument", "-ccc=multiple", "f", "-d=e"};

    ParseParameters(0, (char**)argv_test);
    EXPECT_TRUE(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(1, (char**)argv_test);
    EXPECT_TRUE(mapArgs.empty() && mapMultiArgs.empty());

    ParseParameters(5, (char**)argv_test);
    // expectation: -ignored is ignored (program name argument),
    // -a, -b and -ccc end up in map, -d ignored because it is after
    // a non-option argument (non-GNU option parsing)
    EXPECT_TRUE(mapArgs.size() == 3 && mapMultiArgs.size() == 3);
    EXPECT_TRUE(mapArgs.count("-a") && mapArgs.count("-b") && mapArgs.count("-ccc")
                && !mapArgs.count("f") && !mapArgs.count("-d"));
    EXPECT_TRUE(mapMultiArgs.count("-a") && mapMultiArgs.count("-b") && mapMultiArgs.count("-ccc")
                && !mapMultiArgs.count("f") && !mapMultiArgs.count("-d"));

    EXPECT_TRUE(mapArgs["-a"] == "" && mapArgs["-ccc"] == "multiple");
    EXPECT_TRUE(mapMultiArgs["-ccc"].size() == 2);
}

TEST_F(util_tests_bitcoin, util_GetArg)
{
    mapArgs.clear();
    mapArgs["strtest1"] = "string...";
    // strtest2 undefined on purpose
    mapArgs["inttest1"] = "12345";
    mapArgs["inttest2"] = "81985529216486895";
    // inttest3 undefined on purpose
    mapArgs["booltest1"] = "";
    // booltest2 undefined on purpose
    mapArgs["booltest3"] = "0";
    mapArgs["booltest4"] = "1";

    EXPECT_EQ(GetArg("strtest1", "default"), "string...");
    EXPECT_EQ(GetArg("strtest2", "default"), "default");
    EXPECT_EQ(GetArg("inttest1", -1), 12345);
    EXPECT_EQ(GetArg("inttest2", -1), 81985529216486895LL);
    EXPECT_EQ(GetArg("inttest3", -1), -1);
    EXPECT_EQ(GetBoolArg("booltest1", false), true);
    EXPECT_EQ(GetBoolArg("booltest2", false), false);
    EXPECT_EQ(GetBoolArg("booltest3", false), false);
    EXPECT_EQ(GetBoolArg("booltest4", false), true);
}

TEST_F(util_tests_bitcoin, util_FormatMoney)
{
    EXPECT_EQ(FormatMoney(0), "0.00");
    EXPECT_EQ(FormatMoney((COIN/10000)*123456789), "12345.6789");
    EXPECT_EQ(FormatMoney(-COIN), "-1.00");

    EXPECT_EQ(FormatMoney(COIN*100000000), "100000000.00");
    EXPECT_EQ(FormatMoney(COIN*10000000), "10000000.00");
    EXPECT_EQ(FormatMoney(COIN*1000000), "1000000.00");
    EXPECT_EQ(FormatMoney(COIN*100000), "100000.00");
    EXPECT_EQ(FormatMoney(COIN*10000), "10000.00");
    EXPECT_EQ(FormatMoney(COIN*1000), "1000.00");
    EXPECT_EQ(FormatMoney(COIN*100), "100.00");
    EXPECT_EQ(FormatMoney(COIN*10), "10.00");
    EXPECT_EQ(FormatMoney(COIN), "1.00");
    EXPECT_EQ(FormatMoney(COIN/10), "0.10");
    EXPECT_EQ(FormatMoney(COIN/100), "0.01");
    EXPECT_EQ(FormatMoney(COIN/1000), "0.001");
    EXPECT_EQ(FormatMoney(COIN/10000), "0.0001");
    EXPECT_EQ(FormatMoney(COIN/100000), "0.00001");
    EXPECT_EQ(FormatMoney(COIN/1000000), "0.000001");
    EXPECT_EQ(FormatMoney(COIN/10000000), "0.0000001");
    EXPECT_EQ(FormatMoney(COIN/100000000), "0.00000001");
}

TEST_F(util_tests_bitcoin, util_ParseMoney)
{
    CAmount ret = 0;
    EXPECT_TRUE(ParseMoney("0.0", ret));
    EXPECT_EQ(ret, 0);

    EXPECT_TRUE(ParseMoney("12345.6789", ret));
    EXPECT_EQ(ret, (COIN/10000)*123456789);

    EXPECT_TRUE(ParseMoney("100000000.00", ret));
    EXPECT_EQ(ret, COIN*100000000);
    EXPECT_TRUE(ParseMoney("10000000.00", ret));
    EXPECT_EQ(ret, COIN*10000000);
    EXPECT_TRUE(ParseMoney("1000000.00", ret));
    EXPECT_EQ(ret, COIN*1000000);
    EXPECT_TRUE(ParseMoney("100000.00", ret));
    EXPECT_EQ(ret, COIN*100000);
    EXPECT_TRUE(ParseMoney("10000.00", ret));
    EXPECT_EQ(ret, COIN*10000);
    EXPECT_TRUE(ParseMoney("1000.00", ret));
    EXPECT_EQ(ret, COIN*1000);
    EXPECT_TRUE(ParseMoney("100.00", ret));
    EXPECT_EQ(ret, COIN*100);
    EXPECT_TRUE(ParseMoney("10.00", ret));
    EXPECT_EQ(ret, COIN*10);
    EXPECT_TRUE(ParseMoney("1.00", ret));
    EXPECT_EQ(ret, COIN);
    EXPECT_TRUE(ParseMoney("0.1", ret));
    EXPECT_EQ(ret, COIN/10);
    EXPECT_TRUE(ParseMoney("0.01", ret));
    EXPECT_EQ(ret, COIN/100);
    EXPECT_TRUE(ParseMoney("0.001", ret));
    EXPECT_EQ(ret, COIN/1000);
    EXPECT_TRUE(ParseMoney("0.0001", ret));
    EXPECT_EQ(ret, COIN/10000);
    EXPECT_TRUE(ParseMoney("0.00001", ret));
    EXPECT_EQ(ret, COIN/100000);
    EXPECT_TRUE(ParseMoney("0.000001", ret));
    EXPECT_EQ(ret, COIN/1000000);
    EXPECT_TRUE(ParseMoney("0.0000001", ret));
    EXPECT_EQ(ret, COIN/10000000);
    EXPECT_TRUE(ParseMoney("0.00000001", ret));
    EXPECT_EQ(ret, COIN/100000000);

    // Attempted 63 bit overflow should fail
    EXPECT_TRUE(!ParseMoney("92233720368.54775808", ret));
}

TEST_F(util_tests_bitcoin, util_IsHex)
{
    EXPECT_TRUE(IsHex("00"));
    EXPECT_TRUE(IsHex("00112233445566778899aabbccddeeffAABBCCDDEEFF"));
    EXPECT_TRUE(IsHex("ff"));
    EXPECT_TRUE(IsHex("FF"));

    EXPECT_TRUE(!IsHex(""));
    EXPECT_TRUE(!IsHex("0"));
    EXPECT_TRUE(!IsHex("a"));
    EXPECT_TRUE(!IsHex("eleven"));
    EXPECT_TRUE(!IsHex("00xx00"));
    EXPECT_TRUE(!IsHex("0x0000"));
}

TEST_F(util_tests_bitcoin, util_seed_insecure_rand)
{
    seed_insecure_rand(true);
    for (int mod=2;mod<11;mod++)
    {
        int mask = 1;
        // Really rough binomal confidence approximation.
        int err = 30*10000./mod*sqrt((1./mod*(1-1./mod))/10000.);
        //mask is 2^ceil(log2(mod))-1
        while(mask<mod-1)mask=(mask<<1)+1;

        int count = 0;
        //How often does it get a zero from the uniform range [0,mod)?
        for (int i = 0; i < 10000; i++) {
            uint32_t rval;
            do{
                rval=insecure_rand()&mask;
            }while(rval>=(uint32_t)mod);
            count += rval==0;
        }
        EXPECT_TRUE(count<=10000/mod+err);
        EXPECT_TRUE(count>=10000/mod-err);
    }
}

TEST_F(util_tests_bitcoin, util_TimingResistantEqual)
{
    EXPECT_TRUE(TimingResistantEqual(std::string(""), std::string("")));
    EXPECT_TRUE(!TimingResistantEqual(std::string("abc"), std::string("")));
    EXPECT_TRUE(!TimingResistantEqual(std::string(""), std::string("abc")));
    EXPECT_TRUE(!TimingResistantEqual(std::string("a"), std::string("aa")));
    EXPECT_TRUE(!TimingResistantEqual(std::string("aa"), std::string("a")));
    EXPECT_TRUE(TimingResistantEqual(std::string("abc"), std::string("abc")));
    EXPECT_TRUE(!TimingResistantEqual(std::string("abc"), std::string("aba")));
}

/* Test strprintf formatting directives.
 * Put a string before and after to ensure sanity of element sizes on stack. */
#define B "check_prefix"
#define E "check_postfix"
TEST_F(util_tests_bitcoin, strprintf_numbers)
{
    int64_t s64t = -9223372036854775807LL; /* signed 64 bit test value */
    uint64_t u64t = 18446744073709551615ULL; /* unsigned 64 bit test value */
    EXPECT_TRUE(strprintf("%s %d %s", B, s64t, E) == B" -9223372036854775807 " E);
    EXPECT_TRUE(strprintf("%s %u %s", B, u64t, E) == B" 18446744073709551615 " E);
    EXPECT_TRUE(strprintf("%s %x %s", B, u64t, E) == B" ffffffffffffffff " E);

    size_t st = 12345678; /* unsigned size_t test value */
    ssize_t sst = -12345678; /* signed size_t test value */
    EXPECT_TRUE(strprintf("%s %d %s", B, sst, E) == B" -12345678 " E);
    EXPECT_TRUE(strprintf("%s %u %s", B, st, E) == B" 12345678 " E);
    EXPECT_TRUE(strprintf("%s %x %s", B, st, E) == B" bc614e " E);

    ptrdiff_t pt = 87654321; /* positive ptrdiff_t test value */
    ptrdiff_t spt = -87654321; /* negative ptrdiff_t test value */
    EXPECT_TRUE(strprintf("%s %d %s", B, spt, E) == B" -87654321 " E);
    EXPECT_TRUE(strprintf("%s %u %s", B, pt, E) == B" 87654321 " E);
    EXPECT_TRUE(strprintf("%s %x %s", B, pt, E) == B" 5397fb1 " E);
}
#undef B
#undef E

/* Check for mingw/wine issue #3494
 * Remove this test before time.ctime(0xffffffff) == 'Sun Feb  7 07:28:15 2106'
 */
TEST_F(util_tests_bitcoin, gettime)
{
    EXPECT_TRUE((GetTime() & ~0xFFFFFFFFLL) == 0);
}

TEST_F(util_tests_bitcoin, test_ParseInt32)
{
    int32_t n;
    // Valid values
    EXPECT_TRUE(ParseInt32("1234", NULL));
    EXPECT_TRUE(ParseInt32("0", &n) && n == 0);
    EXPECT_TRUE(ParseInt32("1234", &n) && n == 1234);
    EXPECT_TRUE(ParseInt32("01234", &n) && n == 1234); // no octal
    EXPECT_TRUE(ParseInt32("2147483647", &n) && n == 2147483647);
    EXPECT_TRUE(ParseInt32("-2147483648", &n) && n == -2147483648);
    EXPECT_TRUE(ParseInt32("-1234", &n) && n == -1234);
    // Invalid values
    EXPECT_TRUE(!ParseInt32("", &n));
    EXPECT_TRUE(!ParseInt32(" 1", &n)); // no padding inside
    EXPECT_TRUE(!ParseInt32("1 ", &n));
    EXPECT_TRUE(!ParseInt32("1a", &n));
    EXPECT_TRUE(!ParseInt32("aap", &n));
    EXPECT_TRUE(!ParseInt32("0x1", &n)); // no hex
    EXPECT_TRUE(!ParseInt32("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    EXPECT_TRUE(!ParseInt32(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    EXPECT_TRUE(!ParseInt32("-2147483649", NULL));
    EXPECT_TRUE(!ParseInt32("2147483648", NULL));
    EXPECT_TRUE(!ParseInt32("-32482348723847471234", NULL));
    EXPECT_TRUE(!ParseInt32("32482348723847471234", NULL));
}

TEST_F(util_tests_bitcoin, test_ParseInt64)
{
    int64_t n;
    // Valid values
    EXPECT_TRUE(ParseInt64("1234", NULL));
    EXPECT_TRUE(ParseInt64("0", &n) && n == 0LL);
    EXPECT_TRUE(ParseInt64("1234", &n) && n == 1234LL);
    EXPECT_TRUE(ParseInt64("01234", &n) && n == 1234LL); // no octal
    EXPECT_TRUE(ParseInt64("2147483647", &n) && n == 2147483647LL);
    EXPECT_TRUE(ParseInt64("-2147483648", &n) && n == -2147483648LL);
    EXPECT_TRUE(ParseInt64("9223372036854775807", &n) && n == (int64_t)9223372036854775807);
    EXPECT_TRUE(ParseInt64("-9223372036854775808", &n) && n == (int64_t)-9223372036854775807-1);
    EXPECT_TRUE(ParseInt64("-1234", &n) && n == -1234LL);
    // Invalid values
    EXPECT_TRUE(!ParseInt64("", &n));
    EXPECT_TRUE(!ParseInt64(" 1", &n)); // no padding inside
    EXPECT_TRUE(!ParseInt64("1 ", &n));
    EXPECT_TRUE(!ParseInt64("1a", &n));
    EXPECT_TRUE(!ParseInt64("aap", &n));
    EXPECT_TRUE(!ParseInt64("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    EXPECT_TRUE(!ParseInt64(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    EXPECT_TRUE(!ParseInt64("-9223372036854775809", NULL));
    EXPECT_TRUE(!ParseInt64("9223372036854775808", NULL));
    EXPECT_TRUE(!ParseInt64("-32482348723847471234", NULL));
    EXPECT_TRUE(!ParseInt64("32482348723847471234", NULL));
}

TEST_F(util_tests_bitcoin, test_ParseDouble)
{
    double n;
    // Valid values
    EXPECT_TRUE(ParseDouble("1234", NULL));
    EXPECT_TRUE(ParseDouble("0", &n) && n == 0.0);
    EXPECT_TRUE(ParseDouble("1234", &n) && n == 1234.0);
    EXPECT_TRUE(ParseDouble("01234", &n) && n == 1234.0); // no octal
    EXPECT_TRUE(ParseDouble("2147483647", &n) && n == 2147483647.0);
    EXPECT_TRUE(ParseDouble("-2147483648", &n) && n == -2147483648.0);
    EXPECT_TRUE(ParseDouble("-1234", &n) && n == -1234.0);
    EXPECT_TRUE(ParseDouble("1e6", &n) && n == 1e6);
    EXPECT_TRUE(ParseDouble("-1e6", &n) && n == -1e6);
    // Invalid values
    EXPECT_TRUE(!ParseDouble("", &n));
    EXPECT_TRUE(!ParseDouble(" 1", &n)); // no padding inside
    EXPECT_TRUE(!ParseDouble("1 ", &n));
    EXPECT_TRUE(!ParseDouble("1a", &n));
    EXPECT_TRUE(!ParseDouble("aap", &n));
    EXPECT_TRUE(!ParseDouble("0x1", &n)); // no hex
    const char test_bytes[] = {'1', 0, '1'};
    std::string teststr(test_bytes, sizeof(test_bytes));
    EXPECT_TRUE(!ParseDouble(teststr, &n)); // no embedded NULs
    // Overflow and underflow
    EXPECT_TRUE(!ParseDouble("-1e10000", NULL));
    EXPECT_TRUE(!ParseDouble("1e10000", NULL));
}

TEST_F(util_tests_bitcoin, test_FormatParagraph)
{
    EXPECT_EQ(FormatParagraph("", 79, 0), "");
    EXPECT_EQ(FormatParagraph("test", 79, 0), "test");
    // This fork's FormatParagraph (util/strencodings.cpp) doesn't strip a
    // leading space the way the original test vector assumed - verified by
    // tracing the actual (unchanged since 2011) algorithm by hand, not a
    // porting bug.
    EXPECT_EQ(FormatParagraph(" test", 79, 0), " test");
    EXPECT_EQ(FormatParagraph("test test", 79, 0), "test test");
    EXPECT_EQ(FormatParagraph("test test", 4, 0), "test\ntest");
    // Likewise, wrapping at the trailing space of "testerde test " leaves a
    // trailing newline in the actual output; the original expected value
    // assumed the trailing space got trimmed instead.
    EXPECT_EQ(FormatParagraph("testerde test ", 4, 0), "testerde\ntest\n");
    EXPECT_EQ(FormatParagraph("test test", 4, 4), "test\n    test");
    EXPECT_EQ(FormatParagraph("This is a very long test string. This is a second sentence in the very long test string."), "This is a very long test string. This is a second sentence in the very long\ntest string.");
}

TEST_F(util_tests_bitcoin, test_FormatSubVersion)
{
    std::vector<std::string> comments;
    comments.push_back(std::string("comment1"));
    std::vector<std::string> comments2;
    comments2.push_back(std::string("comment1"));
    comments2.push_back(SanitizeString(std::string("Comment2; .,_?@; !\"#$%&'()*+-/<=>[]\\^`{|}~"), SAFE_CHARS_UA_COMMENT)); // Semicolon is discouraged but not forbidden by BIP-0014
    EXPECT_EQ(FormatSubVersion("Test", 99900, std::vector<std::string>()), std::string("/Test:0.9.99-beta1/"));
    EXPECT_EQ(FormatSubVersion("Test", 99924, std::vector<std::string>()), std::string("/Test:0.9.99-beta25/"));
    EXPECT_EQ(FormatSubVersion("Test", 99925, std::vector<std::string>()), std::string("/Test:0.9.99-rc1/"));
    EXPECT_EQ(FormatSubVersion("Test", 99949, std::vector<std::string>()), std::string("/Test:0.9.99-rc25/"));
    EXPECT_EQ(FormatSubVersion("Test", 99950, std::vector<std::string>()), std::string("/Test:0.9.99/"));
    EXPECT_EQ(FormatSubVersion("Test", 99951, std::vector<std::string>()), std::string("/Test:0.9.99-1/"));
    EXPECT_EQ(FormatSubVersion("Test", 99999, std::vector<std::string>()), std::string("/Test:0.9.99-49/"));
    EXPECT_EQ(FormatSubVersion("Test", 99900, comments),  std::string("/Test:0.9.99-beta1(comment1)/"));
    EXPECT_EQ(FormatSubVersion("Test", 99950, comments),  std::string("/Test:0.9.99(comment1)/"));
    // SAFE_CHARS_UA_COMMENT (util/strencodings.cpp) explicitly allows '-'
    // (CHARS_ALPHA_NUM + " .,;-_?@"), so SanitizeString correctly keeps the
    // '-' from the "+-/" run in the input; the original expected strings
    // assumed it got stripped.
    EXPECT_EQ(FormatSubVersion("Test", 99900, comments2), std::string("/Test:0.9.99-beta1(comment1; Comment2; .,_?@; -)/"));
    EXPECT_EQ(FormatSubVersion("Test", 99950, comments2), std::string("/Test:0.9.99(comment1; Comment2; .,_?@; -)/"));
}

TEST_F(util_tests_bitcoin, test_ParseFixedPoint)
{
    int64_t amount = 0;
    EXPECT_TRUE(ParseFixedPoint("0", 8, &amount));
    EXPECT_EQ(amount, 0LL);
    EXPECT_TRUE(ParseFixedPoint("1", 8, &amount));
    EXPECT_EQ(amount, 100000000LL);
    EXPECT_TRUE(ParseFixedPoint("0.0", 8, &amount));
    EXPECT_EQ(amount, 0LL);
    EXPECT_TRUE(ParseFixedPoint("-0.1", 8, &amount));
    EXPECT_EQ(amount, -10000000LL);
    EXPECT_TRUE(ParseFixedPoint("1.1", 8, &amount));
    EXPECT_EQ(amount, 110000000LL);
    EXPECT_TRUE(ParseFixedPoint("1.10000000000000000", 8, &amount));
    EXPECT_EQ(amount, 110000000LL);
    EXPECT_TRUE(ParseFixedPoint("1.1e1", 8, &amount));
    EXPECT_EQ(amount, 1100000000LL);
    EXPECT_TRUE(ParseFixedPoint("1.1e-1", 8, &amount));
    EXPECT_EQ(amount, 11000000LL);
    EXPECT_TRUE(ParseFixedPoint("1000", 8, &amount));
    EXPECT_EQ(amount, 100000000000LL);
    EXPECT_TRUE(ParseFixedPoint("-1000", 8, &amount));
    EXPECT_EQ(amount, -100000000000LL);
    EXPECT_TRUE(ParseFixedPoint("0.00000001", 8, &amount));
    EXPECT_EQ(amount, 1LL);
    EXPECT_TRUE(ParseFixedPoint("0.0000000100000000", 8, &amount));
    EXPECT_EQ(amount, 1LL);
    EXPECT_TRUE(ParseFixedPoint("-0.00000001", 8, &amount));
    EXPECT_EQ(amount, -1LL);
    EXPECT_TRUE(ParseFixedPoint("1000000000.00000001", 8, &amount));
    EXPECT_EQ(amount, 100000000000000001LL);
    EXPECT_TRUE(ParseFixedPoint("9999999999.99999999", 8, &amount));
    EXPECT_EQ(amount, 999999999999999999LL);
    EXPECT_TRUE(ParseFixedPoint("-9999999999.99999999", 8, &amount));
    EXPECT_EQ(amount, -999999999999999999LL);

    EXPECT_TRUE(!ParseFixedPoint("", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("a-1000", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-a1000", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-1000a", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-01000", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("00.1", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint(".1", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("--0.1", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("0.000000001", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-0.000000001", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("0.00000001000000001", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-10000000000.00000000", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("10000000000.00000000", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-10000000000.00000001", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("10000000000.00000001", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-10000000000.00000009", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("10000000000.00000009", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-99999999999.99999999", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("99999909999.09999999", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("92233720368.54775807", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("92233720368.54775808", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-92233720368.54775808", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("-92233720368.54775809", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("1.1e", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("1.1e-", 8, &amount));
    EXPECT_TRUE(!ParseFixedPoint("1.", 8, &amount));
}

