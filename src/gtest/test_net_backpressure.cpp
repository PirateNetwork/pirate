// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <gtest/gtest.h>

#include "chainparams.h"
#include "crypto/common.h"
#include "gtest/gtestutils.h"
#include "hash.h"
#include "net.h"
#include "netbase.h"
#include "tls/tlsmanager.h"
#include "util/sock.h"

#include <openssl/rsa.h>

#include <memory>
#include <string>
#include <vector>

namespace {

std::vector<char> WireMessage(size_t payloadSize)
{
    std::vector<char> payload(payloadSize, 0x42);
    CMessageHeader header(Params().MessageStart(), NetMsgType::PING, payload.size());
    header.nChecksum = ReadLE32(Hash(payload.begin(), payload.end()).begin());
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << header;
    stream.write(payload.data(), payload.size());
    return std::vector<char>(stream.begin(), stream.end());
}

// A real TLS 1.3 connection carried by memory BIOs. No certificate or key is
// written to disk, and there is no external network endpoint.
class TlsPair
{
public:
    TlsPair() :
        clientContext(nullptr, SSL_CTX_free), serverContext(nullptr, SSL_CTX_free),
        client(nullptr, SSL_free), server(nullptr, SSL_free) {}

    ~TlsPair()
    {
        // CNode normally owns its SSL object. These test SSLs use memory BIOs
        // rather than its socket, so free them here without a socket shutdown.
        if (owner) owner->ssl = nullptr;
    }

    bool Initialize(CNode& node)
    {
        clientContext.reset(SSL_CTX_new(TLS_client_method()));
        serverContext.reset(SSL_CTX_new(TLS_server_method()));
        if (!clientContext || !serverContext) return false;
        if (!SSL_CTX_set_min_proto_version(clientContext.get(), TLS1_3_VERSION) ||
            !SSL_CTX_set_min_proto_version(serverContext.get(), TLS1_3_VERSION)) return false;
        SSL_CTX_set_verify(clientContext.get(), SSL_VERIFY_NONE, nullptr);

        std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> keyContext(
            EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
        EVP_PKEY* rawKey = nullptr;
        if (!keyContext || EVP_PKEY_keygen_init(keyContext.get()) <= 0 ||
            EVP_PKEY_CTX_set_rsa_keygen_bits(keyContext.get(), 2048) <= 0 ||
            EVP_PKEY_keygen(keyContext.get(), &rawKey) <= 0) return false;
        std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key(rawKey, EVP_PKEY_free);
        std::unique_ptr<X509, decltype(&X509_free)> certificate(X509_new(), X509_free);
        if (!certificate || !X509_set_version(certificate.get(), 2) ||
            !ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1) ||
            !X509_gmtime_adj(X509_get_notBefore(certificate.get()), 0) ||
            !X509_gmtime_adj(X509_get_notAfter(certificate.get()), 3600) ||
            !X509_set_pubkey(certificate.get(), key.get())) return false;
        X509_NAME* name = X509_get_subject_name(certificate.get());
        const unsigned char commonName[] = "backpressure-test";
        if (!X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, commonName, -1, -1, 0) ||
            !X509_set_issuer_name(certificate.get(), name) ||
            !X509_sign(certificate.get(), key.get(), EVP_sha256()) ||
            !SSL_CTX_use_certificate(serverContext.get(), certificate.get()) ||
            !SSL_CTX_use_PrivateKey(serverContext.get(), key.get())) return false;

        client.reset(SSL_new(clientContext.get()));
        server.reset(SSL_new(serverContext.get()));
        if (!client || !server) return false;
        BIO* clientBio = nullptr;
        BIO* serverBio = nullptr;
        if (!BIO_new_bio_pair(&clientBio, 0, &serverBio, 0)) return false;
        SSL_set_bio(client.get(), clientBio, clientBio);
        SSL_set_bio(server.get(), serverBio, serverBio);
        SSL_set_connect_state(client.get());
        SSL_set_accept_state(server.get());
        for (int i = 0; i < 100; ++i) {
            for (SSL* ssl : {client.get(), server.get()}) {
                int result = SSL_do_handshake(ssl);
                if (result <= 0) {
                    const int error = SSL_get_error(ssl, result);
                    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE) return false;
                }
            }
            if (SSL_is_init_finished(client.get()) && SSL_is_init_finished(server.get())) {
                owner = &node;
                owner->ssl = server.get();
                return true;
            }
        }
        return false;
    }

    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> clientContext;
    std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> serverContext;
    std::unique_ptr<SSL, decltype(&SSL_free)> client;
    std::unique_ptr<SSL, decltype(&SSL_free)> server;

private:
    CNode* owner = nullptr;
};

class NetBackpressureTest : public BitcoinBasicTestingSetup
{
protected:
    void SetUp() override
    {
        BitcoinBasicTestingSetup::SetUp();
        hadReceiveBuffer = mapArgs.count("-maxreceivebuffer") != 0;
        if (hadReceiveBuffer) oldReceiveBuffer = mapArgs["-maxreceivebuffer"];
        mapArgs["-maxreceivebuffer"] = "1";

        Sock listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        ASSERT_NE(INVALID_SOCKET, listener.Get());
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(0, ::bind(listener.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)));
        ASSERT_EQ(0, ::listen(listener.Get(), 1));
        socklen_t length = sizeof(address);
        ASSERT_EQ(0, ::getsockname(listener.Get(), reinterpret_cast<sockaddr*>(&address), &length));
        client = Sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        ASSERT_NE(INVALID_SOCKET, client.Get());
        ASSERT_EQ(0, ::connect(client.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)));
        Sock accepted(::accept(listener.Get(), nullptr, nullptr));
        ASSERT_NE(INVALID_SOCKET, accepted.Get());
        SOCKET rawSocket = accepted.Get();
        ASSERT_TRUE(SetSocketNonBlocking(rawSocket, true));
        rawSocket = client.Get();
        ASSERT_TRUE(SetSocketNonBlocking(rawSocket, true));
        peer = std::make_unique<CNode>(accepted.Release(), CAddress(), "backpressure-test", true);
    }

    void TearDown() override
    {
        peer.reset();
        client.Reset();
        if (hadReceiveBuffer) mapArgs["-maxreceivebuffer"] = oldReceiveBuffer;
        else mapArgs.erase("-maxreceivebuffer");
        BitcoinBasicTestingSetup::TearDown();
    }

    void FillReceiveQueue()
    {
        LOCK(peer->cs_vRecvMsg);
        peer->vRecvMsg.emplace_back(Params().MessageStart(), SER_NETWORK, PROTOCOL_VERSION);
        CNetMessage& message = peer->vRecvMsg.back();
        message.in_data = true;
        message.hdr.nMessageSize = 1024;
        message.nDataPos = 1024;
        message.vRecv.resize(1024);
        ASSERT_GT(peer->GetTotalRecvSize(), ReceiveFloodSize());
        ASSERT_FALSE(peer->CanReceiveMessages());
    }

    void SelectEvents(fd_set& recv, fd_set& send, fd_set& error)
    {
        FD_ZERO(&recv);
        FD_ZERO(&send);
        FD_ZERO(&error);
        LOCK(peer->cs_hSocket);
        peer->SetSocketSelectEvents(recv, send, error);
    }

    void Service(bool recvReady, bool sendReady, bool errorReady)
    {
        fd_set recv, send, error;
        FD_ZERO(&recv);
        FD_ZERO(&send);
        FD_ZERO(&error);
        if (recvReady) FD_SET(peer->hSocket, &recv);
        if (sendReady) FD_SET(peer->hSocket, &send);
        if (errorReady) FD_SET(peer->hSocket, &error);
        ASSERT_EQ(0, manager.threadSocketHandler(peer.get(), recv, send, error));
    }

    void SendToPeer(const std::vector<char>& wire)
    {
        ASSERT_EQ(static_cast<ssize_t>(wire.size()), client.Send(wire.data(), wire.size(), MSG_NOSIGNAL));
        WaitReadable(peer->hSocket);
    }

    void WaitReadable(SOCKET socket)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval timeout{5, 0};
        ASSERT_EQ(1, ::select(static_cast<int>(socket) + 1, &readable, nullptr, nullptr, &timeout));
    }

    std::unique_ptr<CNode> peer;
    Sock client;
    tls::TLSManager manager;

private:
    bool hadReceiveBuffer = false;
    std::string oldReceiveBuffer;
};

TEST_F(NetBackpressureTest, ExceptionInterestFollowsReceiveBackpressure)
{
    fd_set recv, send, error;
    SelectEvents(recv, send, error);
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &error));
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &send));

    FillReceiveQueue();
    SelectEvents(recv, send, error);
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &error));

    {
        LOCK(peer->cs_vRecvMsg);
        peer->vRecvMsg.clear();
    }
    SelectEvents(recv, send, error);
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &error));
}

TEST_F(NetBackpressureTest, ExceptionOnlyEventCannotReadPastReceiveLimit)
{
    FillReceiveQueue();
    const auto wire = WireMessage(8);
    SendToPeer(wire);
    Service(false, false, true);
    EXPECT_EQ(0u, peer->nRecvBytes);
    ASSERT_EQ(1u, peer->vRecvMsg.size());

    // The unread bytes remain in the socket and are consumed after unpausing.
    peer->vRecvMsg.clear();
    Service(true, false, false);
    EXPECT_EQ(wire.size(), peer->nRecvBytes);
    ASSERT_EQ(1u, peer->vRecvMsg.size());
    EXPECT_TRUE(peer->vRecvMsg.front().complete());
    EXPECT_FALSE(peer->fDisconnect);
}

TEST_F(NetBackpressureTest, StaleReadReadinessCannotReadPastReceiveLimit)
{
    FillReceiveQueue();
    SendToPeer(WireMessage(8));
    Service(true, false, false);
    EXPECT_EQ(0u, peer->nRecvBytes);
    EXPECT_EQ(1u, peer->vRecvMsg.size());
    EXPECT_FALSE(peer->fDisconnect);
}

TEST_F(NetBackpressureTest, ReceivePauseDoesNotBlockOutgoingWrites)
{
    FillReceiveQueue();
    {
        LOCK(peer->cs_vSend);
        peer->vSendMsg.emplace_back(2, 'x');
        peer->nSendSize = 2;
    }
    fd_set recv, send, error;
    SelectEvents(recv, send, error);
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &send));
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &error));
    Service(false, true, true);
    EXPECT_EQ(0u, peer->nSendSize);
    char received[2];
    WaitReadable(client.Get());
    ASSERT_EQ(2, client.Recv(received, sizeof(received), 0));
    EXPECT_EQ('x', received[0]);
    EXPECT_EQ('x', received[1]);
}

TEST_F(NetBackpressureTest, IncompleteMessageCanFinishAboveReceiveBudget)
{
    const auto wire = WireMessage(2048);
    const size_t split = CMessageHeader::HEADER_SIZE + 1024;
    {
        LOCK(peer->cs_vRecvMsg);
        ASSERT_TRUE(peer->ReceiveMsgBytes(wire.data(), split));
        ASSERT_GT(peer->GetTotalRecvSize(), ReceiveFloodSize());
        ASSERT_FALSE(peer->vRecvMsg.front().complete());
    }
    fd_set recv, send, error;
    SelectEvents(recv, send, error);
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_TRUE(FD_ISSET(peer->hSocket, &error));
    SendToPeer(std::vector<char>(wire.begin() + split, wire.end()));
    Service(true, false, false);
    ASSERT_TRUE(peer->vRecvMsg.front().complete());
    SelectEvents(recv, send, error);
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &recv));
    EXPECT_FALSE(FD_ISSET(peer->hSocket, &error));
}

TEST_F(NetBackpressureTest, TlsReceivePauseAndResumptionPreserveRecords)
{
    TlsPair tls;
    ASSERT_TRUE(tls.Initialize(*peer)) << ERR_error_string(ERR_get_error(), nullptr);
    FillReceiveQueue();
    const auto wire = WireMessage(8);
    ASSERT_EQ(static_cast<int>(wire.size()), SSL_write(tls.client.get(), wire.data(), wire.size()));
    BIO* incoming = SSL_get_rbio(tls.server.get());
    const size_t pending = BIO_ctrl_pending(incoming);
    ASSERT_GT(pending, 0u);
    Service(false, false, true);
    EXPECT_EQ(0u, peer->nRecvBytes);
    EXPECT_EQ(pending, BIO_ctrl_pending(incoming));

    peer->vRecvMsg.clear();
    Service(true, false, false);
    EXPECT_EQ(wire.size(), peer->nRecvBytes);
    ASSERT_EQ(1u, peer->vRecvMsg.size());
    EXPECT_TRUE(peer->vRecvMsg.front().complete());
    // No more TLS data is available: SSL_ERROR_WANT_READ must remain retryable.
    Service(true, false, false);
    EXPECT_EQ(wire.size(), peer->nRecvBytes);
    EXPECT_FALSE(peer->fDisconnect);
}

TEST_F(NetBackpressureTest, TlsSplitRecordRemainsRetryable)
{
    TlsPair tls;
    ASSERT_TRUE(tls.Initialize(*peer)) << ERR_error_string(ERR_get_error(), nullptr);
    const auto wire = WireMessage(8);
    ASSERT_EQ(static_cast<int>(wire.size()), SSL_write(tls.client.get(), wire.data(), wire.size()));
    BIO* incoming = SSL_get_rbio(tls.server.get());
    std::vector<char> encrypted(BIO_ctrl_pending(incoming));
    ASSERT_GT(encrypted.size(), 1u);
    ASSERT_EQ(static_cast<int>(encrypted.size()), BIO_read(incoming, encrypted.data(), encrypted.size()));
    const size_t split = encrypted.size() / 2;
    BIO* transport = SSL_get_wbio(tls.client.get());
    ASSERT_EQ(static_cast<int>(split), BIO_write(transport, encrypted.data(), split));
    Service(true, false, false);
    EXPECT_EQ(0u, peer->nRecvBytes);
    EXPECT_FALSE(peer->fDisconnect);
    ASSERT_EQ(static_cast<int>(encrypted.size() - split),
        BIO_write(transport, encrypted.data() + split, encrypted.size() - split));
    Service(true, false, false);
    EXPECT_EQ(wire.size(), peer->nRecvBytes);
    ASSERT_EQ(1u, peer->vRecvMsg.size());
    EXPECT_TRUE(peer->vRecvMsg.front().complete());
    EXPECT_FALSE(peer->fDisconnect);
}

} // namespace
