// Copyright (c) 2012-2014 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/bignum.h"
#include "script/script.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>
#include <limits.h>
#include <stdint.h>

// Tests CScriptNum construction, serialization, and arithmetic operators
// against a CBigNum reference implementation, including overflow/edge-case
// bounds. Covers the TRANSPARENT pool specifically (script-level numeric
// encoding), not Sapling/Ironwood/Sprout shielded logic.

class scriptnum_tests : public BitcoinBasicTestingSetup {};

static const int64_t values[] = \
{ 0, 1, CHAR_MIN, CHAR_MAX, UCHAR_MAX, SHRT_MIN, USHRT_MAX, INT_MIN, INT_MAX, UINT_MAX, LONG_MIN, LONG_MAX };
static const int64_t offsets[] = { 1, 0x79, 0x80, 0x81, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000};

static bool verify(const CBigNum& bignum, const CScriptNum& scriptnum)
{
    return bignum.getvch() == scriptnum.getvch() && bignum.getint() == scriptnum.getint();
}

static void CheckCreateVch(const int64_t& num)
{
    CBigNum bignum(num);
    CScriptNum scriptnum(num);
    EXPECT_TRUE(verify(bignum, scriptnum));

    CBigNum bignum2(bignum.getvch());
    CScriptNum scriptnum2(scriptnum.getvch(), false);
    EXPECT_TRUE(verify(bignum2, scriptnum2));

    CBigNum bignum3(scriptnum2.getvch());
    CScriptNum scriptnum3(bignum2.getvch(), false);
    EXPECT_TRUE(verify(bignum3, scriptnum3));
}

static void CheckCreateInt(const int64_t& num)
{
    CBigNum bignum(num);
    CScriptNum scriptnum(num);
    EXPECT_TRUE(verify(bignum, scriptnum));
    EXPECT_TRUE(verify(bignum.getint(), CScriptNum(scriptnum.getint())));
    EXPECT_TRUE(verify(scriptnum.getint(), CScriptNum(bignum.getint())));
    EXPECT_TRUE(verify(CBigNum(scriptnum.getint()).getint(), CScriptNum(CScriptNum(bignum.getint()).getint())));
}


static void CheckAdd(const int64_t& num1, const int64_t& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    CBigNum bignum3(num1);
    CBigNum bignum4(num1);
    CScriptNum scriptnum3(num1);
    CScriptNum scriptnum4(num1);

    // int64_t overflow is undefined.
    bool invalid = (((num2 > 0) && (num1 > (std::numeric_limits<int64_t>::max() - num2))) ||
                    ((num2 < 0) && (num1 < (std::numeric_limits<int64_t>::min() - num2))));
    if (!invalid)
    {
        EXPECT_TRUE(verify(bignum1 + bignum2, scriptnum1 + scriptnum2));
        EXPECT_TRUE(verify(bignum1 + bignum2, scriptnum1 + num2));
        EXPECT_TRUE(verify(bignum1 + bignum2, scriptnum2 + num1));
    }
}

static void CheckNegate(const int64_t& num)
{
    const CBigNum bignum(num);
    const CScriptNum scriptnum(num);

    // -INT64_MIN is undefined
    if (num != std::numeric_limits<int64_t>::min())
        EXPECT_TRUE(verify(-bignum, -scriptnum));
}

static void CheckSubtract(const int64_t& num1, const int64_t& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);
    bool invalid = false;

    // int64_t overflow is undefined.
    invalid = ((num2 > 0 && num1 < std::numeric_limits<int64_t>::min() + num2) ||
               (num2 < 0 && num1 > std::numeric_limits<int64_t>::max() + num2));
    if (!invalid)
    {
        EXPECT_TRUE(verify(bignum1 - bignum2, scriptnum1 - scriptnum2));
        EXPECT_TRUE(verify(bignum1 - bignum2, scriptnum1 - num2));
    }

    invalid = ((num1 > 0 && num2 < std::numeric_limits<int64_t>::min() + num1) ||
               (num1 < 0 && num2 > std::numeric_limits<int64_t>::max() + num1));
    if (!invalid)
    {
        EXPECT_TRUE(verify(bignum2 - bignum1, scriptnum2 - scriptnum1));
        EXPECT_TRUE(verify(bignum2 - bignum1, scriptnum2 - num1));
    }
}

static void CheckCompare(const int64_t& num1, const int64_t& num2)
{
    const CBigNum bignum1(num1);
    const CBigNum bignum2(num2);
    const CScriptNum scriptnum1(num1);
    const CScriptNum scriptnum2(num2);

    EXPECT_EQ((bignum1 == bignum1), (scriptnum1 == scriptnum1));
    EXPECT_EQ((bignum1 != bignum1),  (scriptnum1 != scriptnum1));
    EXPECT_EQ((bignum1 < bignum1),  (scriptnum1 < scriptnum1));
    EXPECT_EQ((bignum1 > bignum1),  (scriptnum1 > scriptnum1));
    EXPECT_EQ((bignum1 >= bignum1),  (scriptnum1 >= scriptnum1));
    EXPECT_EQ((bignum1 <= bignum1),  (scriptnum1 <= scriptnum1));

    EXPECT_EQ((bignum1 == bignum1), (scriptnum1 == num1));
    EXPECT_EQ((bignum1 != bignum1),  (scriptnum1 != num1));
    EXPECT_EQ((bignum1 < bignum1),  (scriptnum1 < num1));
    EXPECT_EQ((bignum1 > bignum1),  (scriptnum1 > num1));
    EXPECT_EQ((bignum1 >= bignum1),  (scriptnum1 >= num1));
    EXPECT_EQ((bignum1 <= bignum1),  (scriptnum1 <= num1));

    EXPECT_EQ((bignum1 == bignum2),  (scriptnum1 == scriptnum2));
    EXPECT_EQ((bignum1 != bignum2),  (scriptnum1 != scriptnum2));
    EXPECT_EQ((bignum1 < bignum2),  (scriptnum1 < scriptnum2));
    EXPECT_EQ((bignum1 > bignum2),  (scriptnum1 > scriptnum2));
    EXPECT_EQ((bignum1 >= bignum2),  (scriptnum1 >= scriptnum2));
    EXPECT_EQ((bignum1 <= bignum2),  (scriptnum1 <= scriptnum2));

    EXPECT_EQ((bignum1 == bignum2),  (scriptnum1 == num2));
    EXPECT_EQ((bignum1 != bignum2),  (scriptnum1 != num2));
    EXPECT_EQ((bignum1 < bignum2),  (scriptnum1 < num2));
    EXPECT_EQ((bignum1 > bignum2),  (scriptnum1 > num2));
    EXPECT_EQ((bignum1 >= bignum2),  (scriptnum1 >= num2));
    EXPECT_EQ((bignum1 <= bignum2),  (scriptnum1 <= num2));
}

static void RunCreate(const int64_t& num)
{
    CheckCreateInt(num);
    CScriptNum scriptnum(num);
    if (scriptnum.getvch().size() <= CScriptNum::nDefaultMaxNumSize)
        CheckCreateVch(num);
    else
    {
        EXPECT_THROW(CheckCreateVch(num), scriptnum_error);
    }
}

static void RunOperators(const int64_t& num1, const int64_t& num2)
{
    CheckAdd(num1, num2);
    CheckSubtract(num1, num2);
    CheckNegate(num1);
    CheckCompare(num1, num2);
}

TEST_F(scriptnum_tests, creation)
{
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j)
        {
            RunCreate(values[i]);
            RunCreate(values[i] + offsets[j]);
            RunCreate(values[i] - offsets[j]);
        }
    }
}

// CheckCreateVch (used by RunCreate/creation above) only ever constructs
// CScriptNum with fRequireMinimal=false, so script/script.h's non-minimal-
// encoding rejection path (and its 0xff00/0xff80 sign-bit exception) had no
// coverage anywhere in this file.
TEST_F(scriptnum_tests, NonMinimalEncoding)
{
    // Minimal encoding of zero is an empty vector; a single 0x00 byte is
    // therefore non-minimal and must be rejected when fRequireMinimal=true,
    // but still accepted when false (matches relaxed-verification callers).
    std::vector<unsigned char> nonMinimalZero{0x00};
    EXPECT_THROW(CScriptNum(nonMinimalZero, true), scriptnum_error);
    EXPECT_NO_THROW(CScriptNum(nonMinimalZero, false));

    // Sign-bit boundary exception: 0xff00/0xff80 encode +255/-255 and must
    // NOT be rejected even though their last byte's low 7 bits are zero,
    // since the second-to-last byte's sign bit is set.
    std::vector<unsigned char> plus255{0xff, 0x00};
    EXPECT_NO_THROW(CScriptNum(plus255, true));
    EXPECT_EQ(CScriptNum(plus255, true).getint(), 255);

    std::vector<unsigned char> minus255{0xff, 0x80};
    EXPECT_NO_THROW(CScriptNum(minus255, true));
    EXPECT_EQ(CScriptNum(minus255, true).getint(), -255);

    // A genuinely non-minimal multi-byte encoding, where the sign-bit
    // exception does not apply, must still be rejected.
    std::vector<unsigned char> nonMinimalMultiByte{0x01, 0x00};
    EXPECT_THROW(CScriptNum(nonMinimalMultiByte, true), scriptnum_error);
}

TEST_F(scriptnum_tests, operators)
{
    for(size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
    {
        for(size_t j = 0; j < sizeof(offsets) / sizeof(offsets[0]); ++j)
        {
            RunOperators(values[i], values[i]);
            RunOperators(values[i], -values[i]);
            RunOperators(values[i], values[j]);
            RunOperators(values[i], -values[j]);
            RunOperators(values[i] + values[j], values[j]);
            RunOperators(values[i] + values[j], -values[j]);
            RunOperators(values[i] - values[j], values[j]);
            RunOperators(values[i] - values[j], -values[j]);
            RunOperators(values[i] + values[j], values[i] + values[j]);
            RunOperators(values[i] + values[j], values[i] - values[j]);
            RunOperators(values[i] - values[j], values[i] + values[j]);
            RunOperators(values[i] - values[j], values[i] - values[j]);
        }
    }
}
