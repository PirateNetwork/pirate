// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
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

#include "wallet/wallet.h"

#include "asyncrpcqueue.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "consensus/consensus.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "net.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "utilmoneystr.h"
#include "zcash/Note.hpp"
#include "crypter.h"
#include "coins.h"
#include "wallet/asyncrpcoperation_saplingconsolidation.h"
#include "wallet/asyncrpcoperation_sweeptoaddress.h"
#include "zcash/address/zip32.h"
#include "cc/CCinclude.h"
#include "rpcpiratewallet.h"
#include "komodo_utils.h"
#include "komodo_bitcoind.h"
#include "komodo_notary.h"
#include "komodo_interest.h"
#include "komodo_globals.h"
#include "komodo_defs.h"

#include <assert.h>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#ifdef __linux__ //linux only
#include <malloc.h>
#endif

using namespace std;
using namespace libzcash;

std::vector<CWalletRef> vpwallets;

/**
 * Settings
 */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;
unsigned int nTxConfirmTarget = DEFAULT_TX_CONFIRM_TARGET;
bool bSpendZeroConfChange = true;
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;

CAmount minTxValue = DEFAULT_MIN_TX_VALUE;

bool fWalletRbf = DEFAULT_WALLET_RBF;
//
// CBlockIndex *komodo_chainactive(int32_t height);
// extern std::string DONATION_PUBKEY;
// int32_t komodo_dpowconfs(int32_t height,int32_t numconfs);
// int tx_height( const uint256 &hash );
int scanperc;
bool fTxDeleteEnabled = false;
bool fTxConflictDeleteEnabled = false;
int fDeleteInterval = DEFAULT_TX_DELETE_INTERVAL;
unsigned int fDeleteTransactionsAfterNBlocks = DEFAULT_TX_RETENTION_BLOCKS;
unsigned int fKeepLastNTransactions = DEFAULT_TX_RETENTION_LASTTX;
std::string recoverySeedPhrase = "";
bool usingGUI = false;
int recoveryHeight = 0;

SecureString *strOpeningWalletPassphrase;

/**
 * Fees smaller than this (in satoshi) are considered zero fee (for transaction creation)
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(1000);

/**
 * If fee estimation does not have enough data to provide estimates, use this fee instead.
 * Has no effect if not using fee estimation
 * Override with -fallbackfee
 */
CFeeRate CWallet::fallbackFee = CFeeRate(DEFAULT_FALLBACK_FEE);

const uint256 CMerkleTx::ABANDON_HASH(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly
{
    bool operator()(const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

std::string JSOutPoint::ToString() const
{
    return strprintf("JSOutPoint(%s, %d, %d)", hash.ToString().substr(0,10), js, n);
}

std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return NULL;
    return &(it->second);
}

// Generate a new spending key and return its public payment address
libzcash::SproutPaymentAddress CWallet::GenerateNewSproutZKey()
{
    AssertLockHeld(cs_wallet); // mapSproutZKeyMetadata

    auto k = SproutSpendingKey::random();
    auto addr = k.address();

    // Check for collision, even though it is unlikely to ever occur
    if (CCryptoKeyStore::HaveSproutSpendingKey(addr))
        throw std::runtime_error("CWallet::GenerateNewSproutZKey(): Collision detected");

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapSproutZKeyMetadata[addr] = CKeyMetadata(nCreationTime);

    if (!AddSproutZKey(k))
        throw std::runtime_error("CWallet::GenerateNewSproutZKey(): AddSproutZKey failed");
    return addr;
}

// Generate a new Sapling spending key and return its public payment address
SaplingPaymentAddress CWallet::GenerateNewSaplingZKey()
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // Try to get the seed
    HDSeed seed;
    if (!GetHDSeed(seed))
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): HD seed not found");

    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed, pwalletMain->bip39Enabled);
    uint32_t bip44CoinType = Params().BIP44CoinType();

    // We use a fixed keypath scheme of m/32'/coin_type'/account'
    // Derive m/32'
    auto m_32h = m.Derive(32 | ZIP32_HARDENED_KEY_LIMIT);
    // Derive m/32'/coin_type'
    auto m_32h_cth = m_32h.Derive(bip44CoinType | ZIP32_HARDENED_KEY_LIMIT);

    // Derive account key at next index, skip keys already known to the wallet
    libzcash::SaplingExtendedSpendingKey xsk;
    do
    {
        xsk = m_32h_cth.Derive(hdChain.saplingAccountCounter | ZIP32_HARDENED_KEY_LIMIT);
        metadata.hdKeypath = "m/32'/" + std::to_string(bip44CoinType) + "'/" + std::to_string(hdChain.saplingAccountCounter) + "'";
        metadata.seedFp = hdChain.seedFp;

        //Set Primary key for diversification
        if (hdChain.saplingAccountCounter == 0) {
            SetPrimarySpendingKey(xsk);
        }

        // Increment childkey index
        hdChain.saplingAccountCounter++;
    } while (HaveSaplingSpendingKey(xsk.ToXFVK()));

    // Update the chain model in the database
    if (fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(hdChain))
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): Writing HD chain model failed");

    auto ivk = xsk.expsk.full_viewing_key().in_viewing_key();
    mapSaplingZKeyMetadata[ivk] = metadata;

    auto addr = xsk.DefaultAddress();
    if (!AddSaplingZKey(xsk)) {
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): AddSaplingZKey failed");
    }
    // return default sapling payment address.
    return addr;
}

// Generate a new Sapling diversified payment address
SaplingPaymentAddress CWallet::GenerateNewSaplingDiversifiedAddress()
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    libzcash::SaplingExtendedSpendingKey extsk;
    if (pwalletMain->primarySaplingSpendingKey == boost::none) {
        // Try to get the seed
        HDSeed seed;
        if (!GetHDSeed(seed))
            throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): HD seed not found");

        auto m = libzcash::SaplingExtendedSpendingKey::Master(seed, pwalletMain->bip39Enabled);
        uint32_t bip44CoinType = Params().BIP44CoinType();

        //Derive default key
        auto m_32h = m.Derive(32 | ZIP32_HARDENED_KEY_LIMIT);
        auto m_32h_cth = m_32h.Derive(bip44CoinType | ZIP32_HARDENED_KEY_LIMIT);
        extsk = m_32h_cth.Derive(0 | ZIP32_HARDENED_KEY_LIMIT);

        //Check of default spending key
        auto ivk = extsk.expsk.full_viewing_key().in_viewing_key();
        libzcash::SaplingExtendedFullViewingKey extfvk;
        pwalletMain->GetSaplingFullViewingKey(ivk, extfvk);
        if (!HaveSaplingSpendingKey(extfvk)) {

          //Set metadata
          int64_t nCreationTime = GetTime();
          CKeyMetadata metadata(nCreationTime);
          metadata.hdKeypath = "m/32'/" + std::to_string(bip44CoinType) + "'/0'";
          metadata.seedFp = hdChain.seedFp;
          mapSaplingZKeyMetadata[ivk] = metadata;

          //Add Address to wallet
          auto addr = extsk.DefaultAddress();
          if (!AddSaplingZKey(extsk)) {
              throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddSaplingZKey failed");
          }

          //Return default address for default key
          return addr;
        } else {
            SetPrimarySpendingKey(extsk);
        }
    } else {
        extsk = pwalletMain->primarySaplingSpendingKey.get();
    }

    auto ivk = extsk.expsk.full_viewing_key().in_viewing_key();
    SaplingPaymentAddress addr;
    blob88 diversifier;

    //Initalize diversifier
    for (int j = 0; j < diversifier.size(); j++) {
        diversifier.begin()[j] = 0;
    }

    //Get Last used diversifier if one exists
    for (auto entry : mapLastDiversifierPath) {
        if (entry.first == ivk) {
            diversifier = entry.second;
        }
    }

    bool found = false;
    do {
      addr = extsk.ToXFVK().Address(diversifier).get().second;
      if (!GetSaplingExtendedSpendingKey(addr, extsk)) {
          found = true;
          //Save last used diversifier by ivk
          if (!AddLastDiversifierUsed(ivk, diversifier)) {
              throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddLastDiversifierUsed failed");
          }

      }

      //increment the diversifier
      for (int j = 0; j < diversifier.size(); j++) {
          int i = diversifier.begin()[j];
          diversifier.begin()[j]++;
          i++;
          if ( i >= 256) {
              diversifier.begin()[j] = 0;
          } else {
            break;
          }
          //Should only be reached after all combinations have been tried
          if (i >= 256 && j+1 == diversifier.size())
              throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): Unable to find new diversified address with the current key");
      }

    }
    while (!found);

    //Add to wallet
    if (!AddSaplingIncomingViewingKey(ivk, addr)) {
        throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddSaplingIncomingViewingKey failed");
    }

    //Add to wallet
    if (!AddSaplingDiversifiedAddress(addr, ivk, diversifier)) {
        throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddSaplingDiversifiedAddress failed");
    }

    // return diversified sapling payment address
    return addr;
}

bool CWallet::SetPrimarySpendingKey(
    const libzcash::SaplingExtendedSpendingKey &extsk)
{
      AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

      if (IsCrypted() && IsLocked()) {
          return false;
      }

      pwalletMain->primarySaplingSpendingKey = extsk;

      if (!fFileBacked) {
          return true;
      }

      if (!IsCrypted()) {
          return CWalletDB(strWalletFile).WritePrimarySaplingSpendingKey(extsk);
      } else {

          std::vector<unsigned char> vchCryptedSecret;
          uint256 chash = extsk.ToXFVK().fvk.GetFingerprint();
          CKeyingMaterial vchSecret = SerializeForEncryptionInput(extsk);

          if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
              LogPrintf("Encrypting Spending key failed!!!\n");
              return false;
          }

          return CWalletDB(strWalletFile).WriteCryptedPrimarySaplingSpendingKey(extsk, vchCryptedSecret);
      }

      return true;
}

bool CWallet::LoadCryptedPrimarySaplingSpendingKey(const uint256 &extfvkFinger, const std::vector<unsigned char> &vchCryptedSecret)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, extfvkFinger, vchSecret)) {
        LogPrintf("Decrypting Primary Spending Key failed!!!\n");
        return false;
    }

    libzcash::SaplingExtendedSpendingKey extsk;
    DeserializeFromDecryptionOutput(vchSecret, extsk);

    if (extsk.ToXFVK().fvk.GetFingerprint() != extfvkFinger) {
        LogPrintf("Decrypted Primary Spending Key fingerprint is invalid!!!\n");
        return false;
    }

    primarySaplingSpendingKey = extsk;
    return true;
}

// Add spending key to keystore
bool CWallet::AddSaplingZKey(
    const libzcash::SaplingExtendedSpendingKey &extsk)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata


    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    auto ivk = extsk.ToXFVK().fvk.in_viewing_key();

    if (!IsCrypted()) {
        if (!CCryptoKeyStore::AddSaplingSpendingKey(extsk)) {
            LogPrintf("Adding unencrypted Sapling Spending Key failed!!!\n");
            return false;
        }


        if(!CWalletDB(strWalletFile).WriteSaplingZKey(ivk, extsk, mapSaplingZKeyMetadata[ivk])) {
            LogPrintf("Writing unencrypted Sapling Spending Key failed!!!\n");
            return false;
        }
    } else {
        //Encrypt Sapling Extended Speding Key
        auto extfvk = extsk.ToXFVK();

        std::vector<unsigned char> vchCryptedSpendingKey;
        uint256 chash = extfvk.fvk.GetFingerprint();
        CKeyingMaterial vchSpendingKey = SerializeForEncryptionInput(extsk);

        if (!EncryptSerializedWalletObjects(vchSpendingKey, chash, vchCryptedSpendingKey)) {
            LogPrintf("Encrypting Sapling Spending Key failed!!!\n");
            return false;
        }

        //Encrypt metadata
        CKeyMetadata metadata = mapSaplingZKeyMetadata[ivk];
        std::vector<unsigned char> vchCryptedMetaData;
        CKeyingMaterial vchMetaData = SerializeForEncryptionInput(metadata);
        if (!EncryptSerializedWalletObjects(vchMetaData, chash, vchCryptedMetaData)) {
            LogPrintf("Encrypting Sapling Spending Key metadata failed!!!\n");
            return false;
        }

        if (!CCryptoKeyStore::AddCryptedSaplingSpendingKey(extfvk, vchCryptedSpendingKey)) {
            LogPrintf("Adding encrypted Sapling Spending Key failed!!!\n");
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedSaplingZKey(extfvk, vchCryptedSpendingKey, vchCryptedMetaData)) {
            LogPrintf("Writing encrypted Sapling Spending Key failed!!!\n");
            return false;
        }
    }

    nTimeFirstKey = 1; // No birthday information for viewing keys.
    return true;
}

// Add payment address -> incoming viewing key map entry
bool CWallet::AddSaplingIncomingViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr)) {
        return false;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteSaplingPaymentAddress(ivk, addr);
    } else {

        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = HashWithFP(addr);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(addr, ivk);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            LogPrintf("Encrypting Diversified Address failed!!!\n");
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedSaplingPaymentAddress(addr, chash, vchCryptedSecret);

    }

    return true;
}

bool CWallet::AddSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    AssertLockHeld(cs_wallet);

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!CCryptoKeyStore::AddSaplingExtendedFullViewingKey(extfvk)) {
        return false;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteSaplingExtendedFullViewingKey(extfvk);
    } else {

        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = extfvk.fvk.GetFingerprint();
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(extfvk);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedSaplingExtendedFullViewingKey(extfvk, vchCryptedSecret);
    }
}



bool CWallet::AddSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!CCryptoKeyStore::AddSaplingDiversifiedAddress(addr, ivk, path)) {
        return false;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteSaplingDiversifiedAddress(addr, ivk, path);
    }
    else {

        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = HashWithFP(addr);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(addr, ivk, path);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedSaplingDiversifiedAddress(addr, chash, vchCryptedSecret);

    }

    return true;
}

bool CWallet::AddLastDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddLastDiversifierUsed(ivk, path)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteLastDiversifierUsed(ivk, path);
    }
    else {

        uint256 chash = HashWithFP(ivk);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(ivk,path);
        std::vector<unsigned char> vchCryptedSecret;

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteLastCryptedDiversifierUsed(chash, ivk, vchCryptedSecret);

    }

    return true;
}

// Returns a loader that can be used to read an Sapling note commitment
// tree from a stream into the Sapling wallet.
SaplingWalletNoteCommitmentTreeLoader CWallet::GetSaplingNoteCommitmentTreeLoader() {
    return SaplingWalletNoteCommitmentTreeLoader(saplingWallet);
}

// Add spending key to keystore and persist to disk
bool CWallet::AddSproutZKey(const libzcash::SproutSpendingKey &key)
{
    AssertLockHeld(cs_wallet); // mapSproutZKeyMetadata
    auto addr = key.address();

    if (!CCryptoKeyStore::AddSproutSpendingKey(key))
        return false;

    // check if we need to remove from viewing keys
    if (HaveSproutViewingKey(addr))
        RemoveSproutViewingKey(key.viewing_key());

    if (!fFileBacked)
        return true;

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteZKey(addr,
                                                  key,
                                                  mapSproutZKeyMetadata[addr]);
    }
    return true;
}

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey(): AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    // check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);

    if (!fFileBacked)
        return true;

    if (!IsCrypted()) {
        if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) {
            LogPrintf("Adding Transparent Spending Key failed!!!\n");
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()])) {
            LogPrintf("Writing Transparent Spending Key failed!!!\n");
            return false;
        }
    } else {

        //Encrypt Key
        std::vector<unsigned char> vchCryptedSpendingKey;
        CKeyingMaterial vchSpendingKey(secret.begin(), secret.end());
        if (!EncryptSerializedWalletObjects(vchSpendingKey, pubkey.GetHash(), vchCryptedSpendingKey)) {
            LogPrintf("Encrypting Transparent Spending Key failed!!!\n");
            return false;
        }

        //Add to in-memory structures
        if (!CCryptoKeyStore::AddCryptedKey(pubkey, vchCryptedSpendingKey)) {
            LogPrintf("Adding encrypted Transparent Spending Key failed!!!\n");
            return false;
        }

        //Encrypt Key for saving to Disk
        std::vector<unsigned char> vchCryptedSpendingKeySave;
        uint256 chash = HashWithFP(pubkey);
        CKeyingMaterial vchSpendingKeySave = SerializeForEncryptionInput(pubkey, vchCryptedSpendingKey);
        if (!EncryptSerializedWalletObjects(vchSpendingKeySave, chash, vchCryptedSpendingKeySave)) {
            LogPrintf("Encrypting Transparent Spending Key for Disk failed!!!\n");
            return false;
        }

        //Encrypt metadata
        CKeyMetadata metadata = mapKeyMetadata[pubkey.GetID()];
        std::vector<unsigned char> vchCryptedMetaData;
        CKeyingMaterial vchMetaData = SerializeForEncryptionInput(pubkey, metadata);
        if (!EncryptSerializedWalletObjects(vchMetaData, chash, vchCryptedMetaData)) {
            LogPrintf("Encrypting Transparent Spending Key metadata failed!!!\n");
            return false;
        }

        //Write to Disk
        if (!CWalletDB(strWalletFile).WriteCryptedKey(pubkey, vchCryptedSpendingKeySave, chash, vchCryptedMetaData)) {
            LogPrintf("Writing encrypted Transparent Sapling Spending Key failed!!!\n");
            return false;
        }
    }
    return true;
}

bool CWallet::LoadKey(const CKey& key, const CPubKey &pubkey)
{
    return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
}

bool CWallet::LoadCryptedKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    AssertLockHeld(cs_wallet);
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    CPubKey vchPubKey;
    std::vector<unsigned char> vchCryptedSpendingKey;
    DeserializeFromDecryptionOutput(vchSecret, vchPubKey, vchCryptedSpendingKey);
    if (HashWithFP(vchPubKey) != chash) {
        return false;
    }

    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSpendingKey);
}

bool CWallet::AddCryptedSproutSpendingKey(
    const libzcash::SproutPaymentAddress &address,
    const libzcash::ReceivingKey &rk,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedSproutSpendingKey(address, rk, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption) {
            return pwalletdbEncryption->WriteCryptedZKey(address,
                                                         rk,
                                                         vchCryptedSecret,
                                                         mapSproutZKeyMetadata[address]);
        } else {
            return CWalletDB(strWalletFile).WriteCryptedZKey(address,
                                                             rk,
                                                             vchCryptedSecret,
                                                             mapSproutZKeyMetadata[address]);
        }
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKeyMetadata(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, CKeyMetadata &metadata)
{
    AssertLockHeld(cs_wallet);
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    CPubKey vchPubKey;
    DeserializeFromDecryptionOutput(vchSecret, vchPubKey, metadata);
    if (HashWithFP(vchPubKey) != chash) {
        return false;
    }

    return LoadKeyMetadata(vchPubKey, metadata);
}

bool CWallet::LoadZKeyMetadata(const SproutPaymentAddress &addr, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSproutZKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapSproutZKeyMetadata[addr] = meta;
    return true;
}

bool CWallet::LoadCryptedZKey(const libzcash::SproutPaymentAddress &addr, const libzcash::ReceivingKey &rk, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedSproutSpendingKey(addr, rk, vchCryptedSecret);
}

bool CWallet::LoadTempHeldCryptedData()
{
    AssertLockHeld(cs_wallet);

    std::map<uint256, libzcash::SaplingExtendedFullViewingKey> mapSaplingFingerPrints;

    //Get a map of all the saplingfullviewingkey fingerprints
    for (map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey>::iterator it = mapSaplingFullViewingKeys.begin(); it != mapSaplingFullViewingKeys.end(); ++it) {
          libzcash::SaplingExtendedFullViewingKey extfvk = (*it).second;
          mapSaplingFingerPrints[extfvk.fvk.GetFingerprint()] = extfvk;
    }

    for (map<uint256, std::vector<unsigned char>>::iterator it = mapTempHoldCryptedSaplingMetadata.begin(); it != mapTempHoldCryptedSaplingMetadata.end(); ++it) {
        for (map<uint256, libzcash::SaplingExtendedFullViewingKey>::iterator iit = mapSaplingFingerPrints.begin(); iit != mapSaplingFingerPrints.end(); iit++) {
            if ((*it).first == (*iit).first) {

                libzcash::SaplingExtendedFullViewingKey extfvk = (*iit).second;
                uint256 chash = (*it).first;
                std::vector<unsigned char> vchCryptedSecret = (*it).second;

                CKeyingMaterial vchSecret;
                if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
                    return false;
                }

                CKeyMetadata metadata;
                DeserializeFromDecryptionOutput(vchSecret, metadata);

                if (!LoadSaplingZKeyMetadata(extfvk.fvk.in_viewing_key(), metadata)) {
                    return false;
                }

                continue;
            }
        }
    }
    mapTempHoldCryptedSaplingMetadata.clear();
    return true;
}

bool CWallet::LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSaplingZKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    if (meta.seedFp == uint256()) {
        nTimeFirstKey = 1;
    }

    mapSaplingZKeyMetadata[ivk] = meta;
    return true;
}

bool CWallet::LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key)
{
    return CCryptoKeyStore::AddSaplingSpendingKey(key);
}

bool CWallet::LoadCryptedSaplingZKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    AssertLockHeld(cs_wallet);
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::SaplingExtendedSpendingKey extsk;
    DeserializeFromDecryptionOutput(vchSecret, extsk);
    extfvk = extsk.ToXFVK();

    if(extfvk.fvk.GetFingerprint() != chash) {
        return false;
    }

     return CCryptoKeyStore::AddCryptedSaplingSpendingKey(extfvk, vchCryptedSecret);
}

bool CWallet::LoadSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    return CCryptoKeyStore::AddSaplingExtendedFullViewingKey(extfvk);
}

bool CWallet::LoadCryptedSaplingExtendedFullViewingKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::SaplingExtendedFullViewingKey &extfvk)
{

    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    DeserializeFromDecryptionOutput(vchSecret, extfvk);
    if(extfvk.fvk.GetFingerprint() != chash) {
        return false;
    }

     return CCryptoKeyStore::AddSaplingExtendedFullViewingKey(extfvk);
}

bool CWallet::LoadSaplingPaymentAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk)
{
    return CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr);
}

bool CWallet::LoadCryptedSaplingPaymentAddress(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::SaplingPaymentAddress& addr)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::SaplingIncomingViewingKey ivk;
    DeserializeFromDecryptionOutput(vchSecret, addr, ivk);
    if(HashWithFP(addr) != chash) {
        return false;
    }

    return CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr);
}

bool CWallet::LoadSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddSaplingDiversifiedAddress(addr, ivk, path);
}

bool CWallet::LoadCryptedSaplingDiversifiedAddress(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::SaplingPaymentAddress addr;
    libzcash::SaplingIncomingViewingKey ivk;
    blob88 path;
    DeserializeFromDecryptionOutput(vchSecret, addr, ivk, path);
    if(HashWithFP(addr) != chash) {
        return false;
    }

    return CCryptoKeyStore::AddSaplingDiversifiedAddress(addr, ivk, path);
}

bool CWallet::LoadLastDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddLastDiversifierUsed(ivk, path);
}

bool CWallet::LoadLastCryptedDiversifierUsed(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::SaplingIncomingViewingKey ivk;
    blob88 path;
    DeserializeFromDecryptionOutput(vchSecret, ivk, path);
    if (HashWithFP(ivk) != chash) {
        return false;
    }

    return LoadLastDiversifierUsed(ivk, path);
}

bool CWallet::LoadZKey(const libzcash::SproutSpendingKey &key)
{
    return CCryptoKeyStore::AddSproutSpendingKey(key);
}

bool CWallet::AddSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    if (!CCryptoKeyStore::AddSproutViewingKey(vk)) {
        return false;
    }
    nTimeFirstKey = 1; // No birthday information for viewing keys.
    if (!fFileBacked) {
        return true;
    }
    return CWalletDB(strWalletFile).WriteSproutViewingKey(vk);
}

bool CWallet::RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveSproutViewingKey(vk)) {
        return false;
    }
    if (fFileBacked) {
        if (!CWalletDB(strWalletFile).EraseSproutViewingKey(vk)) {
            return false;
        }
    }

    return true;
}

bool CWallet::LoadSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    return CCryptoKeyStore::AddSproutViewingKey(vk);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;

    if (!fFileBacked)
        return true;

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
    }
    else {
      uint160 scriptId = Hash160(redeemScript);

      uint256 chash = HashWithFP(*(const CScriptBase*)(&redeemScript));

      CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
      ss << *(const CScriptBase*)(&redeemScript);
      CKeyingMaterial vchSecret(ss.begin(), ss.end());

      std::vector<unsigned char> vchCryptedSecret;
      if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
          LogPrintf("Encrypting CScripts  failed!!!\n");
          return false;
      }

      return CWalletDB(strWalletFile).WriteCryptedCScript(chash, scriptId, vchCryptedSecret);

    }

    return true;

}

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        std::string strAddr = EncodeDestination(CScriptID(redeemScript));
        LogPrintf("%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::LoadCryptedCScript(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedSecret(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    CScript redeemScript;
    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> *(CScriptBase*)(&redeemScript);

    return LoadCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript &dest)
{
    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddWatchOnly(dest))
        return false;

    nTimeFirstKey = 1; // No birthday information for watch-only keys.
    NotifyWatchonlyChanged(true);

    if (!fFileBacked)
        return true;

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteWatchOnly(dest);
    } else {
        uint256 chash = HashWithFP(*(const CScriptBase*)(&dest));

        CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << *(const CScriptBase*)(&dest);
        CKeyingMaterial vchSecret(ss.begin(), ss.end());

        std::vector<unsigned char> vchCryptedSecret;
        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            LogPrintf("Encrypting Watchs failed!!!\n");
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedWatchOnly(chash, dest, vchCryptedSecret);

    }

    return true;
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
        return false;
    if (!HaveWatchOnly())
        NotifyWatchonlyChanged(false);
    if (fFileBacked)
        if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
            return false;

    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

bool CWallet::LoadCryptedWatchOnly(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedSecret(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    CScript dest;
    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> *(CScriptBase*)(&dest);

    return LoadWatchOnly(dest);
}

bool CWallet::LoadSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    if (CCryptoKeyStore::AddSaplingWatchOnly(extfvk)) {
        NotifyWatchonlyChanged(true);
        return true;
    }

    return false;
}

bool CWallet::OpenWallet(const SecureString& strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::OpenWallet(vMasterKey)) {
                strOpeningWalletPassphrase = new SecureString(strWalletPassphrase);
                return true;
            }
        }
    }
    return false;
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                continue; // try another master key
            if (CCryptoKeyStore::Unlock(vMasterKey)) {
                SetBestChain(currentBlock, chainHeight);
                return true;
            }
        }
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        BOOST_FOREACH(MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::ChainTip(const CBlockIndex *pindex,
                       const CBlock *pblock,
                       SproutMerkleTree sproutTree,
                       SaplingMerkleTree saplingTree,
                       bool added)
{
    LOCK2(cs_main, cs_wallet);

    if (added) {
        IncrementSaplingWallet(pindex);
        // Prevent witness cache building && consolidation transactions
        // from being created when node is syncing after launch,
        // and also when node wakes up from suspension/hibernation and incoming blocks are old.
        bool initialDownloadCheck = IsInitialBlockDownload();
        if (!initialDownloadCheck &&
            pblock->GetBlockTime() > GetTime() - 8640) //Last 144 blocks 2.4 * 60 * 60
        {
            BuildWitnessCache(pindex, false);
            RunSaplingConsolidation(pindex->nHeight);
            RunSaplingSweep(pindex->nHeight);
            while(DeleteWalletTransactions(pindex, false)) {}
        } else {
            //Build intial witnesses on every block
            BuildWitnessCache(pindex, true);
            if (initialDownloadCheck && pindex->nHeight % fDeleteInterval == 0) {
                while(DeleteWalletTransactions(pindex, false)) {}
            }

            //Build full witness cache 1 hour before IsInitialBlockDownload() unlocks
            if (pblock->GetBlockTime() > GetTime() - nMaxTipAge - 3600) {
                BuildWitnessCache(pindex, false);
            }
        }

    } else {
        DecrementSaplingWallet(pindex);
        DecrementNoteWitnesses(pindex);
        UpdateNullifierNoteMapForBlock(pblock);
    }

    // SetBestChain() can be expensive for large wallets, so do only
    // this sometimes; the wallet state will be brought up to date
    // during rescanning on startup.
    {
        // The locator must be derived from the pindex used to increment
        // the witnesses above; pindex can be behind chainActive.Tip().
        // set the currentBlock and chainHeight in memory even if the wallet is not flushed to Disk
        // Needed to report to the GUI on Locked wallets
        currentBlock = chainActive.GetLocator(pindex);
        chainHeight = pindex->nHeight;
    }
    int64_t nNow = GetTimeMicros();
    if (nLastSetChain == 0) {
        // Don't flush during startup.
        nLastSetChain = nNow;
    }
    if (++nSetChainUpdates >= WITNESS_WRITE_UPDATES ||
            nLastSetChain + (int64_t)WITNESS_WRITE_INTERVAL * 1000000 < nNow || fRunSetBestChain) {
        nLastSetChain = nNow;
        nSetChainUpdates = 0;

        SetBestChain(currentBlock, chainHeight);
    }

}

void CWallet::RunSaplingSweep(int blockHeight) {
    if (!NetworkUpgradeActive(blockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        return;
    }
    AssertLockHeld(cs_wallet);
    if (!fSaplingSweepEnabled) {
        return;
    }

    if (nextSweep > blockHeight) {
        return;
    }

    //Don't Run if consolidation will run soon.
    if (fSaplingConsolidationEnabled && nextConsolidation - 15 <= blockHeight) {
        return;
    }

    //Don't Run While consolidation is running.
    if (fConsolidationRunning) {
        return;
    }

    fSweepRunning = true;

    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> lastOperation = q->getOperationForId(saplingSweepOperationId);
    if (lastOperation != nullptr) {
        lastOperation->cancel();
    }
    pendingSaplingSweepTxs.clear();
    std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_sweeptoaddress(blockHeight + 5));
    saplingSweepOperationId = operation->getId();
    q->addOperation(operation);
}


void CWallet::RunSaplingConsolidation(int blockHeight) {
    if (!NetworkUpgradeActive(blockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        return;
    }
    AssertLockHeld(cs_wallet);
    if (!fSaplingConsolidationEnabled) {
        return;
    }

    if (nextConsolidation > blockHeight) {
        return;
    }

    //Don't Run While sweep is running.
    if (fSweepRunning) {
        return;
    }

    fConsolidationRunning = true;

    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> lastOperation = q->getOperationForId(saplingConsolidationOperationId);
    if (lastOperation != nullptr) {
        lastOperation->cancel();
    }
    pendingSaplingConsolidationTxs.clear();
    std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_saplingconsolidation(blockHeight + 5));
    saplingConsolidationOperationId = operation->getId();
    q->addOperation(operation);

}

void CWallet::CommitAutomatedTx(const CTransaction& tx) {
  CWalletTx wtx(this, tx);
  CReserveKey reservekey(pwalletMain);
  CommitTransaction(wtx, reservekey);
}

void CWallet::SetBestChain(const CBlockLocator& loc, const int& height)
{
    //Only execute in online mode, otherwise ignore this function
    //for cold storage offline mode
    if (nMaxConnections > 0 ) {
        AssertLockHeld(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        SetBestChainINTERNAL(walletdb, loc, height);
    }
}

void CWallet::SetWalletBirthday(int nHeight)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteWalletBirthday(nHeight);
}

std::set<std::pair<libzcash::PaymentAddress, uint256>> CWallet::GetNullifiersForAddresses(
        const std::set<libzcash::PaymentAddress> & addresses)
{
    std::set<std::pair<libzcash::PaymentAddress, uint256>> nullifierSet;
    // Sapling ivk -> list of addrs map
    // (There may be more than one diversified address for a given ivk.)
    std::map<libzcash::SaplingIncomingViewingKey, std::vector<libzcash::SaplingPaymentAddress>> ivkMap;
    for (const auto & addr : addresses) {
        auto saplingAddr = boost::get<libzcash::SaplingPaymentAddress>(&addr);
        if (saplingAddr != nullptr) {
            libzcash::SaplingIncomingViewingKey ivk;
            this->GetSaplingIncomingViewingKey(*saplingAddr, ivk);
            ivkMap[ivk].push_back(*saplingAddr);
        }
    }
    for (const auto & txPair : mapWallet) {
        // Sprout
        for (const auto & noteDataPair : txPair.second.mapSproutNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & address = noteData.address;
            if (nullifier && addresses.count(address)) {
                nullifierSet.insert(std::make_pair(address, nullifier.get()));
            }
        }
        // Sapling
        for (const auto & noteDataPair : txPair.second.mapSaplingNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & ivk = noteData.ivk;
            if (nullifier && ivkMap.count(ivk)) {
                for (const auto & addr : ivkMap[ivk]) {
                    nullifierSet.insert(std::make_pair(addr, nullifier.get()));
                }
            }
        }
    }
    return nullifierSet;
}

bool CWallet::IsNoteSproutChange(
        const std::set<std::pair<libzcash::PaymentAddress, uint256>> & nullifierSet,
        const PaymentAddress & address,
        const JSOutPoint & jsop)
{
    // A Note is marked as "change" if the address that received it
    // also spent Notes in the same transaction. This will catch,
    // for instance:
    // - Change created by spending fractions of Notes (because
    //   z_sendmany sends change to the originating z-address).
    // - "Chaining Notes" used to connect JoinSplits together.
    // - Notes created by consolidation transactions (e.g. using
    //   z_mergetoaddress).
    // - Notes sent from one address to itself.
    for (const JSDescription & jsd : mapWallet[jsop.hash].vjoinsplit) {
        for (const uint256 & nullifier : jsd.nullifiers) {
            if (nullifierSet.count(std::make_pair(address, nullifier))) {
                return true;
            }
        }
    }
    return false;
}

bool CWallet::IsNoteSaplingChange(const std::set<std::pair<libzcash::PaymentAddress, uint256>> & nullifierSet,
        const libzcash::PaymentAddress & address,
        const SaplingOutPoint & op)
{
    // A Note is marked as "change" if the address that received it
    // also spent Notes in the same transaction. This will catch,
    // for instance:
    // - Change created by spending fractions of Notes (because
    //   z_sendmany sends change to the originating z-address).
    // - Notes created by consolidation transactions (e.g. using
    //   z_mergetoaddress).
    // - Notes sent from one address to itself.
    for (const SpendDescription &spend : mapWallet[op.hash].vShieldedSpend) {
        if (nullifierSet.count(std::make_pair(address, spend.nullifier))) {
            return true;
        }
    }
    return false;
}
bool CWallet::SetWalletCrypted(CWalletDB* pwalletdb) {
    LOCK(cs_wallet);

    if (fFileBacked)
    {
            return pwalletdb->WriteIsCrypted(true);
    }

    return false;
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

set<uint256> CWallet::GetConflicts(const uint256& txid) const
{
    set<uint256> result;
    AssertLockHeld(cs_wallet);

    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(txid);
    if (it == mapWallet.end())
        return result;
    const CWalletTx& wtx = it->second;

    std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

    BOOST_FOREACH(const CTxIn& txin, wtx.vin)
    {
        if (mapTxSpends.count(txin.prevout) <= 1)
            continue;  // No conflict if zero or one spends
        range = mapTxSpends.equal_range(txin.prevout);
        for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
            result.insert(it->second);
    }

    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_n;

    for (const JSDescription& jsdesc : wtx.vjoinsplit) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (mapTxSproutNullifiers.count(nullifier) <= 1) {
                continue;  // No conflict if zero or one spends
            }
            range_n = mapTxSproutNullifiers.equal_range(nullifier);
            for (TxNullifiers::const_iterator it = range_n.first; it != range_n.second; ++it) {
                result.insert(it->second);
            }
        }
    }

    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_o;

    for (const SpendDescription &spend : wtx.vShieldedSpend) {
        uint256 nullifier = spend.nullifier;
        if (mapTxSaplingNullifiers.count(nullifier) <= 1) {
            continue;  // No conflict if zero or one spends
        }
        range_o = mapTxSaplingNullifiers.equal_range(nullifier);
        for (TxNullifiers::const_iterator it = range_o.first; it != range_o.second; ++it) {
            result.insert(it->second);
        }
    }
    return result;
}

void CWallet::Flush(bool shutdown)
{
    bitdb->Flush(shutdown);
}

bool CWallet::Verify(const string& walletFile, string& warningString, string& errorString)
{
    if (!bitdb->Open(GetDataDir()))
    {
        // try moving the database env out of the way
        boost::filesystem::path pathDatabase = GetDataDir() / "database";
        boost::filesystem::path pathDatabaseBak = GetDataDir() / strprintf("database.%d.bak", GetTime());
        try {
            boost::filesystem::rename(pathDatabase, pathDatabaseBak);
            LogPrintf("Moved old %s to %s. Retrying.\n", pathDatabase.string(), pathDatabaseBak.string());
        } catch (const boost::filesystem::filesystem_error&) {
            // failure is ok (well, not really, but it's not worse than what we started with)
        }

        // try again
        if (!bitdb->Open(GetDataDir())) {
            // if it still fails, it probably means we can't even create the database env
            string msg = strprintf(_("Error initializing wallet database environment %s!"), GetDataDir());
            errorString += msg;
            return true;
        }
    }

    if (GetBoolArg("-salvagewallet", false))
    {
        // Recover readable keypairs:
        if (!CWalletDB::Recover(*bitdb, walletFile, true))
            return false;
    }

    if (boost::filesystem::exists(GetDataDir() / walletFile))
    {
        CDBEnv::VerifyResult r = bitdb->Verify(walletFile, CWalletDB::Recover);
        if (r == CDBEnv::RECOVER_OK)
        {
            warningString += strprintf(_("Warning: wallet.dat corrupt, data salvaged!"
                                     " Original wallet.dat saved as wallet.{timestamp}.bak in %s; if"
                                     " your balance or transactions are incorrect you should"
                                     " restore from a backup."), GetDataDir());
        }
        if (r == CDBEnv::RECOVER_FAIL)
            errorString += _("wallet.dat corrupt, salvage failed");
    }

    return true;
}

template <class T>
void CWallet::SyncMetaData(pair<typename TxSpendMap<T>::iterator, typename TxSpendMap<T>::iterator> range)
{
    // We want all the wallet transactions in range to have the same metadata as
    // the oldest (smallest nOrderPos).
    // So: find smallest nOrderPos:

    int nMinOrderPos = std::numeric_limits<int>::max();
    const CWalletTx* copyFrom = NULL;
    for (typename TxSpendMap<T>::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;

        const auto itmw = mapWallet.find(hash);
        if (itmw != mapWallet.end()) {
            int n = itmw->second.nOrderPos;
            if (n < nMinOrderPos)
            {
                nMinOrderPos = n;
                copyFrom = &(itmw->second);
            }
        }
    }
    // Now copy data from copyFrom to rest:
    for (typename TxSpendMap<T>::iterator it = range.first; it != range.second; ++it)
    {
        const uint256& hash = it->second;

        const auto itmw = mapWallet.find(hash);
        if (itmw != mapWallet.end()) {

            CWalletTx* copyTo = &(itmw->second);
            if (copyFrom == copyTo) continue;
            copyTo->mapValue = copyFrom->mapValue;
            // mapSproutNoteData and mapSaplingNoteData not copied on purpose
            // (it is always set correctly for each CWalletTx)
            copyTo->vOrderForm = copyFrom->vOrderForm;
            // fTimeReceivedIsTxTime not copied on purpose
            // nTimeReceived not copied on purpose
            copyTo->nTimeSmart = copyFrom->nTimeSmart;
            copyTo->fFromMe = copyFrom->fFromMe;
            copyTo->strFromAccount = copyFrom->strFromAccount;
            // nOrderPos not copied on purpose
            // cached members not copied on purpose
        }
    }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0)
            return true; // Spent
    }
    return false;
}

unsigned int CWallet::GetSpendDepth(const uint256& hash, unsigned int n) const
{
    const COutPoint outpoint(hash, n);
    pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
    range = mapTxSpends.equal_range(outpoint);

    for (TxSpends::const_iterator it = range.first; it != range.second; ++it)
    {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0)
            return mit->second.GetDepthInMainChain(); // Spent
    }
    return 0;
}

/**
 * Note is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSproutSpent(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxSproutNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return true; // Spent
        }
    }
    return false;
}

unsigned int CWallet::GetSproutSpendDepth(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxSproutNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return mit->second.GetDepthInMainChain(); // Spent
        }
    }
    return 0;
}

bool CWallet::IsSaplingSpent(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return true; // Spent
        }
    }
    return false;
}

unsigned int CWallet::GetSaplingSpendDepth(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return mit->second.GetDepthInMainChain(); // Spent
        }
    }
    return 0;
}

void CWallet::AddToTransparentSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    mapTxSpends.insert(make_pair(outpoint, wtxid));

    pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData<COutPoint>(range);
}

void CWallet::AddToSproutSpends(const uint256& nullifier, const uint256& wtxid)
{
    mapTxSproutNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxSproutNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

void CWallet::AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid)
{
    mapTxSaplingNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

void CWallet::RemoveFromSpends(const uint256& wtxid)
{
    RemoveFromTransparentSpends(wtxid);
    RemoveFromSproutSpends(wtxid);
    RemoveFromSaplingSpends(wtxid);
}

void CWallet::RemoveFromTransparentSpends(const uint256& wtxid)
{
    if (mapTxSpends.size() > 0)
    {
        std::multimap<COutPoint, uint256>::const_iterator itr = mapTxSpends.cbegin();
        while (itr != mapTxSpends.cend())
        {
            if (itr->second == wtxid)
            {
                itr = mapTxSpends.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
}

void CWallet::RemoveFromSproutSpends(const uint256& wtxid)
{
    if (mapTxSproutNullifiers.size() > 0)
    {
        std::multimap<uint256, uint256>::const_iterator itr = mapTxSproutNullifiers.cbegin();
        while (itr != mapTxSproutNullifiers.cend())
        {
            if (itr->second == wtxid)
            {
                itr = mapTxSproutNullifiers.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
}

void CWallet::RemoveFromSaplingSpends(const uint256& wtxid)
{
    if (mapTxSaplingNullifiers.size() > 0)
    {
        std::multimap<uint256, uint256>::const_iterator itr = mapTxSaplingNullifiers.cbegin();
        while (itr != mapTxSaplingNullifiers.cend())
        {
            if (itr->second == wtxid)
            {
                itr = mapTxSaplingNullifiers.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
}

void CWallet::LoadArcTxs(const uint256& wtxid, const ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtxid] = arcTxPt;
}

void CWallet::AddToArcTxs(const uint256& wtxid, ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtxid] = arcTxPt;

    uint256 txid = wtxid;
    RpcArcTransaction arcTx;

    getRpcArcTx(txid, arcTx, true, rescan);

    arcTxPt.ivks = arcTx.ivks;
    arcTxPt.ovks = arcTx.ovks;
    mapArcTxs[wtxid] = arcTxPt;

    //Update Address txid map
    for (auto it = arcTx.addresses.begin(); it != arcTx.addresses.end(); ++it) {
        std::string addr = *it;

        std::set<uint256> txids;
        std::map<std::string, std::set<uint256>>::iterator ait;

        ait = mapAddressTxids.find(addr);
        if (ait != mapAddressTxids.end()) {
            txids = ait->second;
        }

        txids.insert(txid);

        mapAddressTxids[addr] = txids;

    }
}

void CWallet::AddToArcTxs(const CWalletTx& wtx, int txHeight, ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtx.GetHash()] = arcTxPt;

    CWalletTx tx = wtx;
    RpcArcTransaction arcTx;

    getRpcArcTxSaplingKeys(wtx, txHeight, arcTx, true);

    arcTxPt.ivks = arcTx.ivks;
    arcTxPt.ovks = arcTx.ovks;
    mapArcTxs[wtx.GetHash()] = arcTxPt;

    //Update Address txid map
    for (auto it = arcTx.addresses.begin(); it != arcTx.addresses.end(); ++it) {
        std::string addr = *it;
        std::set<uint256> txids;
        std::map<std::string, std::set<uint256>>::iterator ait;

        ait = mapAddressTxids.find(addr);
        if (ait != mapAddressTxids.end()) {
            txids = ait->second;
        }

        txids.insert(arcTx.txid);

        mapAddressTxids[addr] = txids;

    }
}

void CWallet::AddToArcJSOutPoints(const uint256& nullifier, const JSOutPoint& op)
{
    mapArcJSOutPoints[nullifier] = op;
}

void CWallet::AddToArcSaplingOutPoints(const uint256& nullifier, const SaplingOutPoint& op)
{
    mapArcSaplingOutPoints[nullifier] = op;
}

void CWallet::AddToSpends(const uint256& wtxid)
{
    assert(mapWallet.count(wtxid));
    CWalletTx& thisTx = mapWallet[wtxid];
    if (thisTx.IsCoinBase()) // Coinbases don't spend anything!
        return;

    for (const CTxIn& txin : thisTx.vin) {
        AddToTransparentSpends(txin.prevout, wtxid);
    }
    for (const JSDescription& jsdesc : thisTx.vjoinsplit) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            AddToSproutSpends(nullifier, wtxid);
        }
    }
    for (const SpendDescription &spend : thisTx.vShieldedSpend) {
        AddToSaplingSpends(spend.nullifier, wtxid);
    }
}

void CWallet::ClearNoteWitnessCache()
{
    LOCK(cs_wallet);
    for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {
        for (mapSproutNoteData_t::value_type& item : wtxItem.second.mapSproutNoteData) {
            item.second.witnesses.clear();
            item.second.witnessHeight = -1;
        }
        for (mapSaplingNoteData_t::value_type& item : wtxItem.second.mapSaplingNoteData) {
            item.second.witnesses.clear();
            item.second.witnessHeight = -1;
        }
    }
}

void CWallet::DecrementNoteWitnesses(const CBlockIndex* pindex)
{
    AssertLockHeld(cs_wallet);

    extern int32_t KOMODO_REWIND;

    for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {
        //Sprout
        for (auto& item : wtxItem.second.mapSproutNoteData) {
            auto* nd = &(item.second);
            if (nd->nullifier && pwalletMain->GetSproutSpendDepth(*item.second.nullifier) <= WITNESS_CACHE_SIZE) {
              // Only decrement witnesses that are not above the current height
                if (nd->witnessHeight <= pindex->nHeight) {
                    if (nd->witnesses.size() > 1) {
                        // indexHeight is the height of the block being removed, so
                        // the new witness cache height is one below it.
                        nd->witnesses.pop_front();
                        nd->witnessHeight = pindex->nHeight - 1;
                    }
                }
            }
        }
        //Sapling
        for (auto& item : wtxItem.second.mapSaplingNoteData) {
            auto* nd = &(item.second);
            if (nd->nullifier && pwalletMain->GetSaplingSpendDepth(*item.second.nullifier) <= WITNESS_CACHE_SIZE) {
                // Only decrement witnesses that are not above the current height
                if (nd->witnessHeight <= pindex->nHeight) {
                    if (nd->witnesses.size() > 1) {
                        // indexHeight is the height of the block being removed, so
                        // the new witness cache height is one below it.
                        nd->witnesses.pop_front();
                        nd->witnessHeight = pindex->nHeight - 1;
                    }
                }
            }
        }
    }
    assert(KOMODO_REWIND != 0 || WITNESS_CACHE_SIZE != _COINBASE_MATURITY+10);
}

void CWallet::IncrementSaplingWallet(const CBlockIndex* pindex) {

    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int64_t nNow2 = GetTime();
    bool rebuildWallet = false;
    int nMinimumHeight = pindex->nHeight;
    int lastCheckpoint = saplingWallet.GetLastCheckpointHeight();

    if (NetworkUpgradeActive(pindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {

        //Rebuild if wallet does not exisit
        if (lastCheckpoint<0) {
            rebuildWallet = true;
        }

        //Rebuild if wallet is out of sync with the blockchain
        if (lastCheckpoint != pindex->nHeight - 1 ) {
            rebuildWallet = true;
        }

        //Rebuild wallet if anchor does not match
        if (lastCheckpoint == pindex->nHeight - 1) {
            if (pindex->pprev->hashFinalSaplingRoot != saplingWallet.GetLatestAnchor()) {
                rebuildWallet = true;
            }
        }

        //Rebuild
        if (rebuildWallet) {
            std::map<uint256, CWalletTx*> mapWalletRebuild;

            //Collect valid transactions
            for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
                uint256 txid = (*it).first;
                CWalletTx *pwtx = &(*it).second;

                //Exclude transactions with no Sapling Data
                if (pwtx->mapSaplingNoteData.empty()) {
                    continue;
                }

                //Exclude transactions that are in invalid blocks
                if (mapBlockIndex.count(pwtx->hashBlock) == 0) {
                    continue;
                }

                //Exclude unconfirmed transactions
                if (pwtx->GetDepthInMainChain() <= 0) {
                    continue;
                }

                //Include transaction for rebuild
                mapWalletRebuild[txid] = pwtx;

            }

            //Determine Start Height of Sapling Wallet
            for (map<uint256, CWalletTx*>::iterator it = mapWalletRebuild.begin(); it != mapWalletRebuild.end(); ++it) {
                CWalletTx* pwtx = (*it).second;

                int txHeight = chainActive.Tip()->nHeight - pwtx->GetDepthInMainChain();
                //Check Note data for minimum height
                for (mapSaplingNoteData_t::value_type& item : pwtx->mapSaplingNoteData) {
                    nMinimumHeight = SaplingWitnessMinimumHeight(*item.second.nullifier, txHeight, nMinimumHeight);
                }
            }

            //No transactions exists to begin wallet at this hieght
            if (nMinimumHeight > pindex->nHeight) {
                saplingWallet.Reset();
                return;
            }

            // Set Starting Values
            CBlockIndex* pblockindex = chainActive[nMinimumHeight];

            //Create a new wallet
            SaplingMerkleFrontier saplingFrontierTree;
            pcoinsTip->GetSaplingFrontierAnchorAt(pblockindex->pprev->hashFinalSaplingRoot, saplingFrontierTree);
            saplingWallet.Reset();
            saplingWallet.InitNoteCommitmentTree(saplingFrontierTree);

            //Loop thru blocks to rebuild saplingWallet commitment tree
            while (pblockindex) {

                //Create Checkpoint before incrementing wallet
                saplingWallet.CheckpointNoteCommitmentTree(pblockindex->nHeight);

                if (GetTime() >= nNow2 + 60) {
                    nNow2 = GetTime();
                    LogPrintf("Building Witnesses for block %d. Progress=%f\n", pblockindex->nHeight, Checkpoints::GuessVerificationProgress(Params().Checkpoints(), pblockindex));
                }

                //exit loop if trying to shutdown
                if (ShutdownRequested()) {
                    break;
                }

                //Retrieve the full block to get all of the transaction commitments
                CBlock block;
                ReadBlockFromDisk(block, pblockindex, 1);
                CBlock *pblock = &block;

                for (int i = 0; i < pblock->vtx.size(); i++) {
                    uint256 txid = pblock->vtx[i].GetHash();
                    auto it = mapWalletRebuild.find(txid);

                    //Use single output appending for transaction that belong to the wallet so that they can be marked
                    if (it != mapWalletRebuild.end()) {
                        saplingWallet.CreateEmptyPositionsForTxid(pindex->nHeight, txid);
                        CWalletTx *pwtx = (*it).second;
                        for (int j = 0; j < pblock->vtx[i].vShieldedOutput.size(); j++) {
                            SaplingOutPoint op = SaplingOutPoint(txid, j);
                            auto opit = pwtx->mapSaplingNoteData.find(op);

                            if (opit != pwtx->mapSaplingNoteData.end()) {
                                saplingWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, pblock->vtx[i].vShieldedOutput[j], true);
                            } else {
                                saplingWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, pblock->vtx[i].vShieldedOutput[j], false);
                            }
                        }
                    } else {
                        //No transactions in this tx belong to the wallet, use full tx appending
                        saplingWallet.ClearPositionsForTxid(txid);
                        saplingWallet.AppendNoteCommitments(pblockindex->nHeight,pblock->vtx[i],i);
                    }
                }

                //Check completeness
                if (pblockindex == pindex)
                    break;

                //Set Variables for next loop
                pblockindex = chainActive.Next(pblockindex);
            }

        } else {

            //Retrieve the full block to get all of the transaction commitments
            CBlock block;
            ReadBlockFromDisk(block, pindex, 1);
            CBlock *pblock = &block;

            //Create Checkpoint before incrementing wallet
            saplingWallet.CheckpointNoteCommitmentTree(pindex->nHeight);

            for (int i = 0; i < pblock->vtx.size(); i++) {
                uint256 txid = pblock->vtx[i].GetHash();
                auto it = mapWallet.find(txid);

                //Use single output appending for transaction that belong to the wallet so that they can be marked
                if (it != mapWallet.end()) {
                    saplingWallet.CreateEmptyPositionsForTxid(pindex->nHeight, txid);
                    CWalletTx *pwtx = &(*it).second;
                    for (int j = 0; j < pblock->vtx[i].vShieldedOutput.size(); j++) {
                        SaplingOutPoint op = SaplingOutPoint(txid, j);
                        auto opit = pwtx->mapSaplingNoteData.find(op);

                        if (opit != pwtx->mapSaplingNoteData.end()) {
                            saplingWallet.AppendNoteCommitment(pindex->nHeight, txid, i, j, pblock->vtx[i].vShieldedOutput[j], true);
                        } else {
                            saplingWallet.AppendNoteCommitment(pindex->nHeight, txid, i, j, pblock->vtx[i].vShieldedOutput[j], false);
                        }
                    }
                } else {
                    //No transactions in this tx belong to the wallet, use full tx appending
                    saplingWallet.ClearPositionsForTxid(txid);
                    saplingWallet.AppendNoteCommitments(pindex->nHeight,pblock->vtx[i],i);
                }
            }
        }
    }

    // This assertion slows scanning for blocks with few shielded transactions by an
    // order of magnitude. It is only intended as a consistency check between the node
    // and wallet computing trees. Commented out until we have figured out what is
    // causing the slowness and fixed it.
    // https://github.com/zcash/zcash/issues/6052
    // LogPrintf("Chain Height %i\n", pindex->nHeight);
    // LogPrintf("Sapling Hash Root   - %s\n", pindex->hashFinalSaplingRoot.ToString());
    // LogPrintf("Sapling Wallet Root - %s\n\n", saplingWallet.GetLatestAnchor().ToString());
    // LogPrintf("New Checkpoint %i\n", saplingWallet.GetLastCheckpointHeight());

}

void CWallet::DecrementSaplingWallet(const CBlockIndex* pindex) {

      uint32_t uResultHeight{0};
      assert(pindex->nHeight >= 1);
      assert(saplingWallet.Rewind(pindex->nHeight - 1, uResultHeight));
      assert(uResultHeight == pindex->nHeight - 1);
      // If we have no checkpoints after the rewind, then the latest anchor of the
      // wallet's Orchard note commitment tree will be in an indeterminate state and it
      // will be overwritten in the next `IncrementNoteWitnesses` call, so we can skip
      // the check against `hashFinalOrchardRoot`.

      auto walletLastCheckpointHeight = saplingWallet.GetLastCheckpointHeight();
      if (saplingWallet.GetLastCheckpointHeight()>0) {
          assert(pindex->pprev->hashFinalSaplingRoot == saplingWallet.GetLatestAnchor());
      }

}


template<typename NoteData>
void ClearSingleNoteWitnessCache(NoteData* nd)
{
    nd->witnesses.clear();
    nd->witnessHeight = -1;
    nd->witnessRootValidated = false;
}

int CWallet::SproutWitnessMinimumHeight(const uint256& nullifier, int nWitnessHeight, int nMinimumHeight)
{
    if (GetSproutSpendDepth(nullifier) <= WITNESS_CACHE_SIZE) {
        nMinimumHeight = min(nWitnessHeight, nMinimumHeight);
    }
    return nMinimumHeight;
}

int CWallet::SaplingWitnessMinimumHeight(const uint256& nullifier, int nWitnessHeight, int nMinimumHeight)
{
    if (GetSaplingSpendDepth(nullifier) <= WITNESS_CACHE_SIZE) {
        nMinimumHeight = min(nWitnessHeight, nMinimumHeight);
    }
    return nMinimumHeight;
}

int CWallet::VerifyAndSetInitialWitness(const CBlockIndex* pindex, bool witnessOnly)
{
  AssertLockHeld(cs_main);
  AssertLockHeld(cs_wallet);

  int nWitnessTxIncrement = 0;
  int nWitnessTotalTxCount = mapWallet.size();
  int nMinimumHeight = pindex->nHeight;
  bool walletHasNotes = false; //Use to enable z_sendmany when no notes are present

  for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {

    nWitnessTxIncrement += 1;

    if (wtxItem.second.mapSproutNoteData.empty() && wtxItem.second.mapSaplingNoteData.empty())
      continue;

    if (mapBlockIndex.count(wtxItem.second.hashBlock) == 0)
      continue;

    if (wtxItem.second.GetDepthInMainChain() > 0) {
      walletHasNotes = true;
      auto wtxHash = wtxItem.second.GetHash();
      int wtxHeight = mapBlockIndex[wtxItem.second.hashBlock]->nHeight;

      for (mapSaplingNoteData_t::value_type& item : wtxItem.second.mapSaplingNoteData) {

        auto op = item.first;
        auto* nd = &(item.second);
        CBlockIndex* pblockindex;
        uint256 blockRoot;
        uint256 witnessRoot;

        if (!nd->nullifier)
          ::ClearSingleNoteWitnessCache(nd);

        if (!nd->witnesses.empty() && nd->witnessHeight > 0) {

          //Skip all functions for validated witness while witness only = true
          if (nd->witnessRootValidated && witnessOnly)
            continue;

          //Skip Validation when witness root has been validated
          if (nd->witnessRootValidated) {
            nMinimumHeight = SaplingWitnessMinimumHeight(*item.second.nullifier, nd->witnessHeight, nMinimumHeight);
            continue;
          }

          //Skip Validation when witness height is greater that block height
          if (nd->witnessHeight > pindex->nHeight - 1) {
            nMinimumHeight = SaplingWitnessMinimumHeight(*item.second.nullifier, nd->witnessHeight, nMinimumHeight);
            continue;
          }

          //Validate the witness at the witness height
          witnessRoot = nd->witnesses.front().root();
          pblockindex = chainActive[nd->witnessHeight];
          blockRoot = pblockindex->hashFinalSaplingRoot;
          if (witnessRoot == blockRoot) {
            nd->witnessRootValidated = true;
            nMinimumHeight = SaplingWitnessMinimumHeight(*item.second.nullifier, nd->witnessHeight, nMinimumHeight);
            continue;
          }
        }

        //Clear witness Cache for all other scenarios
        pblockindex = chainActive[wtxHeight];
        ::ClearSingleNoteWitnessCache(nd);

        LogPrintf("Setting Inital Sapling Witness for tx %s, %i of %i\n", wtxHash.ToString(), nWitnessTxIncrement, nWitnessTotalTxCount);

        SaplingMerkleTree saplingTree;
        blockRoot = pblockindex->pprev->hashFinalSaplingRoot;
        pcoinsTip->GetSaplingAnchorAt(blockRoot, saplingTree);

        //Cycle through blocks and transactions building sapling tree until the commitment needed is reached
        const CBlock* pblock;
        CBlock block;
        ReadBlockFromDisk(block, pblockindex, 1);
        pblock = &block;

        for (const CTransaction& tx : block.vtx) {
          auto hash = tx.GetHash();

          // Sapling
          for (uint32_t i = 0; i < tx.vShieldedOutput.size(); i++) {
            const uint256& note_commitment = tx.vShieldedOutput[i].cmu;

            // Increment existing witness until the end of the block
            if (!nd->witnesses.empty()) {
              nd->witnesses.front().append(note_commitment);
            }

            //Only needed for intial witness
            if (nd->witnesses.empty()) {
              saplingTree.append(note_commitment);

              // If this is our note, witness it
              if (hash == wtxHash) {
                SaplingOutPoint outPoint {hash, i};
                if (op == outPoint) {
                  nd->witnesses.push_front(saplingTree.witness());
                }
              }
            }
          }
        }
        nd->witnessHeight = pblockindex->nHeight;
        UpdateSaplingNullifierNoteMapWithTx(wtxItem.second);
        nMinimumHeight = SaplingWitnessMinimumHeight(*item.second.nullifier, nd->witnessHeight, nMinimumHeight);
      }
    }
  }
  //enable z_sendmany when the wallet has no Notes
  if (!IsInitialBlockDownload()) {
      if (!walletHasNotes || nMinimumHeight == pindex->nHeight) {
          fInitWitnessesBuilt = true;
      }
  }

  return nMinimumHeight;
}

static void BuildSingleSaplingWitness(CWallet* wallet, std::vector<SaplingNoteData*> vNoteData, const CBlock *pblock, const int &nHeight, const int threadNumber)
{
    for (auto pnd : vNoteData) {
        if (pnd->witnessHeight == nHeight - 1) {
            SaplingWitness witness = pnd->witnesses.front();
            for (const CTransaction& ctx : pblock->vtx) {
                for (const OutputDescription &outdesc : ctx.vShieldedOutput) {
                    witness.append(outdesc.cmu);
                }
            }

            {
                LOCK(wallet->cs_wallet_threadedfunction);
                pnd->witnesses.push_front(witness);
                while (pnd->witnesses.size() > WITNESS_CACHE_SIZE) {
                    pnd->witnesses.pop_back();
                }

                pnd->witnessHeight = nHeight;
            }
        }
    }
}

void CWallet::BuildWitnessCache(const CBlockIndex* pindex, bool witnessOnly)
{
  AssertLockHeld(cs_main);
  AssertLockHeld(cs_wallet);

  //Get start time for logging
  int64_t nNow1 = GetTime();
  int64_t nNow2 = GetTime();
  int64_t nNow3 = GetTime();

  //Verifiy current witnesses again the SaplingHashRoot and/or set new initial witness
  int startHeight = VerifyAndSetInitialWitness(pindex, witnessOnly) + 1;

  //disable this function
  //return;

  if (startHeight > pindex->nHeight || witnessOnly) {
    return;
  }

  if (fCleanUpMode) {
      fBuilingWitnessCache = false;
      return;
  }

  //Disable RPC while IsInitialBlockDownload()
  if (IsInitialBlockDownload() && !fInitWitnessesBuilt) {
      fBuilingWitnessCache = true;
  }

  // Set Starting Values
  CBlockIndex* pblockindex = chainActive[startHeight];
  int height = chainActive.Height();

  //Show in UI
  bool uiShown = false;
  const CChainParams& chainParams = Params();
  double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false);
  double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);

  //Create a vector of vectors of SaplingNoteData that needs to be updated to pass to the async threads
  //The SaplingNoteData needs to be batched so async threads won't overwhelm the host system with threads
  //Prepare this before going into the blockindex loop to prevent rechecking on each loop
  std::vector<SaplingNoteData*> vNoteData;
  std::vector<std::vector<SaplingNoteData*>> vvNoteData;
  for (int i = 0; i < maxProcessingThreads; i++) {
      vvNoteData.emplace_back(vNoteData);
  }

  int t = 0;
  //Gather up notedata to be processed
  for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      CWalletTx* pwtx = &(it->second);
      if (pwtx->mapSaplingNoteData.empty()) {
          continue;
      }

      if (pwtx->GetDepthInMainChain() > 0) {
          int i = 0;
          for (mapSaplingNoteData_t::value_type& item : pwtx->mapSaplingNoteData) {
              auto* pnd = &(item.second);

              if (pnd->nullifier && GetSaplingSpendDepth(pnd->nullifier.value()) <= WITNESS_CACHE_SIZE) {

                  vvNoteData[t].emplace_back(pnd);
                  //Increment thread vector
                  t++;
                  //reset if tread vector is greater qty of threads being used
                  if (t >= vvNoteData.size()) {
                      t = 0;
                  }

              } else {
                  //remove all but the last witness to save disk space once the note has been spent
                  //and is deep enough to not be affected by chain reorg
                  while (pnd->witnesses.size() > 1) {
                      pnd->witnesses.pop_back();
                  }
              }
              i++;
          }
      }
  }

  //Retrieve the full block to get all of the transaction commitments
  CBlock block;
  ReadBlockFromDisk(block, pblockindex, 1);
  CBlock *pblock = &block;

  while (pblockindex) {

      //exit loop if trying to shutdown
      if (ShutdownRequested()) {
          break;
      }

      //Report Progress to the GUI and log file
      int witnessHeight = pblockindex->nHeight;
      if ((witnessHeight % 100 == 0 || GetTime() >= nNow1 + 15) && witnessHeight < height - 5 ) {
          nNow1 = GetTime();
          if (!uiShown) {
              uiShown = true;
              uiInterface.ShowProgress("Building Witnesses", 0, false);
          }
          scanperc = (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100);
          uiInterface.ShowProgress(_(("Building Witnesses for block " + std::to_string(witnessHeight) + "...").c_str()), std::max(1, std::min(99, scanperc)), false);
      }

      if (GetTime() >= nNow2 + 60) {
          nNow2 = GetTime();
          LogPrintf("Building Witnesses for block %d. Progress=%f\n", witnessHeight, Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex));
      }

      //flush progress to disk every 10 minutes
      if (GetTime() >= nNow3 + 600) {
          nNow3 = GetTime();
          LogPrintf("Writing witness building progress to the disk.\n");
          const CBlockLocator locatorBlock = chainActive.GetLocator(pindex);
          const int locatorHeight = pindex->nHeight;
          SetBestChain(locatorBlock, locatorHeight);
      }

      //Create 1 thread per group of notes
      std::vector<boost::thread*> witnessThreads;
      for (int i = 0; i < vvNoteData.size(); i++) {
          if (!vvNoteData[i].empty()) {
            witnessThreads.emplace_back(new boost::thread(BuildSingleSaplingWitness, this, vvNoteData[i], pblock, witnessHeight, i));
          }
      }

      //prefetch the next block while the worker threads are processing
      auto nNowReadTime = GetTimeMicros();
      CBlockIndex* pnextblockindex = chainActive.Next(pblockindex);
      CBlock nextBlock;
      if (pnextblockindex) {
          ReadBlockFromDisk(nextBlock, pnextblockindex, 1);
      }

      // Cleanup
      for (auto wthread : witnessThreads) {
          wthread->join();
          delete wthread;
      }

      witnessThreads.resize(0);

      //Check completeness
      if (pblockindex == pindex)
          break;

      //Set Variables for next loop
      pblockindex = chainActive.Next(pblockindex);
      block = nextBlock;
      pblock = &block;

  }

  if (uiShown) {
      uiInterface.ShowProgress(_("Witness Cache Complete..."), 100, false);
  }

  //clean up
  for (int i = 0; i < vvNoteData.size(); i++) {
      for (auto pnd : vNoteData) {
          delete pnd;
      }
      vvNoteData[i].resize(0);
  }
  vvNoteData.resize(0);

  fInitWitnessesBuilt = true;
  fBuilingWitnessCache = false;

}

bool CWallet::DecryptWalletTransaction(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& hash, CWalletTx& wtx) {

    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        LogPrintf("Decrypting Wallet Transaction failed!!!\n");
        return false;
    }
    DeserializeFromDecryptionOutput(vchSecret, hash, wtx);
    return HashWithFP(hash) == chash;
}

bool CWallet::DecryptWalletArchiveTransaction(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& txid, ArchiveTxPoint& arcTxPt) {

    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        LogPrintf("Decrypting ArchiveTxPoint failed!!!\n");
        return false;
    }
    DeserializeFromDecryptionOutput(vchSecret, txid, arcTxPt);
    return HashWithFP(txid) == chash;
}

bool CWallet::DecryptArchivedSaplingOutpoint(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& nullifier, SaplingOutPoint& op) {

    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }
    DeserializeFromDecryptionOutput(vchSecret, nullifier, op);
    return HashWithFP(nullifier) == chash;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    LOCK2(cs_wallet, cs_KeyStore);

    if (IsCrypted())
        return false;

    HDSeed seed;
    if (!GetHDSeed(seed)) {
        LogPrintf("HD seed not found. Exiting.\n");
        return false;
    }

    //Create a uniquie seedFP used to salt encryption hashes, DO NOT SAVE THIS TO THE WALLET!!!!
    //This will be used to salt hashes of know values such as transaction ids and public addresses
    seedEncyptionFP = seed.EncryptionFingerprint();

    CKeyingMaterial vMasterKey;

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey;

    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            if (!pwalletdbEncryption) {
                delete pwalletdbEncryption;
                pwalletdbEncryption = NULL;
            }
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        //Set fUseCrypto = true
        CCryptoKeyStore::SetDBCrypted();
        CCryptoKeyStore::UnlockUnchecked(vMasterKey);

        //Encrypt HDSeed
        if (!seed.IsNull()) {
            if (!SetHDSeed(seed)) {
                LogPrintf("Setting encrypted HDSeed failed!!!\n");
                return false;
            }
        }

        //Encrypt Primary Spending Key for diversified addresses
        if (primarySaplingSpendingKey != boost::none) {
            if (!SetPrimarySpendingKey(primarySaplingSpendingKey.get())) {
                LogPrintf("Setting encrypted primary sapling spending key failed!!!\n");
                return false;
            }
        }

        //Encrypt All wallet transactions
        for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
            CWalletTx wtx = (*it).second;
            uint256 txid = wtx.GetHash();

            std::vector<unsigned char> vchCryptedSecret;
            uint256 chash = HashWithFP(txid);
            CKeyingMaterial vchSecret = SerializeForEncryptionInput(txid, wtx);

            if (!EncryptSerializedWalletObjects(vMasterKey, vchSecret, chash, vchCryptedSecret)) {
                LogPrintf("Encrypting CWalletTx failed!!!\n");
                return false;
            }

            if (!pwalletdbEncryption->WriteCryptedTx(txid, chash, vchCryptedSecret, true)) {
                LogPrintf("Writing Encrypted CWalletTx failed!!!\n");
                return false;
            }
        }

        //Encrypt All wallet archived transactions
        for (map<uint256, ArchiveTxPoint>::iterator it = mapArcTxs.begin(); it != mapArcTxs.end(); ++it) {
            ArchiveTxPoint arcTxPt = (*it).second;
            uint256 txid = (*it).first;

            std::vector<unsigned char> vchCryptedSecret;
            uint256 chash = HashWithFP(txid);
            CKeyingMaterial vchSecret = SerializeForEncryptionInput(txid, arcTxPt);

            if (!EncryptSerializedWalletObjects(vMasterKey, vchSecret, chash, vchCryptedSecret)) {
                LogPrintf("Encrypting ArchiveTxPoint failed!!!\n");
                return false;
            }

            if (!pwalletdbEncryption->WriteCryptedArcTx(txid, chash, vchCryptedSecret, true)) {
                LogPrintf("Writing Encrypted ArchiveTxPoint failed!!!\n");
                return false;
            }
        }

        for (map<uint256, SaplingOutPoint>::iterator it = mapArcSaplingOutPoints.begin(); it != mapArcSaplingOutPoints.end(); ++it) {
            uint256 nullifier = (*it).first;
            SaplingOutPoint op = (*it).second;

            std::vector<unsigned char> vchCryptedSecret;
            uint256 chash = HashWithFP(nullifier);
            CKeyingMaterial vchSecret = SerializeForEncryptionInput(nullifier, op);

            if (!EncryptSerializedWalletObjects(vMasterKey, vchSecret, chash, vchCryptedSecret)) {
                LogPrintf("Encrypting Archive Sapling Outpoint failed!!!\n");
                return false;
            }

            // Write all archived sapling outpoint
            if (!pwalletdbEncryption->WriteCryptedArcSaplingOp(nullifier, chash, vchCryptedSecret, true)) {
                LogPrintf("Writing Archive Sapling Outpoint failed!!!\n");
                return false;
            }

        }

        //Encrypt Transparent Keys
        for (map<CKeyID, CKey>::iterator it = mapKeys.begin(); it != mapKeys.end(); ++it) {
            if (!AddKeyPubKey((*it).second, (*it).second.GetPubKey())) {
                LogPrintf("Setting encrypted transparent spending key failed!!!\n");
                return false;
            }
        }

        //Encrypt Sapling Extended Spending Key
        for (map<libzcash::SaplingExtendedFullViewingKey, libzcash::SaplingExtendedSpendingKey>::iterator it = mapSaplingSpendingKeys.begin(); it != mapSaplingSpendingKeys.end(); ++it) {
              if (!AddSaplingZKey((*it).second)) {
                  LogPrintf("Setting encrypted sapling spending key failed!!!\n");
                  return false;
              }
        }

        //Encrypt Extended Full Viewing keys
        for (map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey>::iterator it = mapSaplingFullViewingKeys.begin(); it != mapSaplingFullViewingKeys.end(); ++it) {
              if (!HaveSaplingSpendingKey((*it).second)) {
                  if (!AddSaplingExtendedFullViewingKey((*it).second)) {
                      LogPrintf("Setting encrypted sapling viewing key failed!!!\n");
                      return false;
                  }
              }
        }

        //Encrypt SaplingPaymentAddress
        for (map<libzcash::SaplingPaymentAddress, libzcash::SaplingIncomingViewingKey>::iterator it = mapSaplingIncomingViewingKeys.begin(); it != mapSaplingIncomingViewingKeys.end(); it++)
        {
            if (!AddSaplingIncomingViewingKey((*it).second, (*it).first)) {
                LogPrintf("Setting encrypted sapling payment address failed!!!\n");
                return false;
            }
        }

        //Encrypt Diversified Addresses
        for (map<libzcash::SaplingPaymentAddress, DiversifierPath>::iterator it = mapSaplingPaymentAddresses.begin(); it != mapSaplingPaymentAddresses.end(); ++it) {
            if (!AddSaplingDiversifiedAddress((*it).first, (*it).second.first, (*it).second.second)) {
                LogPrintf("Setting encrypted sapling diversified payment address failed!!!\n");
                return false;
            }
        }

        //Encrypt the last diversifier path used for each spendingkey
        for (map<libzcash::SaplingIncomingViewingKey, blob88>::iterator it = mapLastDiversifierPath.begin(); it != mapLastDiversifierPath.end(); ++it) {
            if (!AddLastDiversifierUsed((*it).first, (*it).second)) {
                LogPrintf("Setting encrypted last diversified path failed!!!\n");
                return false;
            }
        }

        //Encrypt all CScripts
        for (map<const CScriptID, CScript>::iterator it = mapScripts.begin(); it != mapScripts.end(); ++it) {
            if (!AddCScript((*it).second)) {
                LogPrintf("Setting encrypted CScript failed!!!\n");
                return false;
            }
        }

        //Encrypt Watchonly addresses
        for (std::set<CScript>::iterator it = setWatchOnly.begin(); it != setWatchOnly.end(); ++it) {
            if (!AddWatchOnly(*it)) {
                LogPrintf("Setting encrypted watch failed!!!\n");
                return false;
            }
        }

        //Encrypt Addressbook entries
        for (std::map<CTxDestination, CAddressBookData>::iterator it = mapAddressBook.begin(); it != mapAddressBook.end(); ++it) {
            if (!SetAddressBook((*it).first, (*it).second.name, (*it).second.purpose)) {
                LogPrintf("Setting encrypted addressbook failed!!!\n");
                return false;
            }
        }

        //Encrypt DefaultKey
        if (!SetDefaultKey(vchDefaultKey)) {
            LogPrintf("Setting encrypted default key failed!!!\n");
            return false;
        }

        //Clear All unencrypted Spending Keys
        mapKeys.clear();
        mapSaplingSpendingKeys.clear();

        //Write Crypted statuses
        SetWalletCrypted(pwalletdbEncryption);
        SetDBCrypted();

        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        delete pwalletdbEncryption;
        pwalletdbEncryption = NULL;


        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

bool CWallet::EncryptSerializedWalletObjects(
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret){

    return CCryptoKeyStore::EncryptSerializedSecret(vchSecret, chash, vchCryptedSecret);
}

bool CWallet::EncryptSerializedWalletObjects(
    CKeyingMaterial &vMasterKeyIn,
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret) {

    return CCryptoKeyStore::EncryptSerializedSecret(vMasterKeyIn, vchSecret, chash, vchCryptedSecret);
}

bool CWallet::DecryptSerializedWalletObjects(
     const std::vector<unsigned char>& vchCryptedSecret,
     const uint256 chash,
     CKeyingMaterial &vchSecret) {

    return CCryptoKeyStore::DecryptSerializedSecret(vchCryptedSecret, chash, vchSecret);
 }

template<typename WalletObject>
uint256 CWallet::HashWithFP(WalletObject &wObj) {

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << wObj;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << seedEncyptionFP;

    return Hash(s.begin(), s.end(), ss.begin(), ss.end());
}

template<typename WalletObject1>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1) {

    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObj1;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

template<typename WalletObject1, typename WalletObject2>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2) {

    auto wObjs = std::make_pair(wObj1, wObj2);
    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObjs;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3) {

    auto wObjs = std::make_pair(std::make_pair(wObj1, wObj2), wObj3);
    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObjs;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

template<typename WalletObject1>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
}

template<typename WalletObject1, typename WalletObject2>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
    ss >> wObj2;
}

template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
    ss >> wObj2;
    ss >> wObj3;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txOrdered.insert(make_pair(wtx->nOrderPos, TxPair(wtx, (CAccountingEntry*)0)));
        //fprintf(stderr,"ordered iter.%d %s\n",(int32_t)wtx->nOrderPos,wtx->GetHash().GetHex().c_str());
    }
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }

    return txOrdered;
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            item.second.MarkDirty();
    }
}

/**
 * Ensure that every note in the wallet (for which we possess a spending key)
 * has a cached nullifier.
 */
bool CWallet::UpdateNullifierNoteMap()
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        ZCNoteDecryption dec;
        for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {
            for (mapSproutNoteData_t::value_type& item : wtxItem.second.mapSproutNoteData) {
                if (!item.second.nullifier) {
                    if (GetNoteDecryptor(item.second.address, dec)) {
                        auto i = item.first.js;
                        auto hSig = wtxItem.second.vjoinsplit[i].h_sig(
                            *pzcashParams, wtxItem.second.joinSplitPubKey);
                        item.second.nullifier = GetSproutNoteNullifier(
                            wtxItem.second.vjoinsplit[i],
                            item.second.address,
                            dec,
                            hSig,
                            item.first.n);
                    }
                }
            }

            // TODO: Sapling.  This method is only called from RPC walletpassphrase, which is currently unsupported
            // as RPC encryptwallet is hidden behind two flags: -developerencryptwallet -experimentalfeatures

            UpdateNullifierNoteMapWithTx(wtxItem.second);
        }
    }
    return true;
}

/**
 * Update mapSproutNullifiersToNotes and mapSaplingNullifiersToNotes
 * with the cached nullifiers in this tx.
 */
void CWallet::UpdateNullifierNoteMapWithTx(const CWalletTx& wtx)
{
    {
        LOCK(cs_wallet);
        for (const mapSproutNoteData_t::value_type& item : wtx.mapSproutNoteData) {
            if (item.second.nullifier) {
                mapSproutNullifiersToNotes[*item.second.nullifier] = item.first;
                mapArcJSOutPoints[*item.second.nullifier] = item.first;
            }
        }

        for (const mapSaplingNoteData_t::value_type& item : wtx.mapSaplingNoteData) {
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes[*item.second.nullifier] = item.first;
                mapArcSaplingOutPoints[*item.second.nullifier] = item.first;
            }
        }
    }
}

/**
 * Update mapSproutNullifiersToNotes, computing the nullifier from a cached witness if necessary.
 */
void CWallet::UpdateSproutNullifierNoteMapWithTx(CWalletTx& wtx) {
    LOCK(cs_wallet);

    ZCNoteDecryption dec;
    for (mapSproutNoteData_t::value_type& item : wtx.mapSproutNoteData) {
        SproutNoteData nd = item.second;

        if (nd.witnesses.empty()) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (nd.nullifier) {
                mapSproutNullifiersToNotes.erase(nd.nullifier.get());
            }
            nd.nullifier = boost::none;
        }
        else {
            if (GetNoteDecryptor(nd.address, dec)) {
                auto i = item.first.js;
                auto hSig = wtx.vjoinsplit[i].h_sig(
                    *pzcashParams, wtx.joinSplitPubKey);
                auto optNullifier = GetSproutNoteNullifier(
                    wtx.vjoinsplit[i],
                    item.second.address,
                    dec,
                    hSig,
                    item.first.n);

                if (!optNullifier) {
                    // This should not happen.  If it does, maybe the position has been corrupted or miscalculated?
                    assert(false);
                }

                uint256 nullifier = optNullifier.get();
                mapSproutNullifiersToNotes[nullifier] = item.first;
                mapArcJSOutPoints[nullifier] = item.first;
                item.second.nullifier = nullifier;

            }
        }
    }
}

/**
 * Update mapSaplingNullifiersToNotes, computing the nullifier from a cached witness if necessary.
 */
void CWallet::UpdateSaplingNullifierNoteMapWithTx(CWalletTx& wtx) {
    LOCK(cs_wallet);

    for (mapSaplingNoteData_t::value_type &item : wtx.mapSaplingNoteData) {
        SaplingOutPoint op = item.first;
        SaplingNoteData nd = item.second;

        if (nd.witnesses.empty()) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes.erase(item.second.nullifier.get());
            }
            item.second.nullifier = boost::none;
        }
        else {
            uint64_t position = nd.witnesses.front().position();
            // Skip if we only have incoming viewing key
            if (mapSaplingFullViewingKeys.count(nd.ivk) != 0) {
                SaplingExtendedFullViewingKey extfvk = mapSaplingFullViewingKeys.at(nd.ivk);
                OutputDescription output = wtx.vShieldedOutput[op.n];

                auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(output.encCiphertext, nd.ivk, output.ephemeralKey);

                // The transaction would not have entered the wallet unless
                // its plaintext had been successfully decrypted previously.
                assert(optDeserialized != boost::none);

                auto optPlaintext = SaplingNotePlaintext::plaintext_checks_without_height(*optDeserialized, nd.ivk, output.ephemeralKey, output.cmu);

                // An item in mapSaplingNoteData must have already been successfully decrypted,
                // otherwise the item would not exist in the first place.
                assert(optPlaintext != boost::none);

                auto optNote = optPlaintext.get().note(nd.ivk);
                if (!optNote) {
                    assert(false);
                }
                auto optNullifier = optNote.get().nullifier(extfvk.fvk, position);
                if (!optNullifier) {
                    // This should not happen.  If it does, maybe the position has been corrupted or miscalculated?
                    assert(false);
                }
                uint256 nullifier = optNullifier.get();
                mapSaplingNullifiersToNotes[nullifier] = op;
                mapArcSaplingOutPoints[nullifier] = op;
                item.second.nullifier = nullifier;
            }
        }
    }
}

/**
 * Iterate over transactions in a block and update the cached Sapling nullifiers
 * for transactions which belong to the wallet.
 */
void CWallet::UpdateNullifierNoteMapForBlock(const CBlock *pblock) {
    LOCK(cs_wallet);

    for (const CTransaction& tx : pblock->vtx) {
        auto hash = tx.GetHash();
        bool txIsOurs = mapWallet.count(hash);
        if (txIsOurs) {
            UpdateSproutNullifierNoteMapWithTx(mapWallet[hash]);
            UpdateSaplingNullifierNoteMapWithTx(mapWallet[hash]);
        }
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet, CWalletDB* pwalletdb, int nHeight, bool fRescan)
{
    uint256 hash = wtxIn.GetHash();

    if (fFromLoadWallet)
    {
        mapWallet[hash] = wtxIn;
        mapWallet[hash].BindWallet(this);
        UpdateNullifierNoteMapWithTx(mapWallet[hash]);
        AddToSpends(hash);
    }
    else
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        UpdateNullifierNoteMapWithTx(wtx);
        bool fInsertedNew = ret.second;
        if (fInsertedNew) {
            AddToSpends(hash);
            wtx.nOrderPos = IncOrderPosNext(pwalletdb);
        }

        //Set Transaction Time
        wtx.nTimeReceived = GetTime();
        wtx.nTimeSmart = wtx.nTimeReceived;
        if (!wtxIn.hashBlock.IsNull()) {
            if (mapBlockIndex.count(wtxIn.hashBlock)) {
                int64_t blocktime = mapBlockIndex[wtxIn.hashBlock]->GetBlockTime();
                wtx.nTimeSmart = blocktime;
                wtx.nTimeReceived = blocktime;
            } else {
                LogPrintf("AddToWallet(): found %s in block %s not in index\n", wtxIn.GetHash().ToString(),wtxIn.hashBlock.ToString());
            }
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (!wtxIn.hashBlock.IsNull() && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (UpdatedNoteData(wtxIn, wtx)) {
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
        }

        //// debug print
        LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk and update tx archive map
        if (fInsertedNew || fUpdated) {
            ArchiveTxPoint arcTxPt = ArchiveTxPoint(wtx.hashBlock, wtx.nIndex);

            if (nHeight > 0) {
              AddToArcTxs(wtx, nHeight, arcTxPt);
            }
        }

        // Break debit/credit balance caches:
        wtx.MarkDirty();

        // Notify UI of new or updated transaction
        if (!fRescan) {
            NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);
            NotifyBalanceChanged();
        }
        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if ( !strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }

    }
    return true;
}

bool CWallet::UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx)
{
    bool unchangedSproutFlag = (wtxIn.mapSproutNoteData.empty() || wtxIn.mapSproutNoteData == wtx.mapSproutNoteData);
    if (!unchangedSproutFlag) {
        auto tmp = wtxIn.mapSproutNoteData;
        // Ensure we keep any cached witnesses we may already have
        for (const std::pair <JSOutPoint, SproutNoteData> nd : wtx.mapSproutNoteData) {
            if (tmp.count(nd.first) && nd.second.witnesses.size() > 0) {
                tmp.at(nd.first).witnesses.assign(
                        nd.second.witnesses.cbegin(), nd.second.witnesses.cend());
            }
            tmp.at(nd.first).witnessHeight = nd.second.witnessHeight;
        }
        // Now copy over the updated note data
        wtx.mapSproutNoteData = tmp;
    }

    bool unchangedSaplingFlag = (wtxIn.mapSaplingNoteData.empty() || wtxIn.mapSaplingNoteData == wtx.mapSaplingNoteData);
    if (!unchangedSaplingFlag) {
        auto tmp = wtxIn.mapSaplingNoteData;
        // Ensure we keep any cached witnesses we may already have

        for (const std::pair <SaplingOutPoint, SaplingNoteData> nd : wtx.mapSaplingNoteData) {
            if (tmp.count(nd.first) && nd.second.witnesses.size() > 0) {
                tmp.at(nd.first).witnesses.assign(
                        nd.second.witnesses.cbegin(), nd.second.witnesses.cend());
            }
            tmp.at(nd.first).witnessHeight = nd.second.witnessHeight;
        }

        // Now copy over the updated note data
        wtx.mapSaplingNoteData = tmp;
    }

    return !unchangedSproutFlag || !unchangedSaplingFlag;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
void CWallet::AddToWalletIfInvolvingMe(const std::vector<CTransaction> &vtx, std::vector<CTransaction> &vAddedTxes, const CBlock* pblock, const int nHeight, bool fUpdate, std::set<SaplingPaymentAddress>& addressesFound, bool fRescan)
{
    {
        AssertLockHeld(cs_wallet);

        //Step 1 -- inital Tx checks
        std::vector<CTransaction> vFilteredTxes;

        for (int i = 0; i < vtx.size(); i++) {
            if (vtx[i].IsCoinBase() && vtx[i].vout[0].nValue == 0)
                continue;
            bool fExisted = mapWallet.count(vtx[i].GetHash()) != 0;
            if (fExisted && !fUpdate)
                continue;

            vFilteredTxes.emplace_back(vtx[i]);
        }

        //Step 2 -- decrypt transactions
        auto saplingNoteDataAndAddressesToAdd = FindMySaplingNotes(vFilteredTxes, nHeight);
        auto saplingNoteData = saplingNoteDataAndAddressesToAdd.first;
        auto addressesToAdd = saplingNoteDataAndAddressesToAdd.second;

        //Step 3 -- add addresses
        for (const auto &addressToAdd : addressesToAdd) {
            //Loaded into memory only
            //This will be saved during the wallet SetBestChainINTERNAL
            CCryptoKeyStore::AddSaplingIncomingViewingKey(addressToAdd.second, addressToAdd.first);
            //Store addresses to notify GUI later
            addressesFound.insert(addressToAdd.first);
        }

        //Step 4 -- add transactions
        for (int i = 0; i < vFilteredTxes.size(); i++) {

            uint256 hash = vFilteredTxes[i].GetHash();
            mapSaplingNoteData_t noteData;

            for (mapSaplingNoteData_t::iterator it = saplingNoteData.begin(); it != saplingNoteData.end(); it++) {
                SaplingOutPoint op = (*it).first;
                SaplingNoteData nd = (*it).second;
                if (op.hash == hash) {
                      noteData.insert(std::make_pair(op, nd));
                }
            }


            bool fExisted = mapWallet.count(vFilteredTxes[i].GetHash()) != 0;

            if (fExisted || IsMine(vFilteredTxes[i]) || IsFromMe(vFilteredTxes[i]) || noteData.size() > 0)
            {
                /**
                 * New implementation of wallet filter code.
                 *
                 * If any vout of tx is belongs to wallet (IsMine(tx) == true) and tx
                 * is not from us, mean, if every vin not belongs to our wallet
                 * (IsFromMe(tx) == false), then tx need to be checked through wallet
                 * filter. If tx haven't any vin from trusted / whitelisted address it
                 * shouldn't be added into wallet.
                */

                if (!mapMultiArgs["-whitelistaddress"].empty())
                {
                    if (IsMine(vFilteredTxes[i]) && !vFilteredTxes[i].IsCoinBase() && !IsFromMe(vFilteredTxes[i]))
                    {
                        bool fIsFromWhiteList = false;
                        BOOST_FOREACH(const CTxIn& txin, vFilteredTxes[i].vin)
                        {
                            if (fIsFromWhiteList) break;
                            uint256 hashBlock; CTransaction prevTx; CTxDestination dest;
                            if (GetTransaction(txin.prevout.hash, prevTx, hashBlock, true) && ExtractDestination(prevTx.vout[txin.prevout.n].scriptPubKey,dest))
                            {
                                BOOST_FOREACH(const std::string& strWhiteListAddress, mapMultiArgs["-whitelistaddress"])
                                {
                                    if (EncodeDestination(dest) == strWhiteListAddress)
                                    {
                                        fIsFromWhiteList = true;
                                        // std::cerr << __FUNCTION__ << " tx." << tx.GetHash().ToString() << " passed wallet filter! whitelistaddress." << EncodeDestination(dest) << std::endl;
                                        LogPrintf("tx.%s passed wallet filter! whitelistaddress.%s\n", vFilteredTxes[i].GetHash().ToString(),EncodeDestination(dest));
                                        break;
                                    }
                                }
                            }
                        }
                        if (!fIsFromWhiteList)
                        {
                            // std::cerr << __FUNCTION__ << " tx." << tx.GetHash().ToString() << " is NOT passed wallet filter!" << std::endl;
                            LogPrintf("tx.%s is NOT passed wallet filter!\n", vFilteredTxes[i].GetHash().ToString());
                            continue;
                        }
                    }
                }

                CWalletTx wtx(this,vFilteredTxes[i]);

                if (noteData.size() > 0) {
                    wtx.SetSaplingNoteData(noteData);
                }

                // Get merkle branch if transaction was found in a block
                if (pblock)
                    wtx.SetMerkleBranch(*pblock);

                //Set Wallet Birthday on first transaction found
                if (nBirthday > 0 && nHeight < nBirthday) {
                    nBirthday = nHeight;
                    SetWalletBirthday(nBirthday);
                }

                // Do not flush the wallet here for performance reasons
                // this is safe, as in case of a crash, we rescan the necessary blocks on startup through our SetBestChain-mechanism
                CWalletDB walletdb(strWalletFile, "r+", false);

                if (AddToWallet(wtx, false, &walletdb, nHeight, fRescan)) {
                    vAddedTxes.emplace_back(vFilteredTxes[i]);
                }
            }
        }
    }
}

void CWallet::SyncTransactions(const std::vector<CTransaction> &vtx, const CBlock* pblock, const int nHeight)
{
    LOCK(cs_wallet);
    std::set<SaplingPaymentAddress> addressesFound;

    std::vector<CTransaction> vOurs;
    AddToWalletIfInvolvingMe(vtx, vOurs, pblock, nHeight, true, addressesFound, false);

    for (std::set<SaplingPaymentAddress>::iterator it = addressesFound.begin(); it != addressesFound.end(); it++) {
        SetZAddressBook(*it, "z-sapling", "", true);
    }

    for (int i = 0; i < vOurs.size(); i++) {
        MarkAffectedTransactionsDirty(vOurs[i]);
    }
}

void CWallet::MarkAffectedTransactionsDirty(const CTransaction& tx)
{
    // If a transaction changes 'conflicted' state, that changes the balance
    // available of the outputs it spends. So force those to be
    // recomputed, also:
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if (mapWallet.count(txin.prevout.hash))
            mapWallet[txin.prevout.hash].MarkDirty();
    }
    for (const JSDescription& jsdesc : tx.vjoinsplit) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (mapSproutNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapSproutNullifiersToNotes[nullifier].hash)) {
                mapWallet[mapSproutNullifiersToNotes[nullifier].hash].MarkDirty();
            }
        }
    }

    for (const SpendDescription &spend : tx.vShieldedSpend) {
        uint256 nullifier = spend.nullifier;
        if (mapSaplingNullifiersToNotes.count(nullifier) &&
            mapWallet.count(mapSaplingNullifiersToNotes[nullifier].hash)) {
            mapWallet[mapSaplingNullifiersToNotes[nullifier].hash].MarkDirty();
        }
    }
}

bool CWallet::EraseFromWallet(const uint256 &hash)
{
    if (!fFileBacked)
        return false;

    LOCK(cs_wallet);

    if (IsCrypted()) {
        if (!IsLocked()) {
          if (mapWallet.erase(hash)) {
              uint256 chash = HashWithFP(hash);
              return CWalletDB(strWalletFile).EraseCryptedTx(chash);
          }
        }
    } else {
        if (mapWallet.erase(hash)) {
            return CWalletDB(strWalletFile).EraseTx(hash);
        }
    }

    return false;
}

/*Rescan the whole chain for transactions*/
void CWallet::ForceRescanWallet() {
    CBlockIndex* pindex = chainActive.Genesis();
    ScanForWalletTransactions(pindex, true, true, true);
}

void CWallet::RescanWallet()
{
    if (needsRescan)
    {
        CBlockIndex *start = chainActive.Height() > 0 ? chainActive[1] : NULL;
        if (start)
            ScanForWalletTransactions(start, true);
        needsRescan = false;
    }
}


/**
 * Returns a nullifier if the SpendingKey is available
 * Throws std::runtime_error if the decryptor doesn't match this note
 */
boost::optional<uint256> CWallet::GetSproutNoteNullifier(const JSDescription &jsdesc,
                                                         const libzcash::SproutPaymentAddress &address,
                                                         const ZCNoteDecryption &dec,
                                                         const uint256 &hSig,
                                                         uint8_t n) const
{
    boost::optional<uint256> ret;
    auto note_pt = libzcash::SproutNotePlaintext::decrypt(
        dec,
        jsdesc.ciphertexts[n],
        jsdesc.ephemeralKey,
        hSig,
        (unsigned char) n);
    auto note = note_pt.note(address);
    // SpendingKeys are only available if:
    // - We have them (this isn't a viewing key)
    // - The wallet is unlocked
    libzcash::SproutSpendingKey key;
    if (GetSproutSpendingKey(address, key)) {
        ret = note.nullifier(key);
    }
    return ret;
}

/**
 * Finds all output notes in the given transaction that have been sent to
 * PaymentAddresses in this wallet.
 *
 * It should never be necessary to call this method with a CWalletTx, because
 * the result of FindMySproutNotes (for the addresses available at the time) will
 * already have been cached in CWalletTx.mapSproutNoteData.
 */
mapSproutNoteData_t CWallet::FindMySproutNotes(const CTransaction &tx) const
{
    LOCK(cs_wallet);
    uint256 hash = tx.GetHash();

    mapSproutNoteData_t noteData;
    for (size_t i = 0; i < tx.vjoinsplit.size(); i++) {
        auto hSig = tx.vjoinsplit[i].h_sig(*pzcashParams, tx.joinSplitPubKey);
        for (uint8_t j = 0; j < tx.vjoinsplit[i].ciphertexts.size(); j++) {
            for (const NoteDecryptorMap::value_type& item : mapNoteDecryptors) {
                try {
                    auto address = item.first;
                    JSOutPoint jsoutpt {hash, i, j};
                    auto nullifier = GetSproutNoteNullifier(
                        tx.vjoinsplit[i],
                        address,
                        item.second,
                        hSig, j);
                    if (nullifier) {
                        SproutNoteData nd {address, *nullifier};
                        noteData.insert(std::make_pair(jsoutpt, nd));
                    } else {
                        SproutNoteData nd {address};
                        noteData.insert(std::make_pair(jsoutpt, nd));
                    }
                    break;
                } catch (const note_decryption_failed &err) {
                    // Couldn't decrypt with this decryptor
                } catch (const std::exception &exc) {
                    // Unexpected failure
                    LogPrintf("FindMySproutNotes(): Unexpected error while testing decrypt:\n");
                    LogPrintf("%s\n", exc.what());
                }
            }
        }
    }
    return noteData;
}


/**
 * Finds all output notes in the given transaction that have been sent to
 * SaplingPaymentAddresses in this wallet.
 *
 * It should never be necessary to call this method with a CWalletTx, because
 * the result of FindMySaplingNotes (for the addresses available at the time) will
 * already have been cached in CWalletTx.mapSaplingNoteData.
 */
static void DecryptSaplingNoteWorker(const CWallet *wallet, std::vector<const SaplingIncomingViewingKey*> vIvk, std::vector<const OutputDescription*> vOutput, std::vector<uint32_t> vPosition, const std::vector<uint256> vHash, const int &height, mapSaplingNoteData_t *noteData, SaplingIncomingViewingKeyMap *viewingKeysToAdd, int threadNumber)
{
    for (int i = 0; i < vIvk.size(); i++) {
        const SaplingIncomingViewingKey ivk = *vIvk[i];
        const OutputDescription output = *vOutput[i];
        const uint256 hash = vHash[i];

        auto result = SaplingNotePlaintext::decrypt(Params().GetConsensus(), height, output.encCiphertext, ivk, output.ephemeralKey, output.cmu);
        if (result) {

            auto address = ivk.address(result.get().d);

            // We don't cache the nullifier here as computing it requires knowledge of the note position
            // in the commitment tree, which can only be determined when the transaction has been mined.
            SaplingOutPoint op {hash, vPosition[i]};
            SaplingNoteData nd;
            nd.ivk = ivk;

            //Cache Address and value - in Memory Only
            auto note = result.get();
            nd.value = note.value();
            nd.address = address.get();

            if (nd.value >= minTxValue) {
                //Only add notes greater then this value
                //dust filter
                {
                    LOCK(wallet->cs_wallet_threadedfunction);
                    viewingKeysToAdd->insert(make_pair(address.get(),ivk));
                    noteData->insert(std::make_pair(op, nd));
                }
            }
        }
    }
}

std::pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> CWallet::FindMySaplingNotes(const std::vector<CTransaction> &vtx, int height) const
{
    LOCK(cs_wallet);

    //Data to be collected
    mapSaplingNoteData_t noteData;
    SaplingIncomingViewingKeyMap viewingKeysToAdd;

    //Create key thread buckets
    std::vector<const SaplingIncomingViewingKey*> vIvk;
    std::vector<std::vector<const SaplingIncomingViewingKey*>> vvIvk;

    //Create OutputDescription thread buckets
    std::vector<const OutputDescription*> vOutputDescrition;
    std::vector<std::vector<const OutputDescription*>> vvOutputDescrition;

    //Create transaction position thread buckets
    std::vector<uint32_t> vPosition;
    std::vector<std::vector<uint32_t>> vvPosition;

    //Create transaction hash thread buckets
    std::vector<uint256> vHash;
    std::vector<std::vector<uint256>> vvHash;

    for (uint32_t i = 0; i < maxProcessingThreads; i++) {
        vvIvk.emplace_back(vIvk);
        vvOutputDescrition.emplace_back(vOutputDescrition);
        vvPosition.emplace_back(vPosition);
        vvHash.emplace_back(vHash);
    }

    // Protocol Spec: 4.19 Block Chain Scanning (Sapling)
    uint32_t t = 0;
    for (uint32_t j = 0; j < vtx.size(); j++) {
        //Transaction being processed
        uint256 hash = vtx[j].GetHash();
        for (uint32_t i = 0; i < vtx[j].vShieldedOutput.size(); i++) {
            for (auto it = setSaplingIncomingViewingKeys.begin(); it != setSaplingIncomingViewingKeys.end(); it++) {
                vvIvk[t].emplace_back(&(*it));
                vvOutputDescrition[t].emplace_back(&vtx[j].vShieldedOutput[i]);
                vvPosition[t].emplace_back(i);
                vvHash[t].emplace_back(hash);
                //Increment ivk vector
                t++;
                //reset if ivk vector is greater qty of threads being used
                if (t >= vvIvk.size()) {
                    t = 0;
                }
            }
        }
    }


    std::vector<boost::thread*> decryptionThreads;
    for (uint32_t i = 0; i < vvIvk.size(); ++i) {
        if(!vvIvk[i].empty()) {
            decryptionThreads.emplace_back(new boost::thread(DecryptSaplingNoteWorker, this, vvIvk[i], vvOutputDescrition[i], vvPosition[i], vvHash[i], height, &noteData, &viewingKeysToAdd, i));
        }
    }

    // Cleanup
    for (auto dthread : decryptionThreads) {
        dthread->join();
        delete dthread;
    }

    //clean up pointers
    for (uint32_t i = 0; i < vvOutputDescrition.size(); i++) {
        for (auto pOutputDescrition : vOutputDescrition) {
            delete pOutputDescrition;
        }
        vvOutputDescrition[i].resize(0);
    }
    vvOutputDescrition.resize(0);

    for (uint32_t i = 0; i < vvIvk.size(); i++) {
        for (auto pIvk : vIvk) {
            delete pIvk;
        }
        vvIvk[i].resize(0);
    }
    vvIvk.resize(0);

    return std::make_pair(noteData, viewingKeysToAdd);
}

bool CWallet::IsSproutNullifierFromMe(const uint256& nullifier) const
{
    {
        LOCK(cs_wallet);
        if (mapSproutNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapSproutNullifiersToNotes.at(nullifier).hash)) {
            return true;
        }
    }
    return false;
}

bool CWallet::IsSaplingNullifierFromMe(const uint256& nullifier) const
{
    {
        LOCK(cs_wallet);
        if (mapSaplingNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapSaplingNullifiersToNotes.at(nullifier).hash)) {
            return true;
        }
    }
    return false;
}

void CWallet::GetSproutNoteWitnesses(std::vector<JSOutPoint> notes,
                                     std::vector<boost::optional<SproutWitness>>& witnesses,
                                     uint256 &final_anchor)
{
    LOCK(cs_wallet);
    witnesses.resize(notes.size());
    boost::optional<uint256> rt;
    int i = 0;
    for (JSOutPoint note : notes) {
        if (mapWallet.count(note.hash) &&
                mapWallet[note.hash].mapSproutNoteData.count(note) &&
                mapWallet[note.hash].mapSproutNoteData[note].witnesses.size() > 0) {
            witnesses[i] = mapWallet[note.hash].mapSproutNoteData[note].witnesses.front();
            if (!rt) {
                rt = witnesses[i]->root();
            } else {
                assert(*rt == witnesses[i]->root());
            }
        }
        i++;
    }
    // All returned witnesses have the same anchor
    if (rt) {
        final_anchor = *rt;
    }
}

bool CWallet::GetSaplingNoteMerklePaths(std::vector<SaplingOutPoint> notes,
                                      std::vector<MerklePath>& saplingMerklePaths,
                                      uint256 &final_anchor)
{
    LOCK(cs_wallet);
    saplingMerklePaths.resize(notes.size());
    boost::optional<uint256> rt;
    int i = 0;
    for (SaplingOutPoint op : notes) {

        const CWalletTx* wtx = GetWalletTx(op.hash);
        if (wtx == NULL) {
            return false;
        }

        if (wtx->mapSaplingNoteData.count(op)) {

            if (!saplingWallet.GetMerklePathOfNote(op.hash, op.n, saplingMerklePaths[i])) {
                return false;
            }

            LogPrintf("\nGot Path\n");

            //Calculate the anchor
            uint256 anchor;
            OutputDescription output = wtx->vShieldedOutput[op.n];
            if (!saplingWallet.GetPathRootWithCMU(saplingMerklePaths[i], output.cmu, anchor)) {
                return false;
            }

            LogPrintf("Got Anchor %s\n\n", anchor.ToString());
            LogPrintf("Sapling Wallet Anchor %s\n\n", saplingWallet.GetLatestAnchor().ToString());

            //Check first anchor found and assert all following achors match
            if (!rt) {
                rt = anchor;
            } else {
                assert(*rt == anchor);
            }
            i++;
        }
    }

    // All returned witnesses have the same anchor
    if (rt) {
        final_anchor = *rt;
    }

    return true;
}

isminetype CWallet::IsMine(const CTxIn &txin) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                return (::IsMine(*this, prev.vout[txin.prevout.n].scriptPubKey));
        }
    }
    return ISMINE_NO;
}

CAmount CWallet::GetDebit(const CTxIn &txin, const isminefilter& filter) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
                if (::IsMine(*this, prev.vout[txin.prevout.n].scriptPubKey) & filter)
                    return prev.vout[txin.prevout.n].nValue; // komodo_interest?
        }
    }
    return 0;
}

isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return ::IsMine(*this, txout.scriptPubKey);
}

CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a script that is ours, but is not in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (::IsMine(*this, txout.scriptPubKey))
    {
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
            return true;

        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetChange(): value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

typedef vector<unsigned char> valtype;
unsigned int HaveKeys(const vector<valtype>& pubkeys, const CKeyStore& keystore);

unsigned int HaveKeys(const vector<valtype>& pubkeys, const CKeyStore& keystore)
{
    unsigned int nResult = 0;
    BOOST_FOREACH(const valtype& pubkey, pubkeys)
    {
        CKeyID keyID = CPubKey(pubkey).GetID();
        if (keystore.HaveKey(keyID))
            ++nResult;
    }
    return nResult;
}

bool CWallet::IsMine(const CTransaction& tx)
{
    for (int i = 0; i < tx.vout.size(); i++)
    {
        if (IsMine(tx, i))
            return true;
    }
    return false;
}

/*
    Before Verus changes CWallet::IsMine(const CTransaction& tx) called other version
    of IsMine for each vout: CWallet::IsMine(const CTxOut& txout), so now we have two
    similar functions:
        - isminetype CWallet::IsMine(const CTransaction& tx, uint32_t voutNum) (wallet.cpp)
        - isminetype IsMineInner(const CKeyStore& keystore, const CScript& _scriptPubKey, IsMineSigVersion sigversion) (wallet_ismine.cpp)
    TODO: sort this out (!)
*/

// special case handling for non-standard/Verus OP_RETURN script outputs, which need the transaction
// to determine ownership

isminetype CWallet::IsMine(const CTransaction& tx, uint32_t voutNum)
{
    vector<valtype> vSolutions;
    txnouttype whichType;
    const CScriptExt scriptPubKey = CScriptExt(tx.vout[voutNum].scriptPubKey);

    if (!Solver(scriptPubKey, whichType, vSolutions)) {
        if (this->HaveWatchOnly(scriptPubKey))
            return ISMINE_WATCH_ONLY;
        return ISMINE_NO;
    }

    CKeyID keyID;
    CScriptID scriptID;
    CScriptExt subscript;
    int voutNext = voutNum + 1;

    switch (whichType)
    {
        case TX_NONSTANDARD:
        case TX_NULL_DATA:
            break;

        case TX_CRYPTOCONDITION:
            // for now, default is that the first value returned will be the script, subsequent values will be
            // pubkeys. if we have the first pub key in our wallet, we consider this spendable
            if (vSolutions.size() > 1)
            {
                keyID = CPubKey(vSolutions[1]).GetID();
                if (this->HaveKey(keyID))
                    return ISMINE_SPENDABLE;
            }
            break;

        case TX_PUBKEY:
            keyID = CPubKey(vSolutions[0]).GetID();
            if (this->HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;

        case TX_PUBKEYHASH:
            keyID = CKeyID(uint160(vSolutions[0]));
            if (this->HaveKey(keyID))
                return ISMINE_SPENDABLE;
            break;

        case TX_SCRIPTHASH:
            scriptID = CScriptID(uint160(vSolutions[0]));
            if (this->GetCScript(scriptID, subscript))
            {
                // if this is a CLTV, handle it differently
                if (subscript.IsCheckLockTimeVerify())
                {
                    return (::IsMine(*this, subscript));
                }
                else
                {
                    isminetype ret = ::IsMine(*this, subscript);
                    if (ret == ISMINE_SPENDABLE)
                        return ret;
                }
            }
            else if (tx.vout.size() > (voutNext = voutNum + 1) &&
                tx.vout[voutNext].scriptPubKey.size() > 7 &&
                tx.vout[voutNext].scriptPubKey[0] == OP_RETURN)
            {
                // get the opret script from next vout, verify that the front is CLTV and hash matches
                // if so, remove it and use the solver
                opcodetype op;
                std::vector<uint8_t> opretData;
                CScript::const_iterator it = tx.vout[voutNext].scriptPubKey.begin() + 1;
                if (tx.vout[voutNext].scriptPubKey.GetOp2(it, op, &opretData))
                {
                    if (opretData.size() > 0 && opretData[0] == OPRETTYPE_TIMELOCK)
                    {
                        CScript opretScript = CScript(opretData.begin() + 1, opretData.end());

                        if (CScriptID(opretScript) == scriptID &&
                            opretScript.IsCheckLockTimeVerify())
                        {
                            // if we find that this is ours, we need to add this script to the wallet,
                            // and we can then recognize this transaction
                            isminetype t = ::IsMine(*this, opretScript);
                            if (t != ISMINE_NO)
                            {
                                this->AddCScript(opretScript);
                            }
                            return t;
                        }
                    }
                }
            }
            break;

        case TX_MULTISIG:
            // Only consider transactions "mine" if we own ALL the
            // keys involved. Multi-signature transactions that are
            // partially owned (somebody else has a key that can spend
            // them) enable spend-out-from-under-you attacks, especially
            // in shared-wallet situations.
            vector<valtype> keys(vSolutions.begin()+1, vSolutions.begin()+vSolutions.size()-1);
            if (HaveKeys(keys, *this) == keys.size())
                return ISMINE_SPENDABLE;
            break;
    }

    if (this->HaveWatchOnly(scriptPubKey))
        return ISMINE_WATCH_ONLY;

    return ISMINE_NO;
}


bool CWallet::IsFromMe(const CTransaction& tx) const
{
    if (GetDebit(tx, ISMINE_ALL) > 0) {
        return true;
    }
    for (const JSDescription& jsdesc : tx.vjoinsplit) {
        for (const uint256& nullifier : jsdesc.nullifiers) {
            if (IsSproutNullifierFromMe(nullifier)) {
                return true;
            }
        }
    }
    for (const SpendDescription &spend : tx.vShieldedSpend) {
        if (IsSaplingNullifierFromMe(spend.nullifier)) {
            return true;
        }
    }
    return false;
}

CAmount CWallet::GetDebit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nDebit = 0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        nDebit += GetDebit(txin, filter);
        if (!MoneyRange(nDebit))
            throw std::runtime_error("CWallet::GetDebit(): value out of range");
    }
    return nDebit;
}

CAmount CWallet::GetCredit(const CTransaction& tx, int32_t voutNum, const isminefilter& filter) const
{
    if (voutNum >= tx.vout.size() || !MoneyRange(tx.vout[voutNum].nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(tx.vout[voutNum]) & filter) ? tx.vout[voutNum].nValue : 0);
}

CAmount CWallet::GetCredit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nCredit = 0;
    for (int i = 0; i < tx.vout.size(); i++)
    {
        nCredit += GetCredit(tx, i, filter);
    }
    return nCredit;
}

CAmount CWallet::GetChange(const CTransaction& tx) const
{
    CAmount nChange = 0;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        nChange += GetChange(txout);
        if (!MoneyRange(nChange))
            throw std::runtime_error("CWallet::GetChange(): value out of range");
    }
    return nChange;
}

bool CWallet::IsHDFullyEnabled() const
{
    // Only Sapling addresses are HD for now
    return false;
}

void CWallet::GenerateNewSeed()
{
    LOCK(cs_wallet);

    auto seed = HDSeed::Random(HD_WALLET_SEED_LENGTH);

    int64_t nCreationTime = GetTime();

    // If the wallet is encrypted and locked, this will fail.
    if (!SetHDSeed(seed))
        throw std::runtime_error(std::string(__func__) + ": SetHDSeed failed");

    // store the key creation time together with
    // the child index counter in the database
    // as a hdchain object
    CHDChain newHdChain;
    newHdChain.nVersion = CHDChain::VERSION_HD_BASE;
    newHdChain.seedFp = seed.Fingerprint();
    newHdChain.nCreateTime = nCreationTime;
    SetHDChain(newHdChain, false);
}

bool CWallet::IsValidPhrase(std::string &phrase)
{
    LOCK(cs_wallet);

    HDSeed checkSeed;
    return checkSeed.IsValidPhrase(phrase);
}

bool CWallet::RestoreSeedFromPhrase(std::string &phrase)
{
    LOCK(cs_wallet);

    if (!IsValidPhrase(phrase)) {
        return false;
    }

    auto seed = HDSeed::RestoreFromPhrase(phrase);

    int64_t nCreationTime = GetTime();

    // If the wallet is encrypted and locked, this will fail.
    if (!SetHDSeed(seed))
        throw std::runtime_error(std::string(__func__) + ": SetHDSeed failed");

    // store the key creation time together with
    // the child index counter in the database
    // as a hdchain object
    CHDChain newHdChain;
    newHdChain.nVersion = CHDChain::VERSION_HD_BASE;
    newHdChain.seedFp = seed.Fingerprint();
    newHdChain.nCreateTime = nCreationTime;
    SetHDChain(newHdChain, false);

    return true;
}

bool CWallet::SetHDSeed(const HDSeed& seed)
{

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    {
        LOCK(cs_wallet);
        if (!IsCrypted()) {

            if (!CCryptoKeyStore::SetHDSeed(seed)) {
                return false;
            }

            return CWalletDB(strWalletFile).WriteHDSeed(seed);
        } else {

            std::vector<unsigned char> vchCryptedSecret;
            auto seedFp = hdSeed.Fingerprint();

            if (!EncryptSerializedWalletObjects(seed.RawSeed(), seedFp, vchCryptedSecret)) {
                LogPrintf("Encrypting HDSeed failed!!!\n");
                return false;
            }

            if (!CCryptoKeyStore::SetCryptedHDSeed(seedFp, vchCryptedSecret)) {
                LogPrintf("Adding encrypted HDSeed failed!!!\n");
                return false;
            }


            //Double Encrypt seed for saving to Disk
            std::vector<unsigned char> vchCryptedSeedPair;
            uint256 chash = HashWithFP(seedFp);
            CKeyingMaterial vchSeedPair = SerializeForEncryptionInput(seedFp, vchCryptedSecret);
            if (!EncryptSerializedWalletObjects(vchSeedPair, chash, vchCryptedSeedPair)) {
                LogPrintf("Double Encrypting seed for Disk failed!!!\n");
                return false;
            }


            if (!CWalletDB(strWalletFile).WriteCryptedHDSeed(seedFp, chash, vchCryptedSeedPair)) {
                LogPrintf("Writing encrypted HDSeed failed!!!\n");
                return false;
            }
            //Clear unencrypted seed
            hdSeed = HDSeed();
        }
    }
    return true;
}

void CWallet::SetHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);
    if (!memonly && fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": writing chain failed");

    hdChain = chain;
}

bool CWallet::LoadHDSeed(const HDSeed& seed)
{
    return CBasicKeyStore::SetHDSeed(seed);
}

bool CWallet::LoadCryptedHDSeed(const uint256& seedFp, const std::vector<unsigned char>& seed)
{
    return CCryptoKeyStore::SetCryptedHDSeed(seedFp, seed);
}

void CWalletTx::SetSproutNoteData(mapSproutNoteData_t &noteData)
{
    mapSproutNoteData.clear();
    for (const std::pair<JSOutPoint, SproutNoteData> nd : noteData) {
        if (nd.first.js < vjoinsplit.size() &&
                nd.first.n < vjoinsplit[nd.first.js].ciphertexts.size()) {
            // Store the address and nullifier for the Note
            mapSproutNoteData[nd.first] = nd.second;
        } else {
            // If FindMySproutNotes() was used to obtain noteData,
            // this should never happen
            throw std::logic_error("CWalletTx::SetSproutNoteData(): Invalid note");
        }
    }
}

void CWalletTx::SetSaplingNoteData(mapSaplingNoteData_t &noteData)
{
    mapSaplingNoteData.clear();
    for (const std::pair<SaplingOutPoint, SaplingNoteData> nd : noteData) {
        if (nd.first.n < vShieldedOutput.size()) {
            mapSaplingNoteData[nd.first] = nd.second;
        } else {
            throw std::logic_error("CWalletTx::SetSaplingNoteData(): Invalid note");
        }
    }
}

std::pair<SproutNotePlaintext, SproutPaymentAddress> CWalletTx::DecryptSproutNote(
    JSOutPoint jsop) const
{
    LOCK(pwallet->cs_wallet);

    auto nd = this->mapSproutNoteData.at(jsop);
    SproutPaymentAddress pa = nd.address;

    // Get cached decryptor
    ZCNoteDecryption decryptor;
    if (!pwallet->GetNoteDecryptor(pa, decryptor)) {
        // Note decryptors are created when the wallet is loaded, so it should always exist
        throw std::runtime_error(strprintf(
            "Could not find note decryptor for payment address %s",
            EncodePaymentAddress(pa)));
    }

    auto hSig = this->vjoinsplit[jsop.js].h_sig(*pzcashParams, this->joinSplitPubKey);
    try {
        SproutNotePlaintext plaintext = SproutNotePlaintext::decrypt(
                decryptor,
                this->vjoinsplit[jsop.js].ciphertexts[jsop.n],
                this->vjoinsplit[jsop.js].ephemeralKey,
                hSig,
                (unsigned char) jsop.n);

        return std::make_pair(plaintext, pa);
    } catch (const note_decryption_failed &err) {
        // Couldn't decrypt with this spending key
        throw std::runtime_error(strprintf(
            "Could not decrypt note for payment address %s",
            EncodePaymentAddress(pa)));
    } catch (const std::exception &exc) {
        // Unexpected failure
        throw std::runtime_error(strprintf(
            "Error while decrypting note for payment address %s: %s",
            EncodePaymentAddress(pa), exc.what()));
    }
}

boost::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0) {
        return boost::none;
    }

    auto output = this->vShieldedOutput[op.n];
    auto nd = this->mapSaplingNoteData.at(op);

    auto maybe_pt = SaplingNotePlaintext::decrypt(
        params,
        height,
        output.encCiphertext,
        nd.ivk,
        output.ephemeralKey,
        output.cmu);
    assert(maybe_pt != boost::none);
    auto notePt = maybe_pt.get();

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(maybe_pa != boost::none);
    auto pa = maybe_pa.get();

    return std::make_pair(notePt, pa);
}

boost::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0) {
        return boost::none;
    }

    auto output = this->vShieldedOutput[op.n];
    auto nd = this->mapSaplingNoteData.at(op);

    auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(output.encCiphertext, nd.ivk, output.ephemeralKey);

    // The transaction would not have entered the wallet unless
    // its plaintext had been successfully decrypted previously.
    assert(optDeserialized != boost::none);

    auto maybe_pt = SaplingNotePlaintext::plaintext_checks_without_height(
        *optDeserialized,
        nd.ivk,
        output.ephemeralKey,
        output.cmu);
    assert(maybe_pt != boost::none);
    auto notePt = maybe_pt.get();

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(static_cast<bool>(maybe_pa));
    auto pa = maybe_pa.get();

    return std::make_pair(notePt, pa);
}

boost::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op, std::set<uint256>& ovks) const
{
    auto output = this->vShieldedOutput[op.n];

    for (auto ovk : ovks) {
        auto outPt = SaplingOutgoingPlaintext::decrypt(
            output.outCiphertext,
            ovk,
            output.cv,
            output.cmu,
            output.ephemeralKey);
        if (!outPt) {
            // Try decrypting with the next ovk
            continue;
        }

        auto maybe_pt = SaplingNotePlaintext::decrypt(
            params,
            height,
            output.encCiphertext,
            output.ephemeralKey,
            outPt->esk,
            outPt->pk_d,
            output.cmu);
        assert(static_cast<bool>(maybe_pt));
        auto notePt = maybe_pt.get();

        return std::make_pair(notePt, SaplingPaymentAddress(notePt.d, outPt->pk_d));
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return boost::none;
}

boost::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op, std::set<uint256>& ovks) const
{
    auto output = this->vShieldedOutput[op.n];

    for (auto ovk : ovks) {
        auto outPt = SaplingOutgoingPlaintext::decrypt(
            output.outCiphertext,
            ovk,
            output.cv,
            output.cmu,
            output.ephemeralKey);
        if (!outPt) {
            // Try decrypting with the next ovk
            continue;
        }

        auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(output.encCiphertext, output.ephemeralKey, outPt->esk, outPt->pk_d);

        // The transaction would not have entered the wallet unless
        // its plaintext had been successfully decrypted previously.
        assert(optDeserialized != boost::none);

        auto maybe_pt = SaplingNotePlaintext::plaintext_checks_without_height(
            *optDeserialized,
            output.ephemeralKey,
            outPt->esk,
            outPt->pk_d,
            output.cmu);
        assert(static_cast<bool>(maybe_pt));
        auto notePt = maybe_pt.get();

        return std::make_pair(notePt, SaplingPaymentAddress(notePt.d, outPt->pk_d));
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return boost::none;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

// GetAmounts will determine the transparent debits and credits for a given wallet tx.
void CWalletTx::GetAmounts(list<COutputEntry>& listReceived,
                           list<COutputEntry>& listSent, CAmount& nFee, string& strSentAccount, const isminefilter& filter) const
{
    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Is this tx sent/signed by me?
    CAmount nDebit = GetDebit(filter);
    bool isFromMyTaddr = nDebit > 0; // debit>0 means we signed/sent this transaction

    // Compute fee if we sent this transaction.
    if (isFromMyTaddr) {
        CAmount nValueOut = GetValueOut();  // transparent outputs plus all Sprout vpub_old and negative Sapling valueBalance
        CAmount nValueIn = GetShieldedValueIn();
        nFee = nDebit - nValueOut + nValueIn;
    }

    // Create output entry for vpub_old/new, if we sent utxos from this transaction
    if (isFromMyTaddr) {
        CAmount myVpubOld = 0;
        CAmount myVpubNew = 0;
        for (const JSDescription& js : vjoinsplit) {
            bool fMyJSDesc = false;

            // Check input side
            for (const uint256& nullifier : js.nullifiers) {
                if (pwallet->IsSproutNullifierFromMe(nullifier)) {
                    fMyJSDesc = true;
                    break;
                }
            }

            // Check output side
            if (!fMyJSDesc) {
                for (const std::pair<JSOutPoint, SproutNoteData> nd : this->mapSproutNoteData) {
                    if (nd.first.js < vjoinsplit.size() && nd.first.n < vjoinsplit[nd.first.js].ciphertexts.size()) {
                        fMyJSDesc = true;
                        break;
                    }
                }
            }

            if (fMyJSDesc) {
                myVpubOld += js.vpub_old;
                myVpubNew += js.vpub_new;
            }

            if (!MoneyRange(js.vpub_old) || !MoneyRange(js.vpub_new) || !MoneyRange(myVpubOld) || !MoneyRange(myVpubNew)) {
                 throw std::runtime_error("CWalletTx::GetAmounts: value out of range");
            }
        }

        // Create an output for the value taken from or added to the transparent value pool by JoinSplits
        if (myVpubOld > myVpubNew) {
            COutputEntry output = {CNoDestination(), myVpubOld - myVpubNew, (int)vout.size()};
            listSent.push_back(output);
        } else if (myVpubNew > myVpubOld) {
            COutputEntry output = {CNoDestination(), myVpubNew - myVpubOld, (int)vout.size()};
            listReceived.push_back(output);
        }
    }

    // If we sent utxos from this transaction, create output for value taken from (negative valueBalance)
    // or added (positive valueBalance) to the transparent value pool by Sapling shielding and unshielding.
    if (isFromMyTaddr) {
        if (valueBalance < 0) {
            COutputEntry output = {CNoDestination(), -valueBalance, (int) vout.size()};
            listSent.push_back(output);
        } else if (valueBalance > 0) {
            COutputEntry output = {CNoDestination(), valueBalance, (int) vout.size()};
            listReceived.push_back(output);
        }
    }

    // Sent/received.
    int32_t oneshot = 0;
    for (unsigned int i = 0; i < vout.size(); ++i)
    {
        const CTxOut& txout = vout[i];
        isminetype fIsMine = pwallet->IsMine(txout);
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (!(filter & ISMINE_CHANGE) && pwallet->IsChange(txout))
            {
                if ( oneshot++ > 1 )
                {
                    //fprintf(stderr,"skip change vout\n");
                    continue;
                }
            }
        }
        else if (!(fIsMine & filter))
        {
            //fprintf(stderr,"skip filtered vout %d %d\n",(int32_t)fIsMine,(int32_t)filter);
            continue;
        }
        // In either case, we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            //LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",this->GetHash().ToString()); complains on the opreturns
            address = CNoDestination();
        }

        COutputEntry output = {address, txout.nValue, (int)i};

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(output);
        //else fprintf(stderr,"not sent vout %d %d\n",(int32_t)fIsMine,(int32_t)filter);

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine & filter)
            listReceived.push_back(output);
        //else fprintf(stderr,"not received vout %d %d\n",(int32_t)fIsMine,(int32_t)filter);
    }

}

void CWalletTx::GetAccountAmounts(const string& strAccount, CAmount& nReceived,
                                  CAmount& nSent, CAmount& nFee, const isminefilter& filter) const
{
    nReceived = nSent = nFee = 0;

    CAmount allFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;
    GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

    if (strAccount == strSentAccount)
    {
        BOOST_FOREACH(const COutputEntry& s, listSent)
            nSent += s.amount;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        BOOST_FOREACH(const COutputEntry& r, listReceived)
        {
            if (pwallet->mapAddressBook.count(r.destination))
            {
                map<CTxDestination, CAddressBookData>::const_iterator mi = pwallet->mapAddressBook.find(r.destination);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second.name == strAccount)
                    nReceived += r.amount;
            }
            else if (strAccount.empty())
            {
                nReceived += r.amount;
            }
        }
    }
}

void CWallet::WitnessNoteCommitment(std::vector<uint256> commitments,
                                    std::vector<boost::optional<SproutWitness>>& witnesses,
                                    uint256 &final_anchor)
{
    witnesses.resize(commitments.size());
    CBlockIndex* pindex = chainActive.Genesis();
    SproutMerkleTree tree;

    while (pindex) {
        CBlock block;
        ReadBlockFromDisk(block, pindex,1);

        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            BOOST_FOREACH(const JSDescription& jsdesc, tx.vjoinsplit)
            {
                BOOST_FOREACH(const uint256 &note_commitment, jsdesc.commitments)
                {
                    tree.append(note_commitment);

                    BOOST_FOREACH(boost::optional<SproutWitness>& wit, witnesses) {
                        if (wit) {
                            wit->append(note_commitment);
                        }
                    }

                    size_t i = 0;
                    BOOST_FOREACH(uint256& commitment, commitments) {
                        if (note_commitment == commitment) {
                            witnesses.at(i) = tree.witness();
                        }
                        i++;
                    }
                }
            }
        }

        uint256 current_anchor = tree.root();

        // Consistency check: we should be able to find the current tree
        // in our CCoins view.
        SproutMerkleTree dummy_tree;
        assert(pcoinsTip->GetSproutAnchorAt(current_anchor, dummy_tree));

        pindex = chainActive.Next(pindex);
    }

    // TODO: #93; Select a root via some heuristic.
    final_anchor = tree.root();

    BOOST_FOREACH(boost::optional<SproutWitness>& wit, witnesses) {
        if (wit) {
            assert(final_anchor == wit->root());
        }
    }
}
/**
 * Reorder the transactions based on block hieght and block index.
 * Transactions can get out of order when they are deleted and subsequently
 * re-added during intial load rescan.
 */

void CWallet::ReorderWalletTransactions(std::map<std::pair<int,int>, CWalletTx*> &mapSorted, int64_t &maxOrderPos) {
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int maxSortNumber = chainActive.Tip()->nHeight + 1;

    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        CWalletTx* pwtx = &(it->second);
        maxOrderPos = max(maxOrderPos, pwtx->nOrderPos);

        if (mapBlockIndex.count(pwtx->hashBlock) > 0) {
            int wtxHeight = mapBlockIndex[pwtx->hashBlock]->nHeight;
            auto key = std::make_pair(wtxHeight, pwtx->nIndex);
            mapSorted.insert(make_pair(key, pwtx));
        }
        else {
            auto key = std::make_pair(maxSortNumber, 0);
            mapSorted.insert(std::make_pair(key, pwtx));
            maxSortNumber++;
        }
    }
}
 /**Update the nOrderPos with passed in ordered map.
 */

void CWallet::UpdateWalletTransactionOrder(std::map<std::pair<int,int>, CWalletTx*> &mapSorted, bool resetOrder) {
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

   int64_t previousPosition = 0;
   std::map<const uint256, CWalletTx*> mapUpdatedTxs;

   //Check the postion of each transaction relative to the previous one.
   for (map<std::pair<int,int>, CWalletTx*>::iterator it = mapSorted.begin(); it != mapSorted.end(); ++it) {
       CWalletTx* pwtx = it->second;
       const uint256 wtxid = pwtx->GetHash();

       if (pwtx->nOrderPos <= previousPosition || resetOrder) {
           previousPosition++;
           pwtx->nOrderPos = previousPosition;
           mapUpdatedTxs.insert(std::make_pair(wtxid, pwtx));
       }
       else {
           previousPosition = pwtx->nOrderPos;
       }
   }

  //Update transactions nOrderPos for transactions that changed
  CWalletDB walletdb(strWalletFile, "r+", false);
  ArchiveTxPoint arcTxPt;
  for (map<const uint256, CWalletTx*>::iterator it = mapUpdatedTxs.begin(); it != mapUpdatedTxs.end(); ++it) {
      CWalletTx* pwtx = it->second;
      LogPrint("deletetx","Reorder Tx - Updating Positon to %i for Tx %s\n ", pwtx->nOrderPos, pwtx->GetHash().ToString());;
      const auto itmw = mapWallet.find(pwtx->GetHash());
      if (itmw != mapWallet.end()) {
          mapWallet[pwtx->GetHash()].nOrderPos = pwtx->nOrderPos;
      }
  }

  //Update Next Wallet Tx Positon
  nOrderPosNext = previousPosition++;
  CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
  LogPrint("deletetx","Reorder Tx - Total Transactions Reordered %i, Next Position %i\n ", mapUpdatedTxs.size(), nOrderPosNext);

}

/**
 * Delete transactions from the Wallet
 */
bool CWallet::DeleteTransactions(std::vector<uint256> &removeTxs, std::vector<uint256> &removeArcTxs, bool fRescan) {
    AssertLockHeld(cs_wallet);

    bool removingTransactions = false;
    if (removeTxs.size() > 0) {
        removingTransactions = true;
    }


    CWalletDB walletdb(strWalletFile, "r+", false);

    for (int i = 0; i < removeTxs.size(); i++) {
        bool fRemoveFromSpends = !(mapWallet.at(removeTxs[i]).IsCoinBase());
        if (EraseFromWallet(removeTxs[i])) {
            if (fRemoveFromSpends) {
                RemoveFromSpends(removeTxs[i]);
            }
            LogPrint("deletetx","Delete Tx - Deleting tx %s, %i.\n", removeTxs[i].ToString(),i);
        } else {
            LogPrint("deletetx","Delete Tx - Deleting tx %failed.\n", removeTxs[i].ToString());
            return false;
        }
    }

    //Remove Conflicted ArcTx transactions from the wallet database
    for (int i = 0; i < removeArcTxs.size(); i++) {
        if (mapArcTxs.erase(removeArcTxs[i])) {
            walletdb.EraseArcTx(removeArcTxs[i]);
            //remove conflicted transactions from GUI
            if (!fRescan) {
                NotifyTransactionChanged(this, removeArcTxs[i], CT_DELETED);
            }
            LogPrint("deletetx","Delete Tx - Deleting Arc tx %s, %i.\n", removeArcTxs[i].ToString(),i);
        } else {
            LogPrint("deletetx","Delete Tx - Deleting Arc tx %failed.\n", removeArcTxs[i].ToString());
            return false;
        }
    }

    // Miodrag: release memory back to the OS, only works on linux
    #ifdef __linux__
    malloc_trim(0);
    #endif

    return removingTransactions;
}

bool CWallet::DeleteWalletTransactions(const CBlockIndex* pindex, bool fRescan) {

      AssertLockHeld(cs_main);
      AssertLockHeld(cs_wallet);

      int nDeleteAfter = (int)fDeleteTransactionsAfterNBlocks;
      bool runCompact = false;
      bool deletedTransactions = false;
      auto startTime = GetTime();

      if (pindex && fTxDeleteEnabled) {

        //Check for acentries - exit function if found
        {
            std::list<CAccountingEntry> acentries;
            CWalletDB walletdb(strWalletFile);
            walletdb.ListAccountCreditDebit("*", acentries);
            if (acentries.size() > 0) {
                LogPrintf("deletetx not compatible to account entries\n");
                return false;
            }
        }
        //delete transactions

        //Sort Transactions by block and block index
        int64_t maxOrderPos = 0;
        std::map<std::pair<int,int>, CWalletTx*> mapSorted;
        ReorderWalletTransactions(mapSorted, maxOrderPos);
        if (maxOrderPos > int64_t(mapSorted.size())*10) {
          //reset the postion when the max postion is 10x bigger than the
          //number of transactions in the wallet
          UpdateWalletTransactionOrder(mapSorted, true);
        }
        else {
          UpdateWalletTransactionOrder(mapSorted, false);
        }
        auto reorderTime = GetTime();
        LogPrint("deletetx","Delete Tx - Time to Reorder %s\n", DateTimeStrFormat("%H:%M:%S", reorderTime-startTime));

        //Process Transactions in sorted order
        int txConflictCount = 0;
        int txUnConfirmed = 0;
        int txCount = 0;
        int txSaveCount = 0;
        std::vector<uint256> removeTxs;
        std::vector<uint256> removeArcTxs;

        for (auto & item : mapSorted)
        {

          CWalletTx* pwtx = item.second;
          const uint256& wtxid = pwtx->GetHash();
          bool deleteTx = true;
          txCount += 1;
          int wtxDepth = pwtx->GetDepthInMainChain();

          //Keep anything newer than N Blocks
          if (wtxDepth == 0)
            txUnConfirmed++;

          if (wtxDepth < nDeleteAfter && wtxDepth >= 0) {
            LogPrint("deletetx","DeleteTx - Transaction above minimum depth, tx %s\n", pwtx->GetHash().ToString());
            deleteTx = false;
            txSaveCount++;
            continue;
          } else if (wtxDepth == -1) {
            //Enabled by default
            if (!fTxConflictDeleteEnabled) {
              LogPrint("deletetx","DeleteTx - Conflict delete is not enabled tx %s\n", pwtx->GetHash().ToString());
              deleteTx = false;
              txSaveCount++;
              continue;
            } else {
              removeArcTxs.push_back(wtxid);
              txConflictCount++;
            }
          } else {

            //Check for unspent inputs or spend less than N Blocks ago. (Sapling)
            for (auto & pair : pwtx->mapSaplingNoteData) {
              SaplingNoteData nd = pair.second;
              if (!nd.nullifier || pwalletMain->GetSaplingSpendDepth(*nd.nullifier) <= fDeleteTransactionsAfterNBlocks) {
                LogPrint("deletetx","DeleteTx - Unspent sapling input tx %s\n", pwtx->GetHash().ToString());
                deleteTx = false;
                continue;
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for outputs that no longer have parents in the wallet. Exclude parents that are in the same transaction. (Sapling)
            for (int i = 0; i < pwtx->vShieldedSpend.size(); i++) {
              const SpendDescription& spendDesc = pwtx->vShieldedSpend[i];
              if (pwalletMain->IsSaplingNullifierFromMe(spendDesc.nullifier)) {
                const uint256& parentHash = pwalletMain->mapSaplingNullifiersToNotes[spendDesc.nullifier].hash;
                const CWalletTx* parent = pwalletMain->GetWalletTx(parentHash);
                if (parent != NULL && parentHash != wtxid) {
                  LogPrint("deletetx","DeleteTx - Parent of sapling tx %s found\n", pwtx->GetHash().ToString());
                  deleteTx = false;
                  continue;
                }
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for unspent inputs or spend less than N Blocks ago. (Sprout)
            for (auto & pair : pwtx->mapSproutNoteData) {
              SproutNoteData nd = pair.second;
              if (!nd.nullifier || pwalletMain->GetSproutSpendDepth(*nd.nullifier) <= fDeleteTransactionsAfterNBlocks) {
                LogPrint("deletetx","DeleteTx - Unspent sprout input tx %s\n", pwtx->GetHash().ToString());
                deleteTx = false;
                continue;
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for outputs that no longer have parents in the wallet. Exclude parents that are in the same transaction. (Sprout)
            for (int i = 0; i < pwtx->vjoinsplit.size(); i++) {
              const JSDescription& jsdesc = pwtx->vjoinsplit[i];
              for (const uint256 &nullifier : jsdesc.nullifiers) {
                // JSOutPoint op = pwalletMain->mapSproutNullifiersToNotes[nullifier];
                if (pwalletMain->IsSproutNullifierFromMe(nullifier)) {
                  const uint256& parentHash = pwalletMain->mapSproutNullifiersToNotes[nullifier].hash;
                  const CWalletTx* parent = pwalletMain->GetWalletTx(parentHash);
                  if (parent != NULL && parentHash != wtxid) {
                    LogPrint("deletetx","DeleteTx - Parent of sprout tx %s found\n", pwtx->GetHash().ToString());
                    deleteTx = false;
                    continue;
                  }
                }
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for unspent inputs or spend less than N Blocks ago. (Transparent)
            for (unsigned int i = 0; i < pwtx->vout.size(); i++) {
              CTxDestination address;
              ExtractDestination(pwtx->vout[i].scriptPubKey, address);
              if(IsMine(pwtx->vout[i])) {
                if (pwalletMain->GetSpendDepth(pwtx->GetHash(), i) <= fDeleteTransactionsAfterNBlocks) {
                  LogPrint("deletetx","DeleteTx - Unspent transparent input tx %s\n", pwtx->GetHash().ToString());
                  deleteTx = false;
                  continue;
                }
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Chcek for output with that no longer have parents in the wallet. (Transparent)
            for (int i = 0; i < pwtx->vin.size(); i++) {
              const CTxIn& txin = pwtx->vin[i];
              const uint256& parentHash = txin.prevout.hash;
              const CWalletTx* parent = pwalletMain->GetWalletTx(txin.prevout.hash);
              if (parent != NULL && parentHash != wtxid) {
                LogPrint("deletetx","DeleteTx - Parent of transparent tx %s found\n", pwtx->GetHash().ToString());
                deleteTx = false;
                continue;
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Keep Last N Transactions
            if (mapSorted.size() - txCount < fKeepLastNTransactions + txConflictCount + txUnConfirmed) {
              LogPrint("deletetx","DeleteTx - Transaction set position %i, tx %s\n", mapSorted.size() - txCount, wtxid.ToString());
              deleteTx = false;
              txSaveCount++;
              continue;
            }
          }

          //Collect everything else for deletion
          if (deleteTx && int(removeTxs.size()) < MAX_DELETE_TX_SIZE) {
            removeTxs.push_back(wtxid);
            runCompact = true;
          } else {
            break; //no need to continue with the loop
          }
        }

        auto selectTime = GetTime();
        LogPrint("deletetx","Delete Tx - Time to Select %s\n", DateTimeStrFormat("%H:%M:%S", selectTime - reorderTime));

        //Delete Transactions from wallet
        deletedTransactions = DeleteTransactions(removeTxs, removeArcTxs, fRescan);

        auto deleteTime = GetTime();
        LogPrint("deletetx","Delete Tx - Time to Delete %s\n", DateTimeStrFormat("%H:%M:%S", deleteTime - selectTime));
        LogPrintf("Delete Tx - Total Transaction Count %i, Transactions Deleted %i\n", txCount, int(removeTxs.size()));

        if (runCompact) {
          CWalletDB::Compact(*bitdb,strWalletFile);
        }

        auto totalTime = GetTime();
        LogPrint("deletetx","Delete Tx - Time to Run Total Function %s\n", DateTimeStrFormat("%H:%M:%S", totalTime - startTime));
      }

      fRunSetBestChain = deletedTransactions;
      return deletedTransactions;
}


bool CWallet::initalizeArcTx() {
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
        const uint256& wtxid = (*it).first;
        const CWalletTx wtx = (*it).second;
        int txHeight = chainActive.Tip()->nHeight - wtx.GetDepthInMainChain();

        if (wtx.GetDepthInMainChain() > 0) {
            map<uint256, ArchiveTxPoint>::iterator ait = mapArcTxs.find(wtxid);
            if (ait == mapArcTxs.end()){
                return false;
            }
        }

        //Check for sporutdata and rescan if found to remove
        if (wtx.mapSproutNoteData.size() > 0) {
            return false;
        }

        //Initalize in memory saplingnotedata
        for (uint32_t i = 0; i < wtx.vShieldedOutput.size(); ++i) {
            auto op = SaplingOutPoint(wtxid, i);

            if (wtx.mapSaplingNoteData.count(op) != 0) {
                auto nd = wtx.mapSaplingNoteData.at(op);
                auto decrypted = wtx.DecryptSaplingNote(Params().GetConsensus(), txHeight, op);
                if (decrypted) {
                    nd.value = decrypted->first.value();
                    nd.address = decrypted->second;
                    //Set the updated Sapling Note Data
                    it->second.mapSaplingNoteData[op] = nd;
                }
            }
        }
    }

    for (map<uint256, ArchiveTxPoint>::iterator it = mapArcTxs.begin(); it != mapArcTxs.end(); it++) {
        //Add to mapAddressTxids
        AddToArcTxs(it->first, it->second);
    }

  return true;

}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate, bool fIgnoreBirthday, bool LockOnFinish)
{
    if (nMaxConnections == 0) {
        //Ignore function for cold storage offline mode
        return false;
    }
    LOCK2(cs_main, cs_wallet);
    //Notify GUI of rescan
    NotifyRescanStarted();

    int ret = 0;
    int64_t nNow = GetTime();
    const CChainParams& chainParams = Params();

    CBlockIndex* pindex = pindexStart;

    //Reset the wallet location to the rescan start. This will force the rescan to start over
    //if the wallet is killed part way through
    currentBlock = chainActive.GetLocator(pindex);
    chainHeight = pindex->nHeight;
    SetBestChain(currentBlock, chainHeight);

    std::set<uint256> txList;
    std::set<uint256> txListOriginal;

    //Collect Sapling Addresses to notify GUI after rescan
    std::set<SaplingPaymentAddress> addressesFound;

    {
        //Lock cs_keystore to prevent wallet from locking during rescan
        LOCK(cs_KeyStore);

        //Get List of current list of txids
        for (map<uint256, ArchiveTxPoint>::iterator it = pwalletMain->mapArcTxs.begin(); it != pwalletMain->mapArcTxs.end(); ++it)
        {
            txListOriginal.insert((*it).first);
        }
        for (map<uint256, CWalletTx>::iterator it = pwalletMain->mapWallet.begin(); it != pwalletMain->mapWallet.end(); ++it)
        {
            txListOriginal.insert((*it).first);
        }

        // no need to read and scan block, if block was created before
        // our wallet birthday (as adjusted for block time variability)
        while (!fIgnoreBirthday && pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)) && (pindex->nHeight < pwalletMain->nBirthday))
            pindex = chainActive.Next(pindex);

        uiInterface.ShowProgress(_("Rescanning..."), 0, false); // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
        double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false);
        double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);
        while (pindex)
        {
            //exit loop if trying to shutdown
            if (ShutdownRequested()) {
                break;
            }

            if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
            {
                scanperc = (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100);
                uiInterface.ShowProgress(_(("Rescanning - Currently on block " + std::to_string(pindex->nHeight) + "...").c_str()), std::max(1, std::min(99, scanperc)), false);
            }

            bool blockInvolvesMe = false;
            CBlock block;
            ReadBlockFromDisk(block, pindex,1);

            std::vector<CTransaction> vOurs;
            AddToWalletIfInvolvingMe(block.vtx, vOurs, &block, pindex->nHeight, fUpdate, addressesFound, true);

            for (int i = 0; i < vOurs.size(); i++) {
                blockInvolvesMe = true;
                txList.insert(vOurs[i].GetHash());
                ret++;
            }

            SproutMerkleTree sproutTree;
            SaplingMerkleTree saplingTree;
            SaplingMerkleFrontier saplingFrontierTree;
            // This should never fail: we should always be able to get the tree
            // state on the path to the tip of our chain
            assert(pcoinsTip->GetSproutAnchorAt(pindex->hashSproutAnchor, sproutTree));
            if (pindex->pprev) {
                if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
                    assert(pcoinsTip->GetSaplingAnchorAt(pindex->pprev->hashFinalSaplingRoot, saplingTree));
                    assert(pcoinsTip->GetSaplingFrontierAnchorAt(pindex->pprev->hashFinalSaplingRoot, saplingFrontierTree));
                }
            }

            // Build inital witness caches
            if (blockInvolvesMe)
                BuildWitnessCache(pindex, true);

            //Delete Transactions
            if (pindex->nHeight % fDeleteInterval == 0)
                while(DeleteWalletTransactions(pindex, true)) {}

            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex));
            }
            pindex = chainActive.Next(pindex);
        }

        uiInterface.ShowProgress(_("Rescanning..."), 100, false); // hide progress dialog in GUI

        //Write all transactions ant block loacator to the wallet
        currentBlock = chainActive.GetLocator();
        chainHeight = chainActive.Tip()->nHeight;
        SetBestChain(currentBlock, chainHeight);

        //Delete transactions
        while(DeleteWalletTransactions(chainActive.Tip(), true)) {}

        //Update all witness caches
        BuildWitnessCache(chainActive.Tip(), false);

        //Write everything to the wallet
        SetBestChain(currentBlock, chainHeight);

        if (LockOnFinish && IsCrypted()) {
            Lock();
        }
    }

    //Notfiy GUI of all new addresses found
    for (std::set<SaplingPaymentAddress>::iterator it = addressesFound.begin(); it != addressesFound.end(); it++) {
        SetZAddressBook(*it, "z-sapling", "", true);
    }

    //Notify GUI of all new transactions found
    for (set<uint256>::iterator it = txList.begin(); it != txList.end(); ++it)
    {
        bool fInsertedNew = (txListOriginal.count(*it) == 0);
        NotifyTransactionChanged(this, *it, fInsertedNew ? CT_NEW : CT_UPDATED);
    }

    //Notify GUI of changes in balances
    NotifyBalanceChanged();

    //Notify GUI Rescan is complete
    NotifyRescanComplete();

    return ret;
}

void CWallet::ReacceptWalletTransactions()
{
    if ( IsInitialBlockDownload() )
        return;
    // If transactions aren't being broadcasted, don't let them into local mempool either
    if (!fBroadcastTransactions)
        return;
    LOCK2(cs_main, cs_wallet);
    std::map<int64_t, CWalletTx*> mapSorted;

    // Sort pending wallet transactions based on their initial wallet insertion order
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
    {
        const uint256& wtxid = item.first;
        CWalletTx& wtx = item.second;
        assert(wtx.GetHash() == wtxid);

        int nDepth = wtx.GetDepthInMainChain();

        if (!wtx.IsCoinBase() && nDepth < 0) {
            mapSorted.insert(std::make_pair(wtx.nOrderPos, &wtx));
        }
    }

    std::vector<uint256> vwtxh;

    // Try to add wallet transactions to memory pool
    BOOST_FOREACH(PAIRTYPE(const int64_t, CWalletTx*)& item, mapSorted)
    {
        CWalletTx& wtx = *(item.second);

        LOCK(mempool.cs);
        CValidationState state;
        // attempt to add them, but don't set any DOS level
        if (!::AcceptToMemoryPool(mempool, state, wtx, false, NULL, true, 0))
        {
            int nDoS;
            bool invalid = state.IsInvalid(nDoS);

            // log rejection and deletion
            //printf("ERROR reaccepting wallet transaction %s to mempool, reason: %s, DoS: %d\n", wtx.GetHash().ToString().c_str(), state.GetRejectReason().c_str(), nDoS);

            if (!wtx.IsCoinBase() && invalid && nDoS > 0 && state.GetRejectReason() != "tx-overwinter-expired")
            {
                LogPrintf("erasing transaction %s\n", wtx.GetHash().GetHex().c_str());
                vwtxh.push_back(wtx.GetHash());
            }
        }
    }
    for (auto hash : vwtxh)
    {
        EraseFromWallet(hash);
    }
}

bool CWalletTx::RelayWalletTransaction()
{
    if ( pwallet == 0 )
    {
        //fprintf(stderr,"unexpected null pwallet in RelayWalletTransaction\n");
        return(false);
    }
    assert(pwallet->GetBroadcastTransactions());
    if (!IsCoinBase())
    {
        if (GetDepthInMainChain() == 0)
        {
            // if tx is expired, dont relay
            LogPrintf("Relaying wtx %s\n", GetHash().ToString());
            RelayTransaction((CTransaction)*this);
            return true;
        }
    }
    return false;
}

set<uint256> CWalletTx::GetConflicts() const
{
    set<uint256> result;
    if (pwallet != NULL)
    {
        uint256 myHash = GetHash();
        result = pwallet->GetConflicts(myHash);
        result.erase(myHash);
    }
    return result;
}

CAmount CWalletTx::GetDebit(const isminefilter& filter) const
{
    if (vin.empty())
        return 0;

    CAmount debit = 0;
    if(filter & ISMINE_SPENDABLE)
    {
        if (fDebitCached)
            debit += nDebitCached;
        else
        {
            nDebitCached = pwallet->GetDebit(*this, ISMINE_SPENDABLE);
            fDebitCached = true;
            debit += nDebitCached;
        }
    }
    if(filter & ISMINE_WATCH_ONLY)
    {
        if(fWatchDebitCached)
            debit += nWatchDebitCached;
        else
        {
            nWatchDebitCached = pwallet->GetDebit(*this, ISMINE_WATCH_ONLY);
            fWatchDebitCached = true;
            debit += nWatchDebitCached;
        }
    }
    return debit;
}

CAmount CWalletTx::GetCredit(const isminefilter& filter) const
{
    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    int64_t credit = 0;
    if (filter & ISMINE_SPENDABLE)
    {
        // GetBalance can assume transactions in mapWallet won't change
        if (fCreditCached)
            credit += nCreditCached;
        else
        {
            nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
            fCreditCached = true;
            credit += nCreditCached;
        }
    }
    if (filter & ISMINE_WATCH_ONLY)
    {
        if (fWatchCreditCached)
            credit += nWatchCreditCached;
        else
        {
            nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
            fWatchCreditCached = true;
            credit += nWatchCreditCached;
        }
    }
    return credit;
}

CAmount CWalletTx::GetImmatureCredit(bool fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureCreditCached)
            return nImmatureCreditCached;
        nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
        fImmatureCreditCached = true;
        return nImmatureCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableCredit(bool fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableCreditCached)
        return nAvailableCreditCached;

    CAmount nCredit = 0;
    uint256 hashTx = GetHash();
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(hashTx, i))
        {
            nCredit += pwallet->GetCredit(*this, i, ISMINE_SPENDABLE);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableCreditCached = nCredit;
    fAvailableCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool& fUseCache) const
{
    if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain())
    {
        if (fUseCache && fImmatureWatchCreditCached)
            return nImmatureWatchCreditCached;
        nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
        fImmatureWatchCreditCached = true;
        return nImmatureWatchCreditCached;
    }

    return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit(const bool& fUseCache) const
{
    if (pwallet == 0)
        return 0;

    // Must wait until coinbase is safely deep enough in the chain before valuing it
    if (IsCoinBase() && GetBlocksToMaturity() > 0)
        return 0;

    if (fUseCache && fAvailableWatchCreditCached)
        return nAvailableWatchCreditCached;

    CAmount nCredit = 0;
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        if (!pwallet->IsSpent(GetHash(), i))
        {
            nCredit += pwallet->GetCredit(*this, i, ISMINE_WATCH_ONLY);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
        }
    }

    nAvailableWatchCreditCached = nCredit;
    fAvailableWatchCreditCached = true;
    return nCredit;
}

CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

bool CWalletTx::InMempool() const
{
    LOCK(mempool.cs);
    return mempool.exists(GetHash());
}

bool CWalletTx::IsTrusted() const
{
    // Quick answer in most cases
    if (!CheckFinalTx(*this))
        return false;
    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1)
        return true;
    if (nDepth < 0)
        return false;
    if (!bSpendZeroConfChange || !IsFromMe(ISMINE_ALL)) // using wtx's cached debit
        return false;

    // Trusted if all inputs are from us and are in the mempool:
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        // Transactions not sent by us: not trusted
        const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
        if (parent == NULL)
            return false;
        const CTxOut& parentOut = parent->vout[txin.prevout.n];
        if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE)
            return false;
    }
    return true;
}

std::vector<uint256> CWallet::ResendWalletTransactionsBefore(int64_t nTime)
{
    std::vector<uint256> result;

    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<unsigned int, CWalletTx*> mapSorted;
    uint32_t now = (uint32_t)time(NULL);
    std::vector<uint256> vwtxh;
    BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
    {
        CWalletTx& wtx = item.second;
        // Don't rebroadcast if newer than nTime:
        if (wtx.nTimeReceived > nTime)
            continue;

        if ( (wtx.nLockTime >= LOCKTIME_THRESHOLD && wtx.nLockTime < now-KOMODO_MAXMEMPOOLTIME) )
        {
            //LogPrintf("skip Relaying wtx %s nLockTime %u vs now.%u\n", wtx.GetHash().ToString(),(uint32_t)wtx.nLockTime,now);
            //vwtxh.push_back(wtx.GetHash());
            continue;
        }
        mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    BOOST_FOREACH(PAIRTYPE(const unsigned int, CWalletTx*)& item, mapSorted)
    {
        if ( item.second != 0 )
        {
            CWalletTx &wtx = *item.second;
            if (wtx.RelayWalletTransaction())
                result.push_back(wtx.GetHash());
        }
    }
    for (auto hash : vwtxh)
    {
        EraseFromWallets(hash);
    }
    return result;
}

void CWallet::ResendWalletTransactions(int64_t nBestBlockTime)
{
    // Do this infrequently and randomly to avoid giving away
    // that these are our transactions.
    if (GetTime() < nNextResend || !fBroadcastTransactions)
        return;
    bool fFirst = (nNextResend == 0);
    nNextResend = GetTime() + GetRand(30 * 60);
    if (fFirst)
        return;

    // Only do it if there's been a new block since last time
    if (nBestBlockTime < nLastResend)
        return;
    nLastResend = GetTime();

    // Rebroadcast unconfirmed txes older than 5 minutes before the last
    // block was found:
    std::vector<uint256> relayed = ResendWalletTransactionsBefore(nBestBlockTime-5*60);
    if (!relayed.empty())
        LogPrintf("%s: rebroadcast %u unconfirmed transactions\n", __func__, relayed.size());
}

/** @} */ // end of mapWallet




/** @defgroup Actions
 *
 * @{
 */


CAmount CWallet::GetBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!CheckFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (pcoin->IsTrusted())
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }

    return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!CheckFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableWatchOnlyCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const
{
    CAmount nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetImmatureWatchOnlyCredit();
        }
    }
    return nTotal;
}

CAmount CWallet::GetAvailableBalance(const CCoinControl *coinControl) const
{
    LOCK2(cs_main, cs_wallet);

    CAmount balance = 0;
    std::vector<COutput> vCoins;
    AvailableCoins(vCoins, true, coinControl, true, true);
    for (const COutput &out : vCoins)
    {
        if (out.fSpendable)
        {
            balance += out.tx->vout[out.i].nValue;
        }
    }
    return balance;
}

/**
 * populate vCoins with vector of available COutputs.
 */
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl *coinControl, bool fIncludeZeroValue, bool fIncludeCoinBase) const
{
    uint64_t interest,*ptr;
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const uint256& wtxid = it->first;
            const CWalletTx* pcoin = &(*it).second;

            if (!CheckFinalTx(*pcoin))
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && !fIncludeCoinBase)
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (int i = 0; i < pcoin->vout.size(); i++)
            {
                isminetype mine = IsMine(pcoin->vout[i]);
                if (!(IsSpent(wtxid, i)) && mine != ISMINE_NO &&
                    !IsLockedCoin((*it).first, i) && (pcoin->vout[i].nValue > 0 || fIncludeZeroValue) &&
                    (!coinControl || !coinControl->HasSelected() || coinControl->IsSelected((*it).first, i)))
                {
                    if ( !IS_MODE_EXCHANGEWALLET )
                    {
                        uint32_t locktime; int32_t txheight; CBlockIndex *tipindex;
                        if ( chainName.isKMD() && chainActive.Tip() != 0 && chainActive.Tip()->nHeight >= 60000 )
                        {
                            if ( pcoin->vout[i].nValue >= 10*COIN )
                            {
                                if ( (tipindex= chainActive.Tip()) != 0 )
                                {
                                    komodo_accrued_interest(&txheight,&locktime,wtxid,i,0,pcoin->vout[i].nValue,(int32_t)tipindex->nHeight);
                                    interest = komodo_interestnew(txheight,pcoin->vout[i].nValue,locktime,tipindex->nTime);
                                }
                                else
                                    interest = 0;
                                if ( interest != 0 )
                                {
                                    ptr = (uint64_t *)&pcoin->vout[i].interest;
                                    (*ptr) = interest;
                                }
                                else
                                {
                                    ptr = (uint64_t *)&pcoin->vout[i].interest;
                                    (*ptr) = 0;
                                }
                            }
                            else
                            {
                                ptr = (uint64_t *)&pcoin->vout[i].interest;
                                (*ptr) = 0;
                            }
                        }
                        else
                        {
                            ptr = (uint64_t *)&pcoin->vout[i].interest;
                            (*ptr) = 0;
                        }
                    }
                    vCoins.push_back(COutput(pcoin, i, nDepth, (mine & ISMINE_SPENDABLE) != ISMINE_NO));
                }
            }
        }
    }
}

std::map<CTxDestination, std::vector<COutput>> CWallet::ListCoins() const
{
    // TODO: Add AssertLockHeld(cs_wallet) here.
    //
    // Because the return value from this function contains pointers to
    // CWalletTx objects, callers to this function really should acquire the
    // cs_wallet lock before calling it. However, the current caller doesn't
    // acquire this lock yet. There was an attempt to add the missing lock in
    // https://github.com/bitcoin/bitcoin/pull/10340, but that change has been
    // postponed until after https://github.com/bitcoin/bitcoin/pull/10244 to
    // avoid adding some extra complexity to the Qt code.

    std::map<CTxDestination, std::vector<COutput>> result;

    std::vector<COutput> availableCoins;
    AvailableCoins(availableCoins, false, NULL, true, true);

    LOCK2(cs_main, cs_wallet);
    for (auto &coin : availableCoins)
    {
        CTxDestination address;
        if (coin.fSpendable &&
            ExtractDestination(FindNonChangeParentOutput(*coin.tx, coin.i).scriptPubKey, address))
        {
            result[address].emplace_back(std::move(coin));
        }
    }

    std::vector<COutPoint> lockedCoins;
    ListLockedCoins(lockedCoins);
    for (const auto &output : lockedCoins)
    {
        auto it = mapWallet.find(output.hash);
        if (it != mapWallet.end())
        {
            int depth = it->second.GetDepthInMainChain();
            if (depth >= 0 && output.n < it->second.vout.size() &&
                IsMine(it->second.vout[output.n]) == ISMINE_SPENDABLE)
            {
                CTxDestination address;
                if (ExtractDestination(FindNonChangeParentOutput(it->second, output.n).scriptPubKey, address))
                {
                    result[address].emplace_back(
                        &it->second, output.n, depth, true /* spendable */);
                }
            }
        }
    }

    return result;
}

const CTxOut &CWallet::FindNonChangeParentOutput(const CWalletTx &tx, int output) const
{
    const CWalletTx *ptx = &tx;
    int n = output;
    while (IsChange(ptx->vout[n]) && ptx->vin.size() > 0)
    {
        const COutPoint &prevout = ptx->vin[0].prevout;
        auto it = mapWallet.find(prevout.hash);
        if (it == mapWallet.end() || it->second.vout.size() <= prevout.n ||
            !IsMine(it->second.vout[prevout.n]))
        {
            break;
        }
        //        ptx = it->second.get();
        ptx = &it->second;
        n = prevout.n;
    }
    return ptx->vout[n];
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx*,unsigned int> > >vValue, const CAmount& nTotalLower, const CAmount& nTargetValue,vector<char>& vfBest, CAmount& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    seed_insecure_rand();

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        CAmount nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                //The solver here uses a randomized algorithm,
                //the randomness serves no real security purpose but is just
                //needed to prevent degenerate behavior and it is important
                //that the rng is fast. We do not use a constant random sequence,
                //because there may be some privacy improvement by making
                //the selection random.
                if (nPass == 0 ? insecure_rand()&1 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins,set<pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet) const
{
    int32_t count = 0; //uint64_t lowest_interest = 0;
    setCoinsRet.clear();
    //memset(interests,0,sizeof(interests));
    nValueRet = 0;
    // List of values less than target
    pair<CAmount, pair<const CWalletTx*,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<CAmount>::max();
    coinLowestLarger.second.first = NULL;
    vector<pair<CAmount, pair<const CWalletTx*,unsigned int> > > vValue;
    CAmount nTotalLower = 0;

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    BOOST_FOREACH(const COutput &output, vCoins)
    {
        if (!output.fSpendable)
            continue;

        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs))
            continue;

        int i = output.i;
        CAmount n = pcoin->vout[i].nValue;

        pair<CAmount,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + CENT)
        {
            vValue.push_back(coin);
            nTotalLower += n;
            if ( nTotalLower > 4*nTargetValue + CENT )
            {
                break;
            }
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.first == NULL)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    CAmount nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
        ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        LogPrint("selectcoins", "SelectCoins() best subset: ");
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
                LogPrint("selectcoins", "%s", FormatMoney(vValue[i].first));
        LogPrint("selectcoins", "total %s\n", FormatMoney(nBest));
    }

    return true;
}

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTx*,unsigned int> >& setCoinsRet,
        CAmount& nValueRet,  bool& fOnlyCoinbaseCoinsRet, bool& fNeedCoinbaseCoinsRet,
        const CCoinControl* coinControl) const
{
    // Output parameter fOnlyCoinbaseCoinsRet is set to true when the only available coins are coinbase utxos.
    uint64_t tmp; int32_t retval;
    vector<COutput> vCoinsNoCoinbase, vCoinsWithCoinbase;
    AvailableCoins(vCoinsNoCoinbase, true, coinControl, false, false);
    AvailableCoins(vCoinsWithCoinbase, true, coinControl, false, true);
    fOnlyCoinbaseCoinsRet = vCoinsNoCoinbase.size() == 0 && vCoinsWithCoinbase.size() > 0;

    // If coinbase utxos can only be sent to zaddrs, exclude any coinbase utxos from coin selection.
    bool fProtectCoinbase = Params().GetConsensus().fCoinbaseMustBeProtected;
    vector<COutput> vCoins = (fProtectCoinbase) ? vCoinsNoCoinbase : vCoinsWithCoinbase;

    // Output parameter fNeedCoinbaseCoinsRet is set to true if coinbase utxos need to be spent to meet target amount
    if (fProtectCoinbase && vCoinsWithCoinbase.size() > vCoinsNoCoinbase.size()) {
        CAmount value = 0;
        for (const COutput& out : vCoinsNoCoinbase) {
            if (!out.fSpendable) {
                continue;
            }
            value += out.tx->vout[out.i].nValue;
            if ( !IS_MODE_EXCHANGEWALLET )
                value += out.tx->vout[out.i].interest;
        }
        if (value <= nTargetValue) {
            CAmount valueWithCoinbase = 0;
            for (const COutput& out : vCoinsWithCoinbase) {
                if (!out.fSpendable) {
                    continue;
                }
                valueWithCoinbase += out.tx->vout[out.i].nValue;
                if ( !IS_MODE_EXCHANGEWALLET )
                    valueWithCoinbase += out.tx->vout[out.i].interest;
            }
            fNeedCoinbaseCoinsRet = (valueWithCoinbase >= nTargetValue);
        }
    }
    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs)
    {
        BOOST_FOREACH(const COutput& out, vCoins)
        {
            if (!out.fSpendable)
                 continue;
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.i));
        }
        return (nValueRet >= nTargetValue);
    }
    // calculate value from preset inputs and store them
    set<pair<const CWalletTx*, uint32_t> > setPresetCoins;
    CAmount nValueFromPresetInputs = 0;

    std::vector<COutPoint> vPresetInputs;
    if (coinControl)
        coinControl->ListSelected(vPresetInputs);
    BOOST_FOREACH(const COutPoint& outpoint, vPresetInputs)
    {
        map<uint256, CWalletTx>::const_iterator it = mapWallet.find(outpoint.hash);
        if (it != mapWallet.end())
        {
            const CWalletTx* pcoin = &it->second;
            // Clearly invalid input, fail
            if (pcoin->vout.size() <= outpoint.n)
                return false;
            nValueFromPresetInputs += pcoin->vout[outpoint.n].nValue;
            if ( !IS_MODE_EXCHANGEWALLET )
                nValueFromPresetInputs += pcoin->vout[outpoint.n].interest;
            setPresetCoins.insert(make_pair(pcoin, outpoint.n));
        } else
            return false; // TODO: Allow non-wallet inputs
    }

    // remove preset inputs from vCoins
    for (vector<COutput>::iterator it = vCoins.begin(); it != vCoins.end() && coinControl && coinControl->HasSelected();)
    {
        if (setPresetCoins.count(make_pair(it->tx, it->i)))
            it = vCoins.erase(it);
        else
            ++it;
    }
    retval = false;
    if ( nTargetValue <= nValueFromPresetInputs )
        retval = true;
    else if ( SelectCoinsMinConf(nTargetValue, 1, 6, vCoins, setCoinsRet, nValueRet) != 0 )
        retval = true;
    else if ( SelectCoinsMinConf(nTargetValue, 1, 1, vCoins, setCoinsRet, nValueRet) != 0 )
        retval = true;
    else if ( bSpendZeroConfChange && SelectCoinsMinConf(nTargetValue, 0, 1, vCoins, setCoinsRet, nValueRet) != 0 )
        retval = true;
    // because SelectCoinsMinConf clears the setCoinsRet, we now add the possible inputs to the coinset
    setCoinsRet.insert(setPresetCoins.begin(), setPresetCoins.end());
    // add preset inputs to the total value selected
    nValueRet += nValueFromPresetInputs;
    return retval;
}

/****
 * @brief add vIns to transaction
 * @param tx the transaction with vouts
 * @param nFeeRet
 * @param nChangePosRet
 * @param strFailReason
 * @returns true on success
 */
bool CWallet::FundTransaction(CMutableTransaction& tx, CAmount &nFeeRet, int& nChangePosRet,
        std::string& strFailReason)
{
    vector<CRecipient> vecSend;

    // Turn the txout set into a CRecipient vector
    BOOST_FOREACH(const CTxOut& txOut, tx.vout)
    {
        CRecipient recipient = {txOut.scriptPubKey, txOut.nValue, false};
        vecSend.push_back(recipient);
    }

    CCoinControl coinControl;
    coinControl.fAllowOtherInputs = true;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
        coinControl.Select(txin.prevout);

    CReserveKey reservekey(this);
    CWalletTx wtx;

    CAmount nMinFeeOverride = 0;
    if (!CreateTransaction(vecSend, wtx, reservekey, nFeeRet, nChangePosRet, strFailReason, nMinFeeOverride, &coinControl, false))
        return false;

    if (nChangePosRet != -1)
        tx.vout.insert(tx.vout.begin() + nChangePosRet, wtx.vout[nChangePosRet]);

    // Add new txins (keeping original txin scriptSig/order)
    BOOST_FOREACH(const CTxIn& txin, wtx.vin)
    {
        bool found = false;
        BOOST_FOREACH(const CTxIn& origTxIn, tx.vin)
        {
            if (txin.prevout.hash == origTxIn.prevout.hash && txin.prevout.n == origTxIn.prevout.n)
            {
                found = true;
                break;
            }
        }
        if (!found)
            tx.vin.push_back(txin);
    }

    return true;
}

/*****
 * @brief create a transaction
 * @param vecSend who to send to
 * @param wtxNew wallet transaction
 * @param reservekey
 * @param nFeeRet
 * @param nChangePosRet
 * @param strFailReason
 * @param coinControl
 * @param sign true to sign inputs
 */
bool CWallet::CreateTransaction(const vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey,
        CAmount& nFeeRet, int& nChangePosRet, std::string& strFailReason, CAmount& nMinFeeOverride, const CCoinControl* coinControl,
        bool sign)
{
    uint64_t interest2 = 0; CAmount nValue = 0; unsigned int nSubtractFeeFromAmount = 0;
    BOOST_FOREACH (const CRecipient& recipient, vecSend)
    {
        if (nValue < 0 || recipient.nAmount < 0)
        {
            strFailReason = _("Transaction amounts must be positive");
            return false;
        }
        nValue += recipient.nAmount;

        if (recipient.fSubtractFeeFromAmount)
            nSubtractFeeFromAmount++;
    }
    if (vecSend.empty() || nValue < 0)
    {
        strFailReason = _("Transaction amounts must be positive");
        return false;
    }

    wtxNew.fTimeReceivedIsTxTime = true;
    wtxNew.BindWallet(this);
    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);

    if (IS_MODE_EXCHANGEWALLET && chainName.isKMD())
        txNew.nLockTime = 0;
    else
    {
        if ( !komodo_hardfork_active((uint32_t)chainActive.Tip()->nTime) )
            txNew.nLockTime = (uint32_t)chainActive.Tip()->nTime + 1; // set to a time close to now
        else
            txNew.nLockTime = (uint32_t)chainActive.Tip()->GetMedianTimePast();
    }

    // Activates after Overwinter network upgrade
    if (NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
        if (txNew.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD){
            strFailReason = _("nExpiryHeight must be less than TX_EXPIRY_HEIGHT_THRESHOLD.");
            return false;
        }
    }

    unsigned int max_tx_size = MAX_TX_SIZE_AFTER_SAPLING;
    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        max_tx_size = MAX_TX_SIZE_BEFORE_SAPLING;
    }
/*
    // Discourage fee sniping.
    //
    // However because of a off-by-one-error in previous versions we need to
    // neuter it by setting nLockTime to at least one less than nBestHeight.
    // Secondly currently propagation of transactions created for block heights
    // corresponding to blocks that were just mined may be iffy - transactions
    // aren't re-accepted into the mempool - we additionally neuter the code by
    // going ten blocks back. Doesn't yet do anything for sniping, but does act
    // to shake out wallet bugs like not showing nLockTime'd transactions at
    // all.
    txNew.nLockTime = std::max(0, chainActive.Height() - 10);

    // Secondly occasionally randomly pick a nLockTime even further back, so
    // that transactions that are delayed after signing for whatever reason,
    // e.g. high-latency mix networks and some CoinJoin implementations, have
    // better privacy.
    if (GetRandInt(10) == 0)
        txNew.nLockTime = std::max(0, (int)txNew.nLockTime - GetRandInt(100));

    assert(txNew.nLockTime <= (unsigned int)chainActive.Height());
    assert(txNew.nLockTime < LOCKTIME_THRESHOLD);*/

    {
        FeeCalculation feeCalc;

        LOCK2(cs_main, cs_wallet);
        {
            nFeeRet = 0;
            while (true)
            {
                //interest = 0;
                txNew.vin.clear();
                txNew.vout.clear();
                wtxNew.fFromMe = true;
                nChangePosRet = -1;
                bool fFirst = true;

                CAmount nTotalValue = nValue;
                if (nSubtractFeeFromAmount == 0)
                    nTotalValue += nFeeRet;
                double dPriority = 0;
                // vouts to the payees
                BOOST_FOREACH (const CRecipient& recipient, vecSend)
                {
                    CTxOut txout(recipient.nAmount, recipient.scriptPubKey);

                    if (recipient.fSubtractFeeFromAmount)
                    {
                        txout.nValue -= nFeeRet / nSubtractFeeFromAmount; // Subtract fee equally from each selected recipient

                        if (fFirst) // first receiver pays the remainder not divisible by output count
                        {
                            fFirst = false;
                            txout.nValue -= nFeeRet % nSubtractFeeFromAmount;
                        }
                    }

                    if (txout.IsDust(::minRelayTxFee))
                    {
                        if (recipient.fSubtractFeeFromAmount && nFeeRet > 0)
                        {
                            if (txout.nValue < 0)
                                strFailReason = _("The transaction amount is too small to pay the fee");
                            else
                                strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                        }
                        else
                            strFailReason = _("Transaction amount too small");
                        return false;
                    }
                    txNew.vout.push_back(txout);
                }

                // Choose coins to use
                set<pair<const CWalletTx*,unsigned int> > setCoins;
                CAmount nValueIn = 0;
                bool fOnlyCoinbaseCoins = false;
                bool fNeedCoinbaseCoins = false;
                interest2 = 0;
                if (!SelectCoins(nTotalValue, setCoins, nValueIn, fOnlyCoinbaseCoins, fNeedCoinbaseCoins, coinControl))
                {
                    if (fOnlyCoinbaseCoins && Params().GetConsensus().fCoinbaseMustBeProtected) {
                        strFailReason = _("Coinbase funds can only be sent to a zaddr");
                    } else if (fNeedCoinbaseCoins) {
                        strFailReason = _("Insufficient funds, coinbase funds can only be spent after they have been sent to a zaddr");
                    } else {
                        strFailReason = _("Insufficient funds");
                    }
                    return false;
                }
                BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
                {
                    CAmount nCredit = pcoin.first->vout[pcoin.second].nValue;
                    //The coin age after the next block (depth+1) is used instead of the current,
                    //reflecting an assumption the user would accept a bit more delay for
                    //a chance at a free transaction.
                    //But mempool inputs might still be in the mempool, so their age stays 0
                    //fprintf(stderr,"nCredit %.8f interest %.8f\n",(double)nCredit/COIN,(double)pcoin.first->vout[pcoin.second].interest/COIN);
                    if ( !IS_MODE_EXCHANGEWALLET && chainName.isKMD() )
                    {
                        interest2 += pcoin.first->vout[pcoin.second].interest;
                        //fprintf(stderr,"%.8f ",(double)pcoin.first->vout[pcoin.second].interest/COIN);
                    }
                    int age = pcoin.first->GetDepthInMainChain();
                    if (age != 0)
                        age += 1;
                    dPriority += (double)nCredit * age;
                }
                if ( chainName.isKMD() && DONATION_PUBKEY.size() == 66 && interest2 > 5000 )
                {
                    CScript scriptDonation = CScript() << ParseHex(DONATION_PUBKEY) << OP_CHECKSIG;
                    CTxOut newTxOut(interest2,scriptDonation);
                    int32_t nDonationPosRet = txNew.vout.size() - 1; // dont change first or last
                    vector<CTxOut>::iterator position = txNew.vout.begin()+nDonationPosRet;
                    txNew.vout.insert(position, newTxOut);
                    interest2 = 0;
                }
                CAmount nChange = (nValueIn - nValue + interest2);
//fprintf(stderr,"wallet change %.8f (%.8f - %.8f) interest2 %.8f total %.8f\n",(double)nChange/COIN,(double)nValueIn/COIN,(double)nValue/COIN,(double)interest2/COIN,(double)nTotalValue/COIN);
                if (nSubtractFeeFromAmount == 0)
                    nChange -= nFeeRet;

                if (nChange > 0)
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                        scriptChange = GetScriptForDestination(coinControl->destChange);

                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        if ( USE_EXTERNAL_PUBKEY == 0 )
                        {
                            bool ret;
                            ret = reservekey.GetReservedKey(vchPubKey);
                            assert(ret); // should never fail, as we just unlocked
                            scriptChange = GetScriptForDestination(vchPubKey.GetID());
                        }
                        else
                        {
                            //fprintf(stderr,"use notary pubkey\n");
                            scriptChange = CScript() << ParseHex(NOTARY_PUBKEY) << OP_CHECKSIG;
                        }
                    }

                    CTxOut newTxOut(nChange, scriptChange);

                    // We do not move dust-change to fees, because the sender would end up paying more than requested.
                    // This would be against the purpose of the all-inclusive feature.
                    // So instead we raise the change and deduct from the recipient.
                    if (nSubtractFeeFromAmount > 0 && newTxOut.IsDust(::minRelayTxFee))
                    {
                        CAmount nDust = newTxOut.GetDustThreshold(::minRelayTxFee) - newTxOut.nValue;
                        newTxOut.nValue += nDust; // raise change until no more dust
                        for (unsigned int i = 0; i < vecSend.size(); i++) // subtract from first recipient
                        {
                            if (vecSend[i].fSubtractFeeFromAmount)
                            {
                                txNew.vout[i].nValue -= nDust;
                                if (txNew.vout[i].IsDust(::minRelayTxFee))
                                {
                                    strFailReason = _("The transaction amount is too small to send after the fee has been deducted");
                                    return false;
                                }
                                break;
                            }
                        }
                    }

                    // Never create dust outputs; if we would, just
                    // add the dust to the fee.
                    if (newTxOut.IsDust(::minRelayTxFee))
                    {
                        nFeeRet += nChange;
                        reservekey.ReturnKey();
                    }
                    else
                    {
                        nChangePosRet = txNew.vout.size() - 1; // dont change first or last
                        vector<CTxOut>::iterator position = txNew.vout.begin()+nChangePosRet;
                        txNew.vout.insert(position, newTxOut);
                    }
                } else reservekey.ReturnKey();

                // Fill vin
                //
                // Note how the sequence number is set to max()-1 so that the
                // nLockTime set above actually works.
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                    txNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second,CScript(),
                                              std::numeric_limits<unsigned int>::max()-1));

                // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
                size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
                {
                    if (NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
                        limit = 0;
                    }
                }
                if (limit > 0) {
                    size_t n = txNew.vin.size();
                    if (n > limit) {
                        strFailReason = _(strprintf("Too many transparent inputs %zu > limit %zu", n, limit).c_str());
                        return false;
                    }
                }

                // Grab the current consensus branch ID
                auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());

                // Sign
                int nIn = 0;
                CTransaction txNewConst(txNew);
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                {
                    bool signSuccess;
                    const CScript& scriptPubKey = coin.first->vout[coin.second].scriptPubKey;
                    SignatureData sigdata;
                    if (sign)
                        signSuccess = ProduceSignature(TransactionSignatureCreator(this, &txNewConst, nIn, coin.first->vout[coin.second].nValue, SIGHASH_ALL), scriptPubKey, sigdata, consensusBranchId);
                    else
                        signSuccess = ProduceSignature(DummySignatureCreator(this), scriptPubKey, sigdata, consensusBranchId);

                    if (!signSuccess)
                    {
                        strFailReason = _("Signing transaction failed");
                        return false;
                    } else {
                        UpdateTransaction(txNew, nIn, sigdata);
                    }

                    nIn++;
                }

                unsigned int nBytes = ::GetSerializeSize(txNew, SER_NETWORK, PROTOCOL_VERSION);

                // Remove scriptSigs if we used dummy signatures for fee calculation
                if (!sign) {
                    BOOST_FOREACH (CTxIn& vin, txNew.vin)
                        vin.scriptSig = CScript();
                }

                // Embed the constructed transaction data in wtxNew.
                *static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew);

                // Limit size
                if (nBytes >= max_tx_size)
                {
                    strFailReason = _("Transaction too large");
                    return false;
                }

                dPriority = wtxNew.ComputePriority(dPriority, nBytes);

                // Can we complete this as a free transaction?
                if (fSendFreeTransactions && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE)
                {
                    // Not enough fee: enough priority?
                    double dPriorityNeeded = mempool.estimatePriority(nTxConfirmTarget);
                    // Not enough mempool history to estimate: use hard-coded AllowFree.
                    if (dPriorityNeeded <= 0 && AllowFree(dPriority))
                        break;

                    // Small enough, and priority high enough, to send for free
                    if (dPriorityNeeded > 0 && dPriority >= dPriorityNeeded)
                        break;
                }

                CAmount nFeeNeeded = GetMinimumFee(nBytes, coinControl, ::mempool, ::feeEstimator, &feeCalc);

                if ( nFeeNeeded < nMinFeeOverride )
                    nFeeNeeded = nMinFeeOverride;

                // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
                // because we must be at the maximum allowed fee.
                if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes))
                {
                    strFailReason = _("Transaction too large for fee policy");
                    return false;
                }

                if (nFeeRet >= nFeeNeeded)
                    break; // Done, enough fee included.

                // Include more fee and try again.
                nFeeRet = nFeeNeeded;
                continue;
            }
        }
    }

    return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r+") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, false, pwalletdb, chainActive.Tip()->nHeight);

            // Notify that old coins are spent
            set<CWalletTx*> setCoins;
            BOOST_FOREACH(const CTxIn& txin, wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
                NotifyBalanceChanged();
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        if (fBroadcastTransactions)
        {
            // Broadcast
            if (!wtxNew.AcceptToMemoryPool(false))
            {
                fprintf(stderr,"commit failed\n");
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}

DBErrors CWallet::InitalizeCryptedLoad()
{
    return CWalletDB(strWalletFile,"cr+").InitalizeCryptedLoad(this);
}

DBErrors CWallet::LoadCryptedSeedFromDB()
{
    return CWalletDB(strWalletFile,"cr+").LoadCryptedSeedFromDB(this);
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    uiInterface.LoadWallet(this);

    return DB_LOAD_OK;
}


DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    DBErrors nZapWalletTxRet = CWalletDB(strWalletFile,"cr+").ZapWalletTx(this, vWtx);
    if (nZapWalletTxRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // that requires a new key.
        }
    }

    if (nZapWalletTxRet != DB_LOAD_OK)
        return nZapWalletTxRet;

    return DB_LOAD_OK;
}

DBErrors CWallet::ZapOldRecords()
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    return CWalletDB(strWalletFile,"cr+").ZapOldRecords(this);
}

bool CWallet::SetAddressBook(const CTxDestination& address, const string& strName, const string& strPurpose)
{
    bool fUpdated = false;

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, CAddressBookData>::iterator mi = mapAddressBook.find(address);
        fUpdated = mi != mapAddressBook.end();
        mapAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapAddressBook[address].purpose = strPurpose;
    }
    NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO,
                             strPurpose, (fUpdated ? CT_UPDATED : CT_NEW) );
    if (!fFileBacked)
        return false;

    if (!IsCrypted()) {
        if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(EncodeDestination(address), strPurpose))
            return false;
        return CWalletDB(strWalletFile).WriteName(EncodeDestination(address), strName);
    } else {

        const string strAddress = EncodeDestination(address);
        uint256 chash = HashWithFP(strAddress);
        string strPurposeIn = strPurpose;
        if (strPurposeIn.empty())
            strPurposeIn = "None";

        CKeyingMaterial vchSecretPurpose = SerializeForEncryptionInput(strAddress, strPurposeIn);
        std::vector<unsigned char> vchCryptedSecretPurpose;

        if (!EncryptSerializedWalletObjects(vchSecretPurpose, chash, vchCryptedSecretPurpose)) {
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedPurpose(strAddress, chash, vchCryptedSecretPurpose)) {
            return false;
        }

        string strNameIn = strName;
        if (strNameIn.empty())
            strNameIn = "None";

        CKeyingMaterial vchSecretName = SerializeForEncryptionInput(strAddress, strNameIn);
        std::vector<unsigned char> vchCryptedSecretName;

        if (!EncryptSerializedWalletObjects(vchSecretName, chash, vchCryptedSecretName)) {
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedName(strAddress, chash, vchCryptedSecretName)) {
            return false;
        }
    }
    return true;
}

bool CWallet::DecryptAddressBookEntry(const uint256 chash, std::vector<unsigned char> vchCryptedSecret, string& address, string& entry)
{

    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    DeserializeFromDecryptionOutput(vchSecret, address, entry);
    return HashWithFP(address) == chash;
}

bool CWallet::SetZAddressBook(const libzcash::PaymentAddress &address, const string &strName, const string &strPurpose, bool fInTransaction)
{
    bool fUpdated = false;
    {
        LOCK(cs_wallet); // mapZAddressBook
        std::map<libzcash::PaymentAddress, CAddressBookData>::iterator mi = mapZAddressBook.find(address);

        //Address found in a transaction is already present in the address book, no need to go further.
        if (fInTransaction && mi != mapZAddressBook.end())
            return true;

        fUpdated = mi != mapZAddressBook.end();
        mapZAddressBook[address].name = strName;
        if (!strPurpose.empty()) /* update purpose only if requested */
            mapZAddressBook[address].purpose = strPurpose;
    }
    NotifyZAddressBookChanged(this, address, strName, boost::apply_visitor(HaveSpendingKeyForPaymentAddress(this), address),
                              strPurpose, (fUpdated ? CT_UPDATED : CT_NEW));
    if (!fFileBacked)
        return false;

    if (!IsCrypted()) {
        if (!strPurpose.empty() && !CWalletDB(strWalletFile).WriteSaplingPurpose(EncodePaymentAddress(address), strPurpose))
            return false;
        return CWalletDB(strWalletFile).WriteSaplingName(EncodePaymentAddress(address), strName);
    } else {

        const string strAddress = EncodePaymentAddress(address);
        uint256 chash = HashWithFP(strAddress);
        string strPurposeIn = strPurpose;
        if (strPurposeIn.empty())
            strPurposeIn = "None";

        CKeyingMaterial vchSecretPurpose = SerializeForEncryptionInput(strAddress, strPurposeIn);
        std::vector<unsigned char> vchCryptedSecretPurpose;

        if (!EncryptSerializedWalletObjects(vchSecretPurpose, chash, vchCryptedSecretPurpose)) {
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedSaplingPurpose(strAddress, chash, vchCryptedSecretPurpose)) {
            return false;
        }

        string strNameIn = strName;
        if (strNameIn.empty())
            strNameIn = "None";

        CKeyingMaterial vchSecretName = SerializeForEncryptionInput(strAddress, strNameIn);
        std::vector<unsigned char> vchCryptedSecretName;

        if (!EncryptSerializedWalletObjects(vchSecretName, chash, vchCryptedSecretName)) {
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedSaplingName(strAddress, chash, vchCryptedSecretName)) {
            return false;
        }
    }

    return true;
}

bool CWallet::DelAddressBook(const CTxDestination& address)
{
    {
        LOCK(cs_wallet); // mapAddressBook

        if (IsCrypted() && IsLocked()) {
          return false;
        }

        if(fFileBacked)
        {
            // Delete destdata tuples associated with address
            std::string strAddress = EncodeDestination(address);
            BOOST_FOREACH(const PAIRTYPE(string, string) &item, mapAddressBook[address].destdata)
            {
                CWalletDB(strWalletFile).EraseDestData(strAddress, item.first);
            }
        }
        mapAddressBook.erase(address);
    }

    NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

    if (!fFileBacked)
        return false;

    if (!IsCrypted()) {
        if (!CWalletDB(strWalletFile).ErasePurpose(EncodeDestination(address)))
            return false;
        return CWalletDB(strWalletFile).EraseName(EncodeDestination(address));
    } else {

        CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
        s << EncodeDestination(address);
        uint256 chash = Hash(s.begin(), s.end());

        if (!CWalletDB(strWalletFile).EraseCryptedPurpose(chash))
            return false;
        return CWalletDB(strWalletFile).EraseCryptedName(chash);
    }
    return true;
}

bool CWallet::DelZAddressBook(const libzcash::PaymentAddress &address)
{
    {
        LOCK(cs_wallet); // mapZAddressBook

        if (fFileBacked)
        {
            // Delete destdata tuples associated with address
            std::string strAddress = EncodePaymentAddress(address);
            //!!!!! we don't delete data for z-addresses for now
            //            BOOST_FOREACH(const PAIRTYPE(string, string) &item, mapZAddressBook[address].destdata)
            //            {
            //                CWalletDB(strWalletFile).EraseDestData(strAddress, item.first);
            //            }
        }
        mapZAddressBook.erase(address);
    }

    NotifyZAddressBookChanged(this, address, "", boost::apply_visitor(HaveSpendingKeyForPaymentAddress(this), address), "", CT_DELETED);

    if (!fFileBacked)
        return false;
    //!!!!! we don't delete data for z-addresses for now
    //    CWalletDB(strWalletFile).ErasePurpose(CBitcoinAddress(address).ToString());
    //    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
    return true;
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!fFileBacked) {
        return false;
    }

    if (!IsCrypted()) {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    } else {

        uint256 chash = HashWithFP(vchPubKey);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(vchPubKey);
        std::vector<unsigned char> vchCryptedSecret;

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedDefaultKey(chash, vchCryptedSecret)) {
            return false;
        }

    }

    vchDefaultKey = vchPubKey;
    return true;
}

bool CWallet::DecryptDefaultKey(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret, CPubKey &vchPubKey)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    DeserializeFromDecryptionOutput(vchSecret, vchPubKey);
    return HashWithFP(vchPubKey) == chash;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);

        if (IsCrypted() && IsLocked()) {
            return false;
        }

        CWalletDB walletdb(strWalletFile);
        BOOST_FOREACH(int64_t nIndex, setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        int64_t nKeys = max(GetArg("-keypool", 1), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            if (!IsCrypted()) {
                if(!walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()))){
                  LogPrintf("NewKeyPool(): writing generated key failed");
                  return false;
                }
            } else {
              auto keypool = CKeyPool(GenerateNewKey());

              CKeyingMaterial vchSecret = SerializeForEncryptionInput(keypool);
              uint256 chash = HashWithFP(keypool);

              std::vector<unsigned char> vchCryptedSecret;
              if (!EncryptSerializedSecret(vchSecret, chash, vchCryptedSecret)) {
                  LogPrintf("NewKeyPool(): encrypting generated key pool failed");
                  return false;
              }

              if (!walletdb.WriteCryptedPool(nIndex, chash, vchCryptedSecret))
                  LogPrintf("NewKeyPool(): writing generated key failed");
                  return false;
            }

            setKeyPool.insert(nIndex);
        }
        LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize)
{
    {
        LOCK(cs_wallet);

        if (IsCrypted() && IsLocked()) {
            return false;
        }

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (kpSize > 0)
            nTargetSize = kpSize;
        else
            nTargetSize = max(GetArg("-keypool", 1), (int64_t) 0);

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!IsCrypted()) {
                if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                    throw runtime_error("TopUpKeyPool(): writing generated key failed");
            } else {
                auto keypool = CKeyPool(GenerateNewKey());

                CKeyingMaterial vchSecret = SerializeForEncryptionInput(keypool);
                uint256 chash = HashWithFP(keypool);

                std::vector<unsigned char> vchCryptedSecret;
                if (!EncryptSerializedSecret(vchSecret, chash, vchCryptedSecret)) {
                    LogPrintf("NewKeyPool(): encrypting generated key pool failed");
                    return false;
                }

                if (!walletdb.WriteCryptedPool(nEnd, chash, vchCryptedSecret))
                    throw runtime_error("TopUpKeyPool(): writing generated key failed");
            }
            setKeyPool.insert(nEnd);
            LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (IsCrypted() && IsLocked()) {
            return;
        }

        TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());

        if (!IsCrypted()) {
            if (!walletdb.ReadPool(nIndex, keypool)) {
                throw runtime_error("ReserveKeyFromKeyPool(): read failed");
            }
        } else {

            std::pair<uint256, std::vector<unsigned char>> vchCryptedSecretPair;
            if (!walletdb.ReadCryptedPool(nIndex, vchCryptedSecretPair)) {
                throw runtime_error("ReserveKeyFromKeyPool(): read failed");
            }

            CKeyingMaterial vchSecret;
            if (!DecryptSerializedSecret(vchCryptedSecretPair.second, vchCryptedSecretPair.first, vchSecret)) {
                throw runtime_error("ReserveKeyFromKeyPool(): DecryptKeyPool failed");
            }

            DeserializeFromDecryptionOutput(vchSecret, keypool);
            if (HashWithFP(keypool) != vchCryptedSecretPair.first) {
                throw runtime_error("ReserveKeyFromKeyPool(): deserialize failed");
            }
        }


        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool(): unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        //LogPrintf("keypool reserve %d\n", nIndex);
    }
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    //LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances()
{
    map<CTxDestination, CAmount> balances;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!CheckFinalTx(*pcoin) || !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!IsMine(pcoin->vout[i]))
                    continue;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                CAmount n = IsSpent(walletEntry.first, i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0)
        {
            bool any_mine = false;
            // group all input addresses with each other
            BOOST_FOREACH(CTxIn txin, pcoin->vin)
            {
                CTxDestination address;
                if(!IsMine(txin)) /* If this input isn't mine, ignore it */
                    continue;
                if(mapWallet.count(txin.prevout.hash) == 0)
                    continue;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                any_mine = true;
            }

            // group change with input addresses
            if (any_mine)
            {
               BOOST_FOREACH(CTxOut txout, pcoin->vout)
                   if (IsChange(txout))
                   {
                       CTxDestination txoutAddr;
                       if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                           continue;
                       grouping.insert(txoutAddr);
                   }
            }
            if (grouping.size() > 0)
            {
                groupings.insert(grouping);
                grouping.clear();
            }
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i]))
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    BOOST_FOREACH(set<CTxDestination> grouping, groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        BOOST_FOREACH(CTxDestination address, grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        BOOST_FOREACH(set<CTxDestination>* hit, hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH(CTxDestination element, *merged)
            setmap[element] = merged;
    }

    set< set<CTxDestination> > ret;
    BOOST_FOREACH(set<CTxDestination>* uniqueGrouping, uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

std::set<CTxDestination> CWallet::GetAccountAddresses(const std::string& strAccount) const
{
    LOCK(cs_wallet);
    set<CTxDestination> result;
    BOOST_FOREACH(const PAIRTYPE(CTxDestination, CAddressBookData)& item, mapAddressBook)
    {
        const CTxDestination& address = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount)
            result.insert(address);
    }
    return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress)
{
    setAddress.clear();

    if (IsCrypted() && IsLocked()) {
        return;
    }

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH(const int64_t& id, setKeyPool)
    {
        CKeyPool keypool;
        if (!IsCrypted()) {
            if (!walletdb.ReadPool(id, keypool))
                throw runtime_error("GetAllReserveKeyHashes(): read failed");
        } else {
            std::pair<uint256, std::vector<unsigned char>> vchCryptedSecretPair;
            if (!walletdb.ReadCryptedPool(id, vchCryptedSecretPair)) {
                throw runtime_error("ReserveKeyFromKeyPool(): read failed");
            }

            CKeyingMaterial vchSecret;
            if (!DecryptSerializedSecret(vchCryptedSecretPair.second, vchCryptedSecretPair.first, vchSecret)) {
                throw runtime_error("ReserveKeyFromKeyPool(): DecryptKeyPool failed");
            }

            DeserializeFromDecryptionOutput(vchSecret, keypool);
            if (HashWithFP(keypool) != vchCryptedSecretPair.first) {
                throw runtime_error("ReserveKeyFromKeyPool(): deserialize failed");
            }
        }
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes(): unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end()) {
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
            NotifyBalanceChanged();
        }
    }
}

void CWallet::LockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins()
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    COutPoint outpt(hash, n);

    return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts) const
{
    AssertLockHeld(cs_wallet); // setLockedCoins
    for (std::set<COutPoint>::iterator it = setLockedCoins.begin();
         it != setLockedCoins.end(); it++) {
        COutPoint outpt = (*it);
        vOutpts.push_back(outpt);
    }
}


// Note Locking Operations

void CWallet::LockNote(const JSOutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedSproutNotes
    setLockedSproutNotes.insert(output);
}

void CWallet::UnlockNote(const JSOutPoint& output)
{
    AssertLockHeld(cs_wallet); // setLockedSproutNotes
    setLockedSproutNotes.erase(output);
}

void CWallet::UnlockAllSproutNotes()
{
    AssertLockHeld(cs_wallet); // setLockedSproutNotes
    setLockedSproutNotes.clear();
}

bool CWallet::IsLockedNote(const JSOutPoint& outpt) const
{
    AssertLockHeld(cs_wallet); // setLockedSproutNotes

    return (setLockedSproutNotes.count(outpt) > 0);
}

std::vector<JSOutPoint> CWallet::ListLockedSproutNotes()
{
    AssertLockHeld(cs_wallet); // setLockedSproutNotes
    std::vector<JSOutPoint> vOutpts(setLockedSproutNotes.begin(), setLockedSproutNotes.end());
    return vOutpts;
}

void CWallet::LockNote(const SaplingOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.insert(output);
}

void CWallet::UnlockNote(const SaplingOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.erase(output);
}

void CWallet::UnlockAllSaplingNotes()
{
    AssertLockHeld(cs_wallet);
    setLockedSaplingNotes.clear();
}

bool CWallet::IsLockedNote(const SaplingOutPoint& output) const
{
    AssertLockHeld(cs_wallet);
    return (setLockedSaplingNotes.count(output) > 0);
}

std::vector<SaplingOutPoint> CWallet::ListLockedSaplingNotes()
{
    AssertLockHeld(cs_wallet);
    std::vector<SaplingOutPoint> vOutputs(setLockedSaplingNotes.begin(), setLockedSaplingNotes.end());
    return vOutputs;
}

/** @} */ // end of Actions

class CAffectedKeysVisitor : public boost::static_visitor<void> {
private:
    const CKeyStore &keystore;
    std::vector<CKeyID> &vKeys;

public:
    CAffectedKeysVisitor(const CKeyStore &keystoreIn, std::vector<CKeyID> &vKeysIn) : keystore(keystoreIn), vKeys(vKeysIn) {}

    void Process(const CScript &script) {
        txnouttype type;
        std::vector<CTxDestination> vDest;
        int nRequired;
        if (ExtractDestinations(script, type, vDest, nRequired)) {
            BOOST_FOREACH(const CTxDestination &dest, vDest)
                boost::apply_visitor(*this, dest);
        }
    }

    void operator()(const CKeyID &keyId) {
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CPubKey &key) {
        CKeyID keyId = key.GetID();
        if (keystore.HaveKey(keyId))
            vKeys.push_back(keyId);
    }

    void operator()(const CScriptID &scriptId) {
        CScript script;
        if (keystore.GetCScript(scriptId, script))
            Process(script);
    }

    void operator()(const CNoDestination &none) {}
};

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = chainActive[std::max(0, chainActive.Height() - 144)]; // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    BOOST_FOREACH(const CKeyID &keyid, setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = (*it).second;
        BlockMap::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second)) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            BOOST_FOREACH(const CTxOut &txout, wtx.vout) {
                // iterate over all their outputs
                CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
                BOOST_FOREACH(const CKeyID &keyid, vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->GetBlockTime() - 7200; // block times can be 2h off
}

bool CWallet::AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    if (boost::get<CNoDestination>(&dest))
        return false;

    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteDestData(EncodeDestination(dest), key, value);
}

bool CWallet::EraseDestData(const CTxDestination &dest, const std::string &key)
{
    if (!mapAddressBook[dest].destdata.erase(key))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).EraseDestData(EncodeDestination(dest), key);
}

bool CWallet::LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value)
{
    mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
    return true;
}

bool CWallet::GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const
{
    std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
    if(i != mapAddressBook.end())
    {
        CAddressBookData::StringMap::const_iterator j = i->second.destdata.find(key);
        if(j != i->second.destdata.end())
        {
            if(value)
                *value = j->second;
            return true;
        }
    }
    return false;
}

std::vector<std::string> CWallet::GetDestValues(const std::string &prefix) const
{
    LOCK(cs_wallet);
    std::vector<std::string> values;
    for (const auto &address : mapAddressBook)
    {
        for (const auto &data : address.second.destdata)
        {
            if (!data.first.compare(0, prefix.size(), prefix))
            {
                values.emplace_back(data.second);
            }
        }
    }
    return values;
}

CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

void CMerkleTx::SetMerkleBranch(const CBlock& block)
{
    CBlock blockTmp;

    // Update the tx's hashBlock
    hashBlock = block.GetHash();

    // Locate the transaction
    for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
        if (block.vtx[nIndex] == *(CTransaction*)this)
            break;
    if (nIndex == (int)block.vtx.size())
    {
        vMerkleBranch.clear();
        nIndex = -1;
        LogPrintf("ERROR: SetMerkleBranch(): couldn't find tx in block\n");
    }

    // Fill in merkle branch
    vMerkleBranch = block.GetMerkleBranch(nIndex);
}

int CMerkleTx::GetDepthInMainChainINTERNAL(const CBlockIndex* &pindexRet) const
{
    if (hashBlock.IsNull() || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !chainActive.Contains(pindex))
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(const CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    if (!IsCoinBase())
        return 0;
    int32_t depth = GetDepthInMainChain();
    int32_t ut = UnlockTime(0);
    int32_t toMaturity = (ut - chainActive.Height()) < 0 ? 0 : ut - chainActive.Height();
    //printf("depth.%i, unlockTime.%i, toMaturity.%i\n", depth, ut, toMaturity);
    ut = (Params().CoinbaseMaturity() - depth) < 0 ? 0 : Params().CoinbaseMaturity() - depth;
    return(ut < toMaturity ? toMaturity : ut);
}

bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptToMemoryPool(mempool, state, *this, fLimitFree, NULL, fRejectAbsurdFee);
}

//Get Address balances for the GUI
void CWallet::getZAddressBalances(std::map<libzcash::PaymentAddress, CAmount> &balances, int minDepth, bool requireSpendingKey)
{

    LOCK2(cs_main, cs_wallet);

    for (auto & item : mapWallet) {
        CWalletTx wtx = item.second;

        // Filter the transactions before checking for notes
        if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0)
            continue;

        //Confirmed Balance Only
        if (wtx.GetDepthInMainChain() < minDepth)
            continue;

        for (auto & pair : wtx.mapSaplingNoteData) {
            SaplingOutPoint op = pair.first;
            SaplingNoteData nd = pair.second;

            if (nd.nullifier && IsSaplingSpent(*nd.nullifier)) {
                continue;
            }

            // skip notes which cannot be spent
            if (requireSpendingKey) {
                libzcash::SaplingExtendedFullViewingKey extfvk;
                if (!(GetSaplingFullViewingKey(nd.ivk, extfvk) &&
                    HaveSaplingSpendingKey(extfvk))) {
                    continue;
                }
            }

            if (!balances.count(nd.address))
                balances[nd.address] = 0;
            balances[nd.address] += CAmount(nd.value);
        }
    }
}


bool CWallet::SaplingWalletGetMerklePathOfNote(const uint256 txid, int outidx, libzcash::MerklePath &merklePath) {
    return saplingWallet.GetMerklePathOfNote(txid, outidx, merklePath);
}

bool CWallet::SaplingWalletGetPathRootWithCMU(libzcash::MerklePath &merklePath, uint256 cmu, uint256 &anchor) {
   return saplingWallet.GetPathRootWithCMU(merklePath, cmu, anchor);
}

/**
 * Find notes in the wallet filtered by payment address, min depth and ability to spend.
 * These notes are decrypted and added to the output parameter vector, outEntries.
 */
void CWallet::GetFilteredNotes(
    std::vector<CSproutNotePlaintextEntry>& sproutEntries,
    std::vector<SaplingNoteEntry>& saplingEntries,
    std::string address,
    int minDepth,
    bool ignoreSpent,
    bool requireSpendingKey)
{
    std::set<PaymentAddress> filterAddresses;

    if (address.length() > 0) {
        filterAddresses.insert(DecodePaymentAddress(address));
    }

    GetFilteredNotes(sproutEntries, saplingEntries, filterAddresses, minDepth, INT_MAX, ignoreSpent, requireSpendingKey);
}

/**
 * Find notes in the wallet filtered by payment addresses, min depth, max depth,
 * if the note is spent, if a spending key is required, and if the notes are locked.
 * These notes are decrypted and added to the output parameter vector, outEntries.
 */
void CWallet::GetFilteredNotes(
    std::vector<CSproutNotePlaintextEntry>& sproutEntries,
    std::vector<SaplingNoteEntry>& saplingEntries,
    std::set<PaymentAddress>& filterAddresses,
    int minDepth,
    int maxDepth,
    bool ignoreSpent,
    bool requireSpendingKey,
    bool ignoreLocked)
{
    LOCK2(cs_main, cs_wallet);

    for (auto & p : mapWallet) {
        CWalletTx wtx = p.second;

        // Filter the transactions before checking for notes
        if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0)
            continue;

        if (minDepth > 1) {
            int nHeight    = tx_height(wtx.GetHash());
            int nDepth     = wtx.GetDepthInMainChain();
            int dpowconfs  = komodo_dpowconfs(nHeight,nDepth);
            if ( dpowconfs < minDepth || dpowconfs > maxDepth) {
                continue;
            }
        } else {
            if ( wtx.GetDepthInMainChain() < minDepth ||
                wtx.GetDepthInMainChain() > maxDepth) {
                continue;
            }
        }

        // for (auto & pair : wtx.mapSproutNoteData) {
        //     JSOutPoint jsop = pair.first;
        //     SproutNoteData nd = pair.second;
        //     SproutPaymentAddress pa = nd.address;
        //
        //     // skip notes which belong to a different payment address in the wallet
        //     if (!(filterAddresses.empty() || filterAddresses.count(pa))) {
        //         continue;
        //     }
        //
        //     // skip note which has been spent
        //     if (ignoreSpent && nd.nullifier && IsSproutSpent(*nd.nullifier)) {
        //         continue;
        //     }
        //
        //     // skip notes which cannot be spent
        //     if (requireSpendingKey && !HaveSproutSpendingKey(pa)) {
        //         continue;
        //     }
        //
        //     // skip locked notes
        //     if (ignoreLocked && IsLockedNote(jsop)) {
        //         continue;
        //     }
        //
        //     int i = jsop.js; // Index into CTransaction.vjoinsplit
        //     int j = jsop.n; // Index into JSDescription.ciphertexts
        //
        //     // Get cached decryptor
        //     ZCNoteDecryption decryptor;
        //     if (!GetNoteDecryptor(pa, decryptor)) {
        //         // Note decryptors are created when the wallet is loaded, so it should always exist
        //         throw std::runtime_error(strprintf("Could not find note decryptor for payment address %s", EncodePaymentAddress(pa)));
        //     }
        //
        //     // determine amount of funds in the note
        //     auto hSig = wtx.vjoinsplit[i].h_sig(*pzcashParams, wtx.joinSplitPubKey);
        //     try {
        //         SproutNotePlaintext plaintext = SproutNotePlaintext::decrypt(
        //                 decryptor,
        //                 wtx.vjoinsplit[i].ciphertexts[j],
        //                 wtx.vjoinsplit[i].ephemeralKey,
        //                 hSig,
        //                 (unsigned char) j);
        //
        //         sproutEntries.push_back(CSproutNotePlaintextEntry{jsop, pa, plaintext, wtx.GetDepthInMainChain()});
        //
        //     } catch (const note_decryption_failed &err) {
        //         // Couldn't decrypt with this spending key
        //         throw std::runtime_error(strprintf("Could not decrypt note for payment address %s", EncodePaymentAddress(pa)));
        //     } catch (const std::exception &exc) {
        //         // Unexpected failure
        //         throw std::runtime_error(strprintf("Error while decrypting note for payment address %s: %s", EncodePaymentAddress(pa), exc.what()));
        //     }
        // }

        for (auto & pair : wtx.mapSaplingNoteData) {
            SaplingOutPoint op = pair.first;
            SaplingNoteData nd = pair.second;

            auto optDeserialized = SaplingNotePlaintext::attempt_sapling_enc_decryption_deserialization(wtx.vShieldedOutput[op.n].encCiphertext, nd.ivk, wtx.vShieldedOutput[op.n].ephemeralKey);

            // The transaction would not have entered the wallet unless
            // its plaintext had been successfully decrypted previously.
            assert(optDeserialized != boost::none);

            auto notePt = optDeserialized.get();
            auto maybe_pa = nd.ivk.address(notePt.d);
            assert(static_cast<bool>(maybe_pa));
            auto pa = maybe_pa.get();

            // skip notes which belong to a different payment address in the wallet
            if (!(filterAddresses.empty() || filterAddresses.count(pa))) {
                continue;
            }

            if (ignoreSpent && nd.nullifier && IsSaplingSpent(*nd.nullifier)) {
                continue;
            }

            // skip notes which cannot be spent
            if (requireSpendingKey) {
                libzcash::SaplingExtendedFullViewingKey extfvk;
                if (!(GetSaplingFullViewingKey(nd.ivk, extfvk) &&
                    HaveSaplingSpendingKey(extfvk))) {
                    continue;
                }
            }

            // skip locked notes
            // TODO: Add locking for Sapling notes -> done
             if (ignoreLocked && IsLockedNote(op)) {
                 continue;
             }

            auto note = notePt.note(nd.ivk).get();
            saplingEntries.push_back(SaplingNoteEntry {
                op, pa, note, notePt.memo(), wtx.GetDepthInMainChain() });
        }
    }
}


//
// Shielded key and address generalizations
//

bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutViewingKey(zaddr);
}

bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk);
}

bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

bool PaymentAddressBelongsToWallet::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutSpendingKey(zaddr) || m_wallet->HaveSproutViewingKey(zaddr);
}

bool PaymentAddressBelongsToWallet::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;

    // If we have a SaplingExtendedSpendingKey in the wallet, then we will
    // also have the corresponding SaplingFullViewingKey.
    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->HaveSaplingFullViewingKey(ivk);
}

bool PaymentAddressBelongsToWallet::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

boost::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::SproutPaymentAddress &zaddr) const
{
    libzcash::SproutViewingKey vk;
    if (!m_wallet->GetSproutViewingKey(zaddr, vk)) {
        libzcash::SproutSpendingKey k;
        if (!m_wallet->GetSproutSpendingKey(zaddr, k)) {
            return boost::none;
        }
        vk = k.viewing_key();
    }
    return libzcash::ViewingKey(vk);
}

boost::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    if (m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk))
    {
        return libzcash::ViewingKey(extfvk);
    } else {
        return boost::none;
    }
}

boost::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::ViewingKey();
}

bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutSpendingKey(zaddr);
}

bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
        m_wallet->HaveSaplingSpendingKey(extfvk);
}

bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

boost::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::SproutPaymentAddress &zaddr) const
{
    libzcash::SproutSpendingKey k;
    if (m_wallet->GetSproutSpendingKey(zaddr, k)) {
        return libzcash::SpendingKey(k);
    } else {
        return boost::none;
    }
}

boost::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingExtendedSpendingKey extsk;
    if (m_wallet->GetSaplingExtendedSpendingKey(zaddr, extsk)) {
        return libzcash::SpendingKey(extsk);
    } else {
        return boost::none;
    }
}

boost::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::SpendingKey();
}

KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::SproutViewingKey &vkey) const {
    auto addr = vkey.address();

    if (m_wallet->HaveSproutSpendingKey(addr)) {
        return SpendingKeyExists;
    } else if (m_wallet->HaveSproutViewingKey(addr)) {
        return KeyAlreadyExists;
    } else if (m_wallet->AddSproutViewingKey(vkey)) {
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::SaplingExtendedFullViewingKey &extfvk) const {
    if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
        return SpendingKeyExists;
    } else if (m_wallet->HaveSaplingFullViewingKey(extfvk.fvk.in_viewing_key())) {
        return KeyAlreadyExists;
    } else if (m_wallet->AddSaplingExtendedFullViewingKey(extfvk)) {
        m_wallet->LoadSaplingWatchOnly(extfvk);
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewing key");
}

KeyAddResult AddDiversifiedViewingKeyToWallet::operator()(const libzcash::SaplingDiversifiedExtendedFullViewingKey &extdfvk) const {
    auto extfvk = extdfvk.extfvk;
    auto ivk = extfvk.fvk.in_viewing_key();
    auto addr = ivk.address(extdfvk.d).get();
    KeyAddResult result = KeyNotAdded;

    if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
        result = SpendingKeyExists;
    } else if (m_wallet->HaveSaplingFullViewingKey(ivk)) {
        result = KeyAlreadyExists;
    } else if (m_wallet->AddSaplingExtendedFullViewingKey(extfvk)) {
        m_wallet->LoadSaplingWatchOnly(extfvk);
        result = KeyAdded;
    } else {
        return KeyNotAdded;
    }


    if (m_wallet->AddSaplingIncomingViewingKey(ivk, addr)) {
        if (result == SpendingKeyExists || result == KeyAlreadyExists) {
            return KeyExistsAddressAdded;
        } else {
            return KeyAddedAddressAdded;
        }
    }

    if (result == SpendingKeyExists || result == KeyAlreadyExists) {
        return KeyExistsAddressNotAdded;
    }

    return KeyAddedAddressNotAdded;

}

KeyAddResult AddDiversifiedViewingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid diversified viewing key");
}

KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::SproutSpendingKey &sk) const {
    auto addr = sk.address();
    if (log){
        LogPrint("zrpc", "Importing zaddr %s...\n", EncodePaymentAddress(addr));
    }
    if (m_wallet->HaveSproutSpendingKey(addr)) {
        return KeyAlreadyExists;
    } else if (m_wallet-> AddSproutZKey(sk)) {
        m_wallet->mapSproutZKeyMetadata[addr].nCreateTime = nTime;
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::SaplingExtendedSpendingKey &sk) const {
    auto extfvk = sk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto addr = sk.DefaultAddress();
    {
        if (log){
            LogPrint("zrpc", "Importing zaddr %s...\n", EncodePaymentAddress(addr));
        }
        // Don't throw error in case a key is already there
        if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
            return KeyAlreadyExists;
        } else {
            if (!m_wallet-> AddSaplingZKey(sk)) {
                return KeyNotAdded;
            }

            // Sapling addresses can't have been used in transactions prior to activation.
            if (params.vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight == Consensus::NetworkUpgrade::ALWAYS_ACTIVE) {
                m_wallet->mapSaplingZKeyMetadata[ivk].nCreateTime = nTime;
            } else {
                // 154051200 seconds from epoch is Friday, 26 October 2018 00:00:00 GMT - definitely before Sapling activates
                m_wallet->mapSaplingZKeyMetadata[ivk].nCreateTime = std::max((int64_t) 154051200, nTime);
            }
            if (hdKeypath) {
                m_wallet->mapSaplingZKeyMetadata[ivk].hdKeypath = hdKeypath.get();
            }
            if (seedFpStr) {
                uint256 seedFp;
                seedFp.SetHex(seedFpStr.get());
                m_wallet->mapSaplingZKeyMetadata[ivk].seedFp = seedFp;
            }
            return KeyAdded;
        }
    }
}

KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid spending key");
}

KeyAddResult AddDiversifiedSpendingKeyToWallet::operator()(const libzcash::SaplingDiversifiedExtendedSpendingKey &extdsk) const {
    auto extfvk = extdsk.extsk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto addr = ivk.address(extdsk.d).get();
    KeyAddResult result = KeyNotAdded;


    // Don't throw error in case a key is already there
    if (m_wallet->HaveSaplingSpendingKey(extfvk)) {
        result = KeyAlreadyExists;
    } else {
        if (!m_wallet-> AddSaplingZKey(extdsk.extsk)) {
            return KeyNotAdded;
        }
        result = KeyAdded;
    }

    if (m_wallet->AddSaplingIncomingViewingKey(ivk,addr)) {
        if (result == KeyAlreadyExists) {
            return KeyExistsAddressAdded;
        } else {
            return KeyAddedAddressAdded;
        }
    }

    if (result == KeyAlreadyExists) {
        return KeyExistsAddressNotAdded;
    }

    return KeyAddedAddressNotAdded;

}

KeyAddResult AddDiversifiedSpendingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid diversified viewing key");
}
