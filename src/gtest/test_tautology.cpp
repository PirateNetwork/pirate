// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

// This file tests the gtest harness itself, not any product code.
// DISABLED_ObviousFailure is an intentionally disabled "canary" test:
// if it were ever enabled it would fail on purpose, confirming that
// gtest still correctly reports failures. It is not a real bug.
TEST(tautologies, seven_eq_seven) {
    ASSERT_EQ(7, 7);
}

TEST(tautologies, DISABLED_ObviousFailure)
{
    FAIL() << "This is expected";
}
