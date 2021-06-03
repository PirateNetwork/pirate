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
#include <boost/variant.hpp>

/** A virtual base class for key stores */
class CKeyStore
{
protected:
    mutable CCriticalSection cs_KeyStore;
    mutable CCriticalSection cs_SpendingKeyStore;

public:
    virtual ~CKeyStore() {}

    //! Set the HD seed for this keystore
    virtual bool SetHDSeed(const HDSeed& seed) =0;
    virtual bool HaveHDSeed() const =0;
    //! Get the HD seed for this keystore
    virtual bool GetHDSeed(HDSeed& seedOut) const =0;
    //! Get the Bipp-39 seed phrase
    virtual bool GetSeedPhrase(std::string& phraseOut) const =0;

    //! Add a key to the store.
    virtual bool AddKeyPubKey(const CKey &key, const CPubKey &pubkey) =0;
    virtual bool AddKey(const CKey &key);

    //! Check whether a key corresponding to a given address is present in the store.
    virtual bool HaveKey(const CKeyID &address) const =0;
    virtual bool GetKey(const CKeyID &address, CKey& keyOut) const =0;
    virtual void GetKeys(std::set<CKeyID> &setAddress) const =0;
    virtual bool GetPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const;

    //! Support for BIP 0013 : see https://github.com/bitcoin/bips/blob/master/bip-0013.mediawiki
    virtual bool AddCScript(const CScript& redeemScript) =0;
    virtual bool HaveCScript(const CScriptID &hash) const =0;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const =0;

    //! Support for Watch-only addresses
    virtual bool AddWatchOnly(const CScript &dest) =0;
    virtual bool AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool RemoveWatchOnly(const CScript &dest) =0;
    virtual bool RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool HaveWatchOnly(const CScript &dest) const =0;
    virtual bool HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const =0;
    virtual bool HaveWatchOnly() const =0;

    //! Add a spending key to the store.
    virtual bool AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk) =0;

    //! Check whether a spending key corresponding to a given payment address is present in the store.
    virtual bool HaveSproutSpendingKey(const libzcash::SproutPaymentAddress &address) const =0;
    virtual bool GetSproutSpendingKey(const libzcash::SproutPaymentAddress &address, libzcash::SproutSpendingKey& skOut) const =0;
    virtual void GetSproutPaymentAddresses(std::set<libzcash::SproutPaymentAddress> &setAddress) const =0;

    //! Add a Sapling spending key to the store.
    virtual bool AddSaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &sk) =0;

    //! Check whether a Sapling spending key corresponding to a given Sapling viewing key is present in the store.
    virtual bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const =0;
    virtual bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey& skOut) const =0;

    //! Support for Sapling full viewing keys
    virtual bool AddSaplingFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk) =0;
    virtual bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const =0;
    virtual bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const =0;

    //! Sapling incoming viewing keys
    virtual bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr) =0;
    virtual bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const =0;
    virtual bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingIncomingViewingKey& ivkOut) const =0;
    virtual void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const =0;

    //! Sapling diversified addfresses
    virtual bool AddSaplingDiversifiedAddess(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path) =0;

    virtual bool AddLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path) =0;

    //! Support for Sprout viewing keys
    virtual bool AddSproutViewingKey(const libzcash::SproutViewingKey &vk) =0;
    virtual bool RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk) =0;
    virtual bool HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const =0;
    virtual bool GetSproutViewingKey(
        const libzcash::SproutPaymentAddress &address,
        libzcash::SproutViewingKey& vkOut) const =0;
};

typedef std::map<CKeyID, CKey> KeyMap;
typedef std::map<CScriptID, CScript > ScriptMap;
typedef std::set<CScript> WatchOnlySet;
typedef std::set<libzcash::SaplingExtendedFullViewingKey> SaplingWatchOnlySet;
typedef std::map<libzcash::SproutPaymentAddress, libzcash::SproutSpendingKey> SproutSpendingKeyMap;
typedef std::map<libzcash::SproutPaymentAddress, libzcash::SproutViewingKey> SproutViewingKeyMap;
typedef std::map<libzcash::SproutPaymentAddress, ZCNoteDecryption> NoteDecryptorMap;

// Full viewing key has equivalent functionality to a transparent address
// When encrypting wallet, encrypt SaplingSpendingKeyMap, while leaving SaplingFullViewingKeyMap unencrypted
typedef std::map<libzcash::SaplingExtendedFullViewingKey, libzcash::SaplingExtendedSpendingKey> SaplingSpendingKeyMap;
typedef std::map<libzcash::SaplingIncomingViewingKey, libzcash::SaplingExtendedFullViewingKey> SaplingFullViewingKeyMap;
// Only maps from default addresses to ivk, may need to be reworked when adding diversified addresses.
typedef std::map<libzcash::SaplingPaymentAddress, libzcash::SaplingIncomingViewingKey> SaplingIncomingViewingKeyMap;
typedef std::set<libzcash::SaplingIncomingViewingKey> SaplingIncomingViewingKeySet;
typedef std::set<uint256> SaplingOutgoingViewingKeySet;

//diversified addresses
typedef std::pair<libzcash::SaplingIncomingViewingKey, blob88> DiversifierPath;
typedef std::map<libzcash::SaplingPaymentAddress, DiversifierPath> SaplingPaymentAddresses;

typedef std::map<libzcash::SaplingIncomingViewingKey, blob88> LastDiversifierPath;

/** Basic key store, that keeps keys in an address->secret map */
class CBasicKeyStore : public CKeyStore
{
protected:
    HDSeed hdSeed;
    KeyMap mapKeys;
    ScriptMap mapScripts;
    WatchOnlySet setWatchOnly;
    SaplingWatchOnlySet setSaplingWatchOnly;
    SproutSpendingKeyMap mapSproutSpendingKeys;
    SproutViewingKeyMap mapSproutViewingKeys;
    NoteDecryptorMap mapNoteDecryptors;

    SaplingSpendingKeyMap mapSaplingSpendingKeys;
    SaplingFullViewingKeyMap mapSaplingFullViewingKeys;
    SaplingIncomingViewingKeyMap mapSaplingIncomingViewingKeys;
    SaplingIncomingViewingKeySet setSaplingIncomingViewingKeys;
    SaplingOutgoingViewingKeySet setSaplingOutgoingViewingKeys;
    SaplingPaymentAddresses mapSaplingPaymentAddresses;
    LastDiversifierPath mapLastDiversifierPath;

public:
    bool SetHDSeed(const HDSeed& seed);
    bool HaveHDSeed() const;
    bool GetHDSeed(HDSeed& seedOut) const;
    bool GetSeedPhrase(std::string& phraseOut) const;

    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    bool HaveKey(const CKeyID &address) const
    {
        bool result;
        {
            LOCK(cs_KeyStore);
            result = (mapKeys.count(address) > 0);
        }
        return result;
    }
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
    virtual bool AddCScript(const CScript& redeemScript);
    virtual bool HaveCScript(const CScriptID &hash) const;
    virtual bool GetCScript(const CScriptID &hash, CScript& redeemScriptOut) const;

    virtual bool AddWatchOnly(const CScript &dest);
    virtual bool AddSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool RemoveWatchOnly(const CScript &dest);
    virtual bool RemoveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool HaveWatchOnly(const CScript &dest) const;
    virtual bool HaveSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk) const;
    virtual bool HaveWatchOnly() const;

    bool AddSproutSpendingKey(const libzcash::SproutSpendingKey &sk);
    bool HaveSproutSpendingKey(const libzcash::SproutPaymentAddress &address) const
    {
        bool result;
        {
            LOCK(cs_SpendingKeyStore);
            result = (mapSproutSpendingKeys.count(address) > 0);
        }
        return result;
    }
    bool GetSproutSpendingKey(const libzcash::SproutPaymentAddress &address, libzcash::SproutSpendingKey &skOut) const
    {
        {
            LOCK(cs_SpendingKeyStore);
            SproutSpendingKeyMap::const_iterator mi = mapSproutSpendingKeys.find(address);
            if (mi != mapSproutSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }
    bool GetNoteDecryptor(const libzcash::SproutPaymentAddress &address, ZCNoteDecryption &decOut) const
    {
        {
            LOCK(cs_SpendingKeyStore);
            NoteDecryptorMap::const_iterator mi = mapNoteDecryptors.find(address);
            if (mi != mapNoteDecryptors.end())
            {
                decOut = mi->second;
                return true;
            }
        }
        return false;
    }
    void GetSproutPaymentAddresses(std::set<libzcash::SproutPaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_SpendingKeyStore);
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

    //! Sapling
    void GetSaplingIncomingViewingKeySet(SaplingIncomingViewingKeySet &ivks) {
        LOCK(cs_SpendingKeyStore);
        for (SaplingIncomingViewingKeySet::iterator it = setSaplingIncomingViewingKeys.begin(); it != setSaplingIncomingViewingKeys.end(); it++) {
            ivks.insert(*it);
        }
    }
    void GetSaplingOutgoingViewingKeySet(SaplingOutgoingViewingKeySet &ovks) {
        LOCK(cs_SpendingKeyStore);
        for (SaplingOutgoingViewingKeySet::iterator it = setSaplingOutgoingViewingKeys.begin(); it != setSaplingOutgoingViewingKeys.end(); it++) {
            ovks.insert(*it);
        }
    }

    bool AddSaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &sk);
    bool HaveSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk) const
    {
        bool result;
        {
            LOCK(cs_SpendingKeyStore);
            result = (mapSaplingSpendingKeys.count(extfvk) > 0);
        }
        return result;
    }
    bool GetSaplingSpendingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk, libzcash::SaplingExtendedSpendingKey &skOut) const
    {
        {
            LOCK(cs_SpendingKeyStore);

            SaplingSpendingKeyMap::const_iterator mi = mapSaplingSpendingKeys.find(extfvk);
            if (mi != mapSaplingSpendingKeys.end())
            {
                skOut = mi->second;
                return true;
            }
        }
        return false;
    }

    virtual bool AddSaplingFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk);
    virtual bool HaveSaplingFullViewingKey(const libzcash::SaplingIncomingViewingKey &ivk) const;
    virtual bool GetSaplingFullViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        libzcash::SaplingExtendedFullViewingKey& extfvkOut) const;

    virtual bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr);
    virtual bool HaveSaplingIncomingViewingKey(const libzcash::SaplingPaymentAddress &addr) const;
    virtual bool GetSaplingIncomingViewingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingIncomingViewingKey& ivkOut) const;

    bool GetSaplingExtendedSpendingKey(
        const libzcash::SaplingPaymentAddress &addr,
        libzcash::SaplingExtendedSpendingKey &extskOut) const;

    void GetSaplingPaymentAddresses(std::set<libzcash::SaplingPaymentAddress> &setAddress) const
    {
        setAddress.clear();
        {
            LOCK(cs_SpendingKeyStore);
            auto mi = mapSaplingIncomingViewingKeys.begin();
            while (mi != mapSaplingIncomingViewingKeys.end())
            {
                setAddress.insert((*mi).first);
                mi++;
            }
        }
    }

    //! Sapling diversified addfresses
    virtual bool AddSaplingDiversifiedAddess(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);

    virtual bool AddLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);

    virtual bool AddSproutViewingKey(const libzcash::SproutViewingKey &vk);
    virtual bool RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk);
    virtual bool HaveSproutViewingKey(const libzcash::SproutPaymentAddress &address) const;
    virtual bool GetSproutViewingKey(
        const libzcash::SproutPaymentAddress &address,
        libzcash::SproutViewingKey& vkOut) const;
};

typedef std::vector<unsigned char, secure_allocator<unsigned char> > CKeyingMaterial;
typedef std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char> > > CryptedKeyMap;
typedef std::map<libzcash::SproutPaymentAddress, std::vector<unsigned char> > CryptedSproutSpendingKeyMap;

//! Sapling
typedef std::map<libzcash::SaplingExtendedFullViewingKey, std::vector<unsigned char> > CryptedSaplingSpendingKeyMap;

#endif // BITCOIN_KEYSTORE_H
