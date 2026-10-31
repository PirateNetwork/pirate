// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "init.h"
#include "net.h"
#include "random.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "ui_interface.h"
#include "wallet/walletdb.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

extern std::atomic<bool> fRequestShutdown;
extern bool loadComplete;

class ScanTestWallet : public CWallet
{
public:
    explicit ScanTestWallet(const std::string& filename) : CWallet(filename) {}

    int SaplingCheckpoint() const { return saplingWallet.GetLastCheckpointHeight(); }
    int IronwoodCheckpoint() const { return ironwoodWallet.GetLastCheckpointHeight(); }
    uint256 SaplingAnchor() const { return saplingWallet.GetLatestAnchor(); }
    uint256 IronwoodAnchor() const { return ironwoodWallet.GetLatestAnchor(); }
};

class ScanCoinsView : public CCoinsViewCache
{
public:
    explicit ScanCoinsView(CCoinsView* base) : CCoinsViewCache(base) {}

    bool GetSproutAnchorAt(const uint256& root, SproutMerkleTree& tree) const override
    {
        ++sproutReads;
        return CCoinsViewCache::GetSproutAnchorAt(root, tree);
    }
    bool GetSaplingFrontierAnchorAt(const uint256& root, SaplingMerkleFrontier& tree) const override
    {
        ++saplingReads;
        return CCoinsViewCache::GetSaplingFrontierAnchorAt(root, tree);
    }
    bool GetIronwoodFrontierAnchorAt(const uint256& root, IronwoodMerkleFrontier& tree) const override
    {
        ++ironwoodReads;
        return CCoinsViewCache::GetIronwoodFrontierAnchorAt(root, tree);
    }

    mutable int sproutReads = 0;
    mutable int saplingReads = 0;
    mutable int ironwoodReads = 0;
};

// Use real block-file reads and the wallet database, isolated by the shared
// fixture. The synthetic chain has transparent receipts and empty shielded
// frontiers: scanning must still advance both pools across their activations.
class WalletScanTest : public BitcoinTestingSetup
{
protected:
    void SelectTestParams() override
    {
        SelectParams(CBaseChainParams::REGTEST);
        previousOverwinter = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight;
        previousSapling = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
        previousIronwood = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_IRONWOOD].nActivationHeight;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, 1);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, 2);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, 4);
    }

    void SetUp() override
    {
        previousMaxConnections = nMaxConnections;
        previousDeleteEnabled = fTxDeleteEnabled;
        previousShutdown = fRequestShutdown.load();
        previousLoadComplete = loadComplete;
        previousBuilding = fBuilingWitnessCache;
        previousBuilt = fInitWitnessesBuilt;
        fRequestShutdown = false;
        loadComplete = false;
        nMaxConnections = 1;
        fTxDeleteEnabled = false;
        BitcoinTestingSetup::SetUp();

        UnregisterValidationInterface(pwalletMain);
        delete pwalletMain;
        wallet = new ScanTestWallet("wallet.dat");
        pwalletMain = wallet;
        bool firstRun;
        ASSERT_EQ(DB_LOAD_OK, wallet->LoadWallet(firstRun));
        RegisterValidationInterface(wallet);
        wallet->nBirthday = 1;

        CKey key;
        key.MakeNewKey(true);
        {
            LOCK(wallet->cs_wallet);
            ASSERT_TRUE(wallet->AddKeyPubKey(key, key.GetPubKey()));
        }
        receiptScript = GetScriptForDestination(key.GetPubKey().GetID());
        originalCoins = pcoinsTip;
        coins = std::make_unique<ScanCoinsView>(originalCoins);
        pcoinsTip = coins.get();
        progressConnection = uiInterface.ShowProgress.connect(
            [this](const std::string& title, int percent, bool) {
                progress.push_back({title, percent});
                if (progressHook) progressHook(title, percent);
            });
        errorConnection = uiInterface.ThreadSafeMessageBox.connect(
            [](const std::string&, const std::string&, unsigned int) { return false; });
    }

    void TearDown() override
    {
        progressConnection.disconnect();
        errorConnection.disconnect();
        progressHook = nullptr;
        fRequestShutdown = false;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, previousOverwinter);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, previousSapling);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_IRONWOOD, previousIronwood);
        if (originalCoins) pcoinsTip = originalCoins;
        coins.reset();
        BitcoinTestingSetup::TearDown();
        wallet = nullptr;
        nMaxConnections = previousMaxConnections;
        fTxDeleteEnabled = previousDeleteEnabled;
        fRequestShutdown = previousShutdown;
        loadComplete = previousLoadComplete;
        fBuilingWitnessCache = previousBuilding;
        fInitWitnessesBuilt = previousBuilt;
    }

    void AddBlocks(int count)
    {
        LOCK(cs_main);
        ASSERT_EQ(0, chainActive.Height());
        for (int height = 1; height <= count; ++height) {
            CBlock block;
            block.nVersion = 4;
            block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
            block.nTime = chainActive.Tip()->nTime + 60;
            if (height == 1 || height == count) {
                CMutableTransaction receipt;
                receipt.nVersion = 1;
                receipt.vin.emplace_back(COutPoint(GetRandHash(), 0));
                receipt.vout.emplace_back(height * COIN, receiptScript);
                block.vtx.emplace_back(receipt);
                receiptTxids.push_back(block.vtx.back().GetHash());
            }
            block.hashMerkleRoot = block.BuildMerkleTree();

            CDiskBlockPos position(7, nextFileOffset);
            ASSERT_TRUE(WriteBlockToDisk(block, position, Params().MessageStart()));
            nextFileOffset = position.nPos + GetSerializeSize(block, SER_DISK, CLIENT_VERSION);

            auto index = std::make_unique<CBlockIndex>(block);
            index->pprev = chainActive.Tip();
            index->nHeight = height;
            index->nFile = position.nFile;
            index->nDataPos = position.nPos;
            index->nStatus = BLOCK_HAVE_DATA;
            index->nTx = block.vtx.size();
            index->nChainTx = index->pprev->nChainTx + index->nTx;
            if (height >= 2) index->hashFinalSaplingRoot = SaplingMerkleFrontier::empty_root();
            if (height >= 4) index->hashFinalIronwoodRoot = IronwoodMerkleFrontier::empty_root();
            auto inserted = mapBlockIndex.emplace(block.GetHash(), index.get());
            ASSERT_TRUE(inserted.second);
            index->phashBlock = &inserted.first->first;
            index->BuildSkip();
            chainActive.SetTip(index.release());
        }
    }

    uint256 SavedBestBlock()
    {
        CBlockLocator locator;
        EXPECT_TRUE(CWalletDB(wallet->strWalletFile).ReadBestBlock(locator));
        EXPECT_FALSE(locator.vHave.empty());
        return locator.vHave.empty() ? uint256() : locator.vHave.front();
    }

    size_t ProgressCount(int percent) const
    {
        return std::count_if(progress.begin(), progress.end(), [percent](const Progress& item) {
            return item.percent == percent;
        });
    }

    struct Progress {
        std::string title;
        int percent;
    };
    ScanTestWallet* wallet = nullptr;
    CScript receiptScript;
    std::vector<uint256> receiptTxids;
    std::vector<Progress> progress;
    std::function<void(const std::string&, int)> progressHook;
    std::unique_ptr<ScanCoinsView> coins;

private:
    CCoinsViewCache* originalCoins = nullptr;
    boost::signals2::scoped_connection progressConnection;
    boost::signals2::scoped_connection errorConnection;
    unsigned int nextFileOffset = 0;
    int previousMaxConnections = 0;
    bool previousDeleteEnabled = false;
    bool previousShutdown = false;
    bool previousLoadComplete = false;
    bool previousBuilding = false;
    bool previousBuilt = false;
    int previousOverwinter = 0;
    int previousSapling = 0;
    int previousIronwood = 0;
};

TEST_F(WalletScanTest, ScansBothPoolsAndKeepsProgressOpenUntilWalletIsSaved)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(101);
    ASSERT_EQ(101, chainActive.Height());
    const uint256 startHash = chainActive[1]->GetBlockHash();
    const uint256 tipHash = chainActive.Tip()->GetBlockHash();
    bool savingObserved = false;
    bool completed = false;
    progressHook = [&](const std::string& title, int percent) {
        EXPECT_GE(percent, 0);
        EXPECT_LE(percent, 100);
        if (title == "Saving rescanned wallet...") {
            savingObserved = true;
            EXPECT_EQ(99, percent);
            EXPECT_EQ(startHash, SavedBestBlock());
            EXPECT_EQ(101, wallet->SaplingCheckpoint());
            EXPECT_EQ(101, wallet->IronwoodCheckpoint());
        }
        if (percent == 100) {
            EXPECT_TRUE(savingObserved);
            EXPECT_EQ(tipHash, SavedBestBlock());
            EXPECT_EQ(2u, wallet->mapWallet.size());
            completed = true;
        }
    };

    EXPECT_EQ(2, wallet->ScanForWalletTransactions(chainActive[1], true, true, false, true));

    EXPECT_TRUE(savingObserved);
    EXPECT_TRUE(completed);
    EXPECT_EQ(1u, ProgressCount(0));
    EXPECT_EQ(1u, ProgressCount(100));
    for (const auto& phase : {"Rebuilding Sapling witnesses: block 2 of 2...",
                              "Rebuilding Ironwood witnesses: block 4 of 4..."}) {
        EXPECT_TRUE(std::any_of(progress.begin(), progress.end(), [phase](const Progress& item) {
            return item.percent > 0 && item.percent < 100 && item.title == phase;
        }));
    }
    EXPECT_EQ(101, wallet->chainHeight);
    EXPECT_EQ(SaplingMerkleFrontier::empty_root(), wallet->SaplingAnchor());
    EXPECT_EQ(IronwoodMerkleFrontier::empty_root(), wallet->IronwoodAnchor());
    // No old notes need a preceding frontier, and each pool starts before its
    // activation. Scanning must not fetch unused anchors for every block.
    EXPECT_EQ(0, coins->sproutReads);
    EXPECT_EQ(0, coins->saplingReads);
    EXPECT_EQ(0, coins->ironwoodReads);
    ASSERT_EQ(2u, receiptTxids.size());
    EXPECT_EQ(101, wallet->mapWallet.at(receiptTxids.front()).GetDepthInMainChain());
    EXPECT_EQ(1, wallet->mapWallet.at(receiptTxids.back()).GetDepthInMainChain());
    EXPECT_TRUE(std::any_of(progress.begin(), progress.end(), [](const Progress& item) {
        return item.percent > 0 && item.percent < 100 &&
            item.title.find("Scanning wallet transactions and witnesses: block 100 of 101") == 0;
    }));
}

TEST_F(WalletScanTest, RepeatedSingleBlockScanReportsProgressAndKeepsBothPoolsCurrent)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(4);
    for (int attempt = 0; attempt < 2; ++attempt) {
        progress.clear();
        EXPECT_EQ(1, wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, attempt == 0));
        EXPECT_EQ(4, wallet->SaplingCheckpoint());
        EXPECT_EQ(4, wallet->IronwoodCheckpoint());
        EXPECT_EQ(1u, ProgressCount(0));
        EXPECT_EQ(1u, ProgressCount(100));
        EXPECT_TRUE(std::any_of(progress.begin(), progress.end(), [](const Progress& item) {
            return item.percent > 0 && item.percent < 100 &&
                item.title == "Scanning wallet transactions and witnesses: block 4 of 4...";
        }));
        EXPECT_EQ(chainActive.Tip()->GetBlockHash(), SavedBestBlock());
    }
    EXPECT_EQ(1u, wallet->mapWallet.size());
}

TEST_F(WalletScanTest, InterruptedScanKeepsStartingLocatorForRestart)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(101);
    const uint256 startHash = chainActive[1]->GetBlockHash();
    loadComplete = true;
    progressHook = [&](const std::string&, int percent) {
        if (percent > 0 && percent < 100) {
            StartShutdown();
            wallet->FlushBestChain();
            EXPECT_EQ(startHash, SavedBestBlock());
        }
    };

    wallet->ScanForWalletTransactions(chainActive[1], true, true, false, true);

    EXPECT_TRUE(ShutdownRequested());
    EXPECT_EQ(startHash, SavedBestBlock());
    StartShutdown();
    wallet->FlushBestChain();
    EXPECT_EQ(startHash, SavedBestBlock());
    EXPECT_EQ(1, wallet->chainHeight);
    EXPECT_EQ(1u, ProgressCount(100));
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ("Wallet rescan interrupted", progress.back().title);
    EXPECT_FALSE(std::any_of(progress.begin(), progress.end(), [](const Progress& item) {
        return item.title == "Saving rescanned wallet...";
    }));
}

TEST_F(WalletScanTest, ShutdownBeforeFirstWalletUpdateKeepsSavedLocator)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(4);
    const uint256 tipHash = chainActive.Tip()->GetBlockHash();
    // Loading a wallet can leave the in-memory marker empty until a new block
    // or rescan updates it. Its existing database locator must survive shutdown.
    wallet->SetBestChain(chainActive.GetLocator(), chainActive.Height());
    ASSERT_EQ(tipHash, SavedBestBlock());

    loadComplete = true;
    StartShutdown();
    wallet->FlushBestChain();

    EXPECT_TRUE(ShutdownRequested());
    EXPECT_EQ(tipHash, SavedBestBlock());
}

TEST_F(WalletScanTest, FailedRestartSaveDoesNotBeginScanning)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    auto* target = chainActive[5];
    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive[4], true, true, false, true), 0);
    const uint256 saved = SavedBestBlock();
    chainActive.SetTip(target);
    const std::string originalFile = wallet->strWalletFile;
    wallet->strWalletFile = "uncreated-rescan-failure.dat";
    progress.clear();

    EXPECT_EQ(-1, wallet->ScanForWalletTransactions(chainActive[1], true, true, false, true));

    wallet->strWalletFile = originalFile;
    EXPECT_EQ(saved, SavedBestBlock());
    EXPECT_EQ(4, wallet->chainHeight);
    EXPECT_EQ(4, wallet->SaplingCheckpoint());
    EXPECT_EQ(4, wallet->IronwoodCheckpoint());
    EXPECT_TRUE(ShutdownRequested());
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ("Wallet rescan failed", progress.back().title);
}

TEST_F(WalletScanTest, FailedCompletionSaveRetainsTheRestartPoint)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    const std::string originalFile = wallet->strWalletFile;
    const uint256 restartHash = chainActive[1]->GetBlockHash();
    progressHook = [&](const std::string& title, int) {
        if (title == "Saving rescanned wallet...") {
            wallet->strWalletFile = "uncreated-completion-failure.dat";
        }
    };

    EXPECT_EQ(-1, wallet->ScanForWalletTransactions(chainActive[1], true, true, false, true));

    wallet->strWalletFile = originalFile;
    progressHook = nullptr;
    EXPECT_EQ(restartHash, SavedBestBlock());
    EXPECT_EQ(1, wallet->chainHeight);
    EXPECT_TRUE(ShutdownRequested());
    EXPECT_TRUE(wallet->fRunSetBestChain);
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ("Wallet rescan failed", progress.back().title);
    EXPECT_TRUE(wallet->FlushBestChain());
    EXPECT_EQ(restartHash, SavedBestBlock());
}

TEST_F(WalletScanTest, ChainTipUsesTheProvidedBlockForBothPools)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);

    CBlock previousBlock;
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(previousBlock, chainActive[4], 1));
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, chainActive[5], 1));
    wallet->ProcessSaplingBlockTransactions(chainActive[4], &previousBlock);
    wallet->ProcessIronwoodBlockTransactions(chainActive[4], &previousBlock);

    // ConnectTip has already read and validated this block. Neither pool should
    // need another disk read while handling the synchronous notification.
    chainActive[5]->nStatus &= ~BLOCK_HAVE_DATA;
    chainActive[5]->nFile = -1;
    EXPECT_NO_THROW(wallet->ChainTip(chainActive[5], &connectedBlock, true));

    EXPECT_EQ(5, wallet->SaplingCheckpoint());
    EXPECT_EQ(5, wallet->IronwoodCheckpoint());
    EXPECT_EQ(SaplingMerkleFrontier::empty_root(), wallet->SaplingAnchor());
    EXPECT_EQ(IronwoodMerkleFrontier::empty_root(), wallet->IronwoodAnchor());
}

TEST_F(WalletScanTest, InterruptedChainTipRebuildKeepsThePreviousWalletPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));

    chainActive.SetTip(chainActive[4]);
    wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true);
    const uint256 previousHash = chainActive.Tip()->GetBlockHash();
    ASSERT_EQ(previousHash, SavedBestBlock());
    ASSERT_EQ(4, wallet->SaplingCheckpoint());
    ASSERT_EQ(4, wallet->IronwoodCheckpoint());
    chainActive.SetTip(connectedIndex);

    // Force a real rebuild during the notification, then stop it before the
    // target block is appended. The second pool and wallet locator must stay put.
    ASSERT_TRUE(wallet->SaplingWalletReset());
    bool interrupted = false;
    progressHook = [&](const std::string& title, int percent) {
        if (title.find("Rebuilding Sapling witnesses:") == 0 && percent > 0 && percent < 100) {
            interrupted = true;
            StartShutdown();
        }
    };
    wallet->ChainTip(connectedIndex, &connectedBlock, true);
    ASSERT_TRUE(interrupted);
    EXPECT_EQ(-1, wallet->SaplingCheckpoint());
    EXPECT_EQ(4, wallet->IronwoodCheckpoint());
    wallet->FlushBestChain();
    EXPECT_EQ(previousHash, SavedBestBlock());
    EXPECT_EQ(4, wallet->chainHeight);

    progressHook = nullptr;
    fRequestShutdown = false;
    ScanTestWallet reopened(wallet->strWalletFile);
    bool firstRun;
    ASSERT_EQ(DB_LOAD_OK, reopened.LoadWallet(firstRun));
    EXPECT_EQ(-1, reopened.SaplingCheckpoint());
    EXPECT_EQ(4, reopened.IronwoodCheckpoint());
    reopened.ChainTip(connectedIndex, &connectedBlock, true);
    EXPECT_EQ(5, reopened.SaplingCheckpoint());
    EXPECT_EQ(5, reopened.IronwoodCheckpoint());
    EXPECT_EQ(SaplingMerkleFrontier::empty_root(), reopened.SaplingAnchor());
    EXPECT_EQ(IronwoodMerkleFrontier::empty_root(), reopened.IronwoodAnchor());
}
