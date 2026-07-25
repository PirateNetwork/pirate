#include <gtest/gtest.h>

#include "zcash/Address.hpp"
#include "zcash/Note.hpp"

#include "amount.h"
#include "random.h"
#include "librustzcash.h"

#include <array>

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
