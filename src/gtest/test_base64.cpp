// Copyright (c) 2011-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util/strencodings.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class base64_tests : public BitcoinBasicTestingSetup {};

TEST_F(base64_tests, base64_testvectors)
{
    static const std::string vstrIn[]  = {"","f","fo","foo","foob","fooba","foobar"};
    static const std::string vstrOut[] = {"","Zg==","Zm8=","Zm9v","Zm9vYg==","Zm9vYmE=","Zm9vYmFy"};
    for (unsigned int i=0; i<sizeof(vstrIn)/sizeof(vstrIn[0]); i++)
    {
        std::string strEnc = EncodeBase64(vstrIn[i]);
        EXPECT_EQ(strEnc, vstrOut[i]);
        std::string strDec = DecodeBase64(strEnc);
        EXPECT_EQ(strDec, vstrIn[i]);
    }
}
