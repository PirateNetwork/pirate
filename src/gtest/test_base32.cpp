// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "util/strencodings.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class base32_tests : public BitcoinBasicTestingSetup {};

TEST_F(base32_tests, base32_testvectors)
{
    static const std::string vstrIn[]  = {"","f","fo","foo","foob","fooba","foobar"};
    static const std::string vstrOut[] = {"","my======","mzxq====","mzxw6===","mzxw6yq=","mzxw6ytb","mzxw6ytboi======"};
    static const std::string vstrOutNoPadding[] = {"","my","mzxq","mzxw6","mzxw6yq","mzxw6ytb","mzxw6ytboi"};
    for (unsigned int i=0; i<sizeof(vstrIn)/sizeof(vstrIn[0]); i++)
    {
        std::string strEnc = EncodeBase32(vstrIn[i]);
        EXPECT_EQ(strEnc, vstrOut[i]);
        strEnc = EncodeBase32(vstrIn[i], false);
        EXPECT_EQ(strEnc, vstrOutNoPadding[i]);
        std::string strDec = DecodeBase32(vstrOut[i]);
        EXPECT_EQ(strDec, vstrIn[i]);
    }
}
