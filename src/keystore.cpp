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

#include "keystore.h"

#include "key.h"
#include "util.h"

#include <boost/foreach.hpp>

/**
 * @brief Retrieve the public key for a given key ID
 * @param address The key ID (hash160 of public key)
 * @param vchPubKeyOut[out] The public key (if found)
 * @return true if private key exists and public key retrieved, false otherwise
 * 
 * Retrieves the private key for the given address and extracts its public key.
 * This is a convenience function that combines key lookup and public key derivation.
 */
bool CKeyStore::GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const
{
    CKey key;
    if (!GetKey(address, key))
        return false;
    vchPubKeyOut = key.GetPubKey();
    return true;
}

/**
 * @brief Add a private key to the keystore
 * @param key The private key to add
 * @return true on success
 * 
 * Convenience wrapper that extracts the public key from the private key
 * and calls AddKeyPubKey. The public key is used as the lookup index.
 */
bool CKeyStore::AddKey(const CKey &key) {
    return AddKeyPubKey(key, key.GetPubKey());
}

/**
 * @brief Set the HD (Hierarchical Deterministic) seed for the wallet
 * @param seed The HD seed to set
 * @return true if seed was set, false if seed already exists
 * 
 * Sets the wallet's HD seed which is used to derive all keys deterministically.
 * Once set, the seed cannot be changed to prevent accidental key loss.
 * This is a one-time operation per wallet.
 */
bool CBasicKeyStore::SetHDSeed(const HDSeed& seed)
{
    LOCK(cs_KeyStore);
    if (!hdSeed.IsNull()) {
        // Don't allow an existing seed to be changed. We can maybe relax this
        // restriction later once we have worked out the UX implications.
        return false;
    }
    hdSeed = seed;
    return true;
}

/**
 * @brief Check if the keystore has an HD seed
 * @return true if HD seed exists, false otherwise
 */
bool CBasicKeyStore::HaveHDSeed() const
{
    LOCK(cs_KeyStore);
    return !hdSeed.IsNull();
}

/**
 * @brief Retrieve the HD seed from the keystore
 * @param seedOut[out] The HD seed (if it exists)
 * @return true if seed retrieved, false if no seed exists
 */
bool CBasicKeyStore::GetHDSeed(HDSeed& seedOut) const
{
    LOCK(cs_KeyStore);
    if (hdSeed.IsNull()) {
        return false;
    } else {
        seedOut = hdSeed;
        return true;
    }
}

/**
 * @brief Retrieve the mnemonic seed phrase from the HD seed
 * @param phraseOut[out] The seed phrase as a string
 * @return true if phrase retrieved, false if no seed exists
 * 
 * Extracts the human-readable mnemonic phrase from the HD seed.
 * This phrase can be used to recover the wallet.
 */
bool CBasicKeyStore::GetSeedPhrase(std::string &phraseOut) const
{
    LOCK(cs_KeyStore);
    if (hdSeed.IsNull()) {
        return false;
    } else {
        HDSeed seed = hdSeed;
        seed.GetPhrase(phraseOut);
        return true;
    }
}

/**
 * @brief Add a private key with its corresponding public key
 * @param key The private key to add
 * @param pubkey The public key corresponding to the private key
 * @return true on success
 * 
 * Stores the private key indexed by its public key ID (hash160 of pubkey).
 * This is the primary method for adding transparent address keys.
 */
bool CBasicKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    LOCK(cs_KeyStore);
    mapKeys[pubkey.GetID()] = key;
    return true;
}

/**
 * @brief Add a redeem script to the keystore
 * @param redeemScript The script to add
 * @return true on success, false if script exceeds maximum size
 * 
 * Stores a redeem script for P2SH (Pay-to-Script-Hash) addresses.
 * The script is indexed by its hash (CScriptID). Rejects scripts
 * larger than MAX_SCRIPT_ELEMENT_SIZE to prevent DoS attacks.
 */
bool CBasicKeyStore::AddCScript(const CScript& redeemScript)
{
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
        return error("CBasicKeyStore::AddCScript(): redeemScripts > %i bytes are invalid", MAX_SCRIPT_ELEMENT_SIZE);

    LOCK(cs_KeyStore);
    mapScripts[CScriptID(redeemScript)] = redeemScript;
    return true;
}

/**
 * @brief Check if keystore has a redeem script
 * @param hash The script ID (hash of the script)
 * @return true if script exists, false otherwise
 */
bool CBasicKeyStore::HaveCScript(const CScriptID& hash) const
{
    LOCK(cs_KeyStore);
    return mapScripts.count(hash) > 0;
}

/**
 * @brief Retrieve a redeem script from the keystore
 * @param hash The script ID to look up
 * @param redeemScriptOut[out] The script (if found)
 * @return true if script found, false otherwise
 */
bool CBasicKeyStore::GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const
{
    LOCK(cs_KeyStore);
    ScriptMap::const_iterator mi = mapScripts.find(hash);
    if (mi != mapScripts.end())
    {
        redeemScriptOut = (*mi).second;
        return true;
    }
    return false;
}

/**
 * @brief Add a watch-only transparent address
 * @param dest The script to watch (typically a P2PKH or P2SH script)
 * @return true on success
 * 
 * Adds an address to watch without the ability to spend from it.
 * Useful for monitoring addresses or implementing multisig wallets.
 */
bool CBasicKeyStore::AddWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.insert(dest);
    return true;
}

/**
 * @brief Add a Sapling watch-only extended full viewing key
 * @param extfvk The Sapling extended full viewing key
 * @return true on success
 * 
 * Enables watching Sapling addresses without spending capability.
 * Can decrypt incoming transactions but cannot create spending signatures.
 */
bool CBasicKeyStore::AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    setSaplingWatchOnly.insert(extfvk);
    return true;
}

/**
 * @brief Add an Orchard watch-only extended full viewing key
 * @param extfvk The Orchard extended full viewing key
 * @return true on success
 * 
 * Enables watching Orchard addresses (both external and internal) without spending capability.
 * Can decrypt incoming transactions but cannot create spending signatures.
 */
bool CBasicKeyStore::AddOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);
    setOrchardWatchOnly.insert(extfvk);
    return true;
}

/**
 * @brief Remove a transparent watch-only address
 * @param dest The script to stop watching
 * @return true on success
 */
bool CBasicKeyStore::RemoveWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.erase(dest);
    return true;
}

/**
 * @brief Remove a Sapling watch-only viewing key
 * @param extfvk The Sapling extended full viewing key to remove
 * @return true on success
 */
bool CBasicKeyStore::RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    setSaplingWatchOnly.erase(extfvk);
    return true;
}

/**
 * @brief Remove an Orchard watch-only viewing key
 * @param extfvk The Orchard extended full viewing key to remove
 * @return true on success
 */
bool CBasicKeyStore::RemoveOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);
    setOrchardWatchOnly.erase(extfvk);
    return true;
}

/**
 * @brief Check if a transparent script is being watched
 * @param dest The script to check
 * @return true if script is in watch-only set, false otherwise
 */
bool CBasicKeyStore::HaveWatchOnly(const CScript &dest) const
{
    LOCK(cs_KeyStore);
    return setWatchOnly.count(dest) > 0;
}

/**
 * @brief Check if a Sapling viewing key is in watch-only mode
 * @param extfvk The Sapling extended full viewing key to check
 * @return true if viewing key is watch-only, false otherwise
 */
bool CBasicKeyStore::HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const
{
    LOCK(cs_KeyStore);
    return setSaplingWatchOnly.count(extfvk) > 0;
}

/**
 * @brief Check if an Orchard viewing key is in watch-only mode
 * @param extfvk The Orchard extended full viewing key to check
 * @return true if viewing key is watch-only, false otherwise
 */
bool CBasicKeyStore::HaveOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk) const
{
    LOCK(cs_KeyStore);
    return setOrchardWatchOnly.count(extfvk) > 0;
}

/**
 * @brief Check if keystore has any watch-only addresses (transparent, Sapling, or Orchard)
 * @return true if any watch-only keys exist, false otherwise
 */
bool CBasicKeyStore::HaveWatchOnly() const
{
    LOCK(cs_KeyStore);

    if (!setWatchOnly.empty())
        return true;

    if (!setSaplingWatchOnly.empty())
        return true;

    if (!setOrchardWatchOnly.empty())
        return true;

    return false;
}

/**
 * @brief Add a Sprout spending key to the keystore
 * @param sk The Sprout spending key
 * @return true on success
 * 
 * Stores a Sprout spending key and creates the associated note decryptor.
 * The key is indexed by its payment address for efficient lookup.
 * Also registers the note decryptor for scanning incoming Sprout transactions.
 */
bool CBasicKeyStore::AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk)
{
    LOCK(cs_KeyStore);
    auto address = sk.address();
    mapSproutSpendingKeys[address] = sk;
    mapNoteDecryptors.insert(std::make_pair(address, ZCNoteDecryption(sk.receiving_key())));
    return true;
}

/**
 * @brief Add a Sapling extended spending key to the keystore
 * @param sk The Sapling extended spending key
 * @return true on success, false if XFVK addition fails
 * 
 * Derives and stores the extended full viewing key (XFVK) from the spending key,
 * then stores the spending key itself. This automatically adds:
 * - The XFVK with incoming and outgoing viewing keys
 * - The default payment address
 * 
 * The spending key is stored for transaction signing operations.
 */
bool CBasicKeyStore::AddSaplingSpendingKey(
    const libzcash::SaplingExtendedSpendingKey &sk)
{
    LOCK(cs_KeyStore);
    auto extfvk = sk.ToXFVK();

    // if SaplingFullViewingKey is not in SaplingFullViewingKeyMap, add it
    if (!CBasicKeyStore::AddSaplingExtendedFullViewingKey(extfvk)) {
        return false;
    }

    mapSaplingSpendingKeys[extfvk] = sk;

    return true;
}

/**
 * @brief Add an Orchard extended spending key to the keystore
 * @param extsk The Orchard extended spending key to add
 * @return true on success, false if XFVK derivation fails or XFVK addition fails
 * 
 * Derives the extended full viewing key (XFVK) from the spending key and adds both
 * to the keystore. This automatically adds:
 * - The XFVK with both external and internal scopes
 * - Default external and internal payment addresses
 * - Associated incoming and outgoing viewing keys
 * 
 * The spending key is stored in mapOrchardSpendingKeys for signing operations.
 */
bool CBasicKeyStore::AddOrchardSpendingKey(
    const libzcash::OrchardExtendedSpendingKeyPirate &extsk)
{
    LOCK(cs_KeyStore);
    
    // Derive the extended full viewing key from the spending key
    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        return false;  // Key derivation failed
    }
    auto extfvk = extfvkOpt.value();

    // Add the XFVK (this will add both external and internal keys/addresses)
    if (!CBasicKeyStore::AddOrchardExtendedFullViewingKey(extfvk)) {
        return false;
    }

    // Store the spending key for transaction signing
    mapOrchardSpendingKeys[extfvk] = extsk;

    return true;
}

/**
 * @brief Add a Sprout viewing key to the keystore
 * @param vk The Sprout viewing key to add
 * @return true on success
 * 
 * Stores a Sprout viewing key indexed by its payment address and creates
 * the associated note decryptor. Unlike spending keys, viewing keys cannot
 * create spending signatures but can decrypt incoming notes.
 * 
 * The note decryptor is registered in mapNoteDecryptors to enable scanning
 * and decryption of incoming Sprout transactions without spending capability.
 */
bool CBasicKeyStore::AddSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    LOCK(cs_KeyStore);
    auto address = vk.address();
    mapSproutViewingKeys[address] = vk;
    mapNoteDecryptors.insert(std::make_pair(address, ZCNoteDecryption(vk.sk_enc)));
    return true;
}

/**
 * @brief Add a Sapling extended full viewing key to the keystore
 * @param extfvk The Sapling extended full viewing key
 * @return true on success, false if IVK addition fails
 * 
 * Stores a Sapling extended full viewing key (XFVK) which provides view-only
 * access to Sapling addresses. The XFVK contains:
 * - Incoming viewing key (IVK): Used to detect and decrypt received notes
 * - Outgoing viewing key (OVK): Used to decrypt sent notes
 * 
 * This function automatically:
 * - Stores the XFVK indexed by its IVK
 * - Adds the OVK to the outgoing viewing key set
 * - Adds the default payment address with its IVK
 * 
 * Viewing keys enable watch-only functionality where the wallet can see
 * transactions but cannot create spending signatures.
 */
bool CBasicKeyStore::AddSaplingExtendedFullViewingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    libzcash::SaplingIncomingViewingKey ivk;
    extfvk.fvk.DeriveIVK(&ivk);
    mapSaplingFullViewingKeys[ivk] = extfvk;
    setSaplingOutgoingViewingKeys.insert(extfvk.fvk.ovk);

    return CBasicKeyStore::AddSaplingIncomingViewingKey(ivk, extfvk.DefaultAddress());
}

/**
 * @brief Add an Orchard extended full viewing key (XFVK) with both external and internal scopes
 * @param extfvk The Orchard extended full viewing key to add
 * @return true on success, false if key derivation fails
 * 
 * This function extracts and stores both external and internal keys from the XFVK:
 * - External IVK/OVK: Used for receiving funds from external sources (scope = External)
 * - Internal IVK/OVK: Used for change addresses and internal transfers (scope = Internal)
 * 
 * Each derived key is stored with its appropriate scope flag to enable:
 * - Correct address generation (external vs internal/change addresses)
 * - Proper transaction scanning (identifying received vs change outputs)
 * - Scope preservation during database persistence
 * 
 * The external default address and internal default address are automatically added
 * to the keystore with their respective scopes.
 */
bool CBasicKeyStore::AddOrchardExtendedFullViewingKey(
    const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);

    // Derive external keys
    libzcash::OrchardIncomingViewingKey ivk;
    libzcash::OrchardOutgoingViewingKey ovk;
    libzcash::OrchardPaymentAddress address;
    bool ivkDerived         = extfvk.fvk.DeriveIVK(&ivk);
    bool ovkDerived         = extfvk.fvk.DeriveOVK(&ovk);
    bool addressDerived     = extfvk.fvk.DeriveDefaultAddress(&address);

    // Derive internal keys
    libzcash::OrchardIncomingViewingKey ivkInternal;
    libzcash::OrchardOutgoingViewingKey ovkInternal;
    libzcash::OrchardPaymentAddress addressInternal;
    bool ivkInternalDerived     = extfvk.fvk.DeriveIVKinternal(&ivkInternal);
    bool ovkInternalDerived     = extfvk.fvk.DeriveOVKinternal(&ovkInternal);
    bool addressInternalDerived = extfvk.fvk.DeriveDefaultAddressInternal(&addressInternal);

    if (!ivkDerived || !ovkDerived || !addressDerived ||
        !ivkInternalDerived || !ovkInternalDerived || !addressInternalDerived) {
        return false;
    }

    // Store external FVK and OVK with scope flag
    OrchardIVKWithScope ivkExternal(ivk, OrchardKeyScope::External);
    OrchardOVKWithScope ovkExternal(ovk, OrchardKeyScope::External);
    mapOrchardFullViewingKeys[ivkExternal] = extfvk;
    setOrchardOutgoingViewingKeys.insert(ovkExternal);
    
    // Store internal FVK and OVK with scope flag
    OrchardIVKWithScope ivkInternalScoped(ivkInternal, OrchardKeyScope::Internal);
    OrchardOVKWithScope ovkInternalScoped(ovkInternal, OrchardKeyScope::Internal);
    mapOrchardFullViewingKeys[ivkInternalScoped] = extfvk;
    setOrchardOutgoingViewingKeys.insert(ovkInternalScoped);

    // Add external IVK with default address
    if (!CBasicKeyStore::AddOrchardIncomingViewingKey(ivk, address, OrchardKeyScope::External)) {
        return false;
    }
    
    // Add internal IVK with default internal address
    return CBasicKeyStore::AddOrchardIncomingViewingKey(ivkInternal, addressInternal, OrchardKeyScope::Internal);
}

/**
 * @brief Add a Sapling incoming viewing key (IVK) with its associated payment address
 * @param ivk The Sapling incoming viewing key to add
 * @param addr The Sapling payment address associated with this IVK
 * @return true on success
 * 
 * Updates the wallet's internal address->IVK map. If the address already exists in the map,
 * it will be overwritten with the new IVK (though in practice, each address should have only
 * one IVK). The IVK is also added to a set for efficient iteration during transaction scanning.
 * 
 * Unlike Orchard IVKs, Sapling IVKs do not have explicit internal/external scope tracking.
 */
bool CBasicKeyStore::AddSaplingIncomingViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    LOCK(cs_KeyStore);

    // Add addr -> IVK to SaplingIncomingViewingKeyMap
    mapSaplingIncomingViewingKeys[addr] = ivk;
    
    // Add IVK to set for transaction scanning
    setSaplingIncomingViewingKeys.insert(ivk);

    // Track new address->IVK pairs discovered while wallet is locked
    mapUnsavedSaplingIncomingViewingKeys[addr] = ivk;

    return true;
}

/**
 * @brief Add an Orchard incoming viewing key (IVK) with its associated payment address and scope
 * @param ivk The Orchard incoming viewing key to add
 * @param addr The Orchard payment address associated with this IVK
 * @param scope The scope (External or Internal) of this IVK
 * @return true on success
 * 
 * Updates the wallet's internal address->ivk map with scope tracking. Each address maps to
 * exactly one (ivk, scope) pair. The IVK is also added to a set for efficient iteration
 * during transaction scanning. If the same IVK is added with a different scope, the scope
 * in the set will be updated to the most recent value.
 * 
 * The mapUnsavedOrchardIncomingViewingKeys is used to track new address->IVK mappings
 * discovered while the wallet is locked, and is cleared during SetBestChainINTERNAL.
 */
bool CBasicKeyStore::AddOrchardIncomingViewingKey(
    const libzcash::OrchardIncomingViewingKey &ivk,
    const libzcash::OrchardPaymentAddress &addr,
    OrchardKeyScope scope)
{
    LOCK(cs_KeyStore);

    // Add addr -> (ivk, scope) to OrchardIncomingViewingKeyMap
    // This map is used to retrieve the IVK and scope for a given payment address
    mapOrchardIncomingViewingKeys[addr] = std::make_pair(ivk, scope);
    
    // Add IVK to set for transaction scanning, updating scope if IVK already exists
    // Note: The same IVK can be used with different scopes (external/internal),
    // but the set only tracks one scope per IVK (the most recently added)
    setOrchardIncomingViewingKeys[ivk] = scope;

    // Track new address->IVK pairs discovered while wallet is locked
    // This is cleared during SetBestChainINTERNAL to capture new diversified addresses
    mapUnsavedOrchardIncomingViewingKeys[addr] = std::make_pair(ivk, scope);

    return true;
}

/**
 * @brief Add a Sapling diversified payment address with its derivation path
 * @param addr The Sapling payment address
 * @param ivk The incoming viewing key used to derive this address
 * @param path The 88-byte diversification path used to generate this address
 * @return true on success
 * 
 * Stores the mapping between a diversified address and its derivation information.
 * This allows the wallet to track which diversifier index was used for each address,
 * enabling proper address re-derivation and gap limit management.
 */
bool CBasicKeyStore::AddSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path
)
{
    LOCK(cs_KeyStore);
    SaplingDiversifierPath dPath(ivk, path);

    mapSaplingPaymentAddresses[addr] = dPath;

    return true;
}

/**
 * @brief Add an Orchard diversified payment address with its derivation path
 * @param addr The Orchard payment address
 * @param ivk The incoming viewing key used to derive this address
 * @param path The 88-byte diversification path used to generate this address
 * @return true on success
 * 
 * Stores the mapping between a diversified address and its derivation information.
 * This enables the wallet to track which diversifier was used for address generation,
 * supporting proper address management and re-derivation.
 */
bool CBasicKeyStore::AddOrchardDiversifiedAddress(
    const libzcash::OrchardPaymentAddress &addr,
    const libzcash::OrchardIncomingViewingKey &ivk,
    const blob88 &path
)
{
    LOCK(cs_KeyStore);
    OrchardDiversifierPath dPath(ivk, path);

    mapOrchardPaymentAddresses[addr] = dPath;

    return true;
}

/**
 * @brief Record the last diversifier path used for a Sapling IVK
 * @param ivk The Sapling incoming viewing key
 * @param path The last diversifier path used with this IVK
 * @return true on success
 * 
 * Tracks the most recently used diversifier for each IVK to support sequential
 * address generation without gaps. This is essential for address gap limit
 * management and ensuring all addresses can be recovered from seed.
 */
bool CBasicKeyStore::AddLastSaplingDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    LOCK(cs_KeyStore);

    mapLastSaplingDiversifierPath[ivk] = path;

    return true;
}

/**
 * @brief Record the last diversifier path used for an Orchard IVK
 * @param ivk The Orchard incoming viewing key
 * @param path The last diversifier path used with this IVK
 * @return true on success
 * 
 * Tracks the most recently used diversifier for each IVK to enable sequential
 * address generation and proper gap limit handling during wallet recovery.
 */
bool CBasicKeyStore::AddLastOrchardDiversifierUsed(
    const libzcash::OrchardIncomingViewingKey &ivk,
    const blob88 &path)
{
    LOCK(cs_KeyStore);

    mapLastOrchardDiversifierPath[ivk] = path;

    return true;
}

/**
 * @brief Remove a Sprout viewing key from the keystore
 * @param vk The Sprout viewing key to remove
 * @return true on success
 * 
 * Removes the viewing key associated with the derived payment address.
 * Note: This does not remove associated note decryptors.
 */
bool CBasicKeyStore::RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    LOCK(cs_KeyStore);
    mapSproutViewingKeys.erase(vk.address());
    return true;
}

/**
 * @brief Check if keystore has a Sprout viewing key for an address
 * @param address The Sprout payment address to check
 * @return true if viewing key exists, false otherwise
 */
bool CBasicKeyStore::HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const
{
    LOCK(cs_KeyStore);
    return mapSproutViewingKeys.count(address) > 0;
}

/**
 * @brief Check if keystore has a Sapling full viewing key for an IVK
 * @param ivk The Sapling incoming viewing key to check
 * @return true if full viewing key exists, false otherwise
 */
bool CBasicKeyStore::HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const
{
    LOCK(cs_KeyStore);
    return mapSaplingFullViewingKeys.count(ivk) > 0;
}

/**
 * @brief Check if keystore has an Orchard full viewing key for an IVK
 * @param ivk The Orchard incoming viewing key to check
 * @return true if full viewing key exists (external or internal scope), false otherwise
 * 
 * Checks both external and internal scope since the same IVK can exist with either scope.
 */
bool CBasicKeyStore::HaveOrchardFullViewingKey(const libzcash::OrchardIncomingViewingKey &ivk) const
{
    LOCK(cs_KeyStore);
    // Check both external and internal scope
    OrchardIVKWithScope ivkExternal(ivk, OrchardKeyScope::External);
    OrchardIVKWithScope ivkInternal(ivk, OrchardKeyScope::Internal);
    return mapOrchardFullViewingKeys.count(ivkExternal) > 0 || 
           mapOrchardFullViewingKeys.count(ivkInternal) > 0;
}

/**
 * @brief Check if keystore has a Sapling IVK for a payment address
 * @param addr The Sapling payment address to check
 * @return true if IVK exists for this address, false otherwise
 */
bool CBasicKeyStore::HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const
{
    LOCK(cs_KeyStore);
    return mapSaplingIncomingViewingKeys.count(addr) > 0;
}

/**
 * @brief Check if keystore has an Orchard IVK for a payment address
 * @param addr The Orchard payment address to check
 * @return true if IVK exists for this address, false otherwise
 */
bool CBasicKeyStore::HaveOrchardIncomingViewingKey(const libzcash::OrchardPaymentAddress &addr) const
{
    LOCK(cs_KeyStore);
    return mapOrchardIncomingViewingKeys.count(addr) > 0;
}

/**
 * @brief Retrieve a Sprout viewing key for a payment address
 * @param address The Sprout payment address
 * @param vkOut[out] The viewing key (if found)
 * @return true if viewing key found, false otherwise
 */
bool CBasicKeyStore::GetSproutViewingKey(
    const libzcash::SproutPaymentAddress &address,
    libzcash::SproutViewingKey &vkOut) const
{
    LOCK(cs_KeyStore);
    SproutViewingKeyMap::const_iterator mi = mapSproutViewingKeys.find(address);
    if (mi != mapSproutViewingKeys.end()) {
        vkOut = mi->second;
        return true;
    }
    return false;
}

/**
 * @brief Retrieve a Sapling extended full viewing key from an IVK
 * @param ivk The Sapling incoming viewing key
 * @param extfvkOut[out] The extended full viewing key (if found)
 * @return true if XFVK found, false otherwise
 */
bool CBasicKeyStore::GetSaplingFullViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    libzcash::SaplingExtendedFullViewingKey &extfvkOut) const
{
    LOCK(cs_KeyStore);
    SaplingFullViewingKeyMap::const_iterator mi = mapSaplingFullViewingKeys.find(ivk);
    if (mi != mapSaplingFullViewingKeys.end()) {
        extfvkOut = mi->second;
        return true;
    }
    return false;
}

/**
 * @brief Retrieve an Orchard extended full viewing key from an IVK
 * @param ivk The Orchard incoming viewing key
 * @param extfvkOut[out] The extended full viewing key (if found)
 * @return true if XFVK found (external or internal scope), false otherwise
 * 
 * Searches for the XFVK in both external and internal scope. External scope
 * is checked first as it's more commonly used. Returns the first match found.
 */
bool CBasicKeyStore::GetOrchardFullViewingKey(
    const libzcash::OrchardIncomingViewingKey &ivk,
    libzcash::OrchardExtendedFullViewingKeyPirate &extfvkOut) const
{
    LOCK(cs_KeyStore);
    // Try external scope first
    OrchardIVKWithScope ivkExternal(ivk, OrchardKeyScope::External);
    OrchardFullViewingKeyMap::const_iterator mi = mapOrchardFullViewingKeys.find(ivkExternal);
    if (mi != mapOrchardFullViewingKeys.end()) {
        extfvkOut = mi->second;
        return true;
    }
    // Try internal scope
    OrchardIVKWithScope ivkInternal(ivk, OrchardKeyScope::Internal);
    mi = mapOrchardFullViewingKeys.find(ivkInternal);
    if (mi != mapOrchardFullViewingKeys.end()) {
        extfvkOut = mi->second;
        return true;
    }
    return false;
}

/**
 * @brief Retrieve a Sapling incoming viewing key for a payment address
 * @param addr The Sapling payment address
 * @param ivkOut[out] The incoming viewing key (if found)
 * @return true if IVK found, false otherwise
 */
bool CBasicKeyStore::GetSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr,
                                   libzcash::SaplingIncomingViewingKey &ivkOut) const
{
    LOCK(cs_KeyStore);
    SaplingIncomingViewingKeyMap::const_iterator mi = mapSaplingIncomingViewingKeys.find(addr);
    if (mi != mapSaplingIncomingViewingKeys.end()) {
        ivkOut = mi->second;
        return true;
    }
    return false;
}

/**
 * @brief Retrieve the Orchard incoming viewing key for a given payment address
 * @param addr The Orchard payment address to look up
 * @param ivkOut[out] The IVK associated with the address (if found)
 * @return true if address found and IVK retrieved, false otherwise
 * 
 * Looks up the IVK from the address->IVK map. Note that this returns only the IVK
 * and not the scope. To retrieve both IVK and scope, access mapOrchardIncomingViewingKeys
 * directly and use .first for IVK and .second for scope.
 */
bool CBasicKeyStore::GetOrchardIncomingViewingKey(const libzcash::OrchardPaymentAddress &addr,
                                   libzcash::OrchardIncomingViewingKey &ivkOut) const
{
    LOCK(cs_KeyStore);
    OrchardIncomingViewingKeyMap::const_iterator mi = mapOrchardIncomingViewingKeys.find(addr);
    if (mi != mapOrchardIncomingViewingKeys.end()) {
        ivkOut = mi->second.first;  // Extract IVK from (IVK, scope) pair
        return true;
    }
    return false;
}

/**
 * @brief Retrieve the scope (External/Internal) for an Orchard payment address
 * @param addr The Orchard payment address
 * @param scopeOut[out] The scope (External or Internal) if found
 * @return true if address exists in keystore, false otherwise
 */
bool CBasicKeyStore::GetOrchardKeyScope(const libzcash::OrchardPaymentAddress &addr,
                                        OrchardKeyScope &scopeOut) const
{
    LOCK(cs_KeyStore);
    OrchardIncomingViewingKeyMap::const_iterator mi = mapOrchardIncomingViewingKeys.find(addr);
    if (mi != mapOrchardIncomingViewingKeys.end()) {
        scopeOut = mi->second.second;  // Extract scope from (IVK, scope) pair
        return true;
    }
    return false;
}

/**
 * @brief Retrieve a Sapling extended spending key for a payment address
 * @param addr The Sapling payment address
 * @param extskOut[out] The extended spending key (if found)
 * @return true if spending key found, false otherwise
 * 
 * Performs a chain lookup:
 * 1. Address -> IVK (incoming viewing key)
 * 2. IVK -> XFVK (extended full viewing key)
 * 3. XFVK -> Extended spending key
 * 
 * This only succeeds if the wallet has full spending authority for this address.
 * Watch-only wallets will fail at step 3.
 */
bool CBasicKeyStore::GetSaplingExtendedSpendingKey(const libzcash::SaplingPaymentAddress &addr,
                                    libzcash::SaplingExtendedSpendingKey &extskOut) const {
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    LOCK(cs_KeyStore);
    return GetSaplingIncomingViewingKey(addr, ivk) &&
            GetSaplingFullViewingKey(ivk, extfvk) &&
            GetSaplingSpendingKey(extfvk, extskOut);
}

/**
 * @brief Retrieve the Orchard extended spending key for a given payment address
 * @param addr The Orchard payment address
 * @param extskOut[out] The extended spending key (if found)
 * @return true if the spending key was found, false otherwise
 * 
 * Performs a chain lookup:
 * 1. Address -> IVK (incoming viewing key)
 * 2. IVK -> XFVK (extended full viewing key)
 * 3. XFVK -> Extended spending key
 * 
 * This only succeeds if the wallet has the full spending authority for this address.
 * Watch-only wallets will not have the spending key even if they have the viewing keys.
 */
bool CBasicKeyStore::GetOrchardExtendedSpendingKey(const libzcash::OrchardPaymentAddress &addr,
                                    libzcash::OrchardExtendedSpendingKeyPirate &extskOut) const {
    libzcash::OrchardIncomingViewingKey ivk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;

    LOCK(cs_KeyStore);
    // Chain lookup: addr -> ivk -> extfvk -> spending key
    return GetOrchardIncomingViewingKey(addr, ivk) &&
            GetOrchardFullViewingKey(ivk, extfvk) &&
            GetOrchardSpendingKey(extfvk, extskOut);
}
