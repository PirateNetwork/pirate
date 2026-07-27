// Copyright (c) 2018 The Zcash developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/strencodings.h>
#include <gtest/gtestutils.h>
#include <zcash/NoteEncryption.hpp>

#include <gtest/gtest.h>

// Tests ConvertBits<>, the generic bit-width-regrouping helper (e.g. 8-bit
// bytes <-> 5-bit groups) used by bech32 encoding. Protocol-agnostic
// infrastructure, not shielded-pool-specific despite the zcash/ include.
class convertbits_tests : public BitcoinBasicTestingSetup {};

TEST_F(convertbits_tests, convertbits_deterministic)
{
    for (size_t i = 0; i < 256; i++) {
        std::vector<unsigned char> input(32, i);
        std::vector<unsigned char> data;
        std::vector<unsigned char> output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 52u);
        EXPECT_EQ(output.size(), 32u);
        EXPECT_TRUE(input == output);
    }

    for (size_t i = 0; i < 256; i++) {
        std::vector<unsigned char> input(43, i);
        std::vector<unsigned char> data;
        std::vector<unsigned char> output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 69u);
        EXPECT_EQ(output.size(), 43u);
        EXPECT_TRUE(input == output);
    }
}

TEST_F(convertbits_tests, convertbits_random)
{
    for (size_t i = 0; i < 1000; i++) {
        auto input = libzcash::random_uint256();
        std::vector<unsigned char> data;
        std::vector<unsigned char> output;
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, input.begin(), input.end());
        ConvertBits<5, 8, false>([&](unsigned char c) { output.push_back(c); }, data.begin(), data.end());
        EXPECT_EQ(data.size(), 52u);
        EXPECT_EQ(output.size(), 32u);
        EXPECT_TRUE(input == uint256(output));
    }
}
