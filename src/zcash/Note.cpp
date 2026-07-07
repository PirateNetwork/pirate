// Copyright (c) 2016-2024 The Zcash developers
// Copyright (c) 2018-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * @file Note.cpp
 * @brief Shielded note operations for Sprout, Sapling, and Ironwood
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
#include "support/cleanse.h"

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
            reinterpret_cast<const uint256_t&>(pk_d),
            value(),
            reinterpret_cast<const uint256_t&>(rcm_tmp),
            reinterpret_cast<const uint256_t&>(ak),
            reinterpret_cast<const uint256_t&>(nk),
            position,
            reinterpret_cast<uint256_t&>(result)
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
    SaplingPaymentAddress addr;
    if (ivk.DeriveAddress(&addr, d)) {
        Zip212Enabled zip_212_enabled = Zip212Enabled::BeforeZip212;
        if (leadbyte != 0x01) {
            zip_212_enabled = Zip212Enabled::AfterZip212;
        };
        auto tmp = SaplingNote(d, addr.pk_d, value_, rseed, zip_212_enabled);
        
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
    uint256_t ivk_t;
    std::array<unsigned char, 11> diversifier_ret;
    uint256_t pk_d_ret;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_ret;
    uint256_t rseed_ret;
    unsigned char leadbyte_ret;
    uint256_t cmu_ret;
    uint256_t rcm_ret;

    // Serialize ivk into transfer array
    ss << ivk;
    ss >> ivk_t;
    memory_cleanse(ss.data(), ss.size());

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
    
    // Deserialize returned data via CDataStream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
    rs << pk_d_ret << rseed_ret << cmu_ret << rcm_ret;

    ret.d = diversifier_ret;
    rs >> ret.pk_d.value();
    ret.memo_ = memo_ret;
    rs >> ret.rseed;
    ret.leadbyte = leadbyte_ret;
    rs >> ret.cmu_.value() >> ret.rcm_.value();

    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ivk_t.data(), ivk_t.size());
    memory_cleanse(pk_d_ret.data(), pk_d_ret.size());
    memory_cleanse(rseed_ret.data(), rseed_ret.size());
    memory_cleanse(cmu_ret.data(), cmu_ret.size());
    memory_cleanse(rcm_ret.data(), rcm_ret.size());

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
    uint256_t ovk_t;
    std::array<unsigned char, 11> diversifier_ret;
    uint256_t pk_d_ret;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_ret;
    uint256_t rseed_ret;
    unsigned char leadbyte_ret;
    uint256_t cmu_ret;
    uint256_t rcm_ret;

    // Serialize ovk into transfer array
    ss << ovk;
    ss >> ovk_t;
    memory_cleanse(ss.data(), ss.size());

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
    
    // Deserialize returned data via CDataStream
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);
    rs << pk_d_ret << rseed_ret << cmu_ret << rcm_ret;

    ret.d = diversifier_ret;
    rs >> ret.pk_d.value();
    ret.memo_ = memo_ret;
    rs >> ret.rseed;
    ret.leadbyte = leadbyte_ret;
    rs >> ret.cmu_.value() >> ret.rcm_.value();

    memory_cleanse(rs.data(), rs.size());
    memory_cleanse(ovk_t.data(), ovk_t.size());
    memory_cleanse(pk_d_ret.data(), pk_d_ret.size());
    memory_cleanse(rseed_ret.data(), rseed_ret.size());
    memory_cleanse(cmu_ret.data(), cmu_ret.size());
    memory_cleanse(rcm_ret.data(), rcm_ret.size());

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
    const SaplingIncomingViewingKey& ivk,
    const uint256& ak,
    const uint256& nk,
    uint64_t position
)
{
    uint256_t ivk_t;
    uint256_t ak_t;
    uint256_t nk_t;
    uint256_t result_t;

    // Use the IVK that decrypted the note directly (may be external or internal).
    std::copy(ivk.ivk.begin(), ivk.ivk.end(), ivk_t.begin());

    // ak is always the same for both scopes; nk must match the scope of ivk.
    // Caller is responsible for passing nk_internal when ivk is the internal IVK.
    std::copy(ak.begin(), ak.end(), ak_t.begin());
    std::copy(nk.begin(), nk.end(), nk_t.begin());

    // Call Rust method on Output to compute nullifier
    if (!output.compute_nullifier(ivk_t, ak_t, nk_t, position, result_t)) {
        return std::nullopt;
    }

    // Convert result to uint256
    uint256 result;
    std::copy(result_t.begin(), result_t.end(), result.begin());
    
    return result;
}

//==============================================================================
// Ironwood Note Implementation
//==============================================================================

/**
 * @brief Compute the nullifier for an Ironwood note
 * @param fvk The full viewing key used to compute the nullifier
 * @return The unique nullifier, or std::nullopt on failure
 *
 * Uses CDataStream to serialize the viewing key and address for the
 * Rust bridge, then calls ironwood::compute_nullifier to compute the nullifier.
 */
std::optional<uint256> IronwoodNote::nullifier(const libzcash::IronwoodFullViewingKey& fvk) const
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream as(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::IronwoodFullViewingKey_FFI_t fvk_t;
    libzcash::IronwoodPaymentAddress_FFI_t address_t;
    uint256 nullifier_t;

    // Serialize sending data
    ss << fvk;
    ss >> fvk_t;

    as << address;
    as >> address_t;

    if (!ironwood::compute_nullifier(
          fvk_t,
          address_t,
          value_,
          reinterpret_cast<const uint256_t&>(rho_),
          reinterpret_cast<const uint256_t&>(rseed_),
          reinterpret_cast<uint256_t&>(nullifier_t))) {
                return std::nullopt;
    }

    return nullifier_t;
}

//==============================================================================
// Ironwood Note Plaintext Implementation
//==============================================================================

/**
 * @brief Attempt to decrypt an Ironwood action using an incoming viewing key
 * @param action The Ironwood action to decrypt
 * @param ivk The incoming viewing key to use for decryption
 * @return Decrypted plaintext, or std::nullopt on failure
 *
 * All cryptographic operations delegated to Rust try_ironwood_decrypt_action_ivk.
 * Payment address is deserialized from array format after decryption.
 */
std::optional<IronwoodNotePlaintext> IronwoodNotePlaintext::AttemptDecryptIronwoodAction(
    const ironwood_bundle::Action* action,
    const libzcash::IronwoodIncomingViewingKey ivk
)
{
    // Datastreams for serialization
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::IronwoodIncomingViewingKey_FFI_t ivk_t;
    libzcash::IronwoodPaymentAddress_FFI_t address_t;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
    uint64_t value_t;
    uint256 rho_t;
    uint256 rseed_t;

    // Serialize sending data
    ss << ivk;
    ss >> ivk_t;

    if(!try_ironwood_decrypt_action_ivk(
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
    libzcash::IronwoodPaymentAddress address_r;
    rs << address_t;
    rs >> address_r;

    IronwoodNotePlaintext ret = IronwoodNotePlaintext(
        value_t, address_r, memo_t, rho_t, rseed_t, 
        std::nullopt, uint256::FromRawBytes(action->cmx()));

    return ret;
}

/**
 * @brief Attempt to decrypt an Ironwood action using an outgoing viewing key
 * @param action The Ironwood action to decrypt
 * @param ovk The outgoing viewing key to use for decryption
 * @return Decrypted plaintext, or std::nullopt on failure
 *
 * Similar to IVK decryption but uses outgoing viewing key.
 * Allows sender to decrypt their own sent notes.
 */
std::optional<IronwoodNotePlaintext> IronwoodNotePlaintext::AttemptDecryptIronwoodAction(
    const ironwood_bundle::Action* action,
    const libzcash::IronwoodOutgoingViewingKey ovk
)
{
    // Datastreams for serialization
    CDataStream rs(SER_NETWORK, PROTOCOL_VERSION);

    // Transfer Data
    libzcash::IronwoodPaymentAddress_FFI_t address_t;
    std::array<unsigned char, ZC_MEMO_SIZE> memo_t;
    uint64_t value_t;
    uint256 rho_t;
    uint256 rseed_t;

    if(!try_ironwood_decrypt_action_ovk(
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
    libzcash::IronwoodPaymentAddress address_r;
    rs << address_t;
    rs >> address_r;

    IronwoodNotePlaintext ret = IronwoodNotePlaintext(
        value_t, address_r, memo_t, rho_t, rseed_t, 
        std::nullopt, uint256::FromRawBytes(action->cmx()));

    return ret;
}

/**
 * @brief Compute nullifier directly from an encrypted Ironwood action
 * @param action The encrypted Ironwood action
 * @param fvk The full viewing key used to decrypt and compute the nullifier
 * @return The unique nullifier, or std::nullopt on failure
 *
 * This method decrypts the action and computes the nullifier in one call,
 * using the bridge to call the Rust Action::compute_nullifier method.
 * Unlike Sapling, Ironwood does not require a position parameter.
 */
std::optional<uint256> IronwoodNotePlaintext::ComputeNullifierFromAction(
    const ironwood_bundle::Action& action,
    const libzcash::IronwoodFullViewingKey& fvk
)
{
    // FVK is the single source of truth — IVK is derived internally in Rust.
    CDataStream fvk_stream(SER_NETWORK, PROTOCOL_VERSION);
    libzcash::IronwoodFullViewingKey_FFI_t fvk_t;
    uint256_t result_t;
    
    fvk_stream << fvk;
    fvk_stream >> fvk_t;
    
    // Call Rust method on Action to compute nullifier (IVK derived from FVK internally)
    if (!action.compute_nullifier(fvk_t, result_t)) {
        return std::nullopt;
    }

    uint256 result;
    std::copy(result_t.begin(), result_t.end(), result.begin());
    
    return result;
}

/**
 * @brief Reconstruct an Ironwood note from plaintext
 * @return An IronwoodNote with the plaintext's values
 */
std::optional<IronwoodNote> IronwoodNotePlaintext::note() const
{
    return IronwoodNote(address, value_, rho, rseed, cmx);
}
