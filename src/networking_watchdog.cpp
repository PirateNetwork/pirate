// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "networking_watchdog.h"

#include "embedded_binary_hashes.h"
#include "embeddedprocess.h"
#include "fs.h"
#include "util.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

CManagedProcess g_watchdogProcess;

#ifdef _WIN32
HANDLE g_writeHandle = NULL;
#else
int g_writeFd = -1;
#endif

fs::path WatchdogDataDir()
{
    return GetDataDir() / "networking";
}

/** Best-effort: a short line always fits in one pipe write in practice, and there's nothing useful to retry into if it somehow doesn't. */
void SendLine(const std::string& line)
{
#ifdef _WIN32
    if (!g_writeHandle) return;
    DWORD written = 0;
    WriteFile(g_writeHandle, line.data(), (DWORD)line.size(), &written, NULL);
#else
    if (g_writeFd < 0) return;
    ssize_t written = write(g_writeFd, line.data(), line.size());
    (void)written;
#endif
}

} // namespace

bool StartNetworkingWatchdog()
{
    // Hash-verified the same way tor/i2pd are: refuse a sibling pirate-networking
    // that doesn't match what this exact build shipped, rather than silently
    // running whatever binary happens to be sitting there under that name -
    // pirated hands this process a live control channel over its own daemons.
    const fs::path binary = CManagedProcess::FindBinary("pirate-networking", "", EMBEDDED_PIRATE_NETWORKING_SHA256);
    if (binary.empty()) {
        LogPrintf("netwatchdog: no pirate-networking binary found - embedded tor/i2pd will not be "
                  "auto-terminated if this process dies uncleanly (crash/OOM-kill)\n");
        return false;
    }

    const fs::path dataDir = WatchdogDataDir();
    boost::system::error_code ec;
    fs::create_directories(dataDir, ec);
    fs::permissions(dataDir, fs::owner_all, ec);

#ifdef _WIN32
    // Both handles start out non-inheritable so neither can ever leak into
    // an unrelated CreateProcess call (tor's, i2pd's, ...); only a duplicate
    // of the read end, made inheritable right before this one Spawn() call
    // and closed again immediately after, is ever exposed to a child.
    SECURITY_ATTRIBUTES saNonInherit{};
    saNonInherit.nLength = sizeof(SECURITY_ATTRIBUTES);
    saNonInherit.bInheritHandle = FALSE;

    HANDLE hRead = NULL, hWrite = NULL;
    if (!CreatePipe(&hRead, &hWrite, &saNonInherit, 0)) {
        LogPrintf("netwatchdog: CreatePipe failed, error %d\n", GetLastError());
        return false;
    }

    HANDLE hReadInheritable = NULL;
    if (!DuplicateHandle(GetCurrentProcess(), hRead, GetCurrentProcess(), &hReadInheritable,
                          0, TRUE /* inheritable */, DUPLICATE_SAME_ACCESS)) {
        LogPrintf("netwatchdog: DuplicateHandle failed, error %d\n", GetLastError());
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return false;
    }
    CloseHandle(hRead);

    char argBuf[32];
    std::snprintf(argBuf, sizeof(argBuf), "--handle=%llu", (unsigned long long)(uintptr_t)hReadInheritable);
    const bool ok = g_watchdogProcess.Spawn(binary, {argBuf},
                                            dataDir / "networking.stdout.log", dataDir / "networking.stderr.log");
    CloseHandle(hReadInheritable); // the child got its own copy via inheritance
    if (!ok) {
        CloseHandle(hWrite);
        LogPrintf("netwatchdog: failed to launch pirate-networking\n");
        return false;
    }
    g_writeHandle = hWrite;
#else
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        LogPrintf("netwatchdog: pipe() failed, errno %d\n", errno);
        return false;
    }
    // Both ends start close-on-exec so neither can ever leak into an
    // unrelated fork+exec (tor's, i2pd's, ...); the read end has CLOEXEC
    // cleared only for the one exec below, and our copy of it is closed
    // again immediately after - only pirate-networking ends up holding it.
    fcntl(fds[0], F_SETFD, 0);
    const bool ok = g_watchdogProcess.Spawn(binary, {"--fd=" + std::to_string(fds[0])},
                                            dataDir / "networking.stdout.log", dataDir / "networking.stderr.log");
    close(fds[0]);
    if (!ok) {
        close(fds[1]);
        LogPrintf("netwatchdog: failed to launch pirate-networking\n");
        return false;
    }
    g_writeFd = fds[1];
#endif

    LogPrintf("netwatchdog: launched pirate-networking from '%s' to reap embedded tor/i2pd if this "
              "process dies uncleanly\n", binary.string());
    return true;
}

void NotifyNetworkingWatchdog(const std::string& name, int64_t pid)
{
    if (pid <= 0) return;
    SendLine("PID " + name + " " + std::to_string(pid) + "\n");
}

void StopNetworkingWatchdog()
{
#ifdef _WIN32
    if (!g_writeHandle) return;
#else
    if (g_writeFd < 0) return;
#endif
    SendLine("SHUTDOWN\n");

#ifdef _WIN32
    CloseHandle(g_writeHandle);
    g_writeHandle = NULL;
#else
    close(g_writeFd);
    g_writeFd = -1;
#endif
    g_watchdogProcess.Stop(2000);
}
