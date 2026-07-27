// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "main.h"

#include "gtest/gtestutils.h"

#include <boost/signals2/signal.hpp>
#include <gtest/gtest.h>

// Despite the filename, this does not test main.cpp's block/transaction
// validation logic. The one surviving case (test_combiner_all) only checks
// boost::signals2::signal's CombinerAll semantics -- the same combiner type
// main.cpp's validation-interface signals are declared with -- in isolation,
// with no chain/tx code involved.
//
// block_subsidy_test and subsidy_limit_test were not ported: they assumed
// GetBlockSubsidy() halves the reward every consensusParams.nSubsidyHalvingInterval
// blocks, Bitcoin/vanilla-Zcash style. This fork's GetBlockSubsidy() (main.cpp)
// is entirely Komodo-style instead - for chainName.isKMD() it returns a fixed
// ICO allocation at height 1 then flat tiers by hardfork height (KIP-0002);
// for any other asset chain it delegates to komodo_ac_block_subsidy(). Neither
// path ever reads nSubsidyHalvingInterval, so the halving premise these two
// tests checked doesn't correspond to any real code path here - verified by
// reading GetBlockSubsidy() directly, not just a failing assertion.
class main_tests_bitcoin : public BitcoinTestingSetup {};

static bool ReturnFalse() { return false; }
static bool ReturnTrue() { return true; }

TEST_F(main_tests_bitcoin, test_combiner_all)
{
    boost::signals2::signal<bool (), CombinerAll> Test;
    EXPECT_TRUE(Test());
    Test.connect(&ReturnFalse);
    EXPECT_TRUE(!Test());
    Test.connect(&ReturnTrue);
    EXPECT_TRUE(!Test());
    Test.disconnect(&ReturnFalse);
    EXPECT_TRUE(Test());
    Test.disconnect(&ReturnTrue);
    EXPECT_TRUE(Test());
}
