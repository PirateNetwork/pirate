// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZC_ADDRESS_PIRATE_ORCHARD_H_
#define ZC_ADDRESS_PIRATE_ORCHARD_H_

#include "serialize.h"
#include "uint256.h"
#include "zcash/Zcash.h"
#include <rust/orchard/pirate_keys.h>

namespace libzcash
{

const size_t SerializedOrchardPaymentAddressSize = 43;
const size_t SerializedOrchardOutgoingKeySize = 32;
const size_t SerializedOrchardIncomingViewingKeySize = 64;
const size_t SerializedOrchardFullViewingKeySize = 96;
const size_t SerializedOrchardExtendedFullViewingKeySize = 137;
const size_t SerializedOrchardSpendingKeySize = 32;
const size_t SerializedOrchardExtendedSpendingKeySize = 73;

typedef std::array<unsigned char, SerializedOrchardPaymentAddressSize> OrchardPaymentAddress_t;
typedef std::array<unsigned char, SerializedOrchardIncomingViewingKeySize> OrchardIncomingViewingKey_t;
typedef std::array<unsigned char, SerializedOrchardFullViewingKeySize> OrchardFullViewingKey_t;
typedef std::array<unsigned char, SerializedOrchardSpendingKeySize> OrchardSpendingKey_t;
typedef std::array<unsigned char, SerializedOrchardExtendedSpendingKeySize> OrchardExtendedSpendingKey_t;
typedef std::array<unsigned char, ZC_DIVERSIFIER_SIZE> diversifier_t;

//! Orchard functions.
class OrchardPaymentAddressPirate
{
public:
    diversifier_t d;
    uint256 pk_d;

    OrchardPaymentAddressPirate() : d(), pk_d() {}
    OrchardPaymentAddressPirate(diversifier_t d, uint256 pk_d) : d(d), pk_d(pk_d) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(d);
        READWRITE(pk_d);
    }

    //! Get the 256-bit SHA256d hash of this payment address.
    uint256 GetHash() const;

    // Get serialized bytes of an Orchard Address
    OrchardPaymentAddress_t ToBytes() const;

    friend inline bool operator==(const OrchardPaymentAddressPirate& a, const OrchardPaymentAddressPirate& b)
    {
        return a.d == b.d && a.pk_d == b.pk_d;
    }
    friend inline bool operator<(const OrchardPaymentAddressPirate& a, const OrchardPaymentAddressPirate& b)
    {
        return (a.d < b.d ||
                (a.d == b.d && a.pk_d < b.pk_d));
    }
};

class OrchardOutgoingViewingKey : public uint256
{
public:
    OrchardOutgoingViewingKey() : uint256() {}
    OrchardOutgoingViewingKey(uint256 ovk) : uint256(ovk) {}
};

class OrchardIncomingViewingKeyPirate
{
public:
    uint256 dk;
    uint256 ivk;

    OrchardIncomingViewingKeyPirate() : dk(), ivk() {}
    OrchardIncomingViewingKeyPirate(uint256 dk, uint256 ivk) : dk(dk), ivk(ivk) {}

    // Can pass in diversifier for Sapling addr
    std::optional<OrchardPaymentAddressPirate> address(diversifier_t d) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(dk);
        READWRITE(ivk);
    }

    friend inline bool operator==(const OrchardIncomingViewingKeyPirate& a, const OrchardIncomingViewingKeyPirate& b)
    {
        return a.dk == b.dk && a.ivk == b.ivk;
    }
    friend inline bool operator<(const OrchardIncomingViewingKeyPirate& a, const OrchardIncomingViewingKeyPirate& b)
    {
        return (a.dk < b.dk || (a.dk == b.dk && a.ivk < b.ivk));
    }
};

class OrchardFullViewingKeyPirate
{
public:
    uint256 ak;
    uint256 nk;
    uint256 rivk;
    bool internal; // key scope

    OrchardFullViewingKeyPirate() : ak(), nk(), rivk() {}
    OrchardFullViewingKeyPirate(uint256 ak, uint256 nk, uint256 rivk) : ak(ak), nk(nk), rivk(rivk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ak);
        READWRITE(nk);
        READWRITE(rivk);
    }

    // Get the finger print of the Fullviewing key
    uint256 GetFingerprint() const;

    // Get the outgoing viewing keys associated to a full viewing key
    std::optional<OrchardOutgoingViewingKey> GetOVK() const;
    std::optional<OrchardOutgoingViewingKey> GetOVKinternal() const;

    // Get the incoming viewing keys associated to a full viewing key
    std::optional<OrchardIncomingViewingKeyPirate> GetIVK() const;
    std::optional<OrchardIncomingViewingKeyPirate> GetIVKinternal() const;

    // Get the default addresses associated to a full viewing key
    std::optional<OrchardPaymentAddressPirate> GetDefaultAddress() const;
    std::optional<OrchardPaymentAddressPirate> GetDefaultAddressInternal() const;

    // Get the addresses of a given diversifier associated to a full viewing key
    std::optional<OrchardPaymentAddressPirate> GetAddress(blob88 diversifier) const;
    std::optional<OrchardPaymentAddressPirate> GetAddressInternal(blob88 diversifier) const;

    friend inline bool operator==(const OrchardFullViewingKeyPirate& a, const OrchardFullViewingKeyPirate& b)
    {
        return a.ak == b.ak && a.nk == b.nk && a.rivk == b.rivk;
    }
    friend inline bool operator<(const OrchardFullViewingKeyPirate& a, const OrchardFullViewingKeyPirate& b)
    {
        return (a.ak < b.ak ||
                (a.ak == b.ak && a.nk < b.nk) ||
                (a.ak == b.ak && a.nk == b.nk && a.rivk < b.rivk));
    }
};


class OrchardSpendingKeyPirate
{
public:
    uint256 sk;

    OrchardSpendingKeyPirate() : sk() {}
    OrchardSpendingKeyPirate(uint256 sk) : sk(sk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(sk);
    }

    // Generate a random spendingkey
    std::optional<OrchardSpendingKeyPirate> random();

    // Check sk byte validity
    bool IsValid();

    // Get Full Viewing Key
    std::optional<OrchardFullViewingKeyPirate> GetFVK() const;

    // Get Default addresses assoicated to a spending key
    std::optional<OrchardPaymentAddressPirate> GetDefaultAddress() const;
    std::optional<OrchardPaymentAddressPirate> GetDefaultAddressInternal() const;

    friend inline bool operator==(const OrchardSpendingKeyPirate& a, const OrchardSpendingKeyPirate& b)
    {
        return a.sk == b.sk;
    }
    friend inline bool operator<(const OrchardSpendingKeyPirate& a, const OrchardSpendingKeyPirate& b)
    {
        return (a.sk < b.sk);
    }
};

} // namespace libzcash

#endif // ZC_ADDRESS_PIRATE_ORCHARD_H_
