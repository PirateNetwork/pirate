// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "wallet/crypter.h"

// wallet_encryption_audit.md C1: CCrypter::SetKeyFromPassphrase() used to
// only support the legacy EVP_BytesToKey(SHA-512) KDF, which is not
// memory-hard and cheap to brute-force offline. It now also supports scrypt
// (WALLET_DERIVATION_METHOD_SCRYPT), with the cost parameter N validated as a
// power of two in [2^14, 2^20] to bound the memory a crafted wallet file can
// force at unlock time. This pins that validation and the derived key's
// usability (and determinism) down directly, independent of the full CWallet
// encrypt/unlock machinery covered elsewhere (wallet/gtest/test_wallet_zkeys.cpp).

static std::vector<unsigned char> TestSalt()
{
    return std::vector<unsigned char>(WALLET_CRYPTO_SALT_SIZE, 0x42);
}

TEST(CrypterTests, ScryptRejectsNonPowerOfTwoN)
{
    CCrypter crypter;
    SecureString pass("test passphrase");
    // 12345 is not a power of two.
    EXPECT_FALSE(crypter.SetKeyFromPassphrase(pass, TestSalt(), 12345, WALLET_DERIVATION_METHOD_SCRYPT));
}

TEST(CrypterTests, ScryptRejectsNBelowMinimum)
{
    CCrypter crypter;
    SecureString pass("test passphrase");
    // 2^13 is one power-of-two step below the WALLET_SCRYPT_MIN_LOGN=14 floor.
    EXPECT_FALSE(crypter.SetKeyFromPassphrase(pass, TestSalt(), 1u << 13, WALLET_DERIVATION_METHOD_SCRYPT));
}

TEST(CrypterTests, ScryptRejectsNAboveMaximum)
{
    CCrypter crypter;
    SecureString pass("test passphrase");
    // 2^21 is one power-of-two step above the WALLET_SCRYPT_MAX_LOGN=20 ceiling.
    EXPECT_FALSE(crypter.SetKeyFromPassphrase(pass, TestSalt(), 1u << 21, WALLET_DERIVATION_METHOD_SCRYPT));
}

TEST(CrypterTests, ScryptAcceptsDefaultN)
{
    CCrypter crypter;
    SecureString pass("test passphrase");
    EXPECT_TRUE(crypter.SetKeyFromPassphrase(pass, TestSalt(), WALLET_SCRYPT_DEFAULT_N, WALLET_DERIVATION_METHOD_SCRYPT));
}

TEST(CrypterTests, UnknownDerivationMethodRejected)
{
    CCrypter crypter;
    SecureString pass("test passphrase");
    EXPECT_FALSE(crypter.SetKeyFromPassphrase(pass, TestSalt(), 25000, 2 /* neither SHA512 nor SCRYPT */));
}

TEST(CrypterTests, LegacySha512StillAccepted)
{
    // Method 0 must keep working so pre-existing (not-yet-upgraded) wallets
    // stay decryptable; NeedsKDFUpgrade()/AppInit2's auto-upgrade only
    // re-encrypts opportunistically, they don't rewrite every wallet at once.
    CCrypter crypter;
    SecureString pass("test passphrase");
    EXPECT_TRUE(crypter.SetKeyFromPassphrase(pass, TestSalt(), 25000, WALLET_DERIVATION_METHOD_SHA512));
}

// The derived key must actually be usable for encryption, and deterministic:
// re-deriving from the same passphrase/salt/N must reproduce a key that can
// decrypt data encrypted under the first derivation.
TEST(CrypterTests, ScryptDerivedKeyIsUsableAndDeterministic)
{
    SecureString pass("correct horse battery staple");
    std::vector<unsigned char> salt = TestSalt();
    unsigned int N = WALLET_SCRYPT_DEFAULT_N;

    CCrypter crypter1;
    ASSERT_TRUE(crypter1.SetKeyFromPassphrase(pass, salt, N, WALLET_DERIVATION_METHOD_SCRYPT));

    CKeyingMaterial plaintext(32, 0x7A);
    std::vector<unsigned char> ciphertext;
    ASSERT_TRUE(crypter1.Encrypt(plaintext, ciphertext));

    // Fresh CCrypter, independently re-deriving from the same inputs.
    CCrypter crypter2;
    ASSERT_TRUE(crypter2.SetKeyFromPassphrase(pass, salt, N, WALLET_DERIVATION_METHOD_SCRYPT));

    CKeyingMaterial decrypted;
    ASSERT_TRUE(crypter2.Decrypt(ciphertext, decrypted));
    EXPECT_EQ(plaintext, decrypted);
}

// A different passphrase must derive a different (unusable) key.
TEST(CrypterTests, ScryptDifferentPassphraseYieldsDifferentKey)
{
    std::vector<unsigned char> salt = TestSalt();
    unsigned int N = WALLET_SCRYPT_DEFAULT_N;

    CCrypter crypter1;
    ASSERT_TRUE(crypter1.SetKeyFromPassphrase(SecureString("passphrase one"), salt, N, WALLET_DERIVATION_METHOD_SCRYPT));
    CKeyingMaterial plaintext(32, 0x7A);
    std::vector<unsigned char> ciphertext;
    ASSERT_TRUE(crypter1.Encrypt(plaintext, ciphertext));

    CCrypter crypter2;
    ASSERT_TRUE(crypter2.SetKeyFromPassphrase(SecureString("passphrase two"), salt, N, WALLET_DERIVATION_METHOD_SCRYPT));
    CKeyingMaterial decrypted;
    // Either Decrypt() fails outright (bad padding) or it "succeeds" with
    // garbage that doesn't match the original plaintext.
    bool ok = crypter2.Decrypt(ciphertext, decrypted);
    EXPECT_TRUE(!ok || decrypted != plaintext);
}
