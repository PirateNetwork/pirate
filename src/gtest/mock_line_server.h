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
// conversation for one test case.
//
// Linux-only (raw POSIX sockets) - matches this suite's existing scope, since
// `make check` (which runs pirate-gtest) is only ever invoked on the Linux CI
// job (see .github/workflows/pirate_build_all.yml's "Run tests (Linux)" step).

#include <netaddress.h>
#include <netbase.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>

/** Read one '\n'-terminated line from fd, without the trailing newline. Returns false on EOF/error. */
inline bool MockServerReadLine(int fd, std::string& out)
{
    out.clear();
    char c;
    while (true) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) return !out.empty(); // treat EOF after partial data as a short line
        if (c == '\n') return true;
        out.push_back(c);
    }
}

/** Write `line` followed by '\n' to fd. */
inline void MockServerWriteLine(int fd, const std::string& line)
{
    std::string withNewline = line + "\n";
    size_t sent = 0;
    while (sent < withNewline.size()) {
        ssize_t n = write(fd, withNewline.data() + sent, withNewline.size() - sent);
        if (n <= 0) throw std::runtime_error("MockServerWriteLine: write failed");
        sent += static_cast<size_t>(n);
    }
}

class MockLineServer
{
public:
    /** Handler runs on a background thread with the accepted client fd; it owns the whole conversation. */
    using Handler = std::function<void(int)>;

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
        m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_fd < 0) throw std::runtime_error("MockLineServer: socket() failed");

        int one = 1;
        setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0; // ask the OS for a free port

        if (bind(m_listen_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            throw std::runtime_error("MockLineServer: bind() failed");
        }
        if (listen(m_listen_fd, 8) != 0) {
            throw std::runtime_error("MockLineServer: listen() failed");
        }

        socklen_t addrlen = sizeof(addr);
        if (getsockname(m_listen_fd, (struct sockaddr*)&addr, &addrlen) != 0) {
            throw std::runtime_error("MockLineServer: getsockname() failed");
        }

        m_thread = std::thread([this, handler]() {
            while (true) {
                int client_fd = accept(m_listen_fd, nullptr, nullptr);
                if (client_fd < 0) break; // listening socket was closed
                if (m_stopping) {
                    // Stop()'s own unblocking connection, not a real client.
                    close(client_fd);
                    break;
                }
                handler(client_fd);
                close(client_fd);
            }
        });

        return CService(CNetAddr(addr.sin_addr), ntohs(addr.sin_port));
    }

    /** Close the listening socket and join the background thread. Safe to call more than once. */
    void Stop()
    {
        if (m_listen_fd < 0) {
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
        if (getsockname(m_listen_fd, (struct sockaddr*)&addr, &addrlen) == 0) {
            int poke_fd = socket(AF_INET, SOCK_STREAM, 0);
            if (poke_fd >= 0) {
                connect(poke_fd, (struct sockaddr*)&addr, addrlen);
                close(poke_fd);
            }
        }

        if (m_thread.joinable()) {
            m_thread.join();
        }

        close(m_listen_fd);
        m_listen_fd = -1;
    }

private:
    int m_listen_fd{-1};
    std::atomic<bool> m_stopping{false};
    std::thread m_thread;
};
