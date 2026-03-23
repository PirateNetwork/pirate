// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "zcash/address/orchard.hpp"

#include "hash.h"
#include "streams.h"
#include "zcash/NoteEncryption.hpp"
#include <algorithm>
#include <rust/bridge.h>

namespace libzcash
{

const unsigned char ZCASH_ORCHARD_FVFP_PERSONALIZATION[crypto_generichash_blake2b_PERSONALBYTES] =
    {'Z', 'c', 'a', 's', 'h', 'O', 'r', 'c', 'h', 'a', 'r', 'd', 'F', 'V', 'F', 'P'};


//! Orchard
uint256 OrchardPaymentAddress::GetHash() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

OrchardPaymentAddress_FFI_t OrchardPaymentAddress::ToBytes() const
{
    // Data Stream for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // Return Data
    OrchardPaymentAddress_FFI_t address_t;

    // Serialize data
    ss << *this;
    ss >> address_t;

    return address_t;
}

bool OrchardIncomingViewingKey::DeriveAddress(OrchardPaymentAddress* addr, diversifier_t d) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardIncomingViewingKey_FFI_t ivk_t;
    OrchardPaymentAddress_FFI_t address_t;

    // Serialize sending data
    ss << *this;
    ss >> ivk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::ivk_to_address(ivk_t, d, address_t);

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

bool OrchardIncomingViewingKey::DeriveAddressFromIndex(OrchardPaymentAddress* addr, blob88 diversifier_index) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardIncomingViewingKey_FFI_t ivk_t;
    OrchardPaymentAddress_FFI_t address_t;
    uint88_t di_t;

    // Serialize sending data
    ss << *this;
    ss >> ivk_t;
    di_ss << diversifier_index;
    di_ss >> di_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::ivk_to_address_from_index(ivk_t, di_t, address_t);

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

uint256 OrchardFullViewingKey::GetFingerprint() const
{
    CBLAKE2bWriter ss(SER_GETHASH, 0, ZCASH_ORCHARD_FVFP_PERSONALIZATION);
    ss << *this;
    return ss.GetHash();
}

bool OrchardFullViewingKey::DeriveOVK(OrchardOutgoingViewingKey* ovk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardFullViewingKey_FFI_t fvk_t;
    OrchardOutgoingViewingKey_FFI_t ovk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_ovk(fvk_t, ovk_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ovk_t;
        rs >> *ovk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());

    return rustCompleted;
}

bool OrchardFullViewingKey::DeriveOVKinternal(OrchardOutgoingViewingKey* ovk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardFullViewingKey_FFI_t fvk_t;
    OrchardOutgoingViewingKey_FFI_t ovk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_ovk_internal(fvk_t, ovk_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ovk_t;
        rs >> *ovk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());

    return rustCompleted;
}

bool OrchardFullViewingKey::DeriveIVK(OrchardIncomingViewingKey* ivk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardFullViewingKey_FFI_t fvk_t;
    OrchardIncomingViewingKey_FFI_t ivk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_ivk(fvk_t, ivk_t);

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

bool OrchardFullViewingKey::DeriveIVKinternal(OrchardIncomingViewingKey* ivk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardFullViewingKey_FFI_t fvk_t;
    OrchardIncomingViewingKey_FFI_t ivk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_ivk_internal(fvk_t, ivk_t);

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

bool OrchardFullViewingKey::DeriveDefaultAddress(OrchardPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_default_address(fvk_t, address_t);

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

bool OrchardFullViewingKey::DeriveDefaultAddressInternal(OrchardPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::fvk_to_default_address_internal(fvk_t, address_t);

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

bool OrchardFullViewingKey::DeriveAddress(OrchardPaymentAddress* addr, diversifier_t diversifier) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_keys::fvk_to_address(fvk_t, diversifier, address_t);

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

bool OrchardFullViewingKey::DeriveAddressInternal(OrchardPaymentAddress* addr, diversifier_t diversifier) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_keys::fvk_to_address_internal(fvk_t, diversifier, address_t);

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

bool OrchardFullViewingKey::DeriveAddressFromIndex(OrchardPaymentAddress* addr, blob88 diversifier_index) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION); // diversifier stream

    // Tranfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;
    uint88_t di_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;
    di_ss << diversifier_index;
    di_ss >> di_t;

    // Call rust FFI
    rustCompleted = orchard_keys::fvk_to_address_from_index(fvk_t, di_t, address_t);

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

bool OrchardFullViewingKey::DeriveAddressFromIndexInternal(OrchardPaymentAddress* addr, blob88 diversifier_index) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream
    CDataStream di_ss(SER_NETWORK, PROTOCOL_VERSION); // diversifier stream

    // Tranfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardFullViewingKey_FFI_t fvk_t;
    uint88_t di_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;
    di_ss << diversifier_index;
    di_ss >> di_t;

    // Call rust FFI
    rustCompleted = orchard_keys::fvk_to_address_from_index_internal(fvk_t, di_t, address_t);

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

std::optional<OrchardSpendingKey> OrchardSpendingKey::random()
{
    while (true) {
        auto bytes = random_uint256();
        uint256_t sk_bytes;
        std::copy(bytes.begin(), bytes.end(), sk_bytes.begin());
        if (orchard_keys::sk_is_valid(sk_bytes)) {
            return OrchardSpendingKey(bytes);
        }
    }
}

bool OrchardSpendingKey::IsValid()
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream

    // Tranfer Data
    OrchardSpendingKey_FFI_t sk_t;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // rust result
    bool rustCompleted;

    // Call rust FFI
    rustCompleted = orchard_keys::sk_is_valid(sk_t);

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());

    // return results
    return rustCompleted;
}

bool OrchardSpendingKey::DeriveFVK(OrchardFullViewingKey* fvk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Transfer Data
    OrchardFullViewingKey_FFI_t fvk_t;
    OrchardSpendingKey_FFI_t sk_t;
    OrchardIncomingViewingKey_FFI_t ivk_t;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    rustCompleted = orchard_keys::sk_to_fvk(sk_t, fvk_t);

    // Validate: the derived IVK must be non-null (guards against degenerate scalar combinations).
    if (rustCompleted) {
        rustCompleted = orchard_keys::fvk_to_ivk(fvk_t, ivk_t);
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
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());

    return rustCompleted;
}

bool OrchardSpendingKey::DeriveDefaultAddress(OrchardPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardSpendingKey_FFI_t sk_t;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::sk_to_default_address(sk_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());

    return rustCompleted;
}

bool OrchardSpendingKey::DeriveDefaultAddressInternal(OrchardPaymentAddress* addr) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    OrchardPaymentAddress_FFI_t address_t;
    OrchardSpendingKey_FFI_t sk_t;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    bool rustCompleted = orchard_keys::sk_to_default_address_internal(sk_t, address_t);

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> *addr;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());

    return rustCompleted;
}

} // namespace libzcash
