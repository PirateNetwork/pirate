// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "gtest/gtestutils.h"
#include "gtest/mock_line_server.h"

#include <i2p.h>
#include <netaddress.h>
#include <netbase.h>
#include <util.h>
#include <util/strencodings.h>

#include <gtest/gtest.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

// Exercises i2p::sam::Session against a scripted fake SAM proxy
// (MockLineServer, gtest/mock_line_server.h) instead of a real I2P router -
// there was previously zero test coverage of this class. Written to
// regression-test the Session::Connect() result-string misclassification bug
// found during a networking privacy review: a bare `"KEY_NOT_FOUND"` string
// literal instead of `result == "KEY_NOT_FOUND"` is always truthy in C++, so
// `proxy_error` was unconditionally forced to false for every connect
// failure, hiding genuine SAM-proxy/router-level breakage behind a
// "peer just unreachable" classification. See also
// net_tests_bitcoin.GetReachabilityFrom_I2PAndCJDNSNotOutscoredByIPv4 in
// test_net_bitcoin.cpp for the other fix from the same review.

class i2p_tests : public BitcoinBasicTestingSetup {};

namespace {

// Standard-Base64 <-> I2P-Base64 alphabet swap ('+'/'/' <-> '-'/'~'), mirrors
// i2p.cpp's file-local SwapBase64(). Duplicated here since that helper isn't
// exported and isn't worth exposing production API surface for a 4-line swap.
std::string ToI2PBase64(const std::vector<unsigned char>& raw)
{
    std::string b64 = EncodeBase64(raw.data(), raw.size());
    for (char& c : b64) {
        if (c == '+') c = '-';
        else if (c == '/') c = '~';
    }
    return b64;
}

// A syntactically-valid (but semantically arbitrary) 387-byte I2P destination
// private-key blob. Session::MyDestination() only cares that the big-endian
// uint16 at byte offset 385 is a zero certificate length, so the derived
// dest_len is exactly 387 and the whole buffer is fed to DestBinToAddr()
// (which just SHA256-hashes it) - the key material doesn't need to be
// cryptographically real for this to round-trip through Session.
std::vector<unsigned char> MakeFakePrivateKey()
{
    std::vector<unsigned char> key(387, 0xAB);
    key[385] = 0x00; // cert length, high byte
    key[386] = 0x00; // cert length, low byte
    return key;
}

bool StartsWith(const std::string& s, const std::string& prefix)
{
    return s.rfind(prefix, 0) == 0;
}

// A private-key file path in the temp dir that removes the file again when the test finishes.
//
// The path needs real randomness: these tests used a 4-hex-digit random suffix (16 bits) and never
// deleted the file, so leftovers from earlier runs piled up (thousands, over weeks) until a fresh
// name occasionally hit an existing one. Session then loads that key instead of asking the proxy
// for DEST GENERATE, the scripted SAM conversation no longer matches, and the test fails
// intermittently - more often the longer the tree has been used.
class TempKeyFile
{
public:
    TempKeyFile()
        : m_path(GetTempPath() / boost::filesystem::unique_path("i2p_privkey-%%%%%%%%-%%%%%%%%-%%%%%%%%-%%%%%%%%.dat"))
    {
    }
    ~TempKeyFile()
    {
        boost::system::error_code ec;
        boost::filesystem::remove(m_path, ec);
    }
    TempKeyFile(const TempKeyFile&) = delete;
    TempKeyFile& operator=(const TempKeyFile&) = delete;
    const fs::path& Path() const { return m_path; }

private:
    fs::path m_path;
};

// Thread-safe line log: the MockLineServer handler runs on a background
// thread, and gtest assertion macros are not guaranteed safe to call there
// across all gtest versions - so the handler only records what it saw/did,
// and every EXPECT_/ASSERT_ call happens on the main test thread after
// server.Stop() has joined that background thread.
class LineLog
{
public:
    void Push(const std::string& line)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_lines.push_back(line);
    }
    std::vector<std::string> Get() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_lines;
    }
private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_lines;
};

} // namespace

// Regression test for the exact bug fixed in i2p.cpp's Session::Connect():
// CANT_REACH_PEER/TIMEOUT/KEY_NOT_FOUND must classify as "peer problem"
// (proxy_error == false); anything else must classify as a proxy/router
// problem (proxy_error == true). Before the fix, every case set
// proxy_error == false unconditionally.
TEST_F(i2p_tests, SessionConnectDistinguishesProxyVsPeerErrors)
{
    struct Case {
        std::string result;
        bool expectProxyError;
    };
    const std::vector<Case> cases{
        {"CANT_REACH_PEER", false},
        {"TIMEOUT", false},
        {"KEY_NOT_FOUND", false},
        {"I2P_ERROR", true}, // anything else: a real proxy/router-level problem
    };

    const std::vector<unsigned char> privKey = MakeFakePrivateKey();
    const std::string privKeyB64 = ToI2PBase64(privKey);

    for (const Case& c : cases) {
        SCOPED_TRACE(c.result);

        std::atomic<int> connectionIndex{0};
        LineLog log;
        MockLineServer server;

        CService samAddr = server.Start([&](const Sock& sock) {
            const int idx = connectionIndex++;
            std::string line;

            if (idx == 0) {
                // Session::CreateIfNotCreatedAlready(): HELLO, DEST GENERATE, SESSION CREATE.
                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "HELLO REPLY RESULT=OK VERSION=3.1");

                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "DEST REPLY PUB=unused PRIV=" + privKeyB64);

                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "SESSION STATUS RESULT=OK DESTINATION=" + privKeyB64);
            } else {
                // Session::Connect(): a fresh Hello(), then NAMING LOOKUP, then STREAM CONNECT.
                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "HELLO REPLY RESULT=OK VERSION=3.1");

                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "NAMING REPLY RESULT=OK VALUE=" + privKeyB64);

                if (!MockServerReadLine(sock, line)) return;
                log.Push(line);
                MockServerWriteLine(sock, "STREAM STATUS RESULT=" + c.result);
            }
        });

        TempKeyFile keyFile;
        i2p::sam::Session session(keyFile.Path(), samAddr);

        CNetAddr peerAddr;
        ASSERT_TRUE(peerAddr.SetSpecial(
            "ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p"));
        CService peer(peerAddr, 0);

        i2p::Connection conn;
        bool proxyError = false;
        const bool connected = session.Connect(peer, conn, proxyError);

        server.Stop();

        EXPECT_FALSE(connected);
        EXPECT_EQ(proxyError, c.expectProxyError);

        const std::vector<std::string> lines = log.Get();
        ASSERT_EQ(lines.size(), 6u);
        EXPECT_EQ(lines[0], "HELLO VERSION MIN=3.1 MAX=3.1");
        EXPECT_EQ(lines[1], "DEST GENERATE SIGNATURE_TYPE=7");
        EXPECT_TRUE(StartsWith(lines[2], "SESSION CREATE STYLE=STREAM"));
        EXPECT_EQ(lines[3], "HELLO VERSION MIN=3.1 MAX=3.1");
        EXPECT_TRUE(StartsWith(lines[4], "NAMING LOOKUP NAME="));
        EXPECT_TRUE(StartsWith(lines[5], "STREAM CONNECT ID="));
    }
}

// Session::Connect()'s optional router_unreachable out-param tells "the I2P router itself
// can't be reached / won't complete the HELLO handshake" apart from "the router answered and
// something after that failed" (a rejected SESSION CREATE, an unknown destination, ...). proxy_error can't do that job: it is true for anything except a
// STREAM CONNECT that reports an unreachable peer, so a NAMING LOOKUP for an unknown name leaves
// it true even though the router answered. The node's dial backoff is keyed on
// router_unreachable so a flood of stale or fake I2P addresses can't suspend all I2P dialing
// while a healthy router is running.
TEST_F(i2p_tests, SessionConnectReportsRouterUnreachableOnlyWhenRouterUnreachable)
{
    enum class Failure {
        NO_ROUTER,
        HELLO_REFUSED,
        SESSION_CREATE_REJECTED,
        LOOKUP_KEY_NOT_FOUND,
        STREAM_CANT_REACH_PEER,
        STREAM_I2P_ERROR,
    };
    struct Case {
        Failure failure;
        bool expectRouterUnreachable;
        bool expectProxyError;
    };
    const std::vector<Case> cases{
        // Nothing listening on the SAM port: exactly what a missing router looks like.
        {Failure::NO_ROUTER, true, true},
        // Something answers on the SAM port but won't complete the handshake.
        {Failure::HELLO_REFUSED, true, true},
        // The router completed HELLO and then rejected SESSION CREATE (duplicate destination,
        // tunnel limits, ...): it is up, so this must not look like a dead router - one identity's
        // session trouble would otherwise pause dialing for every identity.
        {Failure::SESSION_CREATE_REJECTED, false, true},
        // Router answered HELLO, then doesn't know the name: proxy_error stays true (unchanged
        // behavior) but the router is demonstrably up.
        {Failure::LOOKUP_KEY_NOT_FOUND, false, true},
        {Failure::STREAM_CANT_REACH_PEER, false, false},
        {Failure::STREAM_I2P_ERROR, false, true},
    };

    const std::vector<unsigned char> privKey = MakeFakePrivateKey();
    const std::string privKeyB64 = ToI2PBase64(privKey);

    for (const Case& c : cases) {
        SCOPED_TRACE(static_cast<int>(c.failure));

        std::atomic<int> connectionIndex{0};
        MockLineServer server;

        CService samAddr = server.Start([&](const Sock& sock) {
            const int idx = connectionIndex++;
            std::string line;

            if (idx == 0) {
                // Session::CreateIfNotCreatedAlready(): HELLO, DEST GENERATE, SESSION CREATE.
                if (!MockServerReadLine(sock, line)) return;
                if (c.failure == Failure::HELLO_REFUSED) {
                    MockServerWriteLine(sock, "HELLO REPLY RESULT=NOVERSION");
                    return;
                }
                MockServerWriteLine(sock, "HELLO REPLY RESULT=OK VERSION=3.1");
                if (!MockServerReadLine(sock, line)) return;
                MockServerWriteLine(sock, "DEST REPLY PUB=unused PRIV=" + privKeyB64);
                if (!MockServerReadLine(sock, line)) return;
                if (c.failure == Failure::SESSION_CREATE_REJECTED) {
                    MockServerWriteLine(sock, "SESSION STATUS RESULT=I2P_ERROR MESSAGE=rejected");
                    return;
                }
                MockServerWriteLine(sock, "SESSION STATUS RESULT=OK DESTINATION=" + privKeyB64);
            } else {
                // Session::Connect(): a fresh HELLO, then NAMING LOOKUP, then STREAM CONNECT.
                if (!MockServerReadLine(sock, line)) return;
                MockServerWriteLine(sock, "HELLO REPLY RESULT=OK VERSION=3.1");
                if (!MockServerReadLine(sock, line)) return;
                if (c.failure == Failure::LOOKUP_KEY_NOT_FOUND) {
                    MockServerWriteLine(sock, "NAMING REPLY RESULT=KEY_NOT_FOUND");
                    return;
                }
                MockServerWriteLine(sock, "NAMING REPLY RESULT=OK VALUE=" + privKeyB64);
                if (!MockServerReadLine(sock, line)) return;
                MockServerWriteLine(sock, std::string("STREAM STATUS RESULT=") +
                    (c.failure == Failure::STREAM_CANT_REACH_PEER ? "CANT_REACH_PEER" : "I2P_ERROR"));
            }
        });
        if (c.failure == Failure::NO_ROUTER)
            server.Stop(); // the port is now closed; connecting to it is refused

        TempKeyFile keyFile;
        i2p::sam::Session session(keyFile.Path(), samAddr);

        CNetAddr peerAddr;
        ASSERT_TRUE(peerAddr.SetSpecial(
            "ukeu3k5oycgaauneqgtnvselmt4yemvoilkln7jpvamvfx7dnkdq.b32.i2p"));
        CService peer(peerAddr, 0);

        i2p::Connection conn;
        bool proxyError = false;
        bool routerUnreachable = !c.expectRouterUnreachable; // must be overwritten either way
        const bool connected = session.Connect(peer, conn, proxyError, &routerUnreachable);

        server.Stop();

        EXPECT_FALSE(connected);
        EXPECT_EQ(routerUnreachable, c.expectRouterUnreachable);
        EXPECT_EQ(proxyError, c.expectProxyError);
    }
}

// Basic happy-path coverage: session creation succeeds against a scripted SAM
// proxy, and the private key is persisted to disk and reused (no second
// DEST GENERATE) by a fresh Session pointed at the same key file.
TEST_F(i2p_tests, SessionCreatePersistsAndReusesPrivateKey)
{
    const std::vector<unsigned char> privKey = MakeFakePrivateKey();
    const std::string privKeyB64 = ToI2PBase64(privKey);
    TempKeyFile tempKeyFile;
    const fs::path keyFile = tempKeyFile.Path();

    auto handlerFor = [&](bool expectDestGenerate, LineLog* log) {
        return [&privKeyB64, expectDestGenerate, log](const Sock& sock) {
            std::string line;
            if (!MockServerReadLine(sock, line)) return;
            log->Push(line);
            MockServerWriteLine(sock, "HELLO REPLY RESULT=OK VERSION=3.1");

            if (expectDestGenerate) {
                if (!MockServerReadLine(sock, line)) return;
                log->Push(line);
                MockServerWriteLine(sock, "DEST REPLY PUB=unused PRIV=" + privKeyB64);
            }

            if (!MockServerReadLine(sock, line)) return;
            log->Push(line);
            MockServerWriteLine(sock, "SESSION STATUS RESULT=OK DESTINATION=" + privKeyB64);
        };
    };

    {
        LineLog log;
        MockLineServer server;
        CService samAddr = server.Start(handlerFor(/*expectDestGenerate=*/true, &log));

        i2p::sam::Session session(keyFile, samAddr);
        const bool ok = session.Check();

        server.Stop();

        EXPECT_TRUE(ok);
        const std::vector<std::string> lines = log.Get();
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_EQ(lines[0], "HELLO VERSION MIN=3.1 MAX=3.1");
        EXPECT_EQ(lines[1], "DEST GENERATE SIGNATURE_TYPE=7");
        EXPECT_TRUE(StartsWith(lines[2], "SESSION CREATE STYLE=STREAM"));
    }

    ASSERT_TRUE(fs::exists(keyFile));

    {
        // Fresh Session, same key file: must NOT issue DEST GENERATE again.
        LineLog log;
        MockLineServer server;
        CService samAddr = server.Start(handlerFor(/*expectDestGenerate=*/false, &log));

        i2p::sam::Session session(keyFile, samAddr);
        const bool ok = session.Check();

        server.Stop();

        EXPECT_TRUE(ok);
        const std::vector<std::string> lines = log.Get();
        ASSERT_EQ(lines.size(), 2u);
        EXPECT_EQ(lines[0], "HELLO VERSION MIN=3.1 MAX=3.1");
        EXPECT_TRUE(StartsWith(lines[1], "SESSION CREATE STYLE=STREAM"));
    }
}
