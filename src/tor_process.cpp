// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "tor_process.h"

#include "embedded_binary_hashes.h"
#include "embeddedprocess.h"
#include "netbase.h"
#include "networking_watchdog.h"
#include "torcontrol.h"
#include "util.h"
#include "util/readwritefile.h"
#include "utiltime.h"

#include <atomic>

#include <boost/bind/bind.hpp>

using namespace boost::placeholders;

namespace {

CManagedProcess g_torProcess;

/** How long to wait for tor's ControlPort to come up before giving up. */
static const int TOR_STARTUP_TIMEOUT_MS = 180000;
/** How long to give tor to shut down cleanly before killing it. */
static const int TOR_STOP_TIMEOUT_MS = 5000;
/** How often ThreadSuperviseTor() polls whether the child is still alive. */
static const int64_t TOR_SUPERVISE_INTERVAL_MS = 5000;
/** Respawn backoff cap; mirrors i2pd_process.cpp's I2PD_RESPAWN_BACKOFF_CAP_MS. */
static const int64_t TOR_RESPAWN_BACKOFF_CAP_MS = 300000;

/** Set by StartEmbeddedTor() just before spawning ThreadWaitTorReady(). */
CService g_torControlTarget;

// Spawn parameters, resolved once by StartEmbeddedTor() (binary lookup, port
// picking, torrc) and reused by ThreadSuperviseTor() to relaunch tor if the
// child dies mid-run - see that function's doc comment for why that's needed.
fs::path g_torBinary;
fs::path g_torrcPath;
fs::path g_torStdoutLog;
fs::path g_torStderrLog;

/** Set by StopEmbeddedTor() so ThreadSuperviseTor() knows an exit it
 *  observes right afterward was requested, not a crash to relaunch from. */
std::atomic<bool> g_torStopping{false};

fs::path TorDataDir()
{
    return GetDataDir() / "tor";
}

//! Polls g_torControlTarget for readiness off the main init thread - see
//! StartEmbeddedTor()'s doc comment for why this doesn't block startup.
void ThreadWaitTorReady()
{
    if (!g_torProcess.WaitUntilReady(g_torControlTarget, TOR_STARTUP_TIMEOUT_MS)) {
        LogPrintf("tor: embedded tor daemon did not become ready within %d ms\n", TOR_STARTUP_TIMEOUT_MS);
    }
}

//! Launch tor using the parameters StartEmbeddedTor() already resolved. Used
//! both for the initial launch and by ThreadSuperviseTor() to relaunch it
//! after it dies.
bool SpawnTor()
{
    LogPrintf("tor: launching embedded tor daemon from '%s'\n", g_torBinary.string());
    if (!g_torProcess.Spawn(g_torBinary, {"-f", g_torrcPath.string()}, g_torStdoutLog, g_torStderrLog)) {
        LogPrintf("tor: failed to launch embedded tor daemon\n");
        return false;
    }
    // Report the actual binary basename (not a fixed "tor" label): -torpath can point
    // at an arbitrarily-named external binary, and pirate-networking identifies the
    // process it's about to terminate by comparing against this same name.
    NotifyNetworkingWatchdog(g_torBinary.stem().string(), g_torProcess.GetProcessId());
    return true;
}

//! Watches the embedded tor child for the life of the process and relaunches
//! it (with backoff) if it exits on its own (crash, OOM-kill) - see
//! ThreadSuperviseI2Pd() in i2pd_process.cpp for the equivalent i2pd gap this
//! closes: torcontrol.cpp's own reconnect logic only retries the
//! ControlPort connection, which stays dead forever if tor itself is gone.
void ThreadSuperviseTor()
{
    int64_t backoff = TOR_SUPERVISE_INTERVAL_MS;
    while (true) {
        MilliSleep(TOR_SUPERVISE_INTERVAL_MS);
        boost::this_thread::interruption_point();

        if (g_torStopping) return;

        if (g_torProcess.IsRunning()) {
            backoff = TOR_SUPERVISE_INTERVAL_MS;
            continue;
        }

        LogPrintf("tor: embedded tor daemon is no longer running, relaunching\n");
        if (SpawnTor()) {
            backoff = TOR_SUPERVISE_INTERVAL_MS;
        } else {
            MilliSleep(backoff);
            if (backoff < TOR_RESPAWN_BACKOFF_CAP_MS) backoff *= 2;
        }
    }
}

} // namespace

bool StartEmbeddedTor(boost::thread_group& threadGroup)
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

    const std::string torSocksArg = GetArg("-torsocksport", DEFAULT_TOR_SOCKS);
    CService torSocksTarget = LookupNumeric(torSocksArg.c_str(), 9050);
    if (!torSocksTarget.IsValid()) {
        LogPrintf("tor: invalid -torsocksport address '%s', not starting embedded tor\n", torSocksArg);
        return false;
    }
    const uint16_t chosenSocksPort = PickAvailablePort(torSocksTarget.ToStringIP(), torSocksTarget.GetPort());
    if (chosenSocksPort != torSocksTarget.GetPort()) {
        torSocksTarget = CService(torSocksTarget, chosenSocksPort);
        // Same reasoning as -torcontrol just above: torcontrol.cpp's auth_cb
        // reads -torsocksport lazily, after the daemon this launches has
        // already authenticated over its ControlPort - overriding it here is
        // what makes the picked port reach it instead of the stale default.
        mapArgs["-torsocksport"] = torSocksTarget.ToStringIPPort();
        LogPrintf("tor: -torsocksport port was busy, using %s instead\n", torSocksTarget.ToStringIPPort());
    }

    // "pirate-tor" is our own bundled copy (sibling of this executable, hash-verified);
    // "tor" is only used as the $PATH fallback name, since that's what an externally
    // managed system Tor install is actually called - see FindBinary()'s doc comment.
    const fs::path binary = CManagedProcess::FindBinary("pirate-tor", GetArg("-torpath", ""), EMBEDDED_TOR_SHA256, "tor");
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
    torrc += "SocksPort " + torSocksTarget.ToStringIPPort() + "\n";
    torrc += "Log notice stdout\n";
    if (!WriteBinaryFile(torrcPath, torrc)) {
        LogPrintf("tor: failed to write torrc to '%s'\n", torrcPath.string());
        return false;
    }

    g_torBinary = binary;
    g_torrcPath = torrcPath;
    g_torStdoutLog = dataDir / "tor.stdout.log";
    g_torStderrLog = dataDir / "tor.stderr.log";

    if (!SpawnTor()) return false;

    // Leave it running regardless of how long it takes to become ready;
    // torcontrol.cpp's own reconnect logic picks up the connection whenever
    // the ControlPort actually comes up. ThreadWaitTorReady() just logs a
    // warning off the main thread if it takes unusually long.
    g_torControlTarget = torControlTarget;
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "torready", &ThreadWaitTorReady));
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "torsupervise", &ThreadSuperviseTor));

    return true;
}

void StopEmbeddedTor()
{
    g_torStopping = true;
    g_torProcess.Stop(TOR_STOP_TIMEOUT_MS);
}
