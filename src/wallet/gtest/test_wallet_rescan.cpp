// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "random.h"
#include "rpc/server.h"
#include "wallet/wallet.h"

#include <memory>
#include <stdexcept>
#include <vector>

enum class RescanPool { Sapling, Ironwood };

class RescanTestWallet : public CWallet
{
public:
    RescanTestWallet() : CWallet("rescan-test.dat") {}

    int LastCheckpoint(RescanPool pool) const
    {
        return pool == RescanPool::Sapling ? saplingWallet.GetLastCheckpointHeight() : ironwoodWallet.GetLastCheckpointHeight();
    }

    bool IsValidated(RescanPool pool) const
    {
        return pool == RescanPool::Sapling ? saplingWalletValidated : ironwoodWalletValidated;
    }

    uint256 Anchor(RescanPool pool) const
    {
        return pool == RescanPool::Sapling ? saplingWallet.GetLatestAnchor() : ironwoodWallet.GetLatestAnchor();
    }
};

// Record the chain frontier requested for rebuilding, then fail before reset.
// This tests the real rebuild boundary without constructing shielded proofs.
class RecordingRescanView : public CCoinsView
{
public:
    mutable std::vector<uint256> requestedRoots;

    bool GetSaplingFrontierAnchorAt(const uint256& root, SaplingMerkleFrontier&) const override
    {
        requestedRoots.push_back(root);
        return false;
    }

    bool GetIronwoodFrontierAnchorAt(const uint256& root, IronwoodMerkleFrontier&) const override
    {
        requestedRoots.push_back(root);
        return false;
    }
};

class WalletRescanTest : public BitcoinTestingSetup,
                         public ::testing::WithParamInterface<RescanPool>
{
protected:
    void SelectTestParams() override
    {
        SelectParams(CBaseChainParams::REGTEST);
        previousOverwinter = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight;
        previousSapling = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
        previousIronwood = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_IRONWOOD].nActivationHeight;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 50);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 100);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, 200);
    }

    void SetUp() override
    {
        BitcoinTestingSetup::SetUp();
        LOCK(cs_main);
        wallet.reset(new RescanTestWallet());
        bool firstRun;
        ASSERT_EQ(DB_LOAD_OK, wallet->LoadWallet(firstRun));
        previousTip = chainActive.Tip();
        ASSERT_NE(nullptr, previousTip);
        ASSERT_EQ(0, previousTip->nHeight);
        previousBuilding = fBuilingWitnessCache;
        previousBuilt = fInitWitnessesBuilt;

        CBlockIndex* previous = previousTip;
        unsigned int nextPosition = 0;
        for (int height = previous->nHeight + 1; height <= 206; ++height) {
            CMutableTransaction tx;
            tx.vin.emplace_back(COutPoint(GetRandHash(), 0));
            tx.vout.emplace_back(1, CScript());
            tx.nLockTime = height;

            CBlock block;
            block.hashPrevBlock = previous->GetBlockHash();
            block.nTime = previous->nTime + 60;
            block.vtx.emplace_back(tx);
            block.hashMerkleRoot = block.BuildMerkleTree();
            CDiskBlockPos position(7, nextPosition);
            ASSERT_TRUE(WriteBlockToDisk(block, position, Params().MessageStart()));
            nextPosition = position.nPos + GetSerializeSize(block, SER_DISK, CLIENT_VERSION);

            CBlockIndex* index = new CBlockIndex(block);
            index->pprev = previous;
            index->nHeight = height;
            index->nFile = position.nFile;
            index->nDataPos = position.nPos;
            index->nStatus |= BLOCK_HAVE_DATA;
            index->hashFinalSaplingRoot = SaplingMerkleFrontier::empty_root();
            index->hashFinalIronwoodRoot = IronwoodMerkleFrontier::empty_root();
            auto inserted = mapBlockIndex.emplace(block.GetHash(), index);
            ASSERT_TRUE(inserted.second);
            index->phashBlock = &inserted.first->first;
            previous = index;
            blocks.push_back(block);
        }
        chainActive.SetTip(previous);
    }

    void TearDown() override
    {
        {
            LOCK(cs_main);
            if (originalCoins) {
                delete pcoinsTip;
                pcoinsTip = originalCoins;
                originalCoins = nullptr;
            }
            wallet.reset();
            fBuilingWitnessCache = previousBuilding;
            fInitWitnessesBuilt = previousBuilt;
        }
        // The base fixture owns all mapBlockIndex entries, including our chain.
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, previousOverwinter);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, previousSapling);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, previousIronwood);
        BitcoinTestingSetup::TearDown();
    }

    int ActivationHeight() const
    {
        const auto upgrade = GetParam() == RescanPool::Sapling ? Consensus::UPGRADE_SAPLING : Consensus::UPGRADE_IRONWOOD;
        return Params().GetConsensus().vUpgrades[upgrade].nActivationHeight;
    }

    const CBlock& Block(int height) const { return blocks.at(height - previousTip->nHeight - 1); }

    void AddNote(int height, RescanPool pool, CBlockIndex* index = nullptr)
    {
        CWalletTx tx(wallet.get(), Block(height).vtx.front());
        tx.SetMerkleBranch(Block(height));
        if (index) {
            tx.hashBlock = index->GetBlockHash();
        }
        if (pool == RescanPool::Sapling) {
            tx.mapSaplingNoteData.emplace(SaplingOutPoint(tx.GetHash(), 0), SaplingNoteData());
        } else {
            tx.mapIronwoodNoteData.emplace(IronwoodOutPoint(tx.GetHash(), 0), IronwoodNoteData());
        }
        wallet->mapWallet.emplace(tx.GetHash(), tx);
    }

    bool Validate(const CBlockIndex* index)
    {
        return GetParam() == RescanPool::Sapling ? wallet->ValidateSaplingWalletTrackedPositions(index) : wallet->ValidateIronwoodWalletTrackedPositions(index);
    }

    void Increment(int height)
    {
        if (GetParam() == RescanPool::Sapling) {
            wallet->IncrementSaplingWallet(chainActive[height], &Block(height), true);
        } else {
            wallet->IncrementIronwoodWallet(chainActive[height], &Block(height), true);
        }
    }

    void Process(int height)
    {
        if (GetParam() == RescanPool::Sapling) {
            wallet->ProcessSaplingBlockTransactions(chainActive[height], &Block(height));
        } else {
            wallet->ProcessIronwoodBlockTransactions(chainActive[height], &Block(height));
        }
    }

    void RecordFrontierRequests()
    {
        originalCoins = pcoinsTip;
        pcoinsTip = new CCoinsViewCache(&recordingView);
        for (int height = 1; height <= chainActive.Height(); ++height) {
            chainActive[height]->hashFinalSaplingRoot = GetRandHash();
            chainActive[height]->hashFinalIronwoodRoot = GetRandHash();
        }
    }

    uint256 Root(int height) const
    {
        return GetParam() == RescanPool::Sapling ? chainActive[height]->hashFinalSaplingRoot : chainActive[height]->hashFinalIronwoodRoot;
    }

    std::unique_ptr<RescanTestWallet> wallet;
    CBlockIndex* previousTip = nullptr;
    std::vector<CBlock> blocks;
    RecordingRescanView recordingView;
    CCoinsViewCache* originalCoins = nullptr;
    bool previousBuilding = false;
    bool previousBuilt = false;
    int previousOverwinter = 0;
    int previousSapling = 0;
    int previousIronwood = 0;
};

TEST_P(WalletRescanTest, ValidationOnlyRequiresNotesThroughItsHeight)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddNote(205, GetParam());
    ASSERT_GT(wallet->mapWallet.begin()->second.GetDepthInMainChain(), 0);
    EXPECT_TRUE(Validate(nullptr));
    EXPECT_TRUE(Validate(chainActive[204]));
    EXPECT_FALSE(Validate(chainActive[205]));
    EXPECT_FALSE(Validate(chainActive[206]));
}

TEST_P(WalletRescanTest, RebuildIgnoresOtherPoolsFutureBlocksAndForks)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddNote(5, GetParam() == RescanPool::Sapling ? RescanPool::Ironwood : RescanPool::Sapling);
    AddNote(206, GetParam());
    CBlock forkBlock = Block(6);
    forkBlock.nTime++;
    auto* forkIndex = new CBlockIndex(forkBlock);
    forkIndex->pprev = chainActive[5];
    forkIndex->nHeight = 6;
    auto inserted = mapBlockIndex.emplace(forkBlock.GetHash(), forkIndex);
    ASSERT_TRUE(inserted.second);
    forkIndex->phashBlock = &inserted.first->first;
    AddNote(6, GetParam(), forkIndex);
    RecordFrontierRequests();

    EXPECT_THROW(Increment(205), std::runtime_error);
    ASSERT_EQ(1, recordingView.requestedRoots.size());
    EXPECT_EQ(Root(204), recordingView.requestedRoots.front());
    EXPECT_EQ(-1, wallet->LastCheckpoint(GetParam()));
}

TEST_P(WalletRescanTest, RebuildSeedsImmediatelyBeforeEarliestRelevantNote)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddNote(202, GetParam());
    AddNote(204, GetParam());
    RecordFrontierRequests();

    EXPECT_THROW(Increment(205), std::runtime_error);
    ASSERT_EQ(1, recordingView.requestedRoots.size());
    EXPECT_EQ(Root(201), recordingView.requestedRoots.front());
}

TEST_P(WalletRescanTest, RebuildDoesNotProcessBlocksBeforeActivation)
{
    LOCK2(cs_main, wallet->cs_wallet);
    // Deliberately inconsistent metadata must not pull a rebuild into blocks
    // where the pool cannot exist. Make that old block unavailable as well.
    AddNote(ActivationHeight() - 1, GetParam());
    chainActive[ActivationHeight() - 1]->nStatus &= ~BLOCK_HAVE_DATA;
    chainActive[ActivationHeight() - 1]->nFile = -1;
    ASSERT_TRUE(chainActive[ActivationHeight() - 1]->GetBlockPos().IsNull());

    Increment(ActivationHeight() + 2);
    EXPECT_EQ(ActivationHeight() + 2, wallet->LastCheckpoint(GetParam()));
}

TEST_P(WalletRescanTest, RebuildPreservesEarlierCommitmentsWithoutReadingEarlierBlocks)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddNote(5, GetParam() == RescanPool::Sapling ? RescanPool::Ironwood : RescanPool::Sapling);
    uint256 expectedRoot;
    if (GetParam() == RescanPool::Sapling) {
        SaplingMerkleFrontier frontier;
        frontier.append(uint256S("03"));
        expectedRoot = frontier.root();
        ASSERT_NE(SaplingMerkleFrontier::empty_root(), expectedRoot);
        pcoinsTip->PushAnchor(frontier);
        chainActive[204]->hashFinalSaplingRoot = expectedRoot;
        chainActive[205]->hashFinalSaplingRoot = expectedRoot;
    } else {
        IronwoodMerkleFrontier frontier;
        frontier.append(uint256S("03"));
        expectedRoot = frontier.root();
        ASSERT_NE(IronwoodMerkleFrontier::empty_root(), expectedRoot);
        pcoinsTip->PushAnchor(frontier);
        chainActive[204]->hashFinalIronwoodRoot = expectedRoot;
        chainActive[205]->hashFinalIronwoodRoot = expectedRoot;
    }
    // Only the saved frontier is available for history before the target block.
    for (int height = 1; height < 205; ++height) {
        chainActive[height]->nFile = -1;
        chainActive[height]->nStatus &= ~BLOCK_HAVE_DATA;
    }

    Increment(205);
    EXPECT_EQ(205, wallet->LastCheckpoint(GetParam()));
    EXPECT_EQ(expectedRoot, wallet->Anchor(GetParam()));
    EXPECT_TRUE(wallet->IsValidated(GetParam()));
}

TEST_P(WalletRescanTest, ValidPositionsCannotOverrideAnAnchorMismatch)
{
    LOCK2(cs_main, wallet->cs_wallet);
    Process(204);
    ASSERT_EQ(204, wallet->LastCheckpoint(GetParam()));
    RecordFrontierRequests();

    EXPECT_THROW(Increment(205), std::runtime_error);
    ASSERT_EQ(1, recordingView.requestedRoots.size());
    EXPECT_EQ(Root(204), recordingView.requestedRoots.front());
    // Unavailable chain state leaves the existing witness tree intact.
    EXPECT_EQ(204, wallet->LastCheckpoint(GetParam()));
    EXPECT_FALSE(wallet->IsValidated(GetParam()));
    EXPECT_THROW(Increment(205), std::runtime_error);
    EXPECT_EQ(2, recordingView.requestedRoots.size());
}

TEST_P(WalletRescanTest, GenesisActivationDoesNotRequireAPredecessor)
{
    LOCK2(cs_main, wallet->cs_wallet);
    const auto upgrade = GetParam() == RescanPool::Sapling ? Consensus::UPGRADE_SAPLING : Consensus::UPGRADE_IRONWOOD;
    struct RestoreActivation {
        Consensus::UpgradeIndex upgrade;
        int height;
        ~RestoreActivation() { UpdateNetworkUpgradeParameters(upgrade, height); }
    } restore{upgrade, ActivationHeight()};
    UpdateNetworkUpgradeParameters(upgrade, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    if (GetParam() == RescanPool::Sapling) {
        wallet->IncrementSaplingWallet(chainActive[0], &Params().GenesisBlock(), true);
    } else {
        wallet->IncrementIronwoodWallet(chainActive[0], &Params().GenesisBlock(), true);
    }
    EXPECT_EQ(0, wallet->LastCheckpoint(GetParam()));
    EXPECT_TRUE(wallet->IsValidated(GetParam()));
}

INSTANTIATE_TEST_CASE_P(ShieldedPools, WalletRescanTest,
    ::testing::Values(RescanPool::Sapling, RescanPool::Ironwood));
