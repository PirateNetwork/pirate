// Copyright (c) 2016-2024 The Zcash developers
// Copyright (c) 2018-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * @file NoteEncryption.hpp
 * @brief Sprout note encryption using Curve25519 + ChaCha20-Poly1305
 *
 * Implements authenticated encryption for Sprout notes with forward secrecy.
 * Uses ephemeral Curve25519 keys, BLAKE2b KDF, and ChaCha20-Poly1305 AEAD.
 * Sprout only - Sapling/Ironwood use Rust implementations.
 */

#ifndef ZC_NOTE_ENCRYPTION_H_
#define ZC_NOTE_ENCRYPTION_H_

#include "uint256.h"
#include "uint252.h"

#include "zcash/Zcash.h"
#include "zcash/Address.hpp"

#include <array>

namespace libzcash {

/**
 * @class NoteEncryption
 * @brief Template class for encrypting Sprout notes
 *
 * Provides authenticated encryption of note plaintexts using ephemeral keypairs
 * and Diffie-Hellman key exchange. Each instance generates a fresh ephemeral
 * keypair and can encrypt up to 255 messages before exhausting the nonce space.
 *
 * Template Parameter:
 * @tparam MLEN The plaintext message length in bytes (ZC_NOTEPLAINTEXT_SIZE for Sprout)
 *
 * Encryption Flow:
 * 1. Generate ephemeral keypair (esk, epk)
 * 2. Perform DH exchange with recipient's pk_enc
 * 3. Derive symmetric key using KDF with transaction context
 * 4. Encrypt plaintext with ChaCha20-Poly1305
 * 5. Increment nonce for next encryption
 *
 * @note The ephemeral public key (epk) must be transmitted with the ciphertext
 *       to allow the recipient to decrypt.
 */
template<size_t MLEN>
class NoteEncryption {
protected:
    enum { CLEN=MLEN+NOTEENCRYPTION_AUTH_BYTES };
    uint256 epk;
    uint256 esk;
    unsigned char nonce;
    uint256 hSig;

public:
    typedef std::array<unsigned char, CLEN> Ciphertext;
    typedef std::array<unsigned char, MLEN> Plaintext;

    /**
     * @brief Construct a note encryptor with transaction context
     * @param hSig Transaction signature hash for key derivation context
     *
     * Generates a fresh ephemeral keypair (esk, epk) for this encryption instance.
     */
    NoteEncryption(uint256 hSig);

    /**
     * @brief Get the ephemeral secret key
     * @return The ephemeral secret key (for payment disclosure)
     */
    uint256 get_esk() {
        return esk;
    }

    /**
     * @brief Get the ephemeral public key
     * @return The ephemeral public key (transmitted with ciphertext)
     */
    uint256 get_epk() {
        return epk;
    }

    /**
     * @brief Encrypt a plaintext message for a recipient
     * @param pk_enc The recipient's public encryption key
     * @param message The plaintext to encrypt (MLEN bytes)
     * @return The authenticated ciphertext (MLEN + 16 bytes)
     *
     * Can be called up to 255 times per instance before nonce space exhaustion.
     * Each call increments the internal nonce counter.
     *
     * @throws std::logic_error if nonce space is exhausted or DH fails
     */
    Ciphertext encrypt(const uint256 &pk_enc,
                       const Plaintext &message
                      );

    /**
     * @brief Generate an encryption secret key from a spending key
     * @param a_sk The Sprout spending key (252 bits)
     * @return The derived encryption secret key (clamped Curve25519 scalar)
     *
     * Derives sk_enc from a_sk using PRF_addr_sk_enc and clamps the result
     * for Curve25519 compatibility.
     */
    static uint256 generate_privkey(const uint252 &a_sk);

    /**
     * @brief Generate an encryption public key from a secret key
     * @param sk_enc The encryption secret key
     * @return The corresponding public key on Curve25519
     *
     * Performs Curve25519 scalar multiplication with the base point.
     *
     * @throws std::logic_error if scalar multiplication fails
     */
    static uint256 generate_pubkey(const uint256 &sk_enc);
};

/**
 * @class NoteDecryption
 * @brief Template class for decrypting Sprout notes
 *
 * Provides authenticated decryption of note ciphertexts using the recipient's
 * long-term encryption keypair and the sender's ephemeral public key.
 *
 * Template Parameter:
 * @tparam MLEN The plaintext message length in bytes
 *
 * Decryption Flow:
 * 1. Perform DH exchange with sender's ephemeral public key (epk)
 * 2. Derive same symmetric key using KDF with transaction context
 * 3. Decrypt and authenticate ciphertext with ChaCha20-Poly1305
 * 4. Return plaintext or throw on authentication failure
 *
 * @note Decryption failure indicates either tampering or wrong decryption key.
 */
template<size_t MLEN>
class NoteDecryption {
protected:
    enum { CLEN=MLEN+NOTEENCRYPTION_AUTH_BYTES };
    uint256 sk_enc;
    uint256 pk_enc;

public:
    typedef std::array<unsigned char, CLEN> Ciphertext;
    typedef std::array<unsigned char, MLEN> Plaintext;

    /**
     * @brief Default constructor (for deserialization)
     */
    NoteDecryption() { }
    
    /**
     * @brief Construct a note decryptor with an encryption secret key
     * @param sk_enc The recipient's encryption secret key
     *
     * Derives the corresponding public key for later use in KDF.
     */
    NoteDecryption(uint256 sk_enc);

    /**
     * @brief Decrypt an encrypted note
     * @param ciphertext The encrypted message (MLEN + 16 bytes)
     * @param epk The sender's ephemeral public key
     * @param hSig Transaction signature hash (must match encryption context)
     * @param nonce The nonce used during encryption (usually 0)
     * @return The decrypted plaintext (MLEN bytes)
     *
     * @throws note_decryption_failed if authentication fails or wrong key
     * @throws std::logic_error if DH exchange fails
     */
    Plaintext decrypt(const Ciphertext &ciphertext,
                      const uint256 &epk,
                      const uint256 &hSig,
                      unsigned char nonce
                     ) const;

    friend inline bool operator==(const NoteDecryption& a, const NoteDecryption& b) {
        return a.sk_enc == b.sk_enc && a.pk_enc == b.pk_enc;
    }
    friend inline bool operator<(const NoteDecryption& a, const NoteDecryption& b) {
        return (a.sk_enc < b.sk_enc ||
                (a.sk_enc == b.sk_enc && a.pk_enc < b.pk_enc));
    }
};

/**
 * @brief Generate a random 256-bit value
 * @return A cryptographically secure random uint256
 *
 * Uses libsodium's randombytes_buf for secure random generation.
 */
uint256 random_uint256();

/**
 * @brief Generate a random 252-bit value
 * @return A cryptographically secure random uint252
 *
 * Generates a random 256-bit value and clears the top 4 bits.
 */
uint252 random_uint252();

/**
 * @class note_decryption_failed
 * @brief Exception thrown when note decryption fails
 *
 * Indicates either:
 * - Authentication tag verification failed (tampering or corruption)
 * - Wrong decryption key used
 * - Invalid ciphertext format
 */
class note_decryption_failed : public std::runtime_error {
public:
    note_decryption_failed() : std::runtime_error("Could not decrypt message") { }
};



/**
 * @class PaymentDisclosureNoteDecryption
 * @brief Extended decryption for payment disclosure proof
 *
 * Extends NoteDecryption to support decryption using the ephemeral secret key (esk)
 * instead of the recipient's secret key. This is used for payment disclosure where
 * the sender proves they created a specific transaction output.
 *
 * Payment Disclosure Flow:
 * 1. Sender reveals esk for a specific output
 * 2. Auditor uses esk + recipient's pk_enc to decrypt
 * 3. Decryption success proves sender created the output
 *
 * @tparam MLEN The plaintext message length in bytes
 *
 * @note This does not compromise the recipient's privacy - the auditor learns
 *       the output content but not the recipient's secret key.
 */
template<size_t MLEN>
class PaymentDisclosureNoteDecryption : public NoteDecryption<MLEN> {
protected:
public:
    enum { CLEN=MLEN+NOTEENCRYPTION_AUTH_BYTES };
    typedef std::array<unsigned char, CLEN> Ciphertext;
    typedef std::array<unsigned char, MLEN> Plaintext;

    PaymentDisclosureNoteDecryption() : NoteDecryption<MLEN>() {}
    PaymentDisclosureNoteDecryption(uint256 sk_enc) : NoteDecryption<MLEN>(sk_enc) {}

    /**
     * @brief Decrypt using ephemeral secret key (for payment disclosure)
     * @param ciphertext The encrypted message
     * @param pk_enc The recipient's public encryption key
     * @param esk The revealed ephemeral secret key
     * @param hSig Transaction signature hash
     * @param nonce The nonce used during encryption
     * @return The decrypted plaintext
     *
     * Allows third-party verification that the sender created a specific output
     * by revealing esk without compromising recipient privacy.
     *
     * @throws note_decryption_failed if authentication fails
     * @throws std::logic_error if DH exchange fails
     */
    Plaintext decryptWithEsk(
        const Ciphertext &ciphertext,
        const uint256 &pk_enc,
        const uint256 &esk,
        const uint256 &hSig,
        unsigned char nonce
        ) const;
};

} // namespace libzcash

// Type aliases for Sprout note encryption (ZC_NOTEPLAINTEXT_SIZE = 601 bytes)
typedef libzcash::NoteEncryption<ZC_NOTEPLAINTEXT_SIZE> ZCNoteEncryption;
typedef libzcash::NoteDecryption<ZC_NOTEPLAINTEXT_SIZE> ZCNoteDecryption;
typedef libzcash::PaymentDisclosureNoteDecryption<ZC_NOTEPLAINTEXT_SIZE> ZCPaymentDisclosureNoteDecryption;

#endif /* ZC_NOTE_ENCRYPTION_H_ */
