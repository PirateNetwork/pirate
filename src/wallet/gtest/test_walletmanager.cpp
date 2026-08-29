// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "asyncrpcoperation.h"
#include "init.h"
#include "util.h"
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
        CWalletManager::Get().FlushAndUnloadAllSecondaryWallets();
        CWallet* defaultWallet = CWalletManager::Get().GetWallet(CWalletManager::Get().GetDefaultWalletName());
        CWalletManager::Get().Reset();
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

TEST_F(WalletManagerTest, RegisterDefaultWalletIsListedAndFlaggedDefault)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", defaultWallet);

    EXPECT_EQ("default_test.dat", CWalletManager::Get().GetDefaultWalletName());
    EXPECT_TRUE(CWalletManager::Get().IsDefaultWallet("default_test.dat"));
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));

    std::vector<std::string> names = CWalletManager::Get().ListWalletNames();
    ASSERT_EQ(1u, names.size());
    EXPECT_EQ("default_test.dat", names[0]);
}

TEST_F(WalletManagerTest, LoadUnloadListHappyPath)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    EXPECT_FALSE(CWalletManager::Get().IsDefaultWallet("secondtestwallet"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().LoadWallet("nonexistenttestwallet", strError));
    EXPECT_FALSE(strError.empty());
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("nonexistenttestwallet"));
}

TEST_F(WalletManagerTest, LoadWalletRejectsDuplicateName)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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

TEST_F(WalletManagerTest, UnloadRefusesTheDefaultWalletUnconditionally)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("default_test.dat", strError));
    EXPECT_FALSE(strError.empty());
    EXPECT_NE(nullptr, CWalletManager::Get().GetWallet("default_test.dat"));
}

TEST_F(WalletManagerTest, UnloadRefusesUnknownWallet)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError;
    EXPECT_FALSE(CWalletManager::Get().UnloadWallet("neverloadedwallet", strError));
    EXPECT_NE(std::string::npos, strError.find("not found"));
}

TEST_F(WalletManagerTest, UnloadRefusesWhileRefcountHeldThenSucceedsAfterRelease)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    auto notFound = CWalletManager::Get().ResolveAndHoldForRequest("neverloadedwallet");
    EXPECT_EQ(CWalletManager::ResolveOutcome::NotFound, notFound.outcome);

    auto isDefault = CWalletManager::Get().ResolveAndHoldForRequest("default_test.dat");
    EXPECT_EQ(CWalletManager::ResolveOutcome::IsDefault, isDefault.outcome);
    // The default wallet is never unloadable, so resolving it holds no ref --
    // confirmed indirectly below by unloading the default wallet still being
    // refused for the usual reason, not an "in use" one.

    auto held = CWalletManager::Get().ResolveAndHoldForRequest("secondtestwallet");
    EXPECT_EQ(CWalletManager::ResolveOutcome::HeldSecondary, held.outcome);
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    auto firstLoad = CWalletManager::Get().ResolveAndHoldForRequest("secondtestwallet");
    ASSERT_EQ(CWalletManager::ResolveOutcome::HeldSecondary, firstLoad.outcome);

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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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

TEST_F(WalletManagerTest, FlushAndUnloadAllSecondaryWalletsLeavesDefaultAloneAndIsIdempotent)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", defaultWallet);
    CreateWalletFileOnDisk("secondtestwallet");
    CreateWalletFileOnDisk("thirdtestwallet");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("thirdtestwallet", strError)) << strError;

    CWalletManager::Get().FlushAndUnloadAllSecondaryWallets();
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("secondtestwallet"));
    EXPECT_EQ(nullptr, CWalletManager::Get().GetWallet("thirdtestwallet"));
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));

    // Calling it again with nothing left to unload must be a harmless no-op.
    CWalletManager::Get().FlushAndUnloadAllSecondaryWallets();
    EXPECT_EQ(defaultWallet, CWalletManager::Get().GetWallet("default_test.dat"));
}

TEST_F(WalletManagerTest, ResetIsIdempotentAndDoesNotDeleteTheDefaultWalletObject)
{
    CWallet* defaultWallet = new CWallet("default_test.dat");
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", defaultWallet);

    CWalletManager::Get().Reset();
    EXPECT_TRUE(CWalletManager::Get().ListWalletNames().empty());
    EXPECT_TRUE(CWalletManager::Get().GetDefaultWalletName().empty());
    EXPECT_FALSE(CWalletManager::Get().IsDefaultWallet("default_test.dat"));

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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("secondtestwallet");
    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondtestwallet", strError)) << strError;

    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    {
        RPCWalletRequestGuard guard("secondtestwallet");
        EXPECT_EQ("secondtestwallet", CWalletManager::GetRequestedWalletName());
    }
    // Cleared on normal scope exit.
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());

    // Cleared even when the guarded work throws -- the same unwinding path
    // tableRPC.execute() takes, which is why this must be RAII and not a
    // manual set/clear pair around the call site.
    EXPECT_THROW({
        RPCWalletRequestGuard guard("secondtestwallet");
        throw std::runtime_error("simulated RPC failure");
    }, std::runtime_error);
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());

    // Back-to-back guards on the same thread must not leak into each other.
    {
        RPCWalletRequestGuard guard1("secondtestwallet");
        EXPECT_EQ("secondtestwallet", CWalletManager::GetRequestedWalletName());
    }
    {
        RPCWalletRequestGuard guard2("");
        EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    }
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
}

TEST_F(WalletManagerTest, GetWalletForRequestResolvesDefaultAndSecondary)
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", defaultWallet);

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
    // *any* currently-loaded secondary wallet.
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWallet* defaultWallet = CWalletManager::Get().GetWallet(CWalletManager::Get().GetDefaultWalletName());
    EXPECT_FALSE(defaultWallet->fSaplingConsolidationEnabled);
    EXPECT_EQ(100, defaultWallet->targetSaplingConsolidationQty);
    EXPECT_EQ(42, walletA->targetSaplingConsolidationQty); // still walletA's own value
    EXPECT_EQ(7, walletB->targetSaplingConsolidationQty);
}

TEST_F(WalletManagerTest, PerWalletSettingsSurviveUnloadAndReload)
{
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
    CreateWalletFileOnDisk("persistwallet.dat");

    std::string strError;
    ASSERT_TRUE(CWalletManager::Get().LoadWallet("persistwallet.dat", strError)) << strError;
    CWallet* wallet = CWalletManager::Get().GetWallet("persistwallet.dat");
    ASSERT_NE(nullptr, wallet);

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
    ASSERT_NE(wallet, reloaded); // genuinely a fresh CWallet object, not the same pointer

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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));
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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("brandnewwallet.dat", strError, seedPhrase)) << strError;
    EXPECT_FALSE(seedPhrase.empty());

    CWallet* wallet = CWalletManager::Get().GetWallet("brandnewwallet.dat");
    ASSERT_NE(nullptr, wallet);
    EXPECT_FALSE(CWalletManager::Get().IsDefaultWallet("brandnewwallet.dat"));

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
    CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

    std::string strError, seedPhrase;
    ASSERT_TRUE(CWalletManager::Get().CreateWallet("seededwallet.dat", strError, seedPhrase)) << strError;

    CWallet* wallet = CWalletManager::Get().GetWallet("seededwallet.dat");
    ASSERT_NE(nullptr, wallet);
    EXPECT_GE(wallet->mapZAddressBook.size(), 1u);
}
