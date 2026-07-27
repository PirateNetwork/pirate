// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// pirate-networking: a tiny standalone reaper for the embedded tor/i2pd
// daemons pirated/pirate-qt spawns (see tor_process.cpp, i2pd_process.cpp).
//
// pirated launches exactly one of these alongside itself and hands it a pipe
// (read end here, write end kept open by pirated for as long as pirated is
// alive) plus, over that same pipe, the pid of each daemon it starts. If
// pirated exits cleanly it says so ("SHUTDOWN") before closing the pipe and
// this process just exits too. If pirated instead dies for any other reason
// - crash, OOM-kill, `kill -9` - the write end closes as a side effect of the
// kernel tearing down its file descriptors, we see that as EOF with no prior
// SHUTDOWN, and we terminate every daemon pid we were told about so it can't
// keep running orphaned forever.
//
// Deliberately dependency-free: it does not link any bitcoin library. Its
// only job is to still be functioning after pirated has already crashed, so
// it should not need to first construct any of the state that pirated needs
// to have working (logging, arg parsing, the data directory, ...).

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

struct TrackedProcess {
    std::string name;
    int64_t pid;
};

/** How long to wait after SIGTERM/TerminateProcess before giving up on a graceful exit. */
const int STOP_GRACE_MS = 5000;
const int STOP_POLL_MS = 100;

void LogLine(const std::string& s)
{
    std::fprintf(stderr, "pirate-networking: %s\n", s.c_str());
    std::fflush(stderr);
}

#ifndef _WIN32
/**
 * Best-effort confirmation that `pid` is still the process we think it is,
 * so a pid recycled by the OS after the daemon already exited on its own
 * doesn't get killed by mistake. Only implemented where /proc is available;
 * elsewhere this always returns true (best effort, no verification).
 */
bool ProcessLooksLike(int64_t pid, const std::string& name)
{
#ifdef __linux__
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%lld/comm", (long long)pid);
    FILE* f = std::fopen(path, "r");
    if (!f) return false; // pid doesn't exist at all anymore
    char buf[64] = {0};
    size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
    // Linux truncates /proc/pid/comm to 15 chars + NUL (TASK_COMM_LEN=16); an
    // arbitrarily-named external binary (-torpath/-i2pdpath) can be longer than
    // that, so compare against the same truncation rather than the full name.
    return name.substr(0, 15) == buf;
#else
    (void)pid; (void)name;
    return true;
#endif
}

void TerminateTracked(const TrackedProcess& p)
{
    if (!ProcessLooksLike(p.pid, p.name)) {
        LogLine("skipping " + p.name + " pid " + std::to_string(p.pid) + " - no longer looks like that process");
        return;
    }
    LogLine("pirated is gone without a clean shutdown - stopping " + p.name + " (pid " + std::to_string(p.pid) + ")");
    kill((pid_t)p.pid, SIGTERM);
    for (int waited = 0; waited < STOP_GRACE_MS; waited += STOP_POLL_MS) {
        if (kill((pid_t)p.pid, 0) != 0) return; // gone
        usleep(STOP_POLL_MS * 1000);
    }
    LogLine(p.name + " (pid " + std::to_string(p.pid) + ") did not exit after SIGTERM, sending SIGKILL");
    kill((pid_t)p.pid, SIGKILL);
}

int64_t ParseFdArg(int argc, char** argv)
{
    const std::string prefix = "--fd=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            return std::atoll(arg.c_str() + prefix.size());
        }
    }
    return -1;
}
#else // _WIN32
void TerminateTracked(const TrackedProcess& p)
{
    DWORD pid = (DWORD)p.pid;
    HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        LogLine("skipping " + p.name + " pid " + std::to_string(p.pid) + " - no longer running");
        return;
    }
    wchar_t nameBuf[MAX_PATH];
    DWORD nameLen = MAX_PATH;
    if (QueryFullProcessImageNameW(h, 0, nameBuf, &nameLen)) {
        std::wstring full(nameBuf, nameLen);
        size_t slash = full.find_last_of(L"\\/");
        std::wstring base = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
        std::wstring expected(p.name.begin(), p.name.end());
        expected += L".exe";
        if (_wcsicmp(base.c_str(), expected.c_str()) != 0) {
            LogLine("skipping pid " + std::to_string(p.pid) + " - no longer looks like " + p.name);
            CloseHandle(h);
            return;
        }
    }
    LogLine("pirated is gone without a clean shutdown - stopping " + p.name + " (pid " + std::to_string(p.pid) + ")");
    TerminateProcess(h, 1);
    WaitForSingleObject(h, STOP_GRACE_MS);
    CloseHandle(h);
}

int64_t ParseHandleArg(int argc, char** argv)
{
    const std::string prefix = "--handle=";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            return _strtoui64(arg.c_str() + prefix.size(), nullptr, 10);
        }
    }
    return 0;
}
#endif

/** Parses "PID <name> <pid>" into *out; returns false if `line` isn't that. */
bool ParsePidLine(const std::string& line, TrackedProcess* out)
{
    if (line.compare(0, 4, "PID ") != 0) return false;
    size_t nameStart = 4;
    size_t sep = line.find(' ', nameStart);
    if (sep == std::string::npos) return false;
    out->name = line.substr(nameStart, sep - nameStart);
    out->pid = std::atoll(line.c_str() + sep + 1);
    return !out->name.empty() && out->pid > 0;
}

} // namespace

int main(int argc, char** argv)
{
#ifndef _WIN32
    // Detach from pirated's terminal session/process group: a Ctrl-C or
    // terminal hangup should not take this process down directly - it must
    // only react via the pipe protocol below, otherwise it can't tell the
    // difference between "pirated is shutting down cleanly" and "something
    // signaled the whole foreground group, including us".
    setsid();
    signal(SIGHUP, SIG_IGN);

    int64_t fdArg = ParseFdArg(argc, argv);
    if (fdArg < 0) {
        std::fprintf(stderr, "usage: pirate-networking --fd=<inherited pipe read fd>\n");
        return 1;
    }
    int fd = (int)fdArg;
#else
    int64_t handleArg = ParseHandleArg(argc, argv);
    if (handleArg == 0) {
        std::fprintf(stderr, "usage: pirate-networking --handle=<inherited pipe read handle>\n");
        return 1;
    }
    HANDLE h = (HANDLE)(uintptr_t)handleArg;
#endif

    std::vector<TrackedProcess> tracked;
    bool shutdownRequested = false;
    std::string buffer;
    char chunk[4096];

    for (;;) {
#ifndef _WIN32
        ssize_t n = read(fd, chunk, sizeof(chunk));
        if (n <= 0) break;
#else
        DWORD n = 0;
        if (!ReadFile(h, chunk, sizeof(chunk), &n, NULL) || n == 0) break;
#endif
        buffer.append(chunk, n);

        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);

            if (line == "SHUTDOWN") {
                shutdownRequested = true;
            } else {
                TrackedProcess p;
                if (ParsePidLine(line, &p)) {
                    LogLine("tracking " + p.name + " (pid " + std::to_string(p.pid) + ")");
                    tracked.push_back(p);
                }
            }
        }
        if (shutdownRequested) break;
    }

    if (shutdownRequested) {
        LogLine("pirated is shutting down cleanly, standing down");
        return 0;
    }

    LogLine("lost contact with pirated (pipe closed without a clean SHUTDOWN)");
    for (const TrackedProcess& p : tracked) {
        TerminateTracked(p);
    }
    return 0;
}
