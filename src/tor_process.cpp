// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tor_process.h"

#include "embedded_binary_hashes.h"
#include "embeddedprocess.h"
#include "netbase.h"
#include "torcontrol.h"
#include "util.h"
#include "util/readwritefile.h"

namespace {

CManagedProcess g_torProcess;

/** How long to wait for tor's ControlPort to come up before giving up. */
static const int TOR_STARTUP_TIMEOUT_MS = 180000;
/** How long to give tor to shut down cleanly before killing it. */
static const int TOR_STOP_TIMEOUT_MS = 5000;

fs::path TorDataDir()
{
    return GetDataDir() / "tor";
}

} // namespace

bool StartEmbeddedTor()
{
    if (!GetBoolArg("-torautostart", DEFAULT_TOR_AUTOSTART)) return false;

    const std::string torControlArg = GetArg("-torcontrol", DEFAULT_TOR_CONTROL);
    CService torControlTarget = LookupNumeric(torControlArg.c_str(), 9051);
    if (!torControlTarget.IsValid()) {
        LogPrintf("tor: invalid -torcontrol address '%s', not starting embedded tor\n", torControlArg);
        return false;
    }

    const uint16_t chosenPort = PickAvailablePort(torControlTarget.ToStringIP(), torControlTarget.GetPort());
    if (chosenPort != torControlTarget.GetPort()) {
        torControlTarget = CService(torControlTarget, chosenPort);
        // torcontrol.cpp reads -torcontrol lazily (after StartTorControl() is
        // called, right after this function returns), so overriding it here
        // is what actually makes the picked port reach it.
        mapArgs["-torcontrol"] = torControlTarget.ToStringIPPort();
        LogPrintf("tor: -torcontrol port was busy, using %s instead\n", torControlTarget.ToStringIPPort());
    }

    const fs::path binary = CManagedProcess::FindBinary("tor", GetArg("-torpath", ""), EMBEDDED_TOR_SHA256);
    if (binary.empty()) {
        LogPrint("tor", "tor: no bundled tor binary found, assuming an externally managed tor is in use\n");
        return false;
    }

    const fs::path dataDir = TorDataDir();
    boost::system::error_code ec;
    fs::create_directories(dataDir, ec);
    fs::permissions(dataDir, fs::owner_all, ec);
    fs::create_directories(dataDir / "data", ec);
    fs::permissions(dataDir / "data", fs::owner_all, ec);

    if (!PathIsSafeForConfig(dataDir)) {
        LogPrintf("tor: data directory path '%s' is unsafe to embed in a torrc, not starting embedded tor\n", dataDir.string());
        return false;
    }

    const fs::path torrcPath = dataDir / "torrc";
    std::string torrc;
    torrc += "ControlPort " + torControlTarget.ToStringIPPort() + "\n";
    torrc += "CookieAuthentication 1\n";
    torrc += "DataDirectory " + (dataDir / "data").string() + "\n";
    torrc += "RunAsDaemon 0\n";
    torrc += "SocksPort 0\n";
    torrc += "Log notice stdout\n";
    if (!WriteBinaryFile(torrcPath, torrc)) {
        LogPrintf("tor: failed to write torrc to '%s'\n", torrcPath.string());
        return false;
    }

    LogPrintf("tor: launching embedded tor daemon from '%s'\n", binary.string());
    if (!g_torProcess.Spawn(binary, {"-f", torrcPath.string()},
                            dataDir / "tor.stdout.log", dataDir / "tor.stderr.log")) {
        LogPrintf("tor: failed to launch embedded tor daemon\n");
        return false;
    }

    if (!g_torProcess.WaitUntilReady(torControlTarget, TOR_STARTUP_TIMEOUT_MS)) {
        LogPrintf("tor: embedded tor daemon did not become ready within %d ms\n", TOR_STARTUP_TIMEOUT_MS);
        // Leave it running; torcontrol.cpp's own reconnect logic may still pick it up later.
    }

    return true;
}

void StopEmbeddedTor()
{
    g_torProcess.Stop(TOR_STOP_TIMEOUT_MS);
}
