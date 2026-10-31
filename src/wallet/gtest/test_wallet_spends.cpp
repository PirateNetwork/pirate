// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "main.h"
#include "primitives/block.h"
#include "random.h"
#include "wallet/wallet.h"

#include <memory>
#include <set>

enum class WalletSpendPool { Transparent, Sprout, Sapling, Ironwood };

// Exercise the real wallet spend indexes without constructing shielded proofs:
// the bug is in retaining spender relationships, not transaction validation.
class WalletSpendTrackingTest : public BitcoinBasicTestingSetup,
                                public ::testing::WithParamInterface<WalletSpendPool>
{
protected:
    void SelectTestParams() override { SelectParams(CBaseChainParams::REGTEST); }

    void SetUp() override
    {
        BitcoinBasicTestingSetup::SetUp();
        LOCK(cs_main);
        wallet.reset(new CWallet());

        CMutableTransaction tx;
        tx.vin.emplace_back(COutPoint(GetRandHash(), 0));
        tx.vin.emplace_back(COutPoint(GetRandHash(), 0));
        tx.vout.emplace_back(1, CScript());
        tx.nLockTime = 1;
        const CTransaction confirmed(tx);
        confirmedTxid = confirmed.GetHash();

        CBlock block;
        block.vtx.push_back(confirmed);
        block.hashMerkleRoot = block.BuildMerkleTree();
        previousTip = chainActive.Tip();
        if (previousTip) {
            block.hashPrevBlock = previousTip->GetBlockHash();
        }
        blockHash = block.GetHash();
        confirmedBlock.reset(new CBlockIndex(block));
        confirmedBlock->pprev = previousTip;
        confirmedBlock->nHeight = previousTip ? previousTip->nHeight + 1 : 0;
        auto inserted = mapBlockIndex.emplace(blockHash, confirmedBlock.get());
        ASSERT_TRUE(inserted.second);
        confirmedBlock->phashBlock = &inserted.first->first;
        chainActive.SetTip(confirmedBlock.get());
        blockInstalled = true;

        CWalletTx confirmedWalletTx(wallet.get(), confirmed);
        confirmedWalletTx.SetMerkleBranch(block);
        confirmedWalletTx.nOrderPos = 1;
        wallet->mapWallet.emplace(confirmedTxid, confirmedWalletTx);

        tx.nLockTime = 2;
        CWalletTx offChainWalletTx(wallet.get(), CTransaction(tx));
        offChainWalletTx.nOrderPos = 2;
        offChainTxid = offChainWalletTx.GetHash();
        wallet->mapWallet.emplace(offChainTxid, offChainWalletTx);

        ASSERT_EQ(1, wallet->mapWallet.at(confirmedTxid).GetDepthInMainChain());
        ASSERT_EQ(-1, wallet->mapWallet.at(offChainTxid).GetDepthInMainChain());
    }

    void TearDown() override
    {
        {
            LOCK(cs_main);
            if (blockInstalled) {
                chainActive.SetTip(previousTip);
                mapBlockIndex.erase(blockHash);
            }
            wallet.reset();
            confirmedBlock.reset();
        }
        BitcoinBasicTestingSetup::TearDown();
    }

    void AddSpender(const uint256& key, const uint256& txid)
    {
        switch (GetParam()) {
        case WalletSpendPool::Transparent:
            wallet->AddToTransparentSpends(COutPoint(key, 0), txid);
            break;
        case WalletSpendPool::Sprout: wallet->AddToSproutSpends(key, txid); break;
        case WalletSpendPool::Sapling: wallet->AddToSaplingSpends(key, txid); break;
        case WalletSpendPool::Ironwood: wallet->AddToIronwoodSpends(key, txid); break;
        }
    }

    std::multiset<uint256> Spenders(const uint256& key) const
    {
        std::multiset<uint256> result;
        if (GetParam() == WalletSpendPool::Transparent) {
            const auto range = wallet->mapTxSpends.equal_range(COutPoint(key, 0));
            for (auto it = range.first; it != range.second; ++it) {
                result.insert(it->second);
            }
        } else {
            const auto& index = GetParam() == WalletSpendPool::Sprout ? wallet->mapTxSproutNullifiers :
                                GetParam() == WalletSpendPool::Sapling ? wallet->mapTxSaplingNullifiers :
                                                                       wallet->mapTxIronwoodNullifiers;
            const auto range = index.equal_range(key);
            for (auto it = range.first; it != range.second; ++it) {
                result.insert(it->second);
            }
        }
        return result;
    }

    bool IsSpent(const uint256& key) const
    {
        switch (GetParam()) {
        case WalletSpendPool::Transparent: return wallet->IsSpent(key, 0);
        case WalletSpendPool::Sprout: return wallet->IsSproutSpent(key);
        case WalletSpendPool::Sapling: return wallet->IsSaplingSpent(key);
        case WalletSpendPool::Ironwood: return wallet->IsIronwoodSpent(key);
        }
        return false;
    }

    int SpendDepth(const uint256& key) const
    {
        switch (GetParam()) {
        case WalletSpendPool::Transparent: return wallet->GetSpendDepth(key, 0);
        case WalletSpendPool::Sprout: return wallet->GetSproutSpendDepth(key);
        case WalletSpendPool::Sapling: return wallet->GetSaplingSpendDepth(key);
        case WalletSpendPool::Ironwood: return wallet->GetIronwoodSpendDepth(key);
        }
        return 0;
    }

    void RemoveSpender(const uint256& txid) { wallet->RemoveFromSpends(txid); }

    std::unique_ptr<CWallet> wallet;
    uint256 confirmedTxid;
    uint256 offChainTxid;
    CBlockIndex* previousTip = nullptr;
    std::unique_ptr<CBlockIndex> confirmedBlock;

private:
    uint256 blockHash;
    bool blockInstalled = false;
};

TEST_P(WalletSpendTrackingTest, ConfirmedSpendSurvivesEitherProcessingOrder)
{
    LOCK2(cs_main, wallet->cs_wallet);
    for (bool confirmedFirst : {false, true}) {
        const uint256 key = wallet->mapWallet.at(confirmedTxid).vin[confirmedFirst ? 1 : 0].prevout.hash;
        AddSpender(key, confirmedFirst ? confirmedTxid : offChainTxid);
        AddSpender(key, confirmedFirst ? offChainTxid : confirmedTxid);

        EXPECT_EQ((std::multiset<uint256>{confirmedTxid, offChainTxid}), Spenders(key));
        EXPECT_TRUE(IsSpent(key));
        EXPECT_EQ(1, SpendDepth(key));
        if (GetParam() == WalletSpendPool::Transparent) {
            EXPECT_EQ((std::set<uint256>{confirmedTxid, offChainTxid}), wallet->GetConflicts(confirmedTxid));
            EXPECT_EQ((std::set<uint256>{confirmedTxid, offChainTxid}), wallet->GetConflicts(offChainTxid));
        }
    }
}

TEST_P(WalletSpendTrackingTest, RepeatedProcessingDoesNotDuplicateOrEraseSpenders)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const uint256 key = GetRandHash();
    AddSpender(key, offChainTxid);
    AddSpender(key, offChainTxid);
    EXPECT_EQ((std::multiset<uint256>{offChainTxid}), Spenders(key));
    EXPECT_FALSE(IsSpent(key));

    AddSpender(key, confirmedTxid);
    AddSpender(key, confirmedTxid);
    AddSpender(key, offChainTxid);
    EXPECT_EQ((std::multiset<uint256>{confirmedTxid, offChainTxid}), Spenders(key));
    EXPECT_TRUE(IsSpent(key));
    EXPECT_EQ(1, SpendDepth(key));

    // Deduplicating the pair must not skip metadata synchronization on updates.
    wallet->mapWallet.at(confirmedTxid).mapValue["comment"] = "updated transfer";
    wallet->mapWallet.at(offChainTxid).mapValue["comment"] = "earlier attempt";
    AddSpender(key, confirmedTxid);
    EXPECT_EQ("updated transfer", wallet->mapWallet.at(offChainTxid).mapValue.at("comment"));
    EXPECT_EQ((std::multiset<uint256>{confirmedTxid, offChainTxid}), Spenders(key));
}

TEST_P(WalletSpendTrackingTest, RemovalPreservesOtherSpendersAndInputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const uint256 sharedKey = GetRandHash();
    const uint256 otherKey = GetRandHash();
    AddSpender(sharedKey, confirmedTxid);
    AddSpender(sharedKey, offChainTxid);
    AddSpender(otherKey, confirmedTxid);

    RemoveSpender(offChainTxid);
    EXPECT_EQ((std::multiset<uint256>{confirmedTxid}), Spenders(sharedKey));
    EXPECT_EQ((std::multiset<uint256>{confirmedTxid}), Spenders(otherKey));
    EXPECT_TRUE(IsSpent(sharedKey));

    AddSpender(sharedKey, offChainTxid);
    RemoveSpender(confirmedTxid);
    EXPECT_EQ((std::multiset<uint256>{offChainTxid}), Spenders(sharedKey));
    EXPECT_TRUE(Spenders(otherKey).empty());
    EXPECT_FALSE(IsSpent(sharedKey));
    EXPECT_EQ(0, SpendDepth(sharedKey));

    RemoveSpender(offChainTxid);
    EXPECT_TRUE(Spenders(sharedKey).empty());
}

TEST_P(WalletSpendTrackingTest, SpentStateFollowsChainWithoutLosingCompetingAttempts)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const uint256 key = GetRandHash();
    AddSpender(key, confirmedTxid);
    AddSpender(key, offChainTxid);
    EXPECT_TRUE(IsSpent(key));

    chainActive.SetTip(previousTip);
    EXPECT_FALSE(IsSpent(key));
    EXPECT_EQ(0, SpendDepth(key));

    chainActive.SetTip(confirmedBlock.get());
    EXPECT_TRUE(IsSpent(key));
    EXPECT_EQ(1, SpendDepth(key));
    EXPECT_EQ((std::multiset<uint256>{confirmedTxid, offChainTxid}), Spenders(key));
}

INSTANTIATE_TEST_CASE_P(AllPools, WalletSpendTrackingTest,
    ::testing::Values(WalletSpendPool::Transparent, WalletSpendPool::Sprout,
                      WalletSpendPool::Sapling, WalletSpendPool::Ironwood));
