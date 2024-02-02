#include <gtest/gtest.h>
#include "utilstrencodings.h"
#include "base58.h"
#include <univalue.h>
#include "komodo_utils.h"

#include <iostream>
#include <vector>
#include <utility>
#include <string>
#include <cstring>
#include <memory>

namespace GMPArithTests
{

    /*
        We are currently utilizing mini-gmp.c in our project specifically for bitcoin_base58decode / bitcoin_base58encode
        in a few areas, such as bitcoin_address (found in komodo_utils.cpp) or NSPV-related functions in
        komodo_nSPV_superlite.h or komodo_nSPV_wallet.h. However, our codebase already has other methods to compute
        base58 related tasks, such as EncodeBase58 / DecodeBase58 in base58.h. This means we could easily eliminate
        the base58 implementation provided by mini-gmp.h. However, we need to complete a few tests first.

    */

    std::vector<std::pair<std::string, std::string>> vBase58EncodeDecode = {
        {"", ""},
        {"61", "2g"},
        {"626262", "a3gV"},
        {"636363", "aPEr"},
        {"73696d706c792061206c6f6e6720737472696e67", "2cFupjhnEsSn59qHXstmK2ffpLv2"},
        {"00eb15231dfceb60925886b67d065299925915aeb172c06647", "1NS17iag9jJgTHD1VXjvLCEnZuQ3rJDE9L"},
        {"516b6fcd0f", "ABnLTmg"},
        {"bf4f89001e670274dd", "3SEo3LWLoPntC"},
        {"572e4794", "3EFU7m"},
        {"ecac89cad93923c02321", "EJDM8drfXA6uyA"},
        {"10c8511e", "Rt5zm"},
        {"00000000000000000000", "1111111111"},
        {"3cf1dce4182fce875748c4986b240ff7d7bc3fffb02fd6b4d5", "RXL3YXG2ceaB6C5hfJcN4fvmLH2C34knhA"},
        {"3c19494a36d54f01d680aa3c9b390f522a2a3dff7e2836626b", "RBatmanSuperManPaddingtonBearpcCTt"},
        {"3c531cca970048c1564bfdf8320729381d7755d481522e4402", "RGreetzToCA333FromDecker33332cYmMb"},
        {"22d73e8f", "test2"},
        {"bc907ece717a8f94e07de7bf6f8b3e9f91abb8858ebf831072cdbb9016ef53bc5d01bf0891e2", "UtrRXqvRFUAtCrCTRAHPH6yroQKUrrTJRmxt2h5U4QTUN1jCxTAh"},
        {"bc907ece717a8f94e07de7bf6f8b3e9f91abb8858ebf831072cdbb9016ef53bc5de225fbd6", "7KYb75jv5BgrDCbmW36yhofiBy2vSLpCCWDfJ9dMdZxPWnKicJh"},
    };

    TEST(GMPArithTests, base58operations)
    {

        for (const auto &element : vBase58EncodeDecode)
        {

            assert(element.first.size() % 2 == 0);

            // encode test
            size_t encoded_length = element.second.size() + 1;
            std::vector<char> encodedStr(encoded_length, 0);

            std::vector<unsigned char> sourceData = ParseHex(element.first);
            const char *p_encoded = bitcoin_base58encode(encodedStr.data(), sourceData.data(), sourceData.size());

            ASSERT_STREQ(p_encoded, element.second.c_str());

            p_encoded = nullptr;

            // decode test
            size_t decoded_length = element.second.size();
            /*  Here we assume that the decoded data is not larger than the encoded data.
                Typically, the decoded data is around 73% of the encoded data, calculated as
                <size_t>(element.second.size() * 0.732 ) + 1. However, if there are many
                leading '1's, each of them will decode into a single zero byte. In this case,
                the decoded data will be 100% of the encoded data.

                https://stackoverflow.com/questions/48333136/size-of-buffer-to-hold-base58-encoded-data
            */
            std::vector<uint8_t> decodedStr(decoded_length + 1, 0); // +1 to handle the case when decoded_length == 0, i.e. decodedStr.data() == nullptr (A)
            int32_t data_size = bitcoin_base58decode(decodedStr.data(), element.second.c_str()); // *(char (*)[10])decodedStr.get(),h
            // std::cerr << "'" << element.second << "': " << data_size << std::endl;

            ASSERT_GE(decoded_length, sourceData.size());
            ASSERT_EQ(data_size, sourceData.size());
            ASSERT_TRUE(std::memcmp(decodedStr.data(), sourceData.data(), sourceData.size()) == 0);
        }

        // special cases test (decode)
        std::vector<std::pair<std::string, int32_t>> decodeTestCases = {
            {"", 0},          // empty data to decode
            {"ERROR", -1},    // capital "O" is not a part of base58 alphabet
            {"test2", 4},
            {"RGreetzToCA333FromDecker33332cYmMb", 25},
            {"  test2", 4},   // spaces behind
            {"test2  ", 4},   // spaces after
            {"  test2  ", 4}, // spaces behind and after
            {"te st2", -1},   // spaces in-between
        };
        for (auto const &test : decodeTestCases)
        {
            std::vector<uint8_t> decodedStr(test.first.length() + 1, 0); // +1 for the same as in (A)
            int32_t data_size = bitcoin_base58decode(decodedStr.data(), test.first.c_str());
            // std::cerr << "'" << test.first << "': " << data_size << " - " << HexStr(decodedStr) << std::endl;
            ASSERT_EQ(data_size, test.second);
        }

        // special cases test (encode)
        char buf[] = { '\xff' };
        uint8_t data[] = {0xDE, 0xAD, 0xCA, 0xFE};
        const char *p_encoded = bitcoin_base58encode(&buf[0], data, 0);
        ASSERT_EQ(buf[0], '\0');
    }
}