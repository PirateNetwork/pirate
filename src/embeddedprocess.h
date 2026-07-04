// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_EMBEDDEDPROCESS_H
#define BITCOIN_EMBEDDEDPROCESS_H

#include "fs.h"
#include "netaddress.h"

#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

/**
 * Manages the lifecycle of a single bundled helper daemon (e.g. tor, i2pd) run
 * as a child process of this node. Knows nothing about the protocol the
 * helper speaks; callers (see tor_process.cpp / i2pd_process.cpp) are
 * responsible for that and for deciding what "ready" means for their daemon.
 */
class CManagedProcess
{
public:
    CManagedProcess() = default;
    ~CManagedProcess();

    CManagedProcess(const CManagedProcess&) = delete;
    CManagedProcess& operator=(const CManagedProcess&) = delete;

    /** True if a child process is currently believed to be running. */
    bool IsRunning() const { return m_running; }

    /**
     * Locate a helper binary named `name` (".exe" is appended automatically on
     * Windows). Search order: `explicitPath` if non-empty, then a sibling of
     * the currently running executable, then $PATH. Returns an empty path if
     * nothing usable was found.
     *
     * If `expectedSha256Hex` is non-empty, it is checked - and only checked -
     * against the sibling-of-executable candidate, since that's the one
     * location populated by our own install-exec-hook from the exact binary
     * this build recorded the hash of; `explicitPath` and $PATH candidates are
     * understood to intentionally be a different, externally-managed binary
     * and are never hash-checked. A sibling candidate that fails verification
     * is rejected (not returned) and the search falls through to $PATH.
     */
    static fs::path FindBinary(const std::string& name, const fs::path& explicitPath,
                               const std::string& expectedSha256Hex = "");

    /**
     * Spawn `binary` with `args` (argv[0] is derived from `binary` automatically,
     * do not include it in `args`). The child's stdout/stderr are redirected to
     * `stdoutLog`/`stderrLog` (each truncated before use). Returns false on failure.
     */
    bool Spawn(const fs::path& binary, const std::vector<std::string>& args,
               const fs::path& stdoutLog, const fs::path& stderrLog);

    /**
     * Poll `target` with plain (proxy-bypassing) TCP connect attempts until one
     * succeeds or `timeoutMs` elapses. Returns true once a connection succeeds.
     */
    bool WaitUntilReady(const CService& target, int timeoutMs) const;

    /**
     * Ask the child to exit gracefully, wait up to `termWaitMs`, then forcefully
     * terminate it if it hasn't exited by then. No-op if not currently running.
     */
    void Stop(int termWaitMs);

private:
    bool m_running{false};
#ifdef _WIN32
    PROCESS_INFORMATION m_procInfo{};
#else
    pid_t m_pid{-1};
#endif
};

/**
 * True if nothing is currently bound to `target`. This is a plain bind()
 * probe, not a connect() probe: the question is whether *we* could take this
 * address, not whether some listener (ours or someone else's) answers there.
 */
bool IsPortAvailable(const CService& target);

/**
 * Return `preferred` if `bindIp:preferred` is available, otherwise ask the OS
 * for a free ephemeral port on `bindIp` and return that instead. Falls back
 * to returning `preferred` unchanged if the OS probe itself fails for some
 * reason - the caller's own subsequent bind attempt will surface any real
 * problem.
 */
uint16_t PickAvailablePort(const std::string& bindIp, uint16_t preferred);

/**
 * True if `path`'s string form contains no characters (newline, carriage
 * return) that could break out of a single line if embedded verbatim into a
 * generated line-oriented config file (torrc, i2pd.conf).
 */
bool PathIsSafeForConfig(const fs::path& path);

#endif // BITCOIN_EMBEDDEDPROCESS_H
