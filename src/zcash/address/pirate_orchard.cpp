// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "zcash/address/pirate_orchard.hpp"

#include "hash.h"
#include "streams.h"
#include "zcash/NoteEncryption.hpp"

namespace libzcash
{

const unsigned char ZCASH_ORCHARH_FVFP_PERSONALIZATION[crypto_generichash_blake2b_PERSONALBYTES] =
    {'Z', 'c', 'a', 's', 'h', 'O', 'r', 'c', 'h', 'a', 'r', 'd', 'F', 'V', 'F', 'P'};


//! Orchard
uint256 OrchardPaymentAddressPirate::GetHash() const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << *this;
    return Hash(ss.begin(), ss.end());
}

OrchardPaymentAddress_t OrchardPaymentAddressPirate::ToBytes() const
{
    // Data Stream for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // Return Data
    OrchardPaymentAddress_t address_t;

    // Serialize data
    ss << *this;
    ss >> address_t;

    return address_t;
}

uint256 OrchardFullViewingKeyPirate::GetFingerprint() const
{
    CBLAKE2bWriter ss(SER_GETHASH, 0, ZCASH_ORCHARH_FVFP_PERSONALIZATION);
    ss << *this;
    return ss.GetHash();
}

std::optional<OrchardOutgoingViewingKey> OrchardFullViewingKeyPirate::GetOVK() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardIncomingViewingKey_t ovk_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardOutgoingViewingKey ovk;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_ovk(fvk_t.begin(), ovk_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ovk_t;
        rs >> ovk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return ovk;
    }

    return std::nullopt;
}

std::optional<OrchardOutgoingViewingKey> OrchardFullViewingKeyPirate::GetOVKinternal() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardIncomingViewingKey_t ovk_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardOutgoingViewingKey ovk;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_ovk_internal(fvk_t.begin(), ovk_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ovk_t;
        rs >> ovk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return ovk;
    }

    return std::nullopt;
}

std::optional<OrchardIncomingViewingKeyPirate> OrchardFullViewingKeyPirate::GetIVK() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardIncomingViewingKey_t ivk_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardIncomingViewingKeyPirate ivk;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_ivk(fvk_t.begin(), ivk_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ivk_t;
        rs >> ivk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return ivk;
    }

    return std::nullopt;
}

std::optional<OrchardIncomingViewingKeyPirate> OrchardFullViewingKeyPirate::GetIVKinternal() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardIncomingViewingKey_t ivk_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardIncomingViewingKeyPirate ivk;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_ivk_internal(fvk_t.begin(), ivk_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << ivk_t;
        rs >> ivk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return ivk;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardFullViewingKeyPirate::GetDefaultAddress() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_default_address(fvk_t.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardFullViewingKeyPirate::GetDefaultAddressInternal() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_default_address_internal(fvk_t.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardFullViewingKeyPirate::GetAddress(blob88 diversifier) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_address(fvk_t.begin(), diversifier.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardFullViewingKeyPirate::GetAddressInternal(blob88 diversifier) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardFullViewingKey_t fvk_t;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> fvk_t;

    // Call rust FFI
    rustCompleted = orchard_fvk_to_address_internal(fvk_t.begin(), diversifier.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

std::optional<OrchardSpendingKeyPirate> OrchardSpendingKeyPirate::random()
{
    while (true) {
        auto bytes = random_uint256();
        if (orchard_sk_is_valid(bytes.begin())) {
            return OrchardSpendingKeyPirate(bytes);
        }
    }
}

bool OrchardSpendingKeyPirate::IsValid()
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream

    // Tranfer Data
    OrchardSpendingKey_t sk_t;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // rust result
    bool rustCompleted;

    // Call rust FFI
    rustCompleted = orchard_sk_is_valid(sk_t.begin());

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());

    // return results
    return rustCompleted;
}

std::optional<OrchardFullViewingKeyPirate> OrchardSpendingKeyPirate::GetFVK() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardFullViewingKey_t fvk_t;
    OrchardSpendingKey_t sk_t;

    // Return Type
    OrchardFullViewingKeyPirate fvk;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    rustCompleted = orchard_sk_to_fvk(sk_t.begin(), fvk_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << fvk_t;
        rs >> fvk;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(fvk_t.data(), fvk_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());

    // Return data
    if (rustCompleted) {
        return fvk;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardSpendingKeyPirate::GetDefaultAddress() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardSpendingKey_t sk_t;
    ;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    rustCompleted = orchard_sk_to_default_address(sk_t.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

std::optional<OrchardPaymentAddressPirate> OrchardSpendingKeyPirate::GetDefaultAddressInternal() const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION); // sending stream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION); // returning stream

    // Tranfer Data
    OrchardPaymentAddress_t address_t;
    OrchardSpendingKey_t sk_t;
    ;

    // Return Type
    OrchardPaymentAddressPirate address;

    // rust result
    bool rustCompleted;

    // Serialize sending data
    ss << *this;
    ss >> sk_t;

    // Call rust FFI
    rustCompleted = orchard_sk_to_default_address_internal(sk_t.begin(), address_t.begin());

    // Deserialize rust result on success
    if (rustCompleted) {
        rs << address_t;
        rs >> address;
    }

    // Cleanse the memory of the transfer and serialization objects
    memory_cleanse(ss.data(), ss.size());
    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(address_t.data(), address_t.size());
    memory_cleanse(sk_t.data(), sk_t.size());

    // Return data
    if (rustCompleted) {
        return address;
    }

    return std::nullopt;
}

} // namespace libzcash
