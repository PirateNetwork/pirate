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

        i2p::sam::Session session(
            GetTempPath() / boost::filesystem::unique_path("i2p_privkey-%%%%.dat"),
            samAddr);

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

// Basic happy-path coverage: session creation succeeds against a scripted SAM
// proxy, and the private key is persisted to disk and reused (no second
// DEST GENERATE) by a fresh Session pointed at the same key file.
TEST_F(i2p_tests, SessionCreatePersistsAndReusesPrivateKey)
{
    const std::vector<unsigned char> privKey = MakeFakePrivateKey();
    const std::string privKeyB64 = ToI2PBase64(privKey);
    const fs::path keyFile = GetTempPath() / boost::filesystem::unique_path("i2p_privkey-%%%%.dat");

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
