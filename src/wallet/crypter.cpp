// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "crypter.h"

#include "script/script.h"
#include "script/standard.h"
#include "streams.h"
#include "util.h"

#include <string>
#include <vector>
#include <boost/foreach.hpp>
#include <openssl/aes.h>
#include <openssl/evp.h>

using namespace libzcash;

bool CCrypter::SetKeyFromPassphrase(const SecureString& strKeyData, const std::vector<unsigned char>& chSalt, const unsigned int nRounds, const unsigned int nDerivationMethod)
{
    if (nRounds < 1 || chSalt.size() != WALLET_CRYPTO_SALT_SIZE)
        return false;

    int i = 0;
    if (nDerivationMethod == 0)
        i = EVP_BytesToKey(EVP_aes_256_cbc(), EVP_sha512(), &chSalt[0],
                          (unsigned char *)&strKeyData[0], strKeyData.size(), nRounds, chKey, chIV);

    if (i != (int)WALLET_CRYPTO_KEY_SIZE)
    {
        memory_cleanse(chKey, sizeof(chKey));
        memory_cleanse(chIV, sizeof(chIV));
        return false;
    }

    fKeySet = true;
    return true;
}

bool CCrypter::SetKey(const CKeyingMaterial& chNewKey, const std::vector<unsigned char>& chNewIV)
{
    if (chNewKey.size() != WALLET_CRYPTO_KEY_SIZE || chNewIV.size() != WALLET_CRYPTO_KEY_SIZE)
        return false;

    memcpy(&chKey[0], &chNewKey[0], sizeof chKey);
    memcpy(&chIV[0], &chNewIV[0], sizeof chIV);

    fKeySet = true;
    return true;
}

bool CCrypter::Encrypt(const CKeyingMaterial& vchPlaintext, std::vector<unsigned char> &vchCiphertext)
{
    if (!fKeySet)
        return false;

    // max ciphertext len for a n bytes of plaintext is
    // n + AES_BLOCK_SIZE - 1 bytes
    int nLen = vchPlaintext.size();
    int nCLen = nLen + AES_BLOCK_SIZE, nFLen = 0;
    vchCiphertext = std::vector<unsigned char> (nCLen);

    bool fOk = true;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    assert(ctx);
    if (fOk) fOk = EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV) != 0;
    if (fOk) fOk = EVP_EncryptUpdate(ctx, &vchCiphertext[0], &nCLen, &vchPlaintext[0], nLen) != 0;
    if (fOk) fOk = EVP_EncryptFinal_ex(ctx, (&vchCiphertext[0]) + nCLen, &nFLen) != 0;
    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    vchCiphertext.resize(nCLen + nFLen);
    return true;
}

bool CCrypter::Decrypt(const std::vector<unsigned char>& vchCiphertext, CKeyingMaterial& vchPlaintext)
{
    if (!fKeySet)
        return false;

    // plaintext will always be equal to or lesser than length of ciphertext
    int nLen = vchCiphertext.size();
    int nPLen = nLen, nFLen = 0;

    vchPlaintext = CKeyingMaterial(nPLen);

    bool fOk = true;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    assert(ctx);
    if (fOk) fOk = EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, chKey, chIV) != 0;
    if (fOk) fOk = EVP_DecryptUpdate(ctx, &vchPlaintext[0], &nPLen, &vchCiphertext[0], nLen) != 0;
    if (fOk) fOk = EVP_DecryptFinal_ex(ctx, (&vchPlaintext[0]) + nPLen, &nFLen) != 0;
    EVP_CIPHER_CTX_free(ctx);

    if (!fOk) return false;

    vchPlaintext.resize(nPLen + nFLen);
    return true;
}


static bool EncryptSecret(const CKeyingMaterial& vMasterKey, const CKeyingMaterial &vchPlaintext, const uint256& nIV, std::vector<unsigned char> &vchCiphertext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Encrypt(*((const CKeyingMaterial*)&vchPlaintext), vchCiphertext);
}

static bool DecryptSecret(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCiphertext, const uint256& nIV, CKeyingMaterial& vchPlaintext)
{
    CCrypter cKeyCrypter;
    std::vector<unsigned char> chIV(WALLET_CRYPTO_KEY_SIZE);
    memcpy(&chIV[0], &nIV, WALLET_CRYPTO_KEY_SIZE);
    if (!cKeyCrypter.SetKey(vMasterKey, chIV))
        return false;
    return cKeyCrypter.Decrypt(vchCiphertext, *((CKeyingMaterial*)&vchPlaintext));
}

static bool DecryptHDSeed(
    const CKeyingMaterial& vMasterKey,
    const std::vector<unsigned char>& vchCryptedSecret,
    const uint256& seedFp,
    HDSeed& seed)
{
    CKeyingMaterial vchSecret;

    // Use seed's fingerprint as IV
    // TODO: Handle IV properly when we make encryption a supported feature
    if(!DecryptSecret(vMasterKey, vchCryptedSecret, seedFp, vchSecret))
        return false;

    seed = HDSeed(vchSecret);
    return seed.Fingerprint() == seedFp;
}

static bool DecryptKey(const CKeyingMaterial& vMasterKey, const std::vector<unsigned char>& vchCryptedSecret, const CPubKey& vchPubKey, CKey& key)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, vchPubKey.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != 32)
        return false;

    key.Set(vchSecret.begin(), vchSecret.end(), vchPubKey.IsCompressed());
    return key.VerifyPubKey(vchPubKey);
}

static bool DecryptSproutSpendingKey(const CKeyingMaterial& vMasterKey,
                               const std::vector<unsigned char>& vchCryptedSecret,
                               const libzcash::SproutPaymentAddress& address,
                               libzcash::SproutSpendingKey& sk)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, address.GetHash(), vchSecret))
        return false;

    if (vchSecret.size() != libzcash::SerializedSproutSpendingKeySize)
        return false;

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> sk;
    return sk.address() == address;
}

static bool DecryptSaplingSpendingKey(const CKeyingMaterial& vMasterKey,
                               const std::vector<unsigned char>& vchCryptedSecret,
                               const uint256& extfvkFinger,
                               libzcash::SaplingExtendedSpendingKey& sk)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, extfvkFinger, vchSecret))
        return false;

    if (vchSecret.size() != ZIP32_XSK_SIZE)
        return false;

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> sk;
    return sk.ToXFVK().fvk.GetFingerprint() == extfvkFinger;
}

static bool DecryptSaplingExtendedFullViewingKey(const CKeyingMaterial& vMasterKey,
                               const std::vector<unsigned char>& vchCryptedSecret,
                               const uint256& extfvkFinger,
                               libzcash::SaplingExtendedFullViewingKey& extfvk)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSecret(vMasterKey, vchCryptedSecret, extfvkFinger, vchSecret))
        return false;

    if (vchSecret.size() != ZIP32_XFVK_SIZE)
        return false;

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> extfvk;
    return extfvk.fvk.GetFingerprint() == extfvkFinger;
}

bool CCryptoKeyStore::SetCrypted()
{
    LOCK(cs_KeyStore);
    if (fUseCrypto)
        return true;
    if (!(mapKeys.empty() && mapSproutSpendingKeys.empty() && mapSaplingSpendingKeys.empty()))
        return false;
    fUseCrypto = true;
    return true;
}

bool CCryptoKeyStore::Lock()
{
    if (!SetCrypted())
        return false;

    {
        LOCK(cs_KeyStore);
        vMasterKey.clear();
    }

    NotifyStatusChanged(this);
    return true;
}

//Unlock the wallet without check for initial wallet encryption
bool CCryptoKeyStore::UnlockUnchecked(const CKeyingMaterial& vMasterKeyIn) {
    LOCK(cs_KeyStore);

    vMasterKey = vMasterKeyIn;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::OpenWallet(const CKeyingMaterial& vMasterKeyIn) {
    LOCK(cs_KeyStore);

    //Decrypt Double encrypted Seed to encrypted seed
    CKeyingMaterial vchSecret;
    if(!DecryptSecret(vMasterKeyIn, cryptedHDSeed.second, cryptedHDSeed.first, vchSecret)) {
        LogPrintf("Initial HDSeed decryption failed!!!\n");
        return false;
    }

    //Deserialize to encrypted seed
    uint256 seedFp;
    std::vector<unsigned char> vchCryptedSecret;
    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> seedFp;
    ss >> vchCryptedSecret;

    //Attempt decryption to HDSeed
    HDSeed seed;
    if (!DecryptHDSeed(vMasterKeyIn, vchCryptedSecret, seedFp, seed)) {
        LogPrintf("Final HDSeed decryption failed!!!\n");
        return false;
    }

    //Verify check hash
    CDataStream ds1(SER_NETWORK, PROTOCOL_VERSION);
    ds1 << seedFp;

    CDataStream ds2(SER_NETWORK, PROTOCOL_VERSION);
    ds2 << seed.EncryptionFingerprint();

    uint256 checkHash = Hash(ds1.begin(), ds1.end(), ds2.begin(), ds2.end());
    if (checkHash != cryptedHDSeed.first) {
        return false;
    }

    //Set the single encrypted seed in memory
    cryptedHDSeed = std::make_pair(seedFp, vchCryptedSecret);

    //Clear unencrypted seed
    hdSeed = HDSeed();

    //Set the masterpassword
    vMasterKey = vMasterKeyIn;
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::Unlock(const CKeyingMaterial& vMasterKeyIn)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        bool keyPass = false;
        bool keyFail = false;
        if (!cryptedHDSeed.first.IsNull()) {
            HDSeed seed;
            if (!DecryptHDSeed(vMasterKeyIn, cryptedHDSeed.second, cryptedHDSeed.first, seed))
            {
                keyFail = true;
            } else {
                keyPass = true;
            }
        }
        CryptedKeyMap::const_iterator mi = mapCryptedKeys.begin();
        for (; mi != mapCryptedKeys.end(); ++mi)
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            CKey key;
            if (!DecryptKey(vMasterKeyIn, vchCryptedSecret, vchPubKey, key))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        CryptedSproutSpendingKeyMap::const_iterator miSprout = mapCryptedSproutSpendingKeys.begin();
        for (; miSprout != mapCryptedSproutSpendingKeys.end(); ++miSprout)
        {
            const libzcash::SproutPaymentAddress &address = (*miSprout).first;
            const std::vector<unsigned char> &vchCryptedSecret = (*miSprout).second;
            libzcash::SproutSpendingKey sk;
            if (!DecryptSproutSpendingKey(vMasterKeyIn, vchCryptedSecret, address, sk))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        CryptedSaplingSpendingKeyMap::const_iterator miSapling = mapCryptedSaplingSpendingKeys.begin();
        for (; miSapling != mapCryptedSaplingSpendingKeys.end(); ++miSapling)
        {
            const libzcash::SaplingExtendedFullViewingKey &extfvk = (*miSapling).first;
            const std::vector<unsigned char> &vchCryptedSecret = (*miSapling).second;
            libzcash::SaplingExtendedSpendingKey sk;
            if (!DecryptSaplingSpendingKey(vMasterKeyIn, vchCryptedSecret, extfvk.fvk.GetFingerprint(), sk))
            {
                keyFail = true;
                break;
            }
            keyPass = true;
            if (fDecryptionThoroughlyChecked)
                break;
        }
        if (keyPass && keyFail)
        {
            LogPrintf("The wallet is probably corrupted: Some keys decrypt but not all.\n");
            assert(false);
        }
        if (keyFail || !keyPass)
            return false;
        vMasterKey = vMasterKeyIn;
        fDecryptionThoroughlyChecked = true;
    }
    NotifyStatusChanged(this);
    return true;
}

bool CCryptoKeyStore::SetHDSeed(const HDSeed& seed)
{

    LOCK(cs_KeyStore);
    if (IsCrypted()) {
        return false;
    }

    return CBasicKeyStore::SetHDSeed(seed);
}

bool CCryptoKeyStore::SetCryptedHDSeed(
    const uint256& seedFp,
    const std::vector<unsigned char>& vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted()) {
            return false;
        }

        if (!cryptedHDSeed.first.IsNull()) {
            // Don't allow an existing seed to be changed. We can maybe relax this
            // restriction later once we have worked out the UX implications.
            return false;
        }

        cryptedHDSeed = std::make_pair(seedFp, vchCryptedSecret);

        //Clear unencrypted seed
        hdSeed = HDSeed();
    }
    return true;
}

bool CCryptoKeyStore::HaveHDSeed() const
{
    LOCK(cs_KeyStore);
    if (!IsCrypted())
        return CBasicKeyStore::HaveHDSeed();

    return !cryptedHDSeed.second.empty();
}

bool CCryptoKeyStore::GetHDSeed(HDSeed& seedOut) const
{
    LOCK(cs_KeyStore);
    if (!IsCrypted())
        return CBasicKeyStore::GetHDSeed(seedOut);

    if (cryptedHDSeed.second.empty())
        return false;

    return DecryptHDSeed(vMasterKey, cryptedHDSeed.second, cryptedHDSeed.first, seedOut);
}

bool CCryptoKeyStore::GetSeedPhrase(std::string &phraseOut) const
{
    LOCK(cs_KeyStore);
    if (!IsCrypted())
        return CBasicKeyStore::GetSeedPhrase(phraseOut);

    if (cryptedHDSeed.second.empty())
        return false;

    HDSeed seedOut;
    DecryptHDSeed(vMasterKey, cryptedHDSeed.second, cryptedHDSeed.first, seedOut);

    seedOut.GetPhrase(phraseOut);
    return true;
}

bool CCryptoKeyStore::AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        mapCryptedKeys[vchPubKey.GetID()] = make_pair(vchPubKey, vchCryptedSecret);
    }
    return true;
}

bool CCryptoKeyStore::GetKey(const CKeyID &address, CKey& keyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetKey(address, keyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            const CPubKey &vchPubKey = (*mi).second.first;
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
            return DecryptKey(vMasterKey, vchCryptedSecret, vchPubKey, keyOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CKeyStore::GetPubKey(address, vchPubKeyOut);

        CryptedKeyMap::const_iterator mi = mapCryptedKeys.find(address);
        if (mi != mapCryptedKeys.end())
        {
            vchPubKeyOut = (*mi).second.first;
            return true;
        }
    }
    return false;
}

bool CCryptoKeyStore::AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk)
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::AddSproutSpendingKey(sk);

        if (IsLocked())
            return false;

        std::vector<unsigned char> vchCryptedSecret;
        CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << sk;
        CKeyingMaterial vchSecret(ss.begin(), ss.end());
        auto address = sk.address();
        if (!EncryptSecret(vMasterKey, vchSecret, address.GetHash(), vchCryptedSecret))
            return false;

        if (!AddCryptedSproutSpendingKey(address, sk.receiving_key(), vchCryptedSecret))
            return false;
    }
    return true;
}

bool CCryptoKeyStore::AddCryptedSproutSpendingKey(
    const libzcash::SproutPaymentAddress &address,
    const libzcash::ReceivingKey &rk,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        if (!SetCrypted())
            return false;

        mapCryptedSproutSpendingKeys[address] = vchCryptedSecret;
        mapNoteDecryptors.insert(std::make_pair(address, ZCNoteDecryption(rk)));
    }
    return true;
}

//Generalized Encryption/Decryption of serialized objects
bool CCryptoKeyStore::EncryptSerializedSecret(
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret)
{
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return false;
    }
    if (IsLocked()) {
        return false;
    }
    return EncryptSerializedSecret(vMasterKey, vchSecret, chash, vchCryptedSecret);
}

bool CCryptoKeyStore::EncryptSerializedSecret(
    CKeyingMaterial &vMasterKeyIn,
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret)
{
    LOCK(cs_KeyStore);
    return EncryptSecret(vMasterKeyIn, vchSecret, chash, vchCryptedSecret);
}

bool CCryptoKeyStore::DecryptSerializedSecret(
     const std::vector<unsigned char>& vchCryptedSecret,
     const uint256 chash,
     CKeyingMaterial &vchSecret)
{
    LOCK(cs_KeyStore);
    if (!IsCrypted()) {
        return false;
    }

    if (IsLocked()) {
        return false;
    }

    return DecryptSecret(vMasterKey, vchCryptedSecret, chash, vchSecret);

}

bool CCryptoKeyStore::AddCryptedSaplingSpendingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    {
        LOCK(cs_KeyStore);
        // if SaplingFullViewingKey is not in SaplingFullViewingKeyMap, add it
        if (!CBasicKeyStore::AddSaplingExtendedFullViewingKey(extfvk)) {
            return false;
        }

        mapCryptedSaplingSpendingKeys[extfvk] = vchCryptedSecret;
    }
    return true;
}

bool CCryptoKeyStore::GetSproutSpendingKey(const libzcash::SproutPaymentAddress &address, libzcash::SproutSpendingKey &skOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetSproutSpendingKey(address, skOut);

        CryptedSproutSpendingKeyMap::const_iterator mi = mapCryptedSproutSpendingKeys.find(address);
        if (mi != mapCryptedSproutSpendingKeys.end())
        {
            const std::vector<unsigned char> &vchCryptedSecret = (*mi).second;
            return DecryptSproutSpendingKey(vMasterKey, vchCryptedSecret, address, skOut);
        }
    }
    return false;
}

bool CCryptoKeyStore::GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey &skOut) const
{
    {
        LOCK(cs_KeyStore);
        if (!IsCrypted())
            return CBasicKeyStore::GetSaplingSpendingKey(extfvk, skOut);

        for (auto entry : mapCryptedSaplingSpendingKeys) {
            if (entry.first == extfvk) {
                const std::vector<unsigned char> &vchCryptedSecret = entry.second;
                return DecryptSaplingSpendingKey(vMasterKey, vchCryptedSecret, entry.first.fvk.GetFingerprint(), skOut);
            }
        }
    }
    return false;
}
