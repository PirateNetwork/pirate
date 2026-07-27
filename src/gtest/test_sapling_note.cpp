// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "zcash/Address.hpp"
#include "zcash/Note.hpp"
#include "zcash/address/zip32.h"

#include "amount.h"
#include "random.h"
#include "librustzcash.h"

#include <array>

// Basic sanity checks on Sapling/Ironwood Note construction: SaplingNote.Random
// checks notes built with the same diversifier/pk_d differ in value/rcm as
// expected, and notes derived from distinct spending keys get distinct
// diversifiers and pk_d. IronwoodNote.Random is the Ironwood counterpart -
// this pool didn't exist when SaplingNote.Random was originally written, so
// there was never an Ironwood version of this check anywhere in the suite;
// IronwoodNote is otherwise only ever touched indirectly, via the full
// TransactionBuilder/paymentdisclosure pipeline in other test files.

using namespace libzcash;


// The 96-byte expsk (ask/nsk/ovk only) doesn't carry dk, so it can't derive the ZIP 32
// default diversifier (needs the dk-keyed FF1 permutation) - treat the random spending
// key's seed as a ZIP 32 master seed instead (bip39Enabled=false so it's used directly)
// to get a real dk and a properly key-specific default address.
static libzcash::SaplingPaymentAddress DefaultAddressFromRandomKey(const SaplingSpendingKey& sk) {
    RawHDSeed rawSeed(sk.begin(), sk.end());
    return libzcash::SaplingExtendedSpendingKey::Master(HDSeed(rawSeed), false).DefaultAddress();
}

TEST(SaplingNote, Random)
{
    // Test creating random notes using the same spending key
    auto randSk1 = SaplingSpendingKey::random();
    libzcash::SaplingPaymentAddress address = DefaultAddressFromRandomKey(randSk1);
    SaplingNote note1(address.d, address.pk_d, GetRand(MAX_MONEY), GetRandHash(), Zip212Enabled::BeforeZip212);
    SaplingNote note2(address.d, address.pk_d, GetRand(MAX_MONEY), GetRandHash(), Zip212Enabled::BeforeZip212);

    ASSERT_EQ(note1.d, note2.d);
    ASSERT_EQ(note1.pk_d, note2.pk_d);
    ASSERT_NE(note1.value(), note2.value());
    // Not comparing rcm() here: it only returns a value cached during Rust
    // decryption (see SaplingNote::rcm()'s doc comment) and is always zero for a
    // note built directly via this constructor, regardless of rseed.

    // Test diversifier and pk_d are not the same for different spending keys
    auto randSk3 = SaplingSpendingKey::random();
    libzcash::SaplingPaymentAddress addr3 = DefaultAddressFromRandomKey(randSk3);
    SaplingNote note3(addr3.d, addr3.pk_d, GetRand(MAX_MONEY), GetRandHash(), Zip212Enabled::BeforeZip212);
    ASSERT_NE(note1.d, note3.d);
    ASSERT_NE(note1.pk_d, note3.pk_d);
}

static libzcash::IronwoodPaymentAddress DefaultIronwoodAddressFromRandomSeed(unsigned char seedByte) {
    std::vector<unsigned char, secure_allocator<unsigned char>> rawSeed(32, seedByte);
    HDSeed seed(rawSeed);
    auto sk = libzcash::IronwoodExtendedSpendingKeyPirate::Master(seed, false);
    libzcash::IronwoodPaymentAddress addr;
    EXPECT_TRUE(sk.sk.DeriveDefaultAddress(&addr));
    return addr;
}

TEST(IronwoodNote, Random)
{
    // Test creating random notes at the same address
    auto address1 = DefaultIronwoodAddressFromRandomSeed(1);
    IronwoodNote note1(address1, GetRand(MAX_MONEY), GetRandHash(), GetRandHash(), GetRandHash());
    IronwoodNote note2(address1, GetRand(MAX_MONEY), GetRandHash(), GetRandHash(), GetRandHash());

    ASSERT_TRUE(note1.address == note2.address);
    ASSERT_NE(note1.value(), note2.value());
    ASSERT_NE(note1.rho(), note2.rho());
    ASSERT_NE(note1.rseed(), note2.rseed());

    // Test diversifier and pk_d are not the same for a different address
    auto address3 = DefaultIronwoodAddressFromRandomSeed(3);
    IronwoodNote note3(address3, GetRand(MAX_MONEY), GetRandHash(), GetRandHash(), GetRandHash());
    ASSERT_NE(note1.address.d, note3.address.d);
    ASSERT_NE(note1.address.pk_d, note3.address.pk_d);
}
