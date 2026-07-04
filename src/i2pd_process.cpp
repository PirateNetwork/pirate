// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "i2pd_process.h"

#include "embedded_binary_hashes.h"
#include "embeddedprocess.h"
#include "netbase.h"
#include "util.h"
#include "util/readwritefile.h"

namespace {

CManagedProcess g_i2pdProcess;

/** How long to wait for i2pd's SAM port to come up before giving up. */
static const int I2PD_STARTUP_TIMEOUT_MS = 180000;
/** How long to give i2pd to shut down cleanly before killing it. */
static const int I2PD_STOP_TIMEOUT_MS = 5000;

fs::path I2PdDataDir()
{
    return GetDataDir() / "i2pd";
}

} // namespace

bool StartEmbeddedI2Pd()
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

    const fs::path binary = CManagedProcess::FindBinary("i2pd", GetArg("-i2pdpath", ""), EMBEDDED_I2PD_SHA256);
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

    LogPrintf("i2p: launching embedded i2pd daemon from '%s'\n", binary.string());
    const std::string confArg = "--conf=" + confPath.string();
    const std::string dataDirArg = "--datadir=" + (dataDir / "data").string();
    if (!g_i2pdProcess.Spawn(binary, {confArg, dataDirArg},
                             dataDir / "i2pd.stdout.log", dataDir / "i2pd.stderr.log")) {
        LogPrintf("i2p: failed to launch embedded i2pd daemon\n");
        return false;
    }

    if (!g_i2pdProcess.WaitUntilReady(samTarget, I2PD_STARTUP_TIMEOUT_MS)) {
        LogPrintf("i2p: embedded i2pd daemon did not become ready within %d ms\n", I2PD_STARTUP_TIMEOUT_MS);
        // Leave it running; i2p.cpp's own session-creation retry logic may still pick it up later.
    }

    return true;
}

void StopEmbeddedI2Pd()
{
    g_i2pdProcess.Stop(I2PD_STOP_TIMEOUT_MS);
}
