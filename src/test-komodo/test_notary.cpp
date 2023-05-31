#include "testutils.h"
#include "chainparams.h"
#include "komodo_notary.h"

#include <gtest/gtest.h>

//void undo_init_notaries(); // test helper
void komodo_notaries_uninit();


namespace TestNotary
{

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

} // namespace TestNotary