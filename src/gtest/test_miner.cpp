// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

bool test_tromp_equihash();

TEST(test_miner, check)
{
    EXPECT_FALSE(test_tromp_equihash());
}
