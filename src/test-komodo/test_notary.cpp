#include "testutils.h"
#include "chainparams.h"
#include "komodo_notary.h"

#include <gtest/gtest.h>

void undo_init_notaries(); // test helper

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
    ASSETCHAINS_SYMBOL[0] = 0;
    undo_init_notaries();
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
    }
    EXPECT_EQ(height, 180000);
    // at 180000 we start using notaries_elected(komodo_defs.h) instead of Notaries_genesis(komodo_notary.cpp)
    for(;height <= 814000; ++height)
    {
        result = komodo_notaries(pubkeys, height, timestamp);
        EXPECT_EQ(result, 64);
        EXPECT_EQ( getkmdseason(height), 1);
        EXPECT_EQ( my_key(pubkeys[1]), my_key("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344"));
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
    undo_init_notaries();
    strcpy(ASSETCHAINS_SYMBOL, "LABS");
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
    EXPECT_EQ( getacseason(1751328000), 6);
    EXPECT_EQ( getacseason(1751328001), 0);

    // cleanup
    undo_init_notaries();
}

TEST(TestNotary, ElectedNotary)
{
    // exercise the routine that checks to see if a particular public key is a notary at the current height

    // setup
    undo_init_notaries();
    my_key first_era("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344");
    my_key second_era("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efed961");

    int32_t numnotaries;
    uint8_t pubkey[33];
    first_era.fill(pubkey);
    int32_t height = 0;
    uint32_t timestamp = 0;

    // check the KMD chain, first era
    ASSETCHAINS_SYMBOL[0] = 0;
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

} // namespace TestNotary