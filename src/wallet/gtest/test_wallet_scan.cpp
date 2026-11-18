// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "gtest/gtestutils.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "net.h"
#include "random.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "ui_interface.h"
#include "wallet/walletdb.h"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

extern std::atomic<bool> fRequestShutdown;
extern bool loadComplete;
extern UniValue CallRPC(std::string args);

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
            [this](const std::string& message, const std::string& caption, unsigned int style) {
                uiMessages.push_back({message, caption, style, ShutdownRequested()});
                return false;
            });
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

    void AddBlocks(int count, int spacing = 60)
    {
        LOCK(cs_main);
        ASSERT_EQ(0, chainActive.Height());
        for (int height = 1; height <= count; ++height) {
            CBlock block;
            block.nVersion = 4;
            block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
            block.nTime = chainActive.Tip()->nTime + spacing;
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

    // A wallet persistence failure is reported with a modal dialog, raised
    // while the caller still holds its locks so the node stays frozen until the
    // operator has read it. It must be raised before shutdown is requested: the
    // GUI's shutdown poll quits the application, which closes every event loop
    // on the GUI thread, the dialog's included.
    void ExpectModalWalletErrorBeforeShutdown() const
    {
        ASSERT_FALSE(uiMessages.empty());
        const UiMessage& first = uiMessages.front();
        EXPECT_EQ("Wallet error", first.caption);
        EXPECT_TRUE(first.style & CClientUIInterface::MODAL) << first.message;
        EXPECT_TRUE(first.style & CClientUIInterface::ICON_ERROR) << first.message;
        EXPECT_FALSE(first.shutdownRequestedWhenRaised)
            << "dialog raised after shutdown was requested; the shutdown poll would close it: "
            << first.message;
        EXPECT_NE(std::string::npos, first.message.find("Check disk space and debug.log"))
            << first.message;
        EXPECT_TRUE(ShutdownRequested());
    }

    struct Progress {
        std::string title;
        int percent;
    };
    struct UiMessage {
        std::string message;
        std::string caption;
        unsigned int style;
        bool shutdownRequestedWhenRaised;
    };
    ScanTestWallet* wallet = nullptr;
    CScript receiptScript;
    std::vector<uint256> receiptTxids;
    std::vector<Progress> progress;
    std::vector<UiMessage> uiMessages;
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

// Puts the node in offline (cold storage) mode as -maxconnections=0 does: no
// blockchain, block index, block database or coins database loaded. Restores
// them all on the way out.
struct ScopedOfflineMode {
    CBlockIndex* tip;
    CCoinsViewCache* coinsTip;
    CBlockTreeDB* blockTree;
    BlockMap blockIndex;
    int maxConnections = nMaxConnections;
    ScopedOfflineMode()
    {
        LOCK(cs_main);
        tip = chainActive.Tip();
        chainActive.SetTip(nullptr);
        mapBlockIndex.swap(blockIndex);
        coinsTip = pcoinsTip;
        pcoinsTip = nullptr;
        blockTree = pblocktree;
        pblocktree = nullptr;
        nMaxConnections = 0;
    }
    ~ScopedOfflineMode()
    {
        LOCK(cs_main);
        mapBlockIndex.swap(blockIndex);
        chainActive.SetTip(tip);
        pcoinsTip = coinsTip;
        pblocktree = blockTree;
        nMaxConnections = maxConnections;
    }
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
    ExpectModalWalletErrorBeforeShutdown();
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
    ExpectModalWalletErrorBeforeShutdown();
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

TEST_F(WalletScanTest, FailedChainTipProcessingKeepsThePreviousWalletPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    const uint256 previousHash = chainActive.Tip()->GetBlockHash();
    ASSERT_EQ(previousHash, SavedBestBlock());
    chainActive.SetTip(connectedIndex);
    connectedIndex->nStatus &= ~BLOCK_HAVE_DATA;
    connectedIndex->nFile = -1;

    EXPECT_NO_THROW(wallet->ChainTip(connectedIndex, nullptr, true));

    EXPECT_TRUE(ShutdownRequested());
    EXPECT_EQ(4, wallet->chainHeight);
    EXPECT_EQ(previousHash, SavedBestBlock());
    ExpectModalWalletErrorBeforeShutdown();
}

// cs_main and cs_wallet are held here exactly as validation holds them when it
// drives ChainTip from ConnectTip. The report is a modal dialog raised from that
// position, which is what freezes the node until the operator has read it. This
// covers the wallet's side: the flags it asks for and the order relative to
// shutdown. Whether the GUI thread can then service the dialog without blocking
// on those same locks is not reachable from pirate-gtest, which has no GUI
// thread, and is not covered anywhere.
TEST_F(WalletScanTest, FatalWalletReportIsModalAndPrecedesShutdown)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));
    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    ASSERT_TRUE(uiMessages.empty());
    chainActive.SetTip(connectedIndex);

    // Hand the block over intact and fail only the write, so the report comes
    // from the save rather than from unreadable block data.
    const std::string originalFile = wallet->strWalletFile;
    wallet->strWalletFile = "uncreated-chaintip-failure.dat";
    wallet->fRunSetBestChain = true;

    wallet->ChainTip(connectedIndex, &connectedBlock, true);

    wallet->strWalletFile = originalFile;
    ASSERT_EQ(1u, uiMessages.size());
    ExpectModalWalletErrorBeforeShutdown();
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

// Shutdown can be requested while any block is being connected. Here both pools
// update by one block, which shutdown does not interrupt - only a rebuild stops
// for it - so both reach the block, and the saved position has to follow them.
// Left behind, it would be saved with trees already past it, and the next start
// would find the trees ahead of its scan position and rebuild them from the
// wallet's oldest note.
TEST_F(WalletScanTest, ShutdownDuringCompletedChainTipRecordsTheNewPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));

    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    ASSERT_EQ(chainActive.Tip()->GetBlockHash(), SavedBestBlock());
    ASSERT_EQ(4, wallet->SaplingCheckpoint());
    ASSERT_EQ(4, wallet->IronwoodCheckpoint());
    chainActive.SetTip(connectedIndex);

    // The only shutdown in play is the one requested here.
    ASSERT_FALSE(ShutdownRequested());
    StartShutdown();
    wallet->ChainTip(connectedIndex, &connectedBlock, true);

    // The second pool is still brought up to the block, and the position
    // follows both.
    EXPECT_EQ(5, wallet->SaplingCheckpoint());
    EXPECT_EQ(5, wallet->IronwoodCheckpoint());
    EXPECT_EQ(5, wallet->chainHeight);

    // Shutdown saves the position and both trees in one write; they must agree.
    ASSERT_TRUE(wallet->FlushBestChain());
    EXPECT_EQ(connectedIndex->GetBlockHash(), SavedBestBlock());

    fRequestShutdown = false;
    ScanTestWallet reopened(wallet->strWalletFile);
    bool firstRun;
    ASSERT_EQ(DB_LOAD_OK, reopened.LoadWallet(firstRun));
    EXPECT_EQ(5, reopened.SaplingCheckpoint());
    EXPECT_EQ(5, reopened.IronwoodCheckpoint());
}

// Once Sapling has reached the block, the saved position must follow it even if
// Ironwood's rebuild then stops for shutdown. Kept at the previous block, the
// position would trail the saved Sapling tree, and the next start would rebuild
// Sapling from the wallet's oldest note. Ironwood is left behind instead, which
// is harmless: startup rescans from the saved position, and Ironwood rebuilds
// itself on that block. Ironwood always rebuilds on its activation block, so
// this is not only reached after a fault.
TEST_F(WalletScanTest, ShutdownDuringIronwoodRebuildRecordsSaplingsPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));

    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    ASSERT_EQ(4, wallet->SaplingCheckpoint());
    ASSERT_EQ(4, wallet->IronwoodCheckpoint());
    chainActive.SetTip(connectedIndex);

    // Ironwood must rebuild on this block; Sapling only advances by one. With
    // shutdown already requested, Ironwood's rebuild stops before the block.
    ASSERT_TRUE(wallet->IronwoodWalletReset());
    ASSERT_FALSE(ShutdownRequested());
    StartShutdown();
    wallet->ChainTip(connectedIndex, &connectedBlock, true);

    EXPECT_EQ(5, wallet->SaplingCheckpoint());
    EXPECT_LT(wallet->IronwoodCheckpoint(), 5);
    EXPECT_EQ(5, wallet->chainHeight);
    ASSERT_TRUE(wallet->FlushBestChain());
    EXPECT_EQ(connectedIndex->GetBlockHash(), SavedBestBlock());

    // Restart the way startup does: rescan from the saved position without
    // resetting. Sapling is not ahead of it, so it keeps its tree, and Ironwood
    // rebuilds itself on the saved block.
    fRequestShutdown = false;
    ScanTestWallet reopened(wallet->strWalletFile);
    bool firstRun;
    ASSERT_EQ(DB_LOAD_OK, reopened.LoadWallet(firstRun));
    EXPECT_EQ(5, reopened.SaplingCheckpoint());
    ASSERT_GE(reopened.ScanForWalletTransactions(connectedIndex, true, true, false, false), 0);
    EXPECT_EQ(5, reopened.SaplingCheckpoint());
    EXPECT_EQ(5, reopened.IronwoodCheckpoint());
    EXPECT_EQ(chainActive[5]->hashFinalSaplingRoot, reopened.SaplingAnchor());
}

// Disconnecting a block rewinds both trees to its parent, and the chain tip has
// already moved back there by the time the wallet is told. The recorded position
// must be the parent as well, not the block that was just removed: that block is
// no longer in the chain, and is one higher than the trees.
TEST_F(WalletScanTest, DisconnectedChainTipRecordsTheParentPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));

    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    chainActive.SetTip(connectedIndex);
    wallet->ChainTip(connectedIndex, &connectedBlock, true);
    ASSERT_EQ(5, wallet->SaplingCheckpoint());
    ASSERT_EQ(5, wallet->IronwoodCheckpoint());
    ASSERT_EQ(5, wallet->chainHeight);

    // DisconnectTip moves the tip back to the parent before notifying the wallet.
    CBlockIndex* parentIndex = chainActive[4];
    chainActive.SetTip(parentIndex);
    wallet->ChainTip(connectedIndex, &connectedBlock, false);

    EXPECT_EQ(4, wallet->SaplingCheckpoint());
    EXPECT_EQ(4, wallet->IronwoodCheckpoint());
    EXPECT_EQ(4, wallet->chainHeight);
    ASSERT_TRUE(wallet->FlushBestChain());
    EXPECT_EQ(parentIndex->GetBlockHash(), SavedBestBlock());

    // Restart the way startup does: resolve the saved locator against the chain
    // and rescan from there without resetting. A saved tree ahead of that point
    // would be reset and rebuilt from the wallet's oldest note; neither is.
    CBlockLocator saved;
    ASSERT_TRUE(CWalletDB(wallet->strWalletFile).ReadBestBlock(saved));
    CBlockIndex* rescanFrom = FindForkInGlobalIndex(chainActive, saved);
    ASSERT_EQ(parentIndex, rescanFrom);
    ScanTestWallet reopened(wallet->strWalletFile);
    bool firstRun;
    ASSERT_EQ(DB_LOAD_OK, reopened.LoadWallet(firstRun));
    EXPECT_LE(reopened.SaplingCheckpoint(), rescanFrom->nHeight);
    EXPECT_LE(reopened.IronwoodCheckpoint(), rescanFrom->nHeight);
    ASSERT_GE(reopened.ScanForWalletTransactions(rescanFrom, true, true, false, false), 0);
    EXPECT_EQ(4, reopened.SaplingCheckpoint());
    EXPECT_EQ(4, reopened.IronwoodCheckpoint());
}

// After a disconnect the rewound trees take the next block as usual, and the
// position moves forward with them again.
TEST_F(WalletScanTest, ReconnectAfterDisconnectRecordsTheNewPosition)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(5);
    CBlockIndex* connectedIndex = chainActive[5];
    CBlock connectedBlock;
    ASSERT_TRUE(ReadBlockFromDisk(connectedBlock, connectedIndex, 1));

    chainActive.SetTip(chainActive[4]);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
    chainActive.SetTip(connectedIndex);
    wallet->ChainTip(connectedIndex, &connectedBlock, true);
    ASSERT_EQ(5, wallet->chainHeight);

    chainActive.SetTip(chainActive[4]);
    wallet->ChainTip(connectedIndex, &connectedBlock, false);
    ASSERT_EQ(4, wallet->SaplingCheckpoint());
    ASSERT_EQ(4, wallet->IronwoodCheckpoint());
    ASSERT_EQ(4, wallet->chainHeight);

    chainActive.SetTip(connectedIndex);
    wallet->ChainTip(connectedIndex, &connectedBlock, true);

    EXPECT_EQ(5, wallet->SaplingCheckpoint());
    EXPECT_EQ(5, wallet->IronwoodCheckpoint());
    EXPECT_EQ(5, wallet->chainHeight);
    ASSERT_TRUE(wallet->FlushBestChain());
    EXPECT_EQ(connectedIndex->GetBlockHash(), SavedBestBlock());
}

// Unlocking saves the wallet's pending state, and a lock landing between the
// unlock and the save made the save find the wallet locked and report a disk
// failure, shutting the node down. CCryptoKeyStore::Unlock announces the change
// after releasing its own hold on cs_KeyStore, which is exactly that window, so
// lock the wallet from another thread there - as the GUI does, from its own
// thread - and give the lock time to land. It must wait until the unlock and
// its save are done.
TEST_F(WalletScanTest, LockRacingUnlockIsNotReportedAsASaveFailure)
{
    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "lock race";
    {
        LOCK2(cs_main, wallet->cs_wallet);
        AddBlocks(4);
        ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);
        {
            ScopedWorkingDirectory workingDirectory(pathTemp);
            ASSERT_TRUE(workingDirectory.ok);
            wallet->GenerateNewSeed();
            ASSERT_TRUE(wallet->EncryptWallet(passphrase));
        }
        if (!wallet->IsLocked()) {
            ASSERT_TRUE(wallet->Lock());
        }
    }
    uiMessages.clear();
    ASSERT_FALSE(ShutdownRequested());

    // From here the test holds no wallet lock of its own, so the locking thread
    // waits only on what Unlock itself holds - and the join cannot deadlock.

    std::thread locker;
    std::atomic<bool> lockerDone{false};
    bool injected = false;
    boost::signals2::scoped_connection hook = wallet->NotifyStatusChanged.connect(
        [&](CCryptoKeyStore* keystore) {
            if (injected || keystore->IsLocked()) {
                return;
            }
            injected = true;
            locker = std::thread([&] {
                wallet->Lock();
                lockerDone = true;
            });
            for (int i = 0; i < 50 && !lockerDone; i++) {
                MilliSleep(10);
            }
        });

    const bool unlocked = wallet->Unlock(passphrase);
    if (locker.joinable()) {
        locker.join();
    }
    hook.disconnect();

    ASSERT_TRUE(injected);
    EXPECT_TRUE(unlocked);
    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "unlock reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
    // The lock still happens - after the save, once the unlock lets it in.
    EXPECT_TRUE(lockerDone);
    EXPECT_TRUE(wallet->IsLocked());
}

// A rescan cannot run on a locked encrypted wallet: it could save neither its
// restart point nor anything it finds. That is not a disk failure, and must not
// raise the disk failure dialog and shut the node down. A caller that checks the
// lock before taking cs_wallet, such as the GUI's rescan, can have the wallet
// locked in between.
TEST_F(WalletScanTest, RescanOfLockedWalletIsDeclinedWithoutASaveFailure)
{
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(4);
    ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Tip(), true, true, false, true), 0);

    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "locked rescan";
    {
        ScopedWorkingDirectory workingDirectory(pathTemp);
        ASSERT_TRUE(workingDirectory.ok);
        wallet->GenerateNewSeed();
        ASSERT_TRUE(wallet->EncryptWallet(passphrase));
    }
    if (!wallet->IsLocked()) {
        ASSERT_TRUE(wallet->Lock());
    }
    const uint256 savedBefore = SavedBestBlock();
    uiMessages.clear();
    progress.clear();
    ASSERT_FALSE(ShutdownRequested());

    // Ask to lock on finish, as every RPC caller does, so the failure path also
    // locks the already-locked wallet.
    EXPECT_LT(wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, true, false), 0);
    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "locked rescan reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
    EXPECT_TRUE(wallet->IsLocked());
    // Declined before doing anything: nothing was written, and the progress the
    // GUI sees ends on a failed rescan rather than being left open.
    EXPECT_EQ(savedBefore, SavedBestBlock());
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ("Wallet rescan failed", progress.back().title);
    EXPECT_EQ(100, progress.back().percent);
}

// Adding a key to an encrypted wallet needs the master key more than once: to
// encrypt it for memory, then again for disk. A lock landing between those steps
// used to leave the key in memory but never written, so it vanished on restart
// while a retry saw it as already present. CWallet::Lock() waits for cs_wallet,
// which every key-adding operation holds, so a lock requested part way through
// must wait for the key to be fully added. AddKeyPubKey first drops any
// watch-only entry for the key, announcing the change before it encrypts
// anything; lock the wallet from another thread there.
TEST_F(WalletScanTest, LockDuringKeyAddWaitsForTheAddToFinish)
{
    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "key add race";
    CKey key;
    key.MakeNewKey(true);
    const CPubKey pubkey = key.GetPubKey();
    {
        LOCK2(cs_main, wallet->cs_wallet);
        {
            ScopedWorkingDirectory workingDirectory(pathTemp);
            ASSERT_TRUE(workingDirectory.ok);
            wallet->GenerateNewSeed();
            ASSERT_TRUE(wallet->EncryptWallet(passphrase));
        }
        if (wallet->IsLocked()) {
            ASSERT_TRUE(wallet->Unlock(passphrase));
        }
        // The wallet's only watch-only entry, so dropping it announces the change.
        ASSERT_TRUE(wallet->AddWatchOnly(GetScriptForDestination(pubkey.GetID())));
    }

    std::thread locker;
    std::atomic<bool> lockerDone{false};
    bool injected = false;
    boost::signals2::scoped_connection hook = wallet->NotifyWatchonlyChanged.connect(
        [&](bool fHaveWatchOnly) {
            if (injected || fHaveWatchOnly) {
                return;
            }
            injected = true;
            locker = std::thread([&] {
                wallet->Lock();
                lockerDone = true;
            });
            for (int i = 0; i < 50 && !lockerDone; i++) {
                MilliSleep(10);
            }
        });

    bool added = false;
    {
        LOCK2(cs_main, wallet->cs_wallet);
        added = wallet->AddKeyPubKey(key, pubkey);
    }
    // The test holds no wallet lock here, so the join cannot deadlock.
    if (locker.joinable()) {
        locker.join();
    }
    hook.disconnect();

    ASSERT_TRUE(injected);
    EXPECT_TRUE(added);
    EXPECT_TRUE(wallet->HaveKey(pubkey.GetID()));
    // The lock still lands - once the key has been added.
    EXPECT_TRUE(lockerDone);
    EXPECT_TRUE(wallet->IsLocked());
}

// The rescan RPC needs an unlocked wallet and locks it again when it finishes.
// Any RPC sent while it runs waits for cs_main and cs_wallet; when it gets them
// the wallet must already be locked, or that RPC would be served by an unlock
// the user only gave the rescan. Queue dumpprivkey behind a running rescan: it
// must still be waiting while the scan runs, and then be refused.
TEST_F(WalletScanTest, RpcQueuedBehindARescanFindsTheWalletLocked)
{
    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "queued behind rescan";
    CTxDestination destination;
    ASSERT_TRUE(ExtractDestination(receiptScript, destination));
    const std::string address = EncodeDestination(destination);
    {
        LOCK2(cs_main, wallet->cs_wallet);
        AddBlocks(4);
        {
            ScopedWorkingDirectory workingDirectory(pathTemp);
            ASSERT_TRUE(workingDirectory.ok);
            wallet->GenerateNewSeed();
            ASSERT_TRUE(wallet->EncryptWallet(passphrase));
        }
        if (wallet->IsLocked()) {
            ASSERT_TRUE(wallet->Unlock(passphrase));
        }
    }
    // While unlocked the RPC does hand out the key, so a refusal below can only
    // come from the wallet being locked.
    ASSERT_NO_THROW(CallRPC("dumpprivkey " + address));
    uiMessages.clear();

    // From here the test holds no wallet lock of its own, so the queued RPC waits
    // only on what the rescan holds - and the join cannot deadlock.
    std::thread queued;
    std::atomic<bool> queuedDone{false};
    bool injected = false;
    bool queuedWaitedForTheScan = false;
    std::string queuedResult;
    progressHook = [&](const std::string& title, int percent) {
        if (injected || percent != 0 || title != _("Rescanning...")) {
            return;
        }
        injected = true;
        queued = std::thread([&] {
            try {
                CallRPC("dumpprivkey " + address);
                queuedResult = "returned the private key";
            } catch (const std::exception& e) {
                queuedResult = e.what();
            }
            queuedDone = true;
        });
        for (int i = 0; i < 50 && !queuedDone; i++) {
            MilliSleep(10);
        }
        queuedWaitedForTheScan = !queuedDone;
    };

    EXPECT_NO_THROW(CallRPC("rescan"));
    if (queued.joinable()) {
        queued.join();
    }
    progressHook = nullptr;

    ASSERT_TRUE(injected);
    EXPECT_TRUE(queuedWaitedForTheScan);
    EXPECT_TRUE(queuedDone);
    EXPECT_NE(queuedResult.find("walletpassphrase"), std::string::npos) << queuedResult;
    EXPECT_TRUE(wallet->IsLocked());
    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "rescan reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
}

// In cold storage mode no rescan runs, but a caller that asked for the wallet to
// be locked when the rescan is over (the GUI's Rescan does) must still get it
// locked; otherwise the unlock given for the rescan lasts until the
// walletpassphrase timeout. A caller that did not ask keeps the wallet unlocked.
TEST_F(WalletScanTest, ColdStorageRescanStillLocksTheWalletWhenAsked)
{
    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "cold storage relock";
    LOCK2(cs_main, wallet->cs_wallet);
    AddBlocks(4);
    {
        ScopedWorkingDirectory workingDirectory(pathTemp);
        ASSERT_TRUE(workingDirectory.ok);
        wallet->GenerateNewSeed();
        ASSERT_TRUE(wallet->EncryptWallet(passphrase));
    }
    if (wallet->IsLocked()) {
        ASSERT_TRUE(wallet->Unlock(passphrase));
    }
    progress.clear();
    nMaxConnections = 0;

    EXPECT_EQ(0, wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, false, true));
    EXPECT_FALSE(wallet->IsLocked());

    EXPECT_EQ(0, wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, true, true));
    EXPECT_TRUE(wallet->IsLocked());
    // Nothing was scanned.
    EXPECT_TRUE(progress.empty());
    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "rescan reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
}

// Cold storage (offline) mode loads no blockchain, so the key RPCs must work
// without a chain tip. Importing a key there used to be refused (or crashed the
// node, for importwallet), backing up the wallet crashed the node, and rescan
// reported success without scanning anything. Imports now skip the rescan,
// backups say no chain was loaded, and rescan is refused.
TEST_F(WalletScanTest, OfflineModeKeyRpcsWorkWithoutABlockchain)
{
    struct RestoreExportDir {
        bool hadExportDir = mapArgs.count("-exportdir") > 0;
        std::string exportDir = hadExportDir ? mapArgs["-exportdir"] : std::string();
        ~RestoreExportDir()
        {
            if (hadExportDir) {
                mapArgs["-exportdir"] = exportDir;
            } else {
                mapArgs.erase("-exportdir");
            }
        }
    } restoreExportDir;
    ScopedOfflineMode offline;
    ASSERT_EQ(-1, chainActive.Height());
    mapArgs["-exportdir"] = pathTemp.string();

    try {
        CallRPC("rescan");
        ADD_FAILURE() << "rescan succeeded with no blockchain loaded";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("offline"), std::string::npos) << e.what();
    }

    // Transparent and shielded keys import with the default rescan, and with an
    // explicit rescan height that only an online node could honor.
    CKey transparentKey;
    transparentKey.MakeNewKey(true);
    EXPECT_NO_THROW(CallRPC("importprivkey " + EncodeSecret(transparentKey)));
    CKey transparentKeyWithHeight;
    transparentKeyWithHeight.MakeNewKey(true);
    EXPECT_NO_THROW(CallRPC("importprivkey " + EncodeSecret(transparentKeyWithHeight) + " \"\" true 5"));
    CKey watchedKey;
    watchedKey.MakeNewKey(true);
    EXPECT_NO_THROW(CallRPC("importaddress " + EncodeDestination(watchedKey.GetPubKey().GetID())));
    auto spendingKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    EXPECT_NO_THROW(CallRPC("z_importkey " + EncodeSpendingKey(spendingKey)));
    auto spendingKeyWithHeight = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    EXPECT_NO_THROW(CallRPC("z_importkey " + EncodeSpendingKey(spendingKeyWithHeight) + " yes 10"));
    auto viewingKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false).ToXFVK();
    libzcash::SaplingIncomingViewingKey viewingKeyIvk;
    ASSERT_TRUE(viewingKey.fvk.DeriveIVK(&viewingKeyIvk));
    EXPECT_NO_THROW(CallRPC("z_importviewingkey " + EncodeViewingKey(viewingKey) + " yes 10"));
    // A negative height is still invalid.
    auto rejectedKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    EXPECT_THROW(CallRPC("z_importkey " + EncodeSpendingKey(rejectedKey) + " yes -1"), std::runtime_error);
    {
        LOCK(wallet->cs_wallet);
        EXPECT_TRUE(wallet->HaveKey(transparentKey.GetPubKey().GetID()));
        EXPECT_TRUE(wallet->HaveKey(transparentKeyWithHeight.GetPubKey().GetID()));
        EXPECT_TRUE(wallet->HaveWatchOnly(GetScriptForDestination(watchedKey.GetPubKey().GetID())));
        EXPECT_TRUE(wallet->HaveSaplingSpendingKey(spendingKey.ToXFVK()));
        EXPECT_TRUE(wallet->HaveSaplingSpendingKey(spendingKeyWithHeight.ToXFVK()));
        EXPECT_TRUE(wallet->HaveSaplingFullViewingKey(viewingKeyIvk));
        EXPECT_FALSE(wallet->HaveSaplingSpendingKey(rejectedKey.ToXFVK()));
    }

    // The backup includes every key, even the fixture's key that has no creation
    // time and so would otherwise be dated from a block.
    CTxDestination receiptDestination;
    ASSERT_TRUE(ExtractDestination(receiptScript, receiptDestination));
    CKey receiptKey;
    {
        LOCK(wallet->cs_wallet);
        ASSERT_TRUE(wallet->GetKey(std::get<CKeyID>(receiptDestination), receiptKey));
    }
    EXPECT_NO_THROW(CallRPC("z_exportwallet offlinebackup"));
    std::string backup;
    {
        std::ifstream in((pathTemp / "offlinebackup").string());
        backup.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(backup.find("offline mode"), std::string::npos);
    EXPECT_NE(backup.find(EncodeSecret(receiptKey)), std::string::npos);
    EXPECT_NE(backup.find(EncodeSecret(transparentKey)), std::string::npos);
    EXPECT_NE(backup.find(EncodeSpendingKey(spendingKey)), std::string::npos);

    // Restoring a backup works offline too.
    CKey restoredTransparentKey;
    restoredTransparentKey.MakeNewKey(true);
    auto restoredSpendingKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    const boost::filesystem::path restorePath = pathTemp / "offlinerestore.txt";
    {
        std::ofstream out(restorePath.string());
        out << "# Wallet dump\n\n"
            << EncodeSecret(restoredTransparentKey) << " 2016-08-12T21:55:36Z label=restored\n"
            << EncodeSpendingKey(restoredSpendingKey) << " 2016-08-12T21:55:36Z # zaddr= "
            << EncodePaymentAddress(restoredSpendingKey.DefaultAddress()) << " label= restored\n"
            << "\n# End of dump\n";
    }
    EXPECT_NO_THROW(CallRPC("z_importwallet " + restorePath.string()));
    {
        LOCK(wallet->cs_wallet);
        EXPECT_TRUE(wallet->HaveKey(restoredTransparentKey.GetPubKey().GetID()));
        EXPECT_TRUE(wallet->HaveSaplingSpendingKey(restoredSpendingKey.ToXFVK()));
    }

    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
}

// A wallet backup gives shielded keys times that do not say when they started
// receiving (a spending key carries the time it was derived, which for a wallet
// restored from its seed is the time of the restore). Restoring a backup that
// adds a shielded key must therefore rescan from the start of the chain: it used
// to start about two hours before the tip whenever the backup held no
// transparent keys, missing the key's older notes. A shielded key the wallet
// already has does not need that.
TEST_F(WalletScanTest, ImportWalletRescansFromTheStartForANewShieldedKey)
{
    {
        LOCK2(cs_main, wallet->cs_wallet);
        // An hour apart, so the import's two-hour margin before the tip does not
        // reach back to the start on its own.
        AddBlocks(10, 3600);
    }
    auto spendingKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    const boost::filesystem::path dumpPath = pathTemp / "shieldedonly.txt";
    {
        std::ofstream out(dumpPath.string());
        out << "# Wallet dump\n\n"
            << EncodeSpendingKey(spendingKey) << " 2016-08-12T21:55:36Z # zaddr= "
            << EncodePaymentAddress(spendingKey.DefaultAddress()) << " label= shielded\n"
            << "\n# End of dump\n";
    }
    const std::string scanTitle = "Scanning wallet transactions and witnesses: block ";
    auto firstScannedBlock = [&]() -> std::string {
        for (const auto& item : progress) {
            if (item.title.compare(0, scanTitle.size(), scanTitle) == 0) {
                return item.title.substr(scanTitle.size());
            }
        }
        return "no scan";
    };

    progress.clear();
    EXPECT_NO_THROW(CallRPC("z_importwallet " + dumpPath.string()));
    {
        LOCK(wallet->cs_wallet);
        EXPECT_TRUE(wallet->HaveSaplingSpendingKey(spendingKey.ToXFVK()));
    }
    EXPECT_EQ("0 of 10...", firstScannedBlock());

    // The same backup again adds nothing new, so the rescan keeps the margin.
    progress.clear();
    EXPECT_NO_THROW(CallRPC("z_importwallet " + dumpPath.string()));
    EXPECT_EQ("8 of 10...", firstScannedBlock());

    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "import reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
}

// The decoder accepts any bytes for an Ironwood diversified viewing key, so a
// backup line can hold one that cannot derive its keys. Adding it used to throw
// out of the import part way through: the keys before it stayed in the wallet,
// nothing was rescanned, and the progress dialog was left open. It must now fail
// like any other key the import cannot add, and the import carry on.
TEST_F(WalletScanTest, ImportWalletCarriesOnPastAnUnusableIronwoodViewingKey)
{
    {
        LOCK2(cs_main, wallet->cs_wallet);
        AddBlocks(4);
    }
    auto ironwoodKey = libzcash::IronwoodExtendedSpendingKeyPirate::Master(HDSeed::Random(), false);
    auto ironwoodViewingKey = ironwoodKey.GetXFVK();
    ASSERT_TRUE(ironwoodViewingKey.has_value());
    libzcash::IronwoodDiversifiedExtendedFullViewingKeyPirate unusable;
    unusable.extfvk = ironwoodViewingKey.value();
    unusable.d.fill(0);
    unusable.extfvk.fvk.ak = uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    libzcash::IronwoodIncomingViewingKey ivk;
    ASSERT_FALSE(unusable.extfvk.fvk.DeriveIVK(&ivk));

    auto spendingKey = libzcash::SaplingExtendedSpendingKey::Master(HDSeed::Random(), false);
    const boost::filesystem::path dumpPath = pathTemp / "unusablekey.txt";
    {
        std::ofstream out(dumpPath.string());
        out << "# Wallet dump\n\n"
            << EncodeDiversifiedViewingKey(unusable) << " 2016-08-12T21:55:36Z # zaddr= unusable label= unusable\n"
            << EncodeSpendingKey(spendingKey) << " 2016-08-12T21:55:36Z # zaddr= "
            << EncodePaymentAddress(spendingKey.DefaultAddress()) << " label= after\n"
            << "\n# End of dump\n";
    }

    // The import's own progress dialog is driven by the wallet's signal.
    bool importDialogClosed = false;
    boost::signals2::scoped_connection importProgress = wallet->ShowProgress.connect(
        [&](const std::string& title, int percent) {
            if (title.empty() && percent == 100) importDialogClosed = true;
        });

    progress.clear();
    try {
        CallRPC("z_importwallet " + dumpPath.string());
        ADD_FAILURE() << "the unusable key was reported as imported";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("Error adding some keys"), std::string::npos) << e.what();
    }
    {
        LOCK(wallet->cs_wallet);
        EXPECT_TRUE(wallet->HaveSaplingSpendingKey(spendingKey.ToXFVK()));
    }
    importProgress.disconnect();
    // The import closed its progress dialog and went on to rescan.
    EXPECT_TRUE(importDialogClosed);
    EXPECT_TRUE(std::any_of(progress.begin(), progress.end(), [](const Progress& item) {
        return item.title == "Wallet rescan complete" && item.percent == 100;
    }));
    for (const auto& item : uiMessages) {
        ADD_FAILURE() << "import reported a failure: " << item.caption << ": " << item.message;
    }
    EXPECT_FALSE(ShutdownRequested());
}

// Offline (cold storage) mode loads no blockchain or coins database. Each of
// these commands reports on the chain or needs it to act, and all of them used
// to crash the node offline (most by dereferencing the missing chain tip or
// coins database). Offline they must be refused with a clear error, while their
// help text still answers.
TEST_F(WalletScanTest, ChainRpcsAreRefusedInOfflineModeButKeepTheirHelp)
{
    const std::vector<std::string> chainRpcs = {
        "getbestblockhash", "getblockchaininfo", "getchaintips", "getchaintxstats", "gettxoutsetinfo",
        "gettxout", "geterablockheights", "getrawtransaction", "gettxoutproof", "sendrawtransaction",
        "z_createbuildinstructions", "z_createbuildinstructionscoincontrol", "consolidationstatus",
        "sweepstatus", "sendtoaddress", "zs_listtransactions", "zs_listspentbyaddress",
        "zs_listreceivedbyaddress", "zs_listsentbyaddress", "getalldata", "listunspent", "rescan",
        "kvsearch", "minerids", "notaries", "kvupdate", "submitblock", "createrawtransaction",
        "fundrawtransaction", "signrawtransaction"};
    const std::string offlineError = "is not available in offline (cold storage) mode: no blockchain is loaded";

    ScopedOfflineMode offline;
    for (const auto& method : chainRpcs) {
        try {
            CallRPC(method);
            ADD_FAILURE() << method << " answered in offline mode";
        } catch (const std::runtime_error& e) {
            EXPECT_NE(std::string(e.what()).find(method + " " + offlineError), std::string::npos)
                << method << ": " << e.what();
        }

        const CRPCCommand* command = tableRPC[method];
        ASSERT_NE(nullptr, command) << method;
        try {
            command->actor(UniValue(UniValue::VARR), true, CPubKey());
            ADD_FAILURE() << method << " gave no help";
        } catch (const std::runtime_error& e) {
            EXPECT_EQ(std::string(e.what()).find(offlineError), std::string::npos) << method << ": " << e.what();
            EXPECT_NE(std::string(e.what()).find(method), std::string::npos) << method << ": " << e.what();
        }
    }
}

// No command may answer a help request with an RPC error: "help" lists every command
// by calling it for its help text (a std::exception), and an RPC error used to abort
// the whole list. (A few legacy commands give no help text at all; "help" skips those.)
static void ExpectNoCommandThrowsAnRpcErrorForHelp(const std::string& mode)
{
    for (const std::string& method : tableRPC.listCommands()) {
        const CRPCCommand* command = tableRPC[method];
        ASSERT_NE(nullptr, command) << method;
        try {
            command->actor(UniValue(UniValue::VARR), true, CPubKey());
        } catch (const std::exception& e) {
        } catch (const UniValue& objError) {
            ADD_FAILURE() << method << " threw an RPC error instead of its help " << mode << ": "
                          << find_value(objError, "message").getValStr();
        }
    }
}

// Address creation is not gated on chain height. Offline (no blockchain, so not even
// Sapling counts as active) both address RPCs give an Ironwood address by default
// and either type on request, and "help" lists every command; the height check
// made both address RPCs, and with them the whole "help" list, fail offline.
TEST_F(WalletScanTest, AddressCreationAndHelpWorkInOfflineMode)
{
    ExpectNoCommandThrowsAnRpcErrorForHelp("online");
    {
        LOCK(wallet->cs_wallet);
        if (!wallet->HaveHDSeed()) {
            wallet->GenerateNewSeed();
        }
    }

    ScopedOfflineMode offline;
    ASSERT_FALSE(NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_SAPLING));
    ExpectNoCommandThrowsAnRpcErrorForHelp("offline");

    for (const std::string method : {"z_getnewaddress", "z_getnewaddresskey"}) {
        for (const std::string args : {"", " ironwood", " sapling"}) {
            UniValue result;
            ASSERT_NO_THROW(result = CallRPC(method + args)) << method + args;
            auto address = DecodePaymentAddress(result.get_str());
            if (args == " sapling") {
                EXPECT_NE(nullptr, std::get_if<libzcash::SaplingPaymentAddress>(&address)) << method + args;
            } else {
                EXPECT_NE(nullptr, std::get_if<libzcash::IronwoodPaymentAddress>(&address)) << method + args;
            }
            LOCK(wallet->cs_wallet);
            EXPECT_EQ(1u, wallet->mapZAddressBook.count(address)) << method + args;
            EXPECT_TRUE(std::visit(GetSpendingKeyForPaymentAddress(wallet), address).has_value()) << method + args;
        }
        EXPECT_THROW(CallRPC(method + " sprout"), std::runtime_error) << method;
    }

    std::string help = CallRPC("help").get_str();
    EXPECT_NE(help.find("z_getnewaddress ( type legacy )"), std::string::npos);
    EXPECT_NE(help.find("z_getnewaddresskey ( type legacy )"), std::string::npos);
    EXPECT_NE(help.find("getwalletinfo"), std::string::npos);
    EXPECT_EQ(help.find("help unavailable"), std::string::npos) << help;
}

// A command that throws an RPC error before reaching its help text is listed by
// name with that error; "help" itself still answers, and lists the other commands.
// Uses its own table, so the global one is left as it was.
static UniValue rpcerrorbeforehelp(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    throw JSONRPCError(RPC_INVALID_REQUEST, "not ready");
}
static const CRPCCommand rpcErrorBeforeHelpCommand =
    { "control", "rpcerrorbeforehelp", &rpcerrorbeforehelp, true };

TEST_F(WalletScanTest, HelpSurvivesACommandThatThrowsAnRpcError)
{
    CRPCTable table;
    ASSERT_TRUE(table.appendCommand("rpcerrorbeforehelp", &rpcErrorBeforeHelpCommand));

    std::string help;
    ASSERT_NO_THROW(help = table.help("rpcerrorbeforehelp"));
    EXPECT_EQ("rpcerrorbeforehelp (help unavailable: not ready)", help);
    ASSERT_NO_THROW(help = table.help(""));
    EXPECT_NE(help.find("\nrpcerrorbeforehelp (help unavailable: not ready)\n"), std::string::npos) << help;
    EXPECT_NE(help.find("\nstop\n"), std::string::npos) << help;
}

// An offline signer reviews a transaction with decoderawtransaction, which used to
// crash the node offline on any transaction with a transparent input.
TEST_F(WalletScanTest, DecodeRawTransactionWorksInOfflineMode)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 10);
    mtx.vin.emplace_back(COutPoint(GetRandHash(), 0));
    mtx.vout.emplace_back(1 * COIN, receiptScript);
    const CTransaction tx(mtx);

    ScopedOfflineMode offline;
    UniValue decoded;
    ASSERT_NO_THROW(decoded = CallRPC("decoderawtransaction " + EncodeHexTx(tx)));
    EXPECT_EQ(tx.GetHash().GetHex(), find_value(decoded.get_obj(), "txid").get_str());
    EXPECT_EQ(1u, find_value(decoded.get_obj(), "vin").size());
}

// An offline signer can look at its wallet's transactions; zs_gettransaction
// placed an unconfirmed one (every one, offline) above the missing chain tip.
TEST_F(WalletScanTest, ZsGetTransactionWorksInOfflineMode)
{
    {
        LOCK2(cs_main, wallet->cs_wallet);
        AddBlocks(4);
        ASSERT_GE(wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, false, true), 0);
    }
    ASSERT_FALSE(receiptTxids.empty());
    const std::string txid = receiptTxids.front().GetHex();
    {
        LOCK(wallet->cs_wallet);
        ASSERT_TRUE(wallet->mapWallet.count(receiptTxids.front()));
    }

    ScopedOfflineMode offline;
    UniValue result;
    ASSERT_NO_THROW(result = CallRPC("zs_gettransaction " + txid));
    EXPECT_EQ(txid, find_value(result.get_obj(), "txid").get_str());
}

