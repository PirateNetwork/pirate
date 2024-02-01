#include <gtest/gtest.h>
#include "mini-gmp.h"
#include "utilstrencodings.h"
#include <univalue.h>

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
    };

    TEST(GMPArithTests, base58operations)
    {

        for (const auto &element : vBase58EncodeDecode)
        {

            assert(element.first.size() % 2 == 0);

            // encode test
            size_t encoded_length = element.second.size() + 1;
            std::unique_ptr<char[]> encodedStr(new char[encoded_length]);
            std::memset(encodedStr.get(), 0, encoded_length);

            std::vector<unsigned char> sourceData = ParseHex(element.first);
            const char *p_encoded = bitcoin_base58encode(encodedStr.get(), sourceData.data(), sourceData.size());

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
            std::unique_ptr<uint8_t[]> decodedStr(new uint8_t[decoded_length]);
            std::memset(decodedStr.get(), 0, decoded_length);

            size_t data_size = bitcoin_base58decode(decodedStr.get(), element.second.c_str()); // *(char (*)[10])decodedStr.get(),h

            ASSERT_GE(decoded_length, sourceData.size());
            ASSERT_EQ(data_size, sourceData.size());
            ASSERT_TRUE(std::memcmp(decodedStr.get(), sourceData.data(), sourceData.size()) == 0);
        }
    }
}