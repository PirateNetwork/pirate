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

#ifndef BITCOIN_COINS_H
#define BITCOIN_COINS_H

#define KOMODO_ENABLE_INTEREST //enabling this is a hardfork, activate with new RR method

#include "compressor.h"
#include "core_memusage.h"
#include "memusage.h"
#include "serialize.h"
#include "uint256.h"
#include "base58.h"
#include "pubkey.h"

#include <assert.h>
#include <stdint.h>
#include <vector>
#include <unordered_map>

#include <boost/foreach.hpp>
#include <boost/unordered_map.hpp>
#include "zcash/History.hpp"
#include "zcash/IncrementalMerkleTree.hpp"

/**
 * Pruned version of CTransaction: only retains metadata and unspent transaction outputs
 *
 * Serialized format:
 * - VARINT(nVersion)
 * - VARINT(nCode)
 * - unspentness bitvector, for vout[2] and further; least significant byte first
 * - the non-spent CTxOuts (via CTxOutCompressor)
 * - VARINT(nHeight)
 *
 * The nCode value consists of:
 * - bit 1: IsCoinBase()
 * - bit 2: vout[0] is not spent
 * - bit 4: vout[1] is not spent
 * - The higher bits encode N, the number of non-zero bytes in the following bitvector.
 *   - In case both bit 2 and bit 4 are unset, they encode N-1, as there must be at
 *     least one non-spent output).
 *
 * Example: 0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e
 *          <><><--------------------------------------------><---->
 *          |  \                  |                             /
 *    version   code             vout[1]                  height
 *
 *    - version = 1
 *    - code = 4 (vout[1] is not spent, and 0 non-zero bytes of bitvector follow)
 *    - unspentness bitvector: as 0 non-zero bytes follow, it has length 0
 *    - vout[1]: 835800816115944e077fe7c803cfa57f29b36bf87c1d35
 *               * 8358: compact amount representation for 60000000000 (600 BTC)
 *               * 00: special txout type pay-to-pubkey-hash
 *               * 816115944e077fe7c803cfa57f29b36bf87c1d35: address uint160
 *    - height = 203998
 *
 *
 * Example: 0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b
 *          <><><--><--------------------------------------------------><----------------------------------------------><---->
 *         /  \   \                     |                                                           |                     /
 *  version  code  unspentness       vout[4]                                                     vout[16]           height
 *
 *  - version = 1
 *  - code = 9 (coinbase, neither vout[0] or vout[1] are unspent,
 *                2 (1, +1 because both bit 2 and bit 4 are unset) non-zero bitvector bytes follow)
 *  - unspentness bitvector: bits 2 (0x04) and 14 (0x4000) are set, so vout[2+2] and vout[14+2] are unspent
 *  - vout[4]: 86ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4ee
 *             * 86ef97d579: compact amount representation for 234925952 (2.35 BTC)
 *             * 00: special txout type pay-to-pubkey-hash
 *             * 61b01caab50f1b8e9c50a5057eb43c2d9563a4ee: address uint160
 *  - vout[16]: bbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa4
 *              * bbd123: compact amount representation for 110397 (0.001 BTC)
 *              * 00: special txout type pay-to-pubkey-hash
 *              * 8c988f1a4a4de2161e0f50aac7f17e7f9555caa4: address uint160
 *  - height = 120891
 */
class CCoins
{
public:
    //! whether transaction is a coinbase
    bool fCoinBase;

    //! unspent transaction outputs; spent outputs are .IsNull(); spent outputs at the end of the array are dropped
    std::vector<CTxOut> vout;

    //! at which height this transaction was included in the active block chain
    int nHeight;

    //! version of the CTransaction; accesses to this value should probably check for nHeight as well,
    //! as new tx version will probably only be introduced at certain heights
    int nVersion;
    //uint32_t nLockTime;

    void FromTx(const CTransaction &tx, int nHeightIn) {
        fCoinBase = tx.IsCoinBase();
        vout = tx.vout;
        nHeight = nHeightIn;
        nVersion = tx.nVersion;
        //nLockTime = tx.nLockTime;
        ClearUnspendable();
    }

    //! construct a CCoins from a CTransaction, at a given height
    CCoins(const CTransaction &tx, int nHeightIn) {
        FromTx(tx, nHeightIn);
    }

    void Clear() {
        fCoinBase = false;
        std::vector<CTxOut>().swap(vout);
        nHeight = 0;
        nVersion = 0;
    }

    //! empty constructor
    CCoins() : fCoinBase(false), vout(0), nHeight(0), nVersion(0) { }

    //!remove spent outputs at the end of vout
    void Cleanup() {
        while (vout.size() > 0 && vout.back().IsNull())
            vout.pop_back();
        if (vout.empty())
            std::vector<CTxOut>().swap(vout);
    }

    void ClearUnspendable() {
        BOOST_FOREACH(CTxOut &txout, vout) {
            if (txout.scriptPubKey.IsUnspendable())
                txout.SetNull();
        }
        Cleanup();
    }

    void swap(CCoins &to) {
        std::swap(to.fCoinBase, fCoinBase);
        to.vout.swap(vout);
        std::swap(to.nHeight, nHeight);
        std::swap(to.nVersion, nVersion);
    }

    //! equality test
    friend bool operator==(const CCoins &a, const CCoins &b) {
         // Empty CCoins objects are always equal.
         if (a.IsPruned() && b.IsPruned())
             return true;
         return a.fCoinBase == b.fCoinBase &&
                a.nHeight == b.nHeight &&
                a.nVersion == b.nVersion &&
                a.vout == b.vout;
    }
    friend bool operator!=(const CCoins &a, const CCoins &b) {
        return !(a == b);
    }

    void CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const;

    bool IsCoinBase() const {
        return fCoinBase;
    }

    template<typename Stream>
    void Serialize(Stream &s) const {
        unsigned int nMaskSize = 0, nMaskCode = 0;
        CalcMaskSize(nMaskSize, nMaskCode);
        bool fFirst = vout.size() > 0 && !vout[0].IsNull();
        bool fSecond = vout.size() > 1 && !vout[1].IsNull();
        assert(fFirst || fSecond || nMaskCode);
        unsigned int nCode = 8*(nMaskCode - (fFirst || fSecond ? 0 : 1)) + (fCoinBase ? 1 : 0) + (fFirst ? 2 : 0) + (fSecond ? 4 : 0);
        // version
        ::Serialize(s, VARINT(this->nVersion));
        // header code
        ::Serialize(s, VARINT(nCode));
        // spentness bitmask
        for (unsigned int b = 0; b<nMaskSize; b++) {
            unsigned char chAvail = 0;
            for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++)
                if (!vout[2+b*8+i].IsNull())
                    chAvail |= (1 << i);
            ::Serialize(s, chAvail);
        }
        // txouts themself
        for (unsigned int i = 0; i < vout.size(); i++) {
            if (!vout[i].IsNull())
                ::Serialize(s, CTxOutCompressor(REF(vout[i])));
        }
        // coinbase height
        ::Serialize(s, VARINT(nHeight));
    }

    template<typename Stream>
    void Unserialize(Stream &s) {
        unsigned int nCode = 0;
        // version
        ::Unserialize(s, VARINT(this->nVersion));
        // header code
        ::Unserialize(s, VARINT(nCode));
        fCoinBase = nCode & 1;
        std::vector<bool> vAvail(2, false);
        vAvail[0] = (nCode & 2) != 0;
        vAvail[1] = (nCode & 4) != 0;
        unsigned int nMaskCode = (nCode / 8) + ((nCode & 6) != 0 ? 0 : 1);
        // spentness bitmask
        while (nMaskCode > 0) {
            unsigned char chAvail = 0;
            ::Unserialize(s, chAvail);
            for (unsigned int p = 0; p < 8; p++) {
                bool f = (chAvail & (1 << p)) != 0;
                vAvail.push_back(f);
            }
            if (chAvail != 0)
                nMaskCode--;
        }
        // txouts themself
        vout.assign(vAvail.size(), CTxOut());
        for (unsigned int i = 0; i < vAvail.size(); i++) {
            if (vAvail[i])
                ::Unserialize(s, REF(CTxOutCompressor(vout[i])));
        }
        // coinbase height
        ::Unserialize(s, VARINT(nHeight));
        Cleanup();
    }

    //! mark a vout spent
    bool Spend(uint32_t nPos);

    //! check whether a particular output is still available
    bool IsAvailable(unsigned int nPos) const {
        return (nPos < vout.size() && !vout[nPos].IsNull());
    }

    //! check whether the entire CCoins is spent
    //! note that only !IsPruned() CCoins can be serialized
    bool IsPruned() const {
        BOOST_FOREACH(const CTxOut &out, vout)
            if (!out.IsNull())
                return false;
        return true;
    }

    size_t DynamicMemoryUsage() const {
        size_t ret = memusage::DynamicUsage(vout);
        BOOST_FOREACH(const CTxOut &out, vout) {
            ret += RecursiveDynamicUsage(out.scriptPubKey);
        }
        return ret;
    }

    int64_t TotalTxValue() const {
        int64_t total = 0;
        BOOST_FOREACH(const CTxOut &out, vout) {
            total += out.nValue;
        }
        return total;
    }
};

class CCoinsKeyHasher
{
private:
    uint256 salt;

public:
    CCoinsKeyHasher();

    /**
     * This *must* return size_t. With Boost 1.46 on 32-bit systems the
     * unordered_map will behave unpredictably if the custom hasher returns a
     * uint64_t, resulting in failures when syncing the chain (#4634).
     */
    size_t operator()(const uint256& key) const {
        return key.GetHash(salt);
    }
};

struct CCoinsCacheEntry
{
    CCoins coins; // The actual cached data.
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
        FRESH = (1 << 1), // The parent view does not have this entry (or it is pruned).
    };

    CCoinsCacheEntry() : coins(), flags(0) {}
};

struct CAnchorsSproutCacheEntry
{
    bool entered; // This will be false if the anchor is removed from the cache
    SproutMerkleTree tree; // The tree itself
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CAnchorsSproutCacheEntry() : entered(false), flags(0) {}
};

struct CAnchorsSaplingCacheEntry
{
    bool entered; // This will be false if the anchor is removed from the cache
    SaplingMerkleTree tree; // The tree itself
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CAnchorsSaplingCacheEntry() : entered(false), flags(0) {}
};

struct CAnchorsSaplingFrontierCacheEntry
{
    bool entered; // This will be false if the anchor is removed from the cache
    SaplingMerkleFrontier tree; // The tree itself
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CAnchorsSaplingFrontierCacheEntry() : entered(false), flags(0) {}
};

struct CAnchorsOrchardFrontierCacheEntry
{
    bool entered; // This will be false if the anchor is removed from the cache
    OrchardMerkleFrontier tree; // The tree itself
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CAnchorsOrchardFrontierCacheEntry() : entered(false), flags(0) {}
};

struct CNullifiersCacheEntry
{
    bool entered; // If the nullifier is spent or not
    unsigned char flags;

    enum Flags {
        DIRTY = (1 << 0), // This cache entry is potentially different from the version in the parent view.
    };

    CNullifiersCacheEntry() : entered(false), flags(0) {}
};

enum ShieldedType
{
    SPROUT,
    SAPLING,
    SAPLINGFRONTIER,
    ORCHARDFRONTIER,
};

enum ProofType
{
    OUTPUT,
    SPEND,
};

typedef boost::unordered_map<uint256, CCoinsCacheEntry, CCoinsKeyHasher> CCoinsMap;
typedef boost::unordered_map<uint256, CAnchorsSproutCacheEntry, CCoinsKeyHasher> CAnchorsSproutMap;
typedef boost::unordered_map<uint256, CAnchorsSaplingCacheEntry, CCoinsKeyHasher> CAnchorsSaplingMap;
typedef boost::unordered_map<uint256, CAnchorsSaplingFrontierCacheEntry, CCoinsKeyHasher> CAnchorsSaplingFrontierMap;
typedef boost::unordered_map<uint256, CAnchorsOrchardFrontierCacheEntry, CCoinsKeyHasher> CAnchorsOrchardFrontierMap;
typedef boost::unordered_map<uint256, CNullifiersCacheEntry, CCoinsKeyHasher> CNullifiersMap;
typedef boost::unordered_map<uint32_t, HistoryCache> CHistoryCacheMap;

struct CCoinsStats
{
    int nHeight;
    uint256 hashBlock;
    uint64_t nTransactions;
    uint64_t nTransactionOutputs;
    uint64_t nSerializedSize;
    uint256 hashSerialized;
    CAmount nTotalAmount;

    CCoinsStats() : nHeight(0), nTransactions(0), nTransactionOutputs(0), nSerializedSize(0), nTotalAmount(0) {}
};


/** Abstract view on the open txout dataset. */
class CCoinsView
{
public:
    //! Retrieve the tree (Sprout) at a particular anchored root in the chain
    virtual bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const;

    //! Retrieve the tree (Sapling) at a particular anchored root in the chain
    virtual bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const;

    //! Retrieve the tree (SaplingFroniter) at a particular anchored root in the chain
    virtual bool GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const;

    //! Retrieve the tree (OrchardFroniter) at a particular anchored root in the chain
    virtual bool GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const;

    //! Determine whether a nullifier is spent or not
    virtual bool GetNullifier(const uint256 &nullifier, ShieldedType type) const;

    //! Retrieve the CCoins (unspent transaction outputs) for a given txid
    virtual bool GetCoins(const uint256 &txid, CCoins &coins) const;

    //! Just check whether we have data for a given txid.
    //! This may (but cannot always) return true for fully spent transactions
    virtual bool HaveCoins(const uint256 &txid) const;

    //! Retrieve the block hash whose state this CCoinsView currently represents
    virtual uint256 GetBestBlock() const;

    //! Get the current "tip" or the latest anchored tree root in the chain
    virtual uint256 GetBestAnchor(ShieldedType type) const;

    //! Get the current chain history length (which should be roughly chain height x2)
    virtual HistoryIndex GetHistoryLength(uint32_t epochId) const;

    //! Get history node at specified index
    virtual HistoryNode GetHistoryAt(uint32_t epochId, HistoryIndex index) const;

    //! Get current history root
    virtual uint256 GetHistoryRoot(uint32_t epochId) const;

    //! Do a bulk modification (multiple CCoins changes + BestBlock change).
    //! The passed mapCoins can be modified.
    virtual bool BatchWrite(CCoinsMap &mapCoins,
                            const uint256 &hashBlock,
                            const uint256 &hashSproutAnchor,
                            const uint256 &hashSaplingAnchor,
                            const uint256 &hashSaplingFrontierAnchor,
                            const uint256 &hashOrchardFrontierAnchor,
                            CAnchorsSproutMap &mapSproutAnchors,
                            CAnchorsSaplingMap &mapSaplingAnchors,
                            CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                            CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                            CNullifiersMap &mapSproutNullifiers,
                            CNullifiersMap &mapSaplingNullifiers,
                            CNullifiersMap &mapOrchardNullifiers,
                            CHistoryCacheMap &historyCacheMap);

    //! Calculate statistics about the unspent transaction output set
    virtual bool GetStats(CCoinsStats &stats) const;

    //! As we use CCoinsViews polymorphically, have a virtual destructor
    virtual ~CCoinsView() {}
};


/** CCoinsView backed by another CCoinsView */
class CCoinsViewBacked : public CCoinsView
{
protected:
    CCoinsView *base;

public:
    CCoinsViewBacked(CCoinsView *viewIn);
    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const;
    bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const;
    bool GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const;
    bool GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const;
    bool GetNullifier(const uint256 &nullifier, ShieldedType type) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor(ShieldedType type) const;
    HistoryIndex GetHistoryLength(uint32_t epochId) const;
    HistoryNode GetHistoryAt(uint32_t epochId, HistoryIndex index) const;
    uint256 GetHistoryRoot(uint32_t epochId) const;
    void SetBackend(CCoinsView &viewIn);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    const uint256 &hashSaplingFrontierAnchor,
                    const uint256 &hashOrchardFrontierAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                    CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers,
                    CNullifiersMap &mapOrchardNullifiers,
                    CHistoryCacheMap &historyCacheMap);
    bool GetStats(CCoinsStats &stats) const;
};


class CCoinsViewCache;

/**
 * A reference to a mutable cache entry. Encapsulating it allows us to run
 *  cleanup code after the modification is finished, and keeping track of
 *  concurrent modifications.
 */
class CCoinsModifier
{
private:
    CCoinsViewCache& cache;
    CCoinsMap::iterator it;
    size_t cachedCoinUsage; // Cached memory usage of the CCoins object before modification
    CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage);

public:
    CCoins* operator->() { return &it->second.coins; }
    CCoins& operator*() { return it->second.coins; }
    ~CCoinsModifier();
    friend class CCoinsViewCache;
};

class CTransactionExceptionData
{
    public:
        CScript scriptPubKey;
        uint64_t voutMask;
        CTransactionExceptionData() : scriptPubKey(), voutMask() {}
};

/**
 * CCoinsView that adds a memory cache in front of another CCoinsView
 */
class CCoinsViewCache : public CCoinsViewBacked
{
protected:
    /* Whether this cache has an active modifier. */
    bool hasModifier;

    /**
     * Make mutable so that we can "fill the cache" even from Get-methods
     * declared as "const".
     */
    mutable uint256 hashBlock;
    mutable CCoinsMap cacheCoins;
    mutable uint256 hashSproutAnchor;
    mutable uint256 hashSaplingAnchor;
    mutable uint256 hashSaplingFrontierAnchor;
    mutable uint256 hashOrchardFrontierAnchor;
    mutable CAnchorsSproutMap cacheSproutAnchors;
    mutable CAnchorsSaplingMap cacheSaplingAnchors;
    mutable CAnchorsSaplingFrontierMap cacheSaplingFrontierAnchors;
    mutable CAnchorsOrchardFrontierMap cacheOrchardFrontierAnchors;
    mutable CNullifiersMap cacheSproutNullifiers;
    mutable CNullifiersMap cacheSaplingNullifiers;
    mutable CNullifiersMap cacheOrchardNullifiers;
    mutable CHistoryCacheMap historyCacheMap;

    /* Cached dynamic memory usage for the inner CCoins objects. */
    mutable size_t cachedCoinsUsage;

public:
    CCoinsViewCache(CCoinsView *baseIn);
    ~CCoinsViewCache();

    // Standard CCoinsView methods
    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const;
    bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const;
    bool GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const;
    bool GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const;
    bool GetNullifier(const uint256 &nullifier, ShieldedType type) const;
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor(ShieldedType type) const;
    HistoryIndex GetHistoryLength(uint32_t epochId) const;
    HistoryNode GetHistoryAt(uint32_t epochId, HistoryIndex index) const;
    uint256 GetHistoryRoot(uint32_t epochId) const;
    void SetBestBlock(const uint256 &hashBlock);
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    const uint256 &hashSaplingFrontierAnchor,
                    const uint256 &hashOrchardFrontierAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                    CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers,
                    CNullifiersMap &mapOrchardNullifiers,
                    CHistoryCacheMap &historyCacheMap);


    // Adds the tree to mapSproutAnchors (or mapSaplingAnchors based on the type of tree)
    // and sets the current commitment root to this root.
    template<typename Tree> void PushAnchor(const Tree &tree);

    // Removes the current commitment root from mapAnchors and sets
    // the new current root.
    void PopAnchor(const uint256 &rt, ShieldedType type);

    // Marks nullifiers for a given transaction as spent or not.
    void SetNullifiers(const CTransaction& tx, bool spent);

    // Push MMR node history at the end of the history tree
    void PushHistoryNode(uint32_t epochId, const HistoryNode node);

    // Pop MMR node history from the end of the history tree
    void PopHistoryNode(uint32_t epochId);

    /**
     * Return a pointer to CCoins in the cache, or NULL if not found. This is
     * more efficient than GetCoins. Modifications to other cache entries are
     * allowed while accessing the returned pointer.
     */
    const CCoins* AccessCoins(const uint256 &txid) const;

    /**
     * Return a modifiable reference to a CCoins. If no entry with the given
     * txid exists, a new one is created. Simultaneous modifications are not
     * allowed.
     */
    CCoinsModifier ModifyCoins(const uint256 &txid);

    /**
     * Push the modifications applied to this cache to its base.
     * Failure to call this method before destruction will cause the changes to be forgotten.
     * If false is returned, the state of this cache (and its backing view) will be undefined.
     */
    bool Flush();

    //! Calculate the size of the cache (in number of transactions)
    unsigned int GetCacheSize() const;

    //! Calculate the size of the cache (in bytes)
    size_t DynamicMemoryUsage() const;

    /**
     * @brief get amount of bitcoins coming in to a transaction
     * @note lightweight clients may not know anything besides the hash of previous transactions,
     * so may not be able to calculate this.
     * @param[in] nHeight the chain height
     * @param[out] interestp the interest found
     * @param[in] tx	transaction for which we are checking input total
     * @return	Sum of value of all inputs (scriptSigs), JoinSplit vpub_new, and
     *          positive values of valueBalanceSapling, and valueBalanceOrchard.
     */
    CAmount GetValueIn(int32_t nHeight,int64_t &interestp,const CTransaction& tx) const;

    /**
     * Amount of coins coming in to a transaction in the transparent inputs.
     *
     * @param[in] nHeight the chain height
     * @param[out] interestp the interest found
     * @param[in] tx	transaction for which we are checking input total
     * @return	Sum of value of all inputs (scriptSigs)
     */
    CAmount GetTransparentValueIn(int32_t nHeight,int64_t &interestp,const CTransaction& tx) const;

    //! Check whether all prevouts of the transaction are present in the UTXO set represented by this view
    bool HaveInputs(const CTransaction& tx) const;

    //! Check whether all joinsplit requirements (anchors/nullifiers) are satisfied
    bool HaveJoinSplitRequirements(const CTransaction& tx, int maxProcessingThreads) const;

    //! Return priority of tx at height nHeight
    double GetPriority(const CTransaction &tx, int nHeight) const;

    const CTxOut &GetOutputFor(const CTxIn& input) const;
    const CScript &GetSpendFor(const CTxIn& input) const;
    static const CScript &GetSpendFor(const CCoins *coins, const CTxIn& input);

    friend class CCoinsModifier;

private:
    CCoinsMap::iterator FetchCoins(const uint256 &txid);
    CCoinsMap::const_iterator FetchCoins(const uint256 &txid) const;

    /**
     * By making the copy constructor private, we prevent accidentally using it when one intends to create a cache on top of a base cache.
     */
    CCoinsViewCache(const CCoinsViewCache &);

    //! Generalized interface for popping anchors
    template<typename Tree, typename Cache, typename CacheEntry>
    void AbstractPopAnchor(
        const uint256 &newrt,
        ShieldedType type,
        Cache &cacheAnchors,
        uint256 &hash
    );

    //! Generalized interface for pushing anchors
    template<typename Tree, typename Cache, typename CacheIterator, typename CacheEntry>
    void AbstractPushAnchor(
        const Tree &tree,
        ShieldedType type,
        Cache &cacheAnchors,
        uint256 &hash
    );

    //! Interface for bringing an anchor into the cache.
    template<typename Tree>
    void BringBestAnchorIntoCache(
        const uint256 &currentRoot,
        Tree &tree
    );

    //! Preload history tree for further update.
    //!
    //! If extra = true, extra nodes for deletion are also preloaded.
    //! This will allow to delete tail entries from preloaded tree without
    //! any further database lookups.
    //!
    //! Returns number of peaks, not total number of loaded nodes.
    uint32_t PreloadHistoryTree(uint32_t epochId, bool extra, std::vector<HistoryEntry> &entries, std::vector<uint32_t> &entry_indices);

    //! Selects history cache for specified epoch.
    HistoryCache& SelectHistoryCache(uint32_t epochId) const;
};

#endif // BITCOIN_COINS_H
