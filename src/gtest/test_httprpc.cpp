// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "httprpc.cpp"
#include "httpserver.h"

#ifdef ENABLE_WALLET
#include "asyncrpcoperation.h"
#include "asyncrpcqueue.h"
#include "init.h"
#include "key_io.h"
#include "rpc/register.h"
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"
#include "wallet/walletmanager.h"

#include <boost/filesystem.hpp>
#endif

// Covers HTTP JSON-RPC request handling in httprpc.cpp, specifically
// RFC2617-style HTTP Basic authentication (method checks, missing auth
// header, bad credentials). Pool-agnostic transport-layer surface.

using ::testing::Return;
#ifdef ENABLE_WALLET
using ::testing::DoAll;
using ::testing::SaveArg;
// Not `using ::testing::_;` -- util.h's gettext-style `_(const char*)` free
// function already occupies that name at file scope, so every gmock wildcard
// below is spelled out as ::testing::_ instead (same convention as
// test_deprecation.cpp).
#endif

class MockHTTPRequest : public HTTPRequest {
public:
    MOCK_METHOD0(GetPeer, CService());
    MOCK_METHOD0(GetRequestMethod, HTTPRequest::RequestMethod());
    MOCK_METHOD1(GetHeader, std::pair<bool, std::string>(const std::string& hdr));
    MOCK_METHOD2(WriteHeader, void(const std::string& hdr, const std::string& value));
    MOCK_METHOD2(WriteReply, void(int nStatus, const std::string& strReply));

    MockHTTPRequest() : HTTPRequest(nullptr) {}
    void CleanUp() {
        // So the parent destructor doesn't try to send a reply
        replySent = true;
    }
};

TEST(HTTPRPC, FailsOnGET) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::GET));
    EXPECT_CALL(req, WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests"))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithoutAuthHeader) {
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(false, "")));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

TEST(HTTPRPC, FailsWithBadAuth)
{
    // Mock the getpeerinfo RPC call to succeed, so that a username and password
    // for the remote peer is added to the rpcauth table.
    // EXPECT_CALL(rpcService, CallRPC("getpeerinfo", _, _))
    //     .WillOnce(Return(UniValue(UniValue::VARR)));
    // Mock the lookup function to return a CService.
    // This is necessary because the default mock action for LookupNumeric is to return false.
    // EXPECT_CALL(*pLookupNumericMock, LookupNumeric("127.0.0.1", _, _))
    //     .WillRepeatedly(Return(CService(CNetAddr("127.0.0.1"), 1337)));

    // Test the HTTP basic authentication.
    // Wrong password
    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(true, "Basic spam:eggs")));
    // Construct CService correctly for the GetPeer mock
    CService peerService;
    peerService = LookupNumeric("127.0.0.1", 1337); // Default port, can be anything
    EXPECT_CALL(req, GetPeer())
        .WillRepeatedly(Return(peerService));
    EXPECT_CALL(req, WriteHeader("WWW-Authenticate", "Basic realm=\"jsonrpc\""))
        .Times(1);
    EXPECT_CALL(req, WriteReply(HTTP_UNAUTHORIZED, ""))
        .Times(1);
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, ""));
    req.CleanUp();
}

#ifdef ENABLE_WALLET
// --- Multiwallet URI routing / dispatch gate --------------------------------
//
// HTTPRequest::ReadBody() and ::GetURI() are not virtual, so a mocked request
// can't safely be driven past authentication into body parsing (a real
// evhttp_request* is required there, and MockHTTPRequest is built with
// nullptr). Because the wallet-name-from-URI check runs before ReadBody() is
// ever called, the "unknown wallet name" rejection is still covered against
// the real HTTPReq_JSONRPC below. The dispatch gate itself -- what happens
// once a wallet name IS resolved -- is covered directly against
// CRPCTable::execute() and RPCWalletRequestGuard, the same production code
// HTTPReq_JSONRPC drives; that needs no HTTP mocking at all.

namespace {
struct ScopedRPCAuth {
    std::string prev;
    explicit ScopedRPCAuth(const std::string& userpass) : prev(strRPCUserColonPass) {
        strRPCUserColonPass = userpass;
    }
    ~ScopedRPCAuth() { strRPCUserColonPass = prev; }
};
}

TEST(HTTPRPC, WalletUriUnknownNameThrowsWalletNotFoundBeforeBodyIsRead)
{
    ScopedRPCAuth auth("user:pass");

    MockHTTPRequest req;
    EXPECT_CALL(req, GetRequestMethod())
        .WillRepeatedly(Return(HTTPRequest::POST));
    EXPECT_CALL(req, GetHeader("authorization"))
        .WillRepeatedly(Return(std::make_pair(true, "Basic " + EncodeBase64("user:pass"))));
    EXPECT_CALL(req, WriteHeader(::testing::_, ::testing::_)).Times(::testing::AnyNumber());

    int capturedStatus = 0;
    std::string capturedReply;
    EXPECT_CALL(req, WriteReply(::testing::_, ::testing::_))
        .WillOnce(DoAll(SaveArg<0>(&capturedStatus), SaveArg<1>(&capturedReply)));

    // Syntactically valid (alphanumeric) but never loaded -- if ReadBody()
    // were reached instead, the mock would crash rather than return JSON.
    EXPECT_FALSE(HTTPReq_JSONRPC(&req, "nosuchwallettest"));
    req.CleanUp();

    EXPECT_EQ(HTTP_INTERNAL_SERVER_ERROR, capturedStatus);
    EXPECT_NE(std::string::npos, capturedReply.find(std::to_string((int)RPC_WALLET_NOT_FOUND)));
}

class MultiWalletDispatchTest : public ::testing::Test {
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
        // See test_walletmanager.cpp: CDBEnv::Open() only binds to the first
        // datadir it's ever given, so this needs its own fresh CDBEnv too.
        // Saved so TearDown() can restore it instead of leaving this test's
        // env installed as global state for whatever runs next.
        previousBitdb = bitdb;
        bitdb = std::shared_ptr<CDBEnv>(new CDBEnv{});

        RegisterAllCoreRPCCommands(tableRPC);
        RegisterWalletRPCCommands(tableRPC);
        RegisterMultiWalletRPCCommands(tableRPC);

        // CRPCTable::execute() refuses everything with RPC_IN_WARMUP until this
        // is called; it's a global, one-way (assert-guarded) switch, so only
        // flip it if some earlier test in this binary hasn't already.
        if (RPCIsInWarmup(nullptr))
            SetRPCWarmupFinished();

        CWallet* defaultWallet = new CWallet("default_test.dat");
        CWalletManager::Get().RegisterDefaultWallet("default_test.dat", defaultWallet);
        // GetWalletForRequest() falls back to the real pwalletMain global for
        // the no-selection case, exactly like init.cpp -- which registers the
        // same CWallet* as both. Without this, tests exercising a rewired RPC
        // via the default "/" path would run against whatever pwalletMain was
        // left over from an earlier, unrelated test in this binary.
        previousPwalletMain = pwalletMain;
        pwalletMain = defaultWallet;

        bool fFirstRun;
        CWallet scratch("secondarytestwallet");
        ASSERT_EQ(DB_LOAD_OK, scratch.LoadWallet(fFirstRun));

        std::string strError;
        ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondarytestwallet", strError)) << strError;
    }

    void TearDown() override {
        pwalletMain = previousPwalletMain;
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
        bitdb = previousBitdb;
        ClearDatadirCache();
        if (fHadPreviousDatadir)
            mapArgs["-datadir"] = previousDatadir;
        else
            mapArgs.erase("-datadir");
        boost::filesystem::remove_all(pathTemp);
    }

    boost::filesystem::path pathTemp;
    std::shared_ptr<CDBEnv> previousBitdb;
    bool fHadPreviousDatadir;
    std::string previousDatadir;
    CWallet* previousPwalletMain = nullptr; // in case SetUp() fails before it's assigned
};

TEST_F(MultiWalletDispatchTest, DefaultWalletUriRunsWalletRPCsNormally)
{
    RPCWalletRequestGuard guard("default_test.dat");
    EXPECT_NO_THROW(tableRPC.execute("listwallets", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, RootUriWithNoWalletSegmentAlsoRunsWalletRPCsNormally)
{
    RPCWalletRequestGuard guard(""); // "/" has no wallet segment at all
    EXPECT_NO_THROW(tableRPC.execute("listwallets", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriBlocksOrdinaryWalletRPCs)
{
    // encryptwallet is deliberately NOT in IsMultiWalletAwareRPC(): it
    // unconditionally calls StartShutdown() on success (rpcmultiwallet.cpp),
    // so encrypting one secondary wallet would restart the whole node -- it
    // needs its own design, not a mechanical pwalletMain->pwallet
    // substitution. Good stand-in for "an ordinary, still-gated 'wallet'-
    // category RPC" now that settxfee (Phase 5) and getbalance are rewired.
    RPCWalletRequestGuard guard("secondarytestwallet");
    try {
        tableRPC.execute("encryptwallet", UniValue(UniValue::VARR));
        FAIL() << "expected encryptwallet to be refused against a non-default wallet";
    } catch (const UniValue& objError) {
        EXPECT_EQ((int)RPC_WALLET_NOT_SPECIFIED, find_value(objError, "code").get_int());
    }
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriBlocksNonWalletCategoryRPCsToo)
{
    // The gate must not key off pcmd->category == "wallet": pwalletMain is
    // read directly by RPCs registered under other categories too (e.g.
    // "rawtransactions", "pirate Exclusive"), and a category allowlist would
    // silently let those reach the wrong wallet instead of being refused.
    // fundrawtransaction ("rawtransactions") is registered by
    // RegisterAllCoreRPCCommands in SetUp() and reads pwalletMain directly --
    // the gate must fire before that ever runs. (signrawtransaction, the
    // previous stand-in here, was itself made multiwallet-aware in Phase 10
    // and is no longer a valid "still refused" example.)
    RPCWalletRequestGuard guard("secondarytestwallet");
    try {
        tableRPC.execute("fundrawtransaction", UniValue(UniValue::VARR));
        FAIL() << "expected fundrawtransaction to be refused against a non-default wallet";
    } catch (const UniValue& objError) {
        EXPECT_EQ((int)RPC_WALLET_NOT_SPECIFIED, find_value(objError, "code").get_int());
    }
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriBlocksOpenwalletDuringTheUnlockWindowToo)
{
    // The gate used to live only in execute()'s normal-dispatch "else"
    // branch; the fRPCNeedUnlocked branch (the encrypted-wallet-startup
    // unlock window) let "openwallet" through completely unchecked. Without
    // the gate covering this branch too, a passphrase supplied for one
    // wallet name could unlock a different (the default) wallet instead.
    SetRPCNeedsUnlocked(true);
    struct ScopedUnlockFlag { ~ScopedUnlockFlag() { SetRPCNeedsUnlocked(false); } } resetFlag;

    RPCWalletRequestGuard guard("secondarytestwallet");
    try {
        tableRPC.execute("openwallet", UniValue(UniValue::VARR));
        FAIL() << "expected openwallet to be refused against a non-default wallet";
    } catch (const UniValue& objError) {
        EXPECT_EQ((int)RPC_WALLET_NOT_SPECIFIED, find_value(objError, "code").get_int());
    }
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriStillAllowsRegistryManagementRPCs)
{
    // loadwallet/unloadwallet/listwallets are exempt from the gate in phase 1
    // regardless of which wallet name the URI resolved to.
    RPCWalletRequestGuard guard("secondarytestwallet");
    EXPECT_NO_THROW(tableRPC.execute("listwallets", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, ThreadLocalDoesNotLeakIntoTheNextRequestOnTheSameThread)
{
    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        EXPECT_THROW(tableRPC.execute("settxfee", UniValue(UniValue::VARR)), UniValue);
    }
    // The guard's destructor must clear the thread-local even though execute()
    // threw -- otherwise a later, unrelated request reusing this pooled HTTP
    // worker thread would silently inherit this request's wallet selection.
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_NO_THROW(tableRPC.execute("listwallets", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriNowAllowsTheRewiredCoreSubset)
{
    // Phase 1 could only prove these were REFUSED against a non-default
    // wallet; phase 2 rewired them to actually run. Zero-arg-safe read-only
    // members of the set are enough to prove the gate now lets them through.
    RPCWalletRequestGuard guard("secondarytestwallet");
    EXPECT_NO_THROW(tableRPC.execute("getbalance", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("getwalletinfo", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("listunspent", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("listtransactions", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("listaddressgroupings", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("keypoolrefill", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, SecondaryWalletUriNowAllowsThePhase10RewiredSubset)
{
    // Same property as SecondaryWalletUriNowAllowsTheRewiredCoreSubset above,
    // for the 46 functions Phase 10 rewired -- a zero-arg-safe, read-only
    // sample spanning all three source locations (rpcwallet.cpp, rpcdump.cpp,
    // and the misc RPC files) is enough to prove the gate lets them through.
    RPCWalletRequestGuard guard("secondarytestwallet");
    EXPECT_NO_THROW(tableRPC.execute("getinfo", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("listaccounts", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("listlockunspent", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("getkeypoolsize", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("getunconfirmedbalance", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("resendwallettransactions", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("z_listaddresses", UniValue(UniValue::VARR)));
    EXPECT_NO_THROW(tableRPC.execute("z_listunspent", UniValue(UniValue::VARR)));
}

TEST_F(MultiWalletDispatchTest, GetNewAddressOperatesOnTheSelectedWalletNotTheDefault)
{
    // The real correctness property phase 2 exists for: a rewired RPC must
    // touch the *resolved* wallet's own state, not silently fall through to
    // pwalletMain the way it would if GetWalletForRequest() were wired wrong
    // or a helper function still referenced the global internally.
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondarytestwallet");
    ASSERT_NE(nullptr, secondaryWallet);
    ASSERT_NE(secondaryWallet, pwalletMain);

    UniValue result;
    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        ASSERT_NO_THROW(result = tableRPC.execute("getnewaddress", UniValue(UniValue::VARR)));
    }
    CTxDestination dest = DecodeDestination(result.get_str());
    ASSERT_TRUE(IsValidDestination(dest));

    {
        LOCK(secondaryWallet->cs_wallet);
        EXPECT_TRUE(secondaryWallet->mapAddressBook.count(dest));
    }
    {
        LOCK(pwalletMain->cs_wallet);
        EXPECT_FALSE(pwalletMain->mapAddressBook.count(dest));
    }
}

TEST_F(MultiWalletDispatchTest, GetWalletUnlockTimeForRequestReflectsTheSelectedWalletOnly)
{
    // Phase 10 regression: getinfo's "unlocked_until" used to read the plain
    // nWalletUnlockTime global directly, which only ever reflects the
    // *default* wallet -- calling getinfo against an unlocked secondary
    // wallet reported it as still locked, and calling it against the default
    // wallet while only the secondary was unlocked leaked the secondary's
    // deadline onto an unrelated request. Fixed by having getinfo call the
    // new GetWalletUnlockTimeForRequest(pwallet) (rpc/server.h) instead; this
    // test exercises that accessor directly (rather than through a full
    // getinfo call) to avoid an unrelated, pre-existing BDB hazard where
    // opening a fresh CWalletDB (as getinfo's keypool fields do) immediately
    // after EncryptWallet()'s CDB::Rewrite() in the same live process can
    // fail with "can't open database" -- see the CDB::Rewrite() comment on
    // UnloadSucceedsCleanlyWithAPendingAutoRelockTimerArmed below for the
    // same hazard in a different test.
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondarytestwallet");
    ASSERT_NE(nullptr, secondaryWallet);
    ASSERT_NE(secondaryWallet, pwalletMain);

    // walletpassphrase arms a process-wide RPCRunLater auto-relock timer,
    // which throws unless some RPCTimerInterface is registered -- see
    // UnloadSucceedsCleanlyWithAPendingAutoRelockTimerArmed below. This test
    // doesn't need to observe the timer firing/being destroyed, just a no-op
    // stand-in so RPCRunLater() doesn't throw.
    class NoopRPCTimer : public RPCTimerBase {};
    class NoopRPCTimerInterface : public RPCTimerInterface {
    public:
        const char* Name() override { return "noop"; }
        RPCTimerBase* NewTimer(boost::function<void(void)>&, int64_t) override { return new NoopRPCTimer(); }
    };
    NoopRPCTimerInterface timerInterface;
    RPCRegisterTimerInterface(&timerInterface);
    struct TimerInterfaceGuard {
        RPCTimerInterface* iface;
        ~TimerInterfaceGuard() { RPCUnregisterTimerInterface(iface); }
    } timerGuard{&timerInterface};

    // EncryptWallet() requires an HD seed to already exist -- see the same
    // comment on UnloadSucceedsCleanlyWithAPendingAutoRelockTimerArmed below.
    secondaryWallet->GenerateNewSeed();
    SecureString passphrase;
    passphrase.reserve(64);
    passphrase = "unittestpassphrase";
    ASSERT_TRUE(secondaryWallet->EncryptWallet(passphrase));

    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        UniValue params(UniValue::VARR);
        params.push_back("unittestpassphrase");
        params.push_back(3600);
        ASSERT_NO_THROW(tableRPC.execute("walletpassphrase", params));
    }

    EXPECT_GT(GetWalletUnlockTimeForRequest(secondaryWallet), 0)
        << "the unlocked secondary wallet's own deadline must be visible via its own CWallet*";
    EXPECT_EQ(0, GetWalletUnlockTimeForRequest(pwalletMain))
        << "the default wallet (never unlocked here) must not pick up the secondary wallet's deadline";

    std::string strError;
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondarytestwallet", strError)) << strError;
}

TEST_F(MultiWalletDispatchTest, UnloadSucceedsCleanlyWithAPendingAutoRelockTimerArmed)
{
    // Regression test for a use-after-free an earlier version of this change
    // introduced: walletpassphrase arms a process-wide RPCRunLater timer;
    // UnloadWallet() must call CancelWalletAutoLockTimer() before deleting
    // the wallet, or that timer stays armed to run against freed state
    // whenever its deadline (up to ~100,000,000s out) is reached.
    //
    // RPCRunLater() throws unless some RPCTimerInterface is registered --
    // normally httprpc.cpp's libevent-backed one, set up during real node
    // startup, which this gtest binary never runs. This stand-in doubles as
    // the actual proof the cancellation ran: RPCCancelRunLater() erases the
    // timer's owning shared_ptr, destroying the RPCTimerBase object, so a
    // destructor-set flag is a direct, non-flaky observation -- no need to
    // wait out the real deadline or introspect RPCRunLater's internal map.
    // static, not a stack local: FlaggingRPCTimer's destructor can in
    // principle still run after this test function's frame is gone (e.g. if
    // some later fixture teardown drops the last reference to a timer this
    // test armed), and a bool* pointing into an unwound stack frame would
    // make that a write into freed stack space instead of a harmless no-op
    // write to static storage. Reset explicitly since static storage
    // persists across repeated runs of this test (--gtest_repeat).
    static bool timerWasDestroyed;
    timerWasDestroyed = false;
    class FlaggingRPCTimer : public RPCTimerBase {
    public:
        explicit FlaggingRPCTimer(bool* flag) : destroyedFlag(flag) {}
        ~FlaggingRPCTimer() override { *destroyedFlag = true; }
    private:
        bool* destroyedFlag;
    };
    class FlaggingRPCTimerInterface : public RPCTimerInterface {
    public:
        explicit FlaggingRPCTimerInterface(bool* flag) : destroyedFlag(flag) {}
        const char* Name() override { return "FlaggingTestTimer"; }
        RPCTimerBase* NewTimer(boost::function<void(void)>& func, int64_t millis) override {
            // Captured on the interface itself, not just handed to the
            // RPCTimerBase this returns: RPCCancelRunLater()/StopRPC() erase
            // and destroy that object well before its deadline, but this
            // test still needs to be able to invoke the real callback
            // afterwards to prove LockWallet()'s post-unload branch is safe
            // -- exactly the scenario of a timer that already fired (or was
            // about to) racing against the unload that cancels it.
            lastFunc = func;
            return new FlaggingRPCTimer(destroyedFlag);
        }
        boost::function<void(void)> lastFunc;
    private:
        bool* destroyedFlag;
    };
    FlaggingRPCTimerInterface timerInterface(&timerWasDestroyed);
    RPCRegisterTimerInterface(&timerInterface);
    struct TimerInterfaceGuard {
        RPCTimerInterface* iface;
        ~TimerInterfaceGuard() { RPCUnregisterTimerInterface(iface); }
    } timerGuard{&timerInterface};

    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondarytestwallet");
    ASSERT_NE(nullptr, secondaryWallet);

    // EncryptWallet() requires an HD seed to already exist (it derives a
    // salt fingerprint from it) -- CreateWalletFileOnDisk's plain LoadWallet()
    // never generates one (that normally only happens via the full node
    // startup/first-run flow), so this test wallet needs one made explicitly.
    secondaryWallet->GenerateNewSeed();

    SecureString passphrase;
    passphrase.reserve(64);
    passphrase = "unittestpassphrase";
    ASSERT_TRUE(secondaryWallet->EncryptWallet(passphrase));

    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        UniValue params(UniValue::VARR);
        params.push_back("unittestpassphrase");
        params.push_back(3600);
        ASSERT_NO_THROW(tableRPC.execute("walletpassphrase", params));

        UniValue info;
        ASSERT_NO_THROW(info = tableRPC.execute("getwalletinfo", UniValue(UniValue::VARR)));
        EXPECT_GT(find_value(info, "unlocked_until").get_int64(), 0);
    }
    ASSERT_FALSE(timerWasDestroyed) << "the timer should still be live going into the unload below";

    // Grab the actual callback LockWallet() was armed with -- not just proof
    // that *some* timer object got destroyed, but the real closure a fired
    // timer thread would be holding right as the unload below runs.
    boost::function<void(void)> raceFunc = timerInterface.lastFunc;
    ASSERT_TRUE(static_cast<bool>(raceFunc));

    std::string strError;
    EXPECT_TRUE(CWalletManager::Get().UnloadWallet("secondarytestwallet", strError)) << strError;

    // The actual regression check: without CancelWalletAutoLockTimer()
    // running before the delete, this timer -- and the raw pointer bound
    // into it -- would still be sitting in RPCRunLater's queue right now.
    EXPECT_TRUE(timerWasDestroyed);

    // The residual race this specific redesign (name+generation, re-resolved
    // through ResolveAndHoldForRequest at fire time, rather than a raw
    // CWallet*) targets: a callback already in a timer thread's hands before
    // the cancel/delete above ran must still be safe to invoke afterwards.
    // It should resolve to NotFound and return -- not dereference the now-
    // freed CWallet. This is the one branch of LockWallet() that matters
    // most from a safety standpoint, and unlike the destroyedFlag check
    // above, this actually calls into LockWallet()'s real logic rather than
    // just observing that RPCCancelRunLater() ran.
    //
    // Caveat: ASSERT_NO_THROW alone is a weak regression detector for the
    // specific bug this guards against (a raw CWallet* capture instead of
    // name+generation) -- a freshly freed heap block is usually still
    // readable, so a reintroduced raw-pointer version would likely still run
    // to completion here without a sanitizer. This check is real evidence
    // under ASAN/valgrind; run this test binary under one periodically
    // rather than trusting a green run here alone to catch that class of bug.
    ASSERT_NO_THROW(raceFunc());
}
// LockWallet()'s generation-mismatch branch (a name reloaded as a different
// CWallet before a stale timer fires) is intentionally not additionally
// exercised here: constructing it needs an unload+reload of a wallet whose
// file was just put through CWallet::EncryptWallet()'s CDB::Rewrite() in
// this same live environment, which runs into the exact hazard documented
// on CWalletManager::LoadWallet()'s fAlreadyTouched check (walletmanager.cpp)
// -- not something specific to LockWallet. The mismatch logic itself
// (ResolveAndHoldForRequest()/ReleaseRefIfCurrent() correctly handling a
// stale generation without underflowing the new entry's refcount) already
// has direct coverage in wallet/gtest/test_walletmanager.cpp,
// ReleaseAfterUnloadAndReloadUnderSameNameDoesNotUnderflowTheNewEntry.

TEST_F(MultiWalletDispatchTest, AsyncOperationStatusIsScopedToTheRequestingWallet)
{
    // Phase 3: z_getoperationstatus/z_getoperationresult/z_listoperationids
    // now filter by the requesting wallet (OperationBelongsToWallet(),
    // rpcwallet.cpp) instead of returning every operation in the process.
    // getAsyncRPCQueue() is a process-wide singleton that outlives any one
    // test, so this checks *membership* of specific known operation ids
    // rather than exact array sizes -- other tests in this binary may have
    // left their own (possibly still-pending) operations in the same queue.
    CWallet* secondaryWallet = CWalletManager::Get().GetWallet("secondarytestwallet");
    ASSERT_NE(nullptr, secondaryWallet);

    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    // getAsyncRPCQueue() is a one-way-closable singleton (see AsyncRPCQueue::
    // close(), asyncrpcqueue.h) shared with every other test in this binary.
    // addOperation() silently no-ops once closed, which would otherwise turn
    // into two confusing containsId() failures below instead of a clear
    // diagnosis -- fail loudly here if some other test in the binary ever
    // closes it (rpc_wallet_tests_bitcoin.rpc_z_getoperations in
    // test_rpc_wallet_bitcoin.cpp used to be exactly that; it no longer
    // calls close() as of this comment, for the same reason).
    ASSERT_FALSE(q->isClosed());
    auto secondaryOp = std::make_shared<AsyncRPCOperation>(secondaryWallet);
    q->addOperation(secondaryOp);
    auto defaultOp = std::make_shared<AsyncRPCOperation>(pwalletMain);
    q->addOperation(defaultOp);
    AsyncRPCOperationId secondaryOpId = secondaryOp->getId();
    AsyncRPCOperationId defaultOpId = defaultOp->getId();

    // getAsyncRPCQueue() is process-wide and outlives this test. Pop both
    // from operation_map_ once done (runs even if an ASSERT_* below returns
    // early) so a leftover entry never shows up in some later test's
    // wallet-scoped z_listoperationids/z_getoperationstatus results --
    // "secondarytestwallet" in particular is a name several other
    // MultiWalletDispatchTest cases reuse. This deliberately does NOT try
    // to drain the two ids out of AsyncRPCQueue's separate pending-work
    // queue (no public API removes an id from there without running it,
    // and adding a worker here to force that would itself pollute
    // rpc_wallet_tests_bitcoin.rpc_z_getoperations's own worker-count
    // assertions in the same binary -- observed as a real, order-dependent
    // failure when this was tried). A stale id left in that queue after
    // being popped from the map is already the documented, handled case in
    // AsyncRPCQueue::run() (falls through as "operation not found").
    struct QueueCleanup {
        std::shared_ptr<AsyncRPCQueue> q;
        AsyncRPCOperationId id1, id2;
        ~QueueCleanup() { q->popOperationForId(id1); q->popOperationForId(id2); }
    } cleanup{q, secondaryOpId, defaultOpId};

    auto containsId = [](const UniValue& arr, const AsyncRPCOperationId& id) {
        for (const UniValue& v : arr.getValues()) {
            if (v.get_str() == id)
                return true;
        }
        return false;
    };

    UniValue secondaryIds;
    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        ASSERT_NO_THROW(secondaryIds = tableRPC.execute("z_listoperationids", UniValue(UniValue::VARR)));
    }
    EXPECT_TRUE(containsId(secondaryIds, secondaryOpId));
    EXPECT_FALSE(containsId(secondaryIds, defaultOpId));

    UniValue defaultIds;
    ASSERT_NO_THROW(defaultIds = tableRPC.execute("z_listoperationids", UniValue(UniValue::VARR)));
    EXPECT_TRUE(containsId(defaultIds, defaultOpId));
    EXPECT_FALSE(containsId(defaultIds, secondaryOpId));

    // z_getoperationstatus (no id filter) goes through the same
    // OperationBelongsToWallet() check as z_listoperationids -- confirm it
    // too, since it's a separate code path (z_getoperationstatus_IMPL)
    // rather than a shared helper the two RPCs both call.
    UniValue secondaryStatuses;
    {
        RPCWalletRequestGuard guard("secondarytestwallet");
        ASSERT_NO_THROW(secondaryStatuses = tableRPC.execute("z_getoperationstatus", UniValue(UniValue::VARR)));
    }
    bool foundSecondaryOwn = false, foundDefaultForeign = false;
    for (const UniValue& obj : secondaryStatuses.getValues()) {
        std::string id = find_value(obj, "id").get_str();
        if (id == secondaryOpId) foundSecondaryOwn = true;
        if (id == defaultOpId) foundDefaultForeign = true;
    }
    EXPECT_TRUE(foundSecondaryOwn);
    EXPECT_FALSE(foundDefaultForeign);
}
#endif // ENABLE_WALLET
