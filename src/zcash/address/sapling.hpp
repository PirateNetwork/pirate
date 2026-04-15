// Copyright (c) 2021-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef ZC_ADDRESS_SAPLING_H_
#define ZC_ADDRESS_SAPLING_H_

#include "uint256.h"
#include "serialize.h"
#include "zcash/Zcash.h"

namespace libzcash
{

//! Sapling Payment Address.
//! Holds an 11-byte diversifier (d) and a 256-bit diversified transmission key (pk_d).
//! Together they uniquely identify a shielded destination for receiving Sapling notes.
class SaplingPaymentAddress
{
public:
    diversifier_t d;   //!< 11-byte diversifier: selects a distinct address from the same IVK.
    uint256 pk_d;      //!< Diversified transmission key: derived from the IVK and diversifier.

    SaplingPaymentAddress() : d(), pk_d() {}
    SaplingPaymentAddress(diversifier_t d, uint256 pk_d) : d(d), pk_d(pk_d) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(d);
        READWRITE(pk_d);
    }

    //! Get the 256-bit SHA256d hash of this payment address.
    uint256 GetHash() const;

    // Get serialized bytes of a Sapling Address
    SaplingPaymentAddress_FFI_t ToBytes() const;

    friend inline bool operator==(const SaplingPaymentAddress& a, const SaplingPaymentAddress& b)
    {
        return a.d == b.d && a.pk_d == b.pk_d;
    }
    friend inline bool operator!=(const SaplingPaymentAddress& a, const SaplingPaymentAddress& b)
    {
        return a.d != b.d || a.pk_d != b.pk_d;
    }
    friend inline bool operator<(const SaplingPaymentAddress& a, const SaplingPaymentAddress& b)
    {
        return (a.d < b.d ||
                (a.d == b.d && a.pk_d < b.pk_d));
    }
};

//! Sapling Incoming Viewing Key.
//! Holds the 32-byte scalar ivk derived via CRH^ivk(ak, nk).
//! Used to detect and decrypt incoming notes without spending authority.
class SaplingIncomingViewingKey
{
public:
    uint256 ivk; //!< Incoming viewing key scalar: used to derive payment addresses and decrypt notes.

    SaplingIncomingViewingKey() : ivk() {}
    SaplingIncomingViewingKey(uint256 ivk) : ivk(ivk) {}

    // Derives a payment address from the IVK and diversifier into *addr.
    // Returns true on success, false if the diversifier is invalid.
    bool DeriveAddress(SaplingPaymentAddress* addr, diversifier_t d) const;

    // Derives a payment address from the IVK and diversifier index into *addr.
    // Returns true on success, false if the index maps to an invalid diversifier.
    bool DeriveAddressFromIndex(SaplingPaymentAddress* addr, blob88 diversifier_index) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ivk);
    }

    friend inline bool operator==(const SaplingIncomingViewingKey& a, const SaplingIncomingViewingKey& b)
    {
        return a.ivk == b.ivk;
    }
    friend inline bool operator!=(const SaplingIncomingViewingKey& a, const SaplingIncomingViewingKey& b)
    {
        return a.ivk != b.ivk;
    }
    friend inline bool operator<(const SaplingIncomingViewingKey& a, const SaplingIncomingViewingKey& b)
    {
        return a.ivk < b.ivk;
    }
};

//! Sapling Outgoing Viewing Key.
//! Holds the 32-byte scalar ovk used to decrypt outgoing notes.
//! Enables the sender to recover sent-note data without full spending authority.
class SaplingOutgoingViewingKey
{
public:
    uint256 ovk; //!< Outgoing viewing key scalar: used to decrypt outgoing notes.

    SaplingOutgoingViewingKey() : ovk() {}
    SaplingOutgoingViewingKey(uint256 ovk) : ovk(ovk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ovk);
    }

    friend inline bool operator==(const SaplingOutgoingViewingKey& a, const SaplingOutgoingViewingKey& b)
    {
        return a.ovk == b.ovk;
    }
    friend inline bool operator!=(const SaplingOutgoingViewingKey& a, const SaplingOutgoingViewingKey& b)
    {
        return a.ovk != b.ovk;
    }
    friend inline bool operator<(const SaplingOutgoingViewingKey& a, const SaplingOutgoingViewingKey& b)
    {
        return (a.ovk < b.ovk);
    }
};

//! Sapling Full Viewing Key.
//! Holds the authorization key (ak), nullifier key (nk), and outgoing viewing key (ovk).
//! Sufficient to detect, decrypt, and scan both incoming and outgoing Sapling notes,
//! but insufficient to spend them.
class SaplingFullViewingKey
{
public:
    uint256 ak;  //!< Authorization key: public key corresponding to ask; used in spend authorization.
    uint256 nk;  //!< Nullifier key: used to compute note nullifiers.
    uint256 ovk; //!< Outgoing viewing key: used to decrypt outgoing notes.

    SaplingFullViewingKey() : ak(), nk(), ovk() {}
    SaplingFullViewingKey(uint256 ak, uint256 nk, uint256 ovk) : ak(ak), nk(nk), ovk(ovk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ak);
        READWRITE(nk);
        READWRITE(ovk);
    }

    //! Get the fingerprint of this full viewing key (as defined in ZIP 32).
    uint256 GetFingerprint() const;

    // Derives the outgoing viewing key from this full viewing key into *out_ovk.
    // In Sapling the OVK is a direct field component; always returns true.
    bool DeriveOVK(SaplingOutgoingViewingKey* out_ovk) const;

    // Derives the incoming viewing key from this full viewing key into *ivk.
    // Returns true on success, false if the FFI call fails.
    bool DeriveIVK(SaplingIncomingViewingKey* ivk) const;

    // Derives the default payment address from the FVK into *addr.
    // Returns true on success, false if the FFI call fails.
    bool DeriveDefaultAddress(SaplingPaymentAddress* addr) const;

    // Derives the address for a given diversifier from this full viewing key (external chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddress(SaplingPaymentAddress* addr, diversifier_t diversifier) const;

    // Derives the address for a given diversifier index from this full viewing key (external chain).
    // Writes result to *addr. Returns true on success, false if the FFI call fails.
    bool DeriveAddressFromIndex(SaplingPaymentAddress* addr, blob88 diversifier_index) const;

    // NOTE: All internal-scope (change) derivations have been moved to
    // SaplingExtendedFullViewingKey, which carries the real dk needed to
    // correctly compute nk_internal via sapling_derive_internal_fvk(fvk, dk).

    friend inline bool operator==(const SaplingFullViewingKey& a, const SaplingFullViewingKey& b)
    {
        return a.ak == b.ak && a.nk == b.nk && a.ovk == b.ovk;
    }
    friend inline bool operator<(const SaplingFullViewingKey& a, const SaplingFullViewingKey& b)
    {
        return (a.ak < b.ak ||
            (a.ak == b.ak && a.nk < b.nk) ||
            (a.ak == b.ak && a.nk == b.nk && a.ovk < b.ovk));
    }
};


//! Sapling Expanded Spending Key.
//! Holds the expanded scalar components (ask, nsk, ovk) derived from a raw spending key.
//! Used as an intermediate representation when deriving the full viewing key or creating spends.
class SaplingExpandedSpendingKey
{
public:
    uint256 ask; //!< Authorization spending key scalar: used to sign spends.
    uint256 nsk; //!< Nullifier private key scalar: used to derive the nullifier key nk.
    uint256 ovk; //!< Outgoing viewing key: passthrough from the spending key.

    SaplingExpandedSpendingKey() : ask(), nsk(), ovk() {}
    SaplingExpandedSpendingKey(uint256 ask, uint256 nsk, uint256 ovk) : ask(ask), nsk(nsk), ovk(ovk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(ask);
        READWRITE(nsk);
        READWRITE(ovk);
    }

    // Derives the full viewing key (ak, nk, ovk) from this expanded spending key into *fvk.
    // Returns true on success, false if ask or nsk are not canonical field elements.
    bool DeriveFVK(SaplingFullViewingKey* fvk) const;

    // Derives the default external payment address from this expanded spending key into *addr.
    // Follows expsk → FVK → IVK (External) → first-valid-raw-diversifier.
    // Returns true on success, false if ask or nsk are not canonical field elements.
    bool DeriveDefaultAddress(SaplingPaymentAddress* addr) const;

    // NOTE: DeriveDefaultAddressInternal has been removed. Internal-scope derivations
    // require the real dk and must use SaplingExtendedSpendingKey::ToXFVK().<method>.

    friend inline bool operator==(const SaplingExpandedSpendingKey& a, const SaplingExpandedSpendingKey& b)
    {
        return a.ask == b.ask && a.nsk == b.nsk && a.ovk == b.ovk;
    }
    friend inline bool operator<(const SaplingExpandedSpendingKey& a, const SaplingExpandedSpendingKey& b)
    {
        return (a.ask < b.ask ||
            (a.ask == b.ask && a.nsk < b.nsk) ||
            (a.ask == b.ask && a.nsk == b.nsk && a.ovk < b.ovk));
    }
};

//! Sapling Spending Key.
//! A raw 256-bit scalar from which all other Sapling key material is derived.
//! Use expanded_spending_key() to obtain ask/nsk/ovk, and its Derive* methods for further derivation.
class SaplingSpendingKey : public uint256 {
public:
    SaplingSpendingKey() : uint256() { }
    SaplingSpendingKey(uint256 sk) : uint256(sk) { }

    //! Generates and returns a random valid Sapling spending key.
    static SaplingSpendingKey random();

    //! Derives the expanded spending key (ask, nsk, ovk) from this spending key.
    SaplingExpandedSpendingKey expanded_spending_key() const;
};

} // namespace libzcash

#endif // ZC_ADDRESS_SAPLING_H
