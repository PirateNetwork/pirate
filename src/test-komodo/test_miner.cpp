#include <gtest/gtest.h>

bool test_tromp_equihash();

TEST(test_miner, check)
{
    EXPECT_FALSE(test_tromp_equihash());
}
