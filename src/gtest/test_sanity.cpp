// Copyright (c) 2012-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "compat/sanity.h"
#include "key.h"
#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

class sanity_tests : public BitcoinBasicTestingSetup {};

TEST_F(sanity_tests, basic_sanity)
{
    EXPECT_TRUE(glibc_sanity_test()) << "libc sanity test";
    EXPECT_TRUE(glibcxx_sanity_test()) << "stdlib sanity test";
    EXPECT_TRUE(ECC_InitSanityCheck()) << "openssl ECC test";
}
