// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "asyncrpcoperation.h"
#include "init.h"
#include "net.h"
#include "util.h"
#include "validationinterface.h"
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"

#include <boost/filesystem.hpp>

#include <fstream>
#include <iterator>
#include <vector>

// CWalletManager is a process-wide singleton, so every test must leave it
// exactly as it found it (empty) -- otherwise state leaks into unrelated
// tests elsewhere in the same gtest binary.
class WalletManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        SelectParams(CBaseChainParams::TESTNET);
        pathTemp = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
        boost::filesystem::create_directories(pathTemp);
        ClearDatadirCache();
        fHadPreviousDatadir = mapArgs.count("-datadir") != 0;
        if (fHadPreviousDatadir)
            previousDatadir = mapArgs["-datadir"];
        mapArgs["-datadir"] = pathTemp.string();
        // CDBEnv::Open() binds to the first datadir it's ever given and ignores the
        // path on every later call (see db.cpp) -- without a fresh CDBEnv per test,
        // every CWallet in this file after the first would silently keep reading and
        // writing under some earlier test's directory instead of pathTemp, and the
        // exists()-on-GetDataDir() checks below would disagree with reality.
        // Saved so TearDown() can put the previous one back rather than leaving
        // this test's env installed as global state for whatever runs next.
        previousBitdb = bitdb;
        bitdb = std::shared_ptr<CDBEnv>(new CDBEnv{});
    }

    void TearDown() override {
        CWalletManager::Get().FlushAndUnloadAllExceptActiveWallet();
        CWallet* defaultWallet = CWalletManager::Get().GetWallet(CWalletManager::Get().GetActiveWalletName());
        CWalletManager::Get().Reset();
        // Every other test in this fixture leaves a RegisterInitialWallet()-
        // registered wallet active -- that path never calls
        // RegisterValidationInterface() at all (production's equivalent,
        // init.cpp, does so separately, itself), so deleting it directly was
        // always safe. The no-default-wallet redesign's own tests instead
        // exercise LoadWallet()/CreateWallet() ending up as the sole/active
        // wallet -- that path DOES register with the validation interface,
        // and FlushAndUnloadAllExceptActiveWallet() deliberately skips
        // unregistering the active entry (see its own UnloadWallet()-mirrors
        // comment). Without this, the next test anywhere in this binary that
        // connects a block dispatches ChainTip()/SyncTransaction() to this
        // already-deleted CWallet* -- a real, exercised use-after-free, not a
        // hypothetical one (this is what test_block.cpp's TestStopAt
        // otherwise segfaults on when run after this fixture). Harmless
        // no-op for the RegisterInitialWallet() case, which was never
        // connected to begin with.
        if (defaultWallet)
            UnregisterValidationInterface(defaultWallet);
        delete defaultWallet;
        // Individual wallet DB handles are released above; now close the env
        // itself (see gtestutils.cpp's BitcoinTestingSetup::TearDown -- the
        // SetUp()'s fresh CDBEnv is otherwise never closed, leaking a full
        // BerkeleyDB environment per test for the rest of the process).
        bitdb->Flush(true);
        bitdb->Reset();
        // Restore the previous global bitdb/-datadir (gtestutils.cpp's own
        // BitcoinTestingSetup does the same, with the same comment): leaving
        // this test's env and temp datadir installed as global state would
        // mean any later test in the binary that touches bitdb without
        // resetting it itself -- there's no guarantee every one does --
        // silently operates against this test's already-deleted temp dir,
        // or worse, an operator's real datadir if there never was a previous
        // one.
        bitdb = previousBitdb;
        ClearDatadirCache();
        if (fHadPreviousDatadir)
            mapArgs["-datadir"] = previousDatadir;
        else
            mapArgs.erase("-datadir");
        boost::filesystem::remove_all(pathTemp);
    }

    // Creates a real on-disk wallet file (mirrors what a running node's
    // -wallet= startup or the loadwallet RPC would already have produced)
    // so CWalletManager::LoadWallet's file-must-exist check has something
    // real to find.
    void CreateWalletFileOnDisk(const std::string& name) {
        bool fFirstRun;
        CWallet scratch(name);
        ASSERT_EQ(DB_LOAD_OK, scratch.LoadWallet(fFirstRun));
    }

    boost::filesystem::path pathTemp;
    std::shared_ptr<CDBEnv> previousBitdb;
    bool fHadPreviousDatadir;
    std::string previousDatadir;
};

TEST_F(WalletManagerTest, GetNameReturnsTheWalletsOwnFileName)
{
    // Regression test: CWallet::GetName() was a hardcoded "dummy" stub until
    // Phase 7 of the multiwallet effort (WalletModel::getWalletName(), used
    // to attribute the passphrase-unlock dialog to a specific wallet) became
    // its first real caller and the audit caught the stub still in place.
    CWallet wallet("attributed_test.dat");
    EXPECT_EQ("attributed_test.dat", wallet.GetName());
}

TEST_F(WalletManagerTest, RegisterInitialWalletIsListedAndBecomesActive)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", defaultWallet);

    EXPECT_EQ("default_test.dat", CWalletManager::Get().GetActiveWalletName());
    EXPECT_TRUE(CWalletManager::Get().IsActiveWallet("default_test.dat"));
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));

    std::vector<std::string> names = CWalletManager::Get().ListWalletNames();
    ASSERT_EQ(1u, names.size());
    EXPECT_EQ("default_test.dat", names[0]);
}

TEST_F(WalletManagerTest, LoadUnloadListHappyPath)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    EXPECT_FALSE(CWalletManager::Get().IsActiveWallet("secondtestwallet"));
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));

    std::vector<std::string> names = CWalletManager::Get().ListWalletNames();
    EXPECT_EQ(2u, names.size());

    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));
    names = CWalletManager::Get().ListWalletNames();
    EXPECT_EQ(1u, names.size());

    // Unloading again must fail cleanly, not double-free.
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("not found"));
}

TEST_F(WalletManagerTest, LoadWalletRejectsFileThatDoesNotExist)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("nonexistenttestwallet", strError));
    EXPECT_FALSE(strError.empty());
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("nonexistenttestwallet"));
}

TEST_F(WalletManagerTest, LoadWalletRejectsDuplicateName)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    std::string strError2;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("secondtestwallet", strError2));
    EXPECT_NE(std::string::npos, strError2.find("already loaded"));

    // The registry must be untouched by the rejected reload -- still exactly
    // one secondary entry, still pointing at the original CWallet.
    std::vector<std::string> names = CWalletManager::Get().ListWalletNames();
    EXPECT_EQ(2u, names.size());
}

TEST_F(WalletManagerTest, InvalidWalletNamesAreRejectedStructurally)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    const std::vector<std::string> badNames = {
        "",
        "../../etc/passwd",
        "/etc/passwd",
        "..",
        ".",
        "sub/dir",
        "name with spaces",
        "..\\..\\windows_style_traversal",
        std::string(129, 'a'), // one past the length cap
    };

    for (const std::string& bad : badNames) {
        std::string strError;
        EXPECT_FALSE(CWalletManager::Get().LoadWallet(bad, strError)) << "name should have been rejected: " << bad;
        EXPECT_FALSE(strError.empty());
        // Structural rejection: nothing was registered under this name.
        EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet(bad));
    }
}

TEST_F(WalletManagerTest, ConventionalDotWalletNamesAreAccepted)
{
    // "wallet.dat" -- the default wallet's own on-disk name -- and the plan's
    // own worked example ("second.dat") must both be loadable: a charset that
    // rejects '.' would make the default wallet itself unaddressable via
    // /wallet/<name>/ and reject every conventionally-named secondary wallet.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("second.dat");

    std::string strError;
    EXPECT_TRUE(CWalletManager::Get().LoadWallet("second.dat", strError)) << strError;
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("second.dat"));
}

TEST_F(WalletManagerTest, LoadWalletRunsVerifyOnAFileNeverTouchedInThisProcess)
{
    // Every other LoadWallet test in this file goes through CreateWalletFileOnDisk,
    // which itself opens a scratch CWallet against the target name -- CDB::Close()
    // only ever decrements mapFileUseCount, never erases the entry, so that name
    // is left "already touched" and every one of those tests takes LoadWallet's
    // skip-Verify branch. Copy the bytes onto a brand new name instead, so this
    // one actually exercises CWallet::Verify() itself succeeding.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("sourcewallet");

    boost::system::error_code ec;
    boost::filesystem::copy_file(GetDataDir() / "sourcewallet", GetDataDir() / "neveropenedwallet", ec);
    ASSERT_FALSE(ec) << ec.message();

    std::string strError;
    EXPECT_TRUE(CWalletManager::Get().LoadWallet("neveropenedwallet", strError)) << strError;
}

TEST_F(WalletManagerTest, LoadWalletFailsOnUnsalvageableCorruptionInsteadOfSkippingVerify)
{
    // The test above proves LoadWallet succeeds when CWallet::Verify() runs
    // on a good file, but that alone doesn't prove Verify() actually ran --
    // the skip-Verify branch would produce the same success. This one uses a
    // file so thoroughly corrupted that CWalletDB::Recover can't salvage
    // anything from it, which only CWallet::Verify() (wallet.cpp) itself
    // detects and reports ("... corrupt, salvage failed"); LoadWallet's own
    // plain CWallet::LoadWallet() open (the skip-Verify path) fails on a
    // bad file too, but with different wording. If Verify() were skipped
    // here, this assertion on the message would fail even though LoadWallet
    // still correctly returns false either way.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("sourcewallet");

    boost::filesystem::path sourcePath = GetDataDir() / "sourcewallet";
    boost::filesystem::path corruptPath = GetDataDir() / "corruptedwallet";
    {
        std::ifstream src(sourcePath.string(), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(src)), std::istreambuf_iterator<char>());
        ASSERT_FALSE(bytes.empty());
        std::fill(bytes.begin(), bytes.end(), '\0');
        std::ofstream dst(corruptPath.string(), std::ios::binary);
        dst.write(bytes.data(), (std::streamsize)bytes.size());
    }

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("corruptedwallet", strError));
    EXPECT_TRUE(strError.find("corrupt") != std::string::npos || strError.find("salvage") != std::string::npos)
        << "actual message: " << strError;
}

TEST_F(WalletManagerTest, LoadWalletRejectsAFileAlreadyLoadedUnderADifferentName)
{
    // boost::filesystem::equivalent() is what actually closes this: two
    // different, both individually-valid names that resolve to the same
    // on-disk file -- a symlink here as a portable stand-in for what a
    // case-insensitive filesystem's "Wallet.dat" vs "wallet.dat" (or
    // Windows' trailing-dot aliasing) would do -- must not both be
    // loadable, or the second load silently duplicates the first wallet's
    // live CWallet in memory under a name the registry otherwise treats as
    // a distinct, independently-unloadable entry.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    // GetDataDir() appends a per-network subdirectory on top of "-datadir"
    // (e.g. "testnet3/"), so the symlink has to live there too, not directly
    // under pathTemp, or it lands somewhere LoadWallet() never looks.
    boost::system::error_code ec;
    boost::filesystem::create_symlink(GetDataDir() / "secondtestwallet", GetDataDir() / "secondtestwalletalias", ec);
    ASSERT_FALSE(ec) << ec.message();

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("secondtestwalletalias", strError)) << strError;
    EXPECT_NE(std::string::npos, strError.find("already loaded")) << "actual message: " << strError;
}

TEST_F(WalletManagerTest, UnloadRefusesTheActiveWallet)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("default_test.dat", strError));
    EXPECT_FALSE(strError.empty());
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("default_test.dat"));
}

TEST_F(WalletManagerTest, UnloadRefusesUnknownWallet)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("neverloadedwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("not found"));
}

TEST_F(WalletManagerTest, UnloadRefusesWhileRefcountHeldThenSucceedsAfterRelease)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().AddRef("secondtestwallet"));

    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("in use"));
    // Refused unload must not have touched the entry.
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));

    CWalletManager::Get().ReleaseRef("secondtestwallet");
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));
}

TEST_F(WalletManagerTest, ResolveAndHoldForRequestIsAtomicWithLookupAndTracksOutcome)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    auto notFound = CWalletManager::Get().ResolveAndHoldForRequest("neverloadedwallet");
    EXPECT_EQ(CWalletManager::ResolveOutcome::NotFound, notFound.outcome);

    // No-default-wallet redesign: "default_test.dat" is just the first wallet
    // registered (via RegisterInitialWallet() above), an ordinary registry
    // entry like any other -- resolving it now takes a real ref like
    // everything else does (there is no more an exempt "IsDefault" outcome).
    auto activeWalletHeld = CWalletManager::Get().ResolveAndHoldForRequest("default_test.dat");
    EXPECT_EQ(CWalletManager::ResolveOutcome::Held, activeWalletHeld.outcome);
    // Unloading it is still refused, but now specifically because it's the
    // *active* wallet (see UnloadWallet()'s own check), not because of the
    // ref just taken above -- release that ref first to confirm this.
    CWalletManager::Get().ReleaseRefIfCurrent("default_test.dat", activeWalletHeld.generation);
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("default_test.dat", strError));
    EXPECT_NE(std::string::npos, strError.find("active"));

    auto held = CWalletManager::Get().ResolveAndHoldForRequest("secondtestwallet");
    EXPECT_EQ(CWalletManager::ResolveOutcome::Held, held.outcome);
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("in use"));

    CWalletManager::Get().ReleaseRefIfCurrent("secondtestwallet", held.generation);
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
}

TEST_F(WalletManagerTest, ReleaseAfterUnloadAndReloadUnderSameNameDoesNotUnderflowTheNewEntry)
{
    // Reproduces the exact hazard a name-only release can't guard against:
    // a ref taken against one loaded instance of "secondtestwallet" must
    // never affect a *different* instance later loaded under the same name,
    // or the new entry's refcount underflows and it can never be unloaded
    // again for the life of the process.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    auto firstLoad = CWalletManager::Get().ResolveAndHoldForRequest("secondtestwallet");
    ASSERT_EQ(CWalletManager::ResolveOutcome::Held, firstLoad.outcome);

    // Release normally, unload, and load a fresh instance under the same name.
    CWalletManager::Get().ReleaseRefIfCurrent("secondtestwallet", firstLoad.generation);
    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    // A late release still carrying the OLD generation must be a no-op
    // against the new entry -- without the generation check, this would
    // decrement the new entry's fresh refcount to -1 and UnloadWallet's
    // `refcount != 0` check would refuse it forever.
    CWalletManager::Get().ReleaseRefIfCurrent("secondtestwallet", firstLoad.generation);
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
}

TEST_F(WalletManagerTest, UnloadingASecondaryWalletDoesNotTearDownTheSharedEnvironment)
{
    // UnloadWallet() must close only its own file within the shared BerkeleyDB
    // environment (CWallet::Flush(true)/CDBEnv::Flush(true) is the *process
    // shutdown* path and would close+remove the whole environment instead).
    // Proof: after unloading one secondary, both the default wallet and a
    // brand-new secondary must still be fully loadable -- if the environment
    // itself had been torn down, either loading a fresh wallet below or the
    // default wallet's own on-disk data would already be broken.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;

    // The default wallet is still registered and reachable.
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("default_test.dat"));

    // A fresh wallet can still be loaded through the same shared environment --
    // this would throw ("Failed to open database environment") if the earlier
    // unload had closed/removed it instead of just its own file.
    CreateWalletFileOnDisk("thirdtestwallet");
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("thirdtestwallet", strError)) << strError;
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("thirdtestwallet"));
}

TEST_F(WalletManagerTest, FlushAndUnloadAllExceptActiveWalletLeavesActiveWalletAloneAndIsIdempotent)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", defaultWallet);
    CreateWalletFileOnDisk("secondtestwallet");
    CreateWalletFileOnDisk("thirdtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("thirdtestwallet", strError)) << strError;

    CWalletManager::Get().FlushAndUnloadAllExceptActiveWallet();
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("thirdtestwallet"));
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));

    // Calling it again with nothing left to unload must be a harmless no-op.
    CWalletManager::Get().FlushAndUnloadAllExceptActiveWallet();
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));
}

TEST_F(WalletManagerTest, CheckpointAllWalletsWritesToEveryLoadedWallet)
{
    // Regression test for Phase 11 of the multiwallet effort: StartShutdown()
    // (init.cpp) used to write its best-chain checkpoint straight to
    // pwalletMain only, so a secondary wallet's on-disk record could be many
    // blocks stale by however long it had been since its own last periodic
    // flush. CheckpointAllWallets() generalizes that write to every currently
    // loaded wallet.
    // Unlike most other tests in this file, this one actually needs the
    // default wallet's file to exist on disk (SetBestChain()'s own
    // CWalletDB open uses mode "r+", which doesn't auto-create), since it's
    // exercising a real write-then-read-back round trip, not just registry
    // bookkeeping.
    CreateWalletFileOnDisk("default_test.dat");
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    // CWallet::SetBestChain() is a no-op (see wallet.cpp) unless
    // nMaxConnections > 0; pin it explicitly rather than relying on
    // net.cpp's default value happening to still be in place (test_init.cpp
    // mutates this same global, restoring it in TearDown -- if this test ran
    // interleaved with a bug in that restore, relying on the ambient default
    // here would turn into a confusing ReadBestBlock() failure instead of an
    // obviously-related one).
    int savedMaxConnections = nMaxConnections;
    nMaxConnections = 8;

    CBlockLocator locator(std::vector<uint256>{uint256S(std::string(63, '0') + "1")});
    // height is deliberately not asserted on below: SetBestChainINTERNAL()
    // (wallet.h) takes a height parameter but never actually persists it
    // anywhere -- pre-existing, unrelated to Phase 11 -- so there is nothing
    // on disk to read back for it. The locator round trip below is the only
    // observable effect of CheckpointAllWallets() there is to verify.
    CWalletManager::Get().CheckpointAllWallets(locator, 123);

    nMaxConnections = savedMaxConnections;

    CBlockLocator readLocatorDefault;
    ASSERT_TRUE(CWalletDB("default_test.dat").ReadBestBlock(readLocatorDefault));
    EXPECT_EQ(locator.vHave, readLocatorDefault.vHave);

    CBlockLocator readLocatorSecondary;
    ASSERT_TRUE(CWalletDB("secondtestwallet").ReadBestBlock(readLocatorSecondary));
    EXPECT_EQ(locator.vHave, readLocatorSecondary.vHave);
}

TEST_F(WalletManagerTest, ResetIsIdempotentAndDoesNotDeleteTheActiveWalletObject)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", defaultWallet);

    CWalletManager::Get().Reset();
    EXPECT_TRUE(CWalletManager::Get().ListWalletNames().empty());
    EXPECT_TRUE(CWalletManager::Get().GetActiveWalletName().empty());
    EXPECT_FALSE(CWalletManager::Get().IsActiveWallet("default_test.dat"));

    // Idempotent: calling again on an already-empty registry must not crash.
    CWalletManager::Get().Reset();

    // Reset() must not have freed the CWallet Shutdown() itself still owns.
    delete defaultWallet;
}

TEST_F(WalletManagerTest, RPCWalletRequestGuardActuallyBlocksUnloadWhileAlive)
{
    // The other guard test below exercises the thread-local; this one
    // exercises the actual production path UnloadWallet's "in use" refusal
    // depends on -- RPCWalletRequestGuard's ctor/dtor really do take and
    // release a ref via ResolveAndHoldForRequest/ReleaseRefIfCurrent, not
    // just the raw AddRef/ReleaseRef primitives other tests drive directly.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    {
        RPCWalletRequestGuard guard("secondtestwallet");
        ASSERT_TRUE(guard.IsResolved());
        EXPECT_FALSE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError));
        EXPECT_NE(std::string::npos, strError.find("in use"));
    }
    // Guard destructed at scope exit -> ref released -> now unloadable.
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondtestwallet", strError)) << strError;
}

TEST_F(WalletManagerTest, RPCWalletRequestGuardSetsAndClearsThreadLocal)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());
    {
        RPCWalletRequestGuard guard("secondtestwallet");
        EXPECT_EQ("secondtestwallet", CWalletManager::GetRequestedWalletName());
        EXPECT_TRUE(CWalletManager::WasWalletExplicitlySelected());
    }
    // Cleared on normal scope exit.
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());

    // Cleared even when the guarded work throws -- the same unwinding path
    // tableRPC.execute() takes, which is why this must be RAII and not a
    // manual set/clear pair around the call site.
    EXPECT_THROW({
        RPCWalletRequestGuard guard("secondtestwallet");
        throw std::runtime_error("simulated RPC failure");
    }, std::runtime_error);
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());

    // Back-to-back guards on the same thread must not leak into each other.
    {
        RPCWalletRequestGuard guard1("secondtestwallet");
        EXPECT_EQ("secondtestwallet", CWalletManager::GetRequestedWalletName());
        EXPECT_TRUE(CWalletManager::WasWalletExplicitlySelected());
    }
    {
        // No-default-wallet redesign: an unscoped guard now pins
        // GetRequestedWalletName() to whichever wallet it actually resolved
        // and ref'd (here, "default_test.dat", the active wallet) rather
        // than leaving it empty -- Opus-audit-caught, see the constructor's
        // own comment. WasWalletExplicitlySelected() is what still
        // distinguishes this from a genuinely scoped request.
        RPCWalletRequestGuard guard2("");
        EXPECT_EQ("default_test.dat", CWalletManager::GetRequestedWalletName());
        EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());
    }
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());
}

TEST_F(WalletManagerTest, GetWalletForRequestResolvesActiveAndSecondary)
{
    // GetWalletForRequest() falls back to the real pwalletMain global for the
    // no-selection case, mirroring init.cpp (which registers the very same
    // CWallet* as both pwalletMain and the manager's default). Unlike this
    // fixture's other tests -- which register a default wallet with the
    // manager but never touch the real pwalletMain global, since nothing
    // else in this file calls GetWalletForRequest() -- pwalletMain has to be
    // saved and restored explicitly here rather than left to TearDown().
    // Scope-exit rather than a plain assignment at the end: an ASSERT_*
    // below returning early would otherwise skip the restore and leave
    // pwalletMain pointing at a wallet TearDown() is about to delete,
    // dangling for whatever test in this binary runs next.
    struct PwalletMainRestorer {
        CWallet* saved;
        ~PwalletMainRestorer() { pwalletMain = saved; }
    } restorer{pwalletMain};

    CWallet* defaultWallet = new CWallet("default_test.dat");
    pwalletMain = defaultWallet;
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", defaultWallet);

    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondtestwallet");
    ASSERT_NE(nullptr, secondaryWallet);
    ASSERT_NE(secondaryWallet, defaultWallet);

    EXPECT_EQ(defaultWallet, CWalletManager::GetWalletForRequest());
    {
        RPCWalletRequestGuard guard("secondtestwallet");
        EXPECT_EQ(secondaryWallet, CWalletManager::GetWalletForRequest());
    }
    // Guard destructed -> thread-local cleared -> back to the default.
    EXPECT_EQ(defaultWallet, CWalletManager::GetWalletForRequest());
}

TEST_F(WalletManagerTest, AsyncOperationPinsWalletUnloadableForItsLifetime)
{
    // Phase 3: AsyncRPCOperation(CWallet*) is what z_sendmany/z_shieldcoinbase/
    // z_mergetoaddress/consolidateaddress hand their operations to instead of
    // reading pwalletMain directly (asyncrpcoperation.h/.cpp). Exercised here
    // via the base class directly -- it's concrete (main() has a default
    // body, not pure virtual), so no subclass or funded transaction is
    // needed to test the wallet-pinning mechanism itself in isolation.
    // Registered first (no-default-wallet redesign) so "asyncopwallet" below
    // is a genuine secondary, not the first-loaded-into-empty-registry
    // wallet that would otherwise become active (and therefore itself
    // unloadable-refused for an unrelated reason from the one under test).
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("asyncopwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("asyncopwallet", strError)) << strError;
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("asyncopwallet");
    ASSERT_NE(nullptr, secondaryWallet);

    {
        std::shared_ptr<AsyncRPCOperation> operation = std::make_shared<AsyncRPCOperation>(secondaryWallet);
        EXPECT_EQ("asyncopwallet", operation->getWalletName());

        // The whole point: a wallet an async operation was built against
        // cannot be unloaded while that operation object still exists,
        // exactly like an in-flight HTTP request via RPCWalletRequestGuard.
        EXPECT_FALSE(CWalletManager::Get().UnloadWallet("asyncopwallet", strError));

        // operation's destructor runs at the end of this scope, releasing
        // the ref -- there is no explicit "operation finished" event to
        // wait for (see the constructor's own comment on why release is
        // tied to object lifetime instead).
    }

    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("asyncopwallet", strError)) << strError;
}

TEST_F(WalletManagerTest, AsyncOperationBuiltWithoutAWalletDoesNotPinAnything)
{
    // The parameterless constructor (used by the 3 operation classes
    // CWallet::ChainTip() spawns automatically, which have no
    // request-resolved wallet in scope at all) must not pin or touch the
    // registry -- confirmed here by checking it doesn't block unloading
    // *any* currently-loaded secondary wallet. Registered first for the same
    // reason as the test above -- see its comment.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("unrelatedwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("unrelatedwallet", strError)) << strError;

    {
        std::shared_ptr<AsyncRPCOperation> operation = std::make_shared<AsyncRPCOperation>();
        EXPECT_TRUE(operation->getWalletName().empty());
    }

    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("unrelatedwallet", strError)) << strError;
}

// ─── Phase 5: per-wallet config persistence ────────────────────────────────
// Consolidation/sweep/fee/pruning settings are per-CWallet fields, each
// changed only through a CWallet::Set*() method that also persists it via
// CWalletDB. These tests exercise that mechanism directly (bypassing the RPC
// layer, which is exercised separately in test_rpc_wallet_bitcoin.cpp)
// against two independently loaded secondary wallets, since that's where a
// setting leaking between wallets or failing to survive a reload would
// actually surface.

TEST_F(WalletManagerTest, PerWalletConsolidationSweepSettingsAreIndependentAcrossWallets)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("walleta.dat");
    CreateWalletFileOnDisk("walletb.dat");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("walleta.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("walletb.dat", strError)) << strError;

    CWallet* walletA = CWalletManager::Get().GetWallet("walleta.dat");
    CWallet* walletB = CWalletManager::Get().GetWallet("walletb.dat");
    ASSERT_NE(nullptr, walletA);
    ASSERT_NE(nullptr, walletB);

    walletA->SetSaplingConsolidationEnabled(true);
    walletA->SetSaplingConsolidationTargetQty(42);
    walletA->SetSaplingConsolidationTxFee(12345);
    walletA->SetSaplingConsolidationAddresses({"addrA1", "addrA2"});
    walletA->SetSweepTxFee(999);
    walletA->SetDeleteInterval(500);

    // walletB never touched -- must still read its own compiled-in defaults,
    // not anything walletA just set.
    EXPECT_FALSE(walletB->fSaplingConsolidationEnabled);
    EXPECT_EQ(100, walletB->targetSaplingConsolidationQty);
    EXPECT_EQ(10000, walletB->saplingConsolidationTxFee);
    EXPECT_TRUE(walletB->saplingConsolidationAddresses.empty());
    EXPECT_EQ(10000, walletB->sweepTxFee);
    EXPECT_EQ(DEFAULT_TX_DELETE_INTERVAL, walletB->fDeleteInterval);

    // walletA's own settings took effect.
    EXPECT_TRUE(walletA->fSaplingConsolidationEnabled);
    EXPECT_EQ(42, walletA->targetSaplingConsolidationQty);
    EXPECT_EQ(12345, walletA->saplingConsolidationTxFee);
    ASSERT_EQ(2u, walletA->saplingConsolidationAddresses.size());
    EXPECT_EQ(999, walletA->sweepTxFee);
    EXPECT_EQ(500, walletA->fDeleteInterval);

    // Give walletB its own, different values and re-confirm neither leaked
    // into the default wallet either.
    walletB->SetSaplingConsolidationEnabled(true);
    walletB->SetSaplingConsolidationTargetQty(7);
    CWallet* defaultWallet = CWalletManager::Get().GetWallet(CWalletManager::Get().GetActiveWalletName());
    EXPECT_FALSE(defaultWallet->fSaplingConsolidationEnabled);
    EXPECT_EQ(100, defaultWallet->targetSaplingConsolidationQty);
    EXPECT_EQ(42, walletA->targetSaplingConsolidationQty); // still walletA's own value
    EXPECT_EQ(7, walletB->targetSaplingConsolidationQty);
}

TEST_F(WalletManagerTest, PerWalletSettingsSurviveUnloadAndReload)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("persistwallet.dat");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("persistwallet.dat", strError)) << strError;
    CWallet* wallet = CWalletManager::Get().GetWallet("persistwallet.dat");
    ASSERT_NE(nullptr, wallet);
    CWalletManager::ResolvedWallet originalEntry =
        CWalletManager::Get().ResolveAndHoldForRequest("persistwallet.dat");
    ASSERT_EQ(CWalletManager::ResolveOutcome::Held, originalEntry.outcome);
    // Released straight away: a ref held past here would block the unload below.
    CWalletManager::Get().ReleaseRefIfCurrent("persistwallet.dat", originalEntry.generation);

    // One setting from each of the three groups the plan calls out:
    // consolidation/sweep, fee/behavior, and pruning.
    wallet->SetSaplingConsolidationInterval(4321);
    wallet->SetSpendZeroConfChange(false);
    wallet->SetKeepLastNTransactions(777);

    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("persistwallet.dat", strError)) << strError;
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("persistwallet.dat"));

    ASSERT_TRUE(CWalletManager::Get().LoadWallet("persistwallet.dat", strError)) << strError;
    CWallet* reloaded = CWalletManager::Get().GetWallet("persistwallet.dat");
    ASSERT_NE(nullptr, reloaded);
    // Deliberately not ASSERT_NE(wallet, reloaded): the allocator is free to
    // hand the reload the exact address the unload just freed, and does in
    // practice, so pointer identity is a coin flip rather than a test. The
    // registry generation is what actually identifies an entry instance (see
    // ReleaseRefIfCurrent), and the assertions below -- reading back values
    // that only ever existed on the deleted object's file -- are the real
    // proof this is a fresh load either way.
    CWalletManager::ResolvedWallet reloadedEntry =
        CWalletManager::Get().ResolveAndHoldForRequest("persistwallet.dat");
    ASSERT_EQ(CWalletManager::ResolveOutcome::Held, reloadedEntry.outcome);
    CWalletManager::Get().ReleaseRefIfCurrent("persistwallet.dat", reloadedEntry.generation);
    EXPECT_NE(originalEntry.generation, reloadedEntry.generation);

    // Proves the CWalletDB::ReadKeyValue() round-trip actually persisted these
    // to the file, not just held them in the now-deleted in-memory object.
    EXPECT_EQ(4321, reloaded->saplingConsolidationInterval);
    EXPECT_FALSE(reloaded->bSpendZeroConfChange);
    EXPECT_EQ(777u, reloaded->fKeepLastNTransactions);
}

TEST_F(WalletManagerTest, SalvageOnOneWalletDoesNotTouchAnotherLoadedWallet)
{
    // Closes the exact bug this phase fixed: -salvagewallet used to be read
    // globally inside CWallet::Verify(), so setting it salvaged every wallet
    // loaded process-wide. fSalvage is now a per-call LoadWallet() parameter
    // instead -- this doesn't corrupt a file to prove salvage logic itself
    // runs (that's CWallet::Verify()'s own concern), just that requesting it
    // for one wallet's load has no effect on a second, already-loaded one.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("untouchedwallet.dat");
    CreateWalletFileOnDisk("salvagedwallet.dat");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("untouchedwallet.dat", strError)) << strError;
    CWallet* untouched = CWalletManager::Get().GetWallet("untouchedwallet.dat");
    ASSERT_NE(nullptr, untouched);
    untouched->SetSaplingConsolidationTargetQty(55);

    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("untouchedwallet.dat", strError)) << strError;
    // Reload the same wallet fresh so the pointer above is no longer live,
    // then load a *different* wallet with fSalvage=true.
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("untouchedwallet.dat", strError)) << strError;
    untouched = CWalletManager::Get().GetWallet("untouchedwallet.dat");
    ASSERT_NE(nullptr, untouched);
    EXPECT_EQ(55, untouched->targetSaplingConsolidationQty); // survived its own reload

    ASSERT_TRUE(CWalletManager::Get().LoadWallet("salvagedwallet.dat", strError,
                                                  /*fRescan=*/false, /*nRescanHeight=*/0, /*fSalvage=*/true))
        << strError;

    // untouchedwallet.dat's own file/registry entry is unaffected by the
    // fSalvage=true request against a completely different wallet name.
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("untouchedwallet.dat"));
    EXPECT_EQ(55, untouched->targetSaplingConsolidationQty);
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("salvagedwallet.dat"));
}

TEST_F(WalletManagerTest, CreateWalletRejectsAnAlreadyExistingFile)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("existingwallet.dat");

    std::string strError, seedPhrase;
    EXPECT_FALSE(CWalletManager::Get().CreateWallet("existingwallet.dat", strError, seedPhrase));
    EXPECT_NE(std::string::npos, strError.find("already exists"));
    EXPECT_TRUE(seedPhrase.empty());
    // Rejected: never registered, and the on-disk file untouched by loadwallet.
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("existingwallet.dat"));
}

TEST_F(WalletManagerTest, CreateWalletSucceedsOnANewNameAndReturnsASeedPhrase)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("brandnewwallet.dat", strError, seedPhrase)) << strError;
    EXPECT_FALSE(seedPhrase.empty());

    CWallet* wallet = CWalletManager::Get().GetWallet("brandnewwallet.dat");
    ASSERT_NE(nullptr, wallet);
    EXPECT_FALSE(CWalletManager::Get().IsActiveWallet("brandnewwallet.dat"));
    // Opus-audit-caught regression: bip39Enabled was never set for a wallet
    // created via this RPC, unlike init.cpp's own fresh-HD-seed setup --
    // every address this wallet ever derives would have used a different
    // scheme than the one its own returned seed phrase actually implies.
    EXPECT_TRUE(wallet->bip39Enabled);

    std::vector<std::string> names = CWalletManager::Get().ListWalletNames();
    EXPECT_EQ(2u, names.size());

    // The seed phrase returned matches what the wallet itself now holds --
    // this is the only time it is ever available; there is no error in it
    // having been generated but not actually installed as the wallet's seed.
    std::string phraseFromWallet;
    ASSERT_TRUE(wallet->GetSeedPhrase(phraseFromWallet));
    EXPECT_EQ(seedPhrase, phraseFromWallet);
}

TEST_F(WalletManagerTest, CreateWalletProducesAWalletWithASaplingAddress)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("seededwallet.dat", strError, seedPhrase)) << strError;

    CWallet* wallet = CWalletManager::Get().GetWallet("seededwallet.dat");
    ASSERT_NE(nullptr, wallet);
    EXPECT_GE(wallet->mapZAddressBook.size(), 1u);
}

TEST_F(WalletManagerTest, LoadWalletRefusesAnEncryptedWalletWithAnHonestErrorInsteadOfCallingItCorrupt)
{
    // Every non-default wallet in the process is loaded through this one
    // function -- the loadwallet RPC, `-wallet=` at startup (init.cpp), and
    // the GUI's open-wallet action. All three can now supply a passphrase,
    // which drives the InitalizeCryptedLoad()/LoadCryptedSeedFromDB()/
    // OpenWallet()/seedEncyptionFP handshake init.cpp performs for the
    // default wallet before its own CWallet::LoadWallet() call. This test
    // covers the case where one is *not* supplied.
    //
    // It must fail -- skipping straight to CWallet::LoadWallet() on a crypted
    // file leaves every encrypted record undecryptable -- but *how* it fails
    // matters a great deal: reporting DB_CORRUPT, as it did before this
    // handshake existed here, is what sends an operator hunting for a salvage
    // tool to point at a healthy wallet holding real funds. It must say the
    // wallet is encrypted, must never say "corrupt", must set the
    // passphrase-required out-param so a caller (the GUI) can prompt instead
    // of dead-ending, and must not have salvaged anything on the way out.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("encryptedreload.dat", strError, seedPhrase)) << strError;
    CWallet* wallet = CWalletManager::Get().GetWallet("encryptedreload.dat");
    ASSERT_NE(nullptr, wallet);

    // CreateWallet() gives it a real HD seed, so this genuinely succeeds
    // (unlike the seedless wallets gtest/test_httprpc.cpp's encryptwallet
    // tests use). Done directly rather than through the encryptwallet RPC
    // purely to keep this test at the CWalletManager layer; the RPC route is
    // covered end to end by test_httprpc.cpp's
    // EncryptWalletSucceedsOnASecondaryWalletAndCanBeReloadedWithItsPassphrase.
    SecureString passphrase;
    passphrase.reserve(100);
    passphrase = "a test passphrase";
    ASSERT_TRUE(wallet->EncryptWallet(passphrase));
    ASSERT_TRUE(wallet->IsCrypted());

    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("encryptedreload.dat", strError)) << strError;

    std::string strReloadError;
    bool fPassphraseRequired = false;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("encryptedreload.dat", strReloadError,
        /*fRescan=*/false, /*nRescanHeight=*/0, /*fSalvage=*/false, /*fZapWalletTxes=*/false,
        /*fAllowCreate=*/false, SecureString(), &fPassphraseRequired));
    EXPECT_NE(std::string::npos, strReloadError.find("encrypted")) << strReloadError;
    EXPECT_EQ(std::string::npos, strReloadError.find("corrupt")) << strReloadError;
    // The out-param, not the error text, is what the GUI keys its
    // prompt-and-retry branch off -- so it has to be set here too, not only
    // on the wrong-passphrase path.
    EXPECT_TRUE(fPassphraseRequired) << strReloadError;
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("encryptedreload.dat"));

    // And it must not have salvaged anything on the way out: a salvage leaves
    // the pre-salvage file behind under a .{timestamp}.bak name
    // (CWalletDB::Recover, wallet/walletdb.cpp).
    for (boost::filesystem::directory_iterator it(GetDataDir()), end; it != end; ++it) {
        EXPECT_EQ(std::string::npos, it->path().filename().string().find(".bak"))
            << "a healthy wallet file was salvaged: " << it->path().string();
    }
}

TEST_F(WalletManagerTest, DiscardWalletAfterFailedEncryptionRefusesWhileASecondCallerHoldsARef)
{
    // DiscardWalletAfterFailedEncryption() exists to delete a CWallet that
    // CWallet::EncryptWallet() left half-transitioned, and it is called from
    // inside a request that is itself holding one ref on that wallet -- so it
    // tolerates refcount == 1 where UnloadWallet() demands 0. It must not
    // tolerate more. A second concurrent RPC request (whose
    // RPCWalletRequestGuard is constructed in the dispatch layer under
    // cs_wallets alone, so the caller's cs_main hold does not keep it out), a
    // running or unpolled AsyncRPCOperation on its own thread, or a wallet
    // open in the Qt GUI each hold a ref of their own; deleting the object
    // under any of them is a use-after-free.
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("discardrefwallet.dat");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("discardrefwallet.dat", strError)) << strError;

    CWalletManager::ResolvedWallet callerRef =
        CWalletManager::Get().ResolveAndHoldForRequest("discardrefwallet.dat");  // "this request"
    ASSERT_EQ(CWalletManager::ResolveOutcome::Held, callerRef.outcome);
    CWalletManager::ResolvedWallet otherRef =
        CWalletManager::Get().ResolveAndHoldForRequest("discardrefwallet.dat");  // somebody else
    ASSERT_EQ(CWalletManager::ResolveOutcome::Held, otherRef.outcome);

    EXPECT_FALSE(CWalletManager::Get().DiscardWalletAfterFailedEncryption("discardrefwallet.dat", strError));
    EXPECT_NE(std::string::npos, strError.find("in use")) << strError;
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("discardrefwallet.dat"));

    // Down to just the calling request's own ref: now it may proceed.
    CWalletManager::Get().ReleaseRefIfCurrent("discardrefwallet.dat", otherRef.generation);
    EXPECT_TRUE(CWalletManager::Get().DiscardWalletAfterFailedEncryption("discardrefwallet.dat", strError)) << strError;
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("discardrefwallet.dat"));

    // The caller's own outstanding ref releases harmlessly against a name
    // that no longer exists (ReleaseRefIfCurrent's own contract).
    CWalletManager::Get().ReleaseRefIfCurrent("discardrefwallet.dat", callerRef.generation);
}

TEST_F(WalletManagerTest, DiscardWalletAfterFailedEncryptionRefusesTheActiveWalletAndUnknownNames)
{
    CWalletManager::Get().RegisterInitialWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().DiscardWalletAfterFailedEncryption("default_test.dat", strError));
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("default_test.dat"));

    EXPECT_FALSE(CWalletManager::Get().DiscardWalletAfterFailedEncryption("nosuchwallet.dat", strError));
    EXPECT_NE(std::string::npos, strError.find("not found")) << strError;
}

// ─── No-default-wallet redesign: zero-wallet startup + active-wallet cursor ─

TEST_F(WalletManagerTest, RegistryStartsEmptyAndFirstLoadedWalletBecomesActiveAutomatically)
{
    // True zero-wallet startup: unlike every test above, nothing is ever
    // registered via RegisterInitialWallet() here -- this is the state a
    // fresh data directory with no -wallet= actually starts in (init.cpp's
    // fAutoLoadWalletAtStartup == false).
    EXPECT_TRUE(CWalletManager::Get().ListWalletNames().empty());
    EXPECT_TRUE(CWalletManager::Get().GetActiveWalletName().empty());

    CreateWalletFileOnDisk("firstwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;

    // The first wallet ever loaded into an empty registry becomes active
    // automatically -- no separate setactivewallet call needed.
    EXPECT_EQ("firstwallet.dat", CWalletManager::Get().GetActiveWalletName());
    EXPECT_TRUE(CWalletManager::Get().IsActiveWallet("firstwallet.dat"));
    EXPECT_EQ(CWalletManager::Get().GetWallet("firstwallet.dat"), pwalletMain);
}

TEST_F(WalletManagerTest, SecondLoadedWalletDoesNotStealActiveStatus)
{
    CreateWalletFileOnDisk("firstwallet.dat");
    CreateWalletFileOnDisk("secondwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondwallet.dat", strError)) << strError;

    EXPECT_EQ("firstwallet.dat", CWalletManager::Get().GetActiveWalletName());
    EXPECT_FALSE(CWalletManager::Get().IsActiveWallet("secondwallet.dat"));
    EXPECT_EQ(CWalletManager::Get().GetWallet("firstwallet.dat"), pwalletMain);
}

TEST_F(WalletManagerTest, SetActiveWalletSwitchesAndDeactivates)
{
    CreateWalletFileOnDisk("firstwallet.dat");
    CreateWalletFileOnDisk("secondwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondwallet.dat", strError)) << strError;

    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("secondwallet.dat", strError)) << strError;
    EXPECT_EQ("secondwallet.dat", CWalletManager::Get().GetActiveWalletName());
    EXPECT_EQ(CWalletManager::Get().GetWallet("secondwallet.dat"), pwalletMain);
    EXPECT_FALSE(CWalletManager::Get().IsActiveWallet("firstwallet.dat"));

    // "" deactivates -- both wallets stay loaded, nothing is active.
    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("", strError)) << strError;
    EXPECT_TRUE(CWalletManager::Get().GetActiveWalletName().empty());
    EXPECT_EQ(nullptr, pwalletMain);
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("firstwallet.dat"));
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("secondwallet.dat"));
}

TEST_F(WalletManagerTest, SetActiveWalletRejectsUnknownNameAndLeavesActiveUnchanged)
{
    CreateWalletFileOnDisk("firstwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;

    EXPECT_FALSE(CWalletManager::Get().SetActiveWallet("neverloadedwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("not found"));
    EXPECT_EQ("firstwallet.dat", CWalletManager::Get().GetActiveWalletName());
}

TEST_F(WalletManagerTest, UnloadingTheSoleLoadedWalletRequiresDeactivatingItFirst)
{
    CreateWalletFileOnDisk("onlywallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("onlywallet.dat", strError)) << strError;

    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("onlywallet.dat", strError));
    EXPECT_NE(std::string::npos, strError.find("active"));

    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("", strError)) << strError;
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("onlywallet.dat", strError)) << strError;
    EXPECT_TRUE(CWalletManager::Get().ListWalletNames().empty());
    EXPECT_EQ(nullptr, pwalletMain);
}

TEST_F(WalletManagerTest, ReloadAfterFullUnloadBecomesActiveAgain)
{
    CreateWalletFileOnDisk("onlywallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("onlywallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().UnloadWallet("onlywallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().ListWalletNames().empty());

    ASSERT_TRUE(CWalletManager::Get().LoadWallet("onlywallet.dat", strError)) << strError;
    EXPECT_EQ("onlywallet.dat", CWalletManager::Get().GetActiveWalletName());
    EXPECT_EQ(CWalletManager::Get().GetWallet("onlywallet.dat"), pwalletMain);
}

TEST_F(WalletManagerTest, UnscopedRequestGuardResolvesToWhicheverWalletIsActive)
{
    CreateWalletFileOnDisk("firstwallet.dat");
    CreateWalletFileOnDisk("secondwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondwallet.dat", strError)) << strError;

    {
        // Unscoped (empty name): resolves to the active wallet and holds a
        // ref on it, unlike the old default-wallet behavior where an empty
        // selection never took a ref at all (the default was permanently
        // unloadable, so there was nothing to protect).
        RPCWalletRequestGuard guard("");
        ASSERT_TRUE(guard.IsResolved());
        EXPECT_EQ(CWalletManager::Get().GetWallet("firstwallet.dat"), CWalletManager::GetWalletForRequest());
        // The active wallet is already unloadable-refused on its own merits,
        // but this guard's own ref would refuse it too, independently --
        // confirmed here indirectly via the currently-active check still
        // being the one that fires.
        EXPECT_FALSE(CWalletManager::Get().UnloadWallet("firstwallet.dat", strError));
    }

    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("", strError)) << strError;
    RPCWalletRequestGuard noneActive("");
    EXPECT_FALSE(noneActive.IsResolved());
}

TEST_F(WalletManagerTest, UnscopedRequestStaysPinnedToItsResolvedWalletEvenIfActiveStatusMovesMidRequest)
{
    // Opus-audit-caught race, now closed: an unscoped request used to leave
    // GetRequestedWalletName() empty, so GetWalletForRequest() re-read the
    // *live* pwalletMain for the whole request -- a setactivewallet landing
    // mid-request (another thread, or a nested call on this one) could
    // silently redirect an in-flight handler to a different wallet than the
    // one that was active when the request actually resolved. The guard now
    // pins GetRequestedWalletName() to the resolved name up front, so
    // GetWalletForRequest() keeps returning the *same* wallet for the whole
    // guard's lifetime regardless of what setactivewallet does afterward.
    CreateWalletFileOnDisk("firstwallet.dat");
    CreateWalletFileOnDisk("secondwallet.dat");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("firstwallet.dat", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondwallet.dat", strError)) << strError;

    RPCWalletRequestGuard guard("");
    ASSERT_TRUE(guard.IsResolved());
    CWallet* resolvedAtStart = CWalletManager::GetWalletForRequest();
    EXPECT_EQ(CWalletManager::Get().GetWallet("firstwallet.dat"), resolvedAtStart);
    EXPECT_FALSE(CWalletManager::WasWalletExplicitlySelected());

    // Simulates the race: something else moves which wallet is active while
    // this request's guard is still alive.
    ASSERT_TRUE(CWalletManager::Get().SetActiveWallet("secondwallet.dat", strError)) << strError;
    EXPECT_EQ(CWalletManager::Get().GetWallet("secondwallet.dat"), pwalletMain);

    // GetWalletForRequest() must still return the wallet this guard actually
    // pinned, not the new live pwalletMain -- the whole point of the fix.
    EXPECT_EQ(resolvedAtStart, CWalletManager::GetWalletForRequest());
    EXPECT_EQ("firstwallet.dat", CWalletManager::GetRequestedWalletName());
}

TEST_F(WalletManagerTest, CreateWalletWithARecoveryPhraseRestoresInsteadOfGeneratingRandom)
{
    // Replaces the old -seedphrase=/-wallet= startup-flag combination
    // (removed): CreateWallet() itself now accepts the recovery phrase
    // directly.
    CreateWalletFileOnDisk("sourcewalletforrecovery.dat");
    std::string strError, sourcePhrase;
    {
        CWallet scratch("sourcewalletforrecovery.dat");
        bool fFirstRun;
        ASSERT_EQ(DB_LOAD_OK, scratch.LoadWallet(fFirstRun));
        scratch.GenerateNewSeed();
        ASSERT_TRUE(scratch.GetSeedPhrase(sourcePhrase));
    }
    ASSERT_FALSE(sourcePhrase.empty());

    SecureString recoveryPhrase;
    recoveryPhrase.reserve(sourcePhrase.size() + 1);
    recoveryPhrase = sourcePhrase.c_str();

    std::string seedPhraseOut;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("recoveredwallet.dat", strError, seedPhraseOut, recoveryPhrase))
        << strError;
    EXPECT_EQ(sourcePhrase, seedPhraseOut);

    CWallet* recovered = CWalletManager::Get().GetWallet("recoveredwallet.dat");
    ASSERT_NE(nullptr, recovered);
    std::string phraseFromRecovered;
    ASSERT_TRUE(recovered->GetSeedPhrase(phraseFromRecovered));
    EXPECT_EQ(sourcePhrase, phraseFromRecovered);

    // Opus-audit-caught regressions, both now fixed in CreateWallet()'s own
    // recovery-phrase branch:
    // (1) bip39Enabled was never set (CWallet::SetNull()'s default is
    // false), unlike init.cpp's own fresh-HD-seed setup which always sets it
    // true -- SaplingExtendedSpendingKey::Master()/the Ironwood equivalent
    // (zcash/address/zip32.cpp) derive under a completely different scheme
    // depending on this flag, so a recovered wallet would land on addresses
    // with no funds and no way to reach the real ones.
    EXPECT_TRUE(recovered->bip39Enabled);
    // (2) recovery never rescanned -- LoadWallet()'s own fFirstRun handling
    // (which runs before a recovered wallet even has a seed) pins the
    // checkpoint at the current tip, appropriate for a brand-new random seed
    // but wrong for a phrase that may have existing on-chain history.
    // nBirthday == 0 is CreateWallet()'s own signal that it forced this the
    // same way LoadWallet()'s explicit-fRescan branch and the old, removed
    // -seedphrase= startup path both already did.
    EXPECT_EQ(0, recovered->nBirthday);
}

TEST_F(WalletManagerTest, CreateWalletRejectsAnInvalidRecoveryPhraseWithoutDeletingTheAlreadyRegisteredWallet)
{
    SecureString recoveryPhrase;
    recoveryPhrase.reserve(32);
    recoveryPhrase = "not a valid bip39 phrase at all";

    std::string strError, seedPhraseOut;
    EXPECT_FALSE(CWalletManager::Get().CreateWallet("badrecovery.dat", strError, seedPhraseOut, recoveryPhrase));
    EXPECT_NE(std::string::npos, strError.find("invalid"));
    // Matches CreateWallet()'s existing "seeding failed" behavior for the
    // exception path just below this one in the source: the wallet is
    // already registered (LoadWallet() succeeded) by the time seeding is
    // attempted, so a seeding failure leaves it loaded rather than silently
    // deleting something a concurrent listwallets could already see.
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("badrecovery.dat"));
}

// -secondarywalletpassphrase=<name>:<passphrase> parsing (init.cpp). Uses the
// plain TEST form, not WalletManagerTest: this is pure string handling with no
// datadir, CDBEnv or registry involvement, so the fixture would only add
// teardown risk. The real startup loop that consumes this is buried inside
// AppInit2() and isn't reachable from a unit test; the split itself is, which
// is where the sharp edges are (a passphrase containing the separator, and a
// malformed entry whose contents must never reach debug.log).
TEST(SecondaryWalletPassphraseArg, SplitsOnTheFirstColonOnly)
{
    std::string strName;
    SecureString strPass;
    ASSERT_TRUE(ParseSecondaryWalletPassphraseEntry("second.dat:hunter2", strName, strPass));
    EXPECT_EQ("second.dat", strName);
    EXPECT_EQ("hunter2", std::string(strPass.begin(), strPass.end()));
}

TEST(SecondaryWalletPassphraseArg, KeepsEveryColonInsideThePassphrase)
{
    // The whole reason the split is "first colon" rather than "only colon":
    // a wallet name can never contain ':' (CWalletManager::IsValidWalletName()
    // allows letters, digits, '.', '_' and '-' only), so everything after the
    // first one is unambiguously passphrase and must survive verbatim --
    // truncating at a later colon would silently turn a correct passphrase
    // into a wrong one and look like operator error.
    std::string strName;
    SecureString strPass;
    ASSERT_TRUE(ParseSecondaryWalletPassphraseEntry("second.dat:a:b::c:", strName, strPass));
    EXPECT_EQ("second.dat", strName);
    EXPECT_EQ("a:b::c:", std::string(strPass.begin(), strPass.end()));
}

TEST(SecondaryWalletPassphraseArg, RejectsMalformedEntriesWithoutTouchingTheOutputs)
{
    // Every rejected shape leaves both out-params exactly as the caller had
    // them, so a caller looping over several entries can't accidentally apply
    // a previous entry's passphrase to a later, malformed one.
    const char* rejected[] = {
        "",                        // empty
        "second.dat",              // no separator at all
        "second.dat=hunter2",      // mistyped separator -- carries the passphrase
        "second.dat hunter2",      // ditto
        ":hunter2",                // empty wallet name
    };
    for (const char* entry : rejected) {
        std::string strName = "untouched";
        SecureString strPass;
        strPass = "untouched";
        EXPECT_FALSE(ParseSecondaryWalletPassphraseEntry(entry, strName, strPass)) << entry;
        EXPECT_EQ("untouched", strName) << entry;
        EXPECT_EQ("untouched", std::string(strPass.begin(), strPass.end())) << entry;
    }
}

TEST(SecondaryWalletPassphraseArg, AcceptsAnEmptyPassphraseAsMeaningNoneGiven)
{
    // "name:" parses, and yields the same empty SecureString as omitting the
    // flag entirely -- CWalletManager::LoadWallet() then fails that wallet
    // with its explicit "requires its passphrase" error rather than anything
    // more confusing.
    std::string strName;
    SecureString strPass;
    ASSERT_TRUE(ParseSecondaryWalletPassphraseEntry("second.dat:", strName, strPass));
    EXPECT_EQ("second.dat", strName);
    EXPECT_TRUE(strPass.empty());
}
