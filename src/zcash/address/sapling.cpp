// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "zcash/address/sapling.hpp"

#include "hash.h"
#include "streams.h"
#include "support/cleanse.h"
#include "zcash/NoteEncryption.hpp"
#include "zcash/prf.h"
#include <algorithm>
#include <rust/bridge.h>
#include <rust/sapling_keys_bridge.h>

namespace libzcash
{

const unsigned char ZCASH_SAPLING_FVFP_PERSONALIZATION[crypto_generichash_blake2b_PERSONALBYTES] =
      {'Z', 'c', 'a', 's', 'h', 'S', 'a', 'p', 'l', 'i', 'n', 'g', 'F', 'V', 'F', 'P'};


//! Sapling
uint256 SaplingPaymentAddress::GetHash() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

SaplingPaymentAddress_FFI_t SaplingPaymentAddress::ToBytes() const
{
    // Data Stream for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // Return Data
    SaplingPaymentAddress_FFI_t address_t;

    // Serialize data
    ss << *this;
    ss >> address_t;

    return address_t;
}

bool SaplingIncomingViewingKey::DeriveAddress(SaplingPaymentAddress* addr, diversifier_t d) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    SaplingIncomingViewingKey_FFI_t ivk_t;
    SaplingPaymentAddress_FFI_t address_t;

    // Serialize sending data
    ss << *this;
    ss >> ivk_t;

    // Call rust FFI
    bool rustCompleted = sapling_keys::ivk_to_address(ivk_t, d, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());
    memory_cleanse(address_t.data(), address_t.size());

    return rustCompleted;
}

bool SaplingIncomingViewingKey::DeriveAddressFromIndex(SaplingPaymentAddress* addr, blob88 diversifier_index) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    SaplingIncomingViewingKey_FFI_t ivk_t;
    SaplingPaymentAddress_FFI_t address_t;
    uint88_t di_t;

    // Serialize sending data
    ss << *this;
    ss >> ivk_t;
    di_ss << diversifier_index;
    di_ss >> di_t;

    // Call rust FFI
    bool rustCompleted = sapling_keys::ivk_to_address_from_index(ivk_t, di_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(di_ss.data(), di_ss.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(di_t.data(), di_t.size());

    return rustCompleted;
}

uint256 SaplingFullViewingKey::GetFingerprint() const {
    CBLAKE2bWriter ss(SER_GETHASH, 0, ZCASH_SAPLING_FVFP_PERSONALIZATION);
    ss << *this;
    return ss.GetHash();
}

bool SaplingFullViewingKey::DeriveOVK(SaplingOutgoingViewingKey* out_ovk) const {
    // In Sapling the OVK is a direct component of the FVK — no FFI required.
    out_ovk->ovk = ovk;
    return true;
}

bool SaplingFullViewingKey::DeriveIVK(SaplingIncomingViewingKey* ivk) const {
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    SaplingFullViewingKey_FFI_t fvk_t;
    SaplingIncomingViewingKey_FFI_t ivk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = sapling_keys::fvk_to_ivk(fvk_t, ivk_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ivk_t;
        rs >> *ivk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());

    return rustCompleted;
}

bool SaplingFullViewingKey::DeriveDefaultAddress(SaplingPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    SaplingPaymentAddress_FFI_t address_t;
    SaplingFullViewingKey_FFI_t fvk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = sapling_keys::fvk_to_default_address(fvk_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    return rustCompleted;
}

bool SaplingFullViewingKey::DeriveAddress(SaplingPaymentAddress* addr, diversifier_t diversifier) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    SaplingPaymentAddress_FFI_t address_t;
    SaplingFullViewingKey_FFI_t fvk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = sapling_keys::fvk_to_address(fvk_t, diversifier, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    return rustCompleted;
}

bool SaplingFullViewingKey::DeriveAddressFromIndex(SaplingPaymentAddress* addr, blob88 diversifier_index) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION); // diversifier stream

    // Transfer Data
    SaplingPaymentAddress_FFI_t address_t;
    SaplingFullViewingKey_FFI_t fvk_t;
    uint88_t di_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;
    di_ss << diversifier_index;
    di_ss >> di_t;

    // Call rust FFI
    rustCompleted = sapling_keys::fvk_to_address_from_index(fvk_t, di_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(di_ss.data(), di_ss.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(di_t.data(), di_t.size());

    return rustCompleted;
}

bool SaplingExpandedSpendingKey::DeriveFVK(SaplingFullViewingKey* fvk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    SaplingExpandedSpendingKey_FFI_t expsk_t;
    SaplingFullViewingKey_FFI_t fvk_t;
    SaplingIncomingViewingKey_FFI_t ivk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> expsk_t;

    // Call rust FFI
    rustCompleted = sapling_keys::expsk_to_fvk(expsk_t, fvk_t);

    // Validate: the derived IVK must be non-null (guards against degenerate scalar combinations).
    if (rustCompleted) {
        rustCompleted = sapling_keys::fvk_to_ivk(fvk_t, ivk_t);
        if (rustCompleted) {
            uint256 ivk;
            std::copy(ivk_t.begin(), ivk_t.end(), ivk.begin());
            rustCompleted = !ivk.IsNull();
            memory_cleanse(ivk.begin(), ivk.size());
        }
    }

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << fvk_t;
        rs >> *fvk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(expsk_t.data(), expsk_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());

    return rustCompleted;
}

bool SaplingExpandedSpendingKey::DeriveDefaultAddress(SaplingPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    SaplingExpandedSpendingKey_FFI_t expsk_t;
    SaplingPaymentAddress_FFI_t address_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> expsk_t;

    // Call rust FFI
    rustCompleted = sapling_keys::expsk_to_default_address(expsk_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(expsk_t.data(), expsk_t.size());
    memory_cleanse(address_t.data(), address_t.size());

    return rustCompleted;
}

SaplingSpendingKey SaplingSpendingKey::random() {
    while (true) {
        auto sk = SaplingSpendingKey(random_uint256());
        SaplingFullViewingKey fvk;
        if (sk.expanded_spending_key().DeriveFVK(&fvk)) {
            return sk;
        }
    }
}

SaplingExpandedSpendingKey SaplingSpendingKey::expanded_spending_key() const {
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    SaplingSpendingKey_FFI_t sk_t;
    SaplingExpandedSpendingKey_FFI_t expsk_t;

    // Default result
    SaplingExpandedSpendingKey expsk;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    bool rustCompleted = sapling_keys::sk_to_expsk(sk_t, expsk_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << expsk_t;
        rs >> expsk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(sk_t.data(), sk_t.size());
    memory_cleanse(expsk_t.data(), expsk_t.size());

    return expsk;
}

}
