// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"

#include "test/data/base58_encode_decode.json.h"
#include "test/data/base58_keys_invalid.json.h"

#include "key.h"
#include "key_io.h"
#include "script/script.h"
#include "gtest/gtestutils.h"
#include "gtest/json_test_vectors.h"
#include "uint256.h"
#include "util.h"
#include "util/strencodings.h"

#include <univalue.h>

#include <gtest/gtest.h>

// Named base58_tests_bitcoin: the original file's base58_keys_valid_parse/
// base58_keys_valid_gen cases (against base58_keys_valid.json) were not
// ported. That data assumes vanilla-Bitcoin-mainnet base58 prefixes
// (PUBKEY_ADDRESS=0, SECRET_KEY=128); this fork's actual MAIN params use
// different prefixes (60/188, chainparams.cpp) - see test_key_bitcoin.cpp for
// the same discovery. The other three cases (raw base58 encode/decode, and
// rejecting corrupted address/key strings) don't depend on chain-specific
// prefixes and are ported unchanged.
class base58_tests_bitcoin : public BitcoinBasicTestingSetup {};

// Goal: test low-level base58 encoding functionality
TEST_F(base58_tests_bitcoin, base58_EncodeBase58)
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            ADD_FAILURE() << "Bad test: " << strTest;
            continue;
        }
        std::vector<unsigned char> sourcedata = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        EXPECT_TRUE(EncodeBase58(sourcedata.data(), sourcedata.data() + sourcedata.size()) == base58string) << strTest;
    }
}

// Goal: test low-level base58 decoding functionality
TEST_F(base58_tests_bitcoin, base58_DecodeBase58)
{
    UniValue tests = read_json(std::string(json_tests::base58_encode_decode, json_tests::base58_encode_decode + sizeof(json_tests::base58_encode_decode)));
    std::vector<unsigned char> result;

    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 2) // Allow for extra stuff (useful for comments)
        {
            ADD_FAILURE() << "Bad test: " << strTest;
            continue;
        }
        std::vector<unsigned char> expected = ParseHex(test[0].get_str());
        std::string base58string = test[1].get_str();
        EXPECT_TRUE(DecodeBase58(base58string, result)) << strTest;
        EXPECT_TRUE(result.size() == expected.size() && std::equal(result.begin(), result.end(), expected.begin())) << strTest;
    }

    EXPECT_TRUE(!DecodeBase58("invalid", result));

    // check that DecodeBase58 skips whitespace, but still fails with unexpected non-whitespace at the end.
    EXPECT_TRUE(!DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t a", result));
    EXPECT_TRUE( DecodeBase58(" \t\n\v\f\r skip \r\f\v\n\t ", result));
    std::vector<unsigned char> expected = ParseHex("971a55");
    ASSERT_EQ(result.size(), expected.size());
    EXPECT_TRUE(std::equal(result.begin(), result.end(), expected.begin()));
}

// Goal: check that base58 parsing code is robust against a variety of corrupted data
TEST_F(base58_tests_bitcoin, base58_keys_invalid)
{
    UniValue tests = read_json(std::string(json_tests::base58_keys_invalid, json_tests::base58_keys_invalid + sizeof(json_tests::base58_keys_invalid))); // Negative testcases
    CKey privkey;
    CTxDestination destination;

    for (size_t idx = 0; idx < tests.size(); idx++) {
        UniValue test = tests[idx];
        std::string strTest = test.write();
        if (test.size() < 1) // Allow for extra stuff (useful for comments)
        {
            ADD_FAILURE() << "Bad test: " << strTest;
            continue;
        }
        std::string exp_base58string = test[0].get_str();

        // must be invalid as public and as private key
        destination = DecodeDestination(exp_base58string);
        EXPECT_TRUE(!IsValidDestination(destination)) << "IsValid pubkey:" << strTest;
        privkey = DecodeSecret(exp_base58string);
        EXPECT_TRUE(!privkey.IsValid()) << "IsValid privkey:" << strTest;
    }
}
