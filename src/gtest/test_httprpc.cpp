// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "httprpc.cpp"
#include "httpserver.h"

#ifdef ENABLE_WALLET
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

        CWalletManager::Get().RegisterDefaultWallet("default_test.dat", new CWallet("default_test.dat"));

        bool fFirstRun;
        CWallet scratch("secondarytestwallet");
        ASSERT_EQ(DB_LOAD_OK, scratch.LoadWallet(fFirstRun));

        std::string strError;
        ASSERT_TRUE(CWalletManager::Get().LoadWallet("secondarytestwallet", strError)) << strError;
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
    RPCWalletRequestGuard guard("secondarytestwallet");
    try {
        tableRPC.execute("getbalance", UniValue(UniValue::VARR));
        FAIL() << "expected getbalance to be refused against a non-default wallet";
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
    // signrawtransaction ("rawtransactions") is registered by
    // RegisterAllCoreRPCCommands in SetUp() and reads pwalletMain when a key
    // isn't supplied in the params -- the gate must fire before that ever runs.
    RPCWalletRequestGuard guard("secondarytestwallet");
    try {
        tableRPC.execute("signrawtransaction", UniValue(UniValue::VARR));
        FAIL() << "expected signrawtransaction to be refused against a non-default wallet";
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
        EXPECT_THROW(tableRPC.execute("getbalance", UniValue(UniValue::VARR)), UniValue);
    }
    // The guard's destructor must clear the thread-local even though execute()
    // threw -- otherwise a later, unrelated request reusing this pooled HTTP
    // worker thread would silently inherit this request's wallet selection.
    EXPECT_TRUE(CWalletManager::GetRequestedWalletName().empty());
    EXPECT_NO_THROW(tableRPC.execute("listwallets", UniValue(UniValue::VARR)));
}
#endif // ENABLE_WALLET
