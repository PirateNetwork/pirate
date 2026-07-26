// Copyright (c) 2017 Pieter Wuille
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bech32.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class bech32_tests : public BitcoinBasicTestingSetup {};

static bool CaseInsensitiveEqual(const std::string &s1, const std::string &s2)
{
    if (s1.size() != s2.size()) return false;
    for (size_t i = 0; i < s1.size(); ++i) {
        char c1 = s1[i];
        if (c1 >= 'A' && c1 <= 'Z') c1 -= ('A' - 'a');
        char c2 = s2[i];
        if (c2 >= 'A' && c2 <= 'Z') c2 -= ('A' - 'a');
        if (c1 != c2) return false;
    }
    return true;
}

TEST_F(bech32_tests, bip173_testvectors_valid)
{
    static const std::string CASES[] = {
        "A12UEL5L",
        "a12uel5l",
        "an83characterlonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1tt5tgs",
        "an84characterslonghumanreadablepartthatcontainsthenumber1andtheexcludedcharactersbio1569pvx",
        "abcdef1qpzry9x8gf2tvdw0s3jn54khce6mua7lmqqqxw",
        "11qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqc8247j",
        "split1checkupstagehandshakeupstreamerranterredcaperred2y9e3w",
        "?1ezyfcl",
    };
    for (const std::string& str : CASES) {
        auto ret = bech32::Decode(str);
        EXPECT_FALSE(ret.first.empty());
        std::string recode = bech32::Encode(ret.first, ret.second);
        EXPECT_FALSE(recode.empty());
        EXPECT_TRUE(CaseInsensitiveEqual(str, recode));
    }
}

TEST_F(bech32_tests, bip173_testvectors_invalid)
{
    static const std::string CASES[] = {
        " 1nwldj5",
        "\x7f""1axkwrx",
        "\x80""1eym55h",
        "pzry9x0s0muk",
        "1pzry9x0s0muk",
        "x1b4n0q5v",
        "li1dgmt3",
        "de1lg7wt\xff",
        "A1G7SGD8",
        "10a06t8",
        "1qzzfhee",
    };
    for (const std::string& str : CASES) {
        auto ret = bech32::Decode(str);
        EXPECT_TRUE(ret.first.empty());
    }
}

TEST_F(bech32_tests, bech32_deterministic_valid)
{
    for (size_t i = 0; i < 255; i++) {
        std::vector<unsigned char> input(32, i);
        auto encoded = bech32::Encode("a", input);
        if (i < 32) {
            // Valid input
            EXPECT_FALSE(encoded.empty());
            auto ret = bech32::Decode(encoded);
            EXPECT_TRUE(ret.first == "a");
            EXPECT_TRUE(ret.second == input);
        } else {
            // Invalid input
            EXPECT_TRUE(encoded.empty());
        }
    }

    for (size_t i = 0; i < 255; i++) {
        std::vector<unsigned char> input(43, i);
        auto encoded = bech32::Encode("a", input);
        if (i < 32) {
            // Valid input
            EXPECT_FALSE(encoded.empty());
            auto ret = bech32::Decode(encoded);
            EXPECT_TRUE(ret.first == "a");
            EXPECT_TRUE(ret.second == input);
        } else {
            // Invalid input
            EXPECT_TRUE(encoded.empty());
        }
    }
}
