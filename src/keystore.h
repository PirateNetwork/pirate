// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2026 The Pirate Chain developers
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

#ifndef BITCOIN_KEYSTORE_H
#define BITCOIN_KEYSTORE_H

#include "key.h"
#include "pubkey.h"
#include "script/script.h"
#include "script/standard.h"
#include "script/script_ext.h"
#include "sync.h"
#include "zcash/Address.hpp"
#include "zcash/NoteEncryption.hpp"
#include "zcash/address/zip32.h"

#include <boost/signals2/signal.hpp>

/**
 * @enum KeyScope
 * @brief Defines the scope of shielded keys (External or Internal)
 *
 * External scope (0): Keys used for receiving funds from external sources
 * Internal scope (1): Keys used for change addresses and internal transfers
 *
 * Shared by both Sapling and Ironwood key management throughout the keystore.
 */
enum class KeyScope : uint8_t {
    External = 0,  //!< External addresses for receiving funds
    Internal = 1   //!< Internal addresses for change and internal transfers
};

/**
 * @class CKeyStore
 * @brief Virtual base class for key storage implementations
 * 
 * Provides a unified interface for managing cryptographic keys across different
 * address types: transparent (Bitcoin-style), Sprout (legacy shielded), Sapling
 * (shielded), and Ironwood (latest shielded) addresses.
 * 
 * Key management includes:
 * - HD wallet seed management
 * - Transparent key pairs (CKey/CPubKey)
 * - Redeem scripts for P2SH addresses
 * - Watch-only addresses (view without spend capability)
 * - Shielded spending and viewing keys for Sprout/Sapling/Ironwood
 * - Diversified address tracking
 * 
 * Thread-safety: All operations should be protected by cs_KeyStore mutex.
 */
class CKeyStore
{
protected:


public:
    mutable CCriticalSection cs_KeyStore;  //!< Mutex for thread-safe key access

    virtual ~CKeyStore() {}

    // ========== HD Wallet Seed Management ==========
    //! Set the HD seed for this keystore
    virtual bool SetHDSeed(const HDSeed& seed) =0;
    //! Check if an HD seed exists
    virtual bool HaveHDSeed() const =0;
    //! Get the HD seed for this keystore
    virtual bool GetHDSeed(HDSeed& seedOut) const =0;
    //! Get the BIP-39 seed phrase
    virtual bool GetSeedPhrase(std::string& phraseOut) const =0;

    // ========== Transparent Address Management ==========
    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual void GetKeys(std::set<CKeyID> &setAddress) const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;

    // ========== P2SH Redeem Scripts ==========
    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    // ========== Watch-Only Address Management ==========
    //! Support for Watch-only addresses (can view but not spend)
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool AddIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool RemoveIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const =0;
    virtual bool HaveIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) const =0;
    virtual bool HaveWatchOnly() const =0;

    // ========== Sprout Shielded Addresses ==========
    //! Add a spending key to the store.
    virtual bool AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk) =0;

    //! Check whether a spending key corresponding to a given payment address is present in the store.
    virtual bool HaveSproutSpendingKey(const libzcash::SproutPaymentAddress &address) const =0;
    virtual bool GetSproutSpendingKey(const libzcash::SproutPaymentAddress &address, libzcash::SproutSpendingKey& skOut) const =0;
    virtual void GetSproutPaymentAddresses(std::set<libzcash::SproutPaymentAddress> &setAddress) const =0;

    // ========== Sapling Shielded Addresses ==========
    //! Add a Sapling spending key to the store.
    virtual bool AddSaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &sk) =0;

    //! Check whether a Sapling spending key corresponding to a given Sapling viewing key is present in the store.
    virtual bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const =0;
    virtual bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey& skOut) const =0;

    //! Support for Sapling full viewing keys (XFVK)
    virtual bool AddSaplingExtendedFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const =0;
    virtual bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const =0;

    //! Sapling incoming viewing keys (IVK) with scope tracking (External = receive, Internal = change)
    virtual bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr,
        KeyScope scope = KeyScope::External) =0;
    virtual bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const =0;
    virtual bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingIncomingViewingKey& ivkOut) const =0;
    virtual bool GetSaplingKeyScope(
        const libzcash::SaplingPaymentAddress &addr,
        KeyScope& scopeOut) const =0;
    virtual void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const =0;


    // ========== Ironwood Shielded Addresses ==========
    //! Add an Ironwood spending key to the store.
    virtual bool AddIronwoodSpendingKey(
        const libzcash::IronwoodExtendedSpendingKeyPirate &extsk) =0;

    //! Check whether an Ironwood spending key corresponding to a given Ironwood viewing key is present in the store.
    virtual bool HaveIronwoodSpendingKey(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) const =0;
    virtual bool GetIronwoodSpendingKey(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk, libzcash::IronwoodExtendedSpendingKeyPirate& extskOut) const =0;

    //! Support for Ironwood full viewing keys (XFVK) with external/internal scope
    virtual bool AddIronwoodExtendedFullViewingKey(
        const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) =0;
    virtual bool HaveIronwoodFullViewingKey(const libzcash::IronwoodIncomingViewingKey &ivk) const =0;
    virtual bool GetIronwoodFullViewingKey(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        libzcash::IronwoodExtendedFullViewingKeyPirate& extfvkOut) const =0;

    //! Ironwood incoming viewing keys (IVK) with scope tracking
    virtual bool AddIronwoodIncomingViewingKey(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const libzcash::IronwoodPaymentAddress &addr,
        KeyScope scope) =0;
    virtual bool HaveIronwoodIncomingViewingKey(const libzcash::IronwoodPaymentAddress &addr) const =0;
    virtual bool GetIronwoodIncomingViewingKey(
        const libzcash::IronwoodPaymentAddress &addr,
        libzcash::IronwoodIncomingViewingKey& ivkOut) const =0;
    virtual bool GetIronwoodKeyScope(
        const libzcash::IronwoodPaymentAddress &addr,
        KeyScope& scopeOut) const =0;
    virtual void GetIronwoodPaymentAddresses(std::set<libzcash::IronwoodPaymentAddress> &setAddress) const =0;

    // ========== Diversified Address Management ==========
    //! Sapling diversified addresses
    virtual bool AddSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path) =0;

    virtual bool AddLastSaplingDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path) =0;

    //! Ironwood diversified addresses
    virtual bool AddIronwoodDiversifiedAddress(
        const libzcash::IronwoodPaymentAddress &addr,
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const blob88 &path) =0;

    virtual bool AddLastIronwoodDiversifierUsed(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const blob88 &path) =0;

    // ========== Sprout Viewing Keys ==========
    //! Support for Sprout viewing keys (view-only, no spend)
    virtual bool AddSproutViewingKey(const libzcash::SproutViewingKey &vk) =0;
    virtual bool RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk) =0;
    virtual bool HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const =0;
    virtual bool GetSproutViewingKey(
        const libzcash::SproutPaymentAddress &address,
        libzcash::SproutViewingKey& vkOut) const =0;
};

// ========== Type Definitions ==========

//! Transparent address key storage
typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CScriptID, CScript> ScriptMap;

//! Watch-only address sets (view without spend capability)
typedef std::set<CScript> WatchOnlySet;
typedef std::set<libzcash::SaplingExtendedFullViewingKey> SaplingWatchOnlySet;
typedef std::set<libzcash::IronwoodExtendedFullViewingKeyPirate> IronwoodWatchOnlySet;

//! Sprout shielded address key storage
typedef std::map<libzcash::SproutPaymentAddress, libzcash::SproutSpendingKey> SproutSpendingKeyMap;
typedef std::map<libzcash::SproutPaymentAddress, libzcash::SproutViewingKey> SproutViewingKeyMap;
typedef std::map<libzcash::SproutPaymentAddress, ZCNoteDecryption> NoteDecryptorMap;

// ========== Sapling Type Definitions ==========

/**
 * Sapling extended full viewing key (XFVK) provides view-only access.
 * When encrypting wallet, encrypt SaplingSpendingKeyMap while leaving
 * SaplingFullViewingKeyMap unencrypted for view-only functionality.
 */
typedef std::map<libzcash::SaplingExtendedFullViewingKey, libzcash::SaplingExtendedSpendingKey> SaplingSpendingKeyMap;
typedef std::map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey> SaplingFullViewingKeyMap;

//! Maps from payment address to (incoming viewing key, scope) — scope tracks External vs Internal (change) addresses
typedef std::map<libzcash::SaplingPaymentAddress, std::pair<libzcash::SaplingIncomingViewingKey, KeyScope>> SaplingIncomingViewingKeyMap;

//! Set of incoming viewing keys for transaction scanning
typedef std::map<libzcash::SaplingIncomingViewingKey, KeyScope> SaplingIncomingViewingKeySet;//!< IVK -> scope mapping for transaction scanning

//! Set of outgoing viewing keys for sent note decryption
typedef std::set<uint256> SaplingOutgoingViewingKeySet;

//! Diversified Sapling address management
typedef std::pair<libzcash::SaplingIncomingViewingKey, blob88> SaplingDiversifierPath;
typedef std::map<libzcash::SaplingPaymentAddress, SaplingDiversifierPath> SaplingPaymentAddresses;
typedef std::map<libzcash::SaplingIncomingViewingKey, blob88> LastSaplingDiversifierPath;

// ========== Ironwood Type Definitions ==========

/**
 * @struct IronwoodIVKWithScope
 * @brief Wrapper to track Ironwood incoming viewing key with its scope
 * 
 * Associates an IVK with External or Internal scope to enable:
 * - Proper address generation (external vs internal/change)
 * - Correct transaction scanning and output classification
 * - Scope preservation during database operations
 */
struct IronwoodIVKWithScope {
    libzcash::IronwoodIncomingViewingKey ivk;  //!< The incoming viewing key
    KeyScope scope;                           //!< External or Internal scope
    
    IronwoodIVKWithScope() : ivk(), scope(KeyScope::External) {}
    IronwoodIVKWithScope(const libzcash::IronwoodIncomingViewingKey& ivk_, KeyScope scope_) 
        : ivk(ivk_), scope(scope_) {}
    
    //! Comparison operator for map/set storage (compares both IVK and scope)
    bool operator<(const IronwoodIVKWithScope& other) const {
        if (ivk < other.ivk) return true;
        if (other.ivk < ivk) return false;
        return static_cast<uint8_t>(scope) < static_cast<uint8_t>(other.scope);
    }
    
    bool operator==(const IronwoodIVKWithScope& other) const {
        return ivk == other.ivk && scope == other.scope;
    }
};

/**
 * @struct IronwoodOVKWithScope
 * @brief Wrapper to track Ironwood outgoing viewing key with its scope
 * 
 * Associates an OVK with External or Internal scope for proper
 * sent note decryption based on output type (external payment vs change).
 */
struct IronwoodOVKWithScope {
    libzcash::IronwoodOutgoingViewingKey ovk;  //!< The outgoing viewing key
    KeyScope scope;                     //!< External or Internal scope
    
    IronwoodOVKWithScope() : ovk(), scope(KeyScope::External) {}
    IronwoodOVKWithScope(const libzcash::IronwoodOutgoingViewingKey& ovk_, KeyScope scope_) 
        : ovk(ovk_), scope(scope_) {}
    
    //! Comparison operator for set storage (compares both OVK and scope)
    bool operator<(const IronwoodOVKWithScope& other) const {
        if (ovk < other.ovk) return true;
        if (other.ovk < ovk) return false;
        return static_cast<uint8_t>(scope) < static_cast<uint8_t>(other.scope);
    }
    
    bool operator==(const IronwoodOVKWithScope& other) const {
        return ovk == other.ovk && scope == other.scope;
    }
};

//! Ironwood spending and viewing key storage
typedef std::map<libzcash::IronwoodExtendedFullViewingKeyPirate, libzcash::IronwoodExtendedSpendingKeyPirate> IronwoodSpendingKeyMap;
typedef std::map<IronwoodIVKWithScope, libzcash::IronwoodExtendedFullViewingKeyPirate> IronwoodFullViewingKeyMap;

/**
 * Maps discovered address -> (ivk, scope) for transaction scanning.
 * Each address maps to exactly one (IVK, scope) pair.
 */
typedef std::map<libzcash::IronwoodPaymentAddress, std::pair<libzcash::IronwoodIncomingViewingKey, KeyScope>> IronwoodIncomingViewingKeyMap;

/**
 * Maps IVK -> scope for efficient iteration during transaction scanning.
 * Note: If the same IVK is used with different scopes, only the most
 * recently added scope is tracked.
 */
typedef std::map<libzcash::IronwoodIncomingViewingKey, KeyScope> IronwoodIncomingViewingKeySet;

//! Set of outgoing viewing keys with scope for sent note decryption
typedef std::set<IronwoodOVKWithScope> IronwoodOutgoingViewingKeySet;

//! Diversified Ironwood address management
typedef std::pair<libzcash::IronwoodIncomingViewingKey, blob88> IronwoodDiversifierPath;
typedef std::map<libzcash::IronwoodPaymentAddress, IronwoodDiversifierPath> IronwoodPaymentAddresses;
typedef std::map<libzcash::IronwoodIncomingViewingKey, blob88> LastIronwoodDiversifierPath;

/**
 * @class CBasicKeyStore
 * @brief Basic key store implementation that keeps keys in memory maps
 * 
 * Implements the CKeyStore interface using in-memory maps for all key types.
 * This is the base implementation used by CWallet and provides no encryption.
 * 
 * Key organization:
 * - Transparent: mapKeys (CKeyID -> CKey)
 * - Sprout: mapSproutSpendingKeys, mapSproutViewingKeys
 * - Sapling: mapSaplingSpendingKeys, mapSaplingFullViewingKeys, mapSaplingIncomingViewingKeys
 * - Ironwood: mapIronwoodSpendingKeys, mapIronwoodFullViewingKeys (scope-aware), mapIronwoodIncomingViewingKeys
 * 
 * Thread-safety: All methods use LOCK(cs_KeyStore) for thread-safe access.
 */
class CBasicKeyStore : public CKeyStore
{
protected:
    HDSeed hdSeed;                                      //!< Hierarchical deterministic wallet seed
    
    // Transparent address keys
    KeyMap mapKeys;                                     //!< Transparent private keys
    ScriptMap mapScripts;                               //!< P2SH redeem scripts
    
    // Watch-only addresses (view without spend)
    WatchOnlySet setWatchOnly;                          //!< Transparent watch-only scripts
    SaplingWatchOnlySet setSaplingWatchOnly;            //!< Sapling watch-only XFVKs
    IronwoodWatchOnlySet setIronwoodWatchOnly;            //!< Ironwood watch-only XFVKs
    
    // Sprout shielded keys
    SproutSpendingKeyMap mapSproutSpendingKeys;         //!< Sprout spending keys
    SproutViewingKeyMap mapSproutViewingKeys;           //!< Sprout viewing keys
    NoteDecryptorMap mapNoteDecryptors;                 //!< Sprout note decryptors

    // Sapling shielded keys
    SaplingSpendingKeyMap mapSaplingSpendingKeys;              //!< Sapling spending keys (encrypted in wallet)
    SaplingFullViewingKeyMap mapSaplingFullViewingKeys;        //!< Sapling full viewing keys (unencrypted)
    SaplingIncomingViewingKeyMap mapSaplingIncomingViewingKeys;//!< Address -> IVK mapping
    SaplingIncomingViewingKeyMap mapUnsavedSaplingIncomingViewingKeys; //!< Unsaved IVKs (cleared during SetBestChainINTERNAL)
    SaplingIncomingViewingKeySet setSaplingIncomingViewingKeys;//!< IVK -> scope mapping for transaction scanning
    SaplingOutgoingViewingKeySet setSaplingOutgoingViewingKeys;//!< OVK set for sent note decryption
    SaplingPaymentAddresses mapSaplingPaymentAddresses;        //!< Diversified address tracking
    LastSaplingDiversifierPath mapLastSaplingDiversifierPath;  //!< Last used diversifier per IVK

    // Ironwood shielded keys (with scope tracking)
    IronwoodSpendingKeyMap mapIronwoodSpendingKeys;              //!< Ironwood spending keys (encrypted in wallet)
    IronwoodFullViewingKeyMap mapIronwoodFullViewingKeys;        //!< Ironwood XFVK with scope (unencrypted)
    IronwoodIncomingViewingKeyMap mapIronwoodIncomingViewingKeys;//!< Address -> (IVK, scope) mapping
    IronwoodIncomingViewingKeyMap mapUnsavedIronwoodIncomingViewingKeys; //!< Unsaved IVKs (cleared during SetBestChainINTERNAL)
    IronwoodIncomingViewingKeySet setIronwoodIncomingViewingKeys;//!< IVK -> scope mapping for transaction scanning
    IronwoodOutgoingViewingKeySet setIronwoodOutgoingViewingKeys;//!< OVK with scope for sent note decryption
    IronwoodPaymentAddresses mapIronwoodPaymentAddresses;        //!< Diversified address tracking
    LastIronwoodDiversifierPath mapLastIronwoodDiversifierPath;  //!< Last used diversifier per IVK

public:
    // ========== HD Wallet Seed Management (implemented in keystore.cpp) ==========
    bool SetHDSeed(const HDSeed& seed);
    bool HaveHDSeed() const;
    bool GetHDSeed(HDSeed& seedOut) const;
    bool GetSeedPhrase(std::string& phraseOut) const;
    bool GetSeedPhrase(std::string& phraseOut, uint32_t langCode) const;

    // ========== Transparent Key Management ==========
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    
    /**
     * @brief Check if the keystore contains a specific transparent address key
     * @param address The key identifier (hash of public key) to check
     * @return true if the key is present in mapKeys
     */
    bool HaveKey(const CKeyID &address) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
    
    /**
     * @brief Get all transparent address key IDs from the keystore
     * @param setAddress Output set to populate with all key IDs
     */
    void GetKeys(std::set<CKeyID> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.begin();
            while (mi != mapKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }
    
    /**
     * @brief Retrieve a transparent private key by its address
     * @param address The key identifier to look up
     * @param keyOut Output parameter to receive the private key
     * @return true if key was found and returned, false otherwise
     */
    bool GetKey(const CKeyID &address, CKey &keyOut) const
    {
        {
            LOCK(cs_KeyStore);
            KeyMap::const_iterator mi = mapKeys.find(address);
            if (mi != mapKeys.end())
            {
                keyOut = mi->second;
                return true;
            }
        }
        return false;
    }
    
    // ========== P2SH Redeem Scripts (implemented in keystore.cpp) ==========
    virtual bool AddCScript(const CScript& redeemScript);
    virtual bool HaveCScript(const CScriptID &hash) const;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const;

    // ========== Watch-Only Addresses (implemented in keystore.cpp) ==========
    virtual bool AddWatchOnly(const CScript &dest);
    virtual bool AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool AddIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk);
    virtual bool RemoveWatchOnly(const CScript &dest);
    virtual bool RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool RemoveIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk);
    virtual bool HaveWatchOnly(const CScript &dest) const;
    virtual bool HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const;
    virtual bool HaveIronwoodWatchOnly(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) const;
    virtual bool HaveWatchOnly() const;

    // ========== Sprout Shielded Addresses ==========
    bool AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk);
    
    /**
     * @brief Check if keystore has a Sprout spending key for the given address
     * @param address The Sprout payment address to check
     * @return true if spending key exists
     */
    bool HaveSproutSpendingKey(const libzcash::SproutPaymentAddress &address) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapSproutSpendingKeys.count(address) > 0);
        }
        return result;
    }
    
    /**
     * @brief Retrieve a Sprout spending key by payment address
     * @param address The payment address to look up
     * @param skOut Output parameter for the spending key
     * @return true if key was found
     */
    bool GetSproutSpendingKey(const libzcash::SproutPaymentAddress &address, libzcash::SproutSpendingKey &skOut) const
    {
        {
            LOCK(cs_KeyStore);
            SproutSpendingKeyMap::const_iterator mi = mapSproutSpendingKeys.find(address);
            if (mi != mapSproutSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Get the note decryptor for a Sprout payment address
     * @param address The Sprout payment address
     * @param decOut Output parameter for the note decryptor
     * @return true if decryptor was found
     */
    bool GetNoteDecryptor(const libzcash::SproutPaymentAddress &address, ZCNoteDecryption &decOut) const
    {
        {
            LOCK(cs_KeyStore);
            NoteDecryptorMap::const_iterator mi = mapNoteDecryptors.find(address);
            if (mi != mapNoteDecryptors.end())
            {
                decOut = mi->second;
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Get all Sprout payment addresses (spending keys + viewing keys)
     * @param setAddress Output set to populate with addresses
     */
    void GetSproutPaymentAddresses(std::set<libzcash::SproutPaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            SproutSpendingKeyMap::const_iterator mi = mapSproutSpendingKeys.begin();
            while (mi != mapSproutSpendingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
            SproutViewingKeyMap::const_iterator mvi = mapSproutViewingKeys.begin();
            while (mvi != mapSproutViewingKeys.end())
            {
                setAddress.insert((*mvi).first);
                mvi++;
            }
        }
    }

    // ========== Sapling Shielded Addresses ==========
    
    /**
     * @brief Get all Sapling incoming viewing keys for transaction scanning
     * @param ivks Output set to populate with IVKs
     */
    void GetSaplingIncomingViewingKeySet(SaplingIncomingViewingKeySet &ivks) {
        LOCK(cs_KeyStore);
        for (SaplingIncomingViewingKeySet::iterator it = setSaplingIncomingViewingKeys.begin(); it != setSaplingIncomingViewingKeys.end(); it++) {
            ivks.insert(*it);
        }
    }
    
    /**
     * @brief Get all Sapling outgoing viewing keys for sent note decryption
     * @param ovks Output set to populate with OVKs
     */
    void GetSaplingOutgoingViewingKeySet(SaplingOutgoingViewingKeySet &ovks) {
        LOCK(cs_KeyStore);
        for (SaplingOutgoingViewingKeySet::iterator it = setSaplingOutgoingViewingKeys.begin(); it != setSaplingOutgoingViewingKeys.end(); it++) {
            ovks.insert(*it);
        }
    }

    virtual bool AddSaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &sk);
        
    /**
     * @brief Check if keystore has a Sapling spending key for the given XFVK
     * @param extfvk The extended full viewing key to check
     * @return true if corresponding spending key exists
     */
    bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapSaplingSpendingKeys.count(extfvk) > 0);
        }
        return result;
    }
    
    /**
     * @brief Retrieve a Sapling spending key by its XFVK
     * @param extfvk The extended full viewing key
     * @param skOut Output parameter for the spending key
     * @return true if key was found
     */
    bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey &skOut) const
    {
        {
            LOCK(cs_KeyStore);

            SaplingSpendingKeyMap::const_iterator mi = mapSaplingSpendingKeys.find(extfvk);
            if (mi != mapSaplingSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }

    virtual bool AddSaplingExtendedFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const;
    virtual bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const;

    virtual bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr,
        KeyScope scope = KeyScope::External);
    virtual bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const;
    virtual bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingIncomingViewingKey& ivkOut) const;
    virtual bool GetSaplingKeyScope(
        const libzcash::SaplingPaymentAddress &addr,
        KeyScope& scopeOut) const;

    bool GetSaplingExtendedSpendingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingExtendedSpendingKey &extskOut) const;

    /**
     * @brief Get all Sapling payment addresses from the keystore
     * @param setAddress Output set to populate with addresses
     */
    void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            auto mi = mapSaplingIncomingViewingKeys.begin();
            while (mi != mapSaplingIncomingViewingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }

    // ========== Sapling Diversified Addresses ==========
    virtual bool AddSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);

    virtual bool AddLastSaplingDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);

    // ========== Ironwood Shielded Addresses (with Scope Tracking) ==========
    
    /**
     * @brief Get all Ironwood IVK->scope mappings for transaction scanning
     * @param ivks Output set to populate with (IVK, scope) pairs
     * 
     * Note: If an IVK is used with multiple scopes, only the most recently
     * added scope is returned in the set.
     */
    void GetIronwoodIncomingViewingKeySet(IronwoodIncomingViewingKeySet &ivks) {
        LOCK(cs_KeyStore);
        for (IronwoodIncomingViewingKeySet::iterator it = setIronwoodIncomingViewingKeys.begin(); it != setIronwoodIncomingViewingKeys.end(); it++) {
            ivks.insert(*it);
        }
    }
    
    /**
     * @brief Get all Ironwood OVK+scope pairs for sent note decryption
     * @param ovks Output set to populate with (OVK, scope) wrappers
     */
    void GetIronwoodOutgoingViewingKeySet(IronwoodOutgoingViewingKeySet &ovks) {
        LOCK(cs_KeyStore);
        for (IronwoodOutgoingViewingKeySet::iterator it = setIronwoodOutgoingViewingKeys.begin(); it != setIronwoodOutgoingViewingKeys.end(); it++) {
            ovks.insert(*it);
        }
    }

    virtual bool AddIronwoodSpendingKey(
        const libzcash::IronwoodExtendedSpendingKeyPirate &extsk);
        
    /**
     * @brief Check if keystore has an Ironwood spending key for the given XFVK
     * @param extfvk The extended full viewing key to check
     * @return true if corresponding spending key exists
     */
    bool HaveIronwoodSpendingKey(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapIronwoodSpendingKeys.count(extfvk) > 0);
        }
        return result;
    }
    
    /**
     * @brief Retrieve an Ironwood spending key by its XFVK
     * @param extfvk The extended full viewing key
     * @param extskOut Output parameter for the spending key
     * @return true if key was found
     */
    bool GetIronwoodSpendingKey(const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk, libzcash::IronwoodExtendedSpendingKeyPirate &extskOut) const
    {
        {
            LOCK(cs_KeyStore);

            IronwoodSpendingKeyMap::const_iterator mi = mapIronwoodSpendingKeys.find(extfvk);
            if (mi != mapIronwoodSpendingKeys.end())
            {
                extskOut = mi->second;
                return true;
            }
        }
        return false;
    }

    virtual bool AddIronwoodExtendedFullViewingKey(
        const libzcash::IronwoodExtendedFullViewingKeyPirate &extfvk);
    virtual bool HaveIronwoodFullViewingKey(const libzcash::IronwoodIncomingViewingKey &ivk) const;
    virtual bool GetIronwoodFullViewingKey(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        libzcash::IronwoodExtendedFullViewingKeyPirate& extfvkOut) const;

    virtual bool AddIronwoodIncomingViewingKey(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const libzcash::IronwoodPaymentAddress &addr,
        KeyScope scope);
    virtual bool HaveIronwoodIncomingViewingKey(const libzcash::IronwoodPaymentAddress &addr) const;
    virtual bool GetIronwoodIncomingViewingKey(
        const libzcash::IronwoodPaymentAddress &addr,
        libzcash::IronwoodIncomingViewingKey& ivkOut) const;

    virtual bool GetIronwoodKeyScope(
        const libzcash::IronwoodPaymentAddress &addr,
        KeyScope& scopeOut) const;

    bool GetIronwoodExtendedSpendingKey(
        const libzcash::IronwoodPaymentAddress &addr,
        libzcash::IronwoodExtendedSpendingKeyPirate &extskOut) const;

    /**
     * @brief Get all Ironwood payment addresses from the keystore
     * @param setAddress Output set to populate with addresses
     */
    void GetIronwoodPaymentAddresses(std::set<libzcash::IronwoodPaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_KeyStore);
            auto mi = mapIronwoodIncomingViewingKeys.begin();
            while (mi != mapIronwoodIncomingViewingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }

    // ========== Ironwood Diversified Addresses ==========
    virtual bool AddIronwoodDiversifiedAddress(
        const libzcash::IronwoodPaymentAddress &addr,
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const blob88 &path);

    virtual bool AddLastIronwoodDiversifierUsed(
        const libzcash::IronwoodIncomingViewingKey &ivk,
        const blob88 &path);

    // ========== Sprout Viewing Keys ==========
    virtual bool AddSproutViewingKey(const libzcash::SproutViewingKey &vk);
    virtual bool RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk);
    virtual bool HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const;
    virtual bool GetSproutViewingKey(
        const libzcash::SproutPaymentAddress &address,
        libzcash::SproutViewingKey& vkOut) const;
};

// ========== Encrypted Key Types ==========

//! Secure memory allocation for sensitive cryptographic material
typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;

//! Encrypted transparent keys (CKeyID -> encrypted private key)
typedef std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char> > > CryptedKeyMap;

//! Encrypted Sprout spending keys (payment address -> encrypted key)
typedef std::map<libzcash::SproutPaymentAddress, std::vector<unsigned char> > CryptedSproutSpendingKeyMap;

//! Encrypted Sapling spending keys (XFVK -> encrypted key)
typedef std::map<libzcash::SaplingExtendedFullViewingKey, std::vector<unsigned char> > CryptedSaplingSpendingKeyMap;

//! Encrypted Ironwood spending keys (XFVK -> encrypted key)
typedef std::map<libzcash::IronwoodExtendedFullViewingKeyPirate, std::vector<unsigned char> > CryptedIronwoodSpendingKeyMap;

#endif // BITCOIN_KEYSTORE_H
