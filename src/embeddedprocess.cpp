// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "embeddedprocess.h"

#include "crypto/sha256.h"
#include "netbase.h"
#include "util.h"
#include "util/strencodings.h"
#include "utiltime.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <string>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace {

/** Best-effort path of the currently running executable; empty if unknown. */
fs::path SelfExecutablePath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (len == 0 || len == MAX_PATH) return fs::path();
    return fs::path(buf, buf + len);
#elif defined(__APPLE__)
    char buf[4096];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return fs::path();
    boost::system::error_code ec;
    fs::path resolved = fs::canonical(fs::path(buf), ec);
    return ec ? fs::path(buf) : resolved;
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return fs::path();
    buf[len] = '\0';
    return fs::path(buf);
#endif
}

std::vector<fs::path> SplitPathEnv()
{
    std::vector<fs::path> dirs;
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) return dirs;
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string cur;
    for (const char* p = pathEnv; ; ++p) {
        if (*p == sep || *p == '\0') {
            if (!cur.empty()) dirs.emplace_back(cur);
            cur.clear();
            if (*p == '\0') break;
        } else {
            cur += *p;
        }
    }
    return dirs;
}

#ifndef _WIN32
/**
 * True if `dir` is world-writable without the sticky bit set - the standard
 * "insecure PATH entry" heuristic (e.g. a world-writable /tmp-style
 * directory). Root-owned system directories and ordinary user-local install
 * locations (~/bin etc.) are unaffected; this only flags locations where any
 * other local user/process could plant a same-named file.
 */
bool DirIsInsecure(const fs::path& dir)
{
    struct stat st;
    if (stat(dir.string().c_str(), &st) != 0) return false;
    return (st.st_mode & S_IWOTH) && !(st.st_mode & S_ISVTX);
}
#endif

/** True if `path`'s SHA256 (hex, lowercase) equals `expectedHex`. */
bool VerifyFileSha256(const fs::path& path, const std::string& expectedHex)
{
    FILE* f = fsbridge::fopen(path, "rb");
    if (!f) return false;

    CSHA256 hasher;
    unsigned char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        hasher.Write(buf, n);
    }
    const bool readOk = (feof(f) != 0);
    fclose(f);
    if (!readOk) return false;

    unsigned char digest[CSHA256::OUTPUT_SIZE];
    hasher.Finalize(digest);
    return HexStr(digest, digest + CSHA256::OUTPUT_SIZE) == expectedHex;
}

bool IsUsableBinary(const fs::path& candidate)
{
    boost::system::error_code ec;
    if (!fs::is_regular_file(candidate, ec) || ec) return false;
#ifndef _WIN32
    if (access(candidate.string().c_str(), X_OK) != 0) return false;
    if (DirIsInsecure(candidate.parent_path())) {
        LogPrintf("CManagedProcess: rejecting '%s' - containing directory is world-writable "
                  "without the sticky bit set\n", candidate.string());
        return false;
    }
#endif
    return true;
}

#ifdef _WIN32
/**
 * Quote a single argument per the Win32 argv-parsing rules (the same
 * convention CommandLineToArgvW expects), so a path containing a literal `"`
 * (e.g. derived from an attacker-influenced -datadir) can't break out of its
 * quoted segment and inject additional command-line arguments.
 */
void AppendQuotedArg(std::wstring& out, const std::wstring& arg)
{
    if (!out.empty()) out += L' ';

    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        out += arg;
        return;
    }

    out += L'"';
    for (auto it = arg.begin(); ; ++it) {
        size_t backslashes = 0;
        while (it != arg.end() && *it == L'\\') { ++it; ++backslashes; }

        if (it == arg.end()) {
            out.append(backslashes * 2, L'\\');
            break;
        } else if (*it == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
        } else {
            out.append(backslashes, L'\\');
            out += *it;
        }
    }
    out += L'"';
}
#endif

} // namespace

CManagedProcess::~CManagedProcess()
{
    if (m_running) Stop(2000);
}

fs::path CManagedProcess::FindBinary(const std::string& name, const fs::path& explicitPath,
                                     const std::string& expectedSha256Hex)
{
    if (!explicitPath.empty()) {
        if (IsUsableBinary(explicitPath)) {
            LogPrintf("CManagedProcess: using '%s' for '%s' (explicit path)\n", explicitPath.string(), name);
            return explicitPath;
        }
        LogPrintf("CManagedProcess: configured path '%s' for '%s' is not a usable binary, falling back to auto-detection\n",
                  explicitPath.string(), name);
    }

#ifdef _WIN32
    const std::string fileName = name + ".exe";
#else
    const std::string fileName = name;
#endif

    const fs::path selfPath = SelfExecutablePath();
    if (!selfPath.empty()) {
        const fs::path sibling = selfPath.parent_path() / fileName;
        if (IsUsableBinary(sibling)) {
            if (!expectedSha256Hex.empty() && !VerifyFileSha256(sibling, expectedSha256Hex)) {
                // A mismatch here means the file that shipped next to this executable was
                // tampered with after install - unlike "not present", that's an active tamper
                // signal, not a normal "feature not installed here" case. Fail closed: do not
                // fall through to the weaker, unverified $PATH search below, which would just
                // hand an attacker who can already write to the install directory a second,
                // easier way in.
                LogPrintf("CManagedProcess: SECURITY: '%s' exists but does NOT match the SHA256 recorded "
                          "for '%s' at build time - refusing to start %s at all rather than fall back to "
                          "an unverified copy\n", sibling.string(), name, name);
                return fs::path();
            }
            LogPrintf("CManagedProcess: using '%s' for '%s' (sibling of running executable%s)\n",
                      sibling.string(), name, expectedSha256Hex.empty() ? "" : ", SHA256 verified");
            return sibling;
        }
    }

    for (const fs::path& dir : SplitPathEnv()) {
        const fs::path candidate = dir / fileName;
        if (IsUsableBinary(candidate)) {
            LogPrintf("CManagedProcess: using '%s' for '%s' (found via $PATH)\n", candidate.string(), name);
            return candidate;
        }
    }

    return fs::path();
}

bool CManagedProcess::Spawn(const fs::path& binary, const std::vector<std::string>& args,
                            const fs::path& stdoutLog, const fs::path& stderrLog)
{
    if (m_running) return false;

#ifdef _WIN32
    std::wstring cmdLine;
    AppendQuotedArg(cmdLine, binary.wstring());
    for (const std::string& arg : args) {
        AppendQuotedArg(cmdLine, fs::path(arg).wstring());
    }

    SECURITY_ATTRIBUTES saAttr{};
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;

    HANDLE hOut = CreateFileW(stdoutLog.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &saAttr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hErr = CreateFileW(stderrLog.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &saAttr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    HANDLE hIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &saAttr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = hOut;
    si.hStdError = hErr;
    si.hStdInput = hIn;

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(NULL, &cmdLine[0], NULL, NULL, TRUE,
                              CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP,
                              NULL, NULL, &si, &pi);

    if (hOut != NULL && hOut != INVALID_HANDLE_VALUE) CloseHandle(hOut);
    if (hErr != NULL && hErr != INVALID_HANDLE_VALUE) CloseHandle(hErr);
    if (hIn != NULL && hIn != INVALID_HANDLE_VALUE) CloseHandle(hIn);

    if (!ok) {
        LogPrintf("CManagedProcess: CreateProcess for '%s' failed, error %d\n", binary.string(), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    m_procInfo = pi;
    m_running = true;
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) {
        LogPrintf("CManagedProcess: fork() failed for '%s', errno %d\n", binary.string(), errno);
        return false;
    }
    if (pid == 0) {
        int fdIn = open("/dev/null", O_RDONLY);
        if (fdIn >= 0) { dup2(fdIn, STDIN_FILENO); close(fdIn); }
        int fdOut = open(stdoutLog.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fdOut >= 0) { dup2(fdOut, STDOUT_FILENO); close(fdOut); }
        int fdErr = open(stderrLog.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fdErr >= 0) { dup2(fdErr, STDERR_FILENO); close(fdErr); }

        std::vector<std::string> argStrings;
        argStrings.push_back(binary.string());
        argStrings.insert(argStrings.end(), args.begin(), args.end());
        std::vector<char*> argv;
        argv.reserve(argStrings.size() + 1);
        for (std::string& s : argStrings) argv.push_back(&s[0]);
        argv.push_back(nullptr);

        execv(binary.string().c_str(), argv.data());
        _exit(127); // only reached if execv failed
    }

    m_pid = pid;
    m_running = true;
    return true;
#endif
}

bool CManagedProcess::WaitUntilReady(const CService& target, int timeoutMs) const
{
    const int64_t deadline = GetTimeMillis() + timeoutMs;
    do {
        std::unique_ptr<Sock> sock = CreateSock(target);
        if (sock && ConnectSocketDirectly(target, *sock, 1000)) {
            return true;
        }
        MilliSleep(500);
    } while (GetTimeMillis() < deadline);
    return false;
}

void CManagedProcess::Stop(int termWaitMs)
{
    if (!m_running) return;

#ifdef _WIN32
    GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, m_procInfo.dwProcessId);
    if (WaitForSingleObject(m_procInfo.hProcess, static_cast<DWORD>(termWaitMs)) != WAIT_OBJECT_0) {
        TerminateProcess(m_procInfo.hProcess, 1);
        WaitForSingleObject(m_procInfo.hProcess, INFINITE);
    }
    CloseHandle(m_procInfo.hProcess);
    m_procInfo = PROCESS_INFORMATION{};
#else
    kill(m_pid, SIGTERM);
    const int64_t deadline = GetTimeMillis() + termWaitMs;
    int status = 0;
    pid_t reaped = 0;
    do {
        reaped = waitpid(m_pid, &status, WNOHANG);
        if (reaped == m_pid) break;
        MilliSleep(100);
    } while (GetTimeMillis() < deadline);

    if (reaped != m_pid) {
        kill(m_pid, SIGKILL);
        waitpid(m_pid, &status, 0);
    }
    m_pid = -1;
#endif
    m_running = false;
}

bool IsPortAvailable(const CService& target)
{
    std::unique_ptr<Sock> sock = CreateSock(target);
    if (!sock) return false;

    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!target.GetSockAddr((struct sockaddr*)&sockaddr, &len)) return false;

    return bind(sock->Get(), (struct sockaddr*)&sockaddr, len) == 0;
}

uint16_t PickAvailablePort(const std::string& bindIp, uint16_t preferred)
{
    const CService preferredTarget = LookupNumeric((bindIp + ":" + std::to_string(preferred)).c_str(), preferred);
    if (preferredTarget.IsValid() && IsPortAvailable(preferredTarget)) return preferred;

    const CService ephemeralTarget = LookupNumeric((bindIp + ":0").c_str(), 0);
    std::unique_ptr<Sock> sock = CreateSock(ephemeralTarget);
    if (!sock) return preferred;

    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!ephemeralTarget.GetSockAddr((struct sockaddr*)&sockaddr, &len)) return preferred;
    if (bind(sock->Get(), (struct sockaddr*)&sockaddr, len) != 0) return preferred;

    struct sockaddr_storage actual;
    socklen_t actualLen = sizeof(actual);
    if (getsockname(sock->Get(), (struct sockaddr*)&actual, &actualLen) != 0) return preferred;

    uint16_t port = 0;
    if (actual.ss_family == AF_INET) {
        port = ntohs(reinterpret_cast<struct sockaddr_in*>(&actual)->sin_port);
    } else if (actual.ss_family == AF_INET6) {
        port = ntohs(reinterpret_cast<struct sockaddr_in6*>(&actual)->sin6_port);
    }
    return port ? port : preferred;
}

bool PathIsSafeForConfig(const fs::path& path)
{
    const std::string s = path.string();
    return s.find('\n') == std::string::npos && s.find('\r') == std::string::npos;
}
