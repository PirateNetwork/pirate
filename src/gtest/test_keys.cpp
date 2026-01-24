#include <chainparams.h>
#include <key_io.h>
#include <zcash/Address.hpp>
#include <zcash/address/zip32.h>
#include "consensus/upgrades.h"

#include <gtest/gtest.h>

TEST(Keys, EncodeAndDecodeSapling)
{
    SelectParams(CBaseChainParams::MAIN);

    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32);
    HDSeed seed(rawSeed);
    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed);

    for (uint32_t i = 0; i < 100; i++) {
        auto sk = m.Derive(i);
        auto vk = sk.ToXFVK();
        auto addr = sk.DefaultAddress();

        {
            std::string str = EncodeSpendingKey(sk);
            libzcash::SpendingKey spendingkey2 = DecodeSpendingKey(str);
            ASSERT_TRUE(IsValidSpendingKey(spendingkey2));
            auto sk2 = std::get_if<libzcash::SaplingExtendedSpendingKey>(&spendingkey2);
            ASSERT_TRUE(sk2 != nullptr);
            ASSERT_EQ(sk, *sk2);
        }

        {
            std::string str = EncodePaymentAddress(addr);
            libzcash::PaymentAddress paymentaddr2 = DecodePaymentAddress(str);
            ASSERT_TRUE(IsValidPaymentAddress(paymentaddr2));
            auto addr2 = std::get_if<libzcash::SaplingPaymentAddress>(&paymentaddr2);
            ASSERT_TRUE(addr2 != nullptr);
            ASSERT_EQ(addr, *addr2);
        }

        {
            std::string vk_string = EncodeViewingKey(vk);
            libzcash::ViewingKey viewingkey2 = DecodeViewingKey(vk_string);
            ASSERT_TRUE(IsValidViewingKey(viewingkey2));
            auto vk2_ptr = std::get_if<libzcash::SaplingExtendedFullViewingKey>(&viewingkey2);
            ASSERT_TRUE(vk2_ptr != nullptr);
            auto vk2 = *vk2_ptr;
            ASSERT_EQ(vk, vk2);
            ASSERT_EQ(vk.fvk.in_viewing_key(), vk2.fvk.in_viewing_key());
        }
    }
}
