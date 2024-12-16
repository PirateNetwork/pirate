// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "consensus/validation.h"
#include "main.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "random.h"
#include "script/standard.h"
#include "test/test_bitcoin.h"
#include "transaction_builder.h"
#include "uint256.h"
#include "undo.h"
#include "util/strencodings.h"

#include <map>
#include <vector>

#include "zcash/IncrementalMerkleTree.hpp"
#include <gtest/gtest.h>

namespace TestCoins
{

class CCoinsViewTest : public CCoinsView
{
    uint256 hashBestBlock_;
    uint256 hashBestSproutAnchor_;
    uint256 hashBestSaplingAnchor_;
    uint256 hashBestSaplingFrontierAnchor_;
    uint256 hashBestOrchardFrontierAnchor_;
    std::map<uint256, CCoins> map_;
    std::map<uint256, SproutMerkleTree> mapSproutAnchors_;
    std::map<uint256, SaplingMerkleTree> mapSaplingAnchors_;
    std::map<uint256, SaplingMerkleFrontier> mapSaplingFrontierAnchors_;
    std::map<uint256, OrchardMerkleFrontier> mapOrchardFrontierAnchors_;
    std::map<uint256, bool> mapSproutNullifiers_;
    std::map<uint256, bool> mapSaplingNullifiers_;
    std::map<uint256, bool> mapOrchardNullifiers_;
    std::map<uint32_t, HistoryCache> historyCacheMap_;


public:
    CCoinsViewTest()
    {
        hashBestSproutAnchor_ = SproutMerkleTree::empty_root();
        hashBestSaplingAnchor_ = SaplingMerkleTree::empty_root();
        hashBestSaplingFrontierAnchor_ = SaplingMerkleFrontier::empty_root();
        hashBestOrchardFrontierAnchor_ = OrchardMerkleFrontier::empty_root();
    }

    bool GetSproutAnchorAt(const uint256& rt, SproutMerkleTree& tree) const
    {
        if (rt == SproutMerkleTree::empty_root()) {
            SproutMerkleTree new_tree;
            tree = new_tree;
            return true;
        }

        std::map<uint256, SproutMerkleTree>::const_iterator it = mapSproutAnchors_.find(rt);
        if (it == mapSproutAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }

    bool GetSaplingAnchorAt(const uint256& rt, SaplingMerkleTree& tree) const
    {
        if (rt == SaplingMerkleTree::empty_root()) {
            SaplingMerkleTree new_tree;
            tree = new_tree;
            return true;
        }

        std::map<uint256, SaplingMerkleTree>::const_iterator it = mapSaplingAnchors_.find(rt);
        if (it == mapSaplingAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }

    bool GetSaplingFrontierAnchorAt(const uint256& rt, SaplingMerkleFrontier& tree) const
    {
        if (rt == SaplingMerkleFrontier::empty_root()) {
            SaplingMerkleFrontier new_tree;
            tree = new_tree;
            return true;
        }

        std::map<uint256, SaplingMerkleFrontier>::const_iterator it = mapSaplingFrontierAnchors_.find(rt);
        if (it == mapSaplingFrontierAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }

    bool GetOrchardFrontierAnchorAt(const uint256& rt, OrchardMerkleFrontier& tree) const
    {
        if (rt == OrchardMerkleFrontier::empty_root()) {
            OrchardMerkleFrontier new_tree;
            tree = new_tree;
            return true;
        }

        std::map<uint256, OrchardMerkleFrontier>::const_iterator it = mapOrchardFrontierAnchors_.find(rt);
        if (it == mapOrchardFrontierAnchors_.end()) {
            return false;
        } else {
            tree = it->second;
            return true;
        }
    }


    bool GetNullifier(const uint256& nf, ShieldedType type) const
    {
        const std::map<uint256, bool>* mapToUse;
        switch (type) {
        case SPROUT:
            mapToUse = &mapSproutNullifiers_;
            break;
        case SAPLING:
            mapToUse = &mapSaplingNullifiers_;
            break;
        case ORCHARDFRONTIER:
            mapToUse = &mapOrchardNullifiers_;
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
        }
        std::map<uint256, bool>::const_iterator it = mapToUse->find(nf);
        if (it == mapToUse->end()) {
            return false;
        } else {
            // The map shouldn't contain any false entries.
            assert(it->second);
            return true;
        }
    }

    uint256 GetBestAnchor(ShieldedType type) const
    {
        switch (type) {
        case SPROUT:
            return hashBestSproutAnchor_;
            break;
        case SAPLING:
            return hashBestSaplingAnchor_;
            break;
        case SAPLINGFRONTIER:
            return hashBestSaplingFrontierAnchor_;
            break;
        case ORCHARDFRONTIER:
            return hashBestOrchardFrontierAnchor_;
            break;
        default:
            throw std::runtime_error("Unknown shielded type");
        }
    }

    bool GetCoins(const uint256& txid, CCoins& coins) const
    {
        std::map<uint256, CCoins>::const_iterator it = map_.find(txid);
        if (it == map_.end()) {
            return false;
        }
        coins = it->second;
        if (coins.IsPruned() && insecure_rand() % 2 == 0) {
            // Randomly return false in case of an empty entry.
            return false;
        }
        return true;
    }

    bool HaveCoins(const uint256& txid) const
    {
        CCoins coins;
        return GetCoins(txid, coins);
    }

    uint256 GetBestBlock() const { return hashBestBlock_; }

    void BatchWriteNullifiers(CNullifiersMap& mapNullifiers, std::map<uint256, bool>& cacheNullifiers)
    {
        for (CNullifiersMap::iterator it = mapNullifiers.begin(); it != mapNullifiers.end();) {
            if (it->second.entered) {
                cacheNullifiers[it->first] = true;
            } else {
                cacheNullifiers.erase(it->first);
            }
            mapNullifiers.erase(it++);
        }
        mapNullifiers.clear();
    }

    template <typename Tree, typename Map>
    void BatchWriteAnchors(Map& mapAnchors, std::map<uint256, Tree>& cacheAnchors)
    {
        for (auto it = mapAnchors.begin(); it != mapAnchors.end();) {
            if (it->second.entered) {
                auto ret = cacheAnchors.insert(std::make_pair(it->first, Tree())).first;
                ret->second = it->second.tree;
            } else {
                cacheAnchors.erase(it->first);
            }
            mapAnchors.erase(it++);
        }
    }

    bool BatchWrite(CCoinsMap& mapCoins,
                    const uint256& hashBlock,
                    const uint256& hashSproutAnchor,
                    const uint256& hashSaplingAnchor,
                    const uint256& hashSaplingFrontierAnchor,
                    const uint256& hashOrchardFrontierAnchor,
                    CAnchorsSproutMap& mapSproutAnchors,
                    CAnchorsSaplingMap& mapSaplingAnchors,
                    CAnchorsSaplingFrontierMap& mapSaplingFrontierAnchors,
                    CAnchorsOrchardFrontierMap& mapOrchardFrontierAnchors,
                    CNullifiersMap& mapSproutNullifiers,
                    CNullifiersMap& mapSaplingNullifiers,
                    CNullifiersMap& mapOrchardNullifiers,
                    CHistoryCacheMap& historyCacheMap)
    {
        for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
            map_[it->first] = it->second.coins;
            if (it->second.coins.IsPruned() && insecure_rand() % 3 == 0) {
                // Randomly delete empty entries on write.
                map_.erase(it->first);
            }
            mapCoins.erase(it++);
        }

        BatchWriteAnchors<SproutMerkleTree, CAnchorsSproutMap>(mapSproutAnchors, mapSproutAnchors_);
        BatchWriteAnchors<SaplingMerkleTree, CAnchorsSaplingMap>(mapSaplingAnchors, mapSaplingAnchors_);
        BatchWriteAnchors<SaplingMerkleFrontier, CAnchorsSaplingFrontierMap>(mapSaplingFrontierAnchors, mapSaplingFrontierAnchors_);
        BatchWriteAnchors<OrchardMerkleFrontier, CAnchorsOrchardFrontierMap>(mapOrchardFrontierAnchors, mapOrchardFrontierAnchors_);

        BatchWriteNullifiers(mapSproutNullifiers, mapSproutNullifiers_);
        BatchWriteNullifiers(mapSaplingNullifiers, mapSaplingNullifiers_);
        BatchWriteNullifiers(mapOrchardNullifiers, mapOrchardNullifiers_);

        mapCoins.clear();
        mapSproutAnchors.clear();
        mapSaplingAnchors.clear();
        hashBestBlock_ = hashBlock;
        hashBestSproutAnchor_ = hashSproutAnchor;
        hashBestSaplingAnchor_ = hashSaplingAnchor;
        hashBestSaplingFrontierAnchor_ = hashSaplingFrontierAnchor;
        hashBestOrchardFrontierAnchor_ = hashOrchardFrontierAnchor;
        return true;
    }

    bool GetStats(CCoinsStats& stats) const { return false; }
};

class CCoinsViewCacheTest : public CCoinsViewCache
{
public:
    CCoinsViewCacheTest(CCoinsView* base) : CCoinsViewCache(base) {}

    void SelfTest() const
    {
        // Manually recompute the dynamic usage of the whole data, and compare it.
        size_t ret = memusage::DynamicUsage(cacheCoins) +
                     memusage::DynamicUsage(cacheSproutAnchors) +
                     memusage::DynamicUsage(cacheSaplingAnchors) +
                     memusage::DynamicUsage(cacheSaplingFrontierAnchors) +
                     memusage::DynamicUsage(cacheOrchardFrontierAnchors) +
                     memusage::DynamicUsage(cacheSproutNullifiers) +
                     memusage::DynamicUsage(cacheSaplingNullifiers) +
                     memusage::DynamicUsage(cacheOrchardNullifiers) +
                     memusage::DynamicUsage(historyCacheMap);
        for (CCoinsMap::iterator it = cacheCoins.begin(); it != cacheCoins.end(); it++) {
            ret += it->second.coins.DynamicMemoryUsage();
        }
        EXPECT_EQ(DynamicMemoryUsage(), ret);
    }
};

class TxWithNullifiers
{
public:
    CTransaction tx;
    uint256 sproutNullifier;
    uint256 saplingNullifier;
    uint256 orchardNullifier;

    TxWithNullifiers()
    {
        CMutableTransaction mutableTx;
        
        // Set the transaction version to Orchard
        mutableTx.fOverwintered = true;
        mutableTx.nVersionGroupId = ORCHARD_VERSION_GROUP_ID;
        mutableTx.nVersion = ORCHARD_TX_VERSION;
        mutableTx.nConsensusBranchId = 0x00000000;

        sproutNullifier = GetRandHash();
        JSDescription jsd;
        jsd.nullifiers[0] = sproutNullifier;
        mutableTx.vjoinsplit.emplace_back(jsd);

        mutableTx.saplingBundle = sapling::test_only_invalid_bundle(1, 1, 0);
        saplingNullifier = uint256::FromRawBytes(mutableTx.saplingBundle.GetDetails().spends()[0].nullifier());

        // The Orchard bundle builder always pads to two Actions, so we can just
        // use an empty builder to create a dummy Orchard bundle.
        uint256 orchardAnchor;
        uint256 dataToBeSigned;
        auto builder = orchard::Builder(true, true, orchardAnchor);
        mutableTx.orchardBundle = builder.Build().value().ProveAndSign({}, dataToBeSigned).value();
        orchardNullifier = mutableTx.orchardBundle.GetNullifiers()[0];

        tx = CTransaction(mutableTx);
    }
};

uint256 appendRandomSproutCommitment(SproutMerkleTree& tree)
{
    libzcash::SproutSpendingKey k = libzcash::SproutSpendingKey::random();
    libzcash::SproutPaymentAddress addr = k.address();

    libzcash::SproutNote note(addr.a_pk, 0, uint256(), uint256());

    auto cm = note.cm();
    tree.append(cm);
    return cm;
}

template<typename Tree> void AppendRandomLeaf(Tree &tree);
template<> void AppendRandomLeaf(SproutMerkleTree &tree) { tree.append(GetRandHash()); }
template<> void AppendRandomLeaf(SaplingMerkleTree &tree) { tree.append(GetRandHash()); }
template<> void AppendRandomLeaf(SaplingMerkleFrontier &tree) { 

    CMutableTransaction mutableTx;

    mutableTx.saplingBundle = sapling::test_only_invalid_bundle(1, 1, 0);
    uint256 saplingCMU = uint256::FromRawBytes(mutableTx.saplingBundle.GetDetails().outputs()[0].cmu());
    tree.append(saplingCMU); 
}

template<> void AppendRandomLeaf(OrchardMerkleFrontier &tree) {
    // OrchardMerkleFrontier only has APIs to append entire bundles, but
    // fortunately the tests only require that the tree root change.
    // TODO: Remove the need to create proofs by having a testing-only way to
    // append a random leaf to OrchardMerkleFrontier.
    uint256 orchardAnchor;
    uint256 dataToBeSigned;
    auto builder = orchard::Builder(true, true, orchardAnchor);
    auto bundle = builder.Build().value().ProveAndSign({}, dataToBeSigned).value();
    tree.AppendBundle(bundle);
}

template <typename Tree>
bool GetAnchorAt(const CCoinsViewCacheTest& cache, const uint256& rt, Tree& tree);
template <>
bool GetAnchorAt(const CCoinsViewCacheTest& cache, const uint256& rt, SproutMerkleTree& tree)
{
    return cache.GetSproutAnchorAt(rt, tree);
}
template <>
bool GetAnchorAt(const CCoinsViewCacheTest& cache, const uint256& rt, SaplingMerkleTree& tree)
{
    return cache.GetSaplingAnchorAt(rt, tree);
}
template <>
bool GetAnchorAt(const CCoinsViewCacheTest& cache, const uint256& rt, SaplingMerkleFrontier& tree)
{
    return cache.GetSaplingFrontierAnchorAt(rt, tree);
}
template <>
bool GetAnchorAt(const CCoinsViewCacheTest& cache, const uint256& rt, OrchardMerkleFrontier& tree)
{
    return cache.GetOrchardFrontierAnchorAt(rt, tree);
}

void checkNullifierCache(const CCoinsViewCacheTest& cache, const TxWithNullifiers& txWithNullifiers, bool shouldBeInCache)
{
    // Make sure the nullifiers have not gotten mixed up
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.sproutNullifier, SAPLING));
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.sproutNullifier, ORCHARDFRONTIER));
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.saplingNullifier, SPROUT));
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.saplingNullifier, ORCHARDFRONTIER));
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.orchardNullifier, SPROUT));
    EXPECT_TRUE(!cache.GetNullifier(txWithNullifiers.orchardNullifier, SAPLING));
    // Check if the nullifiers either are or are not in the cache
    bool containsSproutNullifier = cache.GetNullifier(txWithNullifiers.sproutNullifier, SPROUT);
    bool containsSaplingNullifier = cache.GetNullifier(txWithNullifiers.saplingNullifier, SAPLING);
    bool containsOrchardNullifier = cache.GetNullifier(txWithNullifiers.orchardNullifier, ORCHARDFRONTIER);
    EXPECT_TRUE(containsSproutNullifier == shouldBeInCache);
    EXPECT_TRUE(containsSaplingNullifier == shouldBeInCache);
    EXPECT_TRUE(containsOrchardNullifier == shouldBeInCache);
}

TEST(TestCoins, nullifier_regression_test)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        TxWithNullifiers txWithNullifiers;

        // Insert a nullifier into the base.
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Flush to base.

        // Remove the nullifier from cache
        cache1.SetNullifiers(txWithNullifiers.tx, false);

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Also correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        TxWithNullifiers txWithNullifiers;

        // Insert a nullifier into the base.
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Flush to base.

        // Remove the nullifier from cache
        cache1.SetNullifiers(txWithNullifiers.tx, false);
        cache1.Flush(); // Flush to base.

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Works because we bring it from the parent cache:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert a nullifier into the base.
        TxWithNullifiers txWithNullifiers;
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        checkNullifierCache(cache1, txWithNullifiers, true);
        cache1.Flush(); // Empties cache.

        // Create cache on top.
        {
            // Remove the nullifier.
            CCoinsViewCacheTest cache2(&cache1);
            checkNullifierCache(cache2, txWithNullifiers, true);
            cache1.SetNullifiers(txWithNullifiers.tx, false);
            cache2.Flush(); // Empties cache, flushes to cache1.
        }

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }

    // Was broken:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert a nullifier into the base.
        TxWithNullifiers txWithNullifiers;
        cache1.SetNullifiers(txWithNullifiers.tx, true);
        cache1.Flush(); // Empties cache.

        // Create cache on top.
        {
            // Remove the nullifier.
            CCoinsViewCacheTest cache2(&cache1);
            cache2.SetNullifiers(txWithNullifiers.tx, false);
            cache2.Flush(); // Empties cache, flushes to cache1.
        }

        // The nullifier now should be `false`.
        checkNullifierCache(cache1, txWithNullifiers, false);
    }
}

template <typename Tree>
void anchorPopRegressionTestImpl(ShieldedType type)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Create dummy anchor/commitment
        Tree tree;
        AppendRandomLeaf(tree);
        


        // Add the anchor
        cache1.PushAnchor(tree);
        cache1.Flush();

        // Remove the anchor
        cache1.PopAnchor(Tree::empty_root(), type);
        cache1.Flush();

        // Add the anchor back
        cache1.PushAnchor(tree);
        cache1.Flush();

        // The base contains the anchor, of course!
        {
            Tree checkTree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checkTree));
            EXPECT_TRUE(checkTree.root() == tree.root());
        }
    }

    // Previously incorrect behavior
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Create dummy anchor/commitment
        Tree tree;
        AppendRandomLeaf(tree);

        // Add the anchor and flush to disk
        cache1.PushAnchor(tree);
        cache1.Flush();

        // Remove the anchor, but don't flush yet!
        cache1.PopAnchor(Tree::empty_root(), type);

        {
            CCoinsViewCacheTest cache2(&cache1); // Build cache on top
            cache2.PushAnchor(tree);             // Put the same anchor back!
            cache2.Flush();                      // Flush to cache1
        }

        // cache2's flush kinda worked, i.e. cache1 thinks the
        // tree is there, but it didn't bring down the correct
        // treestate...
        {
            Tree checktree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checktree));
            EXPECT_TRUE(checktree.root() == tree.root()); // Oh, shucks.
        }

        // Flushing cache won't help either, just makes the inconsistency
        // permanent.
        cache1.Flush();
        {
            Tree checktree;
            EXPECT_TRUE(GetAnchorAt(cache1, tree.root(), checktree));
            EXPECT_TRUE(checktree.root() == tree.root()); // Oh, shucks.
        }
    }
}

TEST(TestCoins, anchor_pop_regression_test)
{
    anchorPopRegressionTestImpl<SproutMerkleTree>(SPROUT);
    anchorPopRegressionTestImpl<SaplingMerkleTree>(SAPLING);
    anchorPopRegressionTestImpl<SaplingMerkleFrontier>(SAPLINGFRONTIER);
    anchorPopRegressionTestImpl<OrchardMerkleFrontier>(ORCHARDFRONTIER);
}

template <typename Tree>
void anchorRegressionTestImpl(ShieldedType type)
{
    // Correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        AppendRandomLeaf(tree);

        cache1.PushAnchor(tree);
        cache1.Flush();

        cache1.PopAnchor(Tree::empty_root(), type);
        EXPECT_TRUE(cache1.GetBestAnchor(type) == Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Also correct behavior:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        AppendRandomLeaf(tree);
        cache1.PushAnchor(tree);
        cache1.Flush();

        cache1.PopAnchor(Tree::empty_root(), type);
        cache1.Flush();
        EXPECT_TRUE(cache1.GetBestAnchor(type) == Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Works because we bring the anchor in from parent cache.
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        AppendRandomLeaf(tree);
        cache1.PushAnchor(tree);
        cache1.Flush();

        {
            // Pop anchor.
            CCoinsViewCacheTest cache2(&cache1);
            EXPECT_TRUE(GetAnchorAt(cache2, tree.root(), tree));
            cache2.PopAnchor(Tree::empty_root(), type);
            cache2.Flush();
        }

        EXPECT_TRUE(cache1.GetBestAnchor(type) == Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }

    // Was broken:
    {
        CCoinsViewTest base;
        CCoinsViewCacheTest cache1(&base);

        // Insert anchor into base.
        Tree tree;
        AppendRandomLeaf(tree);
        cache1.PushAnchor(tree);
        cache1.Flush();

        {
            // Pop anchor.
            CCoinsViewCacheTest cache2(&cache1);
            cache2.PopAnchor(Tree::empty_root(), type);
            cache2.Flush();
        }

        EXPECT_TRUE(cache1.GetBestAnchor(type) == Tree::empty_root());
        EXPECT_TRUE(!GetAnchorAt(cache1, tree.root(), tree));
    }
}

TEST(TestCoins, anchor_regression_test)
{
    anchorRegressionTestImpl<SproutMerkleTree>(SPROUT);
    anchorRegressionTestImpl<SaplingMerkleTree>(SAPLING);
    anchorRegressionTestImpl<SaplingMerkleFrontier>(SAPLINGFRONTIER);
    anchorRegressionTestImpl<OrchardMerkleFrontier>(ORCHARDFRONTIER);
}

TEST(TestCoins, nullifiers_test)
{
    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    TxWithNullifiers txWithNullifiers;
    checkNullifierCache(cache, txWithNullifiers, false);
    cache.SetNullifiers(txWithNullifiers.tx, true);
    checkNullifierCache(cache, txWithNullifiers, true);
    cache.Flush();

    CCoinsViewCacheTest cache2(&base);

    checkNullifierCache(cache2, txWithNullifiers, true);
    cache2.SetNullifiers(txWithNullifiers.tx, false);
    checkNullifierCache(cache2, txWithNullifiers, false);
    cache2.Flush();

    CCoinsViewCacheTest cache3(&base);

    checkNullifierCache(cache3, txWithNullifiers, false);
}

template <typename Tree>
void anchorsFlushImpl(ShieldedType type)
{
    CCoinsViewTest base;
    uint256 newrt;
    {
        CCoinsViewCacheTest cache(&base);
        Tree tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));
        AppendRandomLeaf(tree);

        newrt = tree.root();

        cache.PushAnchor(tree);
        cache.Flush();
    }

    {
        CCoinsViewCacheTest cache(&base);
        Tree tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));

        // Get the cached entry.
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));

        uint256 check_rt = tree.root();

        EXPECT_TRUE(check_rt == newrt);
    }
}

TEST(TestCoins, anchors_flush_test)
{
    anchorsFlushImpl<SproutMerkleTree>(SPROUT);
    anchorsFlushImpl<SaplingMerkleTree>(SAPLING);
    anchorsFlushImpl<SaplingMerkleFrontier>(SAPLINGFRONTIER);
    anchorsFlushImpl<OrchardMerkleFrontier>(ORCHARDFRONTIER);
}

TEST(TestCoins, chained_joinsplits)
{
    // TODO update this or add a similar test when the SaplingNote class exist
    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    SproutMerkleTree tree;

    JSDescription js1;
    js1.anchor = tree.root();
    js1.commitments[0] = appendRandomSproutCommitment(tree);
    js1.commitments[1] = appendRandomSproutCommitment(tree);

    // Although it's not possible given our assumptions, if
    // two joinsplits create the same treestate twice, we should
    // still be able to anchor to it.
    JSDescription js1b;
    js1b.anchor = tree.root();
    js1b.commitments[0] = js1.commitments[0];
    js1b.commitments[1] = js1.commitments[1];

    JSDescription js2;
    JSDescription js3;

    js2.anchor = tree.root();
    js3.anchor = tree.root();

    js2.commitments[0] = appendRandomSproutCommitment(tree);
    js2.commitments[1] = appendRandomSproutCommitment(tree);

    js3.commitments[0] = appendRandomSproutCommitment(tree);
    js3.commitments[1] = appendRandomSproutCommitment(tree);

    {
        CMutableTransaction mtx;
        mtx.vjoinsplit.push_back(js2);

        EXPECT_TRUE(!cache.HaveJoinSplitRequirements(mtx, 2));
    }

    {
        // js2 is trying to anchor to js1 but js1
        // appears afterwards -- not a permitted ordering
        CMutableTransaction mtx;
        mtx.vjoinsplit.push_back(js2);
        mtx.vjoinsplit.push_back(js1);

        EXPECT_TRUE(!cache.HaveJoinSplitRequirements(mtx, 2));
    }

    {
        CMutableTransaction mtx;
        mtx.vjoinsplit.push_back(js1);
        mtx.vjoinsplit.push_back(js2);

        EXPECT_TRUE(cache.HaveJoinSplitRequirements(mtx, 2));
    }

    {
        CMutableTransaction mtx;
        mtx.vjoinsplit.push_back(js1);
        mtx.vjoinsplit.push_back(js2);
        mtx.vjoinsplit.push_back(js3);

        EXPECT_TRUE(cache.HaveJoinSplitRequirements(mtx, 2));
    }

    {
        CMutableTransaction mtx;
        mtx.vjoinsplit.push_back(js1);
        mtx.vjoinsplit.push_back(js1b);
        mtx.vjoinsplit.push_back(js2);
        mtx.vjoinsplit.push_back(js3);

        EXPECT_TRUE(cache.HaveJoinSplitRequirements(mtx, 2));
    }
}

template <typename Tree>
void anchorsTestImpl(ShieldedType type)
{
    // TODO: These tests should be more methodical.
    //       Or, integrate with Bitcoin's tests later.

    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    EXPECT_TRUE(cache.GetBestAnchor(type) == Tree::empty_root());

    {
        Tree tree;

        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), tree));
        EXPECT_TRUE(cache.GetBestAnchor(type) == tree.root());
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);

        Tree save_tree_for_later;
        save_tree_for_later = tree;

        uint256 newrt = tree.root();
        uint256 newrt2;

        cache.PushAnchor(tree);
        EXPECT_TRUE(cache.GetBestAnchor(type) == newrt);

        {
            Tree confirm_same;
            EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), confirm_same));

            EXPECT_TRUE(confirm_same.root() == newrt);
        }

        AppendRandomLeaf(tree);
        AppendRandomLeaf(tree);

        newrt2 = tree.root();

        cache.PushAnchor(tree);
        EXPECT_TRUE(cache.GetBestAnchor(type) == newrt2);

        Tree test_tree;
        EXPECT_TRUE(GetAnchorAt(cache, cache.GetBestAnchor(type), test_tree));

        EXPECT_TRUE(tree.root() == test_tree.root());

        {
            Tree test_tree2;
            GetAnchorAt(cache, newrt, test_tree2);

            EXPECT_TRUE(test_tree2.root() == newrt);
        }

        {
            cache.PopAnchor(newrt, type);
            Tree obtain_tree;
            assert(!GetAnchorAt(cache, newrt2, obtain_tree)); // should have been popped off
            assert(GetAnchorAt(cache, newrt, obtain_tree));

            assert(obtain_tree.root() == newrt);
        }
    }
}

TEST(TestCoins, anchors_test)
{
    anchorsTestImpl<SproutMerkleTree>(SPROUT);
    anchorsTestImpl<SaplingMerkleTree>(SAPLING);
    anchorsTestImpl<SaplingMerkleFrontier>(SAPLINGFRONTIER);
    anchorsTestImpl<OrchardMerkleFrontier>(ORCHARDFRONTIER);
}

static const unsigned int NUM_SIMULATION_ITERATIONS = 40000;

// This is a large randomized insert/remove simulation test on a variable-size
// stack of caches on top of CCoinsViewTest.
//
// It will randomly create/update/delete CCoins entries to a tip of caches, with
// txids picked from a limited list of random 256-bit hashes. Occasionally, a
// new tip is added to the stack of caches, or the tip is flushed and removed.
//
// During the process, booleans are kept to make sure that the randomized
// operation hits all branches.
TEST(TestCoins, coins_cache_simulation_test)
{
    // Various coverage trackers.
    bool removed_all_caches = false;
    bool reached_4_caches = false;
    bool added_an_entry = false;
    bool removed_an_entry = false;
    bool updated_an_entry = false;
    bool found_an_entry = false;
    bool missed_an_entry = false;

    // A simple map to track what we expect the cache stack to represent.
    std::map<uint256, CCoins> result;

    // The cache stack.
    CCoinsViewTest base;                             // A CCoinsViewTest at the bottom.
    std::vector<CCoinsViewCacheTest*> stack;         // A stack of CCoinsViewCaches on top.
    stack.push_back(new CCoinsViewCacheTest(&base)); // Start with one cache.

    // Use a limited set of random transaction ids, so we do test overwriting entries.
    std::vector<uint256> txids;
    txids.resize(NUM_SIMULATION_ITERATIONS / 8);
    for (unsigned int i = 0; i < txids.size(); i++) {
        txids[i] = GetRandHash();
    }

    for (unsigned int i = 0; i < NUM_SIMULATION_ITERATIONS; i++) {
        // Do a random modification.
        {
            uint256 txid = txids[insecure_rand() % txids.size()]; // txid we're going to modify in this iteration.
            CCoins& coins = result[txid];
            CCoinsModifier entry = stack.back()->ModifyCoins(txid);
            EXPECT_TRUE(coins == *entry);
            if (insecure_rand() % 5 == 0 || coins.IsPruned()) {
                if (coins.IsPruned()) {
                    added_an_entry = true;
                } else {
                    updated_an_entry = true;
                }
                coins.nVersion = insecure_rand();
                coins.vout.resize(1);
                coins.vout[0].nValue = insecure_rand();
                *entry = coins;
            } else {
                coins.Clear();
                entry->Clear();
                removed_an_entry = true;
            }
        }

        // Once every 1000 iterations and at the end, verify the full cache.
        if (insecure_rand() % 1000 == 1 || i == NUM_SIMULATION_ITERATIONS - 1) {
            for (std::map<uint256, CCoins>::iterator it = result.begin(); it != result.end(); it++) {
                const CCoins* coins = stack.back()->AccessCoins(it->first);
                if (coins) {
                    EXPECT_TRUE(*coins == it->second);
                    found_an_entry = true;
                } else {
                    EXPECT_TRUE(it->second.IsPruned());
                    missed_an_entry = true;
                }
            }
            BOOST_FOREACH (const CCoinsViewCacheTest* test, stack) {
                test->SelfTest();
            }
        }

        if (insecure_rand() % 100 == 0) {
            // Every 100 iterations, change the cache stack.
            if (stack.size() > 0 && insecure_rand() % 2 == 0) {
                stack.back()->Flush();
                delete stack.back();
                stack.pop_back();
            }
            if (stack.size() == 0 || (stack.size() < 4 && insecure_rand() % 2)) {
                CCoinsView* tip = &base;
                if (stack.size() > 0) {
                    tip = stack.back();
                } else {
                    removed_all_caches = true;
                }
                stack.push_back(new CCoinsViewCacheTest(tip));
                if (stack.size() == 4) {
                    reached_4_caches = true;
                }
            }
        }
    }

    // Clean up the stack.
    while (stack.size() > 0) {
        delete stack.back();
        stack.pop_back();
    }

    // Verify coverage.
    EXPECT_TRUE(removed_all_caches);
    EXPECT_TRUE(reached_4_caches);
    EXPECT_TRUE(added_an_entry);
    EXPECT_TRUE(removed_an_entry);
    EXPECT_TRUE(updated_an_entry);
    EXPECT_TRUE(found_an_entry);
    EXPECT_TRUE(missed_an_entry);
}

TEST(TestCoins, coins_coinbase_spends)
{
    SelectParams(CBaseChainParams::MAIN); // set params explicitly otherwise it would use params set by some past test what could cause bad-txns-coinbase-spend error
    CCoinsViewTest base;
    CCoinsViewCacheTest cache(&base);

    // Create coinbase transaction
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].scriptSig = CScript() << OP_1;
    mtx.vin[0].nSequence = 0;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = 500;
    mtx.vout[0].scriptPubKey = CScript() << OP_1;

    CTransaction tx(mtx);

    EXPECT_TRUE(tx.IsCoinBase());

    CValidationState state;
    UpdateCoins(tx, cache, 100);

    // Create coinbase spend
    CMutableTransaction mtx2;
    mtx2.vin.resize(1);
    mtx2.vin[0].prevout = COutPoint(tx.GetHash(), 0);
    mtx2.vin[0].scriptSig = CScript() << OP_1;
    mtx2.vin[0].nSequence = 0;

    {
        CTransaction tx2(mtx2);
        EXPECT_TRUE(Consensus::CheckTxInputs(tx2, state, cache, 100 + Params().CoinbaseMaturity(), Params().GetConsensus()));
    }

    mtx2.vout.resize(1);
    mtx2.vout[0].nValue = 500;
    mtx2.vout[0].scriptPubKey = CScript() << OP_1;

    {
        CTransaction tx2(mtx2);
        EXPECT_TRUE(Consensus::CheckTxInputs(tx2, state, cache, 100 + Params().CoinbaseMaturity(), Params().GetConsensus()));
        // EXPECT_TRUE(state.GetRejectReason() == "bad-txns-coinbase-spend-has-transparent-outputs");
    }
}

TEST(TestCoins, ccoins_serialization)
{
    // Good example
    CDataStream ss1(ParseHex("0104835800816115944e077fe7c803cfa57f29b36bf87c1d358bb85e"), SER_DISK, CLIENT_VERSION);
    CCoins cc1;
    ss1 >> cc1;
    EXPECT_EQ(cc1.nVersion, 1);
    EXPECT_EQ(cc1.fCoinBase, false);
    EXPECT_EQ(cc1.nHeight, 203998);
    EXPECT_EQ(cc1.vout.size(), 2);
    EXPECT_EQ(cc1.IsAvailable(0), false);
    EXPECT_EQ(cc1.IsAvailable(1), true);
    EXPECT_EQ(cc1.vout[1].nValue, 60000000000ULL);
    EXPECT_EQ(HexStr(cc1.vout[1].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("816115944e077fe7c803cfa57f29b36bf87c1d35"))))));

    // Good example
    CDataStream ss2(ParseHex("0109044086ef97d5790061b01caab50f1b8e9c50a5057eb43c2d9563a4eebbd123008c988f1a4a4de2161e0f50aac7f17e7f9555caa486af3b"), SER_DISK, CLIENT_VERSION);
    CCoins cc2;
    ss2 >> cc2;
    EXPECT_EQ(cc2.nVersion, 1);
    EXPECT_EQ(cc2.fCoinBase, true);
    EXPECT_EQ(cc2.nHeight, 120891);
    EXPECT_EQ(cc2.vout.size(), 17);
    for (int i = 0; i < 17; i++) {
        EXPECT_EQ(cc2.IsAvailable(i), i == 4 || i == 16);
    }
    EXPECT_EQ(cc2.vout[4].nValue, 234925952);
    EXPECT_EQ(HexStr(cc2.vout[4].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("61b01caab50f1b8e9c50a5057eb43c2d9563a4ee"))))));
    EXPECT_EQ(cc2.vout[16].nValue, 110397);
    EXPECT_EQ(HexStr(cc2.vout[16].scriptPubKey), HexStr(GetScriptForDestination(CKeyID(uint160(ParseHex("8c988f1a4a4de2161e0f50aac7f17e7f9555caa4"))))));

    // Smallest possible example
    CDataStream ssx(SER_DISK, CLIENT_VERSION);
    EXPECT_EQ(HexStr(ssx.begin(), ssx.end()), "");

    CDataStream ss3(ParseHex("0002000600"), SER_DISK, CLIENT_VERSION);
    CCoins cc3;
    ss3 >> cc3;
    EXPECT_EQ(cc3.nVersion, 0);
    EXPECT_EQ(cc3.fCoinBase, false);
    EXPECT_EQ(cc3.nHeight, 0);
    EXPECT_EQ(cc3.vout.size(), 1);
    EXPECT_EQ(cc3.IsAvailable(0), true);
    EXPECT_EQ(cc3.vout[0].nValue, 0);
    EXPECT_EQ(cc3.vout[0].scriptPubKey.size(), 0);

    // scriptPubKey that ends beyond the end of the stream
    CDataStream ss4(ParseHex("0002000800"), SER_DISK, CLIENT_VERSION);
    try {
        CCoins cc4;
        ss4 >> cc4;
        FAIL() << "We should have thrown";
    } catch (const std::ios_base::failure& e) {
    }

    // Very large scriptPubKey (3*10^9 bytes) past the end of the stream
    CDataStream tmp(SER_DISK, CLIENT_VERSION);
    uint64_t x = 3000000000ULL;
    tmp << VARINT(x);
    EXPECT_EQ(HexStr(tmp.begin(), tmp.end()), "8a95c0bb00");
    CDataStream ss5(ParseHex("0002008a95c0bb0000"), SER_DISK, CLIENT_VERSION);
    try {
        CCoins cc5;
        ss5 >> cc5;
        FAIL() << "We should have thrown";
    } catch (const std::ios_base::failure& e) {
    }
}

} // namespace TestCoins
