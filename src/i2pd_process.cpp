// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "i2pd_process.h"

#include "embedded_binary_hashes.h"
#include "embeddedprocess.h"
#include "netbase.h"
#include "networking_watchdog.h"
#include "util.h"
#include "util/readwritefile.h"
#include "utiltime.h"

#include <atomic>

#include <boost/bind/bind.hpp>

using namespace boost::placeholders;

namespace {

CManagedProcess g_i2pdProcess;

/** How long to wait for i2pd's SAM port to come up before giving up. */
static const int I2PD_STARTUP_TIMEOUT_MS = 180000;
/** How long to give i2pd to shut down cleanly before killing it. */
static const int I2PD_STOP_TIMEOUT_MS = 5000;
/** How often ThreadSuperviseI2Pd() polls whether the child is still alive. */
static const int64_t I2PD_SUPERVISE_INTERVAL_MS = 5000;
/** Respawn backoff cap, mirroring ThreadI2PCheck()'s SAM-reconnect backoff cap in net.cpp. */
static const int64_t I2PD_RESPAWN_BACKOFF_CAP_MS = 300000;

/** Set by StartEmbeddedI2Pd() just before spawning ThreadWaitI2PdReady(). */
CService g_i2pdSamTarget;

// Spawn parameters, resolved once by StartEmbeddedI2Pd() (binary lookup,
// port picking, conf file) and reused by ThreadSuperviseI2Pd() to relaunch
// i2pd if the child dies mid-run - see that function's doc comment for why
// that's needed at all.
fs::path g_i2pdBinary;
std::string g_i2pdConfArg;
std::string g_i2pdDataDirArg;
fs::path g_i2pdStdoutLog;
fs::path g_i2pdStderrLog;

/** Set by StopEmbeddedI2Pd() so ThreadSuperviseI2Pd() knows an exit it
 *  observes right afterward was requested, not a crash to relaunch from. */
std::atomic<bool> g_i2pdStopping{false};

fs::path I2PdDataDir()
{
    return GetDataDir() / "i2pd";
}

//! Polls g_i2pdSamTarget for readiness off the main init thread - see
//! StartEmbeddedI2Pd()'s doc comment for why this doesn't block startup.
void ThreadWaitI2PdReady()
{
    if (!g_i2pdProcess.WaitUntilReady(g_i2pdSamTarget, I2PD_STARTUP_TIMEOUT_MS)) {
        LogPrintf("i2p: embedded i2pd daemon did not become ready within %d ms\n", I2PD_STARTUP_TIMEOUT_MS);
    }
}

//! Launch i2pd using the parameters StartEmbeddedI2Pd() already resolved.
//! Used both for the initial launch and by ThreadSuperviseI2Pd() to relaunch
//! it after it dies.
bool SpawnI2Pd()
{
    LogPrintf("i2p: launching embedded i2pd daemon from '%s'\n", g_i2pdBinary.string());
    if (!g_i2pdProcess.Spawn(g_i2pdBinary, {g_i2pdConfArg, g_i2pdDataDirArg}, g_i2pdStdoutLog, g_i2pdStderrLog)) {
        LogPrintf("i2p: failed to launch embedded i2pd daemon\n");
        return false;
    }
    // Report the actual binary basename (not a fixed "i2pd" label): -i2pdpath can point
    // at an arbitrarily-named external binary, and pirate-networking identifies the
    // process it's about to terminate by comparing against this same name.
    NotifyNetworkingWatchdog(g_i2pdBinary.stem().string(), g_i2pdProcess.GetProcessId());
    return true;
}

//! Watches the embedded i2pd child for the life of the process and relaunches
//! it (with backoff) if it exits on its own - a crash or an OOM-kill, say.
//! Nothing else in this codebase notices that: ThreadI2PCheck() (net.cpp)
//! only retries the SAM *connection*, which stays dead forever if the
//! process behind the port is gone, since i2pd is never told to come back.
void ThreadSuperviseI2Pd()
{
    int64_t backoff = I2PD_SUPERVISE_INTERVAL_MS;
    while (true) {
        MilliSleep(I2PD_SUPERVISE_INTERVAL_MS);
        boost::this_thread::interruption_point();

        if (g_i2pdStopping) return;

        if (g_i2pdProcess.IsRunning()) {
            backoff = I2PD_SUPERVISE_INTERVAL_MS;
            continue;
        }

        LogPrintf("i2p: embedded i2pd daemon is no longer running, relaunching\n");
        if (SpawnI2Pd()) {
            backoff = I2PD_SUPERVISE_INTERVAL_MS;
        } else {
            MilliSleep(backoff);
            if (backoff < I2PD_RESPAWN_BACKOFF_CAP_MS) backoff *= 2;
        }
    }
}

} // namespace

bool StartEmbeddedI2Pd(boost::thread_group& threadGroup)
{
    if (!GetBoolArg("-i2pdautostart", DEFAULT_I2PD_AUTOSTART)) return false;

    const std::string i2psamArg = GetArg("-i2psam", "");
    if (i2psamArg.empty()) {
        // No I2P SAM target configured (and none was defaulted in), nothing to launch.
        return false;
    }

    CService samTarget;
    if (!Lookup(i2psamArg.c_str(), samTarget, 7656, fNameLookup) || !samTarget.IsValid()) {
        LogPrintf("i2p: invalid -i2psam address '%s', not starting embedded i2pd\n", i2psamArg);
        return false;
    }

    const uint16_t chosenPort = PickAvailablePort(samTarget.ToStringIP(), samTarget.GetPort());
    if (chosenPort != samTarget.GetPort()) {
        samTarget = CService(samTarget, chosenPort);
        // net.cpp reads the I2P proxy target via GetProxy(NET_I2P, ...) when it
        // later constructs i2p::sam::Session, not by re-parsing -i2psam, so the
        // picked port has to reach it through the same SetProxy() the initial
        // -i2psam parsing in init.cpp already used.
        SetProxy(NET_I2P, proxyType(samTarget));
        LogPrintf("i2p: -i2psam port was busy, using %s instead\n", samTarget.ToStringIPPort());
    }

    // "pirate-i2pd" is our own bundled copy (sibling of this executable, hash-verified);
    // "i2pd" is only used as the $PATH fallback name, since that's what an externally
    // managed system I2P router install is actually called - see FindBinary()'s doc comment.
    const fs::path binary = CManagedProcess::FindBinary("pirate-i2pd", GetArg("-i2pdpath", ""), EMBEDDED_I2PD_SHA256, "i2pd");
    if (binary.empty()) {
        LogPrint("i2p", "i2p: no bundled i2pd binary found, assuming an externally managed I2P router is in use\n");
        return false;
    }

    const fs::path dataDir = I2PdDataDir();
    boost::system::error_code ec;
    fs::create_directories(dataDir, ec);
    fs::permissions(dataDir, fs::owner_all, ec);
    fs::create_directories(dataDir / "data", ec);
    fs::permissions(dataDir / "data", fs::owner_all, ec);

    if (!PathIsSafeForConfig(dataDir)) {
        LogPrintf("i2p: data directory path '%s' is unsafe to embed in an i2pd.conf, not starting embedded i2pd\n", dataDir.string());
        return false;
    }

    const fs::path confPath = dataDir / "i2pd.conf";
    std::string conf;
    conf += "[sam]\n";
    conf += "enabled = true\n";
    conf += "address = " + samTarget.ToStringIP() + "\n";
    conf += "port = " + samTarget.ToStringPort() + "\n";
    conf += "\n[http]\n";
    conf += "enabled = false\n";
    conf += "\n[socksproxy]\n";
    conf += "enabled = false\n";
    if (!WriteBinaryFile(confPath, conf)) {
        LogPrintf("i2p: failed to write i2pd.conf to '%s'\n", confPath.string());
        return false;
    }

    g_i2pdBinary = binary;
    g_i2pdConfArg = "--conf=" + confPath.string();
    g_i2pdDataDirArg = "--datadir=" + (dataDir / "data").string();
    g_i2pdStdoutLog = dataDir / "i2pd.stdout.log";
    g_i2pdStderrLog = dataDir / "i2pd.stderr.log";

    if (!SpawnI2Pd()) return false;

    // Leave it running regardless of how long it takes to become ready;
    // i2p.cpp's own session-creation retry logic (ThreadI2PCheck) picks up
    // the connection whenever the SAM port actually comes up.
    // ThreadWaitI2PdReady() just logs a warning off the main thread if it
    // takes unusually long.
    g_i2pdSamTarget = samTarget;
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "i2pdready", &ThreadWaitI2PdReady));
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "i2pdsupervise", &ThreadSuperviseI2Pd));

    return true;
}

void StopEmbeddedI2Pd()
{
    g_i2pdStopping = true;
    g_i2pdProcess.Stop(I2PD_STOP_TIMEOUT_MS);
}
