// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

// A minimal single-connection, line-oriented TCP server for testing local
// control-protocol clients (I2P SAM, Tor control port, ...) without a real
// I2P router / Tor daemon running. Both of those protocols are plaintext,
// newline-terminated request/reply exchanges over a loopback TCP socket, so
// one small harness covers both: bind 127.0.0.1 on an OS-assigned port,
// accept exactly one connection on a background thread, and hand it to a
// caller-supplied handler that reads/writes lines to drive the scripted
// conversation for one test case. It uses the node's cross-platform socket
// wrapper so the test binary can be built on every supported CI platform.

#include <netaddress.h>
#include <netbase.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

/** Read one '\n'-terminated line, without the trailing newline. Returns false on EOF/error. */
inline bool MockServerReadLine(const Sock& sock, std::string& out)
{
    out.clear();
    char c;
    while (true) {
        const ssize_t n = sock.Recv(&c, 1, 0);
        if (n <= 0) return !out.empty(); // treat EOF after partial data as a short line
        if (c == '\n') return true;
        out.push_back(c);
    }
}

/** Write `line` followed by '\n'. */
inline void MockServerWriteLine(const Sock& sock, const std::string& line)
{
    std::string withNewline = line + "\n";
    size_t sent = 0;
    while (sent < withNewline.size()) {
        const ssize_t n = sock.Send(withNewline.data() + sent, withNewline.size() - sent, 0);
        if (n <= 0) throw std::runtime_error("MockServerWriteLine: write failed");
        sent += static_cast<size_t>(n);
    }
}

class MockLineServer
{
public:
    /** Handler runs on a background thread with the accepted client socket; it owns the conversation. */
    using Handler = std::function<void(const Sock&)>;

    MockLineServer() = default;

    ~MockLineServer()
    {
        Stop();
    }

    MockLineServer(const MockLineServer&) = delete;
    MockLineServer& operator=(const MockLineServer&) = delete;

    /**
     * Start listening on 127.0.0.1:<os-assigned port> and spawn a background
     * thread that accepts connections one at a time, in a loop, running
     * `handler` on each in turn until Stop() is called. Protocols like SAM
     * and the Tor control port open a fresh connection per logical operation
     * (e.g. i2p::sam::Session::Hello() reconnects for every Connect()/
     * Listen()/StreamAccept() call), so a real test driving one of these
     * classes typically needs more than one accepted connection.
     * @return the address the client under test should connect to.
     */
    CService Start(Handler handler)
    {
        m_stopping = false;
        m_listen_socket = Sock(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (m_listen_socket.Get() == INVALID_SOCKET) {
            throw std::runtime_error("MockLineServer: socket() failed");
        }

        int one = 1;
        ::setsockopt(m_listen_socket.Get(), SOL_SOCKET, SO_REUSEADDR,
                     (sockopt_arg_type)&one, sizeof(one));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ask the OS for a free port

        if (::bind(m_listen_socket.Get(), (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            throw std::runtime_error("MockLineServer: bind() failed");
        }
        if (::listen(m_listen_socket.Get(), 8) == SOCKET_ERROR) {
            throw std::runtime_error("MockLineServer: listen() failed");
        }

        socklen_t addrlen = sizeof(addr);
        if (::getsockname(m_listen_socket.Get(), (struct sockaddr*)&addr, &addrlen) == SOCKET_ERROR) {
            throw std::runtime_error("MockLineServer: getsockname() failed");
        }

        m_thread = std::thread([this, handler]() {
            while (true) {
                Sock client_socket(::accept(m_listen_socket.Get(), nullptr, nullptr));
                if (client_socket.Get() == INVALID_SOCKET) break; // listening socket was closed
                if (m_stopping) {
                    // Stop()'s own unblocking connection, not a real client.
                    break;
                }
                handler(client_socket);
            }
        });

        return CService(CNetAddr(addr.sin_addr), ntohs(addr.sin_port));
    }

    /** Close the listening socket and join the background thread. Safe to call more than once. */
    void Stop()
    {
        if (m_listen_socket.Get() == INVALID_SOCKET) {
            if (m_thread.joinable()) m_thread.join();
            return;
        }

        m_stopping = true;

        // The background thread may be parked in accept() waiting for a
        // connection that will never come. Closing the listening socket from
        // this (different) thread is not guaranteed to unblock accept() on
        // Linux, so connect to ourselves to hand it something to return from.
        struct sockaddr_in addr;
        socklen_t addrlen = sizeof(addr);
        if (::getsockname(m_listen_socket.Get(), (struct sockaddr*)&addr, &addrlen) == 0) {
            Sock poke_socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
            if (poke_socket.Get() != INVALID_SOCKET) {
                poke_socket.Connect((struct sockaddr*)&addr, addrlen);
            }
        }

        if (m_thread.joinable()) {
            m_thread.join();
        }

        m_listen_socket.Reset();
    }

private:
    Sock m_listen_socket;
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};
