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

#include "keystore.h"

#include "key.h"
#include "util.h"

#include <boost/foreach.hpp>

bool CKeyStore::GetPubKey(const CKeyID &address, CPubKey &vchPubKeyOut) const
{
    CKey key;
    if (!GetKey(address, key))
        return false;
    vchPubKeyOut = key.GetPubKey();
    return true;
}

bool CKeyStore::AddKey(const CKey &key) {
    return AddKeyPubKey(key, key.GetPubKey());
}

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

bool CBasicKeyStore::HaveHDSeed() const
{
    LOCK(cs_KeyStore);
    return !hdSeed.IsNull();
}

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

bool CBasicKeyStore::AddKeyPubKey(const CKey& key, const CPubKey &pubkey)
{
    LOCK(cs_KeyStore);
    mapKeys[pubkey.GetID()] = key;
    return true;
}

bool CBasicKeyStore::AddCScript(const CScript& redeemScript)
{
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
        return error("CBasicKeyStore::AddCScript(): redeemScripts > %i bytes are invalid", MAX_SCRIPT_ELEMENT_SIZE);

    LOCK(cs_KeyStore);
    mapScripts[CScriptID(redeemScript)] = redeemScript;
    return true;
}

bool CBasicKeyStore::HaveCScript(const CScriptID& hash) const
{
    LOCK(cs_KeyStore);
    return mapScripts.count(hash) > 0;
}

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

bool CBasicKeyStore::AddWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.insert(dest);
    return true;
}

bool CBasicKeyStore::AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    setSaplingWatchOnly.insert(extfvk);
    return true;
}

bool CBasicKeyStore::AddOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);
    setOrchardWatchOnly.insert(extfvk);
    return true;
}

bool CBasicKeyStore::RemoveWatchOnly(const CScript &dest)
{
    LOCK(cs_KeyStore);
    setWatchOnly.erase(dest);
    return true;
}

bool CBasicKeyStore::RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    setSaplingWatchOnly.erase(extfvk);
    return true;
}

bool CBasicKeyStore::RemoveOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);
    setOrchardWatchOnly.erase(extfvk);
    return true;
}

bool CBasicKeyStore::HaveWatchOnly(const CScript &dest) const
{
    LOCK(cs_KeyStore);
    return setWatchOnly.count(dest) > 0;
}

bool CBasicKeyStore::HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const
{
    LOCK(cs_KeyStore);
    return setSaplingWatchOnly.count(extfvk) > 0;
}

bool CBasicKeyStore::HaveOrchardWatchOnly(const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk) const
{
    LOCK(cs_KeyStore);
    return setOrchardWatchOnly.count(extfvk) > 0;
}

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

bool CBasicKeyStore::AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk)
{
    LOCK(cs_KeyStore);
    auto address = sk.address();
    mapSproutSpendingKeys[address] = sk;
    mapNoteDecryptors.insert(std::make_pair(address, ZCNoteDecryption(sk.receiving_key())));
    return true;
}

//! Sapling
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

bool CBasicKeyStore::AddOrchardSpendingKey(
    const libzcash::OrchardExtendedSpendingKeyPirate &extsk)
{
    LOCK(cs_KeyStore);
    auto extfvkOpt = extsk.GetXFVK();
    if (extfvkOpt == std::nullopt) {
        return false;
    }
    auto extfvk = extfvkOpt.value();

    // if OrchardFullViewingKey is not in OrchardFullViewingKeyMap, add it
    if (!CBasicKeyStore::AddOrchardExtendedFullViewingKey(extfvk)) {
        return false;
    }

    mapOrchardSpendingKeys[extfvk] = extsk;

    return true;
}

bool CBasicKeyStore::AddSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    LOCK(cs_KeyStore);
    auto address = vk.address();
    mapSproutViewingKeys[address] = vk;
    mapNoteDecryptors.insert(std::make_pair(address, ZCNoteDecryption(vk.sk_enc)));
    return true;
}

bool CBasicKeyStore::AddSaplingExtendedFullViewingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    LOCK(cs_KeyStore);
    auto ivk = extfvk.fvk.in_viewing_key();
    mapSaplingFullViewingKeys[ivk] = extfvk;
    setSaplingOutgoingViewingKeys.insert(extfvk.fvk.ovk);

    return CBasicKeyStore::AddSaplingIncomingViewingKey(ivk, extfvk.DefaultAddress());
}

bool CBasicKeyStore::AddOrchardExtendedFullViewingKey(
    const libzcash::OrchardExtendedFullViewingKeyPirate &extfvk)
{
    LOCK(cs_KeyStore);
    auto ivkOpt = extfvk.fvk.GetIVK();
    auto ovkOpt = extfvk.fvk.GetOVK();
    auto addressOpt = extfvk.fvk.GetDefaultAddress();

    if (ivkOpt == std::nullopt || ovkOpt == std::nullopt || addressOpt == std::nullopt) {
        return false;
    }

    auto ivk = ivkOpt.value();
    auto ovk = ovkOpt.value();
    auto address = addressOpt.value();;

    mapOrchardFullViewingKeys[ivk] = extfvk;
    setOrchardOutgoingViewingKeys.insert(ovk);

    return CBasicKeyStore::AddOrchardIncomingViewingKey(ivk, address);
}

// This function updates the wallet's internal address->ivk map.
// If we add an address that is already in the map, the map will
// remain unchanged as each address only has one ivk.
bool CBasicKeyStore::AddSaplingIncomingViewingKey(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    LOCK(cs_KeyStore);

    // Add addr -> SaplingIncomingViewing to SaplingIncomingViewingKeyMap
    mapSaplingIncomingViewingKeys[addr] = ivk;
    setSaplingIncomingViewingKeys.insert(ivk);

    //Cleared during SetBestChainINTERNAL to capture new address ivk pairs discovered while the wallet is locked
    mapUnsavedSaplingIncomingViewingKeys[addr] = ivk;

    return true;
}

// This function updates the wallet's internal address->ivk map.
// If we add an address that is already in the map, the map will
// remain unchanged as each address only has one ivk.
bool CBasicKeyStore::AddOrchardIncomingViewingKey(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const libzcash::OrchardPaymentAddressPirate &addr)
{
    LOCK(cs_KeyStore);

    // Add addr -> SaplingIncomingViewing to SaplingIncomingViewingKeyMap
    mapOrchardIncomingViewingKeys[addr] = ivk;
    setOrchardIncomingViewingKeys.insert(ivk);

    //Cleared during SetBestChainINTERNAL to capture new address ivk pairs discovered while the wallet is locked
    mapUnsavedOrchardIncomingViewingKeys[addr] = ivk;

    return true;
}

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

bool CBasicKeyStore::AddOrchardDiversifiedAddress(
    const libzcash::OrchardPaymentAddressPirate &addr,
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path
)
{
    LOCK(cs_KeyStore);
    OrchardDiversifierPath dPath(ivk, path);

    mapOrchardPaymentAddresses[addr] = dPath;

    return true;
}

bool CBasicKeyStore::AddLastSaplingDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    LOCK(cs_KeyStore);

    mapLastSaplingDiversifierPath[ivk] = path;

    return true;
}

bool CBasicKeyStore::AddLastOrchardDiversifierUsed(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    const blob88 &path)
{
    LOCK(cs_KeyStore);

    mapLastOrchardDiversifierPath[ivk] = path;

    return true;
}

bool CBasicKeyStore::RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    LOCK(cs_KeyStore);
    mapSproutViewingKeys.erase(vk.address());
    return true;
}

bool CBasicKeyStore::HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const
{
    LOCK(cs_KeyStore);
    return mapSproutViewingKeys.count(address) > 0;
}

bool CBasicKeyStore::HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const
{
    LOCK(cs_KeyStore);
    return mapSaplingFullViewingKeys.count(ivk) > 0;
}

bool CBasicKeyStore::HaveOrchardFullViewingKey(const libzcash::OrchardIncomingViewingKeyPirate &ivk) const
{
    LOCK(cs_KeyStore);
    return mapOrchardFullViewingKeys.count(ivk) > 0;
}

bool CBasicKeyStore::HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const
{
    LOCK(cs_KeyStore);
    return mapSaplingIncomingViewingKeys.count(addr) > 0;
}

bool CBasicKeyStore::HaveOrchardIncomingViewingKey(const libzcash::OrchardPaymentAddressPirate &addr) const
{
    LOCK(cs_KeyStore);
    return mapOrchardIncomingViewingKeys.count(addr) > 0;
}

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

bool CBasicKeyStore::GetOrchardFullViewingKey(
    const libzcash::OrchardIncomingViewingKeyPirate &ivk,
    libzcash::OrchardExtendedFullViewingKeyPirate &extfvkOut) const
{
    LOCK(cs_KeyStore);
    OrchardFullViewingKeyMap::const_iterator mi = mapOrchardFullViewingKeys.find(ivk);
    if (mi != mapOrchardFullViewingKeys.end()) {
        extfvkOut = mi->second;
        return true;
    }
    return false;
}

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

bool CBasicKeyStore::GetOrchardIncomingViewingKey(const libzcash::OrchardPaymentAddressPirate &addr,
                                   libzcash::OrchardIncomingViewingKeyPirate &ivkOut) const
{
    LOCK(cs_KeyStore);
    OrchardIncomingViewingKeyMap::const_iterator mi = mapOrchardIncomingViewingKeys.find(addr);
    if (mi != mapOrchardIncomingViewingKeys.end()) {
        ivkOut = mi->second;
        return true;
    }
    return false;
}

bool CBasicKeyStore::GetSaplingExtendedSpendingKey(const libzcash::SaplingPaymentAddress &addr,
                                    libzcash::SaplingExtendedSpendingKey &extskOut) const {
    libzcash::SaplingIncomingViewingKey ivk;
    libzcash::SaplingExtendedFullViewingKey extfvk;

    LOCK(cs_KeyStore);
    return GetSaplingIncomingViewingKey(addr, ivk) &&
            GetSaplingFullViewingKey(ivk, extfvk) &&
            GetSaplingSpendingKey(extfvk, extskOut);
}

bool CBasicKeyStore::GetOrchardExtendedSpendingKey(const libzcash::OrchardPaymentAddressPirate &addr,
                                    libzcash::OrchardExtendedSpendingKeyPirate &extskOut) const {
    libzcash::OrchardIncomingViewingKeyPirate ivk;
    libzcash::OrchardExtendedFullViewingKeyPirate extfvk;

    LOCK(cs_KeyStore);
    return GetOrchardIncomingViewingKey(addr, ivk) &&
            GetOrchardFullViewingKey(ivk, extfvk) &&
            GetOrchardSpendingKey(extfvk, extskOut);
}
