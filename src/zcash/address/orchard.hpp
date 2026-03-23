// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZC_ADDRESS_ORCHARD_H_
#define ZC_ADDRESS_ORCHARD_H_

#include "serialize.h"
#include "uint256.h"
#include "zcash/Zcash.h"

namespace libzcash
{

//! Orchard Payment Address.
//! Holds an 11-byte diversifier (d) and a 256-bit diversified transmission key (pk_d).
//! Together they uniquely identify a shielded destination for receiving Orchard notes.
class OrchardPaymentAddress
{
public:
    diversifier_t d;   //!< 11-byte diversifier: selects a distinct address from the same IVK.
    uint256 pk_d;      //!< Diversified transmission key: derived from the IVK and diversifier.

    OrchardPaymentAddress() : d(), pk_d() {}
    OrchardPaymentAddress(diversifier_t d, uint256 pk_d) : d(d), pk_d(pk_d) {}

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
    OrchardPaymentAddress_FFI_t ToBytes() const;

    friend inline bool operator==(const OrchardPaymentAddress& a, const OrchardPaymentAddress& b)
    {
        return a.d == b.d && a.pk_d == b.pk_d;
    }
    friend inline bool operator!=(const OrchardPaymentAddress& a, const OrchardPaymentAddress& b)
    {
        return a.d != b.d || a.pk_d != b.pk_d;
    }
    friend inline bool operator<(const OrchardPaymentAddress& a, const OrchardPaymentAddress& b)
    {
        return (a.d < b.d ||
                (a.d == b.d && a.pk_d < b.pk_d));
    }
};

//! Orchard Incoming Viewing Key.
//! Holds the diversifier key (dk) and scalar incoming viewing key (ivk).
//! Used to detect and decrypt incoming notes without spending authority.
class OrchardIncomingViewingKey
{
public:
    uint256 dk;  //!< Diversifier key: used to derive diversifiers from indices.
    uint256 ivk; //!< Incoming viewing key scalar: used to derive payment addresses and decrypt notes.

    OrchardIncomingViewingKey() : dk(), ivk() {}
    OrchardIncomingViewingKey(uint256 dk, uint256 ivk) : dk(dk), ivk(ivk) {}

    // Derives a payment address from the IVK and diversifier into *addr.
    // Returns true on success, false if the diversifier is invalid.
    bool DeriveAddress(OrchardPaymentAddress* addr, diversifier_t d) const;

    // Derives a payment address from the IVK and diversifier index into *addr.
    // Returns true on success, false if the index maps to an invalid diversifier.
    bool DeriveAddressFromIndex(OrchardPaymentAddress* addr, blob88 diversifier_index) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(dk);
        READWRITE(ivk);
    }

    friend inline bool operator==(const OrchardIncomingViewingKey& a, const OrchardIncomingViewingKey& b)
    {
        return a.dk == b.dk && a.ivk == b.ivk;
    }
    friend inline bool operator!=(const OrchardIncomingViewingKey& a, const OrchardIncomingViewingKey& b)
    {
        return a.dk != b.dk || a.ivk != b.ivk;
    }
    friend inline bool operator<(const OrchardIncomingViewingKey& a, const OrchardIncomingViewingKey& b)
    {
        return (a.dk < b.dk || (a.dk == b.dk && a.ivk < b.ivk));
    }
};

//! Orchard Outgoing Viewing Key.
//! Holds the 32-byte scalar ovk used to decrypt outgoing notes.
//! Enables the sender to recover sent-note data without full spending authority.
class OrchardOutgoingViewingKey
{
public:
    uint256 ovk; //!< Outgoing viewing key scalar: used to decrypt outgoing notes.

    OrchardOutgoingViewingKey() : ovk() {}
    OrchardOutgoingViewingKey(uint256 ovk) : ovk(ovk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ovk);
    }

    friend inline bool operator==(const OrchardOutgoingViewingKey& a, const OrchardOutgoingViewingKey& b)
    {
        return a.ovk == b.ovk;
    }
    friend inline bool operator!=(const OrchardOutgoingViewingKey& a, const OrchardOutgoingViewingKey& b)
    {
        return a.ovk != b.ovk;
    }
    friend inline bool operator<(const OrchardOutgoingViewingKey& a, const OrchardOutgoingViewingKey& b)
    {
        return (a.ovk < b.ovk);
    }
};

//! Orchard Full Viewing Key.
//! Holds the authorization key (ak), nullifier key (nk), and randomized incoming viewing key scalar (rivk).
//! Sufficient to detect, decrypt, and scan both incoming and outgoing Orchard notes,
//! but insufficient to spend them.
class OrchardFullViewingKey
{
public:
    uint256 ak;   //!< Authorization key: public key corresponding to the spending key; used in spend authorization.
    uint256 nk;   //!< Nullifier key: used to compute note nullifiers.
    uint256 rivk; //!< Randomized incoming viewing key scalar: used to derive scoped IVKs.
    bool internal = false; //!< Key scope flag: false = external chain, true = internal chain.

    OrchardFullViewingKey() : ak(), nk(), rivk() {}
    OrchardFullViewingKey(uint256 ak, uint256 nk, uint256 rivk) : ak(ak), nk(nk), rivk(rivk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ak);
        READWRITE(nk);
        READWRITE(rivk);
    }

    //! Get the fingerprint of this full viewing key (as defined in ZIP 32).
    uint256 GetFingerprint() const;

    // Derives the external outgoing viewing key from the FVK into *ovk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveOVK(OrchardOutgoingViewingKey* ovk) const;

    // Derives the internal outgoing viewing key from the FVK into *ovk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveOVKinternal(OrchardOutgoingViewingKey* ovk) const;

    // Derives the external incoming viewing key from the FVK into *ivk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveIVK(OrchardIncomingViewingKey* ivk) const;

    // Derives the internal incoming viewing key from the FVK into *ivk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveIVKinternal(OrchardIncomingViewingKey* ivk) const;

    // Derives the default payment address from the FVK into *addr.
    // Returns true on success, false if the FFI call fails.
    bool DeriveDefaultAddress(OrchardPaymentAddress* addr) const;

    // Derives the internal default payment address from the FVK into *addr.
    // Returns true on success, false if the FFI call fails.
    bool DeriveDefaultAddressInternal(OrchardPaymentAddress* addr) const;

    // Derives the address for a given diversifier from this full viewing key (external chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddress(OrchardPaymentAddress* addr, diversifier_t diversifier) const;

    // Derives the address for a given diversifier from this full viewing key (internal chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddressInternal(OrchardPaymentAddress* addr, diversifier_t diversifier) const;

    // Derives the address for a given diversifier index from this full viewing key (external chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddressFromIndex(OrchardPaymentAddress* addr, blob88 diversifier_index) const;

    // Derives the address for a given diversifier index from this full viewing key (internal chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddressFromIndexInternal(OrchardPaymentAddress* addr, blob88 diversifier_index) const;

    friend inline bool operator==(const OrchardFullViewingKey& a, const OrchardFullViewingKey& b)
    {
        return a.ak == b.ak && a.nk == b.nk && a.rivk == b.rivk;
    }
    friend inline bool operator<(const OrchardFullViewingKey& a, const OrchardFullViewingKey& b)
    {
        return (a.ak < b.ak ||
                (a.ak == b.ak && a.nk < b.nk) ||
                (a.ak == b.ak && a.nk == b.nk && a.rivk < b.rivk));
    }
};


//! Orchard Spending Key.
//! A raw 256-bit scalar from which all other Orchard key material is derived.
//! Use DeriveFVK() to obtain the full viewing key, and its Derive* methods for further derivation.
class OrchardSpendingKey
{
public:
    uint256 sk; //!< Raw spending key scalar: the root secret from which all key material is derived.

    OrchardSpendingKey() : sk() {}
    OrchardSpendingKey(uint256 sk) : sk(sk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(sk);
    }

    //! Generates and returns a random valid Orchard spending key, or empty if generation fails.
    std::optional<OrchardSpendingKey> random();

    //! Returns true if the spending key bytes represent a valid Orchard scalar.
    bool IsValid();

    // Derives the full viewing key from this spending key into *fvk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveFVK(OrchardFullViewingKey* fvk) const;

    // Derives the default external payment address from this spending key into *addr.
    // Returns true on success, false if the FFI call fails.
    bool DeriveDefaultAddress(OrchardPaymentAddress* addr) const;

    // Derives the default internal payment address from this spending key into *addr.
    // Returns true on success, false if the FFI call fails.
    bool DeriveDefaultAddressInternal(OrchardPaymentAddress* addr) const;

    friend inline bool operator==(const OrchardSpendingKey& a, const OrchardSpendingKey& b)
    {
        return a.sk == b.sk;
    }
    friend inline bool operator<(const OrchardSpendingKey& a, const OrchardSpendingKey& b)
    {
        return (a.sk < b.sk);
    }
};

} // namespace libzcash

#endif // ZC_ADDRESS_ORCHARD_H_
