// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "serialize.h"
#include "streams.h"
#include "hash.h"
#include "gtest/gtestutils.h"
#include "util/strencodings.h"

#include <array>
#include <stdint.h>

#include <gtest/gtest.h>

using namespace std;


template<typename T>
void check_ser_rep(T thing, std::vector<unsigned char> expected)
{
    CDataStream ss(SER_DISK, 0);
    ss << thing;

    EXPECT_TRUE(GetSerializeSize(thing, 0, 0) == ss.size());

    std::vector<unsigned char> serialized_representation(ss.begin(), ss.end());

    EXPECT_TRUE(serialized_representation == expected);

    T thing_deserialized;
    ss >> thing_deserialized;

    EXPECT_TRUE(thing_deserialized == thing);
}

class serialize_tests_bitcoin : public BitcoinBasicTestingSetup {};

class CSerializeMethodsTestSingle
{
protected:
    int intval;
    bool boolval;
    std::string stringval;
    const char* charstrval;
    CTransaction txval;
public:
    CSerializeMethodsTestSingle() = default;
    CSerializeMethodsTestSingle(int intvalin, bool boolvalin, std::string stringvalin, const char* charstrvalin, CTransaction txvalin) : intval(intvalin), boolval(boolvalin), stringval(std::move(stringvalin)), charstrval(charstrvalin), txval(txvalin){}
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(intval);
        READWRITE(boolval);
        READWRITE(stringval);
        READWRITE(FLATDATA(charstrval));
        READWRITE(txval);
    }

    bool operator==(const CSerializeMethodsTestSingle& rhs)
    {
        return  intval == rhs.intval && \
                boolval == rhs.boolval && \
                stringval == rhs.stringval && \
                strcmp(charstrval, rhs.charstrval) == 0 && \
                txval == rhs.txval;
    }
};

class CSerializeMethodsTestMany : public CSerializeMethodsTestSingle
{
public:
    using CSerializeMethodsTestSingle::CSerializeMethodsTestSingle;
    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITEMANY(intval, boolval, stringval, FLATDATA(charstrval), txval);
    }
};

TEST_F(serialize_tests_bitcoin, boost_optional)
{
    check_ser_rep<std::optional<unsigned char>>(0xff, {0x01, 0xff});
    check_ser_rep<std::optional<unsigned char>>(std::nullopt, {0x00});
    check_ser_rep<std::optional<std::string>>(std::string("Test"), {0x01, 0x04, 'T', 'e', 's', 't'});

    {
        // Ensure that canonical optional discriminant is used
        CDataStream ss(SER_DISK, 0);
        ss.write("\x02\x04Test", 6);
        std::optional<std::string> into;

        EXPECT_THROW(ss >> into, std::ios_base::failure);
    }
}

TEST_F(serialize_tests_bitcoin, arrays)
{
    std::array<std::string, 2> test_case = {string("zub"), string("baz")};
    CDataStream ss(SER_DISK, 0);
    ss << test_case;

    auto hash = Hash(ss.begin(), ss.end());

    EXPECT_TRUE("037a75620362617a" == HexStr(ss.begin(), ss.end())) << HexStr(ss.begin(), ss.end());
    EXPECT_TRUE(hash == uint256S("13cb12b2dd098dced0064fe4897c97f907ba3ed36ae470c2e7fc2b1111eba35a")) << "actually got: " << hash.ToString();

    {
        // note: boost array of size 2 should serialize to be the same as a tuple
        std::pair<std::string, std::string> test_case_2 = {string("zub"), string("baz")};

        CDataStream ss2(SER_DISK, 0);
        ss2 << test_case_2;

        auto hash2 = Hash(ss2.begin(), ss2.end());

        EXPECT_TRUE(hash == hash2);
    }

    std::array<std::string, 2> decoded_test_case;
    ss >> decoded_test_case;

    EXPECT_TRUE(decoded_test_case == test_case);

    std::array<int32_t, 2> test = {100, 200};

    EXPECT_EQ(GetSerializeSize(test, 0, 0), 8);
}

TEST_F(serialize_tests_bitcoin, sizes)
{
    EXPECT_EQ(sizeof(char), GetSerializeSize(char(0), 0));
    EXPECT_EQ(sizeof(int8_t), GetSerializeSize(int8_t(0), 0));
    EXPECT_EQ(sizeof(uint8_t), GetSerializeSize(uint8_t(0), 0));
    EXPECT_EQ(sizeof(int16_t), GetSerializeSize(int16_t(0), 0));
    EXPECT_EQ(sizeof(uint16_t), GetSerializeSize(uint16_t(0), 0));
    EXPECT_EQ(sizeof(int32_t), GetSerializeSize(int32_t(0), 0));
    EXPECT_EQ(sizeof(uint32_t), GetSerializeSize(uint32_t(0), 0));
    EXPECT_EQ(sizeof(int64_t), GetSerializeSize(int64_t(0), 0));
    EXPECT_EQ(sizeof(uint64_t), GetSerializeSize(uint64_t(0), 0));
    EXPECT_EQ(sizeof(float), GetSerializeSize(float(0), 0));
    EXPECT_EQ(sizeof(double), GetSerializeSize(double(0), 0));
    // Bool is serialized as char
    EXPECT_EQ(sizeof(char), GetSerializeSize(bool(0), 0));

    // Sanity-check GetSerializeSize and c++ type matching
    EXPECT_EQ(GetSerializeSize(char(0), 0), 1);
    EXPECT_EQ(GetSerializeSize(int8_t(0), 0), 1);
    EXPECT_EQ(GetSerializeSize(uint8_t(0), 0), 1);
    EXPECT_EQ(GetSerializeSize(int16_t(0), 0), 2);
    EXPECT_EQ(GetSerializeSize(uint16_t(0), 0), 2);
    EXPECT_EQ(GetSerializeSize(int32_t(0), 0), 4);
    EXPECT_EQ(GetSerializeSize(uint32_t(0), 0), 4);
    EXPECT_EQ(GetSerializeSize(int64_t(0), 0), 8);
    EXPECT_EQ(GetSerializeSize(uint64_t(0), 0), 8);
    EXPECT_EQ(GetSerializeSize(float(0), 0), 4);
    EXPECT_EQ(GetSerializeSize(double(0), 0), 8);
    EXPECT_EQ(GetSerializeSize(bool(0), 0), 1);
}

TEST_F(serialize_tests_bitcoin, floats_conversion)
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    EXPECT_EQ(ser_uint32_to_float(0x00000000), 0.0F);
    EXPECT_EQ(ser_uint32_to_float(0x3f000000), 0.5F);
    EXPECT_EQ(ser_uint32_to_float(0x3f800000), 1.0F);
    EXPECT_EQ(ser_uint32_to_float(0x40000000), 2.0F);
    EXPECT_EQ(ser_uint32_to_float(0x40800000), 4.0F);
    EXPECT_EQ(ser_uint32_to_float(0x44444444), 785.066650390625F);

    EXPECT_EQ(ser_float_to_uint32(0.0F), 0x00000000);
    EXPECT_EQ(ser_float_to_uint32(0.5F), 0x3f000000);
    EXPECT_EQ(ser_float_to_uint32(1.0F), 0x3f800000);
    EXPECT_EQ(ser_float_to_uint32(2.0F), 0x40000000);
    EXPECT_EQ(ser_float_to_uint32(4.0F), 0x40800000);
    EXPECT_EQ(ser_float_to_uint32(785.066650390625F), 0x44444444);
}

TEST_F(serialize_tests_bitcoin, doubles_conversion)
{
    // Choose values that map unambigiously to binary floating point to avoid
    // rounding issues at the compiler side.
    EXPECT_EQ(ser_uint64_to_double(0x0000000000000000ULL), 0.0);
    EXPECT_EQ(ser_uint64_to_double(0x3fe0000000000000ULL), 0.5);
    EXPECT_EQ(ser_uint64_to_double(0x3ff0000000000000ULL), 1.0);
    EXPECT_EQ(ser_uint64_to_double(0x4000000000000000ULL), 2.0);
    EXPECT_EQ(ser_uint64_to_double(0x4010000000000000ULL), 4.0);
    EXPECT_EQ(ser_uint64_to_double(0x4088888880000000ULL), 785.066650390625);

    EXPECT_EQ(ser_double_to_uint64(0.0), 0x0000000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(0.5), 0x3fe0000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(1.0), 0x3ff0000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(2.0), 0x4000000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(4.0), 0x4010000000000000ULL);
    EXPECT_EQ(ser_double_to_uint64(785.066650390625), 0x4088888880000000ULL);
}
/*
Python code to generate the below hashes:

    def reversed_hex(x):
        return binascii.hexlify(''.join(reversed(x)))
    def dsha256(x):
        return hashlib.sha256(hashlib.sha256(x).digest()).digest()

    reversed_hex(dsha256(''.join(struct.pack('<f', x) for x in range(0,1000)))) == '8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c'
    reversed_hex(dsha256(''.join(struct.pack('<d', x) for x in range(0,1000)))) == '43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96'
*/
TEST_F(serialize_tests_bitcoin, floats)
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << float(i);
    }
    EXPECT_TRUE(Hash(ss.begin(), ss.end()) == uint256S("8e8b4cf3e4df8b332057e3e23af42ebc663b61e0495d5e7e32d85099d7f3fe0c"));

    // decode
    for (int i = 0; i < 1000; i++) {
        float j;
        ss >> j;
        EXPECT_TRUE(i == j) << "decoded:" << j << " expected:" << i;
    }
}

TEST_F(serialize_tests_bitcoin, doubles)
{
    CDataStream ss(SER_DISK, 0);
    // encode
    for (int i = 0; i < 1000; i++) {
        ss << double(i);
    }
    EXPECT_TRUE(Hash(ss.begin(), ss.end()) == uint256S("43d0c82591953c4eafe114590d392676a01585d25b25d433557f0d7878b23f96"));

    // decode
    for (int i = 0; i < 1000; i++) {
        double j;
        ss >> j;
        EXPECT_TRUE(i == j) << "decoded:" << j << " expected:" << i;
    }
}

TEST_F(serialize_tests_bitcoin, varints)
{
    // encode

    CDataStream ss(SER_DISK, 0);
    CDataStream::size_type size = 0;
    for (int i = 0; i < 100000; i++) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        EXPECT_TRUE(size == ss.size());
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        ss << VARINT(i);
        size += ::GetSerializeSize(VARINT(i), 0, 0);
        EXPECT_TRUE(size == ss.size());
    }

    // decode
    for (int i = 0; i < 100000; i++) {
        int j = -1;
        ss >> VARINT(j);
        EXPECT_TRUE(i == j) << "decoded:" << j << " expected:" << i;
    }

    for (uint64_t i = 0;  i < 100000000000ULL; i += 999999937) {
        uint64_t j = -1;
        ss >> VARINT(j);
        EXPECT_TRUE(i == j) << "decoded:" << j << " expected:" << i;
    }
}

TEST_F(serialize_tests_bitcoin, compactsize)
{
    CDataStream ss(SER_DISK, 0);
    vector<char>::size_type i, j;

    for (i = 1; i <= MAX_SIZE; i *= 2)
    {
        WriteCompactSize(ss, i-1);
        WriteCompactSize(ss, i);
    }
    for (i = 1; i <= MAX_SIZE; i *= 2)
    {
        j = ReadCompactSize(ss);
        EXPECT_TRUE((i-1) == j) << "decoded:" << j << " expected:" << (i-1);
        j = ReadCompactSize(ss);
        EXPECT_TRUE(i == j) << "decoded:" << j << " expected:" << i;
    }
}

static bool isCanonicalException(const std::ios_base::failure& ex)
{
    std::ios_base::failure expectedException("non-canonical ReadCompactSize()");

    // The string returned by what() can be different for different platforms.
    // Instead of directly comparing the ex.what() with an expected string,
    // create an instance of exception to see if ex.what() matches
    // the expected explanatory string returned by the exception instance.
    return strcmp(expectedException.what(), ex.what()) == 0;
}


static void ExpectNonCanonicalException(CDataStream& ss)
{
    try {
        ReadCompactSize(ss);
        FAIL() << "expected std::ios_base::failure";
    } catch (const std::ios_base::failure& ex) {
        EXPECT_TRUE(isCanonicalException(ex)) << ex.what();
    }
}

TEST_F(serialize_tests_bitcoin, noncanonical)
{
    // Write some non-canonical CompactSize encodings, and
    // make sure an exception is thrown when read back.
    CDataStream ss(SER_DISK, 0);
    vector<char>::size_type n;

    // zero encoded with three bytes:
    ss.write("\xfd\x00\x00", 3);
    ExpectNonCanonicalException(ss);

    // 0xfc encoded with three bytes:
    ss.write("\xfd\xfc\x00", 3);
    ExpectNonCanonicalException(ss);

    // 0xfd encoded with three bytes is OK:
    ss.write("\xfd\xfd\x00", 3);
    n = ReadCompactSize(ss);
    EXPECT_TRUE(n == 0xfd);

    // zero encoded with five bytes:
    ss.write("\xfe\x00\x00\x00\x00", 5);
    ExpectNonCanonicalException(ss);

    // 0xffff encoded with five bytes:
    ss.write("\xfe\xff\xff\x00\x00", 5);
    ExpectNonCanonicalException(ss);

    // zero encoded with nine bytes:
    ss.write("\xff\x00\x00\x00\x00\x00\x00\x00\x00", 9);
    ExpectNonCanonicalException(ss);

    // 0x01ffffff encoded with nine bytes:
    ss.write("\xff\xff\xff\xff\x01\x00\x00\x00\x00", 9);
    ExpectNonCanonicalException(ss);
}

TEST_F(serialize_tests_bitcoin, insert_delete)
{
    // Test inserting/deleting bytes.
    CDataStream ss(SER_DISK, 0);
    EXPECT_EQ(ss.size(), 0);

    ss.write("\x00\x01\x02\xff", 4);
    EXPECT_EQ(ss.size(), 4);

    char c = (char)11;

    // Inserting at beginning/end/middle:
    ss.insert(ss.begin(), c);
    EXPECT_EQ(ss.size(), 5);
    EXPECT_EQ(ss[0], c);
    EXPECT_EQ(ss[1], 0);

    ss.insert(ss.end(), c);
    EXPECT_EQ(ss.size(), 6);
    EXPECT_EQ(ss[4], (char)0xff);
    EXPECT_EQ(ss[5], c);

    ss.insert(ss.begin()+2, c);
    EXPECT_EQ(ss.size(), 7);
    EXPECT_EQ(ss[2], c);

    // Delete at beginning/end/middle
    ss.erase(ss.begin());
    EXPECT_EQ(ss.size(), 6);
    EXPECT_EQ(ss[0], 0);

    ss.erase(ss.begin()+ss.size()-1);
    EXPECT_EQ(ss.size(), 5);
    EXPECT_EQ(ss[4], (char)0xff);

    ss.erase(ss.begin()+1);
    EXPECT_EQ(ss.size(), 4);
    EXPECT_EQ(ss[0], 0);
    EXPECT_EQ(ss[1], 1);
    EXPECT_EQ(ss[2], 2);
    EXPECT_EQ(ss[3], (char)0xff);

    // Make sure GetAndClear does the right thing:
    CSerializeData d;
    ss.GetAndClear(d);
    EXPECT_EQ(ss.size(), 0);
}

TEST_F(serialize_tests_bitcoin, class_methods)
{
    int intval(100);
    bool boolval(true);
    std::string stringval("testing");
    const char* charstrval("testing charstr");
    CMutableTransaction txval;
    CSerializeMethodsTestSingle methodtest1(intval, boolval, stringval, charstrval, txval);
    CSerializeMethodsTestMany methodtest2(intval, boolval, stringval, charstrval, txval);
    CSerializeMethodsTestSingle methodtest3;
    CSerializeMethodsTestMany methodtest4;
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    EXPECT_TRUE(methodtest1 == methodtest2);
    ss << methodtest1;
    ss >> methodtest4;
    ss << methodtest2;
    ss >> methodtest3;
    EXPECT_TRUE(methodtest1 == methodtest2);
    EXPECT_TRUE(methodtest2 == methodtest3);
    EXPECT_TRUE(methodtest3 == methodtest4);

    CDataStream ss2(SER_DISK, PROTOCOL_VERSION, intval, boolval, stringval, FLATDATA(charstrval), txval);
    ss2 >> methodtest3;
    EXPECT_TRUE(methodtest3 == methodtest4);
}

