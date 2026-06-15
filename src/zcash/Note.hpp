// Copyright (c) 2016-2024 The Zcash developers
// Copyright (c) 2018-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * @file Note.hpp
 * @brief Shielded note definitions for Sprout, Sapling, and Orchard
 *
 * Defines Note and NotePlaintext classes for all shielded protocols.
 * Sapling cmu/rcm are cached from Rust decryption for performance.
 */

#ifndef ZC_NOTE_H_
#define ZC_NOTE_H_

#include "uint256.h"
#include "Zcash.h"
#include "Address.hpp"
#include "NoteEncryption.hpp"
#include "consensus/params.h"
#include "consensus/consensus.h"
#include "primitives/orchard.h"

#include "rust/orchard/orchard_actions.h"

#include <array>
#include <optional>

// Forward declarations
namespace sapling {
    class Output;
}

namespace libzcash {

//==============================================================================
// Base Note Classes
//==============================================================================

/**
 * @class BaseNote
 * @brief Abstract base class for all note types
 *
 * Provides the common interface for shielded notes across all protocols.
 * All notes contain a value representing the amount of currency.
 */
class BaseNote {
protected:
    uint64_t value_ = 0;
public:
    BaseNote() {}
    BaseNote(uint64_t value) : value_(value) {};
    virtual ~BaseNote() {};

    inline uint64_t value() const { return value_; };
};

/**
 * @class BaseNotePlaintext
 * @brief Abstract base class for all note plaintext types
 *
 * Represents the decrypted content of a shielded note, including the value
 * and memo field. The memo field allows attaching arbitrary data to a note.
 */
class BaseNotePlaintext {
protected:
    uint64_t value_ = 0;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_;
public:
    BaseNotePlaintext() {}
    BaseNotePlaintext(const BaseNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo)
        : value_(note.value()), memo_(memo) {}
    BaseNotePlaintext(const uint64_t value, std::array<unsigned char, ZC_MEMO_SIZE> memo)
        : value_(value), memo_(memo) {}
    virtual ~BaseNotePlaintext() {}

    inline uint64_t value() const { return value_; }
    inline const std::array<unsigned char, ZC_MEMO_SIZE> & memo() const { return memo_; }
};

//==============================================================================
// Sprout Note Classes
//==============================================================================

/**
 * @class SproutNote
 * @brief Sprout protocol shielded note (legacy)
 *
 * Represents a note in the original Zcash Sprout protocol. Sprout notes
 * use the a_pk (paying key), rho (nullifier seed), and r (commitment randomness)
 * to construct commitments and nullifiers.
 *
 * @note Sprout is considered legacy; new applications should use Sapling or Orchard
 */
class SproutNote : public BaseNote {
public:
    uint256 a_pk;
    uint256 rho;
    uint256 r;

    SproutNote(uint256 a_pk, uint64_t value, uint256 rho, uint256 r)
        : BaseNote(value), a_pk(a_pk), rho(rho), r(r) {}

    SproutNote();

    virtual ~SproutNote() {};

    uint256 cm() const;

    uint256 nullifier(const SproutSpendingKey& a_sk) const;
};

class SproutNotePlaintext : public BaseNotePlaintext {
public:
    uint256 rho;
    uint256 r;

    SproutNotePlaintext() {}

    SproutNotePlaintext(const SproutNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo);

    SproutNote note(const SproutPaymentAddress& addr) const;

    virtual ~SproutNotePlaintext() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        unsigned char leadbyte = 0x00;
        READWRITE(leadbyte);

        if (leadbyte != 0x00) {
            throw std::ios_base::failure("lead byte of SproutNotePlaintext is not recognized");
        }

        READWRITE(value_);
        READWRITE(rho);
        READWRITE(r);
        READWRITE(memo_);
    }

    static SproutNotePlaintext decrypt(const ZCNoteDecryption& decryptor,
                                 const ZCNoteDecryption::Ciphertext& ciphertext,
                                 const uint256& ephemeralKey,
                                 const uint256& h_sig,
                                 unsigned char nonce
                                );

    ZCNoteEncryption::Ciphertext encrypt(ZCNoteEncryption& encryptor,
                                         const uint256& pk_enc
                                        ) const;
};

//==============================================================================
// Sapling Note Classes
//==============================================================================

/**
 * @brief Check if a plaintext version leadbyte is valid
 *
 * @param params Consensus parameters
 * @param height Block height
 * @param leadbyte The version byte from the note plaintext (0x01 or 0x02)
 * @return true if leadbyte is valid
 *
 * @note PirateNetwork fork accepts both 0x01 and 0x02 at all heights for compatibility
 */
inline bool plaintext_version_is_valid(const Consensus::Params& params, int height, unsigned char leadbyte) {
    // Modified to always accept both 0x01 and 0x02 lead bytes for compatibility
    // This allows decryption of notes created with either ZIP 212 format regardless of blockchain height
    if (leadbyte != 0x01 && leadbyte != 0x02) {
        // Only reject if leadbyte is neither 0x01 nor 0x02
        return false;
    }
    return true;
};

/**
 * @enum Zip212Enabled
 * @brief Indicates which ZIP 212 format the note uses
 *
 * ZIP 212 changed the note plaintext format. This enum tracks which format
 * a note uses, though PirateNetwork's fork accepts both formats at all heights.
 */
enum class Zip212Enabled {
    BeforeZip212,
    AfterZip212
};

/**
 * @class SaplingNote
 * @brief Sapling protocol shielded note
 *
 * Represents a note in the Sapling shielded protocol. Sapling notes use
 * elliptic curve cryptography for improved performance over Sprout.
 *
 * Architecture: Note commitment (cmu) and randomness (rcm) are cached from
 * Rust decryption to avoid expensive recomputation. These values are populated
 * once during decryption and reused throughout the note's lifecycle.
 *
 * @see SaplingNotePlaintext for the decrypted plaintext representation
 */
class SaplingNote : public BaseNote {
private:
    uint256 rseed;
    friend class SaplingNotePlaintext;
    Zip212Enabled zip_212_enabled;
    mutable std::optional<uint256> cached_cmu; // Cached note commitment from decryption
    mutable std::optional<uint256> cached_rcm; // Cached randomness from decryption
public:
    diversifier_t d;
    uint256 pk_d;

    SaplingNote(diversifier_t d, uint256 pk_d, uint64_t value, uint256 rseed, Zip212Enabled zip_212_enabled)
            : BaseNote(value), d(d), pk_d(pk_d), rseed(rseed), zip_212_enabled(zip_212_enabled) {}

    virtual ~SaplingNote() {};

    std::optional<uint256> cmu() const;
    std::optional<uint256> nullifier(const SaplingFullViewingKey &vk, const uint64_t position) const;
    uint256 rcm() const;

    Zip212Enabled get_zip_212_enabled() const {
        return zip_212_enabled;
    }
    
    // Set cached values from decryption
    void set_cached_cmu(const uint256& cmu_val) const {
        cached_cmu = cmu_val;
    }
    
    void set_cached_rcm(const uint256& rcm_val) const {
        cached_rcm = rcm_val;
    }
};

/**
 * @class SaplingNotePlaintext
 * @brief Decrypted Sapling note plaintext
 *
 * Represents the decrypted content of a Sapling note. Includes all fields
 * necessary to reconstruct the note, plus cached cryptographic values from
 * the decryption process.
 *
 * Decryption flow:
 * 1. AttemptDecryptSaplingOutput() calls Rust decryption
 * 2. Rust returns cmu, rcm along with note data
 * 3. Values are cached in cmu_ and rcm_ fields
 * 4. note() method transfers cached values to SaplingNote
 * 5. SaplingNote returns cached values instead of recomputing
 *
 * @note pk_d is only populated for OVK (outgoing viewing key) decryption
 */
class SaplingNotePlaintext : public BaseNotePlaintext {
private:
    uint256 rseed;
    unsigned char leadbyte;
public:
    diversifier_t d;
    std::optional<uint256> pk_d; // Only populated for OVK decryption
    std::optional<uint256> cmu_; // Note commitment from Rust decryption
    std::optional<uint256> rcm_; // Randomness for commitment from Rust decryption

    SaplingNotePlaintext() {}

    SaplingNotePlaintext(const SaplingNote& note, std::array<unsigned char, ZC_MEMO_SIZE> memo);

    // New Orchard-style decryption methods
    // Note: PirateNetwork's librustzcash fork accepts both 0x01 and 0x02 lead bytes
    // at all heights (ZIP 212 validation is disabled).
    static std::optional<SaplingNotePlaintext> AttemptDecryptSaplingOutput(
        const sapling::Output& output,
        const SaplingIncomingViewingKey& ivk
    );

    static std::optional<SaplingNotePlaintext> AttemptDecryptSaplingOutput(
        const sapling::Output& output,
        const uint256& ovk
    );

    // Compute nullifier directly from an encrypted output.
    // ivk must be the IVK that successfully decrypted the note (external or internal).
    // ak and nk must match the scope of ivk: for internal notes use nk_internal, not nk.
    static std::optional<uint256> ComputeNullifierFromOutput(
        const sapling::Output& output,
        const SaplingIncomingViewingKey& ivk,
        const uint256& ak,
        const uint256& nk,
        uint64_t position
    );

    std::optional<SaplingNote> note(const SaplingIncomingViewingKey& ivk) const;

    virtual ~SaplingNotePlaintext() {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(leadbyte);

        if (leadbyte != 0x01 && leadbyte != 0x02) {
            throw std::ios_base::failure("lead byte of SaplingNotePlaintext is not recognized");
        }

        READWRITE(d);           // 11 bytes
        READWRITE(value_);      // 8 bytes
        READWRITE(rseed);       // 32 bytes
        READWRITE(memo_);       // 512 bytes
    }

    uint256 rcm() const;
    unsigned char get_leadbyte() const {
        return leadbyte;
    }
};

class SaplingOutgoingPlaintext
{
public:
    uint256 pk_d;
    uint256 esk;

    SaplingOutgoingPlaintext() {};

    SaplingOutgoingPlaintext(uint256 pk_d, uint256 esk) : pk_d(pk_d), esk(esk) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pk_d);        // 8 bytes
        READWRITE(esk);         // 8 bytes
    }

};

//==============================================================================
// Orchard Note Classes
//==============================================================================

/**
 * @class OrchardNote
 * @brief Orchard protocol shielded note
 *
 * Represents a note in the Orchard shielded protocol, the latest generation
 * of Zcash privacy technology. Orchard uses the Pallas/Vesta curve cycle
 * and Halo 2 proof system.
 *
 * @see OrchardNotePlaintext for the decrypted plaintext representation
 */
class OrchardNote : public BaseNote {
private:
    uint256 rho_;
    uint256 rseed_;
    uint256 cmx_;
    friend class OrchardNotePlaintext;
public:
    OrchardPaymentAddress address;

    OrchardNote(OrchardPaymentAddress address, uint64_t value, uint256 rho, uint256 rseed, uint256 cmx)
            : BaseNote(value), address(address), rho_(rho), rseed_(rseed), cmx_(cmx) {}

    virtual ~OrchardNote() {};

    uint256 rho() const { return rho_; }
    uint256 rseed() const { return rseed_; }
    uint256 cmx() const { return cmx_; }

    std::optional<uint256> nullifier(const libzcash::OrchardFullViewingKey& fvk) const;
};

/**
 * @class OrchardNotePlaintext
 * @brief Decrypted Orchard note plaintext
 *
 * Represents the decrypted content of an Orchard note. All decryption is
 * performed by Rust implementations for security and performance.
 *
 * @see OrchardNote
 */
class OrchardNotePlaintext : public BaseNotePlaintext {
private:
    libzcash::OrchardPaymentAddress address;
    uint256 rho;
    uint256 rseed;
    std::optional<uint256> nullifier;
    uint256 cmx;
public:

    OrchardNotePlaintext() {}

    OrchardNotePlaintext(
      const CAmount value,
      const libzcash::OrchardPaymentAddress address,
      const std::array<unsigned char, ZC_MEMO_SIZE> memo,
      const uint256 rho,
      const uint256 rseed,
      const std::optional<uint256> nullifier,
      const uint256 cmx)
    : BaseNotePlaintext(value, memo), address(address), rho(rho), rseed(rseed), nullifier(nullifier), cmx(cmx) {}

    virtual ~OrchardNotePlaintext() {}

    libzcash::OrchardPaymentAddress GetAddress() {
        return address;
    };

    // Rust-based decryption methods
    static std::optional<OrchardNotePlaintext> AttemptDecryptOrchardAction(
        const orchard_bundle::Action* action,
        const libzcash::OrchardIncomingViewingKey ivk
    );

    static std::optional<OrchardNotePlaintext> AttemptDecryptOrchardAction(
        const orchard_bundle::Action* action,
        const libzcash::OrchardOutgoingViewingKey ovk
    );

    // Compute nullifier directly from an encrypted action.
    // IVK is derived internally from the FVK — only fvk is needed.
    static std::optional<uint256> ComputeNullifierFromAction(
        const orchard_bundle::Action& action,
        const libzcash::OrchardFullViewingKey& fvk
    );

    std::optional<OrchardNote> note() const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(value_);      // 8 bytes
        READWRITE(address);     // 43 bytes
        READWRITE(memo_);       // 512 bytes
        READWRITE(rho);         // 32 bytes
        READWRITE(rseed);       // 32 bytes
        READWRITE(nullifier);   // 32 bytes
    }

};

}

#endif // ZC_NOTE_H_
