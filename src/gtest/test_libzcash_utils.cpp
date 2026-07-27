// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include "zcash/util.h"

// Tests small libzcash namespace helpers: byte-vector<->bit-vector
// conversion and bit-vector<->integer conversion. Protocol-agnostic
// infrastructure, not shielded-pool-specific despite the namespace name.

TEST(libzcash_utils, convertBytesVectorToVector)
{
    std::vector<unsigned char> bytes = {0x00, 0x01, 0x03, 0x12, 0xFF};
    std::vector<bool> expected_bits = {
        // 0x00
        0, 0, 0, 0, 0, 0, 0, 0,
        // 0x01
        0, 0, 0, 0, 0, 0, 0, 1,
        // 0x03
        0, 0, 0, 0, 0, 0, 1, 1,
        // 0x12
        0, 0, 0, 1, 0, 0, 1, 0,
        // 0xFF
        1, 1, 1, 1, 1, 1, 1, 1
    };
    ASSERT_TRUE(convertBytesVectorToVector(bytes) == expected_bits);
}

TEST(libzcash_utils, convertVectorToInt)
{
    ASSERT_TRUE(convertVectorToInt({0}) == 0);
    ASSERT_TRUE(convertVectorToInt({1}) == 1);
    ASSERT_TRUE(convertVectorToInt({0,1}) == 1);
    ASSERT_TRUE(convertVectorToInt({1,0}) == 2);
    ASSERT_TRUE(convertVectorToInt({1,1}) == 3);
    ASSERT_TRUE(convertVectorToInt({1,0,0}) == 4);
    ASSERT_TRUE(convertVectorToInt({1,0,1}) == 5);
    ASSERT_TRUE(convertVectorToInt({1,1,0}) == 6);

    ASSERT_THROW(convertVectorToInt(std::vector<bool>(100)), std::length_error);

    {
        std::vector<bool> v(63, 1);
        ASSERT_TRUE(convertVectorToInt(v) == 0x7fffffffffffffff);
    }

    {
        std::vector<bool> v(64, 1);
        ASSERT_TRUE(convertVectorToInt(v) == 0xffffffffffffffff);
    }
}
