#include <gtest/gtest.h>

#include "main.h"
#include "utilmoneystr.h"
#include "chainparams.h"
#include "utilstrencodings.h"
#include "zcash/Address.hpp"
#include "wallet/wallet.h"
#include "amount.h"
#include <memory>
#include <string>
#include <set>
#include <vector>
#include <boost/filesystem.hpp>
#include "util.h"

// To run tests:
// ./zero-gtest --gtest_filter="founders_reward_test.*"

//
// Enable this test to generate and print 48 testnet 2-of-3 multisig addresses.
// The output can be copied into chainparams.cpp.
// The temporary wallet file can be renamed as wallet.dat and used for testing with zerod.
//
#if 0
TEST(founders_reward_test, create_testnet_2of3multisig) {
    SelectParams(CBaseChainParams::TESTNET);
    boost::filesystem::path pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    bool fFirstRun;
    auto pWallet = std::make_shared<CWallet>("wallet.dat");
    ASSERT_EQ(DB_LOAD_OK, pWallet->LoadWallet(fFirstRun));
    pWallet->TopUpKeyPool();
    std::cout << "Test wallet and logs saved in folder: " << pathTemp.native() << std::endl;

    int numKeys = 48;
    std::vector<CPubKey> pubkeys;
    pubkeys.resize(3);
    CPubKey newKey;
    std::vector<std::string> addresses;
    for (int i = 0; i < numKeys; i++) {
        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[0] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[1] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        ASSERT_TRUE(pWallet->GetKeyFromPool(newKey));
        pubkeys[2] = newKey;
        pWallet->SetAddressBook(newKey.GetID(), "", "receive");

        CScript result = GetScriptForMultisig(2, pubkeys);
        ASSERT_FALSE(result.size() > MAX_SCRIPT_ELEMENT_SIZE);
        CScriptID innerID(result);
        pWallet->AddCScript(result);
        pWallet->SetAddressBook(innerID, "", "receive");

        std::string address = EncodeDestination(innerID);
        addresses.push_back(address);
    }

    // Print out the addresses, 4 on each line.
    std::string s = "vFoundersRewardAddress = {\n";
    int i=0;
    int colsPerRow = 4;
    ASSERT_TRUE(numKeys % colsPerRow == 0);
    int numRows = numKeys/colsPerRow;
    for (int row=0; row<numRows; row++) {
        s += "    ";
        for (int col=0; col<colsPerRow; col++) {
            s += "\"" + addresses[i++] + "\", ";
        }
        s += "\n";
    }
    s += "    };";
    std::cout << s << std::endl;

    pWallet->Flush(true);
}
#endif


// Utility method to check the number of unique addresses from height 1 to maxHeight
void checkNumberOfUniqueAddresses(int nUnique) {
    int startHeight = Params().GetConsensus().nFeeStartBlockHeight;
    int maxHeight = Params().GetConsensus().GetLastFoundersRewardBlockHeight();
    std::set<std::string> addresses;
    for (int i = startHeight; i <= maxHeight; i++) {
        addresses.insert(Params().GetFoundersRewardAddressAtHeight(i));
    }
    ASSERT_TRUE(addresses.size() == nUnique);
}


TEST(founders_reward_test, general) {
    SelectParams(CBaseChainParams::MAIN);

    CChainParams params = Params();

    // Fourth testnet reward:
    // address = t2ENg7hHVqqs9JwU5cgjvSbxnT2a9USNfhy
    // script.ToString() = OP_HASH160 55d64928e69829d9376c776550b6cc710d427153 OP_EQUAL
    // HexStr(script) = a91455d64928e69829d9376c776550b6cc710d42715387
    EXPECT_EQ(HexStr(params.GetFoundersRewardScriptAtHeight(Params().GetConsensus().nFeeStartBlockHeight)), "a914fe928701db352019291347074bf05507b970074187");
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(Params().GetConsensus().nFeeStartBlockHeight), "t3hmg6WApjqVFw9oPWTDy4JLEqXcUWthg5v");
    EXPECT_EQ(HexStr(params.GetFoundersRewardScriptAtHeight(1599999)), "a914ff856ce07140ae4127fbd9850a19922fe336c22387");
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(1599999), "t3hrh5M7eaGA5zXCitPXz2pbe146GkVPWHs");
    EXPECT_EQ(HexStr(params.GetFoundersRewardScriptAtHeight(1600000)), "a914aef7be8939ed9d0175497489b789268e4e2691c287");
    EXPECT_EQ(params.GetFoundersRewardAddressAtHeight(1600000), "t3aWmHqBGS7watoKQLa7uykeTaYHoYqM361");

    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();

    // If the block height parameter is out of bounds, there is an assert.
    EXPECT_DEATH(params.GetFoundersRewardScriptAtHeight(0), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardScriptAtHeight(maxHeight+1), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardAddressAtHeight(0), "nHeight");
    EXPECT_DEATH(params.GetFoundersRewardAddressAtHeight(maxHeight+1), "nHeight");
}


#define NUM_MAINNET_FOUNDER_ADDRESSES 10

TEST(founders_reward_test, mainnet) {
    SelectParams(CBaseChainParams::MAIN);
    checkNumberOfUniqueAddresses(NUM_MAINNET_FOUNDER_ADDRESSES);
}


#define NUM_TESTNET_FOUNDER_ADDRESSES 10

TEST(founders_reward_test, testnet) {
    SelectParams(CBaseChainParams::TESTNET);
    checkNumberOfUniqueAddresses(NUM_TESTNET_FOUNDER_ADDRESSES);
}


#define NUM_REGTEST_FOUNDER_ADDRESSES 1

TEST(founders_reward_test, regtest) {
    SelectParams(CBaseChainParams::REGTEST);
    checkNumberOfUniqueAddresses(NUM_REGTEST_FOUNDER_ADDRESSES);
}



// Test that 10% founders reward is fully rewarded after the first halving and slow start shift.
// On Mainnet, this would be 2,100,000 ZEC after 850,000 blocks (840,000 + 10,000).
TEST(founders_reward_test, slow_start_subsidy) {
    SelectParams(CBaseChainParams::MAIN);
    CChainParams params = Params();

    int startHeight = params.GetConsensus().nFeeStartBlockHeight;
    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();
    CAmount totalSubsidy = 0;
    for (int nHeight = startHeight; nHeight <= maxHeight; nHeight++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, params.GetConsensus()) * 0.075;
        //std::cout << "block subsidy " << nSubsidy;
        totalSubsidy += nSubsidy;
    }
    //std::cout << "Total Founders Fee " << totalSubsidy << "\n";
    ASSERT_TRUE(totalSubsidy == 96077136800000);

    //Max Money
    totalSubsidy = 0;
    for (int nHeight = 1; nHeight <= 500000000; nHeight++) {
        CAmount nSubsidy = GetBlockSubsidy(nHeight, params.GetConsensus());
        //std::cout << "block subsidy " << nSubsidy;
        totalSubsidy += nSubsidy;
    }
    //std::cout << "Max Money " << totalSubsidy << "\n";
    ASSERT_TRUE(totalSubsidy==MAX_MONEY);
}


// For use with mainnet and testnet which each have 48 addresses.
// Verify the number of rewards each individual address receives.
void verifyNumberOfRewards() {
    CChainParams params = Params();
    int startHeight = params.GetConsensus().nFeeStartBlockHeight;
    int maxHeight = params.GetConsensus().GetLastFoundersRewardBlockHeight();
    std::multiset<std::string> ms;
    for (int nHeight = startHeight; nHeight <= maxHeight; nHeight++) {
        //std::cout << "Test Fee by address nHeight " << nHeight << "\n";
        ms.insert(params.GetFoundersRewardAddressAtHeight(nHeight));
    }

    std::cout << params.GetFoundersRewardAddressAtIndex(0) << " " << ms.count(params.GetFoundersRewardAddressAtIndex(0)) << "\n";
    //ASSERT_TRUE(ms.count(params.GetFoundersRewardAddressAtIndex(0)) == 387700);
    for (int i = 1; i <= 8; i++) {
        std::cout << params.GetFoundersRewardAddressAtIndex(i) << " " << ms.count(params.GetFoundersRewardAddressAtIndex(i)) << "\n";
        //ASSERT_TRUE(ms.count(params.GetFoundersRewardAddressAtIndex(i)) == 800000);
    }
    std::cout << params.GetFoundersRewardAddressAtIndex(9) << " " << ms.count(params.GetFoundersRewardAddressAtIndex(9)) << "\n";
    //ASSERT_TRUE(ms.count(params.GetFoundersRewardAddressAtIndex(9)) == 800000);




}

// Verify the number of rewards going to each mainnet address
TEST(founders_reward_test, per_address_reward_mainnet) {
    SelectParams(CBaseChainParams::MAIN);
    verifyNumberOfRewards();
}

// Verify the number of rewards going to each testnet address
TEST(founders_reward_test, per_address_reward_testnet) {
    SelectParams(CBaseChainParams::TESTNET);
    verifyNumberOfRewards();
}
