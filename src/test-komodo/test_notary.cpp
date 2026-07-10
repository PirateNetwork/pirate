#include "testutils.h"
#include "chainparams.h"
#include "komodo.h"
#include "komodo_bitcoind.h"
#include "komodo_events.h"
#include "komodo_gateway.h"
#include "komodo_globals.h"
#include "komodo_notary.h"
#include "notaries_staked.h"

#include <boost/filesystem.hpp>
#include <gtest/gtest.h>

//void undo_init_notaries(); // test helper
void komodo_notaries_uninit();


namespace TestNotary
{

class DPoWActiveChain
{
public:
    DPoWActiveChain()
        : genesisHash(uint256S("01")), primaryHash1(uint256S("11")), primaryHash2(uint256S("12")),
          primaryHash3(uint256S("13")), forkHash1(uint256S("21")), forkHash2(uint256S("22")),
          forkHash3(uint256S("23"))
    {
        InitializeIndex(genesis,genesisHash,0,nullptr);
        InitializeIndex(primary1,primaryHash1,1,&genesis);
        InitializeIndex(primary2,primaryHash2,2,&primary1);
        InitializeIndex(primary3,primaryHash3,3,&primary2);
        InitializeIndex(fork1,forkHash1,1,&genesis);
        InitializeIndex(fork2,forkHash2,2,&fork1);
        InitializeIndex(fork3,forkHash3,3,&fork2);
        UsePrimary();
    }

    ~DPoWActiveChain()
    {
        LOCK(cs_main);
        chainActive.SetTip(nullptr);
    }

    void UsePrimary()
    {
        LOCK(cs_main);
        chainActive.SetTip(&primary3);
    }

    void UseFork()
    {
        LOCK(cs_main);
        chainActive.SetTip(&fork3);
    }

    const uint256& PrimaryHash2() const { return primaryHash2; }

private:
    static void InitializeIndex(CBlockIndex& index,uint256& hash,int32_t height,CBlockIndex *previous)
    {
        index.phashBlock = &hash;
        index.nHeight = height;
        index.nTime = 1700000000 + height;
        index.pprev = previous;
    }

    uint256 genesisHash;
    uint256 primaryHash1;
    uint256 primaryHash2;
    uint256 primaryHash3;
    uint256 forkHash1;
    uint256 forkHash2;
    uint256 forkHash3;
    CBlockIndex genesis;
    CBlockIndex primary1;
    CBlockIndex primary2;
    CBlockIndex primary3;
    CBlockIndex fork1;
    CBlockIndex fork2;
    CBlockIndex fork3;
};

CScript DPoWReceiptScript(const uint256& blockHash,int32_t blockHeight,const uint256& litecoinTxid)
{
    std::vector<uint8_t> payload(blockHash.begin(),blockHash.end());
    uint32_t encodedHeight = static_cast<uint32_t>(blockHeight);
    for (int32_t i=0; i<4; ++i)
        payload.push_back((encodedHeight >> (8 * i)) & 0xff);
    payload.insert(payload.end(),litecoinTxid.begin(),litecoinTxid.end());
    const char symbol[] = "PIRATE";
    payload.insert(payload.end(),symbol,symbol + sizeof(symbol));
    return(CScript() << OP_RETURN << payload);
}

/***
 * A little class to help with the different formats keys come in
 */
class my_key
{
public:
    my_key(uint8_t in[33])
    {
        for(int i = 0; i < 33; ++i)
            key.push_back(in[i]);
    }
    my_key(const std::string& in)
    {
        for(int i = 0; i < 33; ++i)
            key.push_back( 
                    (unsigned int)strtol(in.substr(i*2, 2).c_str(), nullptr, 16) );
    }
    bool fill(uint8_t in[33])
    {
        memcpy(in, key.data(), 33);
        return true;
    }
private:
    std::vector<uint8_t> key;
    friend bool operator==(const my_key& lhs, const my_key& rhs);
};

bool operator==(const my_key& lhs, const my_key& rhs)
{
    if (lhs.key == rhs.key)
        return true;
    return false;
}

TEST(TestNotary, KomodoNotaries)
{
    // Test komodo_notaries(), getkmdseason()
    chainName = assetchain(""); // set as KMD
    SelectParams(CBaseChainParams::MAIN);
    komodo_notaries_uninit();
    //undo_init_STAKED();
    uint8_t pubkeys[64][33];
    int32_t height = 0;
    uint32_t timestamp = 0;
    // Get the pubkeys of the first era
    int32_t result = komodo_notaries(pubkeys, height, timestamp);
    EXPECT_EQ(result, 35);
    // the first notary didn't change between era 1 and 2, so look at the 2nd notary
    EXPECT_EQ( my_key(pubkeys[1]), my_key("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344"));
    // make sure the era hasn't changed before it is supposed to
    for(;height <= 179999; ++height)
    {
        result = komodo_notaries(pubkeys, height, timestamp);
        EXPECT_EQ(result, 35);
        EXPECT_EQ( getkmdseason(height), 1);
        EXPECT_EQ( my_key(pubkeys[1]), my_key("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344"));
        if (result != 35 || getkmdseason(height) != 1) break; // cancel long test
    }
    EXPECT_EQ(height, 180000);
    // at 180000 we start using notaries_elected(komodo_defs.h) instead of Notaries_genesis(komodo_notary.cpp)
    for(;height <= 814000; ++height)
    {
        result = komodo_notaries(pubkeys, height, timestamp);
        EXPECT_EQ(result, 64);
        EXPECT_EQ( getkmdseason(height), 1);
        EXPECT_EQ( my_key(pubkeys[1]), my_key("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344"));
        if (result != 64 || getkmdseason(height) != 1) break; // cancel long test
    }
    // make sure the era changes when it was supposed to, and we have a new key
    EXPECT_EQ(height, 814001);
    result = komodo_notaries(pubkeys, height, timestamp);
    EXPECT_EQ(result, 64);
    EXPECT_EQ( getkmdseason(height), 2);
    EXPECT_EQ( my_key(pubkeys[1]), my_key("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efed961") );

    // now try the same thing with notaries_staked, which uses timestamp instead of height
    // NOTE: If height is passed instead of timestamp, the timestamp is computed based on block timestamps
    // notaries come from notaries_STAKED(notaries_staked.h)
    // also tests STAKED_era()
    height = 0;
    timestamp = 1;
    komodo_notaries_uninit();
    //undo_init_STAKED();
    chainName = assetchain("LABS");
    // we should be in era 1 now
    result = komodo_notaries(pubkeys, height, timestamp);
    EXPECT_EQ(result, 22);
    EXPECT_EQ( STAKED_era(timestamp), 1);
    EXPECT_EQ( my_key(pubkeys[13]), my_key("03669457b2934d98b5761121dd01b243aed336479625b293be9f8c43a6ae7aaeff"));
    timestamp = 1572523200;
    EXPECT_EQ(result, 22);
    EXPECT_EQ( STAKED_era(timestamp), 1);
    EXPECT_EQ( my_key(pubkeys[13]), my_key("03669457b2934d98b5761121dd01b243aed336479625b293be9f8c43a6ae7aaeff"));
    // Moving to era 2 should change the notaries. But there is a gap of 777 that uses komodo notaries for some reason
    // (NOTE: the first 12 are the same, so use the 13th)
    timestamp++;
    EXPECT_EQ(timestamp, 1572523201);
    result = komodo_notaries(pubkeys, height, timestamp);
    EXPECT_EQ(result, 64);
    EXPECT_EQ( STAKED_era(timestamp), 0);
    EXPECT_EQ( pubkeys[13][0], 0);
    // advance past the gap
    timestamp += 778;
    result = komodo_notaries(pubkeys, height, timestamp);
    EXPECT_EQ(result, 24);
    EXPECT_EQ( STAKED_era(timestamp), 2);
    EXPECT_EQ( my_key(pubkeys[13]), my_key("02d1dd4c5d5c00039322295aa965f9787a87d234ed4f8174231bbd6162e384eba8"));

    // now test getacseason()
    EXPECT_EQ( getacseason(0), 1);
    EXPECT_EQ( getacseason(1), 1);
    EXPECT_EQ( getacseason(1525132800), 1);
    EXPECT_EQ( getacseason(1525132801), 2);
}

TEST(TestNotary, ElectedNotary)
{
    // exercise the routine that checks to see if a particular public key is a notary at the current height

    SelectParams(CBaseChainParams::MAIN);
    // setup
    komodo_notaries_uninit();
    //undo_init_STAKED();
    my_key first_era("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344");
    my_key second_era("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efed961");

    int32_t numnotaries;
    uint8_t pubkey[33];
    first_era.fill(pubkey);
    int32_t height = 0;
    uint32_t timestamp = 0;

    // check the KMD chain, first era
    chainName = assetchain();
    int32_t result = komodo_electednotary(&numnotaries, pubkey, height, timestamp);
    EXPECT_EQ(result, 1);
    EXPECT_EQ( numnotaries, 35);
    // now try a wrong key
    second_era.fill(pubkey);
    result = komodo_electednotary(&numnotaries, pubkey, height, timestamp);
    EXPECT_EQ(result, -1);
    EXPECT_EQ(numnotaries, 35);

    // KMD chain, second era
    height = 814001;
    result = komodo_electednotary(&numnotaries, pubkey, height, timestamp);
    EXPECT_EQ(result, 1);
    EXPECT_EQ( numnotaries, 64);
    // now try a wrong key
    first_era.fill(pubkey);
    result = komodo_electednotary(&numnotaries, pubkey, height, timestamp);
    EXPECT_EQ(result, -1);
    EXPECT_EQ(numnotaries, 64);
}

// season 7 tests for KMD:
TEST(TestNotary, KomodoNotaries_S7_KMD)
{
    uint8_t pubkeys[64][33];

    SelectParams(CBaseChainParams::MAIN);
    komodo_notaries_uninit();
    chainName = assetchain(""); // set as KMD

    EXPECT_EQ( getkmdseason(3484958+1), 8);
    EXPECT_EQ( getkmdseason(8113400), 8);
    EXPECT_EQ( getkmdseason(8113400+1), 0);
    int32_t result1 = komodo_notaries(pubkeys, 3484958+1, 0);
    EXPECT_EQ(result1, 64);
    EXPECT_EQ( my_key(pubkeys[0]), my_key("03955c7999538cee313bf196a7df59db208c651f6a5a1b0eed94732ba753b4f3f4"));
    EXPECT_EQ( my_key(pubkeys[63]), my_key("02f9a7b49282885cd03969f1f5478287497bc8edfceee9eac676053c107c5fcdaf"));

    // try wrong pubkey
    int32_t numnotaries;
    my_key wrong_pk("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344");
    uint8_t pubkey[33];
    wrong_pk.fill(pubkey);
    int32_t result2 = komodo_electednotary(&numnotaries, pubkey, 3484958+1, 0);
    EXPECT_EQ(result2, -1);
    EXPECT_EQ(numnotaries, 64);

    // cleanup
    komodo_notaries_uninit();
}

// season 7 tests for asset chain:
TEST(TestNotary, KomodoNotaries_S7_AS)
{
    uint8_t pubkeys[64][33];

    SelectParams(CBaseChainParams::MAIN);
    komodo_notaries_uninit();
    chainName = assetchain("MYASSET");
    EXPECT_EQ( getacseason(1688132253+1), 8);
    EXPECT_EQ( getacseason(1951328000), 8);
    EXPECT_EQ( getacseason(1951328001), 0);

    int32_t result1 = komodo_notaries(pubkeys, 0, 1688132253+1);
    EXPECT_EQ(result1, 64);
    EXPECT_EQ( my_key(pubkeys[0]), my_key("03955c7999538cee313bf196a7df59db208c651f6a5a1b0eed94732ba753b4f3f4"));
    EXPECT_EQ( my_key(pubkeys[63]), my_key("02f9a7b49282885cd03969f1f5478287497bc8edfceee9eac676053c107c5fcdaf"));

    // try wrong pubkey
    int32_t numnotaries;
    my_key wrong_pk("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344");
    uint8_t pubkey[33];
    wrong_pk.fill(pubkey);
    int32_t result2 = komodo_electednotary(&numnotaries, pubkey, 3484958+1, 1688132253+1);
    EXPECT_EQ(result2, -1);
    EXPECT_EQ(numnotaries, 64);

    // cleanup
    komodo_notaries_uninit();
}

TEST(TestNotary, DPoWLitecoinActivationIsHeightGated)
{
    chainName = assetchain("PIRATE");
    EXPECT_FALSE(DPoWLitecoinUpgradeActiveAtHeight(99,100));
    EXPECT_TRUE(DPoWLitecoinUpgradeActiveAtHeight(100,100));
    EXPECT_TRUE(DPoWLitecoinUpgradeActiveAtHeight(101,100));

    chainName = assetchain("LABS");
    EXPECT_FALSE(DPoWLitecoinUpgradeActiveAtHeight(100,100));

    chainName = assetchain("");
    EXPECT_FALSE(DPoWLitecoinUpgradeActiveAtHeight(100,100));
}

TEST(TestNotary, DPoWLitecoinQuorumAndDestinationSwitchAreHeightGated)
{
    chainName = assetchain("PIRATE");

    EXPECT_EQ(DPoWRequiredSigsAtHeight(99,64,100),11);
    EXPECT_EQ(DPoWRequiredSigsAtHeight(100,DPOW_LITECOIN_NOTARY_COUNT,100),DPOW_LITECOIN_REQUIRED_SIGS);
    EXPECT_GT(DPoWRequiredSigsAtHeight(100,DPOW_LITECOIN_NOTARY_COUNT - 1,100),64);

    EXPECT_STREQ(DPoWAssetChainDestAtHeight(99,100),DPOW_ASSETCHAIN_LEGACY_DEST);
    EXPECT_STREQ(DPoWAssetChainDestAtHeight(100,100),DPOW_LITECOIN_DEST);
}

TEST(TestNotary, DPoWLitecoinSignatureChecksSwitchAtActivation)
{
    chainName = assetchain("PIRATE");

    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,2,0x3,DPOW_LITECOIN_NOTARY_COUNT,false,100));
    EXPECT_TRUE(DPoWNotarizationSigsMetAtHeight(100,DPOW_LITECOIN_REQUIRED_SIGS,0x7,DPOW_LITECOIN_NOTARY_COUNT,false,100));
    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,DPOW_LITECOIN_REQUIRED_SIGS,0x7,DPOW_LITECOIN_NOTARY_COUNT - 1,false,100));
    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,DPOW_LITECOIN_REQUIRED_SIGS,0x3,DPOW_LITECOIN_NOTARY_COUNT,false,100));

    EXPECT_FALSE(DPoWVoutSigsMetAtHeight(100,2,DPOW_LITECOIN_NOTARY_COUNT,100));
    EXPECT_TRUE(DPoWVoutSigsMetAtHeight(100,DPOW_LITECOIN_REQUIRED_SIGS,DPOW_LITECOIN_NOTARY_COUNT,100));
    EXPECT_FALSE(DPoWVoutSigsMetAtHeight(100,DPOW_LITECOIN_REQUIRED_SIGS,DPOW_LITECOIN_NOTARY_COUNT - 1,100));

    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(99,6,0x3f,64,false,100));
    EXPECT_TRUE(DPoWNotarizationSigsMetAtHeight(99,7,0x7f,64,false,100));
    EXPECT_FALSE(DPoWVoutSigsMetAtHeight(99,6,64,100));
    EXPECT_TRUE(DPoWVoutSigsMetAtHeight(99,7,64,100));

    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(99999,10,0x3ff,64,false,100000));
    EXPECT_TRUE(DPoWNotarizationSigsMetAtHeight(99999,11,0x7ff,64,false,100000));
    EXPECT_FALSE(DPoWVoutSigsMetAtHeight(99999,10,64,100000));
    EXPECT_TRUE(DPoWVoutSigsMetAtHeight(99999,11,64,100000));
}

TEST(TestNotary, DPoWLitecoinReceiptAndReorgUseActiveChain)
{
    chainName = assetchain("PIRATE");
    DPoWActiveChain testChain;
    uint256 litecoinTxid = uint256S("abcdef");
    CScript script = DPoWReceiptScript(testChain.PrimaryHash2(),2,litecoinTxid);
    uint64_t voutmask = 0;
    int32_t isratification = 0,specialtx = 0,notarizedheight = 0;

    EXPECT_TRUE(komodo_notarization_target_is_active(testChain.PrimaryHash2(),2));
    EXPECT_EQ(komodo_voutupdate(true,&isratification,-1,script.data(),script.size(),3,uint256(),1,1,
            &voutmask,&specialtx,&notarizedheight,0,1,0,1700000003),-2);
    EXPECT_EQ(notarizedheight,2);

    testChain.UseFork();
    EXPECT_FALSE(komodo_notarization_target_is_active(testChain.PrimaryHash2(),2));
    EXPECT_NE(komodo_voutupdate(true,&isratification,-1,script.data(),script.size(),3,uint256(),1,1,
            &voutmask,&specialtx,&notarizedheight,0,1,0,1700000003),-2);
}

TEST(TestNotary, DPoWLitecoinRestartReplayRejectsOrphanedReceipt)
{
    chainName = assetchain("PIRATE");
    IS_KOMODO_NOTARY = false;
    DPoWActiveChain testChain;
    komodo::event_notarized event(3,"LTC");
    event.notarizedheight = 2;
    event.blockhash = testChain.PrimaryHash2();
    event.desttxid = uint256S("abcdef");

    boost::filesystem::path tempDir = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
    boost::filesystem::create_directories(tempDir);
    std::string stateFilename = (tempDir / "komodoevents").string();
    FILE *fp = fopen(stateFilename.c_str(),"wb");
    ASSERT_NE(fp,nullptr);
    ASSERT_GT(write_event(event,fp),0U);
    fclose(fp);

    char symbol[] = "PIRATE";
    char dest[] = "LTC";
    komodo_state restartedState;
    EXPECT_TRUE(komodo_faststateinit(&restartedState,stateFilename.c_str(),symbol,dest));
    EXPECT_EQ(restartedState.NumCheckpoints(),1U);
    EXPECT_EQ(restartedState.LastNotarizedHash(),testChain.PrimaryHash2());

    testChain.UseFork();
    komodo_state reindexedState;
    EXPECT_TRUE(komodo_faststateinit(&reindexedState,stateFilename.c_str(),symbol,dest));
    EXPECT_EQ(reindexedState.NumCheckpoints(),0U);
    boost::filesystem::remove_all(tempDir);
}

TEST(TestNotary, DPoWLitecoinCheckpointStateClearsMissingMoM)
{
    komodo_state state;
    state.SetLastNotarizedMoM(uint256S("feed"));
    state.SetLastNotarizedMoMDepth(100);

    uint256 noMoM;
    noMoM.SetNull();
    komodo_apply_notarization_state(&state,2,uint256S("12"),uint256S("abcdef"),noMoM,0);

    EXPECT_TRUE(state.LastNotarizedMoM().IsNull());
    EXPECT_EQ(state.LastNotarizedMoMDepth(),0);
    EXPECT_EQ(state.LastNotarizedHeight(),2);
}

TEST(TestNotary, DPoWLitecoinNspvAndMinerUseConsensusSignerMask)
{
    chainName = assetchain("PIRATE");
    EXPECT_TRUE(DPoWNotarizationSigsMetAtHeight(100,3,0x7,6,false,100));
    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,2,0x3,6,false,100));
    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,3,0x3,6,false,100));
    EXPECT_FALSE(DPoWNotarizationSigsMetAtHeight(100,3,0x7,5,false,100));
}

TEST(TestNotary, DPoWLitecoinExternalRpcFailureIsDiagnostic)
{
    chainName = assetchain("PIRATE");
    DPoWActiveChain testChain;
    komodo_state state;
    komodo::event_notarized event(3,"LTC");
    event.notarizedheight = 2;
    event.blockhash = testChain.PrimaryHash2();
    event.desttxid = uint256S("abcdef");

    std::string previousUserpass(BTCUSERPASS);
    uint16_t previousPort = DEST_PORT;
    bool previousNotary = IS_KOMODO_NOTARY;
    strcpy(BTCUSERPASS,"unavailable:unavailable");
    DEST_PORT = 1;
    IS_KOMODO_NOTARY = true;

    EXPECT_TRUE(komodo_eventadd_notarized(&state,"PIRATE",3,event));
    EXPECT_EQ(state.NumCheckpoints(),1U);

    strncpy(BTCUSERPASS,previousUserpass.c_str(),sizeof(BTCUSERPASS)-1);
    BTCUSERPASS[sizeof(BTCUSERPASS)-1] = 0;
    DEST_PORT = previousPort;
    IS_KOMODO_NOTARY = previousNotary;
}

TEST(TestNotary, DPoWLitecoinConfirmationHeightIsOneBased)
{
    EXPECT_EQ(komodo_txheight_from_confirmations(100,1),100);
    EXPECT_EQ(komodo_txheight_from_confirmations(100,2),99);
    EXPECT_EQ(komodo_txheight_from_confirmations(100,101),0);
    EXPECT_EQ(komodo_txheight_from_confirmations(100,0),0);
}

} // namespace TestNotary
