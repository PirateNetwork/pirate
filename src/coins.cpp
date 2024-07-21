// Copyright (c) 2012-2014 The Bitcoin Core developers
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

#include "coins.h"

#include "memusage.h"
#include "random.h"
#include "version.h"
#include "policy/fees.h"
#include "komodo_defs.h"
#include "importcoin.h"
#include "komodo_utils.h"
#include "komodo_bitcoind.h"
#include "komodo_interest.h"

#include <rust/history.h>

#include <assert.h>

/**
 * calculate number of bytes for the bitmask, and its number of non-zero bytes
 * each bit in the bitmask represents the availability of one output, but the
 * availabilities of the first two outputs are encoded separately
 */
void CCoins::CalcMaskSize(unsigned int &nBytes, unsigned int &nNonzeroBytes) const {
    unsigned int nLastUsedByte = 0;
    for (unsigned int b = 0; 2+b*8 < vout.size(); b++) {
        bool fZero = true;
        for (unsigned int i = 0; i < 8 && 2+b*8+i < vout.size(); i++) {
            if (!vout[2+b*8+i].IsNull()) {
                fZero = false;
                continue;
            }
        }
        if (!fZero) {
            nLastUsedByte = b + 1;
            nNonzeroBytes++;
        }
    }
    nBytes += nLastUsedByte;
}

bool CCoins::Spend(uint32_t nPos)
{
    if (nPos >= vout.size() || vout[nPos].IsNull())
        return false;
    vout[nPos].SetNull();
    Cleanup();
    return true;
}
bool CCoinsView::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const { return false; }
bool CCoinsView::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const { return false; }
bool CCoinsView::GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const { return false; }
bool CCoinsView::GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const { return false; }
bool CCoinsView::GetNullifier(const uint256 &nullifier, ShieldedType type) const { return false; }
bool CCoinsView::GetZkProofHash(const uint256 &zkproofHash, ProofType type, std::set<std::pair<uint256, int>> &txids) const { return false; }
bool CCoinsView::GetCoins(const uint256 &txid, CCoins &coins) const { return false; }
bool CCoinsView::HaveCoins(const uint256 &txid) const { return false; }
uint256 CCoinsView::GetBestBlock() const { return uint256(); }
uint256 CCoinsView::GetBestAnchor(ShieldedType type) const { return uint256(); };
HistoryIndex CCoinsView::GetHistoryLength(uint32_t epochId) const { return 0; }
HistoryNode CCoinsView::GetHistoryAt(uint32_t epochId, HistoryIndex index) const { return HistoryNode(); }
uint256 CCoinsView::GetHistoryRoot(uint32_t epochId) const { return uint256(); }
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins,
                            const uint256 &hashBlock,
                            const uint256 &hashSproutAnchor,
                            const uint256 &hashSaplingAnchor,
                            const uint256 &hashSaplingFontierAnchor,
                            const uint256 &hashOrchardFrontierAnchor,
                            CAnchorsSproutMap &mapSproutAnchors,
                            CAnchorsSaplingMap &mapSaplingAnchors,
                            CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                            CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                            CNullifiersMap &mapSproutNullifiers,
                            CNullifiersMap &mapSaplingNullifiers,
                            CNullifiersMap &mapOrchardNullifiers,
                            CHistoryCacheMap &historyCacheMap,
                            CProofHashMap &mapZkOutputProofHash,
                            CProofHashMap &mapZkSpendProofHash) { return false; }
bool CCoinsView::GetStats(CCoinsStats &stats) const { return false; }


CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) { }

bool CCoinsViewBacked::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const { return base->GetSproutAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const { return base->GetSaplingAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const { return base->GetSaplingFrontierAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const { return base->GetOrchardFrontierAnchorAt(rt, tree); }
bool CCoinsViewBacked::GetNullifier(const uint256 &nullifier, ShieldedType type) const { return base->GetNullifier(nullifier, type); }
bool CCoinsViewBacked::GetZkProofHash(const uint256 &zkproofHash, ProofType type, std::set<std::pair<uint256, int>> &txids) const { return base->GetZkProofHash(zkproofHash, type, txids); }
bool CCoinsViewBacked::GetCoins(const uint256 &txid, CCoins &coins) const { return base->GetCoins(txid, coins); }
bool CCoinsViewBacked::HaveCoins(const uint256 &txid) const { return base->HaveCoins(txid); }
uint256 CCoinsViewBacked::GetBestBlock() const { return base->GetBestBlock(); }
uint256 CCoinsViewBacked::GetBestAnchor(ShieldedType type) const { return base->GetBestAnchor(type); }
HistoryIndex CCoinsViewBacked::GetHistoryLength(uint32_t epochId) const { return base->GetHistoryLength(epochId); }
HistoryNode CCoinsViewBacked::GetHistoryAt(uint32_t epochId, HistoryIndex index) const { return base->GetHistoryAt(epochId, index); }
uint256 CCoinsViewBacked::GetHistoryRoot(uint32_t epochId) const { return base->GetHistoryRoot(epochId); }
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) { base = &viewIn; }
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins,
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
                                  CHistoryCacheMap &historyCacheMap,
                                  CProofHashMap &mapZkOutputProofHash,
                                  CProofHashMap &mapZkSpendProofHash) {
      return base->BatchWrite(mapCoins, hashBlock,
            hashSproutAnchor, hashSaplingAnchor, hashSaplingFrontierAnchor, hashOrchardFrontierAnchor,
            mapSproutAnchors, mapSaplingAnchors, mapSaplingFrontierAnchors, mapOrchardFrontierAnchors,
            mapSproutNullifiers, mapSaplingNullifiers, mapOrchardNullifiers,
            historyCacheMap,
            mapZkOutputProofHash, mapZkSpendProofHash); }


bool CCoinsViewBacked::GetStats(CCoinsStats &stats) const { return base->GetStats(stats); }

CCoinsKeyHasher::CCoinsKeyHasher() : salt(GetRandHash()) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn) : CCoinsViewBacked(baseIn), hasModifier(false), cachedCoinsUsage(0) { }

CCoinsViewCache::~CCoinsViewCache()
{
    assert(!hasModifier);
}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) +
           memusage::DynamicUsage(cacheSproutAnchors) +
           memusage::DynamicUsage(cacheSaplingAnchors) +
           memusage::DynamicUsage(cacheSaplingFrontierAnchors) +
           memusage::DynamicUsage(cacheOrchardFrontierAnchors) +
           memusage::DynamicUsage(cacheSproutNullifiers) +
           memusage::DynamicUsage(cacheSaplingNullifiers) +
           memusage::DynamicUsage(cacheOrchardNullifiers) +
           memusage::DynamicUsage(historyCacheMap) +
           memusage::DynamicUsage(cacheZkOutputProofHash) +
           memusage::DynamicUsage(cacheZkSpendProofHash) +
           cachedCoinsUsage;
}

CCoinsMap::const_iterator CCoinsViewCache::FetchCoins(const uint256 &txid) const {
    CCoinsMap::iterator it = cacheCoins.find(txid);
    if (it != cacheCoins.end())
        return it;
    CCoins tmp;
    if (!base->GetCoins(txid, tmp))
        return cacheCoins.end();
    CCoinsMap::iterator ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry())).first;
    tmp.swap(ret->second.coins);
    if (ret->second.coins.IsPruned()) {
        // The parent only has an empty entry for this txid; we can consider our
        // version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coins.DynamicMemoryUsage();
    return ret;
}


bool CCoinsViewCache::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
    CAnchorsSproutMap::const_iterator it = cacheSproutAnchors.find(rt);
    if (it != cacheSproutAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetSproutAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsSproutMap::iterator ret = cacheSproutAnchors.insert(std::make_pair(rt, CAnchorsSproutCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const {
    CAnchorsSaplingMap::const_iterator it = cacheSaplingAnchors.find(rt);
    if (it != cacheSaplingAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetSaplingAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsSaplingMap::iterator ret = cacheSaplingAnchors.insert(std::make_pair(rt, CAnchorsSaplingCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const {
    CAnchorsSaplingFrontierMap::const_iterator it = cacheSaplingFrontierAnchors.find(rt);
    if (it != cacheSaplingFrontierAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetSaplingFrontierAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsSaplingFrontierMap::iterator ret = cacheSaplingFrontierAnchors.insert(std::make_pair(rt, CAnchorsSaplingFrontierCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const {
    CAnchorsOrchardFrontierMap::const_iterator it = cacheOrchardFrontierAnchors.find(rt);
    if (it != cacheOrchardFrontierAnchors.end()) {
        if (it->second.entered) {
            tree = it->second.tree;
            return true;
        } else {
            return false;
        }
    }

    if (!base->GetOrchardFrontierAnchorAt(rt, tree)) {
        return false;
    }

    CAnchorsOrchardFrontierMap::iterator ret = cacheOrchardFrontierAnchors.insert(std::make_pair(rt, CAnchorsOrchardFrontierCacheEntry())).first;
    ret->second.entered = true;
    ret->second.tree = tree;
    cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();

    return true;
}

bool CCoinsViewCache::GetNullifier(const uint256 &nullifier, ShieldedType type) const {
    CNullifiersMap* cacheToUse;
    switch (type) {
        case SPROUT:
            cacheToUse = &cacheSproutNullifiers;
            break;
        case SAPLING:
            cacheToUse = &cacheSaplingNullifiers;
            break;
        case ORCHARDFRONTIER:
            cacheToUse = &cacheOrchardNullifiers;
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
    }
    CNullifiersMap::iterator it = cacheToUse->find(nullifier);
    if (it != cacheToUse->end())
        return it->second.entered;

    CNullifiersCacheEntry entry;
    bool tmp = base->GetNullifier(nullifier, type);
    entry.entered = tmp;

    cacheToUse->insert(std::make_pair(nullifier, entry));

    return tmp;
}

HistoryIndex CCoinsViewCache::GetHistoryLength(uint32_t epochId) const {
    HistoryCache& historyCache = SelectHistoryCache(epochId);
    return historyCache.length;
}

HistoryNode CCoinsViewCache::GetHistoryAt(uint32_t epochId, HistoryIndex index) const {
    HistoryCache& historyCache = SelectHistoryCache(epochId);

    if (index >= historyCache.length) {
        // Caller should ensure that it is limiting history
        // request to 0..GetHistoryLength(epochId)-1 range
        throw std::runtime_error("Invalid history request");
    }

    if (index >= historyCache.updateDepth) {
        return historyCache.appends[index];
    }

    return base->GetHistoryAt(epochId, index);
}

uint256 CCoinsViewCache::GetHistoryRoot(uint32_t epochId) const {
    return SelectHistoryCache(epochId).root;
}

bool CCoinsViewCache::GetZkProofHash(const uint256 &zkproofHash, ProofType type, std::set<std::pair<uint256, int>> &txids) const {
    CProofHashMap* cacheToUse;

    switch (type) {
        case OUTPUT:
            cacheToUse = &cacheZkOutputProofHash;
            break;
        case SPEND:
            cacheToUse = &cacheZkSpendProofHash;
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
    }

    CProofHashMap::iterator it = cacheToUse->find(zkproofHash);
    if (it != cacheToUse->end()) {
        txids = (*it).second.txids;

        if (txids.size()>1) {
            LogPrintf("Found duplicate zkproof %s\n", zkproofHash.ToString());
            for (std::set<std::pair<uint256, int>>::iterator it = txids.begin(); it != txids.end(); it++) {
                uint256 txid = (*it).first;
                int proofNumber = (*it).second;
                LogPrintf("Txid %s, proofNumber %i\n", txid.ToString(), proofNumber);
            }
        }
        // LogPrintf(" top view zkproof %s found\n", zkproofHash.ToString());
        return !it->second.txids.empty();
    }

    CProofHashCacheEntry entry;
    bool tmp = base->GetZkProofHash(zkproofHash, type, txids);

    if (tmp && txids.size() > 1)
      LogPrintf(" base view zkproof %s found\n", zkproofHash.ToString());

    entry.txids = txids;

    if (txids.size()>1) {
        LogPrintf("Found duplicate zkproof %s\n", zkproofHash.ToString());
        for (std::set<std::pair<uint256, int>>::iterator it = txids.begin(); it != txids.end(); it++) {
            uint256 txid = (*it).first;
            int proofNumber = (*it).second;
            LogPrintf("Txid %s, proofNumber %i\n", txid.ToString(), proofNumber);
        }
    }

    cacheToUse->insert(std::make_pair(zkproofHash, entry));

    return !txids.empty();
}

template<typename Tree, typename Cache, typename CacheIterator, typename CacheEntry>
void CCoinsViewCache::AbstractPushAnchor(
    const Tree &tree,
    ShieldedType type,
    Cache &cacheAnchors,
    uint256 &hash
)
{
    uint256 newrt = tree.root();

    auto currentRoot = GetBestAnchor(type);

    // We don't want to overwrite an anchor we already have.
    // This occurs when a block doesn't modify mapAnchors at all,
    // because there are no joinsplits. We could get around this a
    // different way (make all blocks modify mapAnchors somehow)
    // but this is simpler to reason about.
    if (currentRoot != newrt) {
        auto insertRet = cacheAnchors.insert(std::make_pair(newrt, CacheEntry()));
        CacheIterator ret = insertRet.first;

        ret->second.entered = true;
        ret->second.tree = tree;
        ret->second.flags = CacheEntry::DIRTY;

        if (insertRet.second) {
            // An insert took place
            cachedCoinsUsage += ret->second.tree.DynamicMemoryUsage();
        }

        hash = newrt;
    }
}

template<> void CCoinsViewCache::PushAnchor(const SproutMerkleTree &tree)
{
    AbstractPushAnchor<SproutMerkleTree, CAnchorsSproutMap, CAnchorsSproutMap::iterator, CAnchorsSproutCacheEntry>(
        tree,
        SPROUT,
        cacheSproutAnchors,
        hashSproutAnchor
    );
}

template<> void CCoinsViewCache::PushAnchor(const SaplingMerkleTree &tree)
{
    AbstractPushAnchor<SaplingMerkleTree, CAnchorsSaplingMap, CAnchorsSaplingMap::iterator, CAnchorsSaplingCacheEntry>(
        tree,
        SAPLING,
        cacheSaplingAnchors,
        hashSaplingAnchor
    );
}

template<> void CCoinsViewCache::PushAnchor(const SaplingMerkleFrontier &tree)
{
    AbstractPushAnchor<SaplingMerkleFrontier, CAnchorsSaplingFrontierMap, CAnchorsSaplingFrontierMap::iterator, CAnchorsSaplingFrontierCacheEntry>(
        tree,
        SAPLINGFRONTIER,
        cacheSaplingFrontierAnchors,
        hashSaplingFrontierAnchor
    );
}

template<> void CCoinsViewCache::PushAnchor(const OrchardMerkleFrontier &tree)
{
    AbstractPushAnchor<OrchardMerkleFrontier, CAnchorsOrchardFrontierMap, CAnchorsOrchardFrontierMap::iterator, CAnchorsOrchardFrontierCacheEntry>(
        tree,
        ORCHARDFRONTIER,
        cacheOrchardFrontierAnchors,
        hashOrchardFrontierAnchor
    );
}

template<>
void CCoinsViewCache::BringBestAnchorIntoCache(
    const uint256 &currentRoot,
    SproutMerkleTree &tree
)
{
    assert(GetSproutAnchorAt(currentRoot, tree));
}

template<>
void CCoinsViewCache::BringBestAnchorIntoCache(
    const uint256 &currentRoot,
    SaplingMerkleTree &tree
)
{
    assert(GetSaplingAnchorAt(currentRoot, tree));
}

template<>
void CCoinsViewCache::BringBestAnchorIntoCache(
    const uint256 &currentRoot,
    SaplingMerkleFrontier &tree
)
{
    assert(GetSaplingFrontierAnchorAt(currentRoot, tree));
}

template<>
void CCoinsViewCache::BringBestAnchorIntoCache(
    const uint256 &currentRoot,
    OrchardMerkleFrontier &tree
)
{
    assert(GetOrchardFrontierAnchorAt(currentRoot, tree));
}

void draftMMRNode(std::vector<uint32_t> &indices,
                  std::vector<HistoryEntry> &entries,
                  HistoryNode nodeData,
                  uint32_t alt,
                  uint32_t peak_pos)
{
    // peak_pos - (1 << alt) is the array position of left child.
    // peak_pos - 1 is the array position of right child.
    HistoryEntry newEntry = alt == 0
        ? libzcash::LeafToEntry(nodeData)
        : libzcash::NodeToEntry(nodeData, peak_pos - (1 << alt), peak_pos - 1);

    indices.push_back(peak_pos);
    entries.push_back(newEntry);
}

// Computes floor(log2(x)).
static inline uint32_t floor_log2(uint32_t x) {
    assert(x > 0);
    int log = 0;
    while (x >>= 1) { ++log; }
    return log;
}

// Computes the altitude of the largest subtree for an MMR with n nodes,
// which is floor(log2(n + 1)) - 1.
static inline uint32_t altitude(uint32_t n) {
    return floor_log2(n + 1) - 1;
}

uint32_t CCoinsViewCache::PreloadHistoryTree(uint32_t epochId, bool extra, std::vector<HistoryEntry> &entries, std::vector<uint32_t> &entry_indices) {
    auto treeLength = GetHistoryLength(epochId);

    if (treeLength <= 0) {
        throw std::runtime_error("Invalid PreloadHistoryTree state called - tree should exist");
    } else if (treeLength == 1) {
        entries.push_back(libzcash::LeafToEntry(GetHistoryAt(epochId, 0)));
        entry_indices.push_back(0);
        return 1;
    }

    uint32_t last_peak_pos = 0;
    uint32_t last_peak_alt = 0;
    uint32_t alt = 0;
    uint32_t peak_pos = 0;
    uint32_t total_peaks = 0;

    // Assume the following example peak layout with 14 leaves, and 25 stored nodes in
    // total (the "tree length"):
    //
    //             P
    //            /\
    //           /  \
    //          / \  \
    //        /    \  \  Altitude
    //     _A_      \  \    3
    //   _/   \_     B  \   2
    //  / \   / \   / \  C  1
    // /\ /\ /\ /\ /\ /\ /\ 0
    //
    // We start by determining the altitude of the highest peak (A).
    alt = altitude(treeLength);

    // We determine the position of the highest peak (A) by pretending it is the right
    // sibling in a tree, and its left-most leaf has position 0. Then the left sibling
    // of (A) has position -1, and so we can "jump" to the peak's position by computing
    // -1 + 2^(alt + 1) - 1.
    peak_pos = (1 << (alt + 1)) - 2;

    // Now that we have the position and altitude of the highest peak (A), we collect
    // the remaining peaks (B, C). We navigate the peaks as if they were nodes in this
    // Merkle tree (with additional imaginary nodes 1 and 2, that have positions beyond
    // the MMR's length):
    //
    //             / \
    //            /   \
    //           /     \
    //         /         \
    //       A ==========> 1
    //      / \          //  \
    //    _/   \_       B ==> 2
    //   /\     /\     /\    //
    //  /  \   /  \   /  \   C
    // /\  /\ /\  /\ /\  /\ /\
    //
    while (alt != 0) {
        // If peak_pos is out of bounds of the tree, we compute the position of its left
        // child, and drop down one level in the tree.
        if (peak_pos >= treeLength) {
            // left child, -2^alt
            peak_pos = peak_pos - (1 << alt);
            alt = alt - 1;
        }

        // If the peak exists, we take it and then continue with its right sibling.
        if (peak_pos < treeLength) {
            draftMMRNode(entry_indices, entries, GetHistoryAt(epochId, peak_pos), alt, peak_pos);

            last_peak_pos = peak_pos;
            last_peak_alt = alt;

            // right sibling
            peak_pos = peak_pos + (1 << (alt + 1)) - 1;
        }
    }

    total_peaks = entries.size();

    // Return early if we don't require extra nodes.
    if (!extra) return total_peaks;

    alt = last_peak_alt;
    peak_pos = last_peak_pos;


    //             P
    //            /\
    //           /  \
    //          / \  \
    //        /    \  \
    //     _A_      \  \
    //   _/   \_     B  \
    //  / \   / \   / \  C
    // /\ /\ /\ /\ /\ /\ /\
    //                   D E
    //
    // For extra peaks needed for deletion, we do extra pass on right slope of the last peak
    // and add those nodes + their siblings. Extra would be (D, E) for the picture above.
    while (alt > 0) {
        uint32_t left_pos = peak_pos - (1 << alt);
        uint32_t right_pos = peak_pos - 1;
        alt = alt - 1;

        // drafting left child
        draftMMRNode(entry_indices, entries, GetHistoryAt(epochId, left_pos), alt, left_pos);

        // drafting right child
        draftMMRNode(entry_indices, entries, GetHistoryAt(epochId, right_pos), alt, right_pos);

        // continuing on right slope
        peak_pos = right_pos;
    }

    return total_peaks;
}

HistoryCache& CCoinsViewCache::SelectHistoryCache(uint32_t epochId) const {
    auto entry = historyCacheMap.find(epochId);

    if (entry != historyCacheMap.end()) {
        return entry->second;
    } else {
        auto cache = HistoryCache(
            base->GetHistoryLength(epochId),
            base->GetHistoryRoot(epochId),
            epochId
        );
        return historyCacheMap.insert({epochId, cache}).first->second;
    }
}

void CCoinsViewCache::PushHistoryNode(uint32_t epochId, const HistoryNode node) {
    HistoryCache& historyCache = SelectHistoryCache(epochId);

    if (historyCache.length == 0) {
        // special case, it just goes into the cache right away
        historyCache.Extend(node);

        historyCache.root = uint256::FromRawBytes(mmr::hash_node(epochId, node));

        return;
    }

    std::vector<HistoryEntry> entries;
    std::vector<uint32_t> entry_indices;

    PreloadHistoryTree(epochId, false, entries, entry_indices);

    uint256 newRoot;
    std::array<HistoryNode, 32> appendBuf = {};

    auto effect = mmr::append(
        epochId,
        historyCache.length,
        {entry_indices.data(), entry_indices.size()},
        {entries.data(), entries.size()},
        node,
        {appendBuf.data(), 32}
    );

    for (size_t i = 0; i < effect.count; i++) {
        historyCache.Extend(appendBuf[i]);
    }

    historyCache.root = uint256::FromRawBytes(effect.root);
}

void CCoinsViewCache::PopHistoryNode(uint32_t epochId) {
    HistoryCache& historyCache = SelectHistoryCache(epochId);

    switch (historyCache.length) {
        case 0:
        {
            // Caller is generally not expected to pop from empty tree! Caller
            // should switch to previous epoch and pop history from there.

            // If we are doing an expected rollback that changes the consensus
            // branch ID for some upgrade (or introduces one that wasn't present
            // at the equivalent height) this will occur because
            // `SelectHistoryCache` selects the tree for the new consensus
            // branch ID, not the one that existed on the chain being rolled
            // back.

            // Sensible action is to truncate the history cache:
        }
        case 1:
        {
            // Just resetting tree to empty
            historyCache.Truncate(0);
            historyCache.root = uint256();
            return;
        }
        case 2:
        {
            // - A tree with one leaf has length 1.
            // - A tree with two leaves has length 3.
            throw std::runtime_error("a history tree cannot have two nodes");
        }
        case 3:
        {
            const HistoryNode tmpHistoryRoot = GetHistoryAt(epochId, 0);
            // After removing a leaf from a tree with two leaves, we are left
            // with a single-node tree, whose root is just the hash of that
            // node.
            auto newRoot = mmr::hash_node(
                epochId,
                tmpHistoryRoot);
            historyCache.Truncate(1);
            historyCache.root = uint256::FromRawBytes(newRoot);
            return;
        }
        default:
        {
            // This is a non-elementary pop, so use the full tree logic.
            std::vector<HistoryEntry> entries;
            std::vector<uint32_t> entry_indices;

            uint32_t peak_count = PreloadHistoryTree(epochId, true, entries, entry_indices);

            auto effect = mmr::remove(
                epochId,
                historyCache.length,
                {entry_indices.data(), entry_indices.size()},
                {entries.data(), entries.size()},
                peak_count
            );

            historyCache.Truncate(historyCache.length - effect.count);
            historyCache.root = uint256::FromRawBytes(effect.root);
            return;
        }
    }
}

template<typename Tree, typename Cache, typename CacheEntry>
void CCoinsViewCache::AbstractPopAnchor(
    const uint256 &newrt,
    ShieldedType type,
    Cache &cacheAnchors,
    uint256 &hash
)
{
    auto currentRoot = GetBestAnchor(type);

    // Blocks might not change the commitment tree, in which
    // case restoring the "old" anchor during a reorg must
    // have no effect.
    if (currentRoot != newrt) {
        // Bring the current best anchor into our local cache
        // so that its tree exists in memory.
        {
            Tree tree;
            BringBestAnchorIntoCache(currentRoot, tree);
        }

        // Mark the anchor as unentered, removing it from view
        cacheAnchors[currentRoot].entered = false;

        // Mark the cache entry as dirty so it's propagated
        cacheAnchors[currentRoot].flags = CacheEntry::DIRTY;

        // Mark the new root as the best anchor
        hash = newrt;
    }
}

void CCoinsViewCache::PopAnchor(const uint256 &newrt, ShieldedType type) {
    switch (type) {
        case SPROUT:
            AbstractPopAnchor<SproutMerkleTree, CAnchorsSproutMap, CAnchorsSproutCacheEntry>(
                newrt,
                SPROUT,
                cacheSproutAnchors,
                hashSproutAnchor
            );
            break;
        case SAPLING:
            AbstractPopAnchor<SaplingMerkleTree, CAnchorsSaplingMap, CAnchorsSaplingCacheEntry>(
                newrt,
                SAPLING,
                cacheSaplingAnchors,
                hashSaplingAnchor
            );
            break;
        case SAPLINGFRONTIER:
            AbstractPopAnchor<SaplingMerkleFrontier, CAnchorsSaplingFrontierMap, CAnchorsSaplingFrontierCacheEntry>(
                newrt,
                SAPLINGFRONTIER,
                cacheSaplingFrontierAnchors,
                hashSaplingFrontierAnchor
            );
            break;
        case ORCHARDFRONTIER:
            AbstractPopAnchor<OrchardMerkleFrontier, CAnchorsOrchardFrontierMap, CAnchorsOrchardFrontierCacheEntry>(
                newrt,
                ORCHARDFRONTIER,
                cacheOrchardFrontierAnchors,
                hashOrchardFrontierAnchor
            );
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
    }
}

void CCoinsViewCache::SetNullifiers(const CTransaction& tx, bool spent) {
    for (const JSDescription &joinsplit : tx.vjoinsplit) {
        for (const uint256 &nullifier : joinsplit.nullifiers) {
            std::pair<CNullifiersMap::iterator, bool> ret = cacheSproutNullifiers.insert(std::make_pair(nullifier, CNullifiersCacheEntry()));
            ret.first->second.entered = spent;
            ret.first->second.flags |= CNullifiersCacheEntry::DIRTY;
        }
    }
    for (const auto& spend : tx.GetSaplingSpends()) {
        auto nullifier = uint256::FromRawBytes(spend.nullifier());
        std::pair<CNullifiersMap::iterator, bool> ret = cacheSaplingNullifiers.insert(std::make_pair(nullifier, CNullifiersCacheEntry()));
        ret.first->second.entered = spent;
        ret.first->second.flags |= CNullifiersCacheEntry::DIRTY;
    }
}

void CCoinsViewCache::SetZkProofHashes(const CTransaction& tx, bool addTx) {

    int i = 0;
    for (const auto& spend : tx.GetSaplingSpends()) {
        auto zkproof = spend.zkproof();
        auto proofHash = Hash(zkproof.begin(), zkproof.end());
        std::pair<CProofHashMap::iterator, bool> ret = cacheZkSpendProofHash.insert(std::make_pair(proofHash, CProofHashCacheEntry()));
        if (addTx) {
          ret.first->second.txids.emplace(std::make_pair(tx.GetHash(), i));
        } else {
          ret.first->second.txids.erase(std::make_pair(tx.GetHash(), i));
        }
        ret.first->second.flags |= CProofHashCacheEntry::DIRTY;
        i++;
    }

    i = 0;
    for (const auto& output : tx.GetSaplingOutputs()) {
        auto zkproof = output.zkproof();
        auto proofHash = Hash(zkproof.begin(), zkproof.end());
        std::pair<CProofHashMap::iterator, bool> ret = cacheZkOutputProofHash.insert(std::make_pair(proofHash, CProofHashCacheEntry()));
        if (addTx) {
          ret.first->second.txids.emplace(std::make_pair(tx.GetHash(), i));
        } else {
          ret.first->second.txids.erase(std::make_pair(tx.GetHash(), i));
        }
        ret.first->second.flags |= CProofHashCacheEntry::DIRTY;
        i++;
    }
}

bool CCoinsViewCache::GetCoins(const uint256 &txid, CCoins &coins) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it != cacheCoins.end()) {
        coins = it->second.coins;
        return true;
    }
    return false;
}

CCoinsModifier CCoinsViewCache::ModifyCoins(const uint256 &txid) {
    assert(!hasModifier);
    std::pair<CCoinsMap::iterator, bool> ret = cacheCoins.insert(std::make_pair(txid, CCoinsCacheEntry()));
    size_t cachedCoinUsage = 0;
    if (ret.second) {
        if (!base->GetCoins(txid, ret.first->second.coins)) {
            // The parent view does not have this entry; mark it as fresh.
            ret.first->second.coins.Clear();
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        } else if (ret.first->second.coins.IsPruned()) {
            // The parent view only has a pruned entry for this; mark it as fresh.
            ret.first->second.flags = CCoinsCacheEntry::FRESH;
        }
    } else {
        cachedCoinUsage = ret.first->second.coins.DynamicMemoryUsage();
    }
    // Assume that whenever ModifyCoins is called, the entry will be modified.
    ret.first->second.flags |= CCoinsCacheEntry::DIRTY;
    return CCoinsModifier(*this, ret.first, cachedCoinUsage);
}

const CCoins* CCoinsViewCache::AccessCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    if (it == cacheCoins.end()) {
        return NULL;
    } else {
        return &it->second.coins;
    }
}

bool CCoinsViewCache::HaveCoins(const uint256 &txid) const {
    CCoinsMap::const_iterator it = FetchCoins(txid);
    // We're using vtx.empty() instead of IsPruned here for performance reasons,
    // as we only care about the case where a transaction was replaced entirely
    // in a reorganization (which wipes vout entirely, as opposed to spending
    // which just cleans individual outputs).
    return (it != cacheCoins.end() && !it->second.coins.vout.empty());
}

uint256 CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull())
    {
        if (base)
        {
            hashBlock = base->GetBestBlock();
        }
        else
        {
            hashBlock = uint256();
        }
    }
    return hashBlock;
}


uint256 CCoinsViewCache::GetBestAnchor(ShieldedType type) const {
    switch (type) {
        case SPROUT:
            if (hashSproutAnchor.IsNull())
                hashSproutAnchor = base->GetBestAnchor(type);
            return hashSproutAnchor;
            break;
        case SAPLING:
            if (hashSaplingAnchor.IsNull())
                hashSaplingAnchor = base->GetBestAnchor(type);
            return hashSaplingAnchor;
            break;
        case SAPLINGFRONTIER:
            if (hashSaplingFrontierAnchor.IsNull())
                hashSaplingFrontierAnchor = base->GetBestAnchor(type);
            return hashSaplingFrontierAnchor;
            break;
        case ORCHARDFRONTIER:
            if (hashOrchardFrontierAnchor.IsNull())
                hashOrchardFrontierAnchor = base->GetBestAnchor(type);
            return hashOrchardFrontierAnchor;
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
    }
}

void CCoinsViewCache::SetBestBlock(const uint256 &hashBlockIn) {
    hashBlock = hashBlockIn;
}

void BatchWriteNullifiers(CNullifiersMap &mapNullifiers, CNullifiersMap &cacheNullifiers)
{
    for (CNullifiersMap::iterator child_it = mapNullifiers.begin(); child_it != mapNullifiers.end();) {
        if (child_it->second.flags & CNullifiersCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CNullifiersMap::iterator parent_it = cacheNullifiers.find(child_it->first);

            if (parent_it == cacheNullifiers.end()) {
                CNullifiersCacheEntry& entry = cacheNullifiers[child_it->first];
                entry.entered = child_it->second.entered;
                entry.flags = CNullifiersCacheEntry::DIRTY;
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= CNullifiersCacheEntry::DIRTY;
                }
            }
        }
        CNullifiersMap::iterator itOld = child_it++;
        mapNullifiers.erase(itOld);
    }
}

void BatchWriteProofHashes(CProofHashMap &mapProofHash, CProofHashMap &cacheProofHash)
{
    for (CProofHashMap::iterator child_it = mapProofHash.begin(); child_it != mapProofHash.end();) {
        if (child_it->second.flags & CProofHashCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CProofHashMap::iterator parent_it = cacheProofHash.find(child_it->first);

            if (parent_it == cacheProofHash.end()) {
                CProofHashCacheEntry& entry = cacheProofHash[child_it->first];
                entry.txids = child_it->second.txids;
                entry.flags = CNullifiersCacheEntry::DIRTY;
            } else {
                if (parent_it->second.txids != child_it->second.txids) {
                    parent_it->second.txids = child_it->second.txids;
                    parent_it->second.flags |= CNullifiersCacheEntry::DIRTY;
                }
            }
        }
        CProofHashMap::iterator itOld = child_it++;
        mapProofHash.erase(itOld);
    }
}

template<typename Map, typename MapIterator, typename MapEntry>
void BatchWriteAnchors(
    Map &mapAnchors,
    Map &cacheAnchors,
    size_t &cachedCoinsUsage
)
{
    for (MapIterator child_it = mapAnchors.begin(); child_it != mapAnchors.end();)
    {
        if (child_it->second.flags & MapEntry::DIRTY) {
            MapIterator parent_it = cacheAnchors.find(child_it->first);

            if (parent_it == cacheAnchors.end()) {
                MapEntry& entry = cacheAnchors[child_it->first];
                entry.entered = child_it->second.entered;
                entry.tree = child_it->second.tree;
                entry.flags = MapEntry::DIRTY;

                cachedCoinsUsage += entry.tree.DynamicMemoryUsage();
            } else {
                if (parent_it->second.entered != child_it->second.entered) {
                    // The parent may have removed the entry.
                    parent_it->second.entered = child_it->second.entered;
                    parent_it->second.flags |= MapEntry::DIRTY;
                }
            }
        }

        MapIterator itOld = child_it++;
        mapAnchors.erase(itOld);
    }
}

void BatchWriteHistory(CHistoryCacheMap& historyCacheMap, CHistoryCacheMap& historyCacheMapIn) {
    for (auto nextHistoryCache = historyCacheMapIn.begin(); nextHistoryCache != historyCacheMapIn.end(); nextHistoryCache++) {
        auto historyCacheIn = nextHistoryCache->second;
        auto epochId = nextHistoryCache->first;

        auto historyCache = historyCacheMap.find(epochId);
        if (historyCache != historyCacheMap.end()) {
            // delete old entries since updateDepth
            historyCache->second.Truncate(historyCacheIn.updateDepth);

            // Replace/append new/updated entries. HistoryCache.Extend
            // auto-indexes the nodes, so we need to extend in the same order as
            // this cache is indexed.
            for (size_t i = historyCacheIn.updateDepth; i < historyCacheIn.length; i++) {
                historyCache->second.Extend(historyCacheIn.appends[i]);
            }

            // the lengths should now match
            assert(historyCache->second.length == historyCacheIn.length);

            // write current root
            historyCache->second.root = historyCacheIn.root;
        } else {
            // Just insert the history cache into its parent
            historyCacheMap.insert({epochId, historyCacheIn});
        }
    }
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
                                 const uint256 &hashBlockIn,
                                 const uint256 &hashSproutAnchorIn,
                                 const uint256 &hashSaplingAnchorIn,
                                 const uint256 &hashSaplingFrontierAnchorIn,
                                 const uint256 &hashOrchardFrontierAnchorIn,
                                 CAnchorsSproutMap &mapSproutAnchors,
                                 CAnchorsSaplingMap &mapSaplingAnchors,
                                 CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                                 CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                                 CNullifiersMap &mapSproutNullifiers,
                                 CNullifiersMap &mapSaplingNullifiers,
                                 CNullifiersMap &mapOrchardNullifiers,
                                 CHistoryCacheMap &historyCacheMapIn,
                                 CProofHashMap &mapZkOutputProofHash,
                                 CProofHashMap &mapZkSpendProofHash) {
    assert(!hasModifier);
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) { // Ignore non-dirty entries (optimization).
            CCoinsMap::iterator itUs = cacheCoins.find(it->first);
            if (itUs == cacheCoins.end()) {
                if (!it->second.coins.IsPruned()) {
                    // The parent cache does not have an entry, while the child
                    // cache does have (a non-pruned) one. Move the data up, and
                    // mark it as fresh (if the grandparent did have it, we
                    // would have pulled it in at first GetCoins).
                    assert(it->second.flags & CCoinsCacheEntry::FRESH);
                    CCoinsCacheEntry& entry = cacheCoins[it->first];
                    entry.coins.swap(it->second.coins);
                    cachedCoinsUsage += entry.coins.DynamicMemoryUsage();
                    entry.flags = CCoinsCacheEntry::DIRTY | CCoinsCacheEntry::FRESH;
                }
            } else {
                if ((itUs->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
                    // The grandparent does not have an entry, and the child is
                    // modified and being pruned. This means we can just delete
                    // it from the parent.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    cacheCoins.erase(itUs);
                } else {
                    // A normal modification.
                    cachedCoinsUsage -= itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.coins.swap(it->second.coins);
                    cachedCoinsUsage += itUs->second.coins.DynamicMemoryUsage();
                    itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                }
            }
        }
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    ::BatchWriteAnchors<CAnchorsSproutMap, CAnchorsSproutMap::iterator, CAnchorsSproutCacheEntry>(mapSproutAnchors, cacheSproutAnchors, cachedCoinsUsage);
    ::BatchWriteAnchors<CAnchorsSaplingMap, CAnchorsSaplingMap::iterator, CAnchorsSaplingCacheEntry>(mapSaplingAnchors, cacheSaplingAnchors, cachedCoinsUsage);
    ::BatchWriteAnchors<CAnchorsSaplingFrontierMap, CAnchorsSaplingFrontierMap::iterator, CAnchorsSaplingFrontierCacheEntry>(mapSaplingFrontierAnchors, cacheSaplingFrontierAnchors, cachedCoinsUsage);
    ::BatchWriteAnchors<CAnchorsOrchardFrontierMap, CAnchorsOrchardFrontierMap::iterator, CAnchorsOrchardFrontierCacheEntry>(mapOrchardFrontierAnchors, cacheOrchardFrontierAnchors, cachedCoinsUsage);

    ::BatchWriteNullifiers(mapSproutNullifiers, cacheSproutNullifiers);
    ::BatchWriteNullifiers(mapSaplingNullifiers, cacheSaplingNullifiers);
    ::BatchWriteNullifiers(mapOrchardNullifiers, cacheOrchardNullifiers);

    ::BatchWriteHistory(historyCacheMap, historyCacheMapIn);

    ::BatchWriteProofHashes(mapZkOutputProofHash, cacheZkOutputProofHash);
    ::BatchWriteProofHashes(mapZkSpendProofHash, cacheZkSpendProofHash);

    hashSproutAnchor = hashSproutAnchorIn;
    hashSaplingAnchor = hashSaplingAnchorIn;
    hashSaplingFrontierAnchor = hashSaplingFrontierAnchorIn;
    hashOrchardFrontierAnchor = hashOrchardFrontierAnchorIn;
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock,
                                hashSproutAnchor, hashSaplingAnchor, hashSaplingFrontierAnchor, hashOrchardFrontierAnchor,
                                cacheSproutAnchors, cacheSaplingAnchors, cacheSaplingFrontierAnchors, cacheOrchardFrontierAnchors,
                                cacheSproutNullifiers, cacheSaplingNullifiers, cacheOrchardNullifiers,
                                historyCacheMap,
                                cacheZkOutputProofHash, cacheZkSpendProofHash);
    cacheCoins.clear();
    cacheSproutAnchors.clear();
    cacheSaplingAnchors.clear();
    cacheSaplingFrontierAnchors.clear();
    cacheOrchardFrontierAnchors.clear();
    cacheSproutNullifiers.clear();
    cacheSaplingNullifiers.clear();
    cacheOrchardNullifiers.clear();
    historyCacheMap.clear();
    cacheZkOutputProofHash.clear();
    cacheZkSpendProofHash.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

const CTxOut &CCoinsViewCache::GetOutputFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    assert(coins && coins->IsAvailable(input.prevout.n));
    return coins->vout[input.prevout.n];
}

const CScript &CCoinsViewCache::GetSpendFor(const CCoins *coins, const CTxIn& input)
{
    assert(coins);
    return coins->vout[input.prevout.n].scriptPubKey;
}

const CScript &CCoinsViewCache::GetSpendFor(const CTxIn& input) const
{
    const CCoins* coins = AccessCoins(input.prevout.hash);
    return GetSpendFor(coins, input);
}

/**
 * @brief get amount of bitcoins coming in to a transaction
 * @note lightweight clients may not know anything besides the hash of previous transactions,
 * so may not be able to calculate this.
 * @param[in] nHeight the chain height
 * @param[out] interestp the interest found
 * @param[in] tx transaction for which we are checking input total
 * @returns Sum of value of all inputs (scriptSigs), (positive valueBalance or zero) and JoinSplit vpub_new
 */
CAmount CCoinsViewCache::GetValueIn(int32_t nHeight,int64_t &interestp,const CTransaction& tx) const
{
    CAmount value,nResult = 0;
    interestp = 0;
    if ( tx.IsCoinImport() )
        return GetCoinImportValue(tx);
    if ( tx.IsCoinBase() != 0 )
        return 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        value = GetOutputFor(tx.vin[i]).nValue;
        nResult += value;
#ifdef KOMODO_ENABLE_INTEREST
        if ( chainName.isKMD() && nHeight >= 60000 )
        {
            if ( value >= 10*COIN )
            {
                int64_t interest;
                int32_t txheight;
                uint32_t locktime;
                interest = komodo_accrued_interest(&txheight,&locktime,tx.vin[i].prevout.hash,
                        tx.vin[i].prevout.n,0,value,nHeight);
                nResult += interest;
                interestp += interest;
            }
        }
#endif
    }
    nResult += tx.GetShieldedValueIn();

    return nResult;
}

static bool HaveJoinSplitRequirementsWorkerNullifier(const CCoinsViewCache *coinCache,const std::vector<uint256> vSpendNullifer, int threadNum)
{
    //Perform Sapling Spend checks
    for (int i = 0; i < vSpendNullifer.size(); i++) {
        auto nullifier = vSpendNullifer[i];
        if (coinCache->GetNullifier(nullifier, SAPLING)) // Prevent double spends
            return false;
    }

    return true;
}

static bool HaveJoinSplitRequirementsWorkerAnchor(const CCoinsViewCache *coinCache,const std::vector<uint256> vSpendAnchor, int threadNum)
{
    //Perform Sapling Spend checks
    for (int i = 0; i < vSpendAnchor.size(); i++) {
        auto anchor = vSpendAnchor[i];
        SaplingMerkleTree tree;
        if (!coinCache->GetSaplingAnchorAt(anchor, tree)) {
            return false;
        }
    }

    return true;
}

bool CCoinsViewCache::HaveJoinSplitRequirements(const CTransaction& tx, int maxProcessingThreads) const
{
    boost::unordered_map<uint256, SproutMerkleTree, CCoinsKeyHasher> intermediates;

    BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit)
    {
        BOOST_FOREACH(const uint256& nullifier, joinsplit.nullifiers)
        {
            if (GetNullifier(nullifier, SPROUT)) {
                // If the nullifier is set, this transaction
                // double-spends!
                return false;
            }
        }

        SproutMerkleTree tree;
        auto it = intermediates.find(joinsplit.anchor);
        if (it != intermediates.end()) {
            tree = it->second;
        } else if (!GetSproutAnchorAt(joinsplit.anchor, tree)) {
            return false;
        }

        BOOST_FOREACH(const uint256& commitment, joinsplit.commitments)
        {
            tree.append(commitment);
        }

        intermediates.insert(std::make_pair(tree.root(), tree));
    }

    auto now = GetTimeMicros();

    //Create a Vector of futures to be collected later
    std::vector<std::future<bool>> vFutures;

    //Setup batches
    std::vector<uint256> vSpendAnchor;
    std::vector<std::vector<uint256>> vvSpendAnchor;
    std::vector<uint256> vSpendNullifier;
    std::vector<std::vector<uint256>> vvSpendNullifier;

    //Create Thread Vectors
    for (int i = 0; i < maxProcessingThreads; i++) {
        vvSpendAnchor.emplace_back(vSpendAnchor);
        vvSpendNullifier.emplace_back(vSpendNullifier);
    }

    //Thread counter
    int t = 0;

    //Add this transaction sapling spend to spend thread batches
    for (const auto& spend : tx.GetSaplingSpends()) {
        auto nullifier = uint256::FromRawBytes(spend.nullifier());
        auto anchor = uint256::FromRawBytes(spend.anchor());

        //Push spend data to thread vector
        vvSpendNullifier[t].emplace_back(nullifier);
        vvSpendAnchor[t].emplace_back(anchor);

        //Increment thread vector
        t++;
        //reset if tread vector is greater qty of threads being used
        if (t >= vvSpendNullifier.size()) {
            t = 0;
        }
    }

    //Push batches of spends to async threads
    for (int i = 0; i < vvSpendNullifier.size(); i++) {
        if (!vvSpendNullifier[i].empty()) {
            vFutures.emplace_back(std::async(std::launch::async, HaveJoinSplitRequirementsWorkerNullifier, this, vvSpendNullifier[i], 1000 + i));
        }
    }

    for (int i = 0; i < vvSpendAnchor.size(); i++) {
        if (!vvSpendAnchor[i].empty()) {
            vFutures.emplace_back(std::async(std::launch::async, HaveJoinSplitRequirementsWorkerAnchor, this, vvSpendAnchor[i], 2000 + i));
        }
    }

    //Wait for all threads to complete
    for (auto &future : vFutures) {
        future.wait();
    }

    //Collect the async results
    bool ret = true;
    for (auto &future : vFutures) {
        if (!future.get()) {
            ret = false;
        }
    }

    //cleanup
    vFutures.resize(0);
    vvSpendNullifier.resize(0);
    vvSpendAnchor.resize(0);

    return ret;
}

static bool HaveJoinSplitRequirementsWorkerDuplicateSpendProofs(const CCoinsViewCache *coinCache,const std::vector<uint256> vSpendProof, int threadNum)
{
    //Perform Sapling Spend checks
    for (int i = 0; i < vSpendProof.size(); i++) {
        auto proofHash = vSpendProof[i];
        std::set<std::pair<uint256, int>> txids;
        if (coinCache->GetZkProofHash(proofHash, SPEND, txids))
            return txids.empty();
    }

    return true;
}

static bool HaveJoinSplitRequirementsWorkerDuplicateOutputProofs(const CCoinsViewCache *coinCache,const std::vector<uint256> vOutputProof, int threadNum)
{
    //Perform Sapling Spend checks
    for (int i = 0; i < vOutputProof.size(); i++) {
        auto proofHash = vOutputProof[i];
        std::set<std::pair<uint256, int>> txids;
        if (coinCache->GetZkProofHash(proofHash, OUTPUT, txids))
            return txids.empty();
    }

    return true;
}

//Cannot be combined with anchor and nullifier checking due do differing consensus rules
bool CCoinsViewCache::HaveJoinSplitRequirementsDuplicateProofs(const CTransaction& tx, int maxProcessingThreads) const
{
    auto now = GetTimeMicros();

    now = GetTimeMicros();

    //Create a Vector of futures to be collected later
    std::vector<std::future<bool>> vFutures;

    //Setup spend & output batches
    std::vector<uint256> vSpendProof;
    std::vector<std::vector<uint256>> vvSpendProof;
    std::vector<uint256> vOutputProof;
    std::vector<std::vector<uint256>> vvOutputProof;

    //Create Thread Vectors
    for (int i = 0; i < maxProcessingThreads; i++) {
        vvSpendProof.emplace_back(vSpendProof);
        vvOutputProof.emplace_back(vOutputProof);
    }

    //Thread counter
    int t = 0;

    //Add this transaction sapling spend to spend thread batches
    for (const auto& spend : tx.GetSaplingSpends()) {
        auto zkproof = spend.zkproof();
        auto proofHash = Hash(zkproof.begin(), zkproof.end());
        //Push spend to thread vector
        vvSpendProof[t].emplace_back(proofHash);

        //Increment thread vector
        t++;
        //reset if tread vector is greater qty of threads being used
        if (t >= vvSpendProof.size()) {
            t = 0;
        }
    }

    //Reset Thread Counter
    t = 0;

    //Add this transaction sapling output to outpu thread batches
    for (const auto& output : tx.GetSaplingOutputs()) {
        auto zkproof = output.zkproof();
        auto proofHash = Hash(zkproof.begin(), zkproof.end());
        //Push output data to thread vector
        vvOutputProof[t].emplace_back(proofHash);

        //Increment thread vector
        t++;
        //reset if tread vector is greater qty of threads being used
        if (t >= vvOutputProof.size()) {
            t = 0;
        }
    }

    //Push batches of spends to async threads
    for (int i = 0; i < vvSpendProof.size(); i++) {
        if (!vvSpendProof[i].empty()) {
            //Perform SpendDescription validations
            vFutures.emplace_back(std::async(std::launch::async, HaveJoinSplitRequirementsWorkerDuplicateSpendProofs, this, vvSpendProof[i], 1000 + i));
        }
    }

    //Push batches of output to async threads
    for (int i = 0; i < vvOutputProof.size(); i++) {
        if (!vvOutputProof[i].empty()) {
            //Perform OutputDescription validations
            vFutures.emplace_back(std::async(std::launch::async, HaveJoinSplitRequirementsWorkerDuplicateOutputProofs, this, vvOutputProof[i], 2000 + i));
        }
    }

    //Wait for all threads to complete
    for (auto &future : vFutures) {
        future.wait();
    }

    //Collect the async results
    bool ret = true;
    for (auto &future : vFutures) {
        if (!future.get()) {
            ret = false;
        }
    }

    //cleanup
    vFutures.resize(0);
    vvSpendProof.resize(0);
    vvOutputProof.resize(0);

    return ret;

}

bool CCoinsViewCache::HaveInputs(const CTransaction& tx) const
{
    if (!tx.IsMint()) {
        for (unsigned int i = 0; i < tx.vin.size(); i++) {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins* coins = AccessCoins(prevout.hash);
            if (!coins || !coins->IsAvailable(prevout.n)) {
                //fprintf(stderr,"HaveInputs missing input %s/v%d\n",prevout.hash.ToString().c_str(),prevout.n);
                return false;
            }
        }
    }
    return true;
}

double CCoinsViewCache::GetPriority(const CTransaction &tx, int nHeight) const
{
    if (tx.IsCoinBase())
        return 0.0;

    // Shielded transfers do not reveal any information about the value or age of a note, so we
    // cannot apply the priority algorithm used for transparent utxos.  Instead, we just
    // use the maximum priority for all (partially or fully) shielded transactions.
    // (Note that coinbase transactions cannot contain JoinSplits, or Sapling shielded Spends or Outputs.)

    if (tx.vjoinsplit.size() > 0 || tx.GetSaplingSpendsCount() > 0 || tx.GetSaplingOutputsCount() > 0 || tx.IsCoinImport()) {
        return MAX_PRIORITY;
    }

    // FIXME: this logic is partially duplicated between here and CreateNewBlock in miner.cpp.
    double dResult = 0.0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        const CCoins* coins = AccessCoins(txin.prevout.hash);
        assert(coins);
        if (!coins->IsAvailable(txin.prevout.n)) continue;
        if (coins->nHeight < nHeight) {
            dResult += coins->vout[txin.prevout.n].nValue * (nHeight-coins->nHeight);
        }
    }

    return tx.ComputePriority(dResult);
}

CCoinsModifier::CCoinsModifier(CCoinsViewCache& cache_, CCoinsMap::iterator it_, size_t usage) : cache(cache_), it(it_), cachedCoinUsage(usage) {
    assert(!cache.hasModifier);
    cache.hasModifier = true;
}

CCoinsModifier::~CCoinsModifier()
{
    assert(cache.hasModifier);
    cache.hasModifier = false;
    it->second.coins.Cleanup();
    cache.cachedCoinsUsage -= cachedCoinUsage; // Subtract the old usage
    if ((it->second.flags & CCoinsCacheEntry::FRESH) && it->second.coins.IsPruned()) {
        cache.cacheCoins.erase(it);
    } else {
        // If the coin still exists after the modification, add the new usage
        cache.cachedCoinsUsage += it->second.coins.DynamicMemoryUsage();
    }
}
