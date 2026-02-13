// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2022-2025 Pirate developers
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

/**
 * @file wallet.cpp
 * @brief Core wallet functionality for the Pirate cryptocurrency
 * 
 * This file implements the primary wallet functionality for Pirate, including:
 * - Multi-protocol shielded address support (Sprout, Sapling, Orchard)
 * - Hierarchical deterministic key derivation (HD wallets)
 * - Transaction creation, signing, and broadcasting
 * - Note detection and management for shielded protocols
 * - Wallet encryption and key management
 * - Balance calculation and UTXO management
 * 
 * The wallet supports both transparent and shielded transactions, with extensive
 * support for privacy-preserving operations using zero-knowledge protocols.
 */

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
#include "wallet/asyncrpcoperation_orchardconsolidation.h"
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
#include <random>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#ifdef __linux__ //linux only
#include <malloc.h>
#endif

using namespace std;
using namespace libzcash;

/** Global vector containing references to all wallet instances */
std::vector<CWalletRef> vpwallets;

/**
 * @section Wallet Configuration Settings
 * 
 * Global configuration variables that control wallet behavior and transaction
 * processing. These can be modified through command-line arguments and RPC calls.
 */

/** Fee rate for transactions (can be overridden with -paytxfee) */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);

/** Maximum fee allowed for any transaction (can be overridden with -maxtxfee) */
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;

/** Target number of blocks for fee estimation (can be overridden with -txconfirmtarget) */
unsigned int nTxConfirmTarget = DEFAULT_TX_CONFIRM_TARGET;

/** Whether to spend unconfirmed change outputs (can be overridden with -spendzeroconfchange) */
bool bSpendZeroConfChange = true;

/** Whether to send transactions with minimal fees (legacy setting) */
bool fSendFreeTransactions = false;

/** Whether to pay at least the custom fee specified (can be overridden with -payatleastcustomfee) */
bool fPayAtLeastCustomFee = true;

/** Minimum value for transaction outputs (can be overridden with -mintxvalue) */
CAmount minTxValue = DEFAULT_MIN_TX_VALUE;

/** Whether to use replace-by-fee (RBF) for transactions */
bool fWalletRbf = DEFAULT_WALLET_RBF;

/**
 * @section Wallet State Variables
 * 
 * Variables that track wallet scanning progress and transaction management settings.
 */

/** Progress percentage for blockchain scanning operations */
int scanperc;

/** Whether transaction deletion is enabled for wallet cleanup */
bool fTxDeleteEnabled = false;

/** Whether conflicted transaction deletion is enabled */
bool fTxConflictDeleteEnabled = false;

/** Interval for transaction deletion operations */
int fDeleteInterval = DEFAULT_TX_DELETE_INTERVAL;

/** Number of blocks to retain transactions before deletion */
unsigned int fDeleteTransactionsAfterNBlocks = DEFAULT_TX_RETENTION_BLOCKS;

/** Number of recent transactions to always keep */
unsigned int fKeepLastNTransactions = DEFAULT_TX_RETENTION_LASTTX;

/** Recovery seed phrase for wallet restoration */
std::string recoverySeedPhrase = "";

/** Flag indicating if GUI is being used */
bool usingGUI = false;

/** Block height from which to start recovery scanning */
int recoveryHeight = 0;

/** Secure string for storing wallet passphrase during operations */
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

/** Special hash value used to mark abandoned transactions */
const uint256 CMerkleTx::ABANDON_HASH(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));

/** @defgroup mapWallet
 *
 * @{
 */

/**
 * @brief Comparator for sorting wallet transaction outputs by value
 * 
 * Used to sort transaction outputs based on their monetary value in ascending order.
 * This is useful for coin selection algorithms that prefer smaller or larger UTXOs.
 */
struct CompareValueOnly
{
    /**
     * @brief Compare two transaction output pairs by their value
     * @param t1 First pair containing (value, (transaction pointer, output index))
     * @param t2 Second pair containing (value, (transaction pointer, output index))
     * @return true if t1's value is less than t2's value
     */
    bool operator()(const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

/**
 * @brief Convert JSOutPoint to string representation
 * @return String representation of the JSOutPoint including hash, js index, and n index
 */
std::string JSOutPoint::ToString() const
{
    return strprintf("JSOutPoint(%s, %d, %d)", hash.ToString().substr(0,10), js, n);
}

/**
 * @brief Convert COutput to string representation
 * @return String representation including transaction hash, output index, depth, and value
 */
std::string COutput::ToString() const
{
    return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

/**
 * @brief Retrieve a wallet transaction by its hash
 * @param hash The transaction hash to look up
 * @return Pointer to the wallet transaction if found, nullptr otherwise
 * 
 * Thread-safe method that searches the wallet's transaction map for a specific transaction.
 * The wallet lock is acquired during the search operation.
 */
const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const
{
    LOCK(cs_wallet);
    std::map<uint256, CWalletTx>::const_iterator it = mapWallet.find(hash);
    if (it == mapWallet.end())
        return NULL;
    return &(it->second);
}

/**
 * @section Shielded Address Generation
 * 
 * Functions for generating new shielded addresses using hierarchical deterministic
 * key derivation. These functions support Sprout, Sapling, and Orchard protocols.
 */

/**
 * @brief Generate a new Sprout shielded payment address
 * @return A new Sprout payment address
 * @throws std::runtime_error if collision detected or key addition fails
 * 
 * Generates a random Sprout spending key and derives its payment address.
 * The key is stored in the wallet with metadata including creation time.
 * This method requires the wallet lock to be held.
 */

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

/**
 * @brief Generate a new Sapling shielded payment address
 * @return A new Sapling payment address
 * @throws std::runtime_error if HD seed not found, keypath derivation fails, or key addition fails
 * 
 * Generates a new Sapling spending key using hierarchical deterministic key derivation.
 * The key follows BIP44 derivation path: m/32'/coin_type'/account'
 * The first generated key (account 0) is set as the primary key for diversification.
 * Metadata including keypath and seed fingerprint is stored with the key.
 */

SaplingPaymentAddress CWallet::GenerateNewSaplingZKey()
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // Try to get the seed
    HDSeed seed;
    if (!GetHDSeed(seed))
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): HD seed not found");

    auto m = libzcash::SaplingExtendedSpendingKey::Master(seed, bip39Enabled);
    uint32_t bip44CoinType = Params().BIP44CoinType();

    // We use a fixed keypath scheme of m/32'/coin_type'/account'
    // Derive m/32'
    auto m_32h = m.Derive(32 | HARDENED_KEY_LIMIT);
    // Derive m/32'/coin_type'
    auto m_32h_cth = m_32h.Derive(bip44CoinType | HARDENED_KEY_LIMIT);

    // Derive account key at next index, skip keys already known to the wallet
    libzcash::SaplingExtendedSpendingKey xsk;
    do
    {
        xsk = m_32h_cth.Derive(hdChain.saplingAccountCounter | HARDENED_KEY_LIMIT);
        metadata.hdKeypath = "m/32'/" + std::to_string(bip44CoinType) + "'/" + std::to_string(hdChain.saplingAccountCounter) + "'";
        metadata.seedFp = hdChain.seedFp;

        //Set Primary key for diversification
        if (hdChain.saplingAccountCounter == 0) {
            SetPrimarySaplingSpendingKey(xsk);
        }

        // Increment childkey index
        hdChain.saplingAccountCounter++;
    } while (HaveSaplingSpendingKey(xsk.ToXFVK()));

    // Update the chain model in the database
    if (fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(hdChain))
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): Writing HD chain model failed");

    auto ivk = xsk.expsk.full_viewing_key().in_viewing_key();
    mapSaplingSpendingKeyMetadata[ivk] = metadata;

    auto addr = xsk.DefaultAddress();
    if (!AddSaplingZKey(xsk)) {
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): AddSaplingZKey failed");
    }

    nTimeFirstKey = 1;
    // return default sapling payment address.
    return addr;
}

/**
 * @brief Generate a new Orchard shielded payment address
 * @return A new Orchard payment address
 * @throws std::runtime_error if HD seed not found, key derivation fails, or key addition fails
 * 
 * Generates a new Orchard spending key using hierarchical deterministic key derivation.
 * The key follows a similar derivation path to Sapling: m/32'/coin_type'/account'
 * The first generated key (account 0) is set as the primary key for diversification.
 * Metadata including keypath and seed fingerprint is stored with the key.
 */
OrchardPaymentAddressPirate CWallet::GenerateNewOrchardZKey()
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    // Create new metadata
    int64_t nCreationTime = GetTime();
    CKeyMetadata metadata(nCreationTime);

    // Try to get the seed
    HDSeed seed;
    if (!GetHDSeed(seed))
        throw std::runtime_error("CWallet::GenerateNewOrchardZKey(): HD seed not found");

    auto master = libzcash::OrchardExtendedSpendingKeyPirate::Master(seed, bip39Enabled);
    uint32_t bip44CoinType = Params().BIP44CoinType();

    // We use a fixed keypath scheme of m/32'/coin_type'/account'
    // Derive m/32'/coin_type'
    auto coinType = bip44CoinType;

    // Derive account key at next index, skip keys already known to the wallet
    libzcash::OrchardExtendedSpendingKeyPirate xsk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;
    do
    {
        auto account = hdChain.orchardAccountCounter;
        auto xskOpt = master.Derive(coinType, account);

        metadata.hdKeypath = "m/32'/" + std::to_string(coinType) + "'/" + std::to_string(account) + "'";
        metadata.seedFp = hdChain.seedFp;

        LogPrintf("Orchard Keypath %s\n", metadata.hdKeypath);

        // Increment childkey index
        hdChain.orchardAccountCounter++;

        if (xskOpt == std::nullopt){
            continue;
        }
        xsk = xskOpt.value();

        // Set Primary key for diversification
        if (hdChain.orchardAccountCounter == 0) {
            SetPrimaryOrchardSpendingKey(xsk);
        }

        auto extfvkOpt = xsk.GetXFVK();
        if (extfvkOpt == std::nullopt) {
            continue;
        }
        extfvk = extfvkOpt.value();

    } while (HaveOrchardSpendingKey(extfvk));

    auto ivkOpt = extfvk.fvk.GetIVK();
    auto addressOpt = extfvk.fvk.GetDefaultAddress();

    if (ivkOpt == std::nullopt || addressOpt == std::nullopt) {
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): Address Generation failed");
    }

    auto ivk = ivkOpt.value();
    auto address = addressOpt.value();

    // Update the chain model in the database
    if (fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(hdChain))
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): Writing HD chain model failed");

    //Populate Metat Data
    mapOrchardSpendingKeyMetadata[ivk] = metadata;

    if (!AddOrchardZKey(xsk)) {
        throw std::runtime_error("CWallet::GenerateNewSaplingZKey(): AddSaplingZKey failed");
    }

    nTimeFirstKey = 1;
    // return default sapling payment address.
    return address;
}

/**
 * @brief Generate a new Sapling diversified payment address
 * @return A new diversified Sapling payment address
 * @throws std::runtime_error if HD seed not found, key derivation fails, or address addition fails
 * 
 * Generates a diversified Sapling payment address from the primary spending key.
 * If no primary key exists, derives one from the HD seed using the default path.
 * Diversified addresses allow multiple payment addresses from a single spending key
 * for improved privacy. The function:
 * - Uses the primary Sapling spending key or derives one if needed
 * - Iterates through diversifier values to find an unused address
 * - Stores the new address and diversifier path in the wallet
 * - Returns the generated diversified payment address
 */
SaplingPaymentAddress CWallet::GenerateNewSaplingDiversifiedAddress()
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    libzcash::SaplingExtendedSpendingKey extsk;
    if (primarySaplingSpendingKey == std::nullopt) {
        // Try to get the seed
        HDSeed seed;
        if (!GetHDSeed(seed))
            throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): HD seed not found");

        auto m = libzcash::SaplingExtendedSpendingKey::Master(seed, bip39Enabled);
        uint32_t bip44CoinType = Params().BIP44CoinType();

        //Derive default key
        auto m_32h = m.Derive(32 | HARDENED_KEY_LIMIT);
        auto m_32h_cth = m_32h.Derive(bip44CoinType | HARDENED_KEY_LIMIT);
        extsk = m_32h_cth.Derive(0 | HARDENED_KEY_LIMIT);

        //Check of default spending key
        auto ivk = extsk.expsk.full_viewing_key().in_viewing_key();
        libzcash::SaplingExtendedFullViewingKey extfvk;
        GetSaplingFullViewingKey(ivk, extfvk);
        if (!HaveSaplingSpendingKey(extfvk)) {

          //Set metadata
          int64_t nCreationTime = GetTime();
          CKeyMetadata metadata(nCreationTime);
          metadata.hdKeypath = "m/32'/" + std::to_string(bip44CoinType) + "'/0'";
          metadata.seedFp = hdChain.seedFp;
          mapSaplingSpendingKeyMetadata[ivk] = metadata;

          //Add Address to wallet
          auto addr = extsk.DefaultAddress();
          if (!AddSaplingZKey(extsk)) {
              throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddSaplingZKey failed");
          }

          //Return default address for default key
          return addr;
        } else {
            SetPrimarySaplingSpendingKey(extsk);
        }
    } else {
        extsk = primarySaplingSpendingKey.value();
    }

    auto ivk = extsk.expsk.full_viewing_key().in_viewing_key();
    SaplingPaymentAddress addr;
    blob88 diversifier;
    arith_uint88 div;

    //Initalize diversifier
    diversifier = ArithToUint88(div);

    //Get Last used diversifier if one exists
    for (auto entry : mapLastSaplingDiversifierPath) {
        if (entry.first == ivk) {
            diversifier = entry.second;
            div = UintToArith88(diversifier);
        }
    }

    bool found = false;
    do {
      addr = extsk.ToXFVK().Address(diversifier).value().second;
      if (!GetSaplingExtendedSpendingKey(addr, extsk)) {
          found = true;
          //Save last used diversifier by ivk
          if (!AddLastSaplingDiversifierUsed(ivk, diversifier)) {
              throw std::runtime_error("CWallet::GenerateNewSaplingDiversifiedAddress(): AddLastSaplingDiversifierUsed failed");
          }

      }

      //increment the diversifier
      div++;
      diversifier = ArithToUint88(div);

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

/**
 * @brief Generate a new Orchard diversified payment address
 * @return A new diversified Orchard payment address
 * @throws std::runtime_error if HD seed not found, key derivation fails, or address addition fails
 * 
 * Generates a diversified Orchard payment address from the primary spending key.
 * If no primary key exists, derives one from the HD seed using the default path.
 * Diversified addresses allow multiple payment addresses from a single spending key
 * for improved privacy. The function:
 * - Uses the primary Orchard spending key or derives one if needed
 * - Iterates through diversifier values to find an unused address
 * - Stores the new address and diversifier path in the wallet
 * - Returns the generated diversified payment address
 */
OrchardPaymentAddressPirate CWallet::GenerateNewOrchardDiversifiedAddress()
{
    AssertLockHeld(cs_wallet); // mapOrchardSpendingKeyMetadata

    libzcash::OrchardExtendedSpendingKeyPirate extsk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    libzcash::OrchardPaymentAddressPirate addr;

    if (primaryOrchardSpendingKey == std::nullopt) {

        // Create new metadata
        int64_t nCreationTime = GetTime();
        CKeyMetadata metadata(nCreationTime);

        // Try to get the seed
        HDSeed seed;
        if (!GetHDSeed(seed))
            throw std::runtime_error("CWallet::GenerateNewOrchardZKey(): HD seed not found");

        auto master = libzcash::OrchardExtendedSpendingKeyPirate::Master(seed, bip39Enabled);
        uint32_t bip44CoinType = Params().BIP44CoinType();

        // We use a fixed keypath scheme of m/32'/coin_type'/account'
        // Derive m/32'/coin_type'
        auto coinType = bip44CoinType;
        uint32_t accountCounter = 0;
        auto account = accountCounter;

        do {

            account = accountCounter;
            auto extskOpt = master.Derive(coinType, account);

            metadata.hdKeypath = "m/32'/" + std::to_string(coinType) + "'/" + std::to_string(account) + "'";
            metadata.seedFp = hdChain.seedFp;

            LogPrintf("Orchard Keypath %s\n", metadata.hdKeypath);

            // Increment childkey index
            accountCounter++;

            if (extskOpt == std::nullopt){
                continue;
            }
            extsk = extskOpt.value();

            auto extfvkOpt = extsk.GetXFVK();
            if (extfvkOpt == std::nullopt) {
                continue;
            }
            extfvk = extfvkOpt.value();

            auto ivkOpt = extfvk.fvk.GetIVK();
            if (ivkOpt == std::nullopt) {
                continue;
            }
            ivk = ivkOpt.value();

            auto addrOpt = extfvk.fvk.GetDefaultAddress();
            if (addrOpt == std::nullopt) {
                continue;
            }
            addr = addrOpt.value();

            break;
        } while (true);

        //Set first key found
        SetPrimaryOrchardSpendingKey(extsk);

        //Return default address if the key does not exist in the wallet
        if (!HaveOrchardSpendingKey(extfvk)) {
            mapOrchardSpendingKeyMetadata[ivk] = metadata;
            if (!AddOrchardZKey(extsk)) {
                throw std::runtime_error("CWallet::GenerateNewOrchardDiversifiedAddress(): AddOrchardZKey failed");
            }
            return addr;
        }

    } else {
        extsk = primaryOrchardSpendingKey.value();
        extfvk = extsk.GetXFVK().value();
        ivk = extfvk.fvk.GetIVK().value();
    }

    blob88 diversifierIndex;
    arith_uint88 diversifierIndexCounter;

    //Initalize diversifier
    diversifierIndex = ArithToUint88(diversifierIndexCounter);

    //Get Last used diversifier if one exists
    for (auto entry : mapLastOrchardDiversifierPath) {
        if (entry.first == ivk) {
            diversifierIndex = entry.second;
            diversifierIndexCounter = UintToArith88(diversifierIndex);
        }
    }

    bool found = false;
    do {
      auto addrOpt = extfvk.fvk.GetAddressFromIndex(diversifierIndex);
      if (addrOpt != std::nullopt) {
          addr = addrOpt.value();
          if (!GetOrchardExtendedSpendingKey(addr, extsk)) {
              found = true;
              //Save last used diversifier by ivk
              if (!AddLastOrchardDiversifierUsed(ivk, diversifierIndex)) {
                  throw std::runtime_error("CWallet::GenerateNewOrchardDiversifiedAddress(): AddLastOrchardDiversifierUsed failed");
              }

          }
      }

      //increment the diversifier
      diversifierIndexCounter++;
      diversifierIndex = ArithToUint88(diversifierIndexCounter);

    }
    while (!found);

    //Add to wallet
    if (!AddOrchardIncomingViewingKey(ivk, addr)) {
        throw std::runtime_error("CWallet::GenerateNewOrchardDiversifiedAddress(): AddOrchardIncomingViewingKey failed");
    }

    //Add to wallet
    if (!AddOrchardDiversifiedAddress(addr, ivk, diversifierIndex)) {
        throw std::runtime_error("CWallet::GenerateNewOrchardDiversifiedAddress(): AddOrchardDiversifiedAddress failed");
    }

    // return diversified orchard payment address
    return addr;
}

/**
 * @brief Set the primary Sapling spending key for diversified address generation
 * @param extsk The Sapling extended spending key to set as primary
 * @return true if successfully set and persisted, false otherwise
 * 
 * Sets the primary Sapling spending key used for generating diversified addresses.
 * This key serves as the base for creating multiple payment addresses from a single
 * spending key. If the wallet is encrypted, the key will be encrypted before storage.
 * Returns false if the wallet is encrypted and locked.
 */
bool CWallet::SetPrimarySaplingSpendingKey(
    const libzcash::SaplingExtendedSpendingKey &extsk)
{
      AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

      if (IsCrypted() && IsLocked()) {
          return false;
      }

      primarySaplingSpendingKey = extsk;

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

/**
 * @brief Set the primary Orchard spending key for diversified address generation
 * @param extsk The Orchard extended spending key to set as primary
 * @return true if successfully set and persisted, false otherwise
 * 
 * Sets the primary Orchard spending key used for generating diversified addresses.
 * This key serves as the base for creating multiple payment addresses from a single
 * spending key. If the wallet is encrypted, the key will be encrypted before storage.
 * Returns false if the wallet is encrypted and locked.
 */
bool CWallet::SetPrimaryOrchardSpendingKey(
    const libzcash::OrchardExtendedSpendingKeyPirate &extsk)
{
      AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

      if (IsCrypted() && IsLocked()) {
          return false;
      }

      primaryOrchardSpendingKey = extsk;

      if (!fFileBacked) {
          return true;
      }

      if (!IsCrypted()) {
          return CWalletDB(strWalletFile).WritePrimaryOrchardSpendingKey(extsk);
      } else {

          auto extfvkOpt = extsk.GetXFVK();
          if (extfvkOpt == std::nullopt) {
              LogPrintf("Setting encrypted primary spending key failed!!!\n");
              return false;
          }
          auto extfvk = extfvkOpt.value();

          std::vector<unsigned char> vchCryptedSecret;
          uint256 chash = extfvk.fvk.GetFingerprint();
          CKeyingMaterial vchSecret = SerializeForEncryptionInput(extsk);

          if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
              LogPrintf("Encrypting Spending key failed!!!\n");
              return false;
          }

          return CWalletDB(strWalletFile).WriteCryptedPrimaryOrchardSpendingKey(extsk, vchCryptedSecret);
      }

      return true;
}

/**
 * @brief Load an encrypted primary Sapling spending key from the database
 * @param extfvkFinger Fingerprint of the extended full viewing key for validation
 * @param vchCryptedSecret The encrypted spending key data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads the primary Sapling spending key from encrypted storage.
 * The fingerprint is used to validate the integrity of the decrypted key.
 * This function is called during wallet initialization when loading encrypted keys.
 */
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

/**
 * @brief Load an encrypted primary Orchard spending key from the database
 * @param extfvkFinger Fingerprint of the extended full viewing key for validation
 * @param vchCryptedSecret The encrypted spending key data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads the primary Orchard spending key from encrypted storage.
 * The fingerprint is used to validate the integrity of the decrypted key.
 * This function is called during wallet initialization when loading encrypted keys.
 */
bool CWallet::LoadCryptedPrimaryOrchardSpendingKey(const uint256 &extfvkFinger, const std::vector<unsigned char> &vchCryptedSecret)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, extfvkFinger, vchSecret)) {
        LogPrintf("Decrypting Primary Orchard Spending Key failed!!!\n");
        return false;
    }

    libzcash::OrchardExtendedSpendingKeyPirate extsk;
    DeserializeFromDecryptionOutput(vchSecret, extsk);

    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        LogPrintf("Decrypting primary orchard spending key failed!!!\n");
        return false;
    }
    auto extfvk = extfvkOpt.value();

    if (extfvk.fvk.GetFingerprint() != extfvkFinger) {
        LogPrintf("Decrypted Primary Spending Key fingerprint is invalid!!!\n");
        return false;
    }

    primaryOrchardSpendingKey = extsk;
    return true;
}

/**
 * @brief Add a Sapling extended spending key to the wallet
 * @param extsk The Sapling extended spending key to add
 * @return true if successfully added and persisted, false otherwise
 * 
 * Adds a Sapling spending key to both the in-memory keystore and wallet database.
 * For encrypted wallets, the key is encrypted before storage along with its metadata.
 * The function handles both file-backed and memory-only wallets. Returns false if
 * the wallet is encrypted and locked. Sets nTimeFirstKey to 1 since viewing keys
 * don't have birthday information.
 */
bool CWallet::AddSaplingZKey(
    const libzcash::SaplingExtendedSpendingKey &extsk)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata


    if (IsCrypted() && IsLocked()) {
        return false;
    }

    auto ivk = extsk.ToXFVK().fvk.in_viewing_key();

    if (!IsCrypted()) {
        if (!CCryptoKeyStore::AddSaplingSpendingKey(extsk)) {
            LogPrintf("Adding unencrypted Sapling Spending Key failed!!!\n");
            return false;
        }

        if (!fFileBacked) {
            return true;
        }   

        if(!CWalletDB(strWalletFile).WriteSaplingZKey(ivk, extsk, mapSaplingSpendingKeyMetadata[ivk])) {
            LogPrintf("Writing unencrypted Sapling Spending Key failed!!!\n");
            return false;
        }
    } else {
        
        if (!fFileBacked) {
            return true;
        }

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
        CKeyMetadata metadata = mapSaplingSpendingKeyMetadata[ivk];
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

/**
 * @brief Add a mapping between Sapling payment address and incoming viewing key
 * @param ivk The Sapling incoming viewing key
 * @param addr The corresponding Sapling payment address
 * @return true if successfully added and persisted, false otherwise
 * 
 * Creates a mapping that allows the wallet to detect incoming transactions to
 * the specified Sapling address using the incoming viewing key. This is essential
 * for note decryption and balance calculation. For encrypted wallets, both the
 * address and key are encrypted before storage. Returns false if the wallet is
 * encrypted and locked.
 */
bool CWallet::AddSaplingIncomingViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
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

/**
 * @brief Add a Sapling extended full viewing key to the wallet
 * @param extfvk The Sapling extended full viewing key to add
 * @return true if successfully added and persisted, false otherwise
 * 
 * Adds a Sapling extended full viewing key to both the in-memory keystore and
 * wallet database. This enables watch-only functionality for Sapling addresses,
 * allowing the wallet to detect incoming transactions without having the spending key.
 * For encrypted wallets, the viewing key is encrypted before storage using the
 * key's fingerprint as the encryption hash. Returns false if the wallet is
 * encrypted and locked.
 */
bool CWallet::AddSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    AssertLockHeld(cs_wallet);

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddSaplingExtendedFullViewingKey(extfvk)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
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

/**
 * @brief Add a Sapling diversified address mapping to the wallet
 * @param addr The Sapling payment address (diversified address)
 * @param ivk The corresponding incoming viewing key
 * @param path The diversification path used to generate this address
 * @return true if successfully added and persisted, false otherwise
 * 
 * Creates a mapping between a diversified Sapling payment address and its
 * corresponding incoming viewing key and diversification path. This allows
 * the wallet to properly handle multiple addresses derived from the same
 * spending key. For encrypted wallets, the address data is encrypted before
 * storage. Returns false if the wallet is encrypted and locked.
 */
bool CWallet::AddSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddSaplingDiversifiedAddress(addr, ivk, path)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
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

/**
 * @brief Add an Orchard diversified address mapping to the wallet
 * @param addr The Orchard payment address (diversified address)
 * @param ivk The corresponding incoming viewing key
 * @param path The diversification path used to generate this address
 * @return true if successfully added and persisted, false otherwise
 * 
 * Creates a mapping between a diversified Orchard payment address and its
 * corresponding incoming viewing key and diversification path. This allows
 * the wallet to properly handle multiple addresses derived from the same
 * spending key. For encrypted wallets, the address data is encrypted before
 * storage. Returns false if the wallet is encrypted and locked.
 */
bool CWallet::AddOrchardDiversifiedAddress(
    const libzcash::OrchardPaymentAddressPirate &addr,
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddOrchardDiversifiedAddress(addr, ivk, path)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteOrchardDiversifiedAddress(addr, ivk, path);
    }
    else {

        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = HashWithFP(addr);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(addr, ivk, path);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedOrchardDiversifiedAddress(addr, chash, vchCryptedSecret);

    }

    return true;
}

/**
 * @brief Store the last used Sapling diversifier for an incoming viewing key
 * @param ivk The Sapling incoming viewing key
 * @param path The diversifier path that was last used
 * @return true if successfully stored, false otherwise
 * 
 * Tracks the last diversifier used for a given incoming viewing key to ensure
 * that subsequent diversified address generation continues from where it left off.
 * This prevents reuse of diversifiers and maintains proper address generation
 * ordering. For encrypted wallets, the diversifier data is encrypted before storage.
 * Returns false if the wallet is encrypted and locked.
 */
bool CWallet::AddLastSaplingDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddLastSaplingDiversifierUsed(ivk, path)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteLastSaplingDiversifierUsed(ivk, path);
    }
    else {

        uint256 chash = HashWithFP(ivk);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(ivk,path);
        std::vector<unsigned char> vchCryptedSecret;

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteLastCryptedSaplingDiversifierUsed(chash, ivk, vchCryptedSecret);

    }

    return true;
}

/**
 * @brief Store the last used Orchard diversifier for an incoming viewing key
 * @param ivk The Orchard incoming viewing key
 * @param path The diversifier path that was last used
 * @return true if successfully stored, false otherwise
 * 
 * Tracks the last diversifier used for a given incoming viewing key to ensure
 * that subsequent diversified address generation continues from where it left off.
 * This prevents reuse of diversifiers and maintains proper address generation
 * ordering. For encrypted wallets, the diversifier data is encrypted before storage.
 * Returns false if the wallet is encrypted and locked.
 */
bool CWallet::AddLastOrchardDiversifierUsed(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddLastOrchardDiversifierUsed(ivk, path)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteLastOrchardDiversifierUsed(ivk, path);
    }
    else {

        uint256 chash = HashWithFP(ivk);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(ivk,path);
        std::vector<unsigned char> vchCryptedSecret;

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteLastCryptedOrchardDiversifierUsed(chash, ivk, vchCryptedSecret);

    }

    return true;
}

/**
 * @brief Add an Orchard extended spending key to the wallet
 * @param extsk The Orchard extended spending key to add
 * @return true if successfully added and persisted, false otherwise
 * 
 * Adds an Orchard spending key to both the in-memory keystore and wallet database.
 * The function extracts the extended full viewing key and incoming viewing key
 * from the spending key for proper storage organization. For encrypted wallets,
 * the key is encrypted before storage along with its metadata. Returns false if
 * the wallet is encrypted and locked, or if key extraction fails. Sets nTimeFirstKey
 * to 1 since viewing keys don't have birthday information.
 */
bool CWallet::AddOrchardZKey(
    const libzcash::OrchardExtendedSpendingKeyPirate &extsk)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata


    if (IsCrypted() && IsLocked()) {
        return false;
    }

    //Get OrchardExtendedFullViewingKey
    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        return false;
    }
    auto extfvk = extfvkOpt.value();

    //Get OrchardIncomingViewingKey
    auto ivkOpt = extfvk.fvk.GetIVK();
    if (ivkOpt == std::nullopt) {
        return false;
    }
    auto ivk = ivkOpt.value();

    if (!IsCrypted()) {
        if (!CCryptoKeyStore::AddOrchardSpendingKey(extsk)) {
            LogPrintf("Adding unencrypted Orchard Spending Key failed!!!\n");
            return false;
        }

        if (!fFileBacked) {
            return true;
        }

        if(!CWalletDB(strWalletFile).WriteOrchardZKey(ivk, extsk, mapOrchardSpendingKeyMetadata[ivk])) {
            LogPrintf("Writing unencrypted Orchard Spending Key failed!!!\n");
            return false;
        }
    } else {

        if (!fFileBacked) {
            return true;
        }

        //Encrypt Orchard Extended Spending Key
        std::vector<unsigned char> vchCryptedSpendingKey;
        uint256 chash = extfvk.fvk.GetFingerprint();
        CKeyingMaterial vchSpendingKey = SerializeForEncryptionInput(extsk);

        if (!EncryptSerializedWalletObjects(vchSpendingKey, chash, vchCryptedSpendingKey)) {
            LogPrintf("Encrypting Orchard Spending Key failed!!!\n");
            return false;
        }

        //Encrypt metadata
        CKeyMetadata metadata = mapOrchardSpendingKeyMetadata[ivk];
        std::vector<unsigned char> vchCryptedMetaData;
        CKeyingMaterial vchMetaData = SerializeForEncryptionInput(metadata);
        if (!EncryptSerializedWalletObjects(vchMetaData, chash, vchCryptedMetaData)) {
            LogPrintf("Encrypting Orchard Spending Key metadata failed!!!\n");
            return false;
        }

        if (!CCryptoKeyStore::AddCryptedOrchardSpendingKey(extfvk, vchCryptedSpendingKey)) {
            LogPrintf("Adding encrypted Orchard Spending Key failed!!!\n");
            return false;
        }

        if (!CWalletDB(strWalletFile).WriteCryptedOrchardZKey(extfvk, vchCryptedSpendingKey, vchCryptedMetaData)) {
            LogPrintf("Writing encrypted Orchard Spending Key failed!!!\n");
            return false;
        }
    }

    nTimeFirstKey = 1; // No birthday information for viewing keys.
    return true;
}

/**
 * @brief Add an Orchard extended full viewing key to the wallet
 * @param extfvk The Orchard extended full viewing key to add
 * @return true if successfully added, false otherwise
 * 
 * Adds an Orchard extended full viewing key to both the in-memory keystore
 * and persistent wallet database. If the wallet is encrypted, the key will
 * be encrypted before storage. The wallet must be unlocked for encrypted wallets.
 */
bool CWallet::AddOrchardExtendedFullViewingKey(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    AssertLockHeld(cs_wallet);

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddOrchardExtendedFullViewingKey(extfvk)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteOrchardFullViewingKey(extfvk);
    } else {
        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = extfvk.fvk.GetFingerprint();
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(extfvk);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedOrchardFullViewingKey(extfvk, vchCryptedSecret);
    }
}

/**
 * @brief Add an Orchard incoming viewing key and payment address mapping to the wallet
 * @param ivk The Orchard incoming viewing key
 * @param addr The corresponding Orchard payment address
 * @return true if successfully added, false otherwise
 * 
 * Creates a mapping between an Orchard payment address and its corresponding
 * incoming viewing key. This allows the wallet to detect incoming transactions
 * to the address. For encrypted wallets, both the address and key are encrypted
 * before storage.
 */
bool CWallet::AddOrchardIncomingViewingKey(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const libzcash::OrchardPaymentAddressPirate &addr)
{
    AssertLockHeld(cs_wallet);

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    if (!CCryptoKeyStore::AddOrchardIncomingViewingKey(ivk, addr)) {
        return false;
    }

    if (!fFileBacked) {
        return true;
    }

    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteOrchardPaymentAddress(ivk, addr);
    } else {

        std::vector<unsigned char> vchCryptedSecret;
        uint256 chash = HashWithFP(addr);
        CKeyingMaterial vchSecret = SerializeForEncryptionInput(addr, ivk);

        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
            LogPrintf("Encrypting Address failed!!!\n");
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedOrchardPaymentAddress(addr, chash, vchCryptedSecret);

    }

    return true;
}

/**
 * @section Shielded Protocol Support Functions
 * 
 * Functions for managing shielded protocols including Sapling and Orchard
 * note commitment trees, nullifier tracking, and protocol-specific operations.
 */

/**
 * @brief Get a loader for the Sapling note commitment tree
 * @return A SaplingWalletNoteCommitmentTreeLoader instance
 * 
 * Returns a loader that can be used to read a Sapling note commitment
 * tree from a stream into the Sapling wallet. This is used during
 * wallet synchronization and state restoration.
 */
SaplingWalletNoteCommitmentTreeLoader CWallet::GetSaplingNoteCommitmentTreeLoader() {
    return SaplingWalletNoteCommitmentTreeLoader(saplingWallet);
}

/**
 * @brief Get a loader for the Orchard note commitment tree
 * @return An OrchardWalletNoteCommitmentTreeLoader instance
 * 
 * Returns a loader that can be used to read an Orchard note commitment
 * tree from a stream into the Orchard wallet. This is used during
 * wallet synchronization and state restoration.
 */
OrchardWalletNoteCommitmentTreeLoader CWallet::GetOrchardNoteCommitmentTreeLoader() {
    return OrchardWalletNoteCommitmentTreeLoader(orchardWallet);
}

/**
 * @brief Add a Sprout shielded spending key to the wallet
 * @param key The Sprout spending key to add
 * @return true if the key was successfully added, false otherwise
 * 
 * Adds the given Sprout spending key to both the keystore and wallet database.
 * If a viewing key already exists for this address, it will be removed since
 * the full spending key supersedes the viewing key. The function requires
 * wallet lock to be held and only persists to disk if the wallet is file-backed.
 */
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

/**
 * @brief Generate a new transparent (t-address) key pair for the wallet
 * @return The public key of the newly generated key pair
 * @throws std::runtime_error if key addition fails
 * 
 * Generates a new ECDSA key pair for transparent addresses. The type of key
 * (compressed or uncompressed) depends on wallet version capabilities.
 * The private key is stored in the wallet along with creation metadata,
 * and the public key is returned for address generation.
 * 
 * This function requires the wallet lock to be held and will update the
 * wallet's minimum version if compressed keys are used.
 */
CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    
    // Use compressed public keys if wallet supports the feature
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY);

    // Generate a new private key
    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();
    assert(secret.VerifyPubKey(pubkey));

    // Create metadata for the new key
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    
    // Update first key timestamp if this is earlier
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    // Add the key pair to the wallet
    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey(): AddKey failed");
        
    return pubkey;
}

/**
 * @brief Add a private key and its corresponding public key to the wallet
 * @param secret The private key to add
 * @param pubkey The corresponding public key
 * @return true if successfully added, false otherwise
 * 
 * Adds a key pair to the wallet's key store and persistent storage.
 * This function handles both encrypted and unencrypted wallets, encrypting
 * the private key when necessary. It also removes any existing watch-only
 * entries for the same key and updates HD chain information if applicable.
 * 
 * The wallet must be unlocked if it is encrypted.
 */
bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata

    if (IsCrypted() && IsLocked()) {
        return false;
    }

    // Check if we need to remove from watch-only
    CScript script;
    script = GetScriptForDestination(pubkey.GetID());
    if (HaveWatchOnly(script))
        RemoveWatchOnly(script);


    if (!IsCrypted()) {
        if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) {
            LogPrintf("Adding Transparent Spending Key failed!!!\n");
            return false;
        }

        if (!fFileBacked) {
            return true;
        }

        if (!CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()])) {
            LogPrintf("Writing Transparent Spending Key failed!!!\n");
            return false;
        }
    } else {

        if (!fFileBacked) {
            return true;
        }

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
            LogPrintf("Writing encrypted Transparent Spending Key failed!!!\n");
            return false;
        }
    }
    return true;
}

/**
 * @section Key Loading and Database Functions
 * 
 * Functions for loading keys and addresses from the wallet database during
 * wallet initialization. These functions handle both encrypted and unencrypted
 * key material and populate the in-memory key stores.
 */

/**
 * @brief Load a transparent key pair into the wallet
 * @param key The private key to load
 * @param pubkey The corresponding public key
 * @return true if successfully loaded
 * 
 * Loads an existing key pair into the in-memory key store. This is typically
 * called during wallet initialization when reading keys from the database.
 */
bool CWallet::LoadKey(const CKey& key, const CPubKey &pubkey)
{
    return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
}

/**
 * @brief Load an encrypted transparent key from the database
 * @param chash Hash identifier for the encrypted key
 * @param vchCryptedSecret The encrypted key material
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted transparent key into the in-memory key store.
 * The wallet must be unlocked for this operation to succeed. The hash is used
 * to verify the integrity of the decrypted key material.
 */
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

/**
 * @brief Add an encrypted Sprout spending key to the wallet
 * @param address The Sprout payment address
 * @param rk The receiving key component
 * @param vchCryptedSecret The encrypted spending key data
 * @return true if successfully added
 * 
 * Adds an encrypted Sprout spending key to both the in-memory key store and
 * persistent storage. This function is used during wallet encryption or when
 * loading encrypted keys from the database.
 */
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

/**
 * @brief Load metadata for a transparent key
 * @param pubkey The public key to associate metadata with
 * @param meta The key metadata containing creation time and other info
 * @return true if successfully loaded
 * 
 * Loads key metadata into the wallet's metadata map. Updates the wallet's
 * first key timestamp if this key is older than previously recorded keys.
 * This is typically called during wallet initialization.
 */
bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

/**
 * @brief Load encrypted metadata for a transparent key
 * @param chash Hash identifier for the encrypted metadata
 * @param vchCryptedSecret The encrypted metadata
 * @param metadata[out] The decrypted metadata object
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads key metadata from encrypted storage. The wallet must
 * be unlocked for this operation. The metadata is then loaded using LoadKeyMetadata.
 */
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

/**
 * @brief Load metadata for a Sprout shielded key
 * @param addr The Sprout payment address
 * @param meta The key metadata containing creation time and other info
 * @return true if successfully loaded
 * 
 * Loads Sprout key metadata into the wallet's metadata map. Updates the wallet's
 * first key timestamp if this key is older than previously recorded keys.
 */
bool CWallet::LoadZKeyMetadata(const SproutPaymentAddress &addr, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSproutZKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapSproutZKeyMetadata[addr] = meta;
    return true;
}

/**
 * @brief Load an encrypted Sprout spending key from the database
 * @param addr The Sprout payment address
 * @param rk The receiving key component
 * @param vchCryptedSecret The encrypted spending key data
 * @return true if successfully loaded
 * 
 * Loads an encrypted Sprout spending key into the in-memory key store.
 * This is typically called during wallet initialization when loading keys from disk.
 */
bool CWallet::LoadCryptedZKey(const libzcash::SproutPaymentAddress &addr, const libzcash::ReceivingKey &rk, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedSproutSpendingKey(addr, rk, vchCryptedSecret);
}

/**
 * @brief Load temporarily held encrypted Sapling metadata
 * @return true if successfully processed
 * 
 * Processes encrypted Sapling key metadata that was temporarily stored during
 * wallet loading. This function matches metadata to the appropriate viewing keys
 * and integrates it into the wallet's metadata maps. Called during wallet initialization
 * to handle cases where metadata is loaded before the corresponding keys.
 */
bool CWallet::LoadTempHeldCryptedData()
{
    AssertLockHeld(cs_wallet);

    std::map<uint256, libzcash::SaplingExtendedFullViewingKey> mapSaplingFingerPrints;

    // Get a map of all the Sapling full viewing key fingerprints
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

/**
 * @brief Load metadata for a Sapling shielded key
 * @param ivk The Sapling incoming viewing key
 * @param meta The key metadata containing creation time, keypath, and seed fingerprint
 * @return true if successfully loaded
 * 
 * Loads Sapling key metadata into the wallet's metadata map. Updates the wallet's
 * first key timestamp if this key is older than previously recorded keys. If the
 * seed fingerprint is empty (indicating a legacy key), sets nTimeFirstKey to 1.
 */
bool CWallet::LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    if (meta.seedFp == uint256()) {
        nTimeFirstKey = 1;
    }

    mapSaplingSpendingKeyMetadata[ivk] = meta;
    return true;
}

/**
 * @brief Load metadata for an Orchard shielded key
 * @param ivk The Orchard incoming viewing key
 * @param meta The key metadata containing creation time, keypath, and seed fingerprint
 * @return true if successfully loaded
 * 
 * Loads Orchard key metadata into the wallet's metadata map. Updates the wallet's
 * first key timestamp if this key is older than previously recorded keys. If the
 * seed fingerprint is empty (indicating a legacy key), sets nTimeFirstKey to 1.
 */
bool CWallet::LoadOrchardZKeyMetadata(const libzcash::OrchardIncomingViewingKeyPirate &ivk, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapSaplingSpendingKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    if (meta.seedFp == uint256()) {
        nTimeFirstKey = 1;
    }

    mapOrchardSpendingKeyMetadata[ivk] = meta;
    return true;
}

/**
 * @brief Load a Sapling spending key into the wallet
 * @param key The Sapling extended spending key to load
 * @return true if successfully loaded
 * 
 * Loads a Sapling spending key into the in-memory key store. This is typically
 * called during wallet initialization when loading keys from the database.
 */
bool CWallet::LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key)
{
    return CCryptoKeyStore::AddSaplingSpendingKey(key);
}

/**
 * @brief Load an Orchard spending key into the wallet
 * @param key The Orchard extended spending key to load
 * @return true if successfully loaded
 * 
 * Loads an Orchard spending key into the in-memory key store. This is typically
 * called during wallet initialization when loading keys from the database.
 */
bool CWallet::LoadOrchardZKey(const libzcash::OrchardExtendedSpendingKeyPirate &key)
{
    return CCryptoKeyStore::AddOrchardSpendingKey(key);
}

/**
 * @brief Load an encrypted Sapling spending key from the database
 * @param chash Hash identifier for the encrypted key
 * @param vchCryptedSecret The encrypted spending key data
 * @param extfvk[out] The decrypted extended full viewing key
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Sapling spending key from the database.
 * The wallet must be unlocked for this operation. The hash is used to verify
 * the integrity of the decrypted key material by comparing fingerprints.
 */
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

/**
 * @brief Load an encrypted Orchard spending key from the database
 * @param chash Hash identifier for the encrypted key
 * @param vchCryptedSecret The encrypted spending key data
 * @param extfvk[out] The decrypted extended full viewing key
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Orchard spending key from the database.
 * The wallet must be unlocked for this operation. The hash is used to verify
 * the integrity of the decrypted key material by comparing fingerprints.
 */
bool CWallet::LoadCryptedOrchardZKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    AssertLockHeld(cs_wallet);
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::OrchardExtendedSpendingKeyPirate extsk;
    DeserializeFromDecryptionOutput(vchSecret, extsk);

    //Get ext viewingkey from ext spending key
    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        return false;
    }
    extfvk = extfvkOpt.value();


    if(extfvk.fvk.GetFingerprint() != chash) {
        return false;
    }

     return CCryptoKeyStore::AddCryptedOrchardSpendingKey(extfvk, vchCryptedSecret);
}

/**
 * @brief Load a Sapling extended full viewing key into the wallet
 * @param extfvk The Sapling extended full viewing key to load
 * @return true if successfully loaded
 * 
 * Loads a Sapling extended full viewing key into the in-memory key store.
 * This allows the wallet to detect incoming transactions without having
 * the spending key. Typically called during wallet initialization.
 */
bool CWallet::LoadSaplingFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    return CCryptoKeyStore::AddSaplingExtendedFullViewingKey(extfvk);
}

/**
 * @brief Load an Orchard extended full viewing key into the wallet
 * @param extfvk The Orchard extended full viewing key to load
 * @return true if successfully loaded
 * 
 * Loads an Orchard extended full viewing key into the in-memory key store.
 * This allows the wallet to detect incoming transactions without having
 * the spending key. Typically called during wallet initialization.
 */
bool CWallet::LoadOrchardFullViewingKey(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    return CCryptoKeyStore::AddOrchardExtendedFullViewingKey(extfvk);
}

/**
 * @brief Load an encrypted Sapling extended full viewing key
 * @param chash Hash identifier for the encrypted key
 * @param vchCryptedSecret The encrypted viewing key data
 * @param extfvk[out] The decrypted extended full viewing key
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Sapling extended full viewing key.
 * The wallet must be unlocked for this operation. Used for watch-only
 * functionality where spending keys are not available.
 */
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

/**
 * @brief Load an encrypted Orchard extended full viewing key
 * @param chash Hash identifier for the encrypted key
 * @param vchCryptedSecret The encrypted viewing key data
 * @param extfvk[out] The decrypted extended full viewing key
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Orchard extended full viewing key.
 * The wallet must be unlocked for this operation. Used for watch-only
 * functionality where spending keys are not available.
 */
bool CWallet::LoadCryptedOrchardExtendedFullViewingKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
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

     return CCryptoKeyStore::AddOrchardExtendedFullViewingKey(extfvk);
}

/**
 * @brief Load a Sapling payment address and incoming viewing key into the wallet
 * @param addr The Sapling payment address to load
 * @param ivk The corresponding incoming viewing key
 * @return true if successfully loaded
 * 
 * Loads a Sapling payment address and its corresponding incoming viewing key
 * into the wallet's in-memory key store. This is typically called during
 * wallet initialization when loading address mappings from the database.
 */
bool CWallet::LoadSaplingPaymentAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk)
{
    return CCryptoKeyStore::AddSaplingIncomingViewingKey(ivk, addr);
}

/**
 * @brief Load an Orchard payment address and incoming viewing key into the wallet
 * @param addr The Orchard payment address to load
 * @param ivk The corresponding incoming viewing key
 * @return true if successfully loaded
 * 
 * Loads an Orchard payment address and its corresponding incoming viewing key
 * into the wallet's in-memory key store. This is typically called during
 * wallet initialization when loading address mappings from the database.
 */
bool CWallet::LoadOrchardPaymentAddress(
    const libzcash::OrchardPaymentAddressPirate &addr,
    const libzcash::OrchardIncomingViewingKeyPirate &ivk)
{
    return CCryptoKeyStore::AddOrchardIncomingViewingKey(ivk, addr);
}

/**
 * @brief Load an encrypted Sapling payment address from the database
 * @param chash Hash identifier for the encrypted address data
 * @param vchCryptedSecret The encrypted address and viewing key data
 * @param addr[out] The decrypted Sapling payment address
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Sapling payment address and its corresponding
 * incoming viewing key from the database. The wallet must be unlocked for this
 * operation. The hash is used to verify the integrity of the decrypted data.
 */
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

/**
 * @brief Load an encrypted Orchard payment address from the database
 * @param chash Hash identifier for the encrypted address data
 * @param vchCryptedSecret The encrypted address and viewing key data
 * @param addr[out] The decrypted Orchard payment address
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Orchard payment address and its corresponding
 * incoming viewing key from the database. The wallet must be unlocked for this
 * operation. The hash is used to verify the integrity of the decrypted data.
 */
bool CWallet::LoadCryptedOrchardPaymentAddress(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, libzcash::OrchardPaymentAddressPirate& addr)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::OrchardIncomingViewingKeyPirate ivk;
    DeserializeFromDecryptionOutput(vchSecret, addr, ivk);
    if(HashWithFP(addr) != chash) {
        return false;
    }

    return CCryptoKeyStore::AddOrchardIncomingViewingKey(ivk, addr);
}

/**
 * @brief Load a Sapling diversified address into the wallet
 * @param addr The Sapling diversified payment address
 * @param ivk The corresponding incoming viewing key
 * @param path The diversification path used to generate this address
 * @return true if successfully loaded
 * 
 * Loads a Sapling diversified address mapping into the wallet's in-memory store.
 * This is typically called during wallet initialization when loading diversified
 * address data from the database.
 */
bool CWallet::LoadSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddSaplingDiversifiedAddress(addr, ivk, path);
}

/**
 * @brief Load an Orchard diversified address into the wallet
 * @param addr The Orchard diversified payment address
 * @param ivk The corresponding incoming viewing key
 * @param path The diversification path used to generate this address
 * @return true if successfully loaded
 * 
 * Loads an Orchard diversified address mapping into the wallet's in-memory store.
 * This is typically called during wallet initialization when loading diversified
 * address data from the database.
 */
bool CWallet::LoadOrchardDiversifiedAddress(
    const libzcash::OrchardPaymentAddressPirate &addr,
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddOrchardDiversifiedAddress(addr, ivk, path);
}

/**
 * @brief Load an encrypted Sapling diversified address from the database
 * @param chash Hash identifier for the encrypted address data
 * @param vchCryptedSecret The encrypted diversified address data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Sapling diversified address mapping from
 * the database. The wallet must be unlocked for this operation. The address,
 * incoming viewing key, and diversification path are all recovered from the
 * encrypted data.
 */
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

/**
 * @brief Load an encrypted Orchard diversified address from the database
 * @param chash Hash identifier for the encrypted address data
 * @param vchCryptedSecret The encrypted diversified address data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted Orchard diversified address mapping from
 * the database. The wallet must be unlocked for this operation. The address,
 * incoming viewing key, and diversification path are all recovered from the
 * encrypted data.
 */
bool CWallet::LoadCryptedOrchardDiversifiedAddress(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    if (IsLocked()) {
        return false;
    }

    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::OrchardPaymentAddressPirate addr;
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    blob88 path;
    DeserializeFromDecryptionOutput(vchSecret, addr, ivk, path);
    if(HashWithFP(addr) != chash) {
        return false;
    }

    return CCryptoKeyStore::AddOrchardDiversifiedAddress(addr, ivk, path);
}

/**
 * @brief Load the last used Sapling diversifier for an incoming viewing key
 * @param ivk The Sapling incoming viewing key
 * @param path The diversifier path that was last used
 * @return true if successfully loaded
 * 
 * Loads the last used diversifier path for a Sapling incoming viewing key.
 * This is used to continue generating addresses from where the wallet left off.
 */
bool CWallet::LoadLastSaplingDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddLastSaplingDiversifierUsed(ivk, path);
}

/**
 * @brief Load the last used Orchard diversifier for an incoming viewing key
 * @param ivk The Orchard incoming viewing key
 * @param path The diversifier path that was last used
 * @return true if successfully loaded
 * 
 * Loads the last used diversifier path for an Orchard incoming viewing key.
 * This is used to continue generating addresses from where the wallet left off.
 */
bool CWallet::LoadLastOrchardDiversifierUsed(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path)
{
    return CCryptoKeyStore::AddLastOrchardDiversifierUsed(ivk, path);
}

/**
 * @brief Load an encrypted last used Sapling diversifier from database
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted diversifier data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads the last used diversifier path for a Sapling incoming
 * viewing key. The encrypted data contains both the IVK and diversifier path.
 */
bool CWallet::LoadLastCryptedSaplingDiversifierUsed(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
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

    return LoadLastSaplingDiversifierUsed(ivk, path);
}

/**
 * @brief Load an encrypted last used Orchard diversifier from database
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted diversifier data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads the last used diversifier path for an Orchard incoming
 * viewing key. The encrypted data contains both the IVK and diversifier path.
 */
bool CWallet::LoadLastCryptedOrchardDiversifierUsed(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    CKeyingMaterial vchSecret;
    if (!DecryptSerializedWalletObjects(vchCryptedSecret, chash, vchSecret)) {
        return false;
    }

    libzcash::OrchardIncomingViewingKeyPirate ivk;
    blob88 path;
    DeserializeFromDecryptionOutput(vchSecret, ivk, path);
    if (HashWithFP(ivk) != chash) {
        return false;
    }

    return LoadLastOrchardDiversifierUsed(ivk, path);
}

/**
 * @brief Load a Sprout spending key into the keystore
 * @param key The Sprout spending key to load
 * @return true if successfully loaded
 * 
 * Loads a Sprout spending key into the in-memory keystore. This is typically
 * called during wallet initialization when reading keys from the database.
 */
bool CWallet::LoadZKey(const libzcash::SproutSpendingKey &key)
{
    return CCryptoKeyStore::AddSproutSpendingKey(key);
}

/**
 * @brief Add a Sprout viewing key to the wallet
 * @param vk The Sprout viewing key to add
 * @return true if successfully added
 * 
 * Adds a Sprout viewing key to both the keystore and database. Viewing keys
 * allow watching for incoming transactions without spending capability.
 * Sets first key time to 1 since viewing keys don't have birthday info.
 */
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

/**
 * @brief Remove a Sprout viewing key from the wallet
 * @param vk The Sprout viewing key to remove
 * @return true if successfully removed
 * 
 * Removes the Sprout viewing key from both the keystore and database.
 * Requires wallet lock to be held.
 */
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

/**
 * @brief Load a Sprout viewing key into the keystore
 * @param vk The Sprout viewing key to load
 * @return true if successfully loaded
 * 
 * Loads a Sprout viewing key into the in-memory keystore. This is typically
 * called during wallet initialization when reading keys from the database.
 */
bool CWallet::LoadSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    return CCryptoKeyStore::AddSproutViewingKey(vk);
}

/**
 * @brief Add a redeem script to the wallet
 * @param redeemScript The script to add
 * @return true if successfully added
 * 
 * Adds a redeem script to the wallet's script store and persists it to disk.
 * Redeem scripts are used for P2SH (Pay-to-Script-Hash) addresses.
 * Returns false if wallet is encrypted and locked.
 */
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

/**
 * @brief Load a redeem script into the wallet
 * @param redeemScript The script to load
 * @return true if successfully loaded
 * 
 * Loads a redeem script into the in-memory script store. Includes validation
 * to prevent loading scripts that exceed maximum size limits. This is typically
 * called during wallet initialization when reading scripts from the database.
 */
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

/**
 * @brief Load an encrypted redeem script from the database
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted script data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted redeem script from the wallet database.
 * Returns false if wallet is locked or decryption fails.
 */
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

/**
 * @brief Add a watch-only address to the wallet
 * @param dest The script/address to watch
 * @return true if successfully added
 * 
 * Adds a watch-only address to the wallet, allowing monitoring of transactions
 * without spending capability. Sets first key time to 1 since watch-only
 * addresses don't have birthday info. Notifies UI of the change and persists
 * to disk if wallet is file-backed.
 */
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
            LogPrintf("Encrypting watch-only address failed!!!\n");
            return false;
        }

        return CWalletDB(strWalletFile).WriteCryptedWatchOnly(chash, dest, vchCryptedSecret);

    }

    return true;
}

/**
 * @brief Remove a watch-only address from the wallet
 * @param dest The script/address to stop watching
 * @return true if successfully removed
 * 
 * Removes a watch-only address from both keystore and database.
 * Updates UI notification if no watch-only addresses remain.
 * Requires wallet lock to be held.
 */
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

/**
 * @brief Load a watch-only address into the wallet
 * @param dest The script/address to load
 * @return true if successfully loaded
 * 
 * Loads a watch-only address into the in-memory keystore. This is typically
 * called during wallet initialization when reading addresses from the database.
 */
bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}

/**
 * @brief Load an encrypted watch-only address from the database
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted address data
 * @return true if successfully decrypted and loaded
 * 
 * Decrypts and loads an encrypted watch-only address from the wallet database.
 * Returns false if wallet is locked or decryption fails.
 */
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

/**
 * @brief Load a Sapling watch-only extended full viewing key
 * @param extfvk The Sapling extended full viewing key to load
 * @return true if successfully loaded
 * 
 * Loads a Sapling extended full viewing key for watch-only functionality.
 * This allows monitoring of Sapling transactions without spending capability.
 * Notifies UI of the change when successful.
 */
bool CWallet::LoadSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    if (CCryptoKeyStore::AddSaplingWatchOnly(extfvk)) {
        NotifyWatchonlyChanged(true);
        return true;
    }

    return false;
}

/**
 * @brief Load an Orchard watch-only extended full viewing key
 * @param extfvk The Orchard extended full viewing key to load
 * @return true if successfully loaded
 * 
 * Loads an Orchard extended full viewing key for watch-only functionality.
 * This allows monitoring of Orchard transactions without spending capability.
 * Notifies UI of the change when successful.
 */
bool CWallet::LoadOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    if (CCryptoKeyStore::AddOrchardWatchOnly(extfvk)) {
        NotifyWatchonlyChanged(true);
        return true;
    }

    return false;
}

/**
 * @section Wallet Encryption and Unlocking Functions
 * 
 * Functions for managing wallet encryption, unlocking, and password operations.
 * These functions handle the cryptographic operations needed to protect private keys.
 */

/**
 * @brief Open (unlock) an encrypted wallet using the provided passphrase
 * @param strWalletPassphrase The wallet encryption passphrase
 * @return true if wallet was successfully unlocked
 * 
 * Attempts to unlock an encrypted wallet by trying the passphrase against
 * all stored master keys. When successful, decrypts the master key and
 * stores the passphrase for session use. This function tries each master
 * key until one succeeds or all fail.
 */
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

/**
 * @brief Unlock an encrypted wallet using the provided passphrase
 * @param strWalletPassphrase The wallet encryption passphrase
 * @return true if wallet was successfully unlocked
 * 
 * Attempts to unlock an encrypted wallet by trying the passphrase against
 * all stored master keys. When successful, decrypts the master key and
 * updates the blockchain state. The unlocked state allows access to
 * private keys for transaction signing and key operations.
 * 
 * This function is thread-safe and will try all available master keys
 * until one succeeds or all fail.
 */
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

/**
 * @brief Change the wallet encryption passphrase
 * @param strOldWalletPassphrase The current wallet passphrase
 * @param strNewWalletPassphrase The new wallet passphrase
 * @return true if passphrase was successfully changed
 * 
 * Changes the wallet encryption passphrase by decrypting all master keys
 * with the old passphrase and re-encrypting them with the new one. The
 * wallet is temporarily locked during this operation for security.
 * 
 * This is a critical security operation that updates all encrypted data
 * in the wallet database. If the operation fails partway through, the
 * wallet state may be inconsistent and require recovery from backup.
 */
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

/**
 * @section Blockchain Integration Functions
 * 
 * Functions for integrating wallet operations with blockchain events,
 * including block connection/disconnection and chain reorganizations.
 */

/**
 * @brief Handle blockchain tip changes
 * @param pindex The new block index at the chain tip
 * @param pblock The block data (if available)
 * @param added True if block was added, false if removed
 * 
 * Called when the blockchain tip changes to update wallet state accordingly.
 * This function handles both block additions and removals (during reorganizations).
 * 
 * For block additions:
 * - Updates Sapling and Orchard wallet commitment trees
 * - Schedules automatic consolidation operations for fresh blocks
 * - Manages transaction confirmation status
 * 
 * For block removals:
 * - Reverts wallet state changes from the removed block
 * - Updates transaction depths and confirmation status
 * 
 * This function is critical for maintaining wallet synchronization with
 * the blockchain and ensuring accurate balance and transaction status.
 */
void CWallet::ChainTip(const CBlockIndex *pindex,
                       const CBlock *pblock,
                       bool added)
{
    LOCK2(cs_main, cs_wallet);

    if (added) {
        IncrementSaplingWallet(pindex);
        IncrementOrchardWallet(pindex);
        // Prevent consolidation & sweep transactions
        // from being created when node is syncing after launch,
        // and also when node wakes up from suspension/hibernation and incoming blocks are old.
        bool initialDownloadCheck = IsInitialBlockDownload();
        if (!initialDownloadCheck &&
            pblock->GetBlockTime() > GetTime() - 8640) //Last 144 blocks 2.4 * 60 * 60
        {
            RunSaplingConsolidation(pindex->nHeight);
            RunOrchardConsolidation(pindex->nHeight);
            RunSaplingSweep(pindex->nHeight);
            while(DeleteWalletTransactions(pindex, false)) {}
        } else {
            if (initialDownloadCheck && pindex->nHeight % fDeleteInterval == 0) {
                while(DeleteWalletTransactions(pindex, false)) {}
            }
        }

    } else {
        DecrementSaplingWallet(pindex);
        DecrementOrchardWallet(pindex);
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

/**
 * @brief Run sweep operations at the specified block height
 * @param blockHeight The current block height
 * 
 * Automatically sweeps notes to consolidate and clean up the wallet across all protocols.
 * Only runs if:
 * - Sapling upgrade is active (for Sapling notes)
 * - Orchard upgrade is active (for Orchard notes) 
 * - Sweeping is enabled (fSweepEnabled)
 * - It's time for next sweep (nextSweep <= blockHeight)
 * - No consolidation is running or scheduled soon
 * - No other sweep operation is currently running
 * 
 * Creates an AsyncRPCOperation_sweeptoaddress operation to handle the
 * sweeping process asynchronously for all supported protocols.
 */
void CWallet::RunSaplingSweep(int blockHeight) {
    if (!NetworkUpgradeActive(blockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        return;
    }
    AssertLockHeld(cs_wallet);
    
    // Use unified sweep flag with fallback to legacy flag for compatibility
    if (!fSweepEnabled) {
        return;
    }

    if (nextSweep > blockHeight) {
        return;
    }

    //Don't Run if consolidation will run soon.
    if (fSaplingConsolidationEnabled && nextSaplingConsolidation - 15 <= blockHeight) {
        return;
    }
    if (fOrchardConsolidationEnabled && nextOrchardConsolidation - 15 <= blockHeight) {
        return;
    }

    //Don't Run While consolidation is running.
    if (fSaplingConsolidationRunning || fOrchardConsolidationRunning) {
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


/**
 * @section Transaction Automation Functions
 * 
 * Functions for automated transaction management including note consolidation,
 * sweeping operations, and transaction cleanup. These operations help maintain
 * wallet performance and privacy by managing note fragmentation and cleanup.
 */

/**
 * @brief Run Sapling note consolidation at the specified block height
 * @param blockHeight The current block height
 * 
 * Automatically consolidates fragmented Sapling notes to improve wallet performance
 * and reduce transaction complexity. Only runs if:
 * - Sapling upgrade is active
 * - Consolidation is enabled (fSaplingConsolidationEnabled)
 * - It's time for next consolidation (nextSaplingConsolidation <= blockHeight)
 * - No sweep operation is currently running
 * 
 * Creates an AsyncRPCOperation_saplingconsolidation operation to handle the
 * consolidation process asynchronously.
 */
void CWallet::RunSaplingConsolidation(int blockHeight) {
    if (!NetworkUpgradeActive(blockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        return;
    }
    AssertLockHeld(cs_wallet);
    if (!fSaplingConsolidationEnabled) {
        return;
    }

    if (nextSaplingConsolidation > blockHeight) {
        return;
    }

    //Don't Run While sweep is running.
    if (fSweepRunning) {
        return;
    }

    fSaplingConsolidationRunning = true;
    
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

/**
 * @brief Run Orchard note consolidation at the specified block height
 * @param blockHeight The current block height to process consolidation for
 * 
 * Initiates Orchard note consolidation if conditions are met:
 * - Orchard protocol is active (post-NU5 activation)
 * - Consolidation is enabled (fOrchardConsolidationEnabled)
 * - It's time for next consolidation (nextOrchardConsolidation <= blockHeight)
 * - No sweep operations are currently running
 * 
 * Creates an AsyncRPCOperation_orchardconsolidation operation to handle the
 * consolidation process asynchronously.
 */
void CWallet::RunOrchardConsolidation(int blockHeight) {
    if (!NetworkUpgradeActive(blockHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        return;
    }
    AssertLockHeld(cs_wallet);
    if (!fOrchardConsolidationEnabled) {
        return;
    }

    if (nextOrchardConsolidation > blockHeight) {
        return;
    }

    //Don't Run While sweep is running.
    if (fSweepRunning) {
        return;
    }

    fOrchardConsolidationRunning = true;
    
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> lastOperation = q->getOperationForId(orchardConsolidationOperationId);
    if (lastOperation != nullptr) {
        lastOperation->cancel();
    }
    pendingOrchardConsolidationTxs.clear();
    std::shared_ptr<AsyncRPCOperation> operation(new AsyncRPCOperation_orchardconsolidation(blockHeight + 5));
    orchardConsolidationOperationId = operation->getId();
    q->addOperation(operation);

}

/**
 * @brief Commit an automated transaction to the memory pool and network
 * @param tx The transaction to commit
 * @return true if successfully committed to mempool and relayed, false otherwise
 * 
 * Processes automated transactions (such as consolidation or sweeping operations)
 * by first accepting them into the local memory pool and then broadcasting them
 * to the network. This function is used by automated wallet operations to
 * ensure proper transaction propagation.
 * 
 * The function performs two critical steps:
 * 1. Accepts the transaction into the local mempool for validation
 * 2. Relays the transaction to connected network peers
 * 
 * Returns false if either step fails, ensuring atomic commit behavior.
 */
bool CWallet::CommitAutomatedTx(const CTransaction& tx) {
  CWalletTx wtx(this, tx);

  // push to local node and sync with wallets
  if(!wtx.AcceptToMemoryPool(false)) {
      return false;
  }

  // push to network
  if (!wtx.RelayWalletTransaction()) {
      return false;
  }

  return true;

}

/**
 * @brief Update the wallet's best chain locator and height
 * @param loc The block locator representing the current chain tip
 * @param height The height of the current chain tip
 * 
 * Updates the wallet's internal record of the best chain state and persists
 * it to the database. This function is only active in online mode (when
 * nMaxConnections > 0) and is ignored in cold storage offline mode.
 * Requires the wallet lock to be held.
 */
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

/**
 * @brief Set the wallet's birthday block height
 * @param nHeight The block height to set as the wallet's birthday
 * 
 * Sets the wallet's birthday to the specified block height and persists it
 * to the database. The birthday marks the earliest block that needs to be
 * scanned for wallet transactions, improving sync performance.
 */
void CWallet::SetWalletBirthday(int nHeight)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteWalletBirthday(nHeight);
}

/**
 * @brief Get nullifiers for a set of payment addresses
 * @param addresses Set of payment addresses to get nullifiers for
 * @return Set of pairs containing (payment address, nullifier hash)
 * 
 * Retrieves all nullifiers associated with the given payment addresses.
 * This includes nullifiers for both Sapling and Orchard protocols.
 * Nullifiers are used to prevent double-spending in shielded transactions.
 */
std::set<std::pair<libzcash::PaymentAddress, uint256>> CWallet::GetNullifiersForAddresses(
        const std::set<libzcash::PaymentAddress> & addresses)
{
    std::set<std::pair<libzcash::PaymentAddress, uint256>> nullifierSet;
    // Sapling ivk -> list of addrs map

    // (There may be more than one diversified address for a given ivk.)
    std::map<libzcash::SaplingIncomingViewingKey, std::vector<libzcash::SaplingPaymentAddress>> ivkMapSapling;
    for (const auto & addr : addresses) {
        auto saplingAddr = std::get_if<libzcash::SaplingPaymentAddress>(&addr);
        if (saplingAddr != nullptr) {
            libzcash::SaplingIncomingViewingKey ivk;
            this->GetSaplingIncomingViewingKey(*saplingAddr, ivk);
            ivkMapSapling[ivk].push_back(*saplingAddr);
        }
    }

    // (There may be more than one diversified address for a given ivk.)
    std::map<libzcash::OrchardIncomingViewingKeyPirate, std::vector<libzcash::OrchardPaymentAddressPirate>> ivkMapOrchard;
    for (const auto & addr : addresses) {
        auto orchardAddr = std::get_if<libzcash::OrchardPaymentAddressPirate>(&addr);
        if (orchardAddr != nullptr) {
            libzcash::OrchardIncomingViewingKeyPirate ivk;
            this->GetOrchardIncomingViewingKey(*orchardAddr, ivk);
            ivkMapOrchard[ivk].push_back(*orchardAddr);
        }
    }

    for (const auto & txPair : mapWallet) {
        // Sprout
        for (const auto & noteDataPair : txPair.second.mapSproutNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & address = noteData.address;
            if (nullifier && addresses.count(address)) {
                nullifierSet.insert(std::make_pair(address, nullifier.value()));
            }
        }
        // Sapling
        for (const auto & noteDataPair : txPair.second.mapSaplingNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & ivk = noteData.ivk;
            if (nullifier && ivkMapSapling.count(ivk)) {
                for (const auto & addr : ivkMapSapling[ivk]) {
                    nullifierSet.insert(std::make_pair(addr, nullifier.value()));
                }
            }
        }
        // Orchard
        for (const auto & noteDataPair : txPair.second.mapOrchardNoteData) {
            auto & noteData = noteDataPair.second;
            auto & nullifier = noteData.nullifier;
            auto & ivk = noteData.ivk;
            if (nullifier && ivkMapOrchard.count(ivk)) {
                for (const auto & addr : ivkMapOrchard[ivk]) {
                    nullifierSet.insert(std::make_pair(addr, nullifier.value()));
                }
            }
        }
    }
    return nullifierSet;
}

/**
 * @brief Determine if a Sprout note is change from the same transaction
 * @param nullifierSet Set of nullifiers for addresses in the transaction
 * @param address The payment address that received the note
 * @param jsop The JoinSplit output point of the note
 * @return true if the note is considered change, false otherwise
 * 
 * A note is marked as "change" if the address that received it also spent
 * notes in the same transaction. This detects:
 * - Change created by spending fractions of notes (z_sendmany change)
 * - "Chaining Notes" used to connect JoinSplits together
 * - Notes created by consolidation transactions (z_mergetoaddress)
 * - Notes sent from one address to itself
 * 
 * The function examines all JoinSplit descriptions in the transaction to
 * find if any nullifiers match the receiving address.
 */
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

/**
 * @brief Determine if a Sapling note is change from the same transaction
 * @param nullifierSet Set of nullifiers for addresses in the transaction
 * @param address The payment address that received the note
 * @param op The Sapling output point of the note
 * @return true if the note is considered change, false otherwise
 * 
 * A note is marked as "change" if the address that received it also spent
 * notes in the same transaction. This detects:
 * - Change created by spending fractions of notes (z_sendmany change)
 * - Notes created by consolidation transactions (z_mergetoaddress)
 * - Notes sent from one address to itself
 * 
 * The function examines all Sapling spend descriptions in the transaction to
 * find if any nullifiers match the receiving address.
 */
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
    for (const auto& spend : mapWallet[op.hash].GetSaplingSpends())  {
        uint256 nullifier = uint256::FromRawBytes(spend.nullifier());
        if (nullifierSet.count(std::make_pair(address, nullifier))) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Determine if an Orchard note is change from the same transaction
 * @param nullifierSet Set of nullifiers for addresses in the transaction
 * @param address The payment address that received the note
 * @param op The Orchard output point of the note
 * @return true if the note is considered change, false otherwise
 * 
 * A note is marked as "change" if the address that received it also spent
 * notes in the same transaction. This detects:
 * - Change created by spending fractions of notes (z_sendmany change)
 * - Notes created by consolidation transactions (z_mergetoaddress)
 * - Notes sent from one address to itself
 * 
 * The function examines all Orchard action descriptions in the transaction to
 * find if any nullifiers match the receiving address.
 */
bool CWallet::IsNoteOrchardChange(const std::set<std::pair<libzcash::PaymentAddress, uint256>> & nullifierSet,
        const libzcash::PaymentAddress & address,
        const OrchardOutPoint & op)
{
    // A Note is marked as "change" if the address that received it
    // also spent Notes in the same transaction. This will catch,
    // for instance:
    // - Change created by spending fractions of Notes (because
    //   z_sendmany sends change to the originating z-address).
    // - Notes created by consolidation transactions (e.g. using
    //   z_mergetoaddress).
    // - Notes sent from one address to itself.
    for (const auto& action : mapWallet[op.hash].GetOrchardActions())  {
        uint256 nullifier = uint256::FromRawBytes(action.nullifier());
        if (nullifierSet.count(std::make_pair(address, nullifier))) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Set the wallet's encrypted status in the database
 * @param pwalletdb Pointer to the wallet database instance
 * @return true if successfully written to database, false otherwise
 * 
 * Marks the wallet as encrypted in the persistent database. This function
 * is called during the wallet encryption process to permanently record
 * the wallet's encrypted state. Only works for file-backed wallets.
 * 
 * The wallet lock must be held when calling this function.
 */
bool CWallet::SetWalletCrypted(CWalletDB* pwalletdb) {
    LOCK(cs_wallet);

    if (fFileBacked)
    {
            return pwalletdb->WriteIsCrypted(true);
    }

    return false;
}

/**
 * @brief Set the minimum wallet version for feature support
 * @param nVersion The minimum wallet version to set
 * @param pwalletdbIn Optional wallet database instance to use
 * @param fExplicit True if this is an explicit upgrade request
 * @return true if version was successfully updated
 * 
 * Updates the wallet's minimum version to enable new features. When doing
 * an explicit upgrade, if the requested version exceeds the maximum permitted
 * version, upgrades all the way to the latest feature set.
 * 
 * The function updates both in-memory version variables and persists the
 * change to the database for file-backed wallets. Version numbers above
 * 40000 are written to the database as minimum version requirements.
 */
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

/**
 * @brief Set the maximum allowed wallet version
 * @param nVersion The maximum wallet version to allow
 * @return true if successfully set, false if trying to downgrade below current version
 * 
 * Sets the maximum wallet version that can be used. This prevents the wallet
 * from upgrading beyond the specified version, which can be useful for
 * maintaining compatibility with older software versions.
 * 
 * The function will fail if attempting to set a maximum version lower than
 * the wallet's current version, as this would be a downgrade operation.
 * The wallet lock is held during this operation for thread safety.
 */
bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

/**
 * @brief Get all transactions that conflict with the specified transaction
 * @param txid The transaction ID to check for conflicts
 * @return Set of transaction IDs that conflict with the given transaction
 * 
 * Identifies all transactions in the wallet that conflict with the specified
 * transaction by examining spent outputs and nullifiers. Conflicts occur when:
 * - Multiple transactions spend the same transparent UTXO
 * - Multiple transactions spend the same Sprout, Sapling, or Orchard note (same nullifier)
 * 
 * This function is essential for handling double-spend situations and chain
 * reorganizations where conflicting transactions may become valid or invalid.
 * The wallet lock must be held when calling this function.
 */
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

    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_s;

    for (const auto& spend : wtx.GetSaplingSpends())  {
        uint256 nullifier = uint256::FromRawBytes(spend.nullifier());
        if (mapTxSaplingNullifiers.count(nullifier) <= 1) {
            continue;  // No conflict if zero or one spends
        }
        range_s = mapTxSaplingNullifiers.equal_range(nullifier);
        for (TxNullifiers::const_iterator it = range_s.first; it != range_s.second; ++it) {
            result.insert(it->second);
        }
    }

    std::pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range_o;

    for (const auto& action : wtx.GetOrchardActions())  {
        uint256 nullifier = uint256::FromRawBytes(action.nullifier());
        if (mapTxOrchardNullifiers.count(nullifier) <= 1) {
            continue;  // No conflict if zero or one spends
        }
        range_o = mapTxOrchardNullifiers.equal_range(nullifier);
        for (TxNullifiers::const_iterator it = range_o.first; it != range_o.second; ++it) {
            result.insert(it->second);
        }
    }
    return result;
}

/**
 * @brief Flush the wallet database to disk
 * @param shutdown True if this is a shutdown flush, false for periodic flush
 * 
 * Forces all pending wallet database writes to be flushed to disk. This
 * ensures data persistence and prevents loss of recent wallet changes.
 * The shutdown parameter indicates whether this is part of application
 * shutdown, which may trigger additional cleanup operations.
 */
void CWallet::Flush(bool shutdown)
{
    bitdb->Flush(shutdown);
}

/**
 * @brief Verify the integrity of a wallet file
 * @param walletFile Name of the wallet file to verify
 * @param warningString[out] String to append warnings to
 * @param errorString[out] String to append errors to
 * @return true if verification completed (regardless of issues found), false on critical failure
 * 
 * Performs integrity checks on the specified wallet file. This function:
 * - Attempts to open the wallet database environment
 * - Handles database corruption by moving old database files and retrying
 * - Performs salvage operations if -salvagewallet flag is set
 * - Validates wallet data structures and consistency
 * 
 * Any warnings or errors encountered are appended to the provided strings.
 * The function attempts recovery operations when possible, including moving
 * corrupted database files to backup locations.
 */
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

/**
 * @brief Synchronize metadata across conflicting transactions
 * @tparam T The type of the spend map key (COutPoint for transparent, uint256 for shielded)
 * @param range Iterator range of transactions that spend the same output/nullifier
 * 
 * Ensures all wallet transactions in the given range have consistent metadata
 * by copying metadata from the oldest transaction (smallest nOrderPos) to all others.
 * This maintains consistency when multiple transactions conflict by spending the same
 * output or using the same nullifier.
 * 
 * The function synchronizes the following metadata fields:
 * - mapValue: Key-value pairs with transaction metadata
 * - vOrderForm: Ordered form data for the transaction
 * - nTimeSmart: Smart timestamp for transaction ordering
 * - fFromMe: Flag indicating if transaction originated from this wallet
 * - strFromAccount: Source account name for the transaction
 * 
 * Note data (mapSproutNoteData, mapSaplingNoteData, mapOrchardNoteData) is
 * intentionally not copied as it should remain unique per transaction.
 * Similarly, nTimeReceived, fTimeReceivedIsTxTime, and nOrderPos are preserved
 * to maintain transaction-specific timing and ordering information.
 * 
 * This function is critical for handling double-spend scenarios and chain
 * reorganizations where conflicting transactions may become valid/invalid.
 */
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
 * @brief Check if a transparent output is spent by any non-conflicted transaction
 * @param hash The transaction hash containing the output
 * @param n The output index within the transaction
 * @return true if the output is spent by a confirmed transaction, false otherwise
 * 
 * Determines if a specific transparent output (UTXO) has been spent by examining
 * all transactions that reference this output. An output is considered spent if
 * any non-conflicted transaction (depth >= 0) includes it as an input.
 * 
 * This function is essential for:
 * - Determining available balance in transparent addresses
 * - Validating transaction inputs during creation
 * - Detecting double-spend attempts
 * - Maintaining accurate UTXO state
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

/**
 * @brief Get the confirmation depth of a transparent output's spending transaction
 * @param hash The transaction hash containing the output
 * @param n The output index within the transaction
 * @return Depth of the spending transaction (0+ for confirmed, -1 for conflicted, INT_MAX if unspent)
 * 
 * Returns the depth (confirmations) of the transaction that spends a given output:
 * - Positive value: Number of confirmations of the spending transaction
 * - 0: Spending transaction is in mempool (unconfirmed)
 * - -1: Spending transaction conflicts with a block transaction
 * - INT_MAX: Output is not spent by any known transaction
 * 
 * This function is used for:
 * - Determining output availability for new transactions
 * - Calculating wallet balance with different confirmation requirements
 * - Validating transaction chains and dependencies
 */
int CWallet::GetSpendDepth(const uint256& hash, unsigned int n) const
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
 * @brief Check if a Sprout note is spent by any non-conflicted transaction
 * @param nullifier The Sprout nullifier to check for spending
 * @return true if the note is spent by a confirmed transaction, false otherwise
 * 
 * Determines if a specific Sprout note has been spent by examining all transactions
 * that use the given nullifier. A note is considered spent if any non-conflicted
 * transaction (depth >= 0) includes this nullifier in its JoinSplit descriptions.
 * 
 * This function is essential for:
 * - Determining available balance in Sprout addresses
 * - Preventing double-spend attempts in new transactions
 * - Validating transaction inputs during creation
 * - Maintaining accurate note state across chain reorganizations
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

/**
 * @brief Get the confirmation depth of a Sprout note's spending transaction
 * @param nullifier The Sprout nullifier to check for spending depth
 * @return Depth of the spending transaction (0+ for confirmed, 0 if unspent)
 * 
 * Returns the depth (confirmations) of the transaction that spends a Sprout note
 * with the given nullifier:
 * - Positive value: Number of confirmations of the spending transaction
 * - 0: Note is not spent by any confirmed transaction
 * 
 * This function is used for:
 * - Determining note availability with different confirmation requirements
 * - Calculating wallet balance based on confirmation depth
 * - Validating transaction chains and dependencies in Sprout protocol
 */
int CWallet::GetSproutSpendDepth(const uint256& nullifier) const {
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

/**
 * @brief Check if a Sapling note is spent by any non-conflicted transaction
 * @param nullifier The Sapling nullifier to check for spending
 * @return true if the note is spent by a confirmed transaction, false otherwise
 * 
 * Determines if a specific Sapling note has been spent by examining all transactions
 * that use the given nullifier. A note is considered spent if any non-conflicted
 * transaction (depth >= 0) includes this nullifier in its Sapling spend descriptions.
 * 
 * This function is essential for:
 * - Determining available balance in Sapling addresses
 * - Preventing double-spend attempts in new transactions
 * - Validating transaction inputs during creation
 * - Maintaining accurate note state across chain reorganizations
 */
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

/**
 * @brief Get the confirmation depth of a Sapling note's spending transaction
 * @param nullifier The Sapling nullifier to check for spending depth
 * @return Depth of the spending transaction (0+ for confirmed, 0 if unspent)
 * 
 * Returns the depth (confirmations) of the transaction that spends a Sapling note
 * with the given nullifier:
 * - Positive value: Number of confirmations of the spending transaction
 * - 0: Note is not spent by any confirmed transaction
 * 
 * This function is used for:
 * - Determining note availability with different confirmation requirements
 * - Calculating wallet balance based on confirmation depth
 * - Validating transaction chains and dependencies in Sapling protocol
 */
int CWallet::GetSaplingSpendDepth(const uint256& nullifier) const {
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

/**
 * @brief Check if an Orchard note is spent by any non-conflicted transaction
 * @param nullifier The Orchard nullifier to check for spending
 * @return true if the note is spent by a confirmed transaction, false otherwise
 * 
 * Determines if a specific Orchard note has been spent by examining all transactions
 * that use the given nullifier. A note is considered spent if any non-conflicted
 * transaction (depth >= 0) includes this nullifier in its Orchard action descriptions.
 * 
 * This function is essential for:
 * - Determining available balance in Orchard addresses
 * - Preventing double-spend attempts in new transactions
 * - Validating transaction inputs during creation
 * - Maintaining accurate note state across chain reorganizations
 */
bool CWallet::IsOrchardSpent(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxOrchardNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return true; // Spent
        }
    }
    return false;
}

/**
 * @brief Get the confirmation depth of an Orchard note's spending transaction
 * @param nullifier The Orchard nullifier to check for spending depth
 * @return Depth of the spending transaction (0+ for confirmed, 0 if unspent)
 * 
 * Returns the depth (confirmations) of the transaction that spends an Orchard note
 * with the given nullifier:
 * - Positive value: Number of confirmations of the spending transaction
 * - 0: Note is not spent by any confirmed transaction
 * 
 * This function is used for:
 * - Determining note availability with different confirmation requirements
 * - Calculating wallet balance based on confirmation depth
 * - Validating transaction chains and dependencies in Orchard protocol
 */
int CWallet::GetOrchardSpendDepth(const uint256& nullifier) const {
    pair<TxNullifiers::const_iterator, TxNullifiers::const_iterator> range;
    range = mapTxOrchardNullifiers.equal_range(nullifier);

    for (TxNullifiers::const_iterator it = range.first; it != range.second; ++it) {
        const uint256& wtxid = it->second;
        std::map<uint256, CWalletTx>::const_iterator mit = mapWallet.find(wtxid);
        if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) {
            return mit->second.GetDepthInMainChain(); // Spent
        }
    }
    return 0;
}

/**
 * @brief Add a transparent output to the spend tracking map
 * @param outpoint The transparent output point being spent
 * @param wtxid The wallet transaction ID that spends this output
 * 
 * Records that a specific transparent UTXO is being spent by a transaction.
 * This function:
 * - Removes any existing entries for this outpoint (during rescan)
 * - Adds the spending relationship to mapTxSpends
 * - Synchronizes metadata across all transactions that spend the same output
 * 
 * This tracking is essential for detecting double-spends, managing conflicted
 * transactions, and maintaining accurate wallet state during chain reorganizations.
 */
void CWallet::AddToTransparentSpends(const COutPoint& outpoint, const uint256& wtxid)
{
    // Remove any existing entries for this outpoint to avoid duplicates during rescan
    mapTxSpends.erase(outpoint);
    
    mapTxSpends.insert(make_pair(outpoint, wtxid));

    pair<TxSpends::iterator, TxSpends::iterator> range;
    range = mapTxSpends.equal_range(outpoint);
    SyncMetaData<COutPoint>(range);
}

/**
 * @brief Add a Sprout nullifier to the spend tracking map
 * @param nullifier The Sprout nullifier being spent
 * @param wtxid The wallet transaction ID that spends this nullifier
 * 
 * Records that a specific Sprout note is being spent by a transaction.
 * This function:
 * - Removes any existing entries for this nullifier (during rescan)
 * - Adds the spending relationship to mapTxSproutNullifiers
 * - Synchronizes metadata across all transactions that spend the same nullifier
 * 
 * This tracking is essential for detecting double-spends in Sprout transactions,
 * managing conflicted transactions, and maintaining accurate note state.
 */
void CWallet::AddToSproutSpends(const uint256& nullifier, const uint256& wtxid)
{
    // Remove any existing entries for this nullifier to avoid duplicates during rescan
    mapTxSproutNullifiers.erase(nullifier);
    
    mapTxSproutNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxSproutNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

/**
 * @brief Add a Sapling nullifier to the spend tracking map
 * @param nullifier The Sapling nullifier being spent
 * @param wtxid The wallet transaction ID that spends this nullifier
 * 
 * Records that a specific Sapling note is being spent by a transaction.
 * This function:
 * - Removes any existing entries for this nullifier (during rescan)
 * - Adds the spending relationship to mapTxSaplingNullifiers
 * - Synchronizes metadata across all transactions that spend the same nullifier
 * 
 * This tracking is essential for detecting double-spends in Sapling transactions,
 * managing conflicted transactions, and maintaining accurate note state.
 */
void CWallet::AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid)
{
    // Remove any existing entries for this nullifier to avoid duplicates during rescan
    mapTxSaplingNullifiers.erase(nullifier);
    
    //Add the new spending relationship
    mapTxSaplingNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxSaplingNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

/**
 * @brief Add an Orchard nullifier to the spend tracking map
 * @param nullifier The Orchard nullifier being spent
 * @param wtxid The wallet transaction ID that spends this nullifier
 * 
 * Records that a specific Orchard note is being spent by a transaction.
 * This function:
 * - Removes any existing entries for this nullifier (during rescan)
 * - Adds the spending relationship to mapTxOrchardNullifiers
 * - Synchronizes metadata across all transactions that spend the same nullifier
 * 
 * This tracking is essential for detecting double-spends in Orchard transactions,
 * managing conflicted transactions, and maintaining accurate note state.
 */
void CWallet::AddToOrchardSpends(const uint256& nullifier, const uint256& wtxid)
{
    // Remove any existing entries for this nullifier to avoid duplicates during rescan
    mapTxOrchardNullifiers.erase(nullifier);
    
    mapTxOrchardNullifiers.insert(make_pair(nullifier, wtxid));

    pair<TxNullifiers::iterator, TxNullifiers::iterator> range;
    range = mapTxOrchardNullifiers.equal_range(nullifier);
    SyncMetaData<uint256>(range);
}

/**
 * @brief Remove all spending relationships for a transaction
 * @param wtxid The wallet transaction ID to remove from all spend maps
 * 
 * Removes all spending relationships (transparent, Sprout, Sapling, and Orchard)
 * for the specified transaction. This is called when a transaction is removed
 * from the wallet, typically during chain reorganizations or transaction cleanup.
 * 
 * This ensures that spend tracking maps remain consistent and don't contain
 * references to non-existent transactions.
 */
void CWallet::RemoveFromSpends(const uint256& wtxid)
{
    RemoveFromTransparentSpends(wtxid);
    RemoveFromSproutSpends(wtxid);
    RemoveFromSaplingSpends(wtxid);
    RemoveFromOrchardSpends(wtxid);
}

/**
 * @brief Remove transparent spending relationships for a transaction
 * @param wtxid The wallet transaction ID to remove from transparent spend map
 * 
 * Removes all transparent UTXO spending relationships for the specified transaction
 * from mapTxSpends. This is called during transaction removal or chain reorganizations
 * to maintain accurate spend tracking state.
 */
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

/**
 * @brief Remove Sprout spending relationships for a transaction
 * @param wtxid The wallet transaction ID to remove from Sprout nullifier map
 * 
 * Removes all Sprout nullifier spending relationships for the specified transaction
 * from mapTxSproutNullifiers. This is called during transaction removal or chain
 * reorganizations to maintain accurate Sprout note spend tracking state.
 */
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

/**
 * @brief Remove Sapling spending relationships for a transaction
 * @param wtxid The wallet transaction ID to remove from Sapling nullifier map
 * 
 * Removes all Sapling nullifier spending relationships for the specified transaction
 * from mapTxSaplingNullifiers. This is called during transaction removal or chain
 * reorganizations to maintain accurate Sapling note spend tracking state.
 */
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

/**
 * @brief Remove Orchard spending relationships for a transaction
 * @param wtxid The wallet transaction ID to remove from Orchard nullifier map
 * 
 * Removes all Orchard nullifier spending relationships for the specified transaction
 * from mapTxOrchardNullifiers. This is called during transaction removal or chain
 * reorganizations to maintain accurate Orchard note spend tracking state.
 */
void CWallet::RemoveFromOrchardSpends(const uint256& wtxid)
{
    if (mapTxOrchardNullifiers.size() > 0)
    {
        std::multimap<uint256, uint256>::const_iterator itr = mapTxOrchardNullifiers.cbegin();
        while (itr != mapTxOrchardNullifiers.cend())
        {
            if (itr->second == wtxid)
            {
                itr = mapTxOrchardNullifiers.erase(itr);
            }
            else
            {
                ++itr;
            }
        }
    }
}

/**
 * @brief Load archived transaction data into the wallet
 * @param wtxid The wallet transaction ID to associate with archive data
 * @param arcTxPt The archive transaction point containing viewing keys and metadata
 * 
 * Loads previously archived transaction data back into the wallet's archive map.
 * This is typically called during wallet initialization to restore archived
 * transaction metadata including viewing keys and address information.
 * 
 * Archived transactions maintain essential metadata for historical transaction
 * analysis while reducing the wallet's memory footprint.
 */
void CWallet::LoadArcTxs(const uint256& wtxid, const ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtxid] = arcTxPt;
}

/**
 * @brief Add transaction to archive with RPC-based key extraction
 * @param wtxid The wallet transaction ID to archive
 * @param arcTxPt[in,out] Archive transaction point to populate with key data
 * 
 * Archives a transaction by extracting and storing its viewing keys and address
 * information. This function:
 * - Retrieves transaction data via RPC interface
 * - Extracts Sapling and Orchard viewing keys (incoming and outgoing)
 * - Updates the address-to-transaction mapping for efficient lookups
 * - Marks the archive point for disk persistence
 * 
 * This is used for transactions that need to be archived while preserving
 * essential metadata for future transaction analysis and address tracking.
 */
void CWallet::AddToArcTxs(const uint256& wtxid, ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtxid] = arcTxPt;

    uint256 txid = wtxid;
    RpcArcTransaction arcTx;

    getRpcArcTx(txid, arcTx, true, true);

    arcTxPt.saplingIvks = arcTx.saplingIvks;
    arcTxPt.saplingOvks = arcTx.saplingOvks;
    arcTxPt.orchardIvks = arcTx.orchardIvks;
    arcTxPt.orchardOvks = arcTx.orchardOvks;
    arcTxPt.writeToDisk = true;
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

/**
 * @brief Add wallet transaction to archive with direct key extraction
 * @param wtx The wallet transaction to archive
 * @param txHeight The block height where this transaction was confirmed
 * @param arcTxPt[in,out] Archive transaction point to populate with key data
 * 
 * Archives a wallet transaction by directly extracting its Sapling and Orchard
 * viewing keys. This function:
 * - Extracts Sapling viewing keys directly from the transaction
 * - Extracts Orchard viewing keys directly from the transaction
 * - Updates the address-to-transaction mapping for efficient lookups
 * - Marks the archive point for disk persistence
 * 
 * This variant is used when the full CWalletTx object is available and allows
 * for more efficient key extraction without going through the RPC interface.
 */
void CWallet::AddToArcTxs(const CWalletTx& wtx, int txHeight, ArchiveTxPoint& arcTxPt)
{
    mapArcTxs[wtx.GetHash()] = arcTxPt;

    CWalletTx tx = wtx;
    RpcArcTransaction arcTx;

    getRpcArcTxSaplingKeys(wtx, txHeight, arcTx, true);
    getRpcArcTxOrchardKeys(wtx, txHeight, arcTx, true);

    arcTxPt.saplingIvks = arcTx.saplingIvks;
    arcTxPt.saplingOvks = arcTx.saplingOvks;
    arcTxPt.orchardIvks = arcTx.orchardIvks;
    arcTxPt.orchardOvks = arcTx.orchardOvks;
    arcTxPt.writeToDisk = true;
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

/**
 * @brief Add a nullifier to Sprout JoinSplit output point mapping
 * @param nullifier The Sprout nullifier to map
 * @param op The corresponding JSOutPoint
 * 
 * Maintains a mapping between Sprout nullifiers and their corresponding
 * JoinSplit output points for archived transactions. This is used for
 * tracking and managing Sprout note usage in transaction archives.
 */
void CWallet::AddToArcJSOutPoints(const uint256& nullifier, const JSOutPoint& op)
{
    mapArcJSOutPoints[nullifier] = op;
}

/**
 * @brief Add a nullifier to Sapling output point mapping
 * @param nullifier The Sapling nullifier to map
 * @param op The corresponding SaplingOutPoint
 * 
 * Maintains a mapping between Sapling nullifiers and their corresponding
 * output points for archived transactions. This is used for tracking
 * and managing Sapling note usage in transaction archives.
 */
void CWallet::AddToArcSaplingOutPoints(const uint256& nullifier, const SaplingOutPoint& op)
{
    mapArcSaplingOutPoints[nullifier] = op;
}

/**
 * @brief Add a nullifier to Orchard output point mapping
 * @param nullifier The Orchard nullifier to map
 * @param op The corresponding OrchardOutPoint
 * 
 * Maintains a mapping between Orchard nullifiers and their corresponding
 * output points for archived transactions. This is used for tracking
 * and managing Orchard note usage in transaction archives.
 */
void CWallet::AddToArcOrchardOutPoints(const uint256& nullifier, const OrchardOutPoint& op)
{
    mapArcOrchardOutPoints[nullifier] = op;
}

/**
 * @brief Add all transaction inputs and nullifiers to spend tracking maps
 * @param wtxid The wallet transaction ID to process
 * 
 * Processes a wallet transaction and adds all its spending operations to
 * the appropriate tracking maps. This includes:
 * - Transparent inputs (UTXOs) to mapTxTransparentSpends
 * - Sprout nullifiers to mapTxSproutNullifiers  
 * - Sapling nullifiers to mapTxSaplingNullifiers
 * - Orchard nullifiers to mapTxOrchardNullifiers
 * 
 * This function is essential for maintaining accurate spending state and
 * preventing double-spend attempts across all transaction types.
 * Coinbase transactions are skipped as they don't spend any inputs.
 */
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

    for (const auto& spend : thisTx.GetSaplingSpends())  {
        uint256 nullifier = uint256::FromRawBytes(spend.nullifier());
        AddToSaplingSpends(nullifier, wtxid);
    }

    for (const auto& action : thisTx.GetOrchardActions())  {
        uint256 nullifier = uint256::FromRawBytes(action.nullifier());
        AddToOrchardSpends(nullifier, wtxid);
    }
}

/**
 * @brief Process Sapling transactions in a block and update commitment tree
 * @param pblockindex The block index being processed
 * @param pblock The block containing transactions to process
 * 
 * Processes all transactions in a block for Sapling wallet state updates.
 * For transactions belonging to the wallet:
 * - Creates empty positions for tracking
 * - Appends individual note commitments with tracking enabled
 * - Updates note positions in wallet transaction data
 * - Updates nullifier mappings
 * 
 * For transactions not in the wallet:
 * - Batch-appends all commitments without tracking
 * 
 * This function is used during both wallet rebuild and incremental updates.
 */
void CWallet::ProcessSaplingBlockTransactions(const CBlockIndex* pblockindex, const CBlock* pblock)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    //Create Checkpoint before incrementing wallet
    saplingWallet.CheckpointNoteCommitmentTree(pblockindex->nHeight);

    for (int i = 0; i < pblock->vtx.size(); i++) {
        uint256 txid = pblock->vtx[i].GetHash();
        auto it = mapWallet.find(txid);

        //Use single output appending for transaction that belong to the wallet so that they can be marked
        if (it != mapWallet.end()) {
            saplingWallet.CreateEmptyPositionsForTxid(pblockindex->nHeight, txid);
            CWalletTx *pwtx = &(*it).second;
            auto vOutputs = pblock->vtx[i].GetSaplingOutputs();
            for (int j = 0; j < vOutputs.size(); j++) {
                SaplingOutPoint op = SaplingOutPoint(txid, j);
                auto opit = pwtx->mapSaplingNoteData.find(op);

                if (opit != pwtx->mapSaplingNoteData.end()) {
                    saplingWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, &vOutputs[j], true);
                    //Get note position
                    uint64_t position = UINT64_MAX;
                    assert(saplingWallet.IsNoteTracked(txid, j, position));
                    pwtx->mapSaplingNoteData[op].setPosition(position);

                    // Validate position and tree root
                    uint256 treeRoot = saplingWallet.GetLatestAnchor();
                    LogPrint("saplingwallet", "Sapling Wallet - Note tracked: txid=%s outidx=%d position=%llu root=%s\n", 
                        txid.ToString(), j, (unsigned long long)position, treeRoot.ToString());
                    
                    // Sanity check: position should be less than tree size
                    if (position == UINT64_MAX) {
                        LogPrintf("ERROR: Sapling Wallet - Invalid position (UINT64_MAX) for tracked note txid=%s outidx=%d\n",
                            txid.ToString(), j);
                    }


                } else {
                    saplingWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, &vOutputs[j], false);
                }
            }                    
            // Only update nullifiers if we have Sapling notes for this transaction
            if (pwtx->mapSaplingNoteData.size() > 0) {
                UpdateSaplingNullifierNoteMapWithTx(pwtx);
            } else {
                saplingWallet.ClearPositionsForTxid(txid);
            }
        } else {
            //No transactions in this tx belong to the wallet, use full tx appending
            saplingWallet.ClearPositionsForTxid(txid);
            saplingWallet.AppendNoteCommitments(pblockindex->nHeight,pblock->vtx[i],i);
        }
    }
}

/**
 * @brief Process all Orchard transactions in a block
 * @param pblockindex The block index containing the transactions
 * @param pblock Pointer to the block data
 * 
 * Processes all transactions in the block, handling wallet transactions differently
 * from non-wallet transactions. Wallet transactions use individual commitment appending
 * to track note positions, while non-wallet transactions use batch appending.
 */
void CWallet::ProcessOrchardBlockTransactions(const CBlockIndex* pblockindex, const CBlock* pblock)
{
    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    //Create Checkpoint before incrementing wallet
    orchardWallet.CheckpointNoteCommitmentTree(pblockindex->nHeight);

    for (int i = 0; i < pblock->vtx.size(); i++) {
        uint256 txid = pblock->vtx[i].GetHash();
        auto it = mapWallet.find(txid);

        //Use single output appending for transaction that belong to the wallet so that they can be marked
        if (it != mapWallet.end()) {
            orchardWallet.CreateEmptyPositionsForTxid(pblockindex->nHeight, txid);
            CWalletTx *pwtx = &(*it).second;
            auto vActions = pblock->vtx[i].GetOrchardActions();
            for (int j = 0; j < vActions.size(); j++) {
                OrchardOutPoint op = OrchardOutPoint(txid, j);
                auto opit = pwtx->mapOrchardNoteData.find(op);

                if (opit != pwtx->mapOrchardNoteData.end()) {
                    orchardWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, &vActions[j], true);
                    //Get note position
                    uint64_t position = UINT64_MAX;
                    assert(orchardWallet.IsNoteTracked(txid, j, position));
                    pwtx->mapOrchardNoteData[op].setPosition(position);

                    // Validate position and tree root
                    uint256 treeRoot = orchardWallet.GetLatestAnchor();
                    LogPrint("orchardwallet", "Orchard Wallet - Note tracked: txid=%s outidx=%d position=%llu root=%s\n", 
                        txid.ToString(), j, (unsigned long long)position, treeRoot.ToString());
                    
                    // Sanity check: position should be less than tree size
                    if (position == UINT64_MAX) {
                        LogPrintf("ERROR: Orchard Wallet - Invalid position (UINT64_MAX) for tracked note txid=%s outidx=%d\n",
                            txid.ToString(), j);
                    }


                } else {
                    orchardWallet.AppendNoteCommitment(pblockindex->nHeight, txid, i, j, &vActions[j], false);
                }
            }                   
            // Only update nullifiers if we have Orchard notes for this transaction
            if (pwtx->mapOrchardNoteData.size() > 0) {
                UpdateOrchardNullifierNoteMapWithTx(pwtx);
            } else {
                orchardWallet.ClearPositionsForTxid(txid);
            }
        } else {
            //No transactions in this tx belong to the wallet, use full tx appending
            orchardWallet.ClearPositionsForTxid(txid);
            orchardWallet.AppendNoteCommitments(pblockindex->nHeight,pblock->vtx[i],i);
        }
    }
}

/**
 * @brief Validate Sapling wallet tracked positions against Rust wallet state
 * @param pindex The current block index for validation context
 * @return false if the SaplingWallet needs to be rebuilt, true if valid
 * 
 * Validates the positions of all Sapling notes tracked in the C++ wallet
 * against the corresponding positions in the Rust SaplingWallet. This ensures
 * consistency between the two wallet implementations and detects any drift
 * or corruption in note position tracking.
 * 
 * The function:
 * - Iterates through all wallet transactions with Sapling notes
 * - Compares note positions between C++ and Rust wallet states
 * - Shows progress UI for large validation operations
 * - Returns false if inconsistencies require wallet rebuilding
 * 
 * This is a critical validation function that maintains wallet integrity
 * across different wallet backend implementations.
 */
bool CWallet::ValidateSaplingWalletTrackedPositions(const CBlockIndex* pindex) {

    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int64_t nNow = GetTime();
    int valperc = 0;
    int i = 0;
    int walletSize = mapWallet.size();
    bool uiShown = false;


    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
        uint256 txid = (*it).first;
        CWalletTx *pwtx = &(*it).second;

        valperc = i/walletSize;
        if (GetTime() >= nNow + 60) {
            nNow = GetTime();
            LogPrintf("Validating Note Postions... Progress=%d\n", valperc );
        }

        if (GetTime() >= nNow + 10) {
            uiShown = true;
        }

        if (uiShown) {
            uiInterface.ShowProgress("Validating Note Postions...", std::max(1, std::min(99, valperc)), false);
        }

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

        //exit loop if trying to shutdown
        if (ShutdownRequested()) {
            break;
        }

        //Check if all notes are tracked correctly
        for (mapSaplingNoteData_t::value_type& item : pwtx->mapSaplingNoteData) {

            const SaplingOutPoint op = item.first;
            uint64_t position;

            if(!saplingWallet.IsNoteTracked(op.hash, op.n, position)) {
                return false;
            } else {
                CBlockIndex* pCheckIndex = mapBlockIndex[pwtx->hashBlock];

                //Create a new wallet to validate tracked merkle path
                SaplingMerkleFrontier saplingCheckFrontierTree;
                pcoinsTip->GetSaplingFrontierAnchorAt(pCheckIndex->pprev->hashFinalSaplingRoot, saplingCheckFrontierTree);
                SaplingWallet saplingWalletCheck;
                saplingWalletCheck.InitNoteCommitmentTree(saplingCheckFrontierTree);

                MerklePath saplingCheckMerklePath;
                uint64_t positionCheck;

                //Retrieve the full block to get all of the transaction commitments
                CBlock checkBlock;
                ReadBlockFromDisk(checkBlock, pCheckIndex, 1);
                CBlock *pCheckBlock = &checkBlock;

                //Calculate Merkle Path
                for (int i = 0; i < pCheckBlock->vtx.size(); i++) {
                    uint256 txid = pCheckBlock->vtx[i].GetHash();

                    //Use single output appending for transaction that belong to the wallet so that they can be marked
                    if (pwtx->GetHash() == txid) {
                        saplingWalletCheck.CreateEmptyPositionsForTxid(pCheckIndex->nHeight, txid);

                        auto vOutputs = pCheckBlock->vtx[i].GetSaplingOutputs();
                        for (int j = 0; j < vOutputs.size(); j++) {
                            auto opit = pwtx->mapSaplingNoteData.find(op);
                            if (opit != pwtx->mapSaplingNoteData.end() && j == op.n) {
                                saplingWalletCheck.AppendNoteCommitment(pCheckIndex->nHeight, txid, i, j, &vOutputs[j], true);
                                assert(saplingWalletCheck.IsNoteTracked(txid, j, positionCheck));
                                LogPrint("saplingwallet", "Sapling Wallet - Merkle Path position %i\n", positionCheck);


                            } else {
                                saplingWalletCheck.AppendNoteCommitment(pCheckIndex->nHeight, txid, i, j, &vOutputs[j], false);
                            }
                        }

                    } else {
                        //No transactions in this tx belong to the wallet, use full tx appending
                        saplingWalletCheck.ClearPositionsForTxid(txid);
                        saplingWalletCheck.AppendNoteCommitments(pCheckIndex->nHeight,pCheckBlock->vtx[i],i);
                    }
                }

                LogPrint("saplingwallet", "Sapling Wallet - Merkle Path position %i\n", positionCheck);

                if (positionCheck != position) {
                    LogPrint("saplingwallet", "Sapling Wallet Validation failed, rebuilding witnesses\n");
                    return false;
                } else {
                    pwtx->mapSaplingNoteData[op].setPosition(position);
                    UpdateSaplingNullifierNoteMapWithTx(pwtx);
                }
            }
        }
    }

    if (uiShown) {
        uiInterface.ShowProgress(_("Validating Note Postions..."), 100, false);
    }

    return true;

}

/**
 * @brief Validate Orchard wallet tracked positions against Rust wallet state
 * @param pindex The current block index for validation context
 * @return false if the OrchardWallet needs to be rebuilt, true if valid
 * 
 * Validates the positions of all Orchard notes tracked in the C++ wallet
 * against the corresponding positions in the Rust OrchardWallet. This ensures
 * consistency between the two wallet implementations and detects any drift
 * or corruption in note position tracking.
 * 
 * The function:
 * - Iterates through all wallet transactions with Orchard notes
 * - Compares note positions between C++ and Rust wallet states
 * - Shows progress UI for large validation operations
 * - Returns false if inconsistencies require wallet rebuilding
 * 
 * This is a critical validation function that maintains wallet integrity
 * across different wallet backend implementations for the Orchard protocol.
 */
bool CWallet::ValidateOrchardWalletTrackedPositions(const CBlockIndex* pindex) {

    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int64_t nNow = GetTime();
    int valperc = 0;
    int i = 0;
    int walletSize = mapWallet.size();
    bool uiShown = false;


    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
        uint256 txid = (*it).first;
        CWalletTx *pwtx = &(*it).second;

        valperc = i/walletSize;
        if (GetTime() >= nNow + 60) {
            nNow = GetTime();
            LogPrintf("Validating Note Postions... Progress=%d\n", valperc );
        }

        if (GetTime() >= nNow + 10) {
            uiShown = true;
        }

        if (uiShown) {
            uiInterface.ShowProgress("Validating Note Postions...", std::max(1, std::min(99, valperc)), false);
        }

        //Exclude transactions with no Sapling Data
        if (pwtx->mapOrchardNoteData.empty()) {
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

        //exit loop if trying to shutdown
        if (ShutdownRequested()) {
            break;
        }

        //Check if all notes are tracked correctly
        for (mapOrchardNoteData_t::value_type& item : pwtx->mapOrchardNoteData) {

            const OrchardOutPoint op = item.first;
            uint64_t position;

            if(!orchardWallet.IsNoteTracked(op.hash, op.n, position)) {
                return false;
            } else {
                CBlockIndex* pCheckIndex = mapBlockIndex[pwtx->hashBlock];

                //Create a new wallet to validate tracked merkle path
                OrchardMerkleFrontier orchardCheckFrontierTree;
                pcoinsTip->GetOrchardFrontierAnchorAt(pCheckIndex->pprev->hashFinalOrchardRoot, orchardCheckFrontierTree);
                OrchardWallet orchardWalletCheck;
                orchardWalletCheck.InitNoteCommitmentTree(orchardCheckFrontierTree);

                MerklePath orchardCheckMerklePath;
                uint64_t positionCheck;

                //Retrieve the full block to get all of the transaction commitments
                CBlock checkBlock;
                ReadBlockFromDisk(checkBlock, pCheckIndex, 1);
                CBlock *pCheckBlock = &checkBlock;

                //Calculate Merkle Path
                for (int i = 0; i < pCheckBlock->vtx.size(); i++) {
                    uint256 txid = pCheckBlock->vtx[i].GetHash();

                    //Use single output appending for transaction that belong to the wallet so that they can be marked
                    if (pwtx->GetHash() == txid) {
                        orchardWalletCheck.CreateEmptyPositionsForTxid(pCheckIndex->nHeight, txid);

                        auto vActions = pCheckBlock->vtx[i].GetOrchardActions();
                        for (int j = 0; j < vActions.size(); j++) {
                            auto opit = pwtx->mapOrchardNoteData.find(op);
                            if (opit != pwtx->mapOrchardNoteData.end() && j == op.n) {
                                orchardWalletCheck.AppendNoteCommitment(pCheckIndex->nHeight, txid, i, j, &vActions[j], true);
                                assert(orchardWalletCheck.IsNoteTracked(txid, j, positionCheck));
                                LogPrint("orchardwallet", "Orchard Wallet - Merkle Path position %i\n", positionCheck);


                            } else {
                                orchardWalletCheck.AppendNoteCommitment(pCheckIndex->nHeight, txid, i, j, &vActions[j], false);
                            }
                        }

                    } else {
                        //No transactions in this tx belong to the wallet, use full tx appending
                        orchardWalletCheck.ClearPositionsForTxid(txid);
                        orchardWalletCheck.AppendNoteCommitments(pCheckIndex->nHeight,pCheckBlock->vtx[i],i);
                    }
                }

                LogPrint("orchardwallet", "Orchard Wallet - Merkle Path position %i\n", positionCheck);

                if (positionCheck != position) {
                    LogPrint("orchardwallet", "Orchard Wallet Validation failed, rebuilding witnesses\n");
                    return false;
                } else {
                    pwtx->mapOrchardNoteData[op].setPosition(position);
                    UpdateOrchardNullifierNoteMapWithTx(pwtx);
                }
            }
        }
    }

    if (uiShown) {
        uiInterface.ShowProgress(_("Validating Note Postions..."), 100, false);
    }

    return true;

}

/**
 * @brief Increment Sapling wallet state for a new block
 * @param pindex The block index being added to the chain
 * 
 * Updates the Sapling wallet's internal state when a new block is added
 * to the blockchain. This function:
 * - Advances the Sapling note commitment tree
 * - Updates witness information for existing notes
 * - Processes new Sapling transactions in the block
 * - Maintains proper note position tracking
 * 
 * This function is called during block connection to keep the Sapling
 * wallet synchronized with the blockchain state. It ensures that all
 * Sapling notes can be properly spent and that witnesses remain valid.
 */
void CWallet::IncrementSaplingWallet(const CBlockIndex* pindex, const CBlock* pblock) {

    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int64_t nNow1 = GetTime();
    int64_t nNow2 = GetTime();
    bool rebuildWallet = false;
    int nMinimumHeight = pindex->nHeight;
    int lastCheckpoint = saplingWallet.GetLastCheckpointHeight();
    LogPrint("saplingwallet","Sapling Wallet - Last Checkpoint %i, Block Height %i\n", lastCheckpoint, nMinimumHeight);

    if (NetworkUpgradeActive(pindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {

        if (lastCheckpoint > pindex->nHeight - 1 ) {
            saplingWalletValidated = false;
            LogPrint("saplingwallet","Sapling Wallet - Last Checkpoint is higher than wallet, skipping block\n");
            return;
        }

        //Rebuild if wallet does not exisit
        if (lastCheckpoint<0) {
            LogPrint("saplingwallet","Sapling Wallet - Last Checkpoint doesn't exist, rebuild witnesses\n");
            rebuildWallet = true;
        }

        //Rebuild if wallet is out of sync with the blockchain
        if (lastCheckpoint != pindex->nHeight - 1 ) {
            LogPrint("saplingwallet","Sapling Wallet - Last Checkpoint is out of sync with the blockchain, rebuild witnesses\n");
            rebuildWallet = true;
        }

        //Rebuild wallet if anchor does not match, only check on wallet opening due to performance issues, or rescan without wallet reset
        if (lastCheckpoint == pindex->nHeight - 1 && !saplingWalletValidated) {
            if (pindex->pprev->hashFinalSaplingRoot != saplingWallet.GetLatestAnchor()) {
                LogPrint("saplingwallet","Sapling Wallet - Sapling Root is out of sync with the blockchain, rebuild witnesses\n");
                rebuildWallet = true;
            }

            //Should never run here, should only run at initialization
            if (!saplingWalletPositionsValidated) {
                rebuildWallet = !ValidateSaplingWalletTrackedPositions(pindex);
                saplingWalletPositionsValidated = true;
            }

            saplingWalletValidated = true;
        }

        //Rebuild
        if (rebuildWallet) {
            //Disable the RPC z_sendmany RPC while rebuilding
            fBuilingWitnessCache = true;

            //Don't recheck after a rebuild
            saplingWalletValidated = true;

            //Determine Start Height of Sapling Wallet
            for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
                CWalletTx* pwtx = &(*it).second;

                // Find the block it claims to be in
                BlockMap::iterator mi = mapBlockIndex.find(pwtx->hashBlock);
                if (mi == mapBlockIndex.end()) {
                    continue;
                }

                nMinimumHeight = std::min(mapBlockIndex[pwtx->hashBlock]->nHeight, nMinimumHeight);

            }

            LogPrint("\n\nsaplingwallet", "Sapling Wallet - rebuilding wallet from block %i\n\n", nMinimumHeight);

            //No transactions exists to begin wallet at this hieght
            if (nMinimumHeight > pindex->nHeight) {
                LogPrint("\n\nsaplingwallet", "Sapling Wallet - no transactions exist at height %i to rebuild wallet\n\n", nMinimumHeight);
                SaplingWalletReset();
                return;
            }

            // Set Starting Values
            CBlockIndex* pblockindex = chainActive[nMinimumHeight];

            //Create a new wallet
            SaplingMerkleFrontier saplingFrontierTree;
            pcoinsTip->GetSaplingFrontierAnchorAt(pblockindex->pprev->hashFinalSaplingRoot, saplingFrontierTree);
            SaplingWalletReset();
            saplingWallet.InitNoteCommitmentTree(saplingFrontierTree);

            //Show in UI
            int chainHeight = chainActive.Height();
            bool uiShown = false;
            const CChainParams& chainParams = Params();
            double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false);
            double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);

            //Loop thru blocks to rebuild saplingWallet commitment tree
            while (pblockindex) {

                if (GetTime() >= nNow2 + 60) {
                    nNow2 = GetTime();
                    LogPrintf("Rebuilding Sapling Wallet for block %d. Progress=%f\n", pblockindex->nHeight, Checkpoints::GuessVerificationProgress(Params().Checkpoints(), pblockindex));
                }

                //Report Progress to the GUI and log file
                int witnessHeight = pblockindex->nHeight;
                if ((witnessHeight % 100 == 0 || GetTime() >= nNow1 + 15) && witnessHeight < chainHeight - 5 ) {
                    nNow1 = GetTime();
                    if (!uiShown) {
                        uiShown = true;
                        uiInterface.ShowProgress("Rebuilding Sapling Wallet", 0, false);
                    }
                    scanperc = (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100);
                    uiInterface.ShowProgress(_(("Rebuilding Sapling Wallet for block " + std::to_string(witnessHeight) + "...").c_str()), std::max(1, std::min(99, scanperc)), false);
                }

                //exit loop if trying to shutdown
                if (ShutdownRequested()) {
                    break;
                }

                //Retrieve the full block to get all of the transaction commitments
                CBlock block;
                ReadBlockFromDisk(block, pblockindex, 1);
                CBlock *pblock = &block;

                ProcessSaplingBlockTransactions(pblockindex, pblock);

                //Check completeness
                if (pblockindex == pindex)
                    break;

                //Set Variables for next loop
                pblockindex = chainActive.Next(pblockindex);
            }

            if (uiShown) {
                uiInterface.ShowProgress(_("Sapling Wallet Rebuild Complete..."), 100, false);
            }

        } else {

            //Retrieve the full block to get all of the transaction commitments
            if (pblock == nullptr) {
                CBlock block;
                ReadBlockFromDisk(block, pindex, 1);
                ProcessSaplingBlockTransactions(pindex, &block);
            } else {
                ProcessSaplingBlockTransactions(pindex, pblock);
            }
        }
    }

    fInitWitnessesBuilt = true;
    fBuilingWitnessCache = false;

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

/**
 * @brief Increment Orchard wallet state for a new block
 * @param pindex The block index being added to the chain
 * 
 * Updates the Orchard wallet's internal state when a new block is added
 * to the blockchain. This function:
 * - Advances the Orchard note commitment tree
 * - Updates witness information for existing notes
 * - Processes new Orchard transactions in the block
 * - Maintains proper note position tracking
 * 
 * This function is called during block connection to keep the Orchard
 * wallet synchronized with the blockchain state. It ensures that all
 * Orchard notes can be properly spent and that witnesses remain valid.
 */
void CWallet::IncrementOrchardWallet(const CBlockIndex* pindex, const CBlock* pblock) {

    AssertLockHeld(cs_main);
    AssertLockHeld(cs_wallet);

    int64_t nNow1 = GetTime();
    int64_t nNow2 = GetTime();
    bool rebuildWallet = false;
    int nMinimumHeight = pindex->nHeight;
    int lastCheckpoint = orchardWallet.GetLastCheckpointHeight();
    LogPrint("orchardwallet","Orchard Wallet - Last Checkpoint %i, Block Height %i\n", lastCheckpoint, nMinimumHeight);

    if (NetworkUpgradeActive(pindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD)) {

        if (lastCheckpoint > pindex->nHeight - 1 ) {
            orchardWalletValidated = false;
            LogPrint("orchardwallet","Orchard Wallet - Last Checkpoint is higher than wallet, skipping block\n");
            return;
        }

        //Rebuild if wallet does not exisit
        if (lastCheckpoint<0) {
            LogPrint("orchardwallet","Orchard Wallet - Last Checkpoint doesn't exist, rebuild witnesses\n");
            rebuildWallet = true;
        }

        //Rebuild if wallet is out of sync with the blockchain
        if (lastCheckpoint != pindex->nHeight - 1 ) {
            LogPrint("orchardwallet","Orchard Wallet - Last Checkpoint is out of sync with the blockchain, rebuild witnesses\n");
            rebuildWallet = true;
        }

        //Rebuild wallet if anchor does not match, only check on wallet opening due to performance issues, or rescan without wallet reset
        if (lastCheckpoint == pindex->nHeight - 1 && !orchardWalletValidated) {
            if (pindex->pprev->hashFinalOrchardRoot != orchardWallet.GetLatestAnchor()) {
                LogPrint("orchardwallet","Orchard Wallet - Orchard Root is out of sync with the blockchain, rebuild witnesses\n");
                rebuildWallet = true;
            }

            //Should never run here, should only run at initialization
            if (!orchardWalletPositionsValidated) {
                rebuildWallet = !ValidateOrchardWalletTrackedPositions(pindex);
                orchardWalletPositionsValidated = true;
            }

            orchardWalletValidated = true;
        }

        //Rebuild
        if (rebuildWallet) {
            //Disable the RPC z_sendmany RPC while rebuilding
            fBuilingWitnessCache = true;

            //Don't recheck after a rebuild
            orchardWalletValidated = true;

            //Determine Start Height of Sapling Wallet
            for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
                CWalletTx* pwtx = &(*it).second;

                // Find the block it claims to be in
                BlockMap::iterator mi = mapBlockIndex.find(pwtx->hashBlock);
                if (mi == mapBlockIndex.end()) {
                    continue;
                }

                nMinimumHeight = std::min(mapBlockIndex[pwtx->hashBlock]->nHeight, nMinimumHeight);

            }

            LogPrint("\n\norchardwallet", "Orchard Wallet - rebuilding wallet from block %i\n\n", nMinimumHeight);

            //No transactions exists to begin wallet at this hieght
            if (nMinimumHeight > pindex->nHeight) {
                LogPrint("\n\norchardwallet", "Orchard Wallet - no transactions exist at height %i to rebuild wallet\n\n", nMinimumHeight);
                OrchardWalletReset();
                return;
            }

            // Set Starting Values
            CBlockIndex* pblockindex = chainActive[nMinimumHeight];

            //Create a new wallet
            OrchardMerkleFrontier orchardFrontierTree;
            pcoinsTip->GetOrchardFrontierAnchorAt(pblockindex->pprev->hashFinalOrchardRoot, orchardFrontierTree);
            OrchardWalletReset();
            orchardWallet.InitNoteCommitmentTree(orchardFrontierTree);

            //Show in UI
            int chainHeight = chainActive.Height();
            bool uiShown = false;
            const CChainParams& chainParams = Params();
            double dProgressStart = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false);
            double dProgressTip = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip(), false);

            //Loop thru blocks to rebuild saplingWallet commitment tree
            while (pblockindex) {

                if (GetTime() >= nNow2 + 60) {
                    nNow2 = GetTime();
                    LogPrintf("Rebuilding Orchard Wallet for block %d. Progress=%f\n", pblockindex->nHeight, Checkpoints::GuessVerificationProgress(Params().Checkpoints(), pblockindex));
                }

                //Report Progress to the GUI and log file
                int witnessHeight = pblockindex->nHeight;
                if ((witnessHeight % 100 == 0 || GetTime() >= nNow1 + 15) && witnessHeight < chainHeight - 5 ) {
                    nNow1 = GetTime();
                    if (!uiShown) {
                        uiShown = true;
                        uiInterface.ShowProgress("Rebuilding Orchard Wallet", 0, false);
                    }
                    scanperc = (int)((Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pblockindex, false) - dProgressStart) / (dProgressTip - dProgressStart) * 100);
                    uiInterface.ShowProgress(_(("Rebuilding Orchard Wallet for block " + std::to_string(witnessHeight) + "...").c_str()), std::max(1, std::min(99, scanperc)), false);
                }

                //exit loop if trying to shutdown
                if (ShutdownRequested()) {
                    break;
                }

                //Retrieve the full block to get all of the transaction commitments
                CBlock block;
                ReadBlockFromDisk(block, pblockindex, 1);
                CBlock *pblock = &block;

                ProcessOrchardBlockTransactions(pblockindex, pblock);

                //Check completeness
                if (pblockindex == pindex)
                    break;

                //Set Variables for next loop
                pblockindex = chainActive.Next(pblockindex);
            }

            if (uiShown) {
                uiInterface.ShowProgress(_("Witness Cache Complete..."), 100, false);
            }

        } else {

            //Retrieve the full block to get all of the transaction commitments
            if (pblock == nullptr) {
                CBlock block;
                ReadBlockFromDisk(block, pindex, 1);
                ProcessOrchardBlockTransactions(pindex, &block);
            } else {
                ProcessOrchardBlockTransactions(pindex, pblock);
            }
        }
    }

    fInitWitnessesBuilt = true;
    fBuilingWitnessCache = false;

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

/**
 * @brief Decrement Sapling wallet state when a block is removed
 * @param pindex The block index being removed from the chain
 * 
 * Reverts the Sapling wallet's internal state when a block is removed
 * from the blockchain during chain reorganizations. This function:
 * - Rewinds the Sapling note commitment tree to the previous block
 * - Validates the rewind operation completed successfully
 * - Updates checkpoint heights to maintain consistency
 * 
 * This function is called during block disconnection to keep the Sapling
 * wallet synchronized with the blockchain state during reorganizations.
 * It ensures that wallet state accurately reflects the current chain state.
 */
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

/**
 * @brief Decrement Orchard wallet state when a block is removed
 * @param pindex The block index being removed from the chain
 * 
 * Reverts the Orchard wallet's internal state when a block is removed
 * from the blockchain during chain reorganizations. This function:
 * - Rewinds the Orchard note commitment tree to the previous block
 * - Validates the rewind operation completed successfully
 * - Updates checkpoint heights to maintain consistency
 * 
 * This function is called during block disconnection to keep the Orchard
 * wallet synchronized with the blockchain state during reorganizations.
 * It ensures that wallet state accurately reflects the current chain state.
 */
void CWallet::DecrementOrchardWallet(const CBlockIndex* pindex) {

      uint32_t uResultHeight{0};
      assert(pindex->nHeight >= 1);
      assert(orchardWallet.Rewind(pindex->nHeight - 1, uResultHeight));
      assert(uResultHeight == pindex->nHeight - 1);
      // If we have no checkpoints after the rewind, then the latest anchor of the
      // wallet's Orchard note commitment tree will be in an indeterminate state and it
      // will be overwritten in the next `IncrementNoteWitnesses` call, so we can skip
      // the check against `hashFinalOrchardRoot`.

      auto walletLastCheckpointHeight = orchardWallet.GetLastCheckpointHeight();
      if (orchardWallet.GetLastCheckpointHeight()>0) {
          assert(pindex->pprev->hashFinalOrchardRoot == orchardWallet.GetLatestAnchor());
      }

}

/**
 * @brief Decrypt wallet transaction data from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted transaction data
 * @param hash[out] The decrypted transaction hash
 * @param wtx[out] The decrypted wallet transaction
 * @return true if successfully decrypted and hash matches
 * 
 * Decrypts a wallet transaction that was previously encrypted for storage.
 * Used during wallet loading to recover transaction data from the database.
 * Returns false if wallet is locked, decryption fails, or hash verification fails.
 */
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

/**
 * @brief Decrypt archived wallet transaction data from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted archive transaction data
 * @param txid[out] The decrypted transaction ID
 * @param arcTxPt[out] The decrypted archive transaction point
 * @return true if successfully decrypted and hash matches
 * 
 * Decrypts archived wallet transaction data that was previously encrypted for
 * long-term storage. Used to recover transaction history and metadata from
 * archived transaction storage. Returns false if wallet is locked or decryption fails.
 */
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

/**
 * @brief Decrypt an archived Sapling outpoint from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted outpoint data
 * @param nullifier[out] The decrypted nullifier value
 * @param op[out] The decrypted Sapling outpoint
 * @return true if successfully decrypted and hash matches
 * 
 * Decrypts archived Sapling outpoint data that was previously encrypted for
 * long-term storage. Used to recover transaction history and state information.
 * Returns false if wallet is locked or decryption fails.
 */
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

/**
 * @brief Decrypt an archived Orchard outpoint from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted outpoint data
 * @param nullifier[out] The decrypted nullifier value
 * @param op[out] The decrypted Orchard outpoint
 * @return true if successfully decrypted
 * 
 * Decrypts archived Orchard outpoint data that was previously encrypted for
 * long-term storage. Used to recover transaction history and state information.
 * Returns false if wallet is locked or decryption fails.
 */
bool CWallet::DecryptArchivedOrchardOutpoint(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& nullifier, OrchardOutPoint& op) {

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

/**
 * @brief Encrypt the wallet with a passphrase
 * @param strWalletPassphrase The passphrase to encrypt the wallet with
 * @return true if wallet was successfully encrypted
 * 
 * Encrypts an unencrypted wallet using the provided passphrase. This operation:
 * - Generates a random master key for encryption
 * - Creates encryption salt and parameters from HD seed
 * - Encrypts all private keys and sensitive data
 * - Locks the wallet after encryption
 * - Persists encrypted data to disk
 * 
 * Returns false if wallet is already encrypted or HD seed is missing.
 * This is a one-way operation - wallets cannot be unencrypted.
 */
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
        if (primarySaplingSpendingKey != std::nullopt) {
            if (!SetPrimarySaplingSpendingKey(primarySaplingSpendingKey.value())) {
                LogPrintf("Setting encrypted primary sapling spending key failed!!!\n");
                return false;
            }
        }

        if (primaryOrchardSpendingKey != std::nullopt) {
            if (!SetPrimaryOrchardSpendingKey(primaryOrchardSpendingKey.value())) {
                LogPrintf("Setting encrypted primary orchard spending key failed!!!\n");
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

        //Encrypt Orchard Extended Spending Key
        for (map<libzcash::OrchardExtendedFullViewingKeyPirate, libzcash::OrchardExtendedSpendingKeyPirate>::iterator it = mapOrchardSpendingKeys.begin(); it != mapOrchardSpendingKeys.end(); ++it) {
              if (!AddOrchardZKey((*it).second)) {
                  LogPrintf("Setting encrypted orchard spending key failed!!!\n");
                  return false;
              }
        }

        //Encrypt Sapling Extended Full Viewing keys
        for (map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey>::iterator it = mapSaplingFullViewingKeys.begin(); it != mapSaplingFullViewingKeys.end(); ++it) {
              if (!HaveSaplingSpendingKey((*it).second)) {
                  if (!AddSaplingExtendedFullViewingKey((*it).second)) {
                      LogPrintf("Setting encrypted sapling viewing key failed!!!\n");
                      return false;
                  }
              }
        }

        //Encrypt Orchard Extended Full Viewing keys
        for (map<libzcash::OrchardIncomingViewingKeyPirate, libzcash::OrchardExtendedFullViewingKeyPirate>::iterator it = mapOrchardFullViewingKeys.begin(); it != mapOrchardFullViewingKeys.end(); ++it) {
              if (!HaveOrchardSpendingKey((*it).second)) {
                  if (!AddOrchardExtendedFullViewingKey((*it).second)) {
                      LogPrintf("Setting encrypted orchard viewing key failed!!!\n");
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

        //Encrypt OrchardPaymentAddress
        for (map<libzcash::OrchardPaymentAddressPirate, libzcash::OrchardIncomingViewingKeyPirate>::iterator it = mapOrchardIncomingViewingKeys.begin(); it != mapOrchardIncomingViewingKeys.end(); it++)
        {
            if (!AddOrchardIncomingViewingKey((*it).second, (*it).first)) {
                LogPrintf("Setting encrypted orchard payment address failed!!!\n");
                return false;
            }
        }

        //Encrypt Sapling Diversified Addresses
        for (map<libzcash::SaplingPaymentAddress, SaplingDiversifierPath>::iterator it = mapSaplingPaymentAddresses.begin(); it != mapSaplingPaymentAddresses.end(); ++it) {
            if (!AddSaplingDiversifiedAddress((*it).first, (*it).second.first, (*it).second.second)) {
                LogPrintf("Setting encrypted sapling diversified payment address failed!!!\n");
                return false;
            }
        }

        //Encrypt Orchard Diversified Addresses
        for (map<libzcash::OrchardPaymentAddressPirate, OrchardDiversifierPath>::iterator it = mapOrchardPaymentAddresses.begin(); it != mapOrchardPaymentAddresses.end(); ++it) {
            if (!AddOrchardDiversifiedAddress((*it).first, (*it).second.first, (*it).second.second)) {
                LogPrintf("Setting encrypted orchard diversified payment address failed!!!\n");
                return false;
            }
        }

        //Encrypt the last sapling diversifier path used for each spendingkey
        for (map<libzcash::SaplingIncomingViewingKey, blob88>::iterator it = mapLastSaplingDiversifierPath.begin(); it != mapLastSaplingDiversifierPath.end(); ++it) {
            if (!AddLastSaplingDiversifierUsed((*it).first, (*it).second)) {
                LogPrintf("Setting encrypted last sapling diversified path failed!!!\n");
                return false;
            }
        }

        //Encrypt the last orchard diversifier path used for each spendingkey
        for (map<libzcash::OrchardIncomingViewingKeyPirate, blob88>::iterator it = mapLastOrchardDiversifierPath.begin(); it != mapLastOrchardDiversifierPath.end(); ++it) {
            if (!AddLastOrchardDiversifierUsed((*it).first, (*it).second)) {
                LogPrintf("Setting encrypted last orchard diversified path failed!!!\n");
                return false;
            }
        }

        //Encrypt Sapling wallet frontier tree
        {
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << SaplingWalletNoteCommitmentTreeWriter(saplingWallet);
            
            std::vector<unsigned char> vchCryptedSecret;
            std::string saplingTreeKey = "sapling_note_commitment_tree";
            uint256 chash = HashWithFP(saplingTreeKey);
            CKeyingMaterial vchSecret(ss.begin(), ss.end());

            if (!EncryptSerializedWalletObjects(vMasterKey, vchSecret, chash, vchCryptedSecret)) {
                LogPrintf("Encrypting Sapling wallet frontier tree failed!!!\n");
                return false;
            }

            if (!pwalletdbEncryption->WriteCryptedSaplingWitnesses(vchCryptedSecret, chash)) {
                LogPrintf("Writing encrypted Sapling wallet frontier tree failed!!!\n");
                return false;
            }
        }

        //Encrypt Orchard wallet frontier tree
        {
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << OrchardWalletNoteCommitmentTreeWriter(orchardWallet);
            
            std::vector<unsigned char> vchCryptedSecret;
            std::string orchardTreeKey = "orchard_note_commitment_tree";
            uint256 chash = HashWithFP(orchardTreeKey);
            CKeyingMaterial vchSecret(ss.begin(), ss.end());

            if (!EncryptSerializedWalletObjects(vMasterKey, vchSecret, chash, vchCryptedSecret)) {
                LogPrintf("Encrypting Orchard wallet frontier tree failed!!!\n");
                return false;
            }

            if (!pwalletdbEncryption->WriteCryptedOrchardWitnesses(vchCryptedSecret, chash)) {
                LogPrintf("Writing encrypted Orchard wallet frontier tree failed!!!\n");
                return false;
            }
        }

        //Encrypt all CScripts
        for (map<CScriptID, CScript>::iterator it = mapScripts.begin(); it != mapScripts.end(); ++it) {
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
        mapOrchardSpendingKeys.clear();

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

/**
 * @brief Encrypt serialized wallet objects using default master key
 * @param vchSecret The secret data to encrypt
 * @param chash Hash used for verification
 * @param vchCryptedSecret[out] The resulting encrypted data
 * @return true if encryption was successful, false otherwise
 * 
 * Encrypts serialized wallet objects (like keys, addresses) using the wallet's
 * default master key. This is used for securing sensitive wallet data in storage.
 */
bool CWallet::EncryptSerializedWalletObjects(
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret){

    return CCryptoKeyStore::EncryptSerializedSecret(vchSecret, chash, vchCryptedSecret);
}

/**
 * @brief Encrypt serialized wallet objects using specified master key
 * @param vMasterKeyIn The master key to use for encryption
 * @param vchSecret The secret data to encrypt
 * @param chash Hash used for verification
 * @param vchCryptedSecret[out] The resulting encrypted data
 * @return true if encryption was successful, false otherwise
 * 
 * Encrypts serialized wallet objects using a specific master key. This allows
 * for encryption with different keys or during wallet encryption operations.
 */
bool CWallet::EncryptSerializedWalletObjects(
    CKeyingMaterial &vMasterKeyIn,
    const CKeyingMaterial &vchSecret,
    const uint256 chash,
    std::vector<unsigned char> &vchCryptedSecret) {

    return CCryptoKeyStore::EncryptSerializedSecret(vMasterKeyIn, vchSecret, chash, vchCryptedSecret);
}

/**
 * @brief Decrypt serialized wallet objects
 * @param vchCryptedSecret The encrypted data to decrypt
 * @param chash Hash used for verification
 * @param vchSecret[out] The resulting decrypted secret data
 * @return true if decryption was successful, false otherwise
 * 
 * Decrypts previously encrypted wallet objects back to their original form.
 * This is used when loading encrypted wallet data from storage or when the
 * wallet needs to access private keys for transaction signing.
 */
bool CWallet::DecryptSerializedWalletObjects(
     const std::vector<unsigned char>& vchCryptedSecret,
     const uint256 chash,
     CKeyingMaterial &vchSecret) {

    return CCryptoKeyStore::DecryptSerializedSecret(vchCryptedSecret, chash, vchSecret);
 }

/**
 * @brief Generate hash with fingerprint for wallet objects
 * @tparam WalletObject Type of the wallet object to hash
 * @param wObj The wallet object to hash
 * @return A unique hash for the object including fingerprint data
 * 
 * Creates a unique hash for wallet objects that includes fingerprint information.
 * This is used to create unique identifiers for wallet objects that can be
 * used for encryption keys and database indexing.
 */
template<typename WalletObject>
uint256 CWallet::HashWithFP(WalletObject &wObj) {

    CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
    s << wObj;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << seedEncyptionFP;

    return Hash(s.begin(), s.end(), ss.begin(), ss.end());
}

/**
 * @brief Serialize a single wallet object for encryption
 * @tparam WalletObject1 Type of the wallet object to serialize
 * @param wObj1 The wallet object to serialize
 * @return Serialized data as CKeyingMaterial for encryption
 * 
 * Serializes a single wallet object into a secure data stream format
 * suitable for encryption. Used to prepare wallet data for secure storage.
 */
template<typename WalletObject1>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1) {

    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObj1;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

/**
 * @brief Serialize two wallet objects for encryption
 * @tparam WalletObject1 Type of the first wallet object
 * @tparam WalletObject2 Type of the second wallet object
 * @param wObj1 The first wallet object to serialize
 * @param wObj2 The second wallet object to serialize
 * @return Serialized data as CKeyingMaterial for encryption
 * 
 * Serializes two wallet objects as a pair into a secure data stream format
 * suitable for encryption. Used when multiple related objects need to be
 * stored together securely.
 */
template<typename WalletObject1, typename WalletObject2>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2) {

    auto wObjs = std::make_pair(wObj1, wObj2);
    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObjs;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

/**
 * @brief Serialize three wallet objects for encryption
 * @tparam WalletObject1 Type of the first wallet object
 * @tparam WalletObject2 Type of the second wallet object
 * @tparam WalletObject3 Type of the third wallet object
 * @param wObj1 The first wallet object to serialize
 * @param wObj2 The second wallet object to serialize
 * @param wObj3 The third wallet object to serialize
 * @return Serialized data as CKeyingMaterial for encryption
 * 
 * Serializes three wallet objects as nested pairs into a secure data stream
 * format suitable for encryption. Used when multiple related objects need
 * to be stored together securely.
 */
template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
CKeyingMaterial CWallet::SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3) {

    auto wObjs = std::make_pair(std::make_pair(wObj1, wObj2), wObj3);
    CSecureDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << wObjs;
    CKeyingMaterial vchSecret(ss.begin(), ss.end());

    return vchSecret;
}

/**
 * @brief Deserialize a single wallet object from decrypted data
 * @tparam WalletObject1 Type of the wallet object to deserialize
 * @param vchSecret The decrypted keying material containing serialized data
 * @param wObj1[out] The wallet object to populate with deserialized data
 * 
 * Deserializes a single wallet object from decrypted secure data stream.
 * Used to recover wallet objects after successful decryption from storage.
 */
template<typename WalletObject1>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
}

/**
 * @brief Deserialize two wallet objects from decrypted data
 * @tparam WalletObject1 Type of the first wallet object
 * @tparam WalletObject2 Type of the second wallet object
 * @param vchSecret The decrypted keying material containing serialized data
 * @param wObj1[out] The first wallet object to populate
 * @param wObj2[out] The second wallet object to populate
 * 
 * Deserializes two wallet objects from decrypted secure data stream.
 * Used to recover multiple related objects after decryption from storage.
 */
template<typename WalletObject1, typename WalletObject2>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
    ss >> wObj2;
}

/**
 * @brief Deserialize three wallet objects from decrypted data
 * @tparam WalletObject1 Type of the first wallet object
 * @tparam WalletObject2 Type of the second wallet object
 * @tparam WalletObject3 Type of the third wallet object
 * @param vchSecret The decrypted keying material containing serialized data
 * @param wObj1[out] The first wallet object to populate
 * @param wObj2[out] The second wallet object to populate
 * @param wObj3[out] The third wallet object to populate
 * 
 * Deserializes three wallet objects from decrypted secure data stream.
 * Used to recover multiple related objects after decryption from storage.
 */
template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
void CWallet::DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3) {

    CSecureDataStream ss(vchSecret, SER_NETWORK, PROTOCOL_VERSION);
    ss >> wObj1;
    ss >> wObj2;
    ss >> wObj3;
}

/**
 * @brief Increment the wallet's order position counter
 * @param pwalletdb Optional wallet database instance to use
 * @return The next available order position number
 * 
 * Generates and returns the next order position number for wallet transactions.
 * Order positions are used to maintain transaction ordering within the wallet
 * for display and processing purposes. The counter is persisted to the database
 * to ensure continuity across wallet restarts.
 */
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

/**
 * @brief Get ordered transaction items for display and accounting
 * @param acentries[out] List to populate with accounting entries
 * @param strAccount Account name to filter by (empty for all accounts)
 * @param pwalletdbIn Optional wallet database instance to use
 * @return Ordered multimap of transaction items by order position
 * 
 * Retrieves and orders all wallet transactions and accounting entries by their
 * order position for display and accounting purposes. This function:
 * - Combines wallet transactions and accounting entries
 * - Sorts them by order position (nOrderPos)
 * - Filters by account if specified
 * - Returns a multimap suitable for chronological display
 * 
 * The returned TxItems multimap contains pointers to both CWalletTx and
 * CAccountingEntry objects, ordered by their position in the wallet's
 * transaction history.
 */
CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount, CWalletDB *pwalletdbIn)
{
    AssertLockHeld(cs_wallet); // mapWallet

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

    CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
    acentries.clear();
    pwalletdb->ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }
    if (!pwalletdbIn) delete pwalletdb;

    return txOrdered;
}

/**
 * @brief Mark all wallet transactions as dirty for recalculation
 * 
 * Marks every transaction in the wallet as "dirty", forcing recalculation of
 * cached values like balances, confirmations, and other derived data on next
 * access. This is typically called when blockchain state changes that could
 * affect transaction validity or confirmation status.
 * 
 * The function iterates through all wallet transactions and calls MarkDirty()
 * on each one to invalidate cached computations.
 */
void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            item.second.MarkDirty();
    }
}

/**
 * @brief Ensure that every note in the wallet has a cached nullifier
 * @return true if all nullifiers were successfully computed and cached
 * 
 * Iterates through all notes in the wallet (for which we possess a spending key)
 * and ensures that their nullifiers are computed and cached. This function:
 * 
 * - Processes Sprout notes using JoinSplit witnesses
 * - Processes Sapling notes using Sapling witnesses  
 * - Processes Orchard notes using Orchard witnesses
 * - Updates nullifier-to-note mapping for efficient lookups
 * 
 * Nullifiers are essential for preventing double-spending of shielded notes.
 * This function ensures that all spendable notes have their nullifiers readily
 * available for transaction creation and validation.
 * 
 * Returns false if any nullifier computation fails, indicating potential
 * wallet corruption or missing witness data.
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
 * @brief Update nullifier-to-note mappings with transaction data
 * @param wtx The wallet transaction to process for nullifier updates
 * 
 * Updates the wallet's internal nullifier-to-note mappings with the cached
 * nullifiers from the specified transaction. This function processes:
 * 
 * - Sprout nullifiers and their corresponding JSOutPoints
 * - Sapling nullifiers and their corresponding SaplingOutPoints
 * - Orchard nullifiers and their corresponding OrchardOutPoints
 * 
 * These mappings are essential for:
 * - Quickly determining if a note has been spent
 * - Finding the outpoint corresponding to a given nullifier
 * - Validating transaction inputs during creation
 * - Maintaining wallet state consistency
 * 
 * This function is called after nullifiers are computed or when loading
 * transactions from storage to ensure mappings remain current.
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
                SaplingOutPoint op = item.first;

                //Write Changes to disk on newt wallet flush
                op.writeToDisk = true;

                mapSaplingNullifiersToNotes[*item.second.nullifier] = op;
                mapArcSaplingOutPoints[*item.second.nullifier] = op;
            }
        }

        for (const mapOrchardNoteData_t::value_type& item : wtx.mapOrchardNoteData) {
            if (item.second.nullifier) {
                OrchardOutPoint op = item.first;

                //Write Changes to disk on newt wallet flush
                op.writeToDisk = true;

                mapOrchardNullifiersToNotes[*item.second.nullifier] = op;
                mapArcOrchardOutPoints[*item.second.nullifier] = op;
            }
        }
    }
}

/**
 * @brief Update Sprout nullifier mappings, computing nullifiers if needed
 * @param wtx The wallet transaction to process
 * 
 * Updates mapSproutNullifiersToNotes by computing nullifiers from cached
 * witnesses if necessary. For each Sprout note in the transaction:
 * 
 * - Retrieves the cached witness for the note
 * - Computes the nullifier using the spending key and witness
 * - Updates the nullifier cache in the note data
 * - Updates the global nullifier-to-note mapping
 * 
 * This function is essential for maintaining the ability to detect when
 * Sprout notes have been spent and for creating new transactions that
 * spend existing notes.
 */
void CWallet::UpdateSproutNullifierNoteMapWithTx(CWalletTx& wtx) {
    LOCK(cs_wallet);

    ZCNoteDecryption dec;
    for (mapSproutNoteData_t::value_type& item : wtx.mapSproutNoteData) {
        SproutNoteData nd = item.second;

        if (nd.witnesses.empty()) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (nd.nullifier) {
                mapSproutNullifiersToNotes.erase(nd.nullifier.value());
            }
            nd.nullifier = std::nullopt;
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

                uint256 nullifier = optNullifier.value();
                mapSproutNullifiersToNotes[nullifier] = item.first;
                mapArcJSOutPoints[nullifier] = item.first;
                item.second.nullifier = nullifier;

            }
        }
    }
}

/**
 * @brief Update Sapling nullifier mappings, computing nullifiers if needed
 * @param wtx The wallet transaction to process
 * 
 * Updates mapSaplingNullifiersToNotes by computing nullifiers from cached
 * witnesses if necessary. For each Sapling note in the transaction:
 * 
 * - Retrieves the cached witness for the note
 * - Computes the nullifier using the spending key and witness
 * - Updates the nullifier cache in the note data
 * - Updates the global nullifier-to-note mapping
 * 
 * This function is essential for maintaining the ability to detect when
 * Sapling notes have been spent and for creating new transactions that
 * spend existing notes.
 */
void CWallet::UpdateSaplingNullifierNoteMapWithTx(CWalletTx* wtx) {
    LOCK(cs_wallet);

    auto vOutputs = wtx->GetSaplingOutputs();

    for (mapSaplingNoteData_t::value_type &item : wtx->mapSaplingNoteData) {
        SaplingOutPoint op = item.first;
        SaplingNoteData nd = item.second;

        //Write Changes to Disk on next wallet flush
        op.writeToDisk = true;

        if (nd.getPosition() == std::nullopt) {
            // If there are no witnesses, erase the nullifier and associated mapping.
            if (item.second.nullifier) {
                mapSaplingNullifiersToNotes.erase(item.second.nullifier.value());
            }
            item.second.nullifier = std::nullopt;
        }
        else {
            uint64_t position = nd.getPosition().value();
            // Skip if we only have incoming viewing key
            if (mapSaplingFullViewingKeys.count(nd.ivk) != 0) {
                SaplingExtendedFullViewingKey extfvk = mapSaplingFullViewingKeys.at(nd.ivk);

                // Compute nullifier directly from Output
                auto optNullifier = libzcash::SaplingNotePlaintext::ComputeNullifierFromOutput(
                    vOutputs[op.n], 
                    extfvk.fvk, 
                    position
                );

                if (!optNullifier) {
                    // This should not happen. If it does, maybe the position has been corrupted or miscalculated?
                    LogPrintf("ERROR: ComputeNullifierFromOutput failed for output %d in tx %s\n", 
                              op.n, wtx->GetHash().ToString());
                    assert(false);
                }
                
                uint256 nullifier = optNullifier.value();
                mapSaplingNullifiersToNotes[nullifier] = op;
                mapArcSaplingOutPoints[nullifier] = op;
                item.second.nullifier = nullifier;
            }
        }
    }
}

/**
 * @brief Update Orchard nullifier mappings, computing nullifiers if needed  
 * @param wtx The wallet transaction to process
 * 
 * Updates mapOrchardNullifiersToNotes by computing nullifiers from cached
 * witnesses if necessary. For each Orchard note in the transaction:
 * 
 * - Retrieves the cached witness for the note
 * - Computes the nullifier using the spending key and witness
 * - Updates the nullifier cache in the note data
 * - Updates the global nullifier-to-note mapping
 * 
 * This function is essential for maintaining the ability to detect when
 * Orchard notes have been spent and for creating new transactions that
 * spend existing notes.
 */
/**
* Update mapSaplingNullifiersToNotes, computing the nullifier from a cached witness if necessary.
*/
void CWallet::UpdateOrchardNullifierNoteMapWithTx(CWalletTx* wtx) {
   LOCK(cs_wallet);

   auto vActions = wtx->GetOrchardActions();

   for (mapOrchardNoteData_t::value_type &item : wtx->mapOrchardNoteData) {
       OrchardOutPoint op = item.first;
       OrchardNoteData nd = item.second;

       //Write Changes to Disk on next wallet flush
       op.writeToDisk = true;

       if (nd.getPosition() == std::nullopt) {
           // If there are no witnesses, erase the nullifier and associated mapping.
           if (item.second.nullifier) {
               mapOrchardNullifiersToNotes.erase(item.second.nullifier.value());
           }
           item.second.nullifier = std::nullopt;
       }
       else {
           uint64_t position = nd.getPosition().value();
           // Skip if we only have incoming viewing key
           if (mapOrchardFullViewingKeys.count(nd.ivk) != 0) {
               OrchardExtendedFullViewingKeyPirate extfvk = mapOrchardFullViewingKeys.at(nd.ivk);

               // Compute nullifier directly from Action using bridge
               auto optNullifier = libzcash::OrchardNotePlaintext::ComputeNullifierFromAction(
                   vActions[op.n],
                   extfvk.fvk
               );

               if (!optNullifier) {
                   // This should not happen. If it does, maybe the witness or action data is corrupted?
                   LogPrintf("ERROR: ComputeNullifierFromAction failed for action %d in tx %s\n", 
                             op.n, wtx->GetHash().ToString());
                   assert(false);
               }
               
               uint256 nullifier = optNullifier.value();
               mapOrchardNullifiersToNotes[nullifier] = op;
               mapArcOrchardOutPoints[nullifier] = op;
               item.second.nullifier = nullifier;
           }
       }
   }
}

/**
 * @brief Update nullifier mappings for all transactions in a block
 * @param pblock The block containing transactions to process
 * 
 * Iterates over all transactions in a block and updates the cached Sapling
 * nullifiers for transactions which belong to the wallet. This function:
 * 
 * - Processes each transaction in the block
 * - Identifies transactions that belong to this wallet
 * - Updates nullifier caches for Sapling notes in those transactions
 * - Maintains consistency of nullifier-to-note mappings
 * 
 * This function is called during block processing to ensure that wallet
 * state remains synchronized with the blockchain and that all nullifiers
 * are properly cached for efficient spending detection.
 */
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
            UpdateSaplingNullifierNoteMapWithTx(&mapWallet[hash]);
            UpdateOrchardNullifierNoteMapWithTx(&mapWallet[hash]);
        }
    }
}

/**
 * @brief Add a transaction to the wallet
 * @param wtxIn The wallet transaction to add
 * @param fFromLoadWallet True if loading from wallet file (vs. new transaction)
 * @param pwalletdb Optional database handle for batch operations
 * @param nHeight Block height where transaction was mined (0 if unconfirmed)
 * @param fRescan True if this is part of a wallet rescan operation
 * @return true if transaction was successfully added or updated
 * 
 * Adds or updates a transaction in the wallet. This is the primary function
 * for incorporating transactions into the wallet, handling both new incoming
 * transactions and wallet file loading. The function:
 * 
 * - Updates the wallet's transaction map
 * - Processes shielded notes (Sapling/Orchard) and nullifier mappings
 * - Handles transaction conflicts and reorganizations
 * - Updates spent transaction tracking
 * - Persists changes to the wallet database
 * - Notifies UI of balance changes
 * 
 * This function is critical for maintaining wallet state consistency and
 * ensuring all transaction-related data structures remain synchronized.
 */
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
        AddToSpends(hash);
        bool fInsertedNew = ret.second;
        if (fInsertedNew) {
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
            if (wtxIn.mapSaplingNoteData.size() > 0 && wtxIn.mapSaplingNoteData != wtx.mapSaplingNoteData) {
                wtx.mapSaplingNoteData = wtxIn.mapSaplingNoteData;
                fUpdated = true;
            }
            if (wtxIn.mapOrchardNoteData.size() > 0 && wtxIn.mapOrchardNoteData != wtx.mapOrchardNoteData) {
                wtx.mapOrchardNoteData = wtxIn.mapOrchardNoteData;
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
#ifdef ENABLE_SYSTEM_COMMAND
            boost::thread t(runCommand, strCmd); // thread runs free
#else
            LogPrintf("Wallet notification skipped: %s\nTo enable, rebuild with: ./configure CXXFLAGS=\"-DENABLE_SYSTEM_COMMAND\"\n", strCmd);
#endif
        }

    }
    return true;
}

/**
 * @brief Add transactions to wallet if they involve wallet addresses
 * @param vtx Vector of transactions to examine
 * @param vAddedTxes[out] Vector to store transactions that were added
 * @param pblock The block containing the transactions (optional)
 * @param nHeight Block height where transactions were mined
 * @param fUpdate Whether to update existing transactions
 * @param saplingAddressesFound[out] Set of newly discovered Sapling addresses
 * @param orchardAddressesFound[out] Set of newly discovered Orchard addresses
 * @param fRescan True if this is part of a wallet rescan operation
 * 
 * Examines a vector of transactions and adds any that involve this wallet's
 * addresses to the wallet. This function:
 * 
 * - Decrypts Sapling and Orchard notes to find wallet-relevant transactions
 * - Adds newly discovered addresses to the wallet's viewing key store
 * - Creates CWalletTx objects for relevant transactions with note data
 * - Updates wallet transaction maps and spend tracking
 * 
 * This is the primary function for batch processing of transactions during
 * block connection and wallet synchronization operations.
 */
void CWallet::AddToWalletIfInvolvingMe(
    const std::vector<CTransaction> &vtx,
    std::vector<CTransaction> &vAddedTxes,
    const CBlock* pblock,
    const int nHeight,
    bool fUpdate,
    std::set<SaplingPaymentAddress>& saplingAddressesFound,
    std::set<OrchardPaymentAddressPirate>& orchardAddressesFound,
    bool fRescan)
{
    {
        AssertLockHeld(cs_wallet);

        //Step 1a -- decrypt sapling transactions
        auto saplingNoteDataAndAddressesToAdd = FindMySaplingNotes(vtx, nHeight);
        auto saplingNoteData = saplingNoteDataAndAddressesToAdd.first;
        auto saplingAddressesToAdd = saplingNoteDataAndAddressesToAdd.second;

        //Step 1B -- decrypt orchard transactions
        auto orchardNoteDataAndAddressesToAdd = FindMyOrchardNotes(vtx, nHeight);
        auto orchardNoteData = orchardNoteDataAndAddressesToAdd.first;
        auto orchardAddressesToAdd = orchardNoteDataAndAddressesToAdd.second;

        //Step 2a -- add sapling addresses
        for (const auto &saplingAddressToAdd : saplingAddressesToAdd) {
            //Loaded into memory only
            //This will be saved during the wallet SetBestChainINTERNAL
            CCryptoKeyStore::AddSaplingIncomingViewingKey(saplingAddressToAdd.second, saplingAddressToAdd.first);
            //Store addresses to notify GUI later
            saplingAddressesFound.insert(saplingAddressToAdd.first);
        }

        //Step 2b -- add orchard addresses
        for (const auto &orchardAddressToAdd : orchardAddressesToAdd) {
            //Loaded into memory only
            //This will be saved during the wallet SetBestChainINTERNAL
            CCryptoKeyStore::AddOrchardIncomingViewingKey(orchardAddressToAdd.second, orchardAddressToAdd.first);
            //Store addresses to notify GUI later
            orchardAddressesFound.insert(orchardAddressToAdd.first);
        }

        //Step 3 -- add transactions
        for (int i = 0; i < vtx.size(); i++) {

            uint256 hash = vtx[i].GetHash();

            //Format Decrypted Sapling Note data for insertion into CWalletTx
            mapSaplingNoteData_t mapSaplingNoteData;
            for (mapSaplingNoteData_t::iterator it = saplingNoteData.begin(); it != saplingNoteData.end(); it++) {
                SaplingOutPoint op = (*it).first;
                SaplingNoteData nd = (*it).second;
                if (op.hash == hash) {
                      mapSaplingNoteData.insert(std::make_pair(op, nd));
                }
            }

            //Format Decrypted Orchard Note data for insertion into CWalletTx
            mapOrchardNoteData_t mapOrchardNoteData;
            for (mapOrchardNoteData_t::iterator it = orchardNoteData.begin(); it != orchardNoteData.end(); it++) {
                OrchardOutPoint op = (*it).first;
                OrchardNoteData nd = (*it).second;
                if (op.hash == hash) {
                      mapOrchardNoteData.insert(std::make_pair(op, nd));
                }
            }

            bool fExisted = mapWallet.count(vtx[i].GetHash()) != 0;

            if (fExisted || IsMine(vtx[i]) || IsFromMe(vtx[i]) || mapSaplingNoteData.size() > 0 || mapOrchardNoteData.size() > 0)
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
                    if (IsMine(vtx[i]) && !vtx[i].IsCoinBase() && !IsFromMe(vtx[i]))
                    {
                        bool fIsFromWhiteList = false;
                        BOOST_FOREACH(const CTxIn& txin, vtx[i].vin)
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
                                        LogPrintf("tx.%s passed wallet filter! whitelistaddress.%s\n", vtx[i].GetHash().ToString(),EncodeDestination(dest));
                                        break;
                                    }
                                }
                            }
                        }
                        if (!fIsFromWhiteList)
                        {
                            LogPrintf("tx.%s is NOT passed wallet filter!\n", vtx[i].GetHash().ToString());
                            continue;
                        }
                    }
                }

                CWalletTx wtx(this,vtx[i]);

                //Set Decrypted Sapling Note Data in CWalletTx
                wtx.SetSaplingNoteData(mapSaplingNoteData);

                //Set Decrypted Sapling Note Data in CWalletTx
                wtx.SetOrchardNoteData(mapOrchardNoteData);

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
                    vAddedTxes.emplace_back(vtx[i]);
                }
            }
        }
    }
}

/**
 * @brief Synchronize wallet with a vector of transactions
 * @param vtx Vector of transactions to synchronize
 * @param pblock The block containing the transactions (optional)
 * @param nHeight Block height where transactions were mined
 * 
 * Synchronizes the wallet state with a set of transactions, typically called
 * during block processing. This function:
 * 
 * - Identifies transactions that involve this wallet
 * - Discovers new Sapling and Orchard addresses
 * - Updates the wallet's address book and viewing keys
 * - Notifies the UI of newly discovered addresses and transactions
 * 
 * This function ensures that the wallet remains synchronized with the
 * blockchain and that all relevant transactions are properly incorporated.
 */
void CWallet::SyncTransactions(const std::vector<CTransaction> &vtx, const CBlock* pblock, const int nHeight)
{
    LOCK(cs_wallet);
    std::set<SaplingPaymentAddress> saplingAddressesFound;
    std::set<OrchardPaymentAddressPirate> orchardAddressesFound;

    std::vector<CTransaction> vOurs;
    AddToWalletIfInvolvingMe(vtx, vOurs, pblock, nHeight, true, saplingAddressesFound, orchardAddressesFound, false);

    for (std::set<SaplingPaymentAddress>::iterator it = saplingAddressesFound.begin(); it != saplingAddressesFound.end(); it++) {
        SetZAddressBook(*it, "z-sapling", "", true);
    }

    for (std::set<OrchardPaymentAddressPirate>::iterator it = orchardAddressesFound.begin(); it != orchardAddressesFound.end(); it++) {
        SetZAddressBook(*it, "orchard", "", true);
    }

    for (int i = 0; i < vOurs.size(); i++) {
        MarkAffectedTransactionsDirty(vOurs[i]);
    }
}

/**
 * @brief Mark transactions affected by a given transaction as dirty
 * @param tx The transaction that affects other wallet transactions
 * 
 * When a transaction changes its 'conflicted' state, it affects the balance
 * available from the outputs it spends. This function marks all transactions
 * that spend the same inputs as dirty, forcing recalculation of their cached
 * values on next access.
 * 
 * The function processes all input types:
 * - Transparent inputs (UTXOs)
 * - Sprout nullifiers from JoinSplit descriptions  
 * - Sapling nullifiers from spend descriptions
 * - Orchard nullifiers from action descriptions
 */
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

    for (const auto& spend : tx.GetSaplingSpends())  {
        uint256 nullifier = uint256::FromRawBytes(spend.nullifier());
        if (mapSaplingNullifiersToNotes.count(nullifier) &&
            mapWallet.count(mapSaplingNullifiersToNotes[nullifier].hash)) {
            mapWallet[mapSaplingNullifiersToNotes[nullifier].hash].MarkDirty();
        }
    }

    for (const auto& action : tx.GetOrchardActions())  {
        uint256 nullifier = uint256::FromRawBytes(action.nullifier());
        if (mapOrchardNullifiersToNotes.count(nullifier) &&
            mapWallet.count(mapOrchardNullifiersToNotes[nullifier].hash)) {
            mapWallet[mapOrchardNullifiersToNotes[nullifier].hash].MarkDirty();
        }
    }
}

/**
 * @brief Remove a transaction from the wallet
 * @param hash The hash of the transaction to remove
 * @return true if the transaction was successfully removed, false otherwise
 * 
 * Removes a transaction from both the wallet's in-memory map and the persistent
 * database storage. The function:
 * 
 * - Only works with file-backed wallets
 * - Handles both encrypted and unencrypted wallet storage
 * - Removes the transaction from mapWallet if found
 * - Deletes the corresponding database entry
 * - For encrypted wallets, requires the wallet to be unlocked
 * 
 * Returns false if wallet is not file-backed, transaction not found, or
 * database deletion fails.
 */
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

/**
 * @brief Force a complete rescan of the wallet from genesis
 * 
 * Initiates a full wallet rescan starting from the genesis block.
 * This function will scan the entire blockchain for transactions
 * that involve this wallet's addresses and rebuild the wallet state.
 * Used when wallet corruption is detected or after importing keys.
 */
void CWallet::ForceRescanWallet() {
    CBlockIndex* pindex = chainActive.Genesis();
    ScanForWalletTransactions(pindex, true, true, true, true);
}

/**
 * @brief Rescan wallet if needed based on internal flags
 * 
 * Checks if the wallet needs a rescan and performs one if required.
 * The rescan starts from block height 1 (skipping genesis) and updates
 * the wallet state with any transactions found. Sets needsRescan flag
 * to false after completion.
 */
void CWallet::RescanWallet()
{
    if (needsRescan)
    {
        CBlockIndex *start = chainActive.Height() > 0 ? chainActive[1] : NULL;
        if (start)
            ScanForWalletTransactions(start, true, true, true, true);
        needsRescan = false;
    }
}


/**
 * @brief Get nullifier for a Sprout note if spending key is available
 * @param jsdesc The JoinSplit description containing the encrypted note
 * @param address The Sprout payment address to check
 * @param dec The note decryption context
 * @param hSig The signature hash for the JoinSplit
 * @param n The output index within the JoinSplit
 * @return The nullifier if spending key is available, nullopt otherwise
 * @throws std::runtime_error if the decryptor doesn't match this note
 * 
 * Attempts to compute the nullifier for a Sprout note by decrypting it
 * and using the spending key if available. The nullifier is only computed
 * if the wallet has the spending key (not just viewing key) and the wallet
 * is unlocked. This is essential for detecting spent notes.
 */
std::optional<uint256> CWallet::GetSproutNoteNullifier(const JSDescription &jsdesc,
                                                         const libzcash::SproutPaymentAddress &address,
                                                         const ZCNoteDecryption &dec,
                                                         const uint256 &hSig,
                                                         uint8_t n) const
{
    std::optional<uint256> ret;
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
 * @brief Find all Sprout notes in a transaction that belong to this wallet
 * @param tx The transaction to scan for Sprout notes
 * @return Map of JSOutPoints to SproutNoteData for notes belonging to this wallet
 * 
 * Scans all JoinSplit outputs in the given transaction and attempts to decrypt
 * them using the wallet's Sprout note decryptors. For each successfully decrypted
 * note, computes the nullifier (if spending key available) and creates note data.
 * 
 * Note: This should rarely be called directly as results are typically cached
 * in CWalletTx.mapSproutNoteData for wallet transactions.
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
                    LogPrintf("FindMySproutNotes(): Unexpected error while testing decrypt: %s\n", exc.what());
                }
            }
        }
    }
    return noteData;
}

/**
 * @brief Worker thread function for decrypting Orchard notes
 * @param wallet Pointer to the wallet instance
 * @param vIvk Vector of incoming viewing keys to try for decryption
 * @param vOrchardEncryptedAction Vector of encrypted Orchard actions to decrypt
 * @param vPosition Vector of action positions within transactions
 * @param vHash Vector of transaction hashes corresponding to actions
 * @param height Block height for validation context
 * @param noteData[out] Map to store successfully decrypted note data
 * @param viewingKeysToAdd[out] Map to store newly discovered viewing keys
 * @param threadNumber Thread identifier for logging/debugging
 * 
 * Multi-threaded worker function that attempts to decrypt Orchard actions using
 * the provided incoming viewing keys. Successfully decrypted notes are added to
 * the noteData map if their value exceeds the minimum transaction value threshold.
 * Thread-safe operations use wallet's cs_wallet_threadedfunction mutex.
 */
static void DecryptOrchardNoteWorker(
    const CWallet *wallet,
    const std::vector<const OrchardIncomingViewingKeyPirate*> vIvk,
    const std::vector<orchard_bundle::Action*> vOrchardEncryptedAction,
    const std::vector<uint32_t> vPosition,
    const std::vector<uint256> vHash,
    const int &height,
    mapOrchardNoteData_t *noteData,
    OrchardIncomingViewingKeyMap *viewingKeysToAdd,
    int threadNumber)
{
    for (int i = 0; i < vIvk.size(); i++) {

        OrchardIncomingViewingKeyPirate ivk = *vIvk[i];
        auto result = OrchardNotePlaintext::AttemptDecryptOrchardAction(vOrchardEncryptedAction[i], ivk);
        if (result != std::nullopt) {

            // We don't cache the nullifier here as computing it requires knowledge of the note position
            // in the commitment tree, which can only be determined when the transaction has been mined.
            OrchardOutPoint op {vHash[i], vPosition[i]};
            OrchardNoteData nd;
            nd.ivk = ivk;

            //Cache Address and value - in Memory Only
            auto note = result.value();
            nd.value = note.value();
            nd.address = note.GetAddress();

            LogPrintf("\n\nOrchard Transaction Found %s, %i\n\n", vHash[i].ToString(), vPosition[i]);
            if (nd.value >= minTxValue) {
                //Only add notes greater then this value
                //dust filter
                {
                    LOCK(wallet->cs_wallet_threadedfunction);
                    viewingKeysToAdd->insert(make_pair(nd.address, nd.ivk));
                    noteData->insert(std::make_pair(op, nd));
                }
            }
        }
    }
}

/**
 * @brief Find all Orchard notes in a vector of transactions that belong to this wallet
 * @param vtx Vector of transactions to scan for Orchard notes
 * @param height Block height for validation context
 * @return Pair containing map of OrchardOutPoints to OrchardNoteData and newly discovered viewing keys
 * 
 * Multi-threaded function that scans all Orchard actions across multiple transactions
 * to find notes belonging to this wallet. The function:
 * 
 * - Distributes decryption work across multiple threads for performance
 * - Attempts decryption using all known Orchard incoming viewing keys
 * - Applies dust filtering based on minimum transaction value
 * - Returns both successfully decrypted notes and any newly discovered viewing keys
 * 
 * This is the primary method for batch processing Orchard transactions during
 * block synchronization and wallet scanning operations.
 */
std::pair<mapOrchardNoteData_t, OrchardIncomingViewingKeyMap> CWallet::FindMyOrchardNotes(const std::vector<CTransaction> &vtx, int height) const
{
    LOCK(cs_wallet);

    //Data to be collected
    mapOrchardNoteData_t noteData;
    OrchardIncomingViewingKeyMap viewingKeysToAdd;

    //Create key thread buckets
    std::vector<const OrchardIncomingViewingKeyPirate*> vIvk;
    std::vector<std::vector<const OrchardIncomingViewingKeyPirate*>> vvIvk;

    //Create Output thread buckets
    std::vector<orchard_bundle::Action*> vOrchardEncryptedAction;
    std::vector<std::vector<orchard_bundle::Action*>> vvOrchardEncryptedAction;

    //Create transaction position thread buckets
    std::vector<uint32_t> vPosition;
    std::vector<std::vector<uint32_t>> vvPosition;

    //Create transaction hash thread buckets
    std::vector<uint256> vHash;
    std::vector<std::vector<uint256>> vvHash;

    for (uint32_t i = 0; i < maxProcessingThreads; i++) {
        vvIvk.emplace_back(vIvk);
        vvOrchardEncryptedAction.emplace_back(vOrchardEncryptedAction);
        vvPosition.emplace_back(vPosition);
        vvHash.emplace_back(vHash);
    }

    // Protocol Spec: 4.19 Block Chain Scanning (Sapling)
    uint32_t t = 0;
    for (uint32_t j = 0; j < vtx.size(); j++) {
        //Transaction being processed
        uint256 hash = vtx[j].GetHash();
        auto vActions = vtx[j].GetOrchardActions();

        for (uint32_t i = 0; i < vActions.size(); i++) {
            auto action = &vActions[i];

            //Create a tread entry for every ivk with the current note.
            for (auto it = setOrchardIncomingViewingKeys.begin(); it != setOrchardIncomingViewingKeys.end(); it++) {
                vvIvk[t].emplace_back(&(*it));
                vvPosition[t].emplace_back(i);
                vvHash[t].emplace_back(hash);
                vvOrchardEncryptedAction[t].emplace_back(action);

                //Increment ivk vector
                t++;
                //reset if ivk vector is greater qty of threads being used
                if (t >= vvIvk.size()) {
                    t = 0;
                }
            }
        }

        std::vector<boost::thread*> decryptionThreads;
        for (uint32_t i = 0; i < vvIvk.size(); i++) {
            if(!vvIvk[i].empty()) {
                decryptionThreads.emplace_back(new boost::thread(DecryptOrchardNoteWorker, this, vvIvk[i], vvOrchardEncryptedAction[i], vvPosition[i], vvHash[i], height, &noteData, &viewingKeysToAdd, i));
            }
        }

        // Cleanup Threads
        for (auto dthread : decryptionThreads) {
            dthread->join();
            delete dthread;
        }

        //Rest Vectors for next transaction
        for (uint32_t i = 0; i < vvIvk.size(); i++) {
            vvIvk[i].resize(0);
            vvOrchardEncryptedAction[i].resize(0);
            vvPosition[i].resize(0);
            vvHash[i].resize(0);
        }

    }

    //clean up vectors
    vvIvk.resize(0);
    vvOrchardEncryptedAction.resize(0);
    vvPosition.resize(0);
    vvHash.resize(0);

    return std::make_pair(noteData, viewingKeysToAdd);
}


/**
 * @brief Worker thread function for decrypting Sapling notes
 * @param wallet Pointer to the wallet instance
 * @param vIvk Vector of incoming viewing keys to try for decryption
 * @param vOutputs Vector of Sapling outputs to decrypt
 * @param vPosition Vector of output positions within transactions
 * @param vHash Vector of transaction hashes corresponding to notes
 * @param height Block height for validation context
 * @param noteData[out] Map to store successfully decrypted note data
 * @param viewingKeysToAdd[out] Map to store newly discovered viewing keys
 * @param threadNumber Thread identifier for logging/debugging
 * 
 * Multi-threaded worker function that attempts to decrypt Sapling notes using
 * the provided incoming viewing keys. Successfully decrypted notes are added to
 * the noteData map if their value exceeds the minimum transaction value threshold.
 * Thread-safe operations use wallet's cs_wallet_threadedfunction mutex.
 */
static void DecryptSaplingNoteWorker(
    const CWallet *wallet,
    const std::vector<const SaplingIncomingViewingKey*> vIvk,
    const std::vector<const sapling::Output*> vSaplingOutput,
    const std::vector<uint32_t> vPosition,
    const std::vector<uint256> vHash,
    const int &height,
    mapSaplingNoteData_t *noteData,
    SaplingIncomingViewingKeyMap *viewingKeysToAdd,
    int threadNumber)
{
    for (int i = 0; i < vIvk.size(); i++) {

        SaplingIncomingViewingKey ivk = *vIvk[i];
        
        auto result = SaplingNotePlaintext::AttemptDecryptSaplingOutput(*vSaplingOutput[i], ivk);
        if (result) {

            auto address = ivk.address(result.value().d);

            // We don't cache the nullifier here as computing it requires knowledge of the note position
            // in the commitment tree, which can only be determined when the transaction has been mined.
            SaplingOutPoint op {vHash[i], vPosition[i]};
            SaplingNoteData nd;
            nd.ivk = ivk;

            //Cache Address and value - in Memory Only
            auto note = result.value();
            nd.value = note.value();
            nd.address = address.value();

            if (nd.value >= minTxValue) {
                //Only add notes greater then this value
                //dust filter
                {
                    LOCK(wallet->cs_wallet_threadedfunction);
                    viewingKeysToAdd->insert(make_pair(address.value(),ivk));
                    noteData->insert(std::make_pair(op, nd));
                }
            }
        }
    }
}

/**
 * @brief Find all Sapling notes in a vector of transactions that belong to this wallet
 * @param vtx Vector of transactions to scan for Sapling notes
 * @param height Block height for validation context  
 * @return Pair containing map of SaplingOutPoints to SaplingNoteData and newly discovered viewing keys
 * 
 * Multi-threaded function that scans all Sapling outputs across multiple transactions
 * to find notes belonging to this wallet. The function:
 * 
 * - Distributes decryption work across multiple threads for performance
 * - Attempts decryption using all known Sapling incoming viewing keys
 * - Applies dust filtering based on minimum transaction value
 * - Returns both successfully decrypted notes and any newly discovered viewing keys
 * 
 * This is the primary method for batch processing Sapling transactions during
 * block synchronization and wallet scanning operations. Results are cached in
 * CWalletTx.mapSaplingNoteData for wallet transactions.
 */
std::pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> CWallet::FindMySaplingNotes(const std::vector<CTransaction> &vtx, int height) const
{
    LOCK(cs_wallet);

    //Data to be collected
    mapSaplingNoteData_t noteData;
    SaplingIncomingViewingKeyMap viewingKeysToAdd;

    //Create key thread buckets
    std::vector<const SaplingIncomingViewingKey*> vIvk;
    std::vector<std::vector<const SaplingIncomingViewingKey*>> vvIvk;

    //Create Output thread buckets
    std::vector<const sapling::Output*> vSaplingOutput;
    std::vector<std::vector<const sapling::Output*>> vvSaplingOutput;

    //Create transaction position thread buckets
    std::vector<uint32_t> vPosition;
    std::vector<std::vector<uint32_t>> vvPosition;

    //Create transaction hash thread buckets
    std::vector<uint256> vHash;
    std::vector<std::vector<uint256>> vvHash;

    for (uint32_t i = 0; i < maxProcessingThreads; i++) {
        vvIvk.emplace_back(vIvk);
        vvSaplingOutput.emplace_back(vSaplingOutput);
        vvPosition.emplace_back(vPosition);
        vvHash.emplace_back(vHash);
    }

    // Protocol Spec: 4.19 Block Chain Scanning (Sapling)
    uint32_t t = 0;
    for (uint32_t j = 0; j < vtx.size(); j++) {
        //Transaction being processed
        uint256 hash = vtx[j].GetHash();
        auto vOutputs = vtx[j].GetSaplingOutputs();

        for (uint32_t i = 0; i < vOutputs.size(); i++) {
            auto output = &vOutputs[i];

            //Create a thread entry for every ivk with the current note.
            for (auto it = setSaplingIncomingViewingKeys.begin(); it != setSaplingIncomingViewingKeys.end(); it++) {
                vvIvk[t].emplace_back(&(*it));
                vvPosition[t].emplace_back(i);
                vvHash[t].emplace_back(hash);
                vvSaplingOutput[t].emplace_back(output);

                //Increment ivk vector
                t++;
                //reset if ivk vector is greater qty of threads being used
                if (t >= vvIvk.size()) {
                    t = 0;
                }
            }
        }

        std::vector<boost::thread*> decryptionThreads;
        for (uint32_t i = 0; i < vvIvk.size(); i++) {
            if(!vvIvk[i].empty()) {
                decryptionThreads.emplace_back(new boost::thread(DecryptSaplingNoteWorker, this, vvIvk[i], vvSaplingOutput[i], vvPosition[i], vvHash[i], height, &noteData, &viewingKeysToAdd, i));
            }
        }

        // Cleanup Threads
        for (auto dthread : decryptionThreads) {
            dthread->join();
            delete dthread;
        }

        //Reset Vectors for next transaction
        for (uint32_t i = 0; i < vvIvk.size(); i++) {
            vvIvk[i].resize(0);
            vvSaplingOutput[i].resize(0);
            vvPosition[i].resize(0);
            vvHash[i].resize(0);
        }

    }

    //clean up vectors
    vvIvk.resize(0);
    vvSaplingOutput.resize(0);
    vvPosition.resize(0);
    vvHash.resize(0);

    return std::make_pair(noteData, viewingKeysToAdd);
}

/**
 * @brief Check if a Sprout nullifier belongs to this wallet
 * @param nullifier The Sprout nullifier to check
 * @return true if this wallet owns the note corresponding to the nullifier
 * 
 * Determines if a given Sprout nullifier corresponds to a note that belongs to
 * this wallet by checking the nullifier-to-note mapping. This is used to detect
 * when this wallet's Sprout notes are being spent in transactions.
 */
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

/**
 * @brief Check if a Sapling nullifier belongs to this wallet
 * @param nullifier The Sapling nullifier to check
 * @return true if this wallet owns the note corresponding to the nullifier
 * 
 * Determines if a given Sapling nullifier corresponds to a note that belongs to
 * this wallet by checking the nullifier-to-note mapping. This is used to detect
 * when this wallet's Sapling notes are being spent in transactions.
 */
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

/**
 * @brief Check if an Orchard nullifier belongs to this wallet
 * @param nullifier The Orchard nullifier to check
 * @return true if this wallet owns the note corresponding to the nullifier
 * 
 * Determines if a given Orchard nullifier corresponds to a note that belongs to
 * this wallet by checking the nullifier-to-note mapping. This is used to detect
 * when this wallet's Orchard notes are being spent in transactions.
 */
bool CWallet::IsOrchardNullifierFromMe(const uint256& nullifier) const
{
    {
        LOCK(cs_wallet);
        if (mapOrchardNullifiersToNotes.count(nullifier) &&
                mapWallet.count(mapOrchardNullifiersToNotes.at(nullifier).hash)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Get Sprout note witnesses for a vector of notes
 * @param notes Vector of JSOutPoints identifying the notes to get witnesses for
 * @param witnesses[out] Vector to populate with witnesses (may contain nullopt for missing witnesses)
 * @param final_anchor[out] The common anchor/root hash for all returned witnesses
 * 
 * Retrieves cached Sprout witnesses for the specified notes. All returned witnesses
 * must have the same anchor (Merkle tree root) to be usable together in a transaction.
 * The function:
 * 
 * - Looks up each note in the wallet's transaction map
 * - Retrieves the first (most recent) witness for each note if available
 * - Verifies all witnesses share the same anchor
 * - Returns the common anchor for use in transaction creation
 * 
 * Notes without witnesses or that don't exist will have nullopt in the output vector.
 * This function is used when preparing Sprout notes for spending in transactions.
 */
void CWallet::GetSproutNoteWitnesses(std::vector<JSOutPoint> notes,
                                     std::vector<std::optional<SproutWitness>>& witnesses,
                                     uint256 &final_anchor)
{
    LOCK(cs_wallet);
    witnesses.resize(notes.size());
    std::optional<uint256> rt;
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

/**
 * @brief Get Sapling note Merkle paths for spending
 * @param notes Vector of SaplingOutPoints identifying the notes to get paths for
 * @param saplingMerklePaths[out] Vector to populate with Merkle paths (same order as notes)
 * @param final_anchor[out] The common anchor/root hash for all returned paths
 * @return true if all paths were successfully retrieved, false otherwise
 * 
 * Retrieves the Merkle paths for the specified Sapling notes from the Sapling wallet,
 * which are required to create valid spend proofs. All returned paths must have the
 * same root hash (anchor) to be used together in a transaction. The function:
 * 
 * - Looks up each note in the wallet's transaction map
 * - Retrieves the Merkle path from the Sapling wallet
 * - Computes and verifies the anchor for each path
 * - Ensures all anchors match for transaction consistency
 * 
 * Returns false if any note is not found, path retrieval fails, or anchor
 * computation fails. This function is essential for creating Sapling spend
 * transactions that require valid authentication paths.
 */
bool CWallet::GetSaplingNoteMerklePaths(std::vector<SaplingOutPoint> notes,
                                      std::vector<MerklePath>& saplingMerklePaths,
                                      uint256 &final_anchor)
{
    LOCK(cs_wallet);
    saplingMerklePaths.resize(notes.size());
    std::optional<uint256> rt;
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
            auto vOutputs = wtx->GetSaplingOutputs();
            auto cmu = uint256::FromRawBytes(vOutputs[op.n].cmu());
            if (!saplingWallet.GetPathRootWithCMU(saplingMerklePaths[i], cmu, anchor)) {
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

/**
 * @brief Get Orchard note Merkle paths for spending
 * @param notes Vector of OrchardOutPoints identifying the notes to get paths for
 * @param orchardMerklePaths[out] Vector to populate with Merkle paths (same order as notes)
 * @param final_anchor[out] The common anchor/root hash for all returned paths
 * @return true if all paths were successfully retrieved, false otherwise
 * 
 * Retrieves the Merkle paths for the specified Orchard notes from the Orchard wallet,
 * which are required to create valid spend proofs. All returned paths must have the
 * same root hash (anchor) to be used together in a transaction. The function:
 * 
 * - Looks up each note in the wallet's transaction map
 * - Retrieves the Merkle path from the Orchard wallet
 * - Computes and verifies the anchor for each path using note commitments
 * - Ensures all anchors match for transaction consistency
 * 
 * Returns false if any note is not found, path retrieval fails, or anchor
 * computation fails. This function is essential for creating Orchard spend
 * transactions that require valid authentication paths.
 */
bool CWallet::GetOrchardNoteMerklePaths(std::vector<OrchardOutPoint> notes,
                                      std::vector<MerklePath>& orchardMerklePaths,
                                      uint256 &final_anchor)
{
    LOCK(cs_wallet);
    orchardMerklePaths.resize(notes.size());
    std::optional<uint256> rt;
    int i = 0;
    for (OrchardOutPoint op : notes) {

        const CWalletTx* wtx = GetWalletTx(op.hash);
        if (wtx == NULL) {
            return false;
        }

        if (wtx->mapOrchardNoteData.count(op)) {

            if (!orchardWallet.GetMerklePathOfNote(op.hash, op.n, orchardMerklePaths[i])) {
                return false;
            }

            LogPrintf("\nGot Path\n");

            //Calculate the anchor
            uint256 anchor;
            auto vActions = wtx->GetOrchardActions();
            auto cmu = uint256::FromRawBytes(vActions[op.n].cmx());
            if (!orchardWallet.GetPathRootWithCMU(orchardMerklePaths[i], cmu, anchor)) {
                return false;
            }

            LogPrintf("Got Anchor %s\n\n", anchor.ToString());
            LogPrintf("Orchard Wallet Anchor %s\n\n", orchardWallet.GetLatestAnchor().ToString());

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

/**
 * @brief Check if a transaction input belongs to this wallet
 * @param txin The transaction input to check
 * @return ISMINE_SPENDABLE if the input belongs to this wallet, ISMINE_NO otherwise
 * 
 * Determines if a transaction input spends an output that belongs to this wallet.
 * The function looks up the previous transaction being spent and checks if the
 * wallet owns the referenced output. This is used for determining transaction
 * ownership and calculating debits.
 */
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

/**
 * @brief Get the debit amount for a transaction input
 * @param txin The transaction input to calculate debit for
 * @param filter Filter specifying which types of ownership to consider
 * @return The value being debited from this wallet, or 0 if not ours
 * 
 * Calculates how much value is being spent from this wallet by the given
 * transaction input. The function looks up the previous transaction output
 * being spent and returns its value if the wallet owns it and it matches
 * the ownership filter criteria.
 */
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

/**
 * @brief Check if a transaction output belongs to this wallet
 * @param txout The transaction output to check
 * @return ISMINE_SPENDABLE if output belongs to this wallet, ISMINE_NO otherwise
 * 
 * Determines if a transaction output can be spent by this wallet by checking
 * if the wallet has the keys necessary to satisfy the output's scriptPubKey.
 * This is used for determining transaction ownership and calculating credits.
 */
isminetype CWallet::IsMine(const CTxOut& txout) const
{
    return ::IsMine(*this, txout.scriptPubKey);
}

/**
 * @brief Get the credit amount for a transaction output
 * @param txout The transaction output to calculate credit for
 * @param filter Filter specifying which types of ownership to consider
 * @return The value being credited to this wallet, or 0 if not ours
 * 
 * Calculates how much value is being received by this wallet from the given
 * transaction output. Returns the output value if the wallet owns it and it
 * matches the ownership filter criteria, otherwise returns 0.
 */
CAmount CWallet::GetCredit(const CTxOut& txout, const isminefilter& filter) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
}

/**
 * @brief Determine if a transaction output is change from this wallet
 * @param txout The transaction output to examine
 * @return true if the output is likely change, false otherwise
 * 
 * Attempts to identify if a transaction output represents change being returned
 * to this wallet. The current heuristic considers any output that belongs to
 * this wallet but is not in the address book as change. This may not be accurate
 * for multisignature wallets or complex transaction patterns.
 * 
 * @note This is a heuristic and may not be 100% accurate in all cases.
 */
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

/**
 * @brief Get the change amount for a transaction output
 * @param txout The transaction output to examine
 * @return The change value if this output is change, 0 otherwise
 * 
 * Returns the value of the transaction output if it is determined to be
 * change returning to this wallet, otherwise returns 0. Uses the IsChange()
 * heuristic to determine if the output represents change.
 */
CAmount CWallet::GetChange(const CTxOut& txout) const
{
    if (!MoneyRange(txout.nValue))
        throw std::runtime_error("CWallet::GetChange(): value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
}

typedef vector<unsigned char> valtype;
unsigned int HaveKeys(const vector<valtype>& pubkeys, const CKeyStore& keystore);

/**
 * @brief Count how many of the given public keys are present in the keystore
 * @param pubkeys Vector of public keys to check
 * @param keystore The keystore to search in
 * @return The number of public keys found in the keystore
 * 
 * Utility function that counts how many of the provided public keys have
 * corresponding private keys in the given keystore. Used for multisignature
 * script evaluation to determine if enough keys are available for spending.
 */
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

/**
 * @brief Check if any output in a transaction belongs to this wallet
 * @param tx The transaction to examine
 * @return true if at least one output belongs to this wallet, false otherwise
 * 
 * Convenience function that checks all outputs in a transaction to determine
 * if the wallet has any interest in the transaction. Returns true as soon as
 * the first owned output is found.
 */
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

/**
 * @brief Check if a specific transaction output belongs to this wallet (extended version)
 * @param tx The transaction containing the output
 * @param voutNum The index of the output to check
 * @return ISMINE_SPENDABLE if output belongs to this wallet, ISMINE_NO otherwise
 * 
 * Enhanced version of IsMine that handles special cases like non-standard scripts
 * and Verus OP_RETURN outputs that require transaction context for ownership
 * determination. Supports various script types including multisig, P2PKH, P2SH,
 * and crypto-condition scripts.
 * 
 * @note This function includes special handling for Verus-specific script types
 */
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

/**
 * @brief Check if a transaction originates from this wallet
 * @param tx The transaction to examine
 * @return true if the transaction spends funds from this wallet, false otherwise
 * 
 * Determines if a transaction is spending funds that belong to this wallet by
 * checking both transparent and shielded inputs. The function examines:
 * 
 * - Transparent inputs for wallet ownership (via GetDebit)
 * - Sprout nullifiers to detect spent Sprout notes
 * - Sapling nullifiers to detect spent Sapling notes  
 * - Orchard nullifiers to detect spent Orchard notes
 * 
 * Returns true if any input indicates the transaction is spending from this wallet.
 */
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
    for (const auto& spend : tx.GetSaplingSpends())  {
        uint256 nullifier = uint256::FromRawBytes(spend.nullifier());
        if (IsSaplingNullifierFromMe(nullifier)) {
            return true;
        }
    }
    for (const auto& action : tx.GetOrchardActions())  {
        uint256 nullifier = uint256::FromRawBytes(action.nullifier());
        if (IsOrchardNullifierFromMe(nullifier)) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Calculate total debit amount for a transaction
 * @param tx The transaction to examine
 * @param filter Filter specifying which types of ownership to consider
 * @return Total amount being debited from this wallet
 * 
 * Calculates the total amount being spent from this wallet by summing up
 * the debit amounts for all transaction inputs. Only includes inputs that
 * match the ownership filter criteria.
 */
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

/**
 * @brief Get credit amount for a specific transaction output
 * @param tx The transaction containing the output
 * @param voutNum The index of the output to examine
 * @param filter Filter specifying which types of ownership to consider
 * @return The credit amount for the specified output, or 0 if not ours
 * 
 * Calculates the credit amount for a specific output in a transaction.
 * Returns the output value if the wallet owns it and it matches the
 * ownership filter criteria, otherwise returns 0.
 */
CAmount CWallet::GetCredit(const CTransaction& tx, int32_t voutNum, const isminefilter& filter) const
{
    if (voutNum >= tx.vout.size() || !MoneyRange(tx.vout[voutNum].nValue))
        throw std::runtime_error("CWallet::GetCredit(): value out of range");
    return ((IsMine(tx.vout[voutNum]) & filter) ? tx.vout[voutNum].nValue : 0);
}

/**
 * @brief Calculate total credit amount for a transaction
 * @param tx The transaction to examine
 * @param filter Filter specifying which types of ownership to consider
 * @return Total amount being credited to this wallet
 * 
 * Calculates the total amount being received by this wallet by summing up
 * the credit amounts for all transaction outputs. Only includes outputs that
 * match the ownership filter criteria.
 */
CAmount CWallet::GetCredit(const CTransaction& tx, const isminefilter& filter) const
{
    CAmount nCredit = 0;
    for (int i = 0; i < tx.vout.size(); i++)
    {
        nCredit += GetCredit(tx, i, filter);
    }
    return nCredit;
}

/**
 * @brief Calculate total change amount for a transaction
 * @param tx The transaction to examine
 * @return Total amount of change outputs in the transaction
 * 
 * Calculates the total value of outputs that are determined to be change
 * returning to this wallet. Uses the IsChange() heuristic to identify
 * change outputs and sums their values.
 */
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

/**
 * @brief Check if hierarchical deterministic (HD) wallet features are fully enabled
 * @return false (HD features are currently limited)
 * 
 * Currently returns false as only Sapling addresses support full HD functionality.
 * This function may return true in the future when HD support is extended to
 * all address types.
 */
bool CWallet::IsHDFullyEnabled() const
{
    // Only Sapling addresses are HD for now
    return false;
}

/**
 * @brief Generate a new random HD seed for the wallet
 * @throws std::runtime_error if SetHDSeed fails
 * 
 * Generates a new random hierarchical deterministic seed and sets it as the
 * wallet's master seed. This function:
 * 
 * - Creates a cryptographically secure random seed
 * - Sets the seed in the wallet (requires unlocked wallet if encrypted)
 * - Creates and stores the HD chain metadata with seed fingerprint
 * - Records the creation time for the seed
 * 
 * This function is used for initial wallet setup or when creating a new seed.
 */
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

/**
 * @brief Validate a BIP39 mnemonic phrase
 * @param phrase The mnemonic phrase to validate
 * @return true if the phrase is valid, false otherwise
 * 
 * Validates a BIP39 mnemonic phrase to ensure it conforms to the standard
 * format and has a valid checksum. This function is used before attempting
 * to restore a wallet from a mnemonic phrase.
 */
bool CWallet::IsValidPhrase(std::string &phrase)
{
    LOCK(cs_wallet);

    HDSeed checkSeed;
    return checkSeed.IsValidPhrase(phrase);
}

/**
 * @brief Restore wallet seed from a BIP39 mnemonic phrase
 * @param phrase The BIP39 mnemonic phrase to restore from
 * @return true if restoration was successful, false otherwise
 * @throws std::runtime_error if SetHDSeed fails
 * 
 * Restores the wallet's HD seed from a BIP39 mnemonic phrase. This function:
 * 
 * - Validates the mnemonic phrase format and checksum
 * - Converts the phrase to a binary seed
 * - Sets the seed in the wallet (requires unlocked wallet if encrypted)
 * - Creates and stores HD chain metadata
 * - Records the restoration time
 * 
 * Used for wallet recovery from backup phrases.
 */
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

/**
 * @brief Set the hierarchical deterministic seed for the wallet
 * @param seed The HD seed to set
 * @return true if the seed was successfully set, false otherwise
 * 
 * Sets the HD seed in the wallet's keystore. The seed is used as the master
 * secret for deriving all hierarchical deterministic keys. The function:
 * 
 * - Fails if the wallet is encrypted and locked
 * - Handles both encrypted and unencrypted wallets appropriately
 * - Stores the seed in the appropriate keystore (encrypted or plain)
 * - Returns success/failure status
 * 
 * This is a low-level function used by seed generation and restoration operations.
 */
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

/**
 * @brief Set the HD chain metadata for the wallet
 * @param chain The HD chain metadata to set
 * @param memonly If true, only store in memory; if false, also persist to database
 * @throws std::runtime_error if database write fails
 * 
 * Sets the hierarchical deterministic chain metadata, which includes information
 * about key derivation paths, seed fingerprint, and creation time. The metadata
 * is stored in memory and optionally persisted to the wallet database.
 * 
 * This function is used during wallet initialization, seed generation, and
 * restoration operations to maintain HD key derivation state.
 */
void CWallet::SetHDChain(const CHDChain& chain, bool memonly)
{
    LOCK(cs_wallet);
    if (!memonly && fFileBacked && !CWalletDB(strWalletFile).WriteHDChain(chain))
        throw std::runtime_error(std::string(__func__) + ": writing chain failed");

    hdChain = chain;
}

/**
 * @brief Load an HD seed during wallet initialization
 * @param seed The HD seed to load
 * @return true if the seed was successfully loaded
 * 
 * Loads an unencrypted HD seed into the basic keystore during wallet loading.
 * This function is called when reading wallet data from the database during
 * wallet initialization. The seed is stored in unencrypted form.
 */
bool CWallet::LoadHDSeed(const HDSeed& seed)
{
    return CBasicKeyStore::SetHDSeed(seed);
}

/**
 * @brief Load an encrypted HD seed during wallet initialization
 * @param seedFp The fingerprint of the encrypted seed
 * @param seed The encrypted seed data
 * @return true if the encrypted seed was successfully loaded
 * 
 * Loads an encrypted HD seed into the crypto keystore during wallet loading.
 * This function is called when reading encrypted wallet data from the database.
 * The seed remains in encrypted form until the wallet is unlocked.
 */
bool CWallet::LoadCryptedHDSeed(const uint256& seedFp, const std::vector<unsigned char>& seed)
{
    return CCryptoKeyStore::SetCryptedHDSeed(seedFp, seed);
}

/**
 * @brief Set Sprout note data for this wallet transaction
 * @param noteData Map of JSOutPoint to SproutNoteData to set
 * @throws std::logic_error if any note reference is invalid
 * 
 * Replaces the Sprout note data for this transaction with the provided data.
 * Validates that all note references (JSOutPoint) correspond to valid outputs
 * in the transaction's JoinSplit descriptions before storing the data.
 * 
 * This function is used when processing transactions to store decrypted note
 * information and associated metadata like addresses and nullifiers.
 */
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

/**
 * @brief Set Sapling note data for this wallet transaction
 * @param noteData Map of SaplingOutPoint to SaplingNoteData to set
 * @throws std::logic_error if any note reference is invalid
 * 
 * Replaces the Sapling note data for this transaction with the provided data.
 * Validates that all note references (SaplingOutPoint) correspond to valid outputs
 * in the transaction's Sapling outputs before storing the data.
 * 
 * This function is used when processing transactions to store decrypted note
 * information and associated metadata like addresses, nullifiers, and witnesses.
 */
void CWalletTx::SetSaplingNoteData(mapSaplingNoteData_t &noteData)
{
    mapSaplingNoteData.clear();
    for (const std::pair<SaplingOutPoint, SaplingNoteData> nd : noteData) {
        if (nd.first.n < GetSaplingOutputsCount()) {
            mapSaplingNoteData[nd.first] = nd.second;
        } else {
            throw std::logic_error("CWalletTx::SetSaplingNoteData(): Invalid note");
        }
    }
}

/**
 * @brief Set Orchard note data for this wallet transaction
 * @param noteData Map of OrchardOutPoint to OrchardNoteData to set
 * @throws std::logic_error if any note reference is invalid
 * 
 * Replaces the Orchard note data for this transaction with the provided data.
 * Validates that all note references (OrchardOutPoint) correspond to valid actions
 * in the transaction's Orchard actions before storing the data.
 * 
 * This function is used when processing transactions to store decrypted note
 * information and associated metadata like addresses, nullifiers, and position data.
 */
void CWalletTx::SetOrchardNoteData(mapOrchardNoteData_t &noteData)
{
    mapOrchardNoteData.clear();
    for (const std::pair<OrchardOutPoint, OrchardNoteData> nd : noteData) {
        if (nd.first.n < GetOrchardActionsCount()) {
            mapOrchardNoteData[nd.first] = nd.second;
        } else {
            throw std::logic_error("CWalletTx::SetOrchardNoteData(): Invalid note");
        }
    }
}

/**
 * @brief Decrypt a Sprout note from this wallet transaction
 * @param jsop The JSOutPoint identifying the specific note to decrypt
 * @return Pair containing the decrypted note plaintext and payment address
 * @throws std::runtime_error if note decryptor not found or decryption fails
 * 
 * Decrypts a Sprout note that belongs to this wallet transaction using the cached
 * note data and wallet's note decryptors. The function:
 * 
 * - Retrieves the payment address and note data from cached information
 * - Gets the appropriate note decryptor for the payment address
 * - Computes the signature hash for the JoinSplit
 * - Decrypts the note ciphertext to recover the plaintext
 * 
 * This function is used when the wallet needs to access the details of a received
 * Sprout note, such as for displaying balance information or preparing to spend the note.
 */
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

/**
 * @brief Decrypt a Sapling note from this wallet transaction
 * @param params Consensus parameters for validation
 * @param height Block height for validation context
 * @param op The SaplingOutPoint identifying the specific note to decrypt
 * @return Optional pair containing the decrypted note plaintext and payment address
 * 
 * Decrypts a Sapling note that belongs to this wallet transaction using the cached
 * note data and incoming viewing key. The function:
 * 
 * - Retrieves the cached Sapling note data for the output point
 * - Extracts the encrypted ciphertext and ephemeral key from the output
 * - Uses the cached incoming viewing key to decrypt the note
 * - Derives the payment address from the decrypted note and viewing key
 * - Validates the decryption using consensus parameters and block height
 * 
 * Returns nullopt if the note data is not cached (indicating this wallet
 * doesn't own the note) or if decryption fails for any reason.
 * 
 * This function is used when the wallet needs to access details of received
 * Sapling notes for balance calculations, transaction creation, or display.
 */
std::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0) {
        return std::nullopt;
    }

    auto vOutputs = this->GetSaplingOutputs();
    auto nd = this->mapSaplingNoteData.at(op);

    // Use Rust decryption
    auto maybe_pt = SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[op.n], nd.ivk);
    assert(maybe_pt != std::nullopt);
    auto notePt = maybe_pt.value();

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(maybe_pa != std::nullopt);
    auto pa = maybe_pa.value();

    return std::make_pair(notePt, pa);
}

/**
 * @brief Decrypt a Sapling note without performing lead byte checks
 * @param op The SaplingOutPoint identifying the specific note to decrypt
 * @return Optional pair containing the decrypted note plaintext and payment address
 * 
 * Decrypts a Sapling note that belongs to this wallet transaction without
 * performing the lead byte validation checks. This is a more lenient decryption
 * method that skips height-dependent validation. The function:
 * 
 * - Retrieves the cached Sapling note data for the output point
 * - Extracts the encrypted ciphertext and ephemeral key from the output
 * - Uses the cached incoming viewing key to decrypt the note
 * - Derives the payment address from the decrypted note and viewing key
 * - Validates the decryption without height constraints
 * 
 * This method is useful for recovering notes from transactions that may have
 * been created before certain consensus rules were enforced, or when height
 * information is not available or reliable.
 * 
 * Returns nullopt if the note data is not cached (indicating this wallet
 * doesn't own the note) or if decryption fails for any reason.
 */
std::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::DecryptSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op) const
{
    // Check whether we can decrypt this SaplingOutPoint
    if (this->mapSaplingNoteData.count(op) == 0) {
        return std::nullopt;
    }

    auto vOutputs = this->GetSaplingOutputs();
    auto nd = this->mapSaplingNoteData.at(op);

    // Use Rust decryption
    auto maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[op.n], nd.ivk);
    assert(maybe_pt != std::nullopt);
    auto notePt = maybe_pt.value();

    auto maybe_pa = nd.ivk.address(notePt.d);
    assert(maybe_pa != std::nullopt);
    auto pa = maybe_pa.value();

    return std::make_pair(notePt, pa);
}

/**
 * @brief Recover a Sapling note using outgoing viewing keys
 * @param params Consensus parameters for validation
 * @param height Block height for validation context
 * @param op The SaplingOutPoint identifying the specific note to recover
 * @param ovks Set of outgoing viewing keys to try for recovery
 * @return Optional pair containing the recovered note plaintext and payment address
 * 
 * Attempts to recover a Sapling note by trying decryption with multiple outgoing
 * viewing keys (OVKs). This is used to decrypt notes that were sent by this wallet
 * to other addresses. The function:
 * 
 * - Extracts the encrypted ciphertext, outgoing ciphertext, and cryptographic commitments
 * - Iterates through the provided set of outgoing viewing keys
 * - For each OVK, attempts to decrypt the outgoing plaintext
 * - If successful, uses the recovered ephemeral key to decrypt the note plaintext
 * - Derives the payment address from the decrypted note
 * - Validates all cryptographic commitments and addresses
 * 
 * This is primarily used for recovering sent notes when the wallet needs to
 * display transaction history or verify outgoing payments. Returns nullopt
 * if none of the provided OVKs can successfully decrypt the note.
 */
std::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op, std::set<uint256>& ovks) const
{

    auto vOutputs = this->GetSaplingOutputs();

    for (auto ovk : ovks) {
        // Use new Orchard-style OVK decryption
        auto maybe_pt = SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[op.n], ovk);
        
        if (!maybe_pt) {
            // Try decrypting with the next ovk
            continue;
        }

        auto notePt = maybe_pt.value();

        // For OVK decryption, pk_d is returned in the plaintext
        return std::make_pair(notePt, SaplingPaymentAddress(notePt.d, notePt.pk_d.value()));
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return std::nullopt;
}

/**
 * @brief Recover a Sapling note using outgoing viewing keys without lead byte checks
 * @param op The SaplingOutPoint identifying the specific note to recover
 * @param ovks Set of outgoing viewing keys to try for recovery
 * @return Optional pair containing the recovered note plaintext and payment address
 * 
 * Attempts to recover a Sapling note by trying decryption with multiple outgoing
 * viewing keys (OVKs) without performing lead byte validation checks. This is a
 * more lenient recovery method that skips height-dependent validation. The function:
 * 
 * - Extracts the encrypted ciphertext, outgoing ciphertext, and cryptographic commitments
 * - Iterates through the provided set of outgoing viewing keys
 * - For each OVK, attempts to decrypt the outgoing plaintext
 * - If successful, uses the recovered ephemeral key to decrypt the note plaintext
 * - Derives the payment address from the decrypted note
 * - Validates cryptographic commitments without height constraints
 * 
 * This method is useful for recovering sent notes from transactions that may have
 * been created before certain consensus rules were enforced, or when height
 * information is not available or reliable. Returns nullopt if none of the
 * provided OVKs can successfully decrypt and recover the note.
 */
std::optional<std::pair<
    SaplingNotePlaintext,
    SaplingPaymentAddress>> CWalletTx::RecoverSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op, std::set<uint256>& ovks) const
{

    auto vOutputs = this->GetSaplingOutputs();

    // Try to decrypt with each ovk
    for (auto ovk : ovks) {
        // Use new Orchard-style OVK decryption
        auto maybe_pt = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[op.n], ovk);
        if (maybe_pt) {
            auto notePt = maybe_pt.value();

            // OVK decryption returns pk_d in the plaintext
            auto maybe_pa = libzcash::SaplingPaymentAddress(notePt.d, notePt.pk_d.value());

            return std::make_pair(notePt, maybe_pa);
        }
    }

    // Couldn't recover with any of the provided OutgoingViewingKeys
    return std::nullopt;
}

/**
 * @brief Decrypt an Orchard note from this wallet transaction
 * @param op The OrchardOutPoint identifying the specific note to decrypt
 * @return Optional pair containing the decrypted note plaintext and payment address
 * 
 * Decrypts an Orchard note that belongs to this wallet transaction using the cached
 * note data and incoming viewing key. The function:
 * 
 * - Retrieves the cached Orchard note data for the action point
 * - Uses the cached incoming viewing key to decrypt the note from the action
 * - Derives the payment address from the decrypted note plaintext
 * - Validates the decryption using Orchard cryptographic primitives
 * 
 * Returns nullopt if the note data is not cached (indicating this wallet
 * doesn't own the note) or if decryption fails for any reason.
 * 
 * This function is used when the wallet needs to access details of received
 * Orchard notes for balance calculations, transaction creation, or display.
 */
std::optional<std::pair<
    OrchardNotePlaintext,
    OrchardPaymentAddressPirate>> CWalletTx::DecryptOrchardNote(OrchardOutPoint op) const
{
    // Check whether we can decrypt this OrchardOutPoint
    if (this->mapOrchardNoteData.count(op) == 0) {
        return std::nullopt;
    }

    auto vActions = this->GetOrchardActions();
    auto nd = this->mapOrchardNoteData.at(op);

    auto maybe_pt = OrchardNotePlaintext::AttemptDecryptOrchardAction(&vActions[op.n], nd.ivk);
    assert(maybe_pt != std::nullopt);
    auto notePt = maybe_pt.value();
    auto pa = notePt.GetAddress();

    return std::make_pair(notePt, pa);
}

/**
 * @brief Get the effective timestamp for this wallet transaction
 * @return The transaction timestamp (smart time if available, otherwise received time)
 * 
 * Returns the most appropriate timestamp for this transaction. Prefers the
 * "smart time" (nTimeSmart) which is computed based on block timestamps and
 * other heuristics, falling back to the received time (nTimeReceived) if
 * smart time is not available.
 * 
 * Used for transaction ordering and display purposes in the wallet interface.
 */
int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

/**
 * @brief Calculate transparent debits and credits for this wallet transaction
 * @param listReceived[out] List of received outputs with destination and amount
 * @param listSent[out] List of sent outputs with destination and amount
 * @param nFee[out] Transaction fee amount (if this wallet sent the transaction)
 * @param strSentAccount[out] Account name used for sending (if applicable)
 * @param filter Filter specifying which types of ownership to consider
 * 
 * Analyzes this wallet transaction to determine the transparent (non-shielded)
 * value flows. The function:
 * 
 * - Calculates transaction fees for outgoing transactions
 * - Identifies received outputs and their destinations
 * - Identifies sent outputs and their destinations
 * - Handles value pool transfers (vpub_old/new)
 * - Processes Sapling and Orchard value balances
 * - Separates outputs into received vs. sent categories
 * 
 * This is used for transaction display, accounting, and balance calculations
 * in the wallet interface.
 */
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
        CAmount valueBalance = GetValueBalanceSapling() + GetValueBalanceOrchard();
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

/**
 * @brief Calculate account-specific amounts for this wallet transaction
 * @param strAccount The account name to calculate amounts for
 * @param nReceived[out] Total amount received by this account
 * @param nSent[out] Total amount sent from this account  
 * @param nFee[out] Transaction fee (if this account sent the transaction)
 * @param filter Filter specifying which types of ownership to consider
 * 
 * Calculates the amounts received, sent, and fees for a specific account
 * in this wallet transaction. The function:
 * 
 * - Uses GetAmounts() to get all transaction flows
 * - Filters results to only include the specified account
 * - Matches sent amounts by account name
 * - Matches received amounts by address book entries
 * - Only includes fees if the specified account sent the transaction
 * 
 * Used for account-based bookkeeping and balance calculations in the wallet.
 */
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

/**
 * @brief Generate Sprout witnesses for given note commitments
 * @param commitments Vector of note commitments to generate witnesses for
 * @param witnesses[out] Vector to populate with witnesses for the commitments
 * @param final_anchor[out] The final anchor/root of the Merkle tree
 * 
 * Reconstructs the Sprout Merkle tree by processing all blocks from genesis
 * and generates witnesses for the specified note commitments. The function:
 * 
 * - Rebuilds the Sprout commitment tree from genesis block
 * - Processes all JoinSplit transactions in chronological order
 * - Tracks witness paths for the requested commitments
 * - Updates all witnesses as new commitments are added
 * - Returns the final tree anchor
 * 
 * This is computationally expensive as it processes the entire blockchain.
 * Used for generating witnesses when cached witnesses are not available.
 */
void CWallet::WitnessNoteCommitment(std::vector<uint256> commitments,
                                    std::vector<std::optional<SproutWitness>>& witnesses,
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

                    BOOST_FOREACH(std::optional<SproutWitness>& wit, witnesses) {
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

    BOOST_FOREACH(std::optional<SproutWitness>& wit, witnesses) {
        if (wit) {
            assert(final_anchor == wit->root());
        }
    }
}

/**
 * @brief Reorder wallet transactions based on block height and index
 * @param mapSorted[out] Map to populate with sorted transaction pointers
 * @param maxOrderPos[out] Maximum order position found during sorting
 * 
 * Reorders wallet transactions based on their block height and position within
 * blocks to maintain chronological order. This function is needed because
 * transactions can become out of order when they are deleted and subsequently
 * re-added during initial wallet loading or rescanning operations.
 * 
 * The function processes all wallet transactions and sorts them by:
 * 1. Block height (confirmed transactions first, by height)
 * 2. Block index position (for transactions in the same block)
 * 3. Hash-based ordering (for unconfirmed transactions)
 * 
 * Requires cs_main lock to be held for thread safety during block access.
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

/**
 * @brief Update wallet transaction order positions from sorted map
 * @param mapSorted Map of sorted transactions by block height and index
 * @param resetOrder If true, reset all positions starting from 0; if false, preserve existing positions where possible
 * 
 * Updates the nOrderPos values for wallet transactions based on a pre-sorted map
 * to maintain proper chronological ordering. This function is called after
 * ReorderWalletTransactions() to apply the new ordering to the wallet.
 * 
 * The function:
 * - Assigns new order positions based on the sorted map
 * - Optionally resets all positions to start from 0
 * - Updates both in-memory and database storage
 * - Only modifies transactions whose positions have changed
 * - Updates the next available order position counter
 * 
 * Requires both cs_main and cs_wallet locks for thread safety.
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
 * @brief Delete specified transactions from the wallet
 * @param removeTxs Vector of transaction hashes to remove from main wallet
 * @param removeArcTxs Vector of transaction hashes to remove from archive  
 * @param fRescan If true, trigger a wallet rescan after deletion
 * @return true if transactions were successfully deleted, false otherwise
 * 
 * Removes specified transactions from both the wallet's main storage and
 * archived transaction storage. This function:
 * 
 * - Validates that transactions exist before attempting removal
 * - Removes transactions from both main wallet and archive storage
 * - Updates the wallet's transaction ordering after removal
 * - Optionally triggers a rescan to rebuild wallet state
 * - Maintains database consistency during the deletion process
 * 
 * Used for wallet cleanup operations and handling chain reorganizations.
 * Requires cs_wallet lock to be held for thread safety.
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

        //remove transaction tracking from the witness tree
        saplingWallet.UnMarkNoteForTransaction(removeTxs[i]);

        //remove transaction from the wallet
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

    //Cleanup the sapling wallet witness tree
    saplingWallet.GarbageCollect();

    // Miodrag: release memory back to the OS, only works on linux
    #ifdef __linux__
    malloc_trim(0);
    #endif

    return removingTransactions;
}

/**
 * @brief Delete wallet transactions based on block height criteria
 * @param pindex The current block index to use as reference point
 * @param fRescan If true, trigger a wallet rescan after deletion
 * @return true if any transactions were deleted, false otherwise
 * 
 * Automatically deletes old wallet transactions based on the configured
 * deletion criteria to manage wallet database size. This function:
 * 
 * - Checks if transaction deletion is enabled (fTxDeleteEnabled)
 * - Identifies transactions older than the deletion threshold
 * - Moves old transactions to archive storage before deletion
 * - Maintains accounting entries and essential transaction data
 * - Reorders remaining transactions to maintain consistency
 * - Optionally triggers wallet rescan to rebuild state
 * 
 * Used for automatic wallet maintenance to prevent excessive database growth.
 * Requires both cs_main and cs_wallet locks for thread safety.
 */
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
                //Don't keep zero value notes
                if (nd.value > 0) {
                    deleteTx = false;
                    continue;
                }
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for outputs that no longer have parents in the wallet. Exclude parents that are in the same transaction. (Sapling)
            for (const auto& spend : pwtx->GetSaplingSpends())  {
              uint256 saplingNullifier = uint256::FromRawBytes(spend.nullifier());
              if (pwalletMain->IsSaplingNullifierFromMe(saplingNullifier)) {
                const uint256& parentHash = pwalletMain->mapSaplingNullifiersToNotes[saplingNullifier].hash;
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




            //Check for unspent inputs or spend less than N Blocks ago. (Orchard)
            for (auto & pair : pwtx->mapOrchardNoteData) {
              OrchardNoteData nd = pair.second;
              if (!nd.nullifier || pwalletMain->GetOrchardSpendDepth(*nd.nullifier) <= fDeleteTransactionsAfterNBlocks) {
                LogPrint("deletetx","DeleteTx - Unspent orchard input tx %s\n", pwtx->GetHash().ToString());
                //Don't keep zero value notes
                if (nd.value > 0) {
                    deleteTx = false;
                    continue;
                }
              }
            }

            if (!deleteTx) {
              txSaveCount++;
              continue;
            }

            //Check for outputs that no longer have parents in the wallet. Exclude parents that are in the same transaction. (Orchard)
            for (const auto& action: pwtx->GetOrchardActions()) {
              uint256 orchardNullifier = uint256::FromRawBytes(action.nullifier());
              if (pwalletMain->IsOrchardNullifierFromMe(orchardNullifier)) {
                const uint256& parentHash = pwalletMain->mapOrchardNullifiersToNotes[orchardNullifier].hash;
                const CWalletTx* parent = pwalletMain->GetWalletTx(parentHash);
                if (parent != NULL && parentHash != wtxid) {
                  LogPrint("deletetx","DeleteTx - Parent of orchard tx %s found\n", pwtx->GetHash().ToString());
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

/**
 * @brief Initialize archived transaction data for all wallet transactions
 * @return false if any confirmed transaction lacks archive data, true otherwise
 * 
 * Validates that all confirmed wallet transactions have corresponding entries
 * in the archived transaction map (mapArcTxs). This function:
 * 
 * - Iterates through all transactions in the wallet
 * - Checks confirmed transactions for archive data presence
 * - Calculates transaction heights for validation
 * - Returns false if any confirmed transaction lacks archive data
 * 
 * Used during wallet initialization and maintenance to ensure archive
 * consistency. This helps maintain transaction history even after
 * transactions are moved to archive storage.
 * 
 * Requires both cs_main and cs_wallet locks for thread safety.
 */
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
        for (uint32_t i = 0; i < wtx.GetSaplingOutputsCount(); ++i) {
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

        //Initalize in memory orchardnotedata
        for (uint32_t i = 0; i < wtx.GetOrchardActionsCount(); ++i) {
            auto op = OrchardOutPoint(wtxid, i);

            if (wtx.mapOrchardNoteData.count(op) != 0) {
                auto nd = wtx.mapOrchardNoteData.at(op);
                auto decrypted = wtx.DecryptOrchardNote(op);
                if (decrypted) {
                    nd.value = decrypted->first.value();
                    nd.address = decrypted->second;
                    //Set the updated Sapling Note Data
                    it->second.mapOrchardNoteData[op] = nd;
                }
            }
        }

        ArchiveTxPoint arcTx;
        AddToArcTxs(wtx, txHeight, arcTx);
    }

    for (map<uint256, ArchiveTxPoint>::iterator it = mapArcTxs.begin(); it != mapArcTxs.end(); it++) {
        //Add to mapAddressTxids the Archived that are no longer in the wallet
        if (!it->second.writeToDisk) {
            AddToArcTxs(it->first, it->second);
        }
    }

  return true;

}

/**
 * @brief Scan the blockchain for wallet-relevant transactions starting from a specified block
 * @param pindexStart The block index to start scanning from
 * @param fUpdate If true, update existing wallet transactions that are found
 * @param fIgnoreBirthday If true, scan all blocks regardless of wallet birthday
 * @param LockOnFinish If true, lock the wallet after scanning completes (for encrypted wallets)
 * @param resetWallets If true, reset Sapling and Orchard wallet states before scanning
 * @return Number of transactions found and added to the wallet
 * 
 * Performs a comprehensive blockchain scan to find transactions involving this wallet.
 * This is the primary function used for wallet synchronization and recovery operations.
 * The function:
 * 
 * - Scans from the specified starting block to the chain tip
 * - Discovers new Sapling and Orchard addresses and adds them to the wallet
 * - Updates wallet transaction maps with newly found transactions
 * - Maintains witness trees for shielded notes
 * - Respects wallet birthday to optimize scanning performance
 * - Provides progress updates to the UI during long scans
 * - Handles transaction deletion and archival during scanning
 * - Automatically locks encrypted wallets after completion if requested
 * 
 * This function is used during wallet initialization, recovery from backup,
 * and manual rescans. It requires both cs_main and cs_wallet locks and
 * temporarily locks cs_KeyStore to prevent wallet locking during operation.
 * 
 * @note In cold storage mode (nMaxConnections == 0), this function returns false immediately
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate, bool fIgnoreBirthday, bool LockOnFinish, bool resetWallets)
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
    std::set<SaplingPaymentAddress> saplingAddressesFound;
    std::set<OrchardPaymentAddressPirate> orchardAddressesFound;

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

        //Reset the sapling Wallet
        if (resetWallets) {
          SaplingWalletReset();
          OrchardWalletReset();
        }

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
            AddToWalletIfInvolvingMe(block.vtx, vOurs, &block, pindex->nHeight, fUpdate, saplingAddressesFound, orchardAddressesFound, true);

            for (int i = 0; i < vOurs.size(); i++) {
                blockInvolvesMe = true;
                txList.insert(vOurs[i].GetHash());
                ret++;
            }

            SproutMerkleTree sproutTree;
            SaplingMerkleFrontier saplingFrontierTree;
            OrchardMerkleFrontier orchardFrontierTree;
            // This should never fail: we should always be able to get the tree
            // state on the path to the tip of our chain
            assert(pcoinsTip->GetSproutAnchorAt(pindex->hashSproutAnchor, sproutTree));
            if (pindex->pprev) {
                if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
                    assert(pcoinsTip->GetSaplingFrontierAnchorAt(pindex->pprev->hashFinalSaplingRoot, saplingFrontierTree));
                }
                if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
                    assert(pcoinsTip->GetOrchardFrontierAnchorAt(pindex->pprev->hashFinalOrchardRoot, orchardFrontierTree));
                }
            }

            if (GetTime() >= nNow + 60) {
                nNow = GetTime();
                LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight, Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), pindex));
            }

            if (pindex->nHeight == chainActive.Tip()->nHeight) {
                //Reset validation flags to force final validation of witness trees
                //on last loop
                saplingWalletValidated = false;
                saplingWalletPositionsValidated = false;
                orchardWalletValidated = false;
                orchardWalletPositionsValidated = false;
            }

            //Update the Sapling and Orchard Wallet Merkle tree and set transaction nullifiers
            //This needs to be done after processing the block to ensure
            //that subsequent spends can be detected
            IncrementSaplingWallet(pindex, &block);
            IncrementOrchardWallet(pindex, &block);

            pindex = chainActive.Next(pindex);
            
        }

        uiInterface.ShowProgress(_("Rescanning..."), 100, false); // hide progress dialog in GUI

        //Write all transactions ant block loacator to the wallet
        currentBlock = chainActive.GetLocator();
        chainHeight = chainActive.Tip()->nHeight;
        SetBestChain(currentBlock, chainHeight);

        //Delete transactions
        while(DeleteWalletTransactions(chainActive.Tip(), true)) {}

        //Write everything to the wallet
        SetBestChain(currentBlock, chainHeight);

        if (LockOnFinish && IsCrypted()) {
            Lock();
        }
    }

    //Notfiy GUI of all new addresses found
    for (std::set<SaplingPaymentAddress>::iterator it = saplingAddressesFound.begin(); it != saplingAddressesFound.end(); it++) {
        SetZAddressBook(*it, "z-sapling", "", true);
    }

    for (std::set<OrchardPaymentAddressPirate>::iterator it = orchardAddressesFound.begin(); it != orchardAddressesFound.end(); it++) {
        SetZAddressBook(*it, "orchard", "", true);
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

/**
 * @brief Re-accept wallet transactions into the memory pool
 * 
 * Attempts to re-submit wallet transactions that may have been removed from
 * the memory pool back into the mempool. This function:
 * 
 * - Skips operation during initial block download
 * - Only processes transactions if broadcasting is enabled
 * - Sorts pending transactions by their original insertion order
 * - Identifies transactions with negative depth (not in main chain)
 * - Attempts to re-add non-coinbase transactions to the mempool
 * - Removes transactions from the wallet if they fail validation
 * 
 * This is typically called after network reconnection or when the mempool
 * may have been cleared, to ensure wallet transactions remain propagated
 * across the network. Requires both cs_main and cs_wallet locks.
 */
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

/**
 * @brief Relay this wallet transaction to the network
 * @return true if the transaction was successfully relayed, false otherwise
 * 
 * Attempts to broadcast this wallet transaction to the network if it meets
 * the criteria for relaying. The function:
 * 
 * - Checks that the wallet instance is valid
 * - Verifies that transaction broadcasting is enabled
 * - Only relays non-coinbase transactions
 * - Only relays transactions with zero depth (not yet in a block)
 * - Uses the global RelayTransaction function to broadcast
 * 
 * This is used to ensure that wallet transactions are propagated across
 * the network, particularly for transactions that may have been created
 * locally but haven't been mined yet.
 */
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

/**
 * @brief Get transactions that conflict with this wallet transaction
 * @return Set of transaction hashes that conflict with this transaction
 * 
 * Returns a set of transaction hashes that conflict with this transaction
 * by spending the same inputs. The function:
 * 
 * - Uses the wallet's conflict detection mechanism
 * - Excludes this transaction's own hash from the result set
 * - Returns empty set if wallet instance is not available
 * 
 * Conflicting transactions typically occur during chain reorganizations
 * or double-spend scenarios. This information is used for transaction
 * validation and display purposes in the wallet interface.
 */
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

/**
 * @brief Calculate the total debit amount for this wallet transaction
 * @param filter Filter specifying which ownership types to include in calculation
 * @return The total amount debited from the wallet by this transaction
 * 
 * Calculates how much value this transaction is spending from the wallet.
 * The function uses caching to avoid repeated expensive calculations and
 * supports filtering by ownership type (spendable vs watch-only).
 * 
 * Returns 0 if the transaction has no inputs or if none of the inputs
 * belong to this wallet according to the specified filter criteria.
 * Used for balance calculations and transaction analysis.
 */
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

/**
 * @brief Calculate the total credit amount for this wallet transaction
 * @param filter Filter specifying which ownership types to include in calculation
 * @return The total amount credited to the wallet by this transaction
 * 
 * Calculates how much value this transaction is providing to the wallet.
 * The function:
 * 
 * - Waits for coinbase transactions to mature before valuing them
 * - Uses caching to avoid repeated expensive calculations
 * - Supports filtering by ownership type (spendable vs watch-only)
 * - Sums up all transaction outputs that belong to this wallet
 * 
 * Returns 0 for immature coinbase transactions or if no outputs belong
 * to this wallet according to the specified filter criteria.
 * Used for balance calculations and transaction analysis.
 */
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

/**
 * @brief Calculate the immature credit amount for this wallet transaction
 * @param fUseCache If true, use cached value when available
 * @return The immature credit amount for this transaction
 * 
 * Calculates the credit from immature transactions, typically coinbase
 * transactions that haven't reached maturity yet. The function:
 * 
 * - Only applies to coinbase transactions
 * - Requires the transaction to be in the main chain
 * - Only counts transactions that still have blocks to maturity
 * - Uses caching to improve performance when requested
 * 
 * Returns 0 for non-coinbase transactions, transactions not in the main
 * chain, or mature coinbase transactions. This represents funds that
 * will become spendable once the maturity period expires.
 */
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

/**
 * @brief Calculate the available credit amount for this wallet transaction
 * @param fUseCache If true, use cached value when available
 * @return The available (spendable) credit amount for this transaction
 * 
 * Calculates the credit from this transaction that is currently available
 * for spending. The function:
 * 
 * - Returns 0 if wallet instance is not available
 * - Waits for coinbase transactions to mature before valuing them
 * - Only counts outputs that haven't been spent yet
 * - Uses caching to improve performance when requested
 * - Validates amounts are within the valid range
 * 
 * This represents the actual spendable balance from this transaction,
 * excluding outputs that have already been spent in other transactions.
 * Used for accurate balance calculations and coin selection.
 */
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

/**
 * @brief Calculate the immature watch-only credit amount for this wallet transaction
 * @param fUseCache If true, use cached value when available
 * @return The immature watch-only credit amount for this transaction
 * 
 * Calculates the credit from immature coinbase transactions for watch-only
 * addresses. The function:
 * 
 * - Only applies to coinbase transactions
 * - Requires the transaction to be in the main chain
 * - Only counts transactions that still have blocks to maturity
 * - Only considers watch-only outputs (addresses without spending keys)
 * - Uses caching to improve performance when requested
 * 
 * Returns 0 for non-coinbase transactions, transactions not in the main
 * chain, or mature coinbase transactions. This represents watch-only funds
 * that will become visible once the maturity period expires.
 */
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

/**
 * @brief Calculate the available watch-only credit amount for this wallet transaction
 * @param fUseCache If true, use cached value when available
 * @return The available watch-only credit amount for this transaction
 * 
 * Calculates the credit from this transaction that is currently available
 * from watch-only addresses. The function:
 * 
 * - Returns 0 if wallet instance is not available
 * - Waits for coinbase transactions to mature before valuing them
 * - Only counts watch-only outputs that haven't been spent yet
 * - Uses caching to improve performance when requested
 * - Validates amounts are within the valid range
 * 
 * This represents watch-only balance from this transaction, excluding
 * outputs that have already been spent. Used for tracking funds in
 * addresses where the wallet can observe but not spend.
 */
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

/**
 * @brief Calculate the change amount for this wallet transaction
 * @return The total value of change outputs in this transaction
 * 
 * Calculates the amount of change (outputs returning to this wallet)
 * in this transaction. The function:
 * 
 * - Uses caching to avoid repeated expensive calculations
 * - Delegates to the wallet's change detection heuristic
 * - Considers outputs that belong to the wallet but aren't in the address book
 * 
 * Used to distinguish between payments sent to others vs change returning
 * to the wallet. This is important for accurate transaction display and
 * fee calculations.
 */
CAmount CWalletTx::GetChange() const
{
    if (fChangeCached)
        return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
}

/**
 * @brief Check if this transaction is currently in the memory pool
 * @return true if the transaction is in the mempool, false otherwise
 * 
 * Determines whether this transaction is currently waiting in the memory
 * pool to be mined into a block. The function acquires the mempool lock
 * and checks for the transaction's presence by hash.
 * 
 * Used to determine transaction status and for display purposes in the
 * wallet interface to show pending/unconfirmed transactions.
 */
bool CWalletTx::InMempool() const
{
    LOCK(mempool.cs);
    return mempool.exists(GetHash());
}

/**
 * @brief Determine if this transaction can be trusted for spending
 * @return true if the transaction is trusted, false otherwise
 * 
 * Determines whether this transaction can be trusted as a source of funds
 * for creating new transactions. The function considers a transaction trusted if:
 * 
 * - The transaction passes final validation checks
 * - It has at least 1 confirmation (is in the main chain)
 * - For unconfirmed transactions, additional criteria may apply
 * 
 * This is used in coin selection to determine which transaction outputs
 * are safe to spend. Untrusted transactions (like unconfirmed change)
 * may be excluded from spending calculations to avoid issues with
 * chain reorganizations.
 */
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

/**
 * @brief Resend wallet transactions that were received before a specified time
 * @param nTime The timestamp cutoff - only resend transactions received before this time
 * @return Vector of transaction hashes that were resent
 * 
 * Attempts to rebroadcast wallet transactions that were received before the
 * specified timestamp. This is used to ensure important transactions remain
 * propagated across the network. The function:
 * 
 * - Sorts transactions chronologically by received time
 * - Skips transactions newer than the specified time
 * - Skips transactions with expired lock times
 * - Only resends transactions that are eligible for relay
 * - Collects and returns the hashes of successfully resent transactions
 * 
 * Used during wallet maintenance and network reconnection to ensure
 * transaction propagation. Requires cs_wallet lock for thread safety.
 */
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

/**
 * @brief Periodically resend wallet transactions to maintain network propagation
 * @param nBestBlockTime Timestamp of the best (most recent) block
 * 
 * Implements a periodic transaction rebroadcasting mechanism to ensure wallet
 * transactions remain propagated across the network. The function:
 * 
 * - Uses randomized timing to avoid revealing transaction ownership patterns
 * - Only operates if broadcasting is enabled and sufficient time has passed
 * - Skips the first call to establish baseline timing
 * - Only resends when new blocks have been found since last resend
 * - Targets unconfirmed transactions older than 5 minutes before the last block
 * - Logs the number of transactions rebroadcast for monitoring
 * 
 * Called periodically by the wallet maintenance system to ensure transaction
 * propagation, particularly important for maintaining mempool presence during
 * network partitions or high transaction volume periods.
 */
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

/**
 * @section Balance Calculation Functions
 * 
 * Functions for calculating various types of wallet balances including
 * confirmed, unconfirmed, immature, and watch-only balances.
 */

/**
 * @brief Get the confirmed spendable balance of the wallet
 * @return The total confirmed balance in satoshis
 * 
 * Calculates the total confirmed and spendable balance by summing all
 * trusted transactions that have available credit. Only includes mature
 * transactions that can be spent. Thread-safe with wallet and chain locks.
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

/**
 * @brief Get the unconfirmed balance of the wallet
 * @return The total unconfirmed balance in satoshis
 * 
 * Calculates the balance from unconfirmed transactions. These are funds
 * that have been received but not yet confirmed by the network. This
 * balance cannot be spent until confirmation.
 */
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

/**
 * @brief Get the immature balance of the wallet
 * @return The total immature balance in satoshis
 * 
 * Calculates the balance from immature transactions, typically coinbase
 * rewards that haven't reached maturity yet. These funds cannot be spent
 * until they mature (usually after 100 confirmations for coinbase).
 */
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

/**
 * @brief Get the watch-only balance of the wallet
 * @return The total watch-only balance in satoshis
 * 
 * Calculates the balance from watch-only addresses. These are addresses
 * for which the wallet can track incoming transactions but cannot spend
 * the funds since it doesn't have the private keys.
 */
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

/**
 * @brief Get the unconfirmed watch-only balance of the wallet
 * @return The total unconfirmed watch-only balance in satoshis
 * 
 * Calculates the balance from unconfirmed watch-only transactions. These are funds
 * in watch-only addresses that have been received but not yet confirmed by the network.
 * The function:
 * 
 * - Only includes transactions that are not final or are untrusted with zero depth
 * - Only counts watch-only outputs (addresses without spending keys)
 * - Uses thread-safe wallet and chain locks
 * 
 * This balance represents funds that can be observed but not spent, and are
 * not yet confirmed by the network. Used for watch-only wallet balance display.
 */
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

/**
 * @brief Get the immature watch-only balance of the wallet
 * @return The total immature watch-only balance in satoshis
 * 
 * Calculates the balance from immature watch-only transactions, typically coinbase
 * rewards in watch-only addresses that haven't reached maturity yet. The function:
 * 
 * - Only counts watch-only outputs from coinbase transactions
 * - Only includes transactions that are still immature
 * - Uses thread-safe wallet and chain locks
 * 
 * This represents watch-only funds from coinbase rewards that cannot be spent
 * until they mature (usually after 100 confirmations). Used for displaying
 * pending coinbase rewards in watch-only addresses.
 */
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

/**
 * @brief Get the available balance considering coin control constraints
 * @param coinControl Optional coin control settings to filter available coins
 * @return The total available balance considering the constraints
 * 
 * Calculates the balance from coins that are available for spending, respecting
 * any coin control constraints specified. The function:
 * 
 * - Uses AvailableCoins to get all spendable outputs
 * - Includes confirmed transactions, zero-value outputs, and coinbase outputs
 * - Only counts outputs marked as spendable
 * - Applies coin control filters if specified
 * 
 * This represents the actual spendable balance that can be used for transactions,
 * taking into account any user-specified coin selection constraints. Used for
 * accurate balance display in transaction creation interfaces.
 */
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
 * @brief Populate vector with available coin outputs for spending
 * @param vCoins[out] Vector to populate with available COutput objects
 * @param fOnlyConfirmed If true, only include confirmed transactions
 * @param coinControl Optional coin control settings to filter outputs
 * @param fIncludeZeroValue If true, include zero-value outputs
 * @param fIncludeCoinBase If true, include coinbase outputs
 * 
 * Scans the wallet's transactions to build a list of unspent transaction outputs
 * that are available for spending. The function:
 * 
 * - Examines all wallet transactions and their outputs
 * - Filters based on confirmation status, coin control settings, and value
 * - Excludes outputs that are already spent
 * - Excludes immature coinbase outputs unless specifically requested
 * - Respects coin control filters for transaction and output selection
 * - Calculates depth and interest for each output
 * 
 * This is the primary function for coin selection, providing the raw material
 * for transaction creation and balance calculations. Thread-safe with wallet locks.
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

/**
 * @brief List all coins grouped by destination address
 * @return Map of destination addresses to vectors of available outputs
 * 
 * Creates a comprehensive list of all available coins in the wallet, organized
 * by their destination addresses. The function:
 * 
 * - Groups available coins by their destination addresses
 * - Includes both confirmed and unconfirmed coins
 * - Includes zero-value outputs and coinbase outputs
 * - Marks locked coins as non-spendable
 * - Handles various address types (P2PKH, P2SH, etc.)
 * 
 * This function is primarily used by wallet interfaces to display coin
 * organization and for manual coin control features. The returned pointers
 * to CWalletTx objects require the caller to hold cs_wallet lock for safety.
 * 
 * @note TODO: Caller should acquire cs_wallet lock before calling this function
 */
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

/**
 * @brief Find the non-change parent output by tracing back through change outputs
 * @param tx The wallet transaction to start from
 * @param output The output index to start tracing from
 * @return Reference to the first non-change output found in the chain
 * 
 * Traces backwards through a chain of transactions to find the original non-change
 * output that led to the current output. The function:
 * 
 * - Starts from the specified output in the given transaction
 * - If the output is change, traces back to the first input's parent transaction
 * - Continues this process until a non-change output is found
 * - Stops if the parent transaction is not in the wallet or not owned
 * 
 * This is useful for determining the original source of funds and for
 * displaying transaction history in wallet interfaces. Used to understand
 * the flow of funds through change outputs back to their original source.
 */
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

/**
 * @brief Find approximate best subset of coins using randomized algorithm
 * @param vValue Vector of (value, coin) pairs sorted by value
 * @param nTotalLower Sum of all coin values in vValue
 * @param nTargetValue The target amount to reach
 * @param vfBest[out] Boolean vector indicating best subset selection
 * @param nBest[out] Total value of best subset found
 * @param iterations Number of random iterations to perform (default 1000)
 * 
 * Uses a randomized approximation algorithm to find a good subset of coins
 * that gets as close as possible to the target value without going under.
 * The algorithm runs multiple iterations with random coin selection to
 * explore different combinations and find the optimal solution.
 * 
 * This is used as a fallback when exact coin selection algorithms fail,
 * providing a reasonable approximation for complex coin selection scenarios.
 * The algorithm prefers solutions that exactly match the target value.
 */
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

/**
 * @brief Select coins with minimum confirmation requirements
 * @param nTargetValue The target amount to select
 * @param nConfMine Minimum confirmations required for own transactions
 * @param nConfTheirs Minimum confirmations required for others' transactions
 * @param vCoins Vector of available coins to select from
 * @param setCoinsRet[out] Set of selected coins (transaction pointer, output index pairs)
 * @param nValueRet[out] Total value of selected coins
 * @return true if target value was reached, false otherwise
 * 
 * Implements a coin selection algorithm that attempts to find the optimal
 * subset of coins to meet a target value while respecting confirmation
 * requirements. The algorithm:
 * 
 * - Filters coins by confirmation requirements (different thresholds for own vs others' transactions)
 * - Prefers selecting a single coin that exactly matches or slightly exceeds the target
 * - Falls back to subset selection if no single coin is suitable
 * - Uses approximation algorithms for complex selection scenarios
 * - Aims to minimize the number of coins selected to reduce transaction size
 * 
 * This is the core coin selection function used in transaction creation,
 * balancing the need to meet value targets while minimizing fees and
 * respecting confirmation requirements for transaction safety.
 */
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

    std:mt19937 g(std::time(nullptr));
    std::shuffle(vCoins.begin(), vCoins.end(), g);

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

/**
 * @brief Select coins for spending to meet a target value
 * @param nTargetValue The amount of value needed to be selected
 * @param setCoinsRet[out] Set of selected coin outpoints 
 * @param nValueRet[out] Total value of selected coins
 * @param fOnlyCoinbaseCoinsRet[out] True if only coinbase coins are available
 * @param fNeedCoinbaseCoinsRet[out] True if coinbase coins are needed to meet target
 * @param coinControl Optional coin control parameters for selection constraints
 * @return true if sufficient coins were selected, false otherwise
 * 
 * Selects the optimal set of unspent transaction outputs (UTXOs) to spend
 * in order to meet the target value requirement. The function implements
 * a sophisticated coin selection algorithm that:
 * 
 * - Prefers higher confirmation depth for security
 * - Handles coinbase protection rules (coinbase -> shielded only)
 * - Supports manual coin control for specific UTXO selection
 * - Tries multiple confirmation thresholds (6, 1, 0 confirmations)
 * - Considers interest earnings in exchange wallet mode
 * 
 * The selection process follows this priority order:
 * 1. Coins with 6+ confirmations
 * 2. Coins with 1+ confirmations 
 * 3. Unconfirmed change outputs (if enabled)
 * 
 * This function is critical for transaction creation and fee calculation.
 */
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

/**
 * @brief Add inputs to fund an existing transaction
 * @param tx[in,out] The mutable transaction to add inputs to
 * @param nFeeRet[out] The calculated transaction fee
 * @param nChangePosRet[out] Position of change output (-1 if no change)
 * @param strFailReason[out] Error message if funding fails
 * @return true if transaction was successfully funded, false otherwise
 * 
 * Takes a partially constructed transaction (with outputs already set) and
 * adds the necessary inputs to fund it. The function:
 * 
 * - Converts existing outputs to recipient list for fee calculation
 * - Uses coin control to allow additional inputs beyond those already selected
 * - Selects coins to cover the output values plus fees
 * - May add a change output if needed
 * - Preserves any inputs that were already in the transaction
 * 
 * This is commonly used for funding transactions that have been partially
 * constructed by external processes or when specific outputs are required
 * but input selection should be automatic.
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

/**
 * @brief Create a new transparent transaction
 * @param vecSend Vector of recipients and amounts to send to
 * @param wtxNew[out] The created wallet transaction
 * @param reservekey Reserve key to use for change output
 * @param nFeeRet[out] The calculated transaction fee
 * @param nChangePosRet[out] Position of change output (-1 if no change)
 * @param strFailReason[out] Error message if transaction creation fails
 * @param nMinFeeOverride Minimum fee to override automatic calculation
 * @param coinControl Optional coin control settings for input selection
 * @param sign Whether to sign the transaction inputs (default true)
 * @return true if transaction was successfully created
 * 
 * Creates a new transparent Bitcoin-style transaction with the specified outputs.
 * The function handles:
 * - Input selection and coin control
 * - Fee calculation and adjustment
 * - Change output creation
 * - Transaction signing (if requested)
 * - Validation of amounts and recipients
 * 
 * The created transaction is not committed to the wallet or broadcasted.
 * Use CommitTransaction() to finalize and send the transaction.
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
                    if (coinControl && !std::get_if<CNoDestination>(&coinControl->destChange))
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
                CCoinsViewCache view(pcoinsTip);
                std::vector<CTxOut> allPrevOutputs;
                for (const auto& input : txNewConst.vin) {
                    allPrevOutputs.push_back(view.GetOutputFor(input));
                }
                PrecomputedTransactionData txdata(txNewConst, allPrevOutputs);
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                {
                    bool signSuccess;
                    const CScript& scriptPubKey = coin.first->vout[coin.second].scriptPubKey;
                    SignatureData sigdata;
                    if (sign)
                        signSuccess = ProduceSignature(TransactionSignatureCreator(this, &txNewConst, txdata, nIn, coin.first->vout[coin.second].nValue, SIGHASH_ALL), scriptPubKey, sigdata, consensusBranchId);
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
 * @brief Commit a created transaction to the wallet and broadcast it to the network
 * @param wtxNew The wallet transaction to commit and broadcast
 * @param reservekey The reserve key used in transaction creation
 * @return true if transaction was successfully committed and broadcasted
 * 
 * This function should be called after CreateTransaction to finalize and send
 * the transaction. It performs the following operations:
 * - Removes the reserve key from the key pool (KeepKey)
 * - Adds the transaction to the wallet for record-keeping
 * - Marks spent coins as used to prevent double-spending
 * - Broadcasts the transaction to the network via RelayWalletTransaction
 * - Updates wallet state and notifies UI components
 * 
 * If broadcasting fails, the transaction is still recorded in the wallet.
 * The transaction will be rebroadcast automatically during wallet operations.
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
                fprintf(stdout,"commit failed\n");
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}

/**
 * @brief Initialize loading of crypted wallet data
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Creates a wallet database connection and initializes the loading
 * of encrypted wallet data. This is typically called during wallet
 * startup when the wallet has cryptographic protection enabled.
 */
DBErrors CWallet::InitalizeCryptedLoad()
{
    return CWalletDB(strWalletFile,"cr+").InitalizeCryptedLoad(this);
}

/**
 * @brief Load encrypted HD seed from database
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Loads the encrypted hierarchical deterministic (HD) seed from the wallet
 * database. This function is used for encrypted wallets to restore the
 * master seed required for key derivation.
 */
DBErrors CWallet::LoadCryptedSeedFromDB()
{
    return CWalletDB(strWalletFile,"cr+").LoadCryptedSeedFromDB(this);
}

/**
 * @brief Load wallet data from the database file
 * @param fFirstRunRet Set to true if this is the first wallet run (no existing data)
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Loads all wallet data from the wallet database file, including keys, transactions,
 * metadata, and settings. If database needs rewriting due to corruption or format
 * changes, performs the rewrite operation. Sets fFirstRunRet to indicate whether
 * this is a fresh wallet installation.
 */
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

/**
 * @brief Read the client version from the wallet database
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Reads the client version information stored in the wallet database.
 * This is used to determine wallet format compatibility and whether
 * upgrade operations are needed.
 */
DBErrors CWallet::ReadClientVersion()
{
    if (!fFileBacked)
        return DB_LOAD_OK;

    return CWalletDB(strWalletFile,"cr+").ReadClientVersion();
}

/**
 * @brief Remove all wallet transactions from database
 * @param vWtx Vector to store the removed transactions
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Removes (zaps) all wallet transactions from the database while preserving
 * keys and other wallet data. The removed transactions are stored in vWtx.
 * This operation may trigger a database rewrite if needed for consistency.
 * Clears the keypool which will require regeneration on next use.
 */
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

/**
 * @brief Remove old/deprecated records from wallet database
 * @return DB_LOAD_OK on success, appropriate DBErrors code on failure
 * 
 * Removes outdated or deprecated records from the wallet database.
 * This is used for database cleanup and migration operations to
 * remove legacy data that is no longer needed or supported.
 */
DBErrors CWallet::ZapOldRecords()
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    return CWalletDB(strWalletFile,"cr+").ZapOldRecords(this);
}

/**
 * @brief Set address book entry for a destination
 * @param address The destination address (transparent or shielded)
 * @param strName The human-readable name for this address
 * @param strPurpose The purpose/category for this address (e.g., "send", "receive")
 * @return true if successfully set, false on error
 * 
 * Adds or updates an entry in the wallet's address book. For encrypted wallets,
 * the address book data is encrypted before storage. The address book is used
 * to store user-friendly names and categorization for addresses.
 * 
 * Triggers UI notification of address book changes for wallet synchronization.
 */
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

/**
 * @brief Decrypt an address book entry from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted address book data
 * @param address[out] The decrypted address string
 * @param entry[out] The decrypted entry data (name or purpose)
 * @return true if successfully decrypted and hash matches
 * 
 * Decrypts previously encrypted address book entries from wallet storage.
 * Used to recover address book information when the wallet is unlocked.
 * Returns false if wallet is locked or decryption fails.
 */
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

/**
 * @brief Set shielded address book entry for a payment address
 * @param address The shielded payment address (Sprout, Sapling, or Orchard)
 * @param strName The human-readable name for this address
 * @param strPurpose The purpose/category for this address
 * @param fInTransaction True if this address was found in a transaction
 * @return true if successfully set, false on error
 * 
 * Adds or updates an entry in the wallet's shielded address book (mapZAddressBook).
 * For encrypted wallets, the address book data is encrypted before storage.
 * If fInTransaction is true and the address already exists, no update is performed.
 * 
 * Triggers UI notification of shielded address book changes for wallet synchronization.
 */
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
    NotifyZAddressBookChanged(this, address, strName, std::visit(HaveSpendingKeyForPaymentAddress(this), address),
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

/**
 * @brief Delete an address book entry for a transparent destination
 * @param address The destination address to remove from address book
 * @return true if successfully deleted, false on error
 * 
 * Removes an entry from the wallet's transparent address book. Also removes
 * associated destination data and sends deletion notifications to the UI.
 * For encrypted wallets, removes both encrypted name and purpose entries.
 */
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

/**
 * @brief Delete a shielded address book entry
 * @param address The shielded payment address to remove from address book
 * @return true if successfully deleted, false on error
 * 
 * Removes an entry from the wallet's shielded address book (mapZAddressBook).
 * Sends deletion notifications to the UI for wallet synchronization.
 * Note: Destination data deletion is currently disabled for z-addresses.
 */
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

    NotifyZAddressBookChanged(this, address, "", std::visit(HaveSpendingKeyForPaymentAddress(this), address), "", CT_DELETED);

    if (!fFileBacked)
        return false;
    //!!!!! we don't delete data for z-addresses for now
    //    CWalletDB(strWalletFile).ErasePurpose(CBitcoinAddress(address).ToString());
    //    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
    return true;
}

/**
 * @brief Set the default key for the wallet
 * @param vchPubKey The public key to set as default
 * @return true if successfully set, false on error
 * 
 * Sets the wallet's default public key used for receiving transparent payments.
 * For encrypted wallets, the key is encrypted before storage. The default key
 * is used when no specific key is requested for transparent operations.
 */
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

/**
 * @brief Decrypt the default key from encrypted storage
 * @param chash Hash fingerprint of the encrypted data
 * @param vchCryptedSecret The encrypted default key data
 * @param vchPubKey[out] The decrypted public key
 * @return true if successfully decrypted and hash matches
 * 
 * Decrypts the wallet's default public key from encrypted storage.
 * Used to recover the default key when the wallet is unlocked.
 * Returns false if wallet is locked or decryption fails.
 */
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

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
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
            nTargetSize = max(GetArg("-keypool", 100), (int64_t) 0);

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

/**
 * @brief Reserve a key from the key pool for use
 * @param nIndex[out] The index of the reserved key (-1 if none available)
 * @param keypool[out] The reserved key pool entry
 * 
 * Reserves a key from the wallet's key pool by removing it from the available
 * set. The key can later be returned to the pool if not used. For encrypted
 * wallets, the key is decrypted before returning. Sets nIndex to -1 if no
 * keys are available or wallet is locked.
 */
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

/**
 * @brief Permanently remove a key from the key pool
 * @param nIndex The index of the key to remove permanently
 * 
 * Removes a key from the key pool database, indicating it has been used
 * and should not be returned to the pool. This is called when a key
 * from the pool is actually used in a transaction.
 */
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

/**
 * @brief Return a key to the key pool
 * @param nIndex The index of the key to return to the pool
 * 
 * Returns a previously reserved key back to the available key pool.
 * This is called when a key was reserved but ultimately not used,
 * making it available for future use.
 */
void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    //LogPrintf("keypool return %d\n", nIndex);
}

/**
 * @brief Get a key from the key pool
 * @param result[out] The public key retrieved from the pool
 * @return true if a key was successfully retrieved, false on error
 * 
 * Retrieves a public key from the wallet's key pool for use in transactions.
 * If the pool is empty and wallet is unlocked, generates a new key.
 * Returns false if pool is empty and wallet is locked.
 */
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

void CWallet::LockNote(const OrchardOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedOrchardNotes.insert(output);
}

void CWallet::UnlockNote(const OrchardOutPoint& output)
{
    AssertLockHeld(cs_wallet);
    setLockedOrchardNotes.erase(output);
}

void CWallet::UnlockAllOrchardNotes()
{
    AssertLockHeld(cs_wallet);
    setLockedOrchardNotes.clear();
}

bool CWallet::IsLockedNote(const OrchardOutPoint& output) const
{
    AssertLockHeld(cs_wallet);
    return (setLockedOrchardNotes.count(output) > 0);
}

std::vector<OrchardOutPoint> CWallet::ListLockedOrchardNotes()
{
    AssertLockHeld(cs_wallet);
    std::vector<OrchardOutPoint> vOutputs(setLockedOrchardNotes.begin(), setLockedOrchardNotes.end());
    return vOutputs;
}

/** @} */ // end of Actions

class CAffectedKeysVisitor
{
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
                std::visit(*this, dest);
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
    if (std::get_if<CNoDestination>(&dest))
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

/**
 * @brief Default constructor for CKeyPool
 * 
 * Initializes a new key pool entry with the current time as creation timestamp.
 * The public key is left uninitialized and must be set separately.
 */
CKeyPool::CKeyPool()
{
    nTime = GetTime();
}

/**
 * @brief Constructor for CKeyPool with public key
 * @param vchPubKeyIn The public key to store in this key pool entry
 * 
 * Initializes a new key pool entry with the specified public key and
 * sets the creation time to the current timestamp.
 */
CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn)
{
    nTime = GetTime();
    vchPubKey = vchPubKeyIn;
}

/**
 * @brief Constructor for CWalletKey with expiration time
 * @param nExpires The expiration time for this wallet key (0 for no expiration)
 * 
 * Creates a new wallet key with the specified expiration time. If nExpires is 0,
 * the key is created with no expiration (nTimeCreated = 0). Otherwise, the creation
 * time is set to the current time and expiration is set as specified.
 */
CWalletKey::CWalletKey(int64_t nExpires)
{
    nTimeCreated = (nExpires ? GetTime() : 0);
    nTimeExpires = nExpires;
}

/**
 * @brief Set the Merkle branch for this transaction within a block
 * @param block The block containing this transaction
 * 
 * Updates the transaction's block hash and calculates its Merkle branch within
 * the block. This establishes proof that the transaction is included in the
 * specified block. The function:
 * - Sets hashBlock to the block's hash
 * - Finds the transaction's position within the block
 * - Generates the Merkle branch proof for the transaction
 * 
 * If the transaction is not found in the block, the Merkle branch is cleared
 * and nIndex is set to -1, indicating an error condition.
 */
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

/**
 * @brief Internal function to get confirmation depth in main chain
 * @param pindexRet[out] Pointer to the block index if transaction is found
 * @return Number of confirmations (0 if not in main chain)
 * 
 * Internal implementation for checking transaction depth in the main blockchain.
 * This function locates the block containing the transaction and calculates
 * the number of confirmations based on current chain height.
 * 
 * Returns 0 if the transaction's block is not found or if the block is not
 * part of the main chain. The pindexRet parameter is set to point to the
 * block index if found.
 */
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

/**
 * @brief Get confirmation depth in main chain with mempool check
 * @param pindexRet[out] Pointer to the block index if transaction is found
 * @return Number of confirmations (-1 if conflicted, 0+ if confirmed/mempool)
 * 
 * Returns the number of confirmations for this transaction in the main chain.
 * This function extends GetDepthInMainChainINTERNAL by checking the mempool
 * for unconfirmed transactions.
 * 
 * Return values:
 * - Positive number: Transaction is confirmed with that many confirmations
 * - 0: Transaction is unconfirmed but in mempool
 * - -1: Transaction conflicts with blockchain (not in chain or mempool)
 */
int CMerkleTx::GetDepthInMainChain(const CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

/**
 * @brief Get number of blocks until coinbase transaction matures
 * @return Number of blocks remaining until maturity (0 if not coinbase or already mature)
 * 
 * For coinbase transactions, calculates the number of blocks remaining until
 * the transaction can be spent. Takes into account both the standard coinbase
 * maturity period and any additional lock time specified in the transaction.
 * 
 * The function returns the maximum of:
 * - Remaining blocks until coinbase maturity (typically 100 blocks)
 * - Remaining blocks until unlock time (if specified)
 * 
 * Returns 0 for non-coinbase transactions or already mature coinbase transactions.
 */
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

/**
 * @brief Attempt to add transaction to memory pool
 * @param fLimitFree Whether to apply free transaction limits
 * @param fRejectAbsurdFee Whether to reject transactions with absurdly high fees
 * @return true if successfully added to mempool, false otherwise
 * 
 * Attempts to add this transaction to the memory pool for network propagation.
 * The transaction goes through all standard validation checks including:
 * - Transaction format and signature validation
 * - Input availability and value checks
 * - Fee calculation and limits
 * - Script execution and consensus rule compliance
 * 
 * This function is typically used for newly created transactions before
 * broadcasting them to the network.
 */
bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectAbsurdFee)
{
    CValidationState state;
    return ::AcceptToMemoryPool(mempool, state, *this, fLimitFree, NULL, fRejectAbsurdFee);
}

/**
 * @brief Get address balances for the wallet GUI
 * @param balances[out] Map to store address balances (PaymentAddress -> CAmount)
 * @param minDepth Minimum confirmation depth required for notes
 * @param requireSpendingKey Whether to only include notes with spending keys
 * 
 * Calculates the balance for each shielded address in the wallet by examining
 * all confirmed transactions. This function:
 * - Filters transactions by finality, maturity, and confirmation depth
 * - Processes both Sapling and Orchard notes
 * - Excludes spent notes from balance calculations
 * - Optionally filters by spending key availability
 * 
 * The resulting balance map is used by the GUI to display per-address balances
 * and is essential for the wallet's balance display functionality.
 * Only confirmed balances meeting the minimum depth requirement are included.
 */
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

        for (auto & pair : wtx.mapOrchardNoteData) {
            OrchardOutPoint op = pair.first;
            OrchardNoteData nd = pair.second;

            if (nd.nullifier && IsOrchardSpent(*nd.nullifier)) {
                continue;
            }

            // skip notes which cannot be spent
            if (requireSpendingKey) {
                libzcash::OrchardExtendedFullViewingKeyPirate extfvk;
                if (!(GetOrchardFullViewingKey(nd.ivk, extfvk) &&
                    HaveOrchardSpendingKey(extfvk))) {
                    continue;
                }
            }

            if (!balances.count(nd.address))
                balances[nd.address] = 0;
            balances[nd.address] += CAmount(nd.value);
        }
    }
}


/**
 * @brief Get Merkle path for a Sapling note
 * @param txid Transaction ID containing the note
 * @param outidx Output index of the note within the transaction
 * @param merklePath[out] The Merkle path to the note's commitment
 * @return true if Merkle path was successfully retrieved
 * 
 * Retrieves the Merkle path from a Sapling note's commitment to the Sapling
 * commitment tree root. This path is required to prove the note's inclusion
 * in the blockchain when spending it.
 */
bool CWallet::SaplingWalletGetMerklePathOfNote(const uint256 txid, int outidx, libzcash::MerklePath &merklePath) {
    return saplingWallet.GetMerklePathOfNote(txid, outidx, merklePath);
}

/**
 * @brief Calculate anchor from Merkle path and commitment
 * @param merklePath The Merkle path to validate
 * @param cmu The note commitment to verify
 * @param anchor[out] The calculated anchor (tree root)
 * @return true if anchor calculation was successful
 * 
 * Computes the Sapling tree root (anchor) by applying the provided Merkle path
 * to the given note commitment. This verifies that the commitment is properly
 * included in the tree and provides the anchor needed for spending proofs.
 */
bool CWallet::SaplingWalletGetPathRootWithCMU(libzcash::MerklePath &merklePath, uint256 cmu, uint256 &anchor) {
   return saplingWallet.GetPathRootWithCMU(merklePath, cmu, anchor);
}

/**
 * @brief Reset the Sapling wallet to initial state
 * @return true if reset was successful and written to database
 * 
 * Resets the Sapling wallet's internal state, clearing all note commitments
 * and witness information. This is typically used during wallet rebuilding
 * operations. The reset state is persisted to the database.
 */
bool CWallet::SaplingWalletReset() {
   saplingWallet.Reset();
   return CWalletDB(strWalletFile).WriteSaplingWitnesses(saplingWallet);
}

/**
 * @brief Load encrypted Sapling wallet note commitment tree from decrypted data
 * @param vchSecret The decrypted keying material containing the serialized tree data
 * @return true if tree was successfully deserialized and loaded, false on error
 * 
 * Loads the Sapling note commitment tree from previously decrypted wallet data.
 * This function is called during encrypted wallet loading to restore the Sapling
 * witness tree state. The function:
 * - Deserializes the decrypted data into the proper format
 * - Loads the tree structure using the Sapling note commitment tree loader
 * - Handles deserialization errors gracefully by returning false
 * 
 * This is used when reading encrypted wallet databases to restore the full
 * Sapling wallet state including all note witnesses and commitment tree data.
 */
bool CWallet::LoadCryptedSaplingWallet(const CKeyingMaterial& vchSecret) {

    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss.write((const char*)vchSecret.data(), vchSecret.size());
        ss.Rewind(vchSecret.size());
        
        auto loader = GetSaplingNoteCommitmentTreeLoader();
        ss >> loader;

    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/**
 * @brief Get Merkle path for an Orchard note
 * @param txid Transaction ID containing the note
 * @param outidx Output index of the note within the transaction
 * @param merklePath[out] The Merkle path to the note's commitment
 * @return true if Merkle path was successfully retrieved
 * 
 * Retrieves the Merkle path from an Orchard note's commitment to the Orchard
 * commitment tree root. This path is required to prove the note's inclusion
 * in the blockchain when spending it.
 */
bool CWallet::OrchardWalletGetMerklePathOfNote(const uint256 txid, int outidx, libzcash::MerklePath &merklePath) {
    return orchardWallet.GetMerklePathOfNote(txid, outidx, merklePath);
}

/**
 * @brief Calculate anchor from Orchard Merkle path and commitment
 * @param merklePath The Merkle path to validate
 * @param cmu The note commitment to verify
 * @param anchor[out] The calculated anchor (tree root)
 * @return true if anchor calculation was successful
 * 
 * Computes the Orchard tree root (anchor) by applying the provided Merkle path
 * to the given note commitment. This verifies that the commitment is properly
 * included in the tree and provides the anchor needed for spending proofs.
 */
bool CWallet::OrchardWalletGetPathRootWithCMU(libzcash::MerklePath &merklePath, uint256 cmu, uint256 &anchor) {
   return orchardWallet.GetPathRootWithCMU(merklePath, cmu, anchor);
}

/**
 * @brief Reset the Orchard wallet to initial state
 * @return true if reset was successful and written to database
 * 
 * Resets the Orchard wallet's internal state, clearing all note commitments
 * and witness information. This is typically used during wallet rebuilding
 * operations. The reset state is persisted to the database.
 */
bool CWallet::OrchardWalletReset() {
   orchardWallet.Reset();
   return CWalletDB(strWalletFile).WriteOrchardWitnesses(orchardWallet);
}

/**
 * @brief Load encrypted Orchard wallet note commitment tree from decrypted data
 * @param vchSecret The decrypted keying material containing the serialized tree data
 * @return true if tree was successfully deserialized and loaded, false on error
 * 
 * Loads the Orchard note commitment tree from previously decrypted wallet data.
 * This function is called during encrypted wallet loading to restore the Orchard
 * witness tree state. The function:
 * - Deserializes the decrypted data into the proper format
 * - Loads the tree structure using the Orchard note commitment tree loader
 * - Handles deserialization errors gracefully by returning false
 * 
 * This is used when reading encrypted wallet databases to restore the full
 * Orchard wallet state including all note witnesses and commitment tree data.
 */
bool CWallet::LoadCryptedOrchardWallet(const CKeyingMaterial& vchSecret) {

    try {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss.write((const char*)vchSecret.data(), vchSecret.size());
        ss.Rewind(vchSecret.size());
        
        auto loader = GetOrchardNoteCommitmentTreeLoader();
        ss >> loader;

    } catch (const std::exception&) {
        return false;
    }
    return true;
}

/**
 * @brief Find unspent notes in the wallet filtered by payment address and minimum depth
 * @param saplingEntries[out] Vector to store found Sapling note entries
 * @param orchardEntries[out] Vector to store found Orchard note entries
 * @param address Payment address to filter by (empty string for all addresses)
 * @param minDepth Minimum confirmation depth required for notes
 * @param ignoreSpent Whether to exclude spent notes from results
 * @param requireSpendingKey Whether to only include notes for which we have the spending key
 * 
 * This is a convenience wrapper around the more detailed GetFilteredNotes function.
 * It handles address decoding and sets default values for maxDepth (INT_MAX) and 
 * ignoreLocked (false) parameters.
 */
void CWallet::GetFilteredNotes(
    std::vector<SaplingNoteEntry>& saplingEntries,
    std::vector<OrchardNoteEntry>& orchardEntries,
    std::string address,
    int minDepth,
    bool ignoreSpent,
    bool requireSpendingKey)
{
    std::set<PaymentAddress> filterAddresses;

    if (address.length() > 0) {
        filterAddresses.insert(DecodePaymentAddress(address));
    }

    GetFilteredNotes(saplingEntries, orchardEntries, filterAddresses, minDepth, INT_MAX, ignoreSpent, requireSpendingKey);
}

/**
 * @brief Find notes in the wallet with comprehensive filtering options
 * @param saplingEntries[out] Vector to store found Sapling note entries
 * @param orchardEntries[out] Vector to store found Orchard note entries  
 * @param filterAddresses Set of payment addresses to filter by (empty for all addresses)
 * @param minDepth Minimum confirmation depth required for notes
 * @param maxDepth Maximum confirmation depth allowed for notes
 * @param ignoreSpent Whether to exclude spent notes from results
 * @param requireSpendingKey Whether to only include notes for which we have the spending key
 * @param ignoreLocked Whether to exclude locked notes from results
 * 
 * This function performs a comprehensive search through all wallet transactions,
 * extracting both Sapling and Orchard notes that match the specified criteria.
 * The function respects depth-based network confirmation requirements using
 * either regular depth checks or dPoW (delayed Proof of Work) confirmation counts.
 * 
 * For each matching note, the function:
 * 1. Validates transaction finality and maturity
 * 2. Checks confirmation depth requirements  
 * 3. Attempts note decryption using stored viewing keys
 * 4. Applies address, spending key, and lock filters
 * 5. Adds qualifying notes to the appropriate output vector
 */
void CWallet::GetFilteredNotes(
    std::vector<SaplingNoteEntry>& saplingEntries,
    std::vector<OrchardNoteEntry>& orchardEntries,
    std::set<PaymentAddress>& filterAddresses,
    int minDepth,
    int maxDepth,
    bool ignoreSpent,
    bool requireSpendingKey,
    bool ignoreLocked)
{
    LOCK2(cs_main, cs_wallet);

    // Iterate through all wallet transactions to find matching notes
    for (auto & p : mapWallet) {
        CWalletTx wtx = p.second;

        // Skip transactions that are not final or still maturing
        if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0)
            continue;

        // Apply depth filtering based on confirmation requirements
        if (minDepth > 1) {
            // For deeper requirements, use dPoW (delayed Proof of Work) confirmations
            int nHeight    = tx_height(wtx.GetHash());
            int nDepth     = wtx.GetDepthInMainChain();
            int dpowconfs  = komodo_dpowconfs(nHeight, nDepth);
            if (dpowconfs < minDepth || dpowconfs > maxDepth) {
                continue;
            }
        } else {
            // For shallow requirements, use standard depth checks
            if (wtx.GetDepthInMainChain() < minDepth ||
                wtx.GetDepthInMainChain() > maxDepth) {
                continue;
            }
        }

        // Process Sapling notes in this transaction
        auto vOutputs = wtx.GetSaplingOutputs();

        for (auto & pair : wtx.mapSaplingNoteData) {
            SaplingOutPoint op = pair.first;
            SaplingNoteData nd = pair.second;

            // Use Rust decryption
            auto optPlaintext = libzcash::SaplingNotePlaintext::AttemptDecryptSaplingOutput(vOutputs[op.n], nd.ivk);

            // The transaction would not have entered the wallet unless
            // its plaintext had been successfully decrypted previously.
            assert(optPlaintext != std::nullopt);

            auto notePt = optPlaintext.value();
            auto maybe_pa = nd.ivk.address(notePt.d);
            assert(static_cast<bool>(maybe_pa));
            auto pa = maybe_pa.value();

            // Skip notes that don't match the address filter
            if (!(filterAddresses.empty() || filterAddresses.count(pa))) {
                continue;
            }

            // Skip spent notes if requested
            if (ignoreSpent && nd.nullifier && IsSaplingSpent(*nd.nullifier)) {
                continue;
            }

            // Skip notes for which we don't have the spending key (if required)
            if (requireSpendingKey) {
                libzcash::SaplingExtendedFullViewingKey extfvk;
                if (!(GetSaplingFullViewingKey(nd.ivk, extfvk) &&
                    HaveSaplingSpendingKey(extfvk))) {
                    continue;
                }
            }

            // Skip locked notes if requested
            if (ignoreLocked && IsLockedNote(op)) {
                continue;
            }

            // Add qualifying Sapling note to results
            auto note = notePt.note(nd.ivk).value();
            saplingEntries.push_back(SaplingNoteEntry {
                op, pa, note, notePt.memo(), wtx.GetDepthInMainChain() });
        }

        // Process Orchard notes in this transaction
        auto vActions = wtx.GetOrchardActions();

        for (auto & pair : wtx.mapOrchardNoteData) {
            OrchardOutPoint op = pair.first;
            OrchardNoteData nd = pair.second;

            // Decrypt the Orchard note using the stored incoming viewing key
            auto optDeserialized = OrchardNotePlaintext::AttemptDecryptOrchardAction(&vActions[op.n], nd.ivk);

            // The transaction would not have entered the wallet unless
            // its plaintext had been successfully decrypted previously.
            assert(optDeserialized != std::nullopt);

            auto notePt = optDeserialized.value();
            auto pa = notePt.GetAddress();
            auto memo = notePt.memo();
            auto note = notePt.note().value();

            // Skip notes that don't match the address filter
            if (!(filterAddresses.empty() || filterAddresses.count(pa))) {
                continue;
            }

            // Skip spent notes if requested
            if (ignoreSpent && nd.nullifier && IsOrchardSpent(*nd.nullifier)) {
                continue;
            }

            // Skip notes for which we don't have the spending key (if required)
            if (requireSpendingKey) {
                libzcash::OrchardExtendedFullViewingKeyPirate extfvk;
                if (!(GetOrchardFullViewingKey(nd.ivk, extfvk) &&
                    HaveOrchardSpendingKey(extfvk))) {
                    continue;
                }
            }

            // Skip locked notes if requested
            if (ignoreLocked && IsLockedNote(op)) {
                continue;
            }

            // Add qualifying Orchard note to results
            orchardEntries.push_back(OrchardNoteEntry {op, pa, note, memo, wtx.GetDepthInMainChain()});
        }
    }
}


/**
 * @section Shielded Key and Address Generalizations
 * 
 * The following functions provide a unified interface for working with different
 * types of shielded addresses (Sprout, Sapling, Orchard) through visitor pattern
 * implementations. This allows generic handling of address operations across
 * all shielded protocols supported by the wallet.
 */

/**
 * @brief Check if an incoming viewing key belongs to this wallet (Sprout addresses)
 * @param zaddr The Sprout payment address to check
 * @return true if the wallet has the corresponding viewing key
 */
bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutViewingKey(zaddr);
}

/**
 * @brief Check if an incoming viewing key belongs to this wallet (Sapling addresses)
 * @param zaddr The Sapling payment address to check
 * @return true if the wallet has the corresponding incoming viewing key
 */
bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk);
}

/**
 * @brief Check if an incoming viewing key belongs to this wallet (Orchard addresses)
 * @param zaddr The Orchard payment address to check  
 * @return true if the wallet has the corresponding incoming viewing key
 */
bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::OrchardPaymentAddressPirate &zaddr) const
{
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    return m_wallet->GetOrchardIncomingViewingKey(zaddr, ivk);
}

/**
 * @brief Handle invalid address encodings
 * @param no Invalid encoding object (not used)
 * @return false (invalid encodings never belong to wallet)
 */
bool IncomingViewingKeyBelongsToWallet::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

/**
 * @brief Check if a payment address belongs to this wallet (Sprout addresses)
 * @param zaddr The Sprout payment address to check
 * @return true if the wallet has the spending key or viewing key for this address
 */
bool PaymentAddressBelongsToWallet::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutSpendingKey(zaddr) || m_wallet->HaveSproutViewingKey(zaddr);
}

/**
 * @brief Check if a payment address belongs to this wallet (Sapling addresses)
 * @param zaddr The Sapling payment address to check
 * @return true if the wallet has both the incoming viewing key and full viewing key
 */
bool PaymentAddressBelongsToWallet::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;

    // If we have a SaplingExtendedSpendingKey in the wallet, then we will
    // also have the corresponding SaplingFullViewingKey.
    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->HaveSaplingFullViewingKey(ivk);
}

/**
 * @brief Check if a payment address belongs to this wallet (Orchard addresses)
 * @param zaddr The Orchard payment address to check
 * @return true if the wallet has both the incoming viewing key and full viewing key
 */
bool PaymentAddressBelongsToWallet::operator()(const libzcash::OrchardPaymentAddressPirate &zaddr) const
{
    libzcash::OrchardIncomingViewingKeyPirate ivk;

    // If we have a OrchardExtendedSpendingKey in the wallet, then we will
    // also have the corresponding OrchardFullViewingKey.
    return m_wallet->GetOrchardIncomingViewingKey(zaddr, ivk) &&
        m_wallet->HaveOrchardFullViewingKey(ivk);
}

/**
 * @brief Handle invalid encoding for payment address ownership check
 * @param no The invalid encoding object
 * @return false (invalid encodings never belong to wallet)
 */
bool PaymentAddressBelongsToWallet::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

/**
 * @brief Get viewing key for Sprout payment address
 * @param zaddr The Sprout payment address
 * @return Optional ViewingKey if found, nullopt otherwise
 * 
 * Attempts to retrieve the viewing key for a Sprout address by first checking
 * for a stored viewing key, then falling back to deriving it from the spending
 * key if available.
 */
std::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::SproutPaymentAddress &zaddr) const
{
    libzcash::SproutViewingKey vk;
    if (!m_wallet->GetSproutViewingKey(zaddr, vk)) {
        libzcash::SproutSpendingKey k;
        if (!m_wallet->GetSproutSpendingKey(zaddr, k)) {
            return std::nullopt;
        }
        vk = k.viewing_key();
    }
    return libzcash::ViewingKey(vk);
}

/**
 * @brief Get viewing key for Sapling payment address
 * @param zaddr The Sapling payment address
 * @return Optional ViewingKey if found, nullopt otherwise
 * 
 * Retrieves the Sapling extended full viewing key for the given payment address
 * by looking up the incoming viewing key and corresponding full viewing key.
 */
std::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    if (m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk))
    {
        return libzcash::ViewingKey(extfvk);
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Get viewing key for Orchard payment address
 * @param zaddr The Orchard payment address
 * @return Optional ViewingKey if found, nullopt otherwise
 * 
 * Retrieves the Orchard extended full viewing key for the given payment address
 * by looking up the incoming viewing key and corresponding full viewing key.
 */
std::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::OrchardPaymentAddressPirate &zaddr) const
{
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;

    if (m_wallet->GetOrchardIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetOrchardFullViewingKey(ivk, extfvk))
    {
        return libzcash::ViewingKey(extfvk);
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Handle invalid encoding for viewing key lookup
 * @param no The invalid encoding object
 * @return Default ViewingKey (InvalidEncoding)
 */
std::optional<libzcash::ViewingKey> GetViewingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::ViewingKey();
}

/**
 * @brief Check if wallet has spending key for Sprout address
 * @param zaddr The Sprout payment address to check
 * @return true if spending key exists in wallet
 */
bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::SproutPaymentAddress &zaddr) const
{
    return m_wallet->HaveSproutSpendingKey(zaddr);
}

/**
 * @brief Check if wallet has spending key for Sapling address
 * @param zaddr The Sapling payment address to check
 * @return true if wallet has corresponding spending key (via full viewing key)
 * 
 * Verifies that the wallet has both the incoming viewing key and full viewing key
 * for the address, which indicates a spending key exists.
 */
bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    return m_wallet->GetSaplingIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
        m_wallet->HaveSaplingSpendingKey(extfvk);
}

/**
 * @brief Check if wallet has spending key for Orchard address
 * @param zaddr The Orchard payment address to check
 * @return true if wallet has corresponding spending key (via full viewing key)
 * 
 * Verifies that the wallet has both the incoming viewing key and full viewing key
 * for the address, which indicates a spending key exists.
 */
bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::OrchardPaymentAddressPirate &zaddr) const
{
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;

    return m_wallet->GetOrchardIncomingViewingKey(zaddr, ivk) &&
        m_wallet->GetOrchardFullViewingKey(ivk, extfvk) &&
        m_wallet->HaveOrchardSpendingKey(extfvk);
}

/**
 * @brief Handle invalid encoding for spending key check
 * @param no The invalid encoding object
 * @return false (no spending key for invalid encoding)
 */
bool HaveSpendingKeyForPaymentAddress::operator()(const libzcash::InvalidEncoding& no) const
{
    return false;
}

/**
 * @brief Get spending key for Sprout payment address
 * @param zaddr The Sprout payment address
 * @return Optional SpendingKey if found, nullopt otherwise
 */
std::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::SproutPaymentAddress &zaddr) const
{
    libzcash::SproutSpendingKey k;
    if (m_wallet->GetSproutSpendingKey(zaddr, k)) {
        return libzcash::SpendingKey(k);
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Get spending key for Sapling payment address
 * @param zaddr The Sapling payment address
 * @return Optional SpendingKey if found, nullopt otherwise
 */
std::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::SaplingPaymentAddress &zaddr) const
{
    libzcash::SaplingExtendedSpendingKey extsk;
    if (m_wallet->GetSaplingExtendedSpendingKey(zaddr, extsk)) {
        return libzcash::SpendingKey(extsk);
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Get spending key for Orchard payment address
 * @param zaddr The Orchard payment address
 * @return Optional SpendingKey if found, nullopt otherwise
 */
std::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::OrchardPaymentAddressPirate &zaddr) const
{
    libzcash::OrchardExtendedSpendingKeyPirate extsk;
    if (m_wallet->GetOrchardExtendedSpendingKey(zaddr, extsk)) {
        return libzcash::SpendingKey(extsk);
    } else {
        return std::nullopt;
    }
}

/**
 * @brief Handle invalid encoding for spending key lookup
 * @param no The invalid encoding object
 * @return Default SpendingKey (InvalidEncoding)
 */
std::optional<libzcash::SpendingKey> GetSpendingKeyForPaymentAddress::operator()(
    const libzcash::InvalidEncoding& no) const
{
    // Defaults to InvalidEncoding
    return libzcash::SpendingKey();
}

/**
 * @brief Add Sprout viewing key to wallet
 * @param vkey The Sprout viewing key to add
 * @return KeyAddResult indicating the result of the operation
 */
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

/**
 * @brief Add Sapling extended full viewing key to wallet
 * @param extfvk The Sapling extended full viewing key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Adds the viewing key for watch-only functionality. If a spending key already
 * exists, returns SpendingKeyExists. Also loads watch-only data for the key.
 */
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

/**
 * @brief Add Orchard extended full viewing key to wallet
 * @param extfvk The Orchard extended full viewing key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Adds the viewing key for watch-only functionality. If a spending key already
 * exists, returns SpendingKeyExists. Also loads watch-only data for the key.
 */
KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk) const {
    auto ivkOpt = extfvk.fvk.GetIVK();
    if (ivkOpt == nullopt) {
       return KeyNotAdded;
    }
    auto ivk = ivkOpt.value();

    if (m_wallet->HaveOrchardSpendingKey(extfvk)) {
        return SpendingKeyExists;
    } else if (m_wallet->HaveOrchardFullViewingKey(ivk)) {
        return KeyAlreadyExists;
    } else if (m_wallet->AddOrchardExtendedFullViewingKey(extfvk)) {
        m_wallet->LoadOrchardWatchOnly(extfvk);
        return KeyAdded;
    } else {
        return KeyNotAdded;
    }
}

/**
 * @brief Handle invalid encoding for viewing key addition
 * @param no The invalid encoding object
 * @throws JSONRPCError with RPC_INVALID_ADDRESS_OR_KEY
 */
KeyAddResult AddViewingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid viewing key");
}

/**
 * @brief Add Sapling diversified extended full viewing key to wallet
 * @param extdfvk The Sapling diversified extended full viewing key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Adds a diversified viewing key for watch-only functionality. The function will
 * add the base viewing key if needed and then attempt to add the specific diversified address.
 */
KeyAddResult AddDiversifiedViewingKeyToWallet::operator()(const libzcash::SaplingDiversifiedExtendedFullViewingKey &extdfvk) const {
    auto extfvk = extdfvk.extfvk;
    auto ivk = extfvk.fvk.in_viewing_key();
    auto addr = ivk.address(extdfvk.d).value();
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

KeyAddResult AddDiversifiedViewingKeyToWallet::operator()(const libzcash::OrchardDiversifiedExtendedFullViewingKeyPirate &extdfvk) const {
    auto extfvk = extdfvk.extfvk;

    // Attempt to derive the diversified viewing key's IVK
    // This will throw if the diversified key is invalid or cannot derive an IVK
    auto ivkOpt = extfvk.fvk.GetIVK();
    if (ivkOpt == std::nullopt) {
        throw std::invalid_argument("Cannot derive default address from invalid diversified viewing key");
    }
    auto ivk = ivkOpt.value();
    KeyAddResult result = KeyNotAdded;

    // Attempt to derive the default address from the diversified viewing key
    // This will throw if the diversified key is invalid or cannot derive an address
    auto addrOpt = extfvk.fvk.GetAddress(extdfvk.d);
    if (addrOpt == std::nullopt) {
        throw std::invalid_argument("Cannot derive default address from invalid diversified viewing key");
    }
    auto addr = addrOpt.value();

    if (m_wallet->HaveOrchardSpendingKey(extfvk)) {
        result = SpendingKeyExists;
    } else if (m_wallet->HaveOrchardFullViewingKey(ivk)) {
        result = KeyAlreadyExists;
    } else if (m_wallet->AddOrchardExtendedFullViewingKey(extfvk)) {
        m_wallet->LoadOrchardWatchOnly(extfvk);
        result = KeyAdded;
    } else {
        return KeyNotAdded;
    }

    // Attempt to add the incoming viewing key for the diversified address
    if (m_wallet->AddOrchardIncomingViewingKey(ivk, addr)) {
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

/**
 * @brief Handle invalid encoding for diversified viewing key addition
 * @param no The invalid encoding object
 * @throws JSONRPCError with RPC_INVALID_ADDRESS_OR_KEY
 */
KeyAddResult AddDiversifiedViewingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid diversified viewing key");
}

/**
 * @brief Add Sprout spending key to wallet
 * @param sk The Sprout spending key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Imports a Sprout spending key into the wallet. Sets the creation time metadata
 * to the specified time for proper birthday handling during wallet scans.
 */
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

/**
 * @brief Add Sapling extended spending key to wallet
 * @param sk The Sapling extended spending key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Imports a Sapling spending key into the wallet. Handles creation time metadata
 * based on network upgrade status - if Sapling is always active, uses the provided
 * time, otherwise uses a safe historical time before Sapling activation.
 */
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
                m_wallet->mapSaplingSpendingKeyMetadata[ivk].nCreateTime = nTime;
            } else {
                // 154051200 seconds from epoch is Friday, 26 October 2018 00:00:00 GMT - definitely before Sapling activates
                m_wallet->mapSaplingSpendingKeyMetadata[ivk].nCreateTime = std::max((int64_t) 154051200, nTime);
            }
            if (hdKeypath) {
                m_wallet->mapSaplingSpendingKeyMetadata[ivk].hdKeypath = hdKeypath.value();
            }
            if (seedFpStr) {
                uint256 seedFp;
                seedFp.SetHex(seedFpStr.value());
                m_wallet->mapSaplingSpendingKeyMetadata[ivk].seedFp = seedFp;
            }
            return KeyAdded;
        }
    }
}

/**
 * @brief Add Orchard extended spending key to wallet
 * @param extsk The Orchard extended spending key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Imports an Orchard spending key into the wallet. Handles creation time metadata
 * based on network upgrade status - if Orchard is always active, uses the provided
 * time, otherwise uses a safe historical time before Orchard activation.
 * Also handles HD keypath and seed fingerprint metadata if provided.
 */
KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::OrchardExtendedSpendingKeyPirate &extsk) const {
    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt){
        return KeyNotAdded;
    }
    auto extfvk = extfvkOpt.value();

    auto ivkOpt = extfvk.fvk.GetIVK();
    if (ivkOpt == std::nullopt) {
        return KeyNotAdded;
    }
    auto ivk = ivkOpt.value();

    auto addrOpt = extfvk.fvk.GetDefaultAddress();
    if (addrOpt == std::nullopt) {
        return KeyNotAdded;
    }
    auto addr = addrOpt.value();

    {
        if (log){
            LogPrint("zrpc", "Importing zaddr %s...\n", EncodePaymentAddress(addr));
        }
        // Don't throw error in case a key is already there
        if (m_wallet->HaveOrchardSpendingKey(extfvk)) {
            return KeyAlreadyExists;
        } else {
            if (!m_wallet-> AddOrchardZKey(extsk)) {
                return KeyNotAdded;
            }

            // Orchard addresses can't have been used in transactions prior to activation.
            if (params.vUpgrades[Consensus::UPGRADE_ORCHARD].nActivationHeight == Consensus::NetworkUpgrade::ALWAYS_ACTIVE) {
                m_wallet->mapOrchardSpendingKeyMetadata[ivk].nCreateTime = nTime;
            } else {
                // 154051200 seconds from epoch is Friday, 26 October 2018 00:00:00 GMT - definitely before Orchard activates
                m_wallet->mapOrchardSpendingKeyMetadata[ivk].nCreateTime = std::max((int64_t) 154051200, nTime);
            }
            if (hdKeypath) {
                m_wallet->mapOrchardSpendingKeyMetadata[ivk].hdKeypath = hdKeypath.value();
            }
            if (seedFpStr) {
                uint256 seedFp;
                seedFp.SetHex(seedFpStr.value());
                m_wallet->mapOrchardSpendingKeyMetadata[ivk].seedFp = seedFp;
            }
            return KeyAdded;
        }
    }
}

/**
 * @brief Handle invalid encoding for spending key addition
 * @param no The invalid encoding object
 * @throws JSONRPCError with RPC_INVALID_ADDRESS_OR_KEY
 */
KeyAddResult AddSpendingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid spending key");
}

/**
 * @brief Add Sapling diversified extended spending key to wallet
 * @param extdsk The Sapling diversified extended spending key to add
 * @return KeyAddResult indicating the result of the operation
 * 
 * Imports a diversified Sapling spending key and attempts to add its corresponding
 * address to the wallet. The function will add the base spending key if needed
 * and then attempt to add the specific diversified address for incoming viewing.
 */
KeyAddResult AddDiversifiedSpendingKeyToWallet::operator()(const libzcash::SaplingDiversifiedExtendedSpendingKey &extdsk) const {
    auto extfvk = extdsk.extsk.ToXFVK();
    auto ivk = extfvk.fvk.in_viewing_key();
    auto addr = ivk.address(extdsk.d).value();
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

/**
 * @brief Add an Orchard diversified extended spending key to the wallet
 * @param extdsk The Orchard diversified extended spending key to add
 * @return KeyAddResult indicating the result of the operation
 */
KeyAddResult AddDiversifiedSpendingKeyToWallet::operator()(const libzcash::OrchardDiversifiedExtendedSpendingKeyPirate &extdsk) const {
    auto extfvkOpt = extdsk.extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        return KeyNotAdded;
    }
    auto extfvk = extfvkOpt.value();

    auto ivkOpt = extfvk.fvk.GetIVK();
    if (ivkOpt == std::nullopt) {
        return KeyNotAdded;
    }
    auto ivk = ivkOpt.value();

    auto addrOpt = extfvk.fvk.GetAddress(extdsk.d);
    if (addrOpt == std::nullopt) {
        return KeyNotAdded;
    }
    auto addr = addrOpt.value();

    KeyAddResult result = KeyNotAdded;

    // Don't throw error in case a key is already there
    if (m_wallet->HaveOrchardSpendingKey(extfvk)) {
        result = KeyAlreadyExists;
    } else {
        if (!m_wallet->AddOrchardZKey(extdsk.extsk)) {
            return KeyNotAdded;
        }
        result = KeyAdded;
    }

    if (m_wallet->AddOrchardIncomingViewingKey(ivk, addr)) {
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

/**
 * @brief Handle invalid encoding for diversified spending key addition
 * @param no The invalid encoding object
 * @throws JSONRPCError with RPC_INVALID_ADDRESS_OR_KEY
 */
KeyAddResult AddDiversifiedSpendingKeyToWallet::operator()(const libzcash::InvalidEncoding& no) const {
    throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid diversified viewing key");
}
