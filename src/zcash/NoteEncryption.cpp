// Copyright (c) 2016-2024 The Zcash developers
// Copyright (c) 2018-2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * @file NoteEncryption.cpp
 * @brief Sprout note encryption implementation
 *
 * Implements Curve25519 DH key exchange, BLAKE2b KDF, and ChaCha20-Poly1305 AEAD
 * for Sprout note encryption. Keys are clamped for security, nonces bound to transactions.
 */

#include "NoteEncryption.hpp"
#include <stdexcept>
#include "sodium.h"
#include <boost/static_assert.hpp>
#include "prf.h"
#include "librustzcash.h"

#define NOTEENCRYPTION_CIPHER_KEYSIZE 32

/**
 * @brief Clamp Curve25519 scalar for security
 * @param key 32-byte scalar (modified in place)
 */
void clamp_curve25519(unsigned char key[crypto_scalarmult_SCALARBYTES])
{
    key[0] &= 248;
    key[31] &= 127;
    key[31] |= 64;
}

/**
 * @brief Derive symmetric key using BLAKE2b
 * @param K Output 32-byte key
 * @param dhsecret DH shared secret
 * @param epk Ephemeral public key
 * @param pk_enc Recipient public key
 * @param hSig Transaction hash
 * @param nonce Encryption counter (0-254)
 * @throws std::logic_error if nonce exhausted
 */
void KDF(unsigned char K[NOTEENCRYPTION_CIPHER_KEYSIZE],
    const uint256 &dhsecret,
    const uint256 &epk,
    const uint256 &pk_enc,
    const uint256 &hSig,
    unsigned char nonce
   )
{
    if (nonce == 0xff) {
        throw std::logic_error("no additional nonce space for KDF");
    }

    unsigned char block[128] = {};
    memcpy(block+0, hSig.begin(), 32);
    memcpy(block+32, dhsecret.begin(), 32);
    memcpy(block+64, epk.begin(), 32);
    memcpy(block+96, pk_enc.begin(), 32);

    unsigned char personalization[crypto_generichash_blake2b_PERSONALBYTES] = {};
    memcpy(personalization, "ZcashKDF", 8);
    memcpy(personalization+8, &nonce, 1);

    if (crypto_generichash_blake2b_salt_personal(K, NOTEENCRYPTION_CIPHER_KEYSIZE,
                                                 block, 128,
                                                 NULL, 0, // No key.
                                                 NULL,    // No salt.
                                                 personalization
                                                ) != 0)
    {
        throw std::logic_error("hash function failure");
    }
}

namespace libzcash {

/**
 * @brief Initialize encryptor with ephemeral keypair
 * @tparam MLEN Plaintext length
 * @param hSig Transaction hash
 */
template<size_t MLEN>
NoteEncryption<MLEN>::NoteEncryption(uint256 hSig) : nonce(0), hSig(hSig) {
    // All of this code assumes crypto_scalarmult_BYTES is 32
    // There's no reason that will _ever_ change, but for
    // completeness purposes, let's check anyway.
    BOOST_STATIC_ASSERT(32 == crypto_scalarmult_BYTES);
    BOOST_STATIC_ASSERT(32 == crypto_scalarmult_SCALARBYTES);
    BOOST_STATIC_ASSERT(NOTEENCRYPTION_AUTH_BYTES == crypto_aead_chacha20poly1305_ABYTES);

    // Create the ephemeral keypair
    esk = random_uint256();
    epk = generate_pubkey(esk);
}

/**
 * @brief Initialize decryptor with secret key
 * @tparam MLEN Plaintext length
 * @param sk_enc Encryption secret key
 */
template<size_t MLEN>
NoteDecryption<MLEN>::NoteDecryption(uint256 sk_enc) : sk_enc(sk_enc) {
    this->pk_enc = NoteEncryption<MLEN>::generate_pubkey(sk_enc);
}

/**
 * @brief Encrypt message for recipient
 * @param pk_enc Recipient public key
 * @param message Plaintext
 * @return Ciphertext with auth tag
 * @throws std::logic_error if encryption fails
 */
template<size_t MLEN>
typename NoteEncryption<MLEN>::Ciphertext NoteEncryption<MLEN>::encrypt
                                          (const uint256 &pk_enc,
                                           const NoteEncryption<MLEN>::Plaintext &message
                                          )
{
    uint256 dhsecret;

    if (crypto_scalarmult(dhsecret.begin(), esk.begin(), pk_enc.begin()) != 0) {
        throw std::logic_error("Could not create DH secret");
    }

    // Construct the symmetric key
    unsigned char K[NOTEENCRYPTION_CIPHER_KEYSIZE];
    KDF(K, dhsecret, epk, pk_enc, hSig, nonce);

    // Increment the number of encryptions we've performed
    nonce++;

    // The nonce is zero because we never reuse keys
    unsigned char cipher_nonce[crypto_aead_chacha20poly1305_IETF_NPUBBYTES] = {};

    NoteEncryption<MLEN>::Ciphertext ciphertext;

    crypto_aead_chacha20poly1305_ietf_encrypt(ciphertext.begin(), NULL,
                                         message.begin(), MLEN,
                                         NULL, 0, // no "additional data"
                                         NULL, cipher_nonce, K);

    return ciphertext;
}

/**
 * @brief Decrypt encrypted note
 * @param ciphertext Encrypted message
 * @param epk Ephemeral public key
 * @param hSig Transaction hash
 * @param nonce Encryption counter
 * @return Decrypted plaintext
 * @throws note_decryption_failed if auth fails
 */
template<size_t MLEN>
typename NoteDecryption<MLEN>::Plaintext NoteDecryption<MLEN>::decrypt
                                         (const NoteDecryption<MLEN>::Ciphertext &ciphertext,
                                          const uint256 &epk,
                                          const uint256 &hSig,
                                          unsigned char nonce
                                         ) const
{
    uint256 dhsecret;

    if (crypto_scalarmult(dhsecret.begin(), sk_enc.begin(), epk.begin()) != 0) {
        throw std::logic_error("Could not create DH secret");
    }

    unsigned char K[NOTEENCRYPTION_CIPHER_KEYSIZE];
    KDF(K, dhsecret, epk, pk_enc, hSig, nonce);

    // The nonce is zero because we never reuse keys
    unsigned char cipher_nonce[crypto_aead_chacha20poly1305_IETF_NPUBBYTES] = {};

    NoteDecryption<MLEN>::Plaintext plaintext;

    // Message length is always NOTEENCRYPTION_AUTH_BYTES less than
    // the ciphertext length.
    if (crypto_aead_chacha20poly1305_ietf_decrypt(plaintext.begin(), NULL,
                                             NULL,
                                             ciphertext.begin(), NoteDecryption<MLEN>::CLEN,
                                             NULL,
                                             0,
                                             cipher_nonce, K) != 0) {
        throw note_decryption_failed();
    }

    return plaintext;
}

/**
 * @brief Decrypt using revealed ephemeral key for payment proof
 * @param ciphertext Encrypted message
 * @param pk_enc Recipient public key
 * @param esk Revealed ephemeral secret key
 * @param hSig Transaction hash
 * @param nonce Encryption counter
 * @return Decrypted plaintext
 * @throws note_decryption_failed if auth fails
 */
template<size_t MLEN>
typename PaymentDisclosureNoteDecryption<MLEN>::Plaintext PaymentDisclosureNoteDecryption<MLEN>::decryptWithEsk
                                         (const PaymentDisclosureNoteDecryption<MLEN>::Ciphertext &ciphertext,
                                          const uint256 &pk_enc,
                                          const uint256 &esk,
                                          const uint256 &hSig,
                                          unsigned char nonce
                                         ) const
{
    uint256 dhsecret;

    if (crypto_scalarmult(dhsecret.begin(), esk.begin(), pk_enc.begin()) != 0) {
        throw std::logic_error("Could not create DH secret");
    }

    // Regenerate keypair
    uint256 epk = NoteEncryption<MLEN>::generate_pubkey(esk);

    unsigned char K[NOTEENCRYPTION_CIPHER_KEYSIZE];
    KDF(K, dhsecret, epk, pk_enc, hSig, nonce);

    // The nonce is zero because we never reuse keys
    unsigned char cipher_nonce[crypto_aead_chacha20poly1305_IETF_NPUBBYTES] = {};

    PaymentDisclosureNoteDecryption<MLEN>::Plaintext plaintext;

    // Message length is always NOTEENCRYPTION_AUTH_BYTES less than
    // the ciphertext length.
    if (crypto_aead_chacha20poly1305_ietf_decrypt(plaintext.begin(), NULL,
                                             NULL,
                                             ciphertext.begin(), PaymentDisclosureNoteDecryption<MLEN>::CLEN,
                                             NULL,
                                             0,
                                             cipher_nonce, K) != 0) {
        throw note_decryption_failed();
    }

    return plaintext;
}




/**
 * @brief Derive encryption key from spending key
 * @param a_sk Spending key
 * @return Clamped secret key
 */
template<size_t MLEN>
uint256 NoteEncryption<MLEN>::generate_privkey(const uint252 &a_sk)
{
    uint256 sk = PRF_addr_sk_enc(a_sk);

    clamp_curve25519(sk.begin());

    return sk;
}

/**
 * @brief Compute public key from secret key
 * @param sk_enc Secret key
 * @return Public key
 * @throws std::logic_error if operation fails
 */
template<size_t MLEN>
uint256 NoteEncryption<MLEN>::generate_pubkey(const uint256 &sk_enc)
{
    uint256 pk;

    if (crypto_scalarmult_base(pk.begin(), sk_enc.begin()) != 0) {
        throw std::logic_error("Could not create public key");
    }

    return pk;
}

/**
 * @brief Generate secure random 256-bit value
 * @return Random uint256
 */
uint256 random_uint256()
{
    uint256 ret;
    randombytes_buf(ret.begin(), 32);

    return ret;
}

/**
 * @brief Generate secure random 252-bit value
 * @return Random uint252
 */
uint252 random_uint252()
{
    uint256 rand = random_uint256();
    (*rand.begin()) &= 0x0F;

    return uint252(rand);
}

// Template instantiations for Sprout notes
template class NoteEncryption<ZC_NOTEPLAINTEXT_SIZE>;
template class NoteDecryption<ZC_NOTEPLAINTEXT_SIZE>;
template class PaymentDisclosureNoteDecryption<ZC_NOTEPLAINTEXT_SIZE>;

} // namespace libzcash
