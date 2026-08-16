// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "assetchain.h"
#include "chainparams.h"

#include <gtest/gtest.h>

// Regression coverage for GetACDNSSeeds()/GetACFixedSeeds() (chainparams.cpp),
// added alongside the fix that stopped every asset chain built from this
// codebase - PIRATETST included - from inheriting mainnet PIRATE's DNS seeds
// (dnsseed1-2.cryptoforge.cc) and baked-in fixed-seed IP list. Previously
// these were set unconditionally in CMainParams's own constructor with no
// -ac_name awareness at all; now they're only ever populated for
// chainName.isSymbol("PIRATE"), via chainparams_commandline() calling these
// two functions. See chainparams.cpp and the "Isolate PIRATETST/other asset
// chains from PIRATE's mainnet peer sources" commit for the full rationale.
//
// chainName is process-global state (assetchain, declared in assetchain.h),
// so each case here saves and restores it, mirroring how the
// UpdateNetworkUpgradeParameters-based tests elsewhere in this suite use an
// RAII reverter around other global consensus/chain state.
class chainparams_tests : public ::testing::Test {
protected:
    assetchain savedChainName;

    void SetUp() override {
        savedChainName = chainName;
    }
    void TearDown() override {
        chainName = savedChainName;
    }
};

TEST_F(chainparams_tests, PirateGetsRealDNSAndFixedSeeds)
{
    chainName = assetchain("PIRATE");

    auto seeds = GetACDNSSeeds();
    ASSERT_EQ(seeds.size(), 2u);
    EXPECT_EQ(seeds[0].host, "dnsseed1.cryptoforge.cc");
    EXPECT_EQ(seeds[1].host, "dnsseed2.cryptoforge.cc");

    auto fixedSeeds = GetACFixedSeeds();
    EXPECT_GT(fixedSeeds.size(), 0u)
        << "mainnet PIRATE should have real baked-in fixed-seed IP data "
           "(chainparamsseeds.h's chainparams_seed_main)";
}

TEST_F(chainparams_tests, PiratetstGetsNoSeedsAtAll)
{
    chainName = assetchain("PIRATETST");

    EXPECT_TRUE(GetACDNSSeeds().empty())
        << "PIRATETST has no real DNS seed infrastructure of its own and "
           "must not inherit mainnet PIRATE's";
    EXPECT_TRUE(GetACFixedSeeds().empty())
        << "PIRATETST has no curated fixed-seed IP data of its own and must "
           "not inherit mainnet PIRATE's";
}

// The bug this whole mechanism fixes wasn't specific to PIRATETST - the
// original code gated only on !testnet/!regtest, never on -ac_name, so *any*
// asset chain built from this codebase inherited PIRATE's mainnet seeds.
// This exercises that general case directly: a chain name that is neither
// PIRATE nor PIRATETST (nor KMD) must still come back empty.
TEST_F(chainparams_tests, ArbitraryOtherAssetChainGetsNoSeeds)
{
    chainName = assetchain("SOMEOTHERCHAIN");

    EXPECT_TRUE(GetACDNSSeeds().empty());
    EXPECT_TRUE(GetACFixedSeeds().empty());
}

// isKMD() (empty symbol) is a distinct case from "some other asset chain" -
// GetACDNSSeeds()/GetACFixedSeeds() are documented as not meant to be called
// for KMD at all (see GetACCheckPoints()'s own comment to the same effect),
// but if they ever are, the result must still never be PIRATE's mainnet
// seed data.
TEST_F(chainparams_tests, KmdGetsNoSeeds)
{
    chainName = assetchain("");
    ASSERT_TRUE(chainName.isKMD());

    EXPECT_TRUE(GetACDNSSeeds().empty());
    EXPECT_TRUE(GetACFixedSeeds().empty());
}
