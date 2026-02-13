// Copyright (c) 2016-2024 The Zcash developers
// Copyright (c) 2018-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * @file Note.cpp
 * @brief Shielded note operations for Sprout, Sapling, and Orchard
 *
 * Implements note creation, encryption, decryption, and commitment computation.
 * Sapling cmu/rcm are cached from Rust decryption to avoid expensive recomputation.
 */

#include "Note.hpp"
#include "prf.h"
#include "crypto/sha256.h"
#include "consensus/consensus.h"

#include "random.h"
#include "version.h"
#include "streams.h"

#include "zcash/util.h"
#include "librustzcash.h"
#include "rust/bridge.h"

using namespace libzcash;

//==============================================================================
// Sprout Note Implementation
//==============================================================================

/**
 * @brief Default constructor - generates random values for testing
 * Creates a Sprout note with random paying key, nullifier seed, and commitment randomness
 */
SproutNote::SproutNote() {
    a_pk = random_uint256();
    rho = random_uint256();
    r = random_uint256();
}

/**
 * @brief Compute the note commitment for a Sprout note
 * @return The SHA256 commitment to this note's contents
 *
 * Sprout commitments use SHA256 over the discriminant, paying key,
 * value, nullifier seed, and randomness.
 */
uint256 SproutNote::cm() const {
    unsigned char discriminant = 0xb0;

    CSHA256 hasher;
    hasher.Write(&discriminant, 1);
    hasher.Write(a_pk.begin(), 32);

    auto value_vec = convertIntToVectorLE(value_);

    hasher.Write(&value_vec[0], value_vec.size());
    hasher.Write(rho.begin(), 32);
    hasher.Write(r.begin(), 32);

    uint256 result;
    hasher.Finalize(result.begin());

    return result;
}

/**
 * @brief Compute the nullifier for a Sprout note
 * @param a_sk The spending key used to compute the nullifier
 * @return The unique nullifier for this note
 *
 * The nullifier is computed using a PRF over the spending key and rho.
 * Once published, a nullifier prevents double-spending of the note.
 */
uint256 SproutNote::nullifier(const SproutSpendingKey& a_sk) const {
    return PRF_nf(a_sk, rho);
}

//==============================================================================
// Sprout Note Plaintext Implementation
//==============================================================================

/**
 * @brief Construct plaintext from a Sprout note and memo
 * @param note The note to convert to plaintext
 * @param memo The memo to attach (512 bytes)
 */
SproutNotePlaintext::SproutNotePlaintext(
    const SproutNote& note,
    std::array<unsigned char, ZC_MEMO_SIZE> memo) : BaseNotePlaintext(note, memo)
{
    rho = note.rho;
    r = note.r;
}

/**
 * @brief Reconstruct a Sprout note from plaintext
 * @param addr The payment address to receive the note
 * @return A SproutNote with the plaintext's value and randomness
 */
SproutNote SproutNotePlaintext::note(const SproutPaymentAddress& addr) const
{
    return SproutNote(addr.a_pk, value_, rho, r);
}

SproutNotePlaintext SproutNotePlaintext::decrypt(const ZCNoteDecryption& decryptor,
                                     const ZCNoteDecryption::Ciphertext& ciphertext,
                                     const uint256& ephemeralKey,
                                     const uint256& h_sig,
                                     unsigned char nonce
                                    )
{
    auto plaintext = decryptor.decrypt(ciphertext, ephemeralKey, h_sig, nonce);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << plaintext;

    SproutNotePlaintext ret;
    ss >> ret;

    assert(ss.size() == 0);

    return ret;
}

ZCNoteEncryption::Ciphertext SproutNotePlaintext::encrypt(ZCNoteEncryption& encryptor,
                                                    const uint256& pk_enc
                                                   ) const
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << (*this);

    ZCNoteEncryption::Plaintext pt;

    assert(pt.size() == ss.size());

    memcpy(&pt[0], &ss[0], pt.size());

    return encryptor.encrypt(pk_enc, pt);
}

//==============================================================================
// Sapling Note Implementation
//==============================================================================

/**
 * @brief Return cached note commitment from Rust decryption
 * @return The cached commitment, or std::nullopt if not set
 *
 * This value is populated during decryption and cached to avoid expensive
 * recomputation (group operations on BLS12-381 curve).
 */
std::optional<uint256> SaplingNote::cmu() const {
    return cached_cmu;
}

/**
 * @brief Compute the nullifier for a Sapling note
 * @param vk The full viewing key used to compute the nullifier
 * @param position The note's position in the commitment tree
 * @return The unique nullifier, or std::nullopt on failure
 *
 * Calls sapling::compute_nullifier to perform the nullifier computation using the
 * note's diversifier, pk_d, value, cached rcm, and the viewing key components.
 */
std::optional<uint256> SaplingNote::nullifier(const SaplingFullViewingKey& vk, const uint64_t position) const
{
    auto ak = vk.ak;
    auto nk = vk.nk;

    uint256 result;
    uint256 rcm_tmp = rcm();
    if (!sapling::compute_nullifier(
            d,
            reinterpret_cast<const std::array<unsigned char, 32>&>(pk_d),
            value(),
            reinterpret_cast<const std::array<unsigned char, 32>&>(rcm_tmp),
            reinterpret_cast<const std::array<unsigned char, 32>&>(ak),
            reinterpret_cast<const std::array<unsigned char, 32>&>(nk),
            position,
            reinterpret_cast<std::array<unsigned char, 32>&>(result)
    ))
    {
        return std::nullopt;
    }

    return result;
}

/**
 * @brief Return cached randomness from Rust decryption
 * @return The cached rcm value, or zero if not set
 *
 * Like cmu(), this value is populated during decryption and cached.
 * Should always be set when note is created from a decrypted plaintext.
 */
uint256 SaplingNote::rcm() const {
    // Return cached value populated by Rust decryption
    // This should always be set when note is created from decrypted plaintext
    return cached_rcm.value_or(uint256());
}

//==============================================================================
// Sapling Note Plaintext Implementation
//==============================================================================

/**
 * @brief Construct plaintext from a Sapling note and memo
 * @param note The note to convert to plaintext
 * @param memo The memo to attach (512 bytes)
 *
 * Determines the appropriate leadbyte based on ZIP 212 status.
 */
SaplingNotePlaintext::SaplingNotePlaintext(
    const SaplingNote& note,
    std::array<unsigned char, ZC_MEMO_SIZE> memo) : BaseNotePlaintext(note, memo)
{
    d = note.d;
    rseed = note.rseed;
    if (note.get_zip_212_enabled() == libzcash::Zip212Enabled::AfterZip212) {
        leadbyte = 0x02;
    } else {
        leadbyte = 0x01;
    }
}

/**
 * @brief Reconstruct a Sapling note from plaintext
 * @param ivk The incoming viewing key to derive the payment address
 * @return A SaplingNote with cached cmu/rcm, or std::nullopt on failure
 *
 * Transfers the cached cmu and rcm values from plaintext to the note,
 * enabling the note to return these values without recomputation.
 */
std::optional<SaplingNote> SaplingNotePlaintext::note(const SaplingIncomingViewingKey& ivk) const
{
    auto addr = ivk.address(d);
    if (addr) {
        Zip212Enabled zip_212_enabled = Zip212Enabled::BeforeZip212;
        if (leadbyte != 0x01) {
            zip_212_enabled = Zip212Enabled::AfterZip212;
        };
        auto tmp = SaplingNote(d, addr.value().pk_d, value_, rseed, zip_212_enabled);
        
        // Transfer cached cmu and rcm from Rust decryption if available
        if (cmu_) {
            tmp.set_cached_cmu(cmu_.value());
        }
        if (rcm_) {
            tmp.set_cached_rcm(rcm_.value());
        }
        
        return tmp;
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Return cached randomness from decryption
 * @return The cached rcm value, or zero if not set
 *
 * This value is populated during AttemptDecryptSaplingOutput.
 */
uint256 SaplingNotePlaintext::rcm() const {
    // Return cached value populated by Rust decryption
    // This should always be set when plaintext is created from decryption
    return rcm_.value_or(uint256());
}

/**
 * @brief Attempt to decrypt a Sapling output using an incoming viewing key
 * @param output The Sapling output to decrypt
 * @param ivk The incoming viewing key to use for decryption
 * @return Decrypted plaintext with cached cmu/rcm, or std::nullopt on failure
 *
 * Decryption flow:
 * 1. Convert ivk (uint256) to std::array for CXX bridge
 * 2. Call Rust try_decrypt_output_ivk with output parameters
 * 3. Rust writes decrypted data directly into temporary arrays
 * 4. Copy results to plaintext struct, including cached cmu/rcm
 * 5. Return populated plaintext
 *
 * All cryptographic operations are performed by Rust for security.
 */
std::optional<SaplingNotePlaintext> SaplingNotePlaintext::AttemptDecryptSaplingOutput(
    const sapling::Output& output,
    const SaplingIncomingViewingKey& ivk
)
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    std::array<unsigned char, 32> ivk_t;
    std::array<unsigned char, 11> diversifier_ret;
    std::array<unsigned char, 32> pk_d_ret;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_ret;
    std::array<unsigned char, 32> rseed_ret;
    unsigned char leadbyte_ret;
    std::array<unsigned char, 32> cmu_ret;
    std::array<unsigned char, 32> rcm_ret;

    // Convert ivk to array
    std::copy(ivk.begin(), ivk.end(), ivk_t.begin());

    // Construct the plaintext from the decrypted data
    SaplingNotePlaintext ret;
    ret.pk_d = uint256();
    ret.cmu_ = uint256();
    ret.rcm_ = uint256();

    // Call the Rust method on the Output object (it's a member function via CXX bridge)
    if(!output.try_decrypt_output_ivk(
        ivk_t,
        ret.value_,
        diversifier_ret,
        pk_d_ret,
        memo_ret,
        rseed_ret,
        leadbyte_ret,
        cmu_ret,
        rcm_ret)) {
            return std::nullopt;
    }
    
    // Copy returned data to plaintext
    std::copy(diversifier_ret.begin(), diversifier_ret.end(), ret.d.begin());
    std::copy(pk_d_ret.begin(), pk_d_ret.end(), ret.pk_d.value().begin());
    ret.memo_ = memo_ret;
    std::copy(rseed_ret.begin(), rseed_ret.end(), ret.rseed.begin());
    ret.leadbyte = leadbyte_ret;
    std::copy(cmu_ret.begin(), cmu_ret.end(), ret.cmu_.value().begin());
    std::copy(rcm_ret.begin(), rcm_ret.end(), ret.rcm_.value().begin());

    return ret;
}

std::optional<SaplingNotePlaintext> SaplingNotePlaintext::AttemptDecryptSaplingOutput(
    const sapling::Output& output,
    const uint256& ovk
)
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    std::array<unsigned char, 32> ovk_t;
    std::array<unsigned char, 11> diversifier_ret;
    std::array<unsigned char, 32> pk_d_ret;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_ret;
    std::array<unsigned char, 32> rseed_ret;
    unsigned char leadbyte_ret;
    std::array<unsigned char, 32> cmu_ret;
    std::array<unsigned char, 32> rcm_ret;

    // Convert ovk to array
    std::copy(ovk.begin(), ovk.end(), ovk_t.begin());

    // Construct the plaintext from the decrypted data
    SaplingNotePlaintext ret;
    ret.pk_d = uint256();
    ret.cmu_ = uint256();
    ret.rcm_ = uint256();

    // Call the Rust method on the Output object (it's a member function via CXX bridge)
    if(!output.try_decrypt_output_ovk(
        ovk_t,
        ret.value_,
        diversifier_ret,
        pk_d_ret,
        memo_ret,
        rseed_ret,
        leadbyte_ret,
        cmu_ret,
        rcm_ret)) {
            return std::nullopt;
    }
    
    // Copy returned data to plaintext
    std::copy(diversifier_ret.begin(), diversifier_ret.end(), ret.d.begin());
    std::copy(pk_d_ret.begin(), pk_d_ret.end(), ret.pk_d.value().begin());
    ret.memo_ = memo_ret;
    std::copy(rseed_ret.begin(), rseed_ret.end(), ret.rseed.begin());
    ret.leadbyte = leadbyte_ret;
    std::copy(cmu_ret.begin(), cmu_ret.end(), ret.cmu_.value().begin());
    std::copy(rcm_ret.begin(), rcm_ret.end(), ret.rcm_.value().begin());

    return ret;
}

/**
 * @brief Compute nullifier directly from an encrypted Sapling output
 * @param output The encrypted Sapling output
 * @param vk The full viewing key used to decrypt and compute the nullifier
 * @param position The note's position in the commitment tree
 * @return The unique nullifier, or std::nullopt on failure
 *
 * This method decrypts the output and computes the nullifier in one call,
 * using the bridge to call the Rust Output::compute_nullifier method.
 */
std::optional<uint256> SaplingNotePlaintext::ComputeNullifierFromOutput(
    const sapling::Output& output,
    const SaplingFullViewingKey& vk,
    uint64_t position
)
{
    std::array<unsigned char, 32> ivk_arr;
    std::array<unsigned char, 32> ak_arr;
    std::array<unsigned char, 32> nk_arr;
    std::array<unsigned char, 32> result_arr;

    // Convert viewing key components to arrays
    std::copy(vk.ak.begin(), vk.ak.end(), ak_arr.begin());
    std::copy(vk.nk.begin(), vk.nk.end(), nk_arr.begin());
    
    // Derive IVK from ak and nk
    librustzcash_crh_ivk(ak_arr.data(), nk_arr.data(), ivk_arr.data());

    // Call Rust method on Output to compute nullifier
    if (!output.compute_nullifier(ivk_arr, ak_arr, nk_arr, position, result_arr)) {
        return std::nullopt;
    }

    // Convert result to uint256
    uint256 result;
    std::copy(result_arr.begin(), result_arr.end(), result.begin());
    
    return result;
}

//==============================================================================
// Orchard Note Implementation
//==============================================================================

/**
 * @brief Compute the nullifier for an Orchard note
 * @param fvk The full viewing key used to compute the nullifier
 * @return The unique nullifier, or std::nullopt on failure
 *
 * Uses CDataStream to serialize the viewing key and address for the
 * Rust bridge, then calls orchard::compute_nullifier to compute the nullifier.
 */
std::optional<uint256> OrchardNote::nullifier(const libzcash::OrchardFullViewingKeyPirate& fvk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream as(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::OrchardFullViewingKey_t fvk_t;
    libzcash::OrchardPaymentAddress_t address_t;
    uint256 nullifier_t;

    // Serialize sending data
    ss << fvk;
    ss >> fvk_t;

    as << address;
    as >> address_t;

    if (!orchard::compute_nullifier(
          fvk_t,
          address_t,
          value_,
          reinterpret_cast<const std::array<unsigned char, 32>&>(rho_),
          reinterpret_cast<const std::array<unsigned char, 32>&>(rseed_),
          reinterpret_cast<std::array<unsigned char, 32>&>(nullifier_t))) {
                return std::nullopt;
    }

    return nullifier_t;
}

//==============================================================================
// Orchard Note Plaintext Implementation
//==============================================================================

/**
 * @brief Attempt to decrypt an Orchard action using an incoming viewing key
 * @param action The Orchard action to decrypt
 * @param ivk The incoming viewing key to use for decryption
 * @return Decrypted plaintext, or std::nullopt on failure
 *
 * All cryptographic operations delegated to Rust try_orchard_decrypt_action_ivk.
 * Payment address is deserialized from array format after decryption.
 */
std::optional<OrchardNotePlaintext> OrchardNotePlaintext::AttemptDecryptOrchardAction(
    const orchard_bundle::Action* action,
    const libzcash::OrchardIncomingViewingKeyPirate ivk
)
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::OrchardIncomingViewingKey_t ivk_t;
    libzcash::OrchardPaymentAddress_t address_t;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
    uint64_t value_t;
    uint256 rho_t;
    uint256 rseed_t;

    // Serialize sending data
    ss << ivk;
    ss >> ivk_t;

    if(!try_orchard_decrypt_action_ivk(
        action->as_ptr(),
        ivk_t.begin(),
        &value_t,
        address_t.begin(),
        memo_t.begin(),
        rho_t.begin(),
        rseed_t.begin())) {
          return std::nullopt;
    }

    // Deserialize returned data
    libzcash::OrchardPaymentAddressPirate address_r;
    rs << address_t;
    rs >> address_r;

    OrchardNotePlaintext ret = OrchardNotePlaintext(
        value_t, address_r, memo_t, rho_t, rseed_t, 
        std::nullopt, uint256::FromRawBytes(action->cmx()));

    return ret;
}

/**
 * @brief Attempt to decrypt an Orchard action using an outgoing viewing key
 * @param action The Orchard action to decrypt
 * @param ovk The outgoing viewing key to use for decryption
 * @return Decrypted plaintext, or std::nullopt on failure
 *
 * Similar to IVK decryption but uses outgoing viewing key.
 * Allows sender to decrypt their own sent notes.
 */
std::optional<OrchardNotePlaintext> OrchardNotePlaintext::AttemptDecryptOrchardAction(
    const orchard_bundle::Action* action,
    const libzcash::OrchardOutgoingViewingKey ovk
)
{
    // Datastreams for serialization
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::OrchardPaymentAddress_t address_t;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
    uint64_t value_t;
    uint256 rho_t;
    uint256 rseed_t;

    if(!try_orchard_decrypt_action_ovk(
        action->as_ptr(),
        ovk.ovk.begin(),
        &value_t,
        address_t.begin(),
        memo_t.begin(),
        rho_t.begin(),
        rseed_t.begin())) {
          return std::nullopt;
    }

    // Deserialize returned data
    libzcash::OrchardPaymentAddressPirate address_r;
    rs << address_t;
    rs >> address_r;

    OrchardNotePlaintext ret = OrchardNotePlaintext(
        value_t, address_r, memo_t, rho_t, rseed_t, 
        std::nullopt, uint256::FromRawBytes(action->cmx()));

    return ret;
}

/**
 * @brief Compute nullifier directly from an encrypted Orchard action
 * @param action The encrypted Orchard action
 * @param fvk The full viewing key used to decrypt and compute the nullifier
 * @return The unique nullifier, or std::nullopt on failure
 *
 * This method decrypts the action and computes the nullifier in one call,
 * using the bridge to call the Rust Action::compute_nullifier method.
 * Unlike Sapling, Orchard does not require a position parameter.
 */
std::optional<uint256> OrchardNotePlaintext::ComputeNullifierFromAction(
    const orchard_bundle::Action& action,
    const libzcash::OrchardFullViewingKeyPirate& fvk
)
{
    // Serialize IVK and FVK for bridge call
    CDataStream ivk_stream(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream fvk_stream(SER_NETWORK, PROTOCOL_VERSION);
    
    libzcash::OrchardIncomingViewingKey_t ivk_arr;
    libzcash::OrchardFullViewingKey_t fvk_arr;
    std::array<unsigned char, 32> result_arr;
    
    // Get IVK from FVK
    auto ivk_opt = fvk.GetIVK();
    if (!ivk_opt) {
        return std::nullopt;
    }
    
    ivk_stream << ivk_opt.value();
    ivk_stream >> ivk_arr;
    
    fvk_stream << fvk;
    fvk_stream >> fvk_arr;
    
    // Call Rust method on Action to compute nullifier
    if (!action.compute_nullifier(ivk_arr, fvk_arr, result_arr)) {
        return std::nullopt;
    }

    // Convert result to uint256
    uint256 result;
    std::copy(result_arr.begin(), result_arr.end(), result.begin());
    
    return result;
}

/**
 * @brief Reconstruct an Orchard note from plaintext
 * @return An OrchardNote with the plaintext's values
 */
std::optional<OrchardNote> OrchardNotePlaintext::note() const
{
    return OrchardNote(address, value_, rho, rseed, cmx);
}
