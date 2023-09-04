// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "init.h"
#include "crypto/common.h"
#include "primitives/block.h"
#include "addrman.h"
#include "amount.h"
#include "checkpoints.h"
#include "compat/sanity.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "httpserver.h"
#include "httprpc.h"
#include "key.h"
#include "notarisationdb.h"
#include "params.h"
#include "komodo.h"
#include "komodo_globals.h"
#include "komodo_notary.h"
#include "komodo_gateway.h"
#include "main.h"

#ifdef ENABLE_MINING
#include "key_io.h"
#endif
#include "main.h"
#include "metrics.h"
#include "miner.h"
#include "net.h"
#include "rpc/server.h"
#include "rpc/register.h"
#include "script/standard.h"
#include "scheduler.h"
#include "txdb.h"
#include "torcontrol.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "wallet/asyncrpcoperation_saplingconsolidation.h"
#include "wallet/asyncrpcoperation_sweeptoaddress.h"
#endif
#include <stdint.h>
#include <stdio.h>

#ifndef _WIN32
#include <signal.h>
#endif

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/function.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <openssl/crypto.h>
#include <thread>

#include <libsnark/common/profiling.hpp>

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#if ENABLE_PROTON
#include "amqp/amqpnotificationinterface.h"
#endif

#include "librustzcash.h"

using namespace std;

#include "komodo_defs.h"
#include "komodo_extern_globals.h"
#include "assetchain.h"

#include "komodo_gateway.h"
#include "rpc/net.h"
extern void ThreadSendAlert();
//extern bool komodo_dailysnapshot(int32_t height);  //todo remove
//extern int32_t KOMODO_SNAPSHOT_INTERVAL;

extern int nMaxConnections;          // from net.cpp
extern bool bOverrideMaxConnections; // from net.cpp

ZCJoinSplit* pzcashParams = NULL;

assetchain chainName;

#ifdef ENABLE_WALLET
CWallet* pwalletMain = NULL;
#endif
bool fFeeEstimatesInitialized = false;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = NULL;
#endif

#if ENABLE_PROTON
static AMQPNotificationInterface* pAMQPNotificationInterface = NULL;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use file descriptors, and the ones used for
// accessing block files don't count towards the fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
    BF_NONE         = 0,
    BF_EXPLICIT     = (1U << 0),
    BF_REPORT_ERROR = (1U << 1),
    BF_WHITELIST    = (1U << 2),
};

static const char* FEE_ESTIMATES_FILENAME="fee_estimates.dat";

static const char* DEFAULT_ASMAP_FILENAME="ip_asn.map";

CClientUIInterface uiInterface; // Declared but not defined in ui_interface.h

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit().
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//

std::atomic<bool> fRequestShutdown(false);
bool loadComplete = false;

void StartShutdown()
{

      //Flush wallet on exit
      //Write all transactions and block loacator to the wallet
#ifdef ENABLE_WALLET
    if (loadComplete) {
        LogPrintf("Flushing wallet to disk on shutdown.\n");
        LOCK2(cs_main, pwalletMain->cs_wallet);
        CBlockLocator currentBlock = chainActive.GetLocator();
        int chainHeight = chainActive.Tip()->nHeight;
        pwalletMain->SetBestChain(currentBlock, chainHeight);
    }
#endif

    fRequestShutdown = true;
}
bool ShutdownRequested()
{
    return fRequestShutdown;
}

class CCoinsViewErrorCatcher : public CCoinsViewBacked
{
public:
    CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        try {
            return CCoinsViewBacked::GetCoins(txid, coins);
        } catch(const std::runtime_error& e) {
            uiInterface.ThreadSafeMessageBox(_("Error reading from database, shutting down."), "", CClientUIInterface::MSG_ERROR);
            LogPrintf("Error reading from database: %s\n", e.what());
            // Starting the shutdown sequence and returning false to the caller would be
            // interpreted as 'entry not found' (as opposed to unable to read data), and
            // could lead to invalid interpretation. Just exit immediately, as we can't
            // continue anyway, and all writes should be atomic.
            abort();
        }
    }
    // Writes do not need similar protection, as failure to write is handled by the caller.
};

static CCoinsViewDB *pcoinsdbview = NULL;
static CCoinsViewErrorCatcher *pcoinscatcher = NULL;
static boost::scoped_ptr<ECCVerifyHandle> globalVerifyHandle;

void Interrupt(boost::thread_group& threadGroup)
{
    InterruptHTTPServer();
    InterruptHTTPRPC();
    InterruptRPC();
    InterruptREST();
    InterruptTorControl();
    threadGroup.interrupt_all();
}

void Shutdown()
{
    LogPrintf("%s: In progress...\n", __func__);
    static CCriticalSection cs_Shutdown;
    TRY_LOCK(cs_Shutdown, lockShutdown);
    if (!lockShutdown)
        return;

    /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
    /// for example if the data directory was found to be locked.
    /// Be sure that anything that writes files or flushes caches only does this if the respective
    /// module was initialized.
    static char shutoffstr[128];
    sprintf(shutoffstr,"%s-shutoff",chainName.symbol().c_str());
    //RenameThread("verus-shutoff");
    RenameThread(shutoffstr);
    mempool.AddTransactionsUpdated(1);

    StopHTTPRPC();
    StopREST();
    StopRPC();
    StopHTTPServer();
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(false);
#endif
#ifdef ENABLE_MINING
 #ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
 #else
    GenerateBitcoins(false, 0);
 #endif
#endif
    StopNode();
    StopTorControl();
    UnregisterNodeSignals(GetNodeSignals());

    if (fFeeEstimatesInitialized)
    {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_fileout(fopen(est_path.string().c_str(), "wb"), SER_DISK, CLIENT_VERSION);
        if (!est_fileout.IsNull())
            mempool.WriteFeeEstimates(est_fileout);
        else
            LogPrintf("%s: Failed to write fee estimates to %s\n", __func__, est_path.string());
        fFeeEstimatesInitialized = false;
    }

    {
        LOCK(cs_main);
        if (pcoinsTip != NULL) {
            FlushStateToDisk();
        }
        if (pcoinsTip != NULL) {
            delete pcoinsTip;
            pcoinsTip = NULL;
        }
        if (pcoinscatcher != NULL) {
            delete pcoinscatcher;
            pcoinscatcher = NULL;
        }
        if (pcoinsdbview != NULL) {
            delete pcoinsdbview;
            pcoinsdbview = NULL;
        }
        if (pblocktree != NULL) {
            delete pblocktree;
            pblocktree = NULL;
        }
        if (pnotarisations != NULL) {
            delete pnotarisations;
            pnotarisations = NULL;
        }
    }
#ifdef ENABLE_WALLET
    if (pwalletMain)
        pwalletMain->Flush(true);
#endif

#if ENABLE_ZMQ
    if (pzmqNotificationInterface) {
        UnregisterValidationInterface(pzmqNotificationInterface);
        delete pzmqNotificationInterface;
        pzmqNotificationInterface = NULL;
    }
#endif

#if ENABLE_PROTON
    if (pAMQPNotificationInterface) {
        UnregisterValidationInterface(pAMQPNotificationInterface);
        delete pAMQPNotificationInterface;
        pAMQPNotificationInterface = NULL;
    }
#endif

#ifndef WIN32
    try {
        boost::filesystem::remove(GetPidFile());
    } catch (const boost::filesystem::filesystem_error& e) {
        LogPrintf("%s: Unable to remove pidfile: %s\n", __func__, e.what());
    }
#endif
    UnregisterAllValidationInterfaces();
#ifdef ENABLE_WALLET
    delete pwalletMain;
    pwalletMain = NULL;
#endif
    delete pzcashParams;
    pzcashParams = NULL;
    globalVerifyHandle.reset();
    ECC_Stop();
    LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int)
{
    fRequestShutdown = true;
}

void HandleSIGHUP(int)
{
    fReopenDebugLog = true;
}

bool static InitError(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_ERROR);
    return false;
}

bool static InitWarning(const std::string &str)
{
    uiInterface.ThreadSafeMessageBox(str, "", CClientUIInterface::MSG_WARNING);
    return true;
}

bool static Bind(const CService &addr, unsigned int flags) {
    if (!(flags & BF_EXPLICIT) && !IsReachable(addr))
        return false;
    std::string strError;
    if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
        if (flags & BF_REPORT_ERROR)
            return InitError(strError);
        return false;
    }
    return true;
}

void OnRPCStopped()
{
    cvBlockChange.notify_all();
    LogPrint("rpc", "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand& cmd)
{
    // Observe safe mode
    string strWarning = GetWarnings("rpc");
    if (strWarning != "" && !GetBoolArg("-disablesafemode", false) &&
        !cmd.okSafeMode)
        throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
}

std::string HelpMessage(HelpMessageMode mode)
{
    const bool showDebug = GetBoolArg("-help-debug", false);

    // When adding new options to the categories, please keep and ensure alphabetical ordering.
    // Do not translate _(...) -help-debug options, many technical terms, and only a very small audience, so is unnecessary stress to translators

    string strUsage = HelpMessageGroup(_("Options:"));
    strUsage += HelpMessageOpt("-?", _("This help message"));
    strUsage += HelpMessageOpt("-alerts", strprintf(_("Receive and display P2P network alerts (default: %u)"), DEFAULT_ALERTS));
    strUsage += HelpMessageOpt("-alertnotify=<cmd>", _("Execute command when a relevant alert is received or we see a really long fork (%s in cmd is replaced by message)"));
    strUsage += HelpMessageOpt("-blocknotify=<cmd>", _("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
    strUsage += HelpMessageOpt("-checkblocks=<n>", strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), 288));
    strUsage += HelpMessageOpt("-checklevel=<n>", strprintf(_("How thorough the block verification of -checkblocks is (0-4, default: %u)"), 3));
    strUsage += HelpMessageOpt("-clientname=<SomeName>", _("Full node client name, default 'Dabloon'"));
    strUsage += HelpMessageOpt("-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), "komodo.conf"));
    if (mode == HMM_BITCOIND)
    {
#if !defined(WIN32)
        strUsage += HelpMessageOpt("-daemon", _("Run in the background as a daemon and accept commands"));
#endif
    }
    strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
    strUsage += HelpMessageOpt("-exportdir=<dir>", _("Specify directory to be used when exporting data"));
    strUsage += HelpMessageOpt("-dbcache=<n>", strprintf(_("Set database cache size in megabytes (%d to %d, default: %d)"), nMinDbCache, nMaxDbCache, nDefaultDbCache));
    strUsage += HelpMessageOpt("-loadblock=<file>", _("Imports blocks from external blk000??.dat file") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-maxorphantx=<n>", strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"), DEFAULT_MAX_ORPHAN_TRANSACTIONS));
    strUsage += HelpMessageOpt("-mempooltxinputlimit=<n>", _("[DEPRECATED FROM OVERWINTER] Set the maximum number of transparent inputs in a transaction that the mempool will accept (default: 0 = no limit applied)"));
    strUsage += HelpMessageOpt("-par=<n>", strprintf(_("Set the number of script verification threads (%u to %d, 0 = auto, <0 = leave that many cores free, default: %d)"),
        -(int)boost::thread::hardware_concurrency(), MAX_SCRIPTCHECK_THREADS, DEFAULT_SCRIPTCHECK_THREADS));
    strUsage += HelpMessageOpt("-maxprocessingthreads=<n>", strprintf(_("Set the number of processing threads used (default: %i)"),GetNumCores()));

#ifndef _WIN32
    strUsage += HelpMessageOpt("-pid=<file>", strprintf(_("Specify pid file (default: %s)"), "komodod.pid"));
#endif
    // strUsage += HelpMessageOpt("-prune=<n>", strprintf(_("Reduce storage requirements by pruning (deleting) old blocks. This mode disables wallet support and is incompatible with -txindex. "
    //         "Warning: Reverting this setting requires re-downloading the entire blockchain. "
    //         "(default: 0 = disable pruning blocks, >%u = target size in MiB to use for block files)"), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
    strUsage += HelpMessageOpt("-bootstrap", _("Download and install bootstrap on startup (1 to show GUI prompt, 2 to force download when using CLI)"));
    strUsage += HelpMessageOpt("-reindex", _("Rebuild block chain index from current blk000??.dat files on startup"));
#if !defined(WIN32)
    strUsage += HelpMessageOpt("-sysperms", _("Create new files with system default permissions, instead of umask 077 (only effective with disabled wallet functionality)"));
#endif
    // strUsage += HelpMessageOpt("-txindex", strprintf(_("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), 0));
    strUsage += HelpMessageOpt("-addressindex", strprintf(_("Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses (default: %u)"), DEFAULT_ADDRESSINDEX));
    strUsage += HelpMessageOpt("-timestampindex", strprintf(_("Maintain a timestamp index for block hashes, used to query blocks hashes by a range of timestamps (default: %u)"), DEFAULT_TIMESTAMPINDEX));
    strUsage += HelpMessageOpt("-spentindex", strprintf(_("Maintain a full spent index, used to query the spending txid and input index for an outpoint (default: %u)"), DEFAULT_SPENTINDEX));
    strUsage += HelpMessageGroup(_("Connection options:"));
    strUsage += HelpMessageOpt("-addnode=<ip>", _("Add a node to connect to and attempt to keep the connection open"));
    strUsage += HelpMessageOpt("-asmap=<file>", strprintf("Specify asn mapping used for bucketing of the peers (default: %s). Relative paths will be prefixed by the net-specific datadir location.", DEFAULT_ASMAP_FILENAME));
    strUsage += HelpMessageOpt("-banscore=<n>", strprintf(_("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
    strUsage += HelpMessageOpt("-bantime=<n>", strprintf(_("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
    strUsage += HelpMessageOpt("-bind=<addr>", _("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-connect=<ip>", _("Connect only to the specified node(s)"));
    strUsage += HelpMessageOpt("-discover", _("Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)"));
    strUsage += HelpMessageOpt("-dns", _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + _("(default: 1)"));
    strUsage += HelpMessageOpt("-dnsseed", _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
    strUsage += HelpMessageOpt("-externalip=<ip>", _("Specify your own public address"));
    strUsage += HelpMessageOpt("-forcednsseed", strprintf(_("Always query for peer addresses via DNS lookup (default: %u)"), 0));
    strUsage += HelpMessageOpt("-listen", _("Accept connections from outside (default: 1 if no -proxy or -connect)"));
    strUsage += HelpMessageOpt("-listenonion", strprintf(_("Automatically create Tor hidden service (default: %d)"), DEFAULT_LISTEN_ONION));
    strUsage += HelpMessageOpt("-maxconnections=<n>", strprintf(_("Maintain at most <n> connections to peers (default: %u)"), DEFAULT_MAX_PEER_CONNECTIONS));
    strUsage += HelpMessageOpt("-maxreceivebuffer=<n>", strprintf(_("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), 5000));
    strUsage += HelpMessageOpt("-maxsendbuffer=<n>", strprintf(_("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), 1000));
    strUsage += HelpMessageOpt("-onion=<ip:port>", strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
    strUsage += HelpMessageOpt("-i2psam=<ip:port>", strprintf(_("I2P SAM proxy to reach I2P peers and accept I2P connections (default: none)")));
    strUsage += HelpMessageOpt("-i2pacceptincoming", strprintf(_("If set and -i2psam is also set then incoming I2P connections are accepted via the SAM proxy. If this is not set but -i2psam is set then only outgoing connections will be made to the I2P network. Ignored if -i2psam is not set. Listening for incoming I2P connections is done through the SAM proxy, not by binding to a local address and port (default: 1)")));
    strUsage += HelpMessageOpt("-onlynet=<net>", _("Only connect to nodes in network <net> (ipv4, ipv6, onion or i2p)"));
    strUsage += HelpMessageOpt("-disableipv4", _("Disable Ipv4 network connections") + " " + _("(default: 0)"));
    strUsage += HelpMessageOpt("-disableipv6", _("Disable Ipv6 network connections") + " " + _("(default: 0)"));
    strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(_("Relay non-P2SH multisig (default: %u)"), 1));
    strUsage += HelpMessageOpt("-peerbloomfilters", strprintf(_("Support filtering of blocks and transaction with Bloom filters (default: %u)"), 1));
    strUsage += HelpMessageOpt("-nspv_msg", strprintf(_("Enable NSPV messages processing (default: %u)"), DEFAULT_NSPV_PROCESSING));
    if (showDebug)
        strUsage += HelpMessageOpt("-enforcenodebloom", strprintf("Enforce minimum protocol version to limit use of Bloom filters (default: %u)", 0));
    strUsage += HelpMessageOpt("-port=<port>", strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"), 7770, 17770));
    strUsage += HelpMessageOpt("-proxy=<ip:port>", _("Connect through SOCKS5 proxy"));
    strUsage += HelpMessageOpt("-proxyrandomize", strprintf(_("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"), 1));
    strUsage += HelpMessageOpt("-seednode=<ip>", _("Connect to a node to retrieve peer addresses, and disconnect"));
    strUsage += HelpMessageOpt("-timeout=<n>", strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
    strUsage += HelpMessageOpt("-torcontrol=<ip>:<port>", strprintf(_("Tor control port to use if onion listening enabled (default: %s)"), DEFAULT_TOR_CONTROL));
    strUsage += HelpMessageOpt("-torpassword=<pass>", _("Tor control port password (default: empty)"));
    strUsage += HelpMessageOpt("-tlsenforcement=<0 or 1>", _("Only connect to TLS compatible peers. (default: 0)"));
    strUsage += HelpMessageOpt("-tlsfallbacknontls=<0 or 1>", _("If a TLS connection fails, the next connection attempt of the same peer (based on IP address) takes place without TLS (default: 1)"));
    strUsage += HelpMessageOpt("-tlsvalidate=<0 or 1>", _("Connect to peers only with valid certificates (default: 0)"));
    strUsage += HelpMessageOpt("-tlskeypath=<path>", _("Full path to a private key"));
    strUsage += HelpMessageOpt("-tlskeypwd=<password>", _("Password for a private key encryption (default: not set, i.e. private key will be stored unencrypted)"));
    strUsage += HelpMessageOpt("-tlscertpath=<path>", _("Full path to a certificate"));
    strUsage += HelpMessageOpt("-tlstrustdir=<path>", _("Full path to a trusted certificates directory"));
    strUsage += HelpMessageOpt("-plaintextpeer=<ip>", _("Bypass the TLS connection and allow an unencrypted connection for this ip. Overrides tlsenforcement for this ip."));
    strUsage += HelpMessageOpt("-whitebind=<addr>", _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
    strUsage += HelpMessageOpt("-whitelist=<netmask>", _("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") +
        " " + _("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway"));

#ifdef ENABLE_WALLET
    strUsage += HelpMessageGroup(_("Wallet options:"));
    strUsage += HelpMessageOpt("-seedphrase=<phrase>", _("Recover wallet from seed phrase if a wallet file does not exist."));
    strUsage += HelpMessageOpt("-disablewallet", _("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-mintxvalue=<amt>", strprintf(_("Set minimum incoming value of notes that will be added to the wallet in Arrtoshis (default: %u)"), 1));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(_("Set key pool size to <n> (default: %u)"), 100));
    strUsage += HelpMessageOpt("-cleanup", _("Enable clean up mode. This will put the node in a special mode to reduce the number of unpsent notes through consolidation, requires consolidation to be enabled. Spending functions will be disabled until the consolidation is compleate at which time the node will return to normal operations."));
    strUsage += HelpMessageOpt("-consolidation", _("Enable auto Sapling note consolidation"));
    strUsage += HelpMessageOpt("-consolidatesaplingaddress=<zaddr>", _("Specify Sapling Address to Consolidate. Consolidation address must be the same as sweep  (default: all)"));
    strUsage += HelpMessageOpt("-consolidationtxfee", strprintf(_("Fee amount in Satoshis used send consolidation transactions. (default %i)"), DEFAULT_CONSOLIDATION_FEE));
    strUsage += HelpMessageOpt("-sweep", _("Enable auto Sapling note sweep, automatically move all funds to a sigle address periodocally."));
    strUsage += HelpMessageOpt("-sweepsaplingaddress=<zaddr>", _("Specify Sapling Address to Sweep funds to. (default: all)"));
    strUsage += HelpMessageOpt("-sweeptxfee", strprintf(_("Fee amount in Satoshis used send sweep transactions. (default %i)"), DEFAULT_SWEEP_FEE));
    strUsage += HelpMessageOpt("-deletetx", _("Enable Old Transaction Deletion"));
    strUsage += HelpMessageOpt("-deleteinterval", strprintf(_("Delete transaction every <n> blocks during inital block download (default: %i)"), DEFAULT_TX_DELETE_INTERVAL));
    strUsage += HelpMessageOpt("-keeptxnum", strprintf(_("Keep the last <n> transactions (default: %i)"), DEFAULT_TX_RETENTION_LASTTX));
    strUsage += HelpMessageOpt("-keeptxfornblocks", strprintf(_("Keep transactions for at least <n> blocks (default: %i)"), DEFAULT_TX_RETENTION_BLOCKS));
    if (showDebug)
        strUsage += HelpMessageOpt("-mintxfee=<amt>", strprintf("Fees (in %s/kB) smaller than this are considered zero fee for transaction creation (default: %s)",
            CURRENCY_UNIT, FormatMoney(CWallet::minTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-paytxfee=<amt>", strprintf(_("Fee (in %s/kB) to add to transactions you send (default: %s)"),
        CURRENCY_UNIT, FormatMoney(payTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-rescan", _("Rescan the block chain for missing wallet transactions") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-rescanheight", _("Rescan the block chain from the specified height when rescan=1 on startup"));
    strUsage += HelpMessageOpt("-salvagewallet", _("Attempt to recover private keys from a corrupt wallet.dat") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions", strprintf(_("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange", strprintf(_("Spend unconfirmed change when sending transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>", strprintf(_("If paytxfee is not set, include enough fee so transactions begin confirmation on average within n blocks (default: %u)"), DEFAULT_TX_CONFIRM_TARGET));
    strUsage += HelpMessageOpt("-txexpirydelta", strprintf(_("Set the number of blocks after which a transaction that has not been mined will become invalid (default: %u)"), DEFAULT_TX_EXPIRY_DELTA));
    strUsage += HelpMessageOpt("-maxtxfee=<amt>", strprintf(_("Maximum total fees (in %s) to use in a single wallet transaction; setting this too low may abort large transactions (default: %s)"),
        CURRENCY_UNIT, FormatMoney(maxTxFee)));
    strUsage += HelpMessageOpt("-upgradewallet", _("Upgrade wallet to latest format") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-wallet=<file>", _("Specify wallet file (within data directory)") + " " + strprintf(_("(default: %s)"), "wallet.dat"));
    strUsage += HelpMessageOpt("-walletbroadcast", _("Make the wallet broadcast transactions") + " " + strprintf(_("(default: %u)"), true));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>", _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    strUsage += HelpMessageOpt("-whitelistaddress=<Raddress>", _("Enable the wallet filter for notary nodes and add one Raddress to the whitelist of the wallet filter. If -whitelistaddress= is used, then the wallet filter is automatically activated. Several Raddresses can be defined using several -whitelistaddress= (similar to -addnode). The wallet filter will filter the utxo to only ones coming from my own Raddress (derived from pubkey) and each Raddress defined using -whitelistaddress= this option is mostly for Notary Nodes)."));
    strUsage += HelpMessageOpt("-zapwallettxes=<mode>", _("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
        " " + _("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
#endif

#if ENABLE_ZMQ
    strUsage += HelpMessageGroup(_("ZeroMQ notification options:"));
    strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", _("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", _("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", _("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", _("Enable publish raw transaction in <address>"));
#endif

#if ENABLE_PROTON
    strUsage += HelpMessageGroup(_("AMQP 1.0 notification options:"));
    strUsage += HelpMessageOpt("-amqppubhashblock=<address>", _("Enable publish hash block in <address>"));
    strUsage += HelpMessageOpt("-amqppubhashtx=<address>", _("Enable publish hash transaction in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawblock=<address>", _("Enable publish raw block in <address>"));
    strUsage += HelpMessageOpt("-amqppubrawtx=<address>", _("Enable publish raw transaction in <address>"));
#endif

    strUsage += HelpMessageGroup(_("Debugging/Testing options:"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-checkpoints", strprintf("Disable expensive verification for known chain history (default: %u)", 1));
        strUsage += HelpMessageOpt("-dblogsize=<n>", strprintf("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)", 100));
        strUsage += HelpMessageOpt("-disablesafemode", strprintf("Disable safemode, override a real safe mode event (default: %u)", 0));
        strUsage += HelpMessageOpt("-testsafemode", strprintf("Force safe mode (default: %u)", 0));
        strUsage += HelpMessageOpt("-dropmessagestest=<n>", "Randomly drop 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", "Randomly fuzz 1 of every <n> network messages");
        strUsage += HelpMessageOpt("-flushwallet", strprintf("Run a thread to flush wallet periodically (default: %u)", 1));
        strUsage += HelpMessageOpt("-stopafterblockimport", strprintf("Stop running after importing blocks from disk (default: %u)", 0));
        strUsage += HelpMessageOpt("-nuparams=hexBranchId:activationHeight", "Use given activation height for specified network upgrade (regtest-only)");
    }
    string debugCategories = "addrman, alert, bench, coindb, db, deletetx, estimatefee, http, libevent, lock, mempool, net, partitioncheck, pow, proxy, prune, "
                             "rand, reindex, rpc, selectcoins, tor, zmq, zrpc, zrpcunsafe (implies zrpc)"; // Don't translate these
    strUsage += HelpMessageOpt("-debug=<category>", strprintf(_("Output debugging information (default: %u, supplying <category> is optional)"), 0) + ". " +
        _("If <category> is not supplied or if <category> = 1, output all debugging information.") + " " + _("<category> can be:") + " " + debugCategories + ".");
    strUsage += HelpMessageOpt("-experimentalfeatures", _("Enable use of experimental features"));
    strUsage += HelpMessageOpt("-help-debug", _("Show all debugging options (usage: --help -help-debug)"));
    strUsage += HelpMessageOpt("-logips", strprintf(_("Include IP addresses in debug output (default: %u)"), 0));
    strUsage += HelpMessageOpt("-logtimestamps", strprintf(_("Prepend debug output with timestamp (default: %u)"), 1));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-limitfreerelay=<n>", strprintf("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default: %u)", 15));
        strUsage += HelpMessageOpt("-relaypriority", strprintf("Require high priority for relaying free or low-fee transactions (default: %u)", 0));
        strUsage += HelpMessageOpt("-maxsigcachesize=<n>", strprintf("Limit size of signature cache to <n> entries (default: %u)", 50000));
        strUsage += HelpMessageOpt("-maxtipage=<n>", strprintf("Maximum tip age in seconds to consider node in initial block download (default: %u)", DEFAULT_MAX_TIP_AGE));
    }
    strUsage += HelpMessageOpt("-minrelaytxfee=<amt>", strprintf(_("Fees (in %s/kB) smaller than this are considered zero fee for relaying (default: %s)"),
        CURRENCY_UNIT, FormatMoney(::minRelayTxFee.GetFeePerK())));
    strUsage += HelpMessageOpt("-printtoconsole", _("Send trace/debug info to console instead of debug.log file"));
    if (showDebug)
    {
        strUsage += HelpMessageOpt("-printpriority", strprintf("Log transaction priority and fee per kB when mining blocks (default: %u)", 0));
        strUsage += HelpMessageOpt("-privdb", strprintf("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)", 1));
        strUsage += HelpMessageOpt("-regtest", "Enter regression test mode, which uses a special chain in which blocks can be solved instantly. "
            "This is intended for regression testing tools and app development.");
    }
    strUsage += HelpMessageOpt("-shrinkdebugfile", _("Shrink debug.log file on client startup (default: 1 when no -debug)"));
    strUsage += HelpMessageOpt("-testnet", _("Use the test network"));

    strUsage += HelpMessageGroup(_("Node relay options:"));
    strUsage += HelpMessageOpt("-datacarrier", strprintf(_("Relay and mine data carrier transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-datacarriersize", strprintf(_("Maximum size of data in data carrier transactions we relay and mine (default: %u)"), MAX_OP_RETURN_RELAY));

    strUsage += HelpMessageGroup(_("Block creation options:"));
    strUsage += HelpMessageOpt("-blockminsize=<n>", strprintf(_("Set minimum block size in bytes (default: %u)"), 0));
    strUsage += HelpMessageOpt("-blockmaxsize=<n>", strprintf(_("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE));
    strUsage += HelpMessageOpt("-blockprioritysize=<n>", strprintf(_("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"), DEFAULT_BLOCK_PRIORITY_SIZE));
    if (GetBoolArg("-help-debug", false))
        strUsage += HelpMessageOpt("-blockversion=<n>", strprintf("Override block version to test forking scenarios (default: %d)", (int)CBlock::CURRENT_VERSION));

#ifdef ENABLE_MINING
    strUsage += HelpMessageGroup(_("Mining options:"));
    strUsage += HelpMessageOpt("-gen", strprintf(_("Mine/generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt("-genproclimit=<n>", strprintf(_("Set the number of threads for coin mining if enabled (-1 = all cores, default: %d)"), 0));
    strUsage += HelpMessageOpt("-equihashsolver=<name>", _("Specify the Equihash solver to be used if enabled (default: \"default\")"));
    strUsage += HelpMessageOpt("-largetxthrottle", strprintf(_("Throttle the block template to 1 large transaction and 5 medium transactions per block (default: %u)"), 1));
    strUsage += HelpMessageOpt("-mineraddress=<addr>", _("Send mined coins to a specific single address"));
    strUsage += HelpMessageOpt("-minetolocalwallet", strprintf(
            _("Require that mined blocks use a coinbase address in the local wallet (default: %u)"),
 #ifdef ENABLE_WALLET
            1
 #else
            0
 #endif
            ));
#endif

    strUsage += HelpMessageGroup(_("RPC server options:"));
    strUsage += HelpMessageOpt("-server", _("Accept command line and JSON-RPC commands"));
    strUsage += HelpMessageOpt("-rest", strprintf(_("Accept public REST requests (default: %u)"), 0));
    strUsage += HelpMessageOpt("-rpcbind=<addr>", _("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for IPv6. This option can be specified multiple times (default: bind to all interfaces)"));
    strUsage += HelpMessageOpt("-rpcuser=<user>", _("Username for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcpassword=<pw>", _("Password for JSON-RPC connections"));
    strUsage += HelpMessageOpt("-rpcport=<port>", strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"), 7771, 17771));
    strUsage += HelpMessageOpt("-rpcallowip=<ip>", _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. 1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. 1.2.3.4/24). This option can be specified multiple times"));
    strUsage += HelpMessageOpt("-rpcthreads=<n>", strprintf(_("Set the number of threads to service RPC calls (default: %d)"), DEFAULT_HTTP_THREADS));
    if (showDebug) {
        strUsage += HelpMessageOpt("-rpcworkqueue=<n>", strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE));
        strUsage += HelpMessageOpt("-rpcservertimeout=<n>", strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT));
    }

    // Disabled until we can lock notes and also tune performance of libsnark which by default uses multiple threads
    //strUsage += HelpMessageOpt("-rpcasyncthreads=<n>", strprintf(_("Set the number of threads to service Async RPC calls (default: %d)"), 1));

    if (mode == HMM_BITCOIND) {
        strUsage += HelpMessageGroup(_("Metrics Options (only if -daemon and -printtoconsole are not set):"));
        strUsage += HelpMessageOpt("-showmetrics", _("Show metrics on stdout (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsui", _("Set to 1 for a persistent metrics screen, 0 for sequential metrics output (default: 1 if running in a console, 0 otherwise)"));
        strUsage += HelpMessageOpt("-metricsrefreshtime", strprintf(_("Number of seconds between metrics refreshes (default: %u if running in a console, %u otherwise)"), 1, 600));
    }
    strUsage += HelpMessageGroup(_("Komodo Asset Chain options:"));
    strUsage += HelpMessageOpt("-ac_algo", _("Choose PoW mining algorithm, default is Equihash"));
    strUsage += HelpMessageOpt("-ac_blocktime", _("Block time in seconds, default is 60"));
    strUsage += HelpMessageOpt("-ac_cc", _("Cryptoconditions, default 0"));
    strUsage += HelpMessageOpt("-ac_beam", _("BEAM integration"));
    strUsage += HelpMessageOpt("-ac_coda", _("CODA integration"));
    strUsage += HelpMessageOpt("-ac_cclib", _("Cryptoconditions dynamicly loadable library"));
    strUsage += HelpMessageOpt("-ac_ccenable", _("Cryptoconditions to enable"));
    strUsage += HelpMessageOpt("-ac_ccactivate", _("Block height to enable Cryptoconditions"));
    strUsage += HelpMessageOpt("-ac_decay", _("Percentage of block reward decrease at each halving"));
    strUsage += HelpMessageOpt("-ac_end", _("Block height at which block rewards will end"));
    strUsage += HelpMessageOpt("-ac_eras", _("Block reward eras"));
    strUsage += HelpMessageOpt("-ac_founders", _("Number of blocks between founders reward payouts"));
    strUsage += HelpMessageOpt("-ac_halving", _("Number of blocks between each block reward halving"));
    strUsage += HelpMessageOpt("-ac_name", _("Name of asset chain"));
    strUsage += HelpMessageOpt("-ac_notarypay", _("Pay notaries, default 0"));
    strUsage += HelpMessageOpt("-ac_perc", _("Percentage of block rewards paid to the founder"));
    strUsage += HelpMessageOpt("-ac_private", _("Shielded transactions only (except coinbase + notaries), default is 0"));
    strUsage += HelpMessageOpt("-ac_pubkey", _("Public key for receiving payments on the network"));
    strUsage += HelpMessageOpt("-ac_public", _("Transparent transactions only, default 0"));
    strUsage += HelpMessageOpt("-ac_reward", _("Block reward in satoshis, default is 0"));
    strUsage += HelpMessageOpt("-ac_sapling", _("Sapling activation block height"));
    strUsage += HelpMessageOpt("-ac_script", _("P2SH/multisig address to receive founders rewards"));
    strUsage += HelpMessageOpt("-ac_staked", _("Percentage of blocks that are Proof-Of-Stake, default 0"));
    strUsage += HelpMessageOpt("-ac_supply", _("Starting supply, default is 0"));
    strUsage += HelpMessageOpt("-ac_timelockfrom", _("Timelocked coinbase start height"));
    strUsage += HelpMessageOpt("-ac_timelockgte",  _("Timelocked coinbase minimum amount to be locked"));
    strUsage += HelpMessageOpt("-ac_timelockto",   _("Timelocked coinbase stop height"));
    strUsage += HelpMessageOpt("-ac_txpow", _("Enforce transaction-rate limit, default 0"));

    return strUsage;
}

static void BlockNotifyCallback(bool initialSync, const CBlockIndex *pBlockIndex)
{
    if (initialSync || !pBlockIndex)
        return;

    std::string strCmd = GetArg("-blocknotify", "");
    if (!strCmd.empty()) {
        boost::replace_all(strCmd, "%s", pBlockIndex->GetBlockHash().GetHex());
        boost::thread t(runCommand, strCmd); // thread runs free
    }
}


struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};


// If we're using -prune with -reindex, then delete block files that will be ignored by the
// reindex.  Since reindexing works by starting at block file 0 and looping until a blockfile
// is missing, do the same here to delete any later block files after a gap.  Also delete all
// rev files since they'll be rewritten by the reindex anyway.  This ensures that vinfoBlockFile
// is in sync with what's actually on disk by the time we start downloading, so that pruning
// works correctly.
void CleanupBlockRevFiles()
{
    using namespace boost::filesystem;
    map<string, path> mapBlockFiles;

    // Glob all blk?????.dat and rev?????.dat files from the blocks directory.
    // Remove the rev files immediately and insert the blk file paths into an
    // ordered map keyed by block file index.
    LogPrintf("Removing unusable blk?????.dat and rev?????.dat files for -reindex with -prune\n");
    path blocksdir = GetDataDir() / "blocks";
    for (directory_iterator it(blocksdir); it != directory_iterator(); it++) {
        if (is_regular_file(*it) &&
            it->path().filename().string().length() == 12 &&
            it->path().filename().string().substr(8,4) == ".dat")
        {
            if (it->path().filename().string().substr(0,3) == "blk")
                mapBlockFiles[it->path().filename().string().substr(3,5)] = it->path();
            else if (it->path().filename().string().substr(0,3) == "rev")
                remove(it->path());
        }
    }
    path komodostate = GetDataDir() / KOMODO_STATE_FILENAME;
    remove(komodostate);
    path minerids = GetDataDir() / "minerids";
    remove(minerids);
    // Remove all block files that aren't part of a contiguous set starting at
    // zero by walking the ordered map (keys are block file indices) by
    // keeping a separate counter.  Once we hit a gap (or if 0 doesn't exist)
    // start removing block files.
    int nContigCounter = 0;
    BOOST_FOREACH(const PAIRTYPE(string, path)& item, mapBlockFiles) {
        if (atoi(item.first) == nContigCounter) {
            nContigCounter++;
            continue;
        }
        remove(item.second);
    }
}

void ThreadImport(std::vector<boost::filesystem::path> vImportFiles)
{
    if (nMaxConnections==0) {
        //Cold storage: Offline mode. Skip ThreadImport
        return;
    }

    // Online mode. Start ThreadImport
    RenameThread("zcash-loadblk");
    // -reindex
    if (fReindex) {
        CImportingNow imp;
        int nFile = 0;
        while (true) {
            CDiskBlockPos pos(nFile, 0);
            if (!boost::filesystem::exists(GetBlockPosFilename(pos, "blk")))
                break; // No block files left to reindex
            FILE *file = OpenBlockFile(pos, true);
            if (!file)
                break; // This error is logged in OpenBlockFile
            LogPrintf("Reindexing block file blk%05u.dat...\n", (unsigned int)nFile);
            LoadExternalBlockFile(file, &pos);
            nFile++;
        }
        pblocktree->WriteReindexing(false);
        fReindex = false;
        LogPrintf("Reindexing finished\n");
        // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
        InitBlockIndex();
        KOMODO_LOADINGBLOCKS = false;
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LogPrintf("Importing bootstrap.dat...\n");
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        } else {
            LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
        }
    }

    // -loadblock=
    BOOST_FOREACH(const boost::filesystem::path& path, vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file) {
            CImportingNow imp;
            LogPrintf("Importing blocks file %s...\n", path.string());
            LoadExternalBlockFile(file);
        } else {
            LogPrintf("Warning: Could not open blocks file %s\n", path.string());
        }
    }

    if (GetBoolArg("-stopafterblockimport", false)) {
        LogPrintf("Stopping after block import\n");
        StartShutdown();
    }
}

void ThreadNotifyRecentlyAdded()
{
    while (true) {
        // Run the notifier on an integer second in the steady clock.
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto nextFire = std::chrono::duration_cast<std::chrono::seconds>(
            now + std::chrono::seconds(1));
        std::this_thread::sleep_until(
            std::chrono::time_point<std::chrono::steady_clock>(nextFire));

        boost::this_thread::interruption_point();

        mempool.NotifyRecentlyAdded();
    }
}

/**
 * @brief periodically (every 10 secs) update internal structures
 * @note this does nothing on asset chains, only the kmd chain
 */
void ThreadUpdateKomodoInternals() {
    RenameThread("int-updater");

    int fireDelaySeconds = 10;

    try {
        while (true) {

            if ( chainName.isKMD() )
                fireDelaySeconds = 10;
            else
                fireDelaySeconds = ASSETCHAINS_BLOCKTIME/5 + 1;

            // Run the updater on an integer fireDelaySeconds seconds in the steady clock.
            auto now = std::chrono::steady_clock::now().time_since_epoch();
            auto nextFire = std::chrono::duration_cast<std::chrono::seconds>(
                now + std::chrono::seconds(fireDelaySeconds));
            std::this_thread::sleep_until(
                std::chrono::time_point<std::chrono::steady_clock>(nextFire));

            boost::this_thread::interruption_point();

            if (chainName.isKMD() && KOMODO_NSPV_FULLNODE) {
                komodo_update_interest();
                komodo_longestchain();
            }
        }
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (const std::exception& e) {
        PrintExceptionContinue(&e, "ThreadUpdateKomodoInternals()");
    }
    catch (...) {
        PrintExceptionContinue(NULL, "ThreadUpdateKomodoInternals()");
    }

}

/** Sanity checks
 *  Ensure that Bitcoin is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck(void)
{
    if(!ECC_InitSanityCheck()) {
        InitError("Elliptic curve cryptography sanity check failure. Aborting.");
        return false;
    }
    if (!glibc_sanity_test() || !glibcxx_sanity_test())
        return false;

    return true;
}


static void ZC_LoadParams(
    const CChainParams& chainparams, bool verified
)
{
    struct timeval tv_start, tv_end;
    float elapsed;

    fs::path sapling_spend = ZC_GetParamsDir() / "sapling-spend.params";
    fs::path sapling_output = ZC_GetParamsDir() / "sapling-output.params";
    fs::path sprout_groth16 = ZC_GetParamsDir() / "sprout-groth16.params";

    if (!(
        fs::exists(sapling_spend) &&
        fs::exists(sapling_output) &&
        fs::exists(sprout_groth16)
    )) {
        uiInterface.ThreadSafeMessageBox(strprintf(
            _("Cannot find the Zcash network parameters in the following directory:\n"
              "%s\n"
              "Please run 'zcash-fetch-params' or './zcutil/fetch-params.sh' and then restart."),
                ZC_GetParamsDir()),
            "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }

    static_assert(
        sizeof(fs::path::value_type) == sizeof(codeunit),
        "librustzcash not configured correctly");
    auto sapling_spend_str = sapling_spend.native();
    auto sapling_output_str = sapling_output.native();
    auto sprout_groth16_str = sprout_groth16.native();

    LogPrintf("Loading Sapling (Spend) parameters from %s\n", sapling_spend.string().c_str());
    LogPrintf("Loading Sapling (Output) parameters from %s\n", sapling_output.string().c_str());
    LogPrintf("Loading Sapling (Sprout Groth16) parameters from %s\n", sprout_groth16.string().c_str());
    gettimeofday(&tv_start, 0);

    printf("ZC_LoadParams(): librustzcash_init_zksnark_params()\n");
    printf("sapling_spend: %s\n",sapling_spend_str.c_str() );
    printf("hash: 8270785a1a0d0bc77196f000ee6d221c9c9894f55307bd9357c3f0105d31ca63991ab91324160d8f53e2bbd3c2633a6eb8bdf5205d822e7f3f73edac51b2b70c\n");
    printf("sapling_output: %s\n",sapling_output_str.c_str() );
    printf("hash: 657e3d38dbb5cb5e7dd2970e8b03d69b4787dd907285b5a7f0790dcc8072f60bf593b32cc2d1c030e00ff5ae64bf84c5c3beb84ddc841d48264b4a171744d028\n");
    printf("sprout_groth16:%s\n",sprout_groth16_str.c_str() );
    printf("hash: e9b238411bd6c0ec4791e9d04245ec350c9c5744f5610dfcce4365d5ca49dfefd5054e371842b3f88fa1b9d7e8e075249b3ebabd167fa8b0f3161292d36c180a\n");

    librustzcash_init_zksnark_params(
        reinterpret_cast<const codeunit*>(sprout_groth16_str.c_str()),
        sprout_groth16_str.length(),
        true
    );

    gettimeofday(&tv_end, 0);
    elapsed = float(tv_end.tv_sec-tv_start.tv_sec) + (tv_end.tv_usec-tv_start.tv_usec)/float(1000000);
    LogPrintf("Loaded Sapling parameters in %fs seconds.\n", elapsed);
}

bool AppInitServers(boost::thread_group& threadGroup)
{
    RPCServer::OnStopped(&OnRPCStopped);
    RPCServer::OnPreCommand(&OnRPCPreCommand);
    if (!InitHTTPServer())
        return false;
    if (!StartRPC())
        return false;
    if (!StartHTTPRPC())
        return false;
    if (GetBoolArg("-rest", false) && !StartREST())
        return false;
    if (!StartHTTPServer())
        return false;
    return true;
}

//extern int32_t KOMODO_REWIND;

class InvalidGenesisException : public std::runtime_error
{
public:
    InvalidGenesisException(const std::string& msg) : std::runtime_error(msg) {}
};

/****
 * Attempt to open the databases
 * @param[in] nBlockTreeDBCache size of cache for block tree db
 * @param[in] dbCompression true to compress block tree db files
 * @param[in] dbMaxOpenFiles max number of open files for block tree db
 * @param[in] nCoinDBCache size of cache for coin db
 * @param[out] strLoadError error message
 * @returns true on success
 * @throws InvalidGenesisException if data directory is incorrect
 */
bool AttemptDatabaseOpen(size_t nBlockTreeDBCache, bool dbCompression, size_t dbMaxOpenFiles, size_t nCoinDBCache,
        std::string &strLoadError)
{
    try {
        UnloadBlockIndex();
        delete pcoinsTip;
        delete pcoinsdbview;
        delete pcoinscatcher;
        delete pblocktree;
        delete pnotarisations;

        pblocktree = new CBlockTreeDB(nBlockTreeDBCache, false, fReindex, dbCompression, dbMaxOpenFiles);
        pcoinsdbview = new CCoinsViewDB(nCoinDBCache, false, fReindex);
        pcoinscatcher = new CCoinsViewErrorCatcher(pcoinsdbview);
        pcoinsTip = new CCoinsViewCache(pcoinscatcher);
        pnotarisations = new NotarisationDB(100*1024*1024, false, fReindex);

        if (fReindex) {
            boost::filesystem::remove(GetDataDir() / KOMODO_STATE_FILENAME);
            boost::filesystem::remove(GetDataDir() / "signedmasks");
            pblocktree->WriteReindexing(true);
            //If we're reindexing in prune mode, wipe away unusable block files and all undo data files
            if (fPruneMode)
                CleanupBlockRevFiles();
        }

        if (!LoadBlockIndex(fReindex)) {
            strLoadError = _("Error loading block database");
            return false;
        }

        const CChainParams& chainparams = Params();
        // If the loaded chain has a wrong genesis, bail out immediately
        // (we're likely using a testnet datadir, or the other way around).
        if (!mapBlockIndex.empty() && mapBlockIndex.count(chainparams.GetConsensus().hashGenesisBlock) == 0)
            throw InvalidGenesisException(_("Incorrect or no genesis block found. Wrong datadir for network?"));
        komodo_init(1);
        if (ShutdownRequested()) return false;

        // Initialize the block index (no-op if non-empty database was already loaded)
        if (!InitBlockIndex()) {
            strLoadError = _("Error initializing block database");
            return false;
        }
        KOMODO_LOADINGBLOCKS = false;
        // Check for changed -txindex state
        // if (fTxIndex != GetBoolArg("-txindex", true)) {
        //     strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
        //     return false;
        // }

        // Check for changed -prune state.  What we are concerned about is a user who has pruned blocks
        // in the past, but is now trying to run unpruned.
        if (fHavePruned && !fPruneMode) {
            strLoadError = _("You need to rebuild the database using -reindex to go back to unpruned mode.  This will redownload the entire blockchain");
            return false;
        }

        if ( ASSETCHAINS_CC != 0 && KOMODO_SNAPSHOT_INTERVAL != 0 && chainActive.Height() >= KOMODO_SNAPSHOT_INTERVAL )
        {
            if ( !komodo_dailysnapshot(chainActive.Height()) )
            {
                strLoadError = _("daily snapshot failed, please reindex your chain.");
                return false;
            }
        }

        if (!fReindex) {
            uiInterface.InitMessage(_("Rewinding blocks if needed..."));
            if (!RewindBlockIndex(chainparams)) {
                strLoadError = _("Unable to rewind the database to a pre-upgrade state. You will need to redownload the blockchain");
                return false;
            }
        }

        uiInterface.InitMessage(_("Verifying blocks..."));
        if (fHavePruned && GetArg("-checkblocks", 288) > MIN_BLOCKS_TO_KEEP) {
            LogPrintf("Prune: pruned datadir may not have more than %d blocks; -checkblocks=%d may fail\n",
                MIN_BLOCKS_TO_KEEP, GetArg("-checkblocks", 288));
        }
        if ( KOMODO_REWIND == 0 )
        {
            if (!CVerifyDB().VerifyDB(pcoinsdbview, GetArg("-checklevel", 3),
                                        GetArg("-checkblocks", 288))) {
                strLoadError = _("Corrupted block database detected");
                return false;
            }
        }
    } catch (const std::exception& e) {
        if (fDebug) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening block database");
        return false;
    }

    return true;
}

/***
 * Initialize everything and fire up the services
 * @pre Parameters should be parsed and config file should be read
 * @param threadGroup
 * @param scheduler
 * @returns true on success
 */
bool AppInit2(boost::thread_group& threadGroup, CScheduler& scheduler)
{
    // ********************************************************* Step 1: setup
#ifdef _MSC_VER
    // Turn off Microsoft heap dump noise
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, 0));
#endif
#if _MSC_VER >= 1400
    // Disable confusing "helpful" text message on abort, Ctrl-C
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef _WIN32
    // Enable Data Execution Prevention (DEP)
    // Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
    // A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
    // We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
    // which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
    typedef BOOL (WINAPI *PSETPROCDEPPOL)(DWORD);
    PSETPROCDEPPOL setProcDEPPol = (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
    if (setProcDEPPol != NULL) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

    if (!SetupNetworking())
        return InitError("Error: Initializing networking failed");

#ifndef _WIN32
    if (GetBoolArg("-sysperms", false)) {
#ifdef ENABLE_WALLET
        if (!GetBoolArg("-disablewallet", false))
            return InitError("Error: -sysperms is not allowed in combination with enabled wallet functionality");
#endif
    } else {
        umask(077);
    }

    // Clean shutdown on SIGTERM
    struct sigaction sa;
    sa.sa_handler = HandleSIGTERM;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // Reopen debug.log on SIGHUP
    struct sigaction sa_hup;
    sa_hup.sa_handler = HandleSIGHUP;
    sigemptyset(&sa_hup.sa_mask);
    sa_hup.sa_flags = 0;
    sigaction(SIGHUP, &sa_hup, NULL);

    // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
    struct sigaction sa_pipe;
    sa_pipe.sa_handler = SIG_IGN;
    sigemptyset(&sa_pipe.sa_mask);
    sa_pipe.sa_flags = 0;
    sigaction(SIGPIPE, &sa_pipe, NULL);

#endif

    std::set_new_handler(new_handler_terminate);

    // ********************************************************* Step 2: parameter interactions
    const CChainParams& chainparams = Params();

    // Set this early so that experimental features are correctly enabled/disabled
    fExperimentalMode = GetBoolArg("-experimentalfeatures", true);

    // Fail early if user has set experimental options without the global flag
    if (!fExperimentalMode) {
        if (mapArgs.count("-developerencryptwallet")) {
            return InitError(_("Wallet encryption requires -experimentalfeatures."));
        }
        else if (mapArgs.count("-paymentdisclosure")) {
            return InitError(_("Payment disclosure requires -experimentalfeatures."));
        } else if (mapArgs.count("-zmergetoaddress")) {
            return InitError(_("RPC method z_mergetoaddress requires -experimentalfeatures."));
        }
    }

    // Set this early so that parameter interactions go to console
    fPrintToConsole = GetBoolArg("-printtoconsole", false);
    fLogTimestamps = GetBoolArg("-logtimestamps", true);
    fLogIPs = GetBoolArg("-logips", false);

    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("Pirate version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);

    //Get Thread Metrics
    // unsigned int minThreads = 1;
    maxProcessingThreads = std::max((int)std::thread::hardware_concurrency() - 1, 1);

    if (mapArgs.count("-maxprocessingthreads")) {
        int processingThreads = GetArg("-maxprocessingthreads", 0);
        if (processingThreads < 0) {
            return InitError(_("Maximum number of processing threads cannot be negative"));
        } else if (processingThreads > 0) {
            maxProcessingThreads = processingThreads;
        }
    }
    LogPrintf("Maximum number of processing threads used in multithreaded functions %i\n", maxProcessingThreads);

    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified

    // Check plaintext peers

    if (mapMultiArgs.count("-plaintextpeer")) {
        const std::vector<std::string>& vAllow = mapMultiArgs["-plaintextpeer"];
        BOOST_FOREACH (std::string strAllow, vAllow) {
            CSubNet subnet;
            LookupSubNet(strAllow.c_str(), subnet);
            if (!subnet.IsValid()) {
                return InitError(strprintf(_("Invalid -plaintextpeer %s."), strAllow));
            } else {
                LogPrintf("Whitelisting unencrypted p2p connection for %s\n", strAllow);
            }
        }
    }

    if (mapArgs.count("-bind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -bind set -> setting -listen=1\n", __func__);
    }
    if (mapArgs.count("-whitebind")) {
        if (SoftSetBoolArg("-listen", true))
            LogPrintf("%s: parameter interaction: -whitebind set -> setting -listen=1\n", __func__);
    }

    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0) {
        // when only connecting to trusted nodes, do not seed via DNS, or listen by default
        if (SoftSetBoolArg("-dnsseed", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -dnsseed=0\n", __func__);
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -connect set -> setting -listen=0\n", __func__);
    }

    if (mapArgs.count("-proxy")) {
        // to protect privacy, do not listen by default if a default proxy server is specified
        if (SoftSetBoolArg("-listen", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
        // to protect privacy, do not discover addresses by default
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -proxy set -> setting -discover=0\n", __func__);
    }

    if (!GetBoolArg("-listen", DEFAULT_LISTEN)) {
        // do not try to retrieve public IP when not listening (pointless)
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -discover=0\n", __func__);
        if (SoftSetBoolArg("-listenonion", false))
            LogPrintf("%s: parameter interaction: -listen=0 -> setting -listenonion=0\n", __func__);
    }

    if (mapArgs.count("-externalip")) {
        // if an explicit public IP is specified, do not try to find others
        if (SoftSetBoolArg("-discover", false))
            LogPrintf("%s: parameter interaction: -externalip set -> setting -discover=0\n", __func__);
    }

    // Read asmap file if configured
    if (mapArgs.count("-asmap")) {
        fs::path asmap_path = fs::path(GetArg("-asmap", ""));
        if (asmap_path.empty()) {
            asmap_path = DEFAULT_ASMAP_FILENAME;
        }
        if (!asmap_path.is_absolute()) {
            asmap_path = GetDataDir() / asmap_path;
        }
        if (!fs::exists(asmap_path)) {
            InitError(strprintf(_("Could not find asmap file %s"), asmap_path));
            return false;
        }
        std::vector<bool> asmap = CAddrMan::DecodeAsmap(asmap_path);
        if (asmap.size() == 0) {
            InitError(strprintf(_("Could not parse asmap file %s"), asmap_path));
            return false;
        }
        const uint256 asmap_version = SerializeHash(asmap);
        addrman.m_asmap = std::move(asmap); // //node.connman->SetAsmap(std::move(asmap));
        LogPrintf("Using asmap version %s for IP bucketing\n", asmap_version.ToString());
    } else {
        LogPrintf("Using /16 prefix for IP bucketing\n");
    }

    if (GetBoolArg("-salvagewallet", false)) {
        // Rewrite just private keys: rescan to find transactions
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -salvagewallet=1 -> setting -rescan=1\n", __func__);
    }

    // -zapwallettx implies a rescan
    if (GetBoolArg("-zapwallettxes", false)) {
        if (SoftSetBoolArg("-rescan", true))
            LogPrintf("%s: parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n", __func__);
    }

    // Make sure enough file descriptors are available
    int nBind = std::max((int)mapArgs.count("-bind") + (int)mapArgs.count("-whitebind"), 1);
    nMaxConnections = GetArg("-maxconnections", DEFAULT_MAX_PEER_CONNECTIONS);
    //fprintf(stderr,"nMaxConnections %d\n",nMaxConnections);
    nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
    int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
    //fprintf(stderr,"nMaxConnections %d FD_SETSIZE.%d nBind.%d expr.%d \n",nMaxConnections,FD_SETSIZE,nBind,(int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS));
    if (nFD < MIN_CORE_FILEDESCRIPTORS)
        return InitError(_("Not enough file descriptors available."));
    if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections)
        nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;
    if (bOverrideMaxConnections==true)
    {
        //fprintf(stderr,"init: GUI config override maxconnections=%d\n",nMaxConnections);
        nMaxConnections=0;
    }
    // if using block pruning, then disable txindex
    // also disable the wallet (for now, until SPV support is implemented in wallet)
//     if (GetArg("-prune", 0)) {
//         if (GetBoolArg("-txindex", true))
//             return InitError(_("Prune mode is incompatible with -txindex."));
// #ifdef ENABLE_WALLET
//         if (!GetBoolArg("-disablewallet", false)) {
//             if (SoftSetBoolArg("-disablewallet", true))
//                 LogPrintf("%s : parameter interaction: -prune -> setting -disablewallet=1\n", __func__);
//             else
//                 return InitError(_("Can't run with a wallet in prune mode."));
//         }
// #endif
//     }

    // ********************************************************* Step 3: parameter-to-internal-flags

    fDebug = !mapMultiArgs["-debug"].empty();
    // Special-case: if -debug=0/-nodebug is set, turn off debugging messages
    const vector<string>& categories = mapMultiArgs["-debug"];
    if (GetBoolArg("-nodebug", false) || find(categories.begin(), categories.end(), string("0")) != categories.end())
        fDebug = false;

    // Special case: if debug=zrpcunsafe, implies debug=zrpc, so add it to debug categories
    if (find(categories.begin(), categories.end(), string("zrpcunsafe")) != categories.end()) {
        if (find(categories.begin(), categories.end(), string("zrpc")) == categories.end()) {
            LogPrintf("%s: parameter interaction: setting -debug=zrpcunsafe -> -debug=zrpc\n", __func__);
            vector<string>& v = mapMultiArgs["-debug"];
            v.push_back("zrpc");
        }
    }

    // Check for -debugnet
    if (GetBoolArg("-debugnet", false))
        InitWarning(_("Warning: Unsupported argument -debugnet ignored, use -debug=net."));
    // Check for -socks - as this is a privacy risk to continue, exit here
    if (mapArgs.count("-socks"))
        return InitError(_("Error: Unsupported argument -socks found. Setting SOCKS version isn't possible anymore, only SOCKS5 proxies are supported."));
    // Check for -tor - as this is a privacy risk to continue, exit here
    if (GetBoolArg("-tor", false))
        return InitError(_("Error: Unsupported argument -tor found, use -onion."));

    if (GetBoolArg("-benchmark", false))
        InitWarning(_("Warning: Unsupported argument -benchmark ignored, use -debug=bench."));

    // Checkmempool and checkblockindex default to true in regtest mode
    int ratio = std::min<int>(std::max<int>(GetArg("-checkmempool", chainparams.DefaultConsistencyChecks() ? 1 : 0), 0), 1000000);
    if (ratio != 0) {
        mempool.setSanityCheck(1.0 / ratio);
    }
    fCheckBlockIndex = GetBoolArg("-checkblockindex", chainparams.DefaultConsistencyChecks());
    fCheckpointsEnabled = GetBoolArg("-checkpoints", true);

    // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
    nScriptCheckThreads = GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
    if (nScriptCheckThreads <= 0)
        nScriptCheckThreads += GetNumCores();
    if (nScriptCheckThreads <= 1)
        nScriptCheckThreads = 0;
    else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
        nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;

    fServer = GetBoolArg("-server", false);

    // block pruning; get the amount of disk space (in MB) to allot for block & undo files
    // int64_t nSignedPruneTarget = GetArg("-prune", 0) * 1024 * 1024;
    // if (nSignedPruneTarget < 0) {
    //     return InitError(_("Prune cannot be configured with a negative value."));
    // }
    // nPruneTarget = (uint64_t) nSignedPruneTarget;
    // if (nPruneTarget) {
    //     if (nPruneTarget < MIN_DISK_SPACE_FOR_BLOCK_FILES) {
    //         return InitError(strprintf(_("Prune configured below the minimum of %d MB.  Please use a higher number."), MIN_DISK_SPACE_FOR_BLOCK_FILES / 1024 / 1024));
    //     }
    //     LogPrintf("Prune configured to target %uMiB on disk for block and undo files.\n", nPruneTarget / 1024 / 1024);
    //     fPruneMode = true;
    // }

    RegisterAllCoreRPCCommands(tableRPC);
#ifdef ENABLE_WALLET
    bool fDisableWallet = GetBoolArg("-disablewallet", false);
    if ( KOMODO_NSPV_SUPERLITE )
    {
        fDisableWallet = true;
        nLocalServices = 0;
    }
    if (!fDisableWallet)
        RegisterWalletRPCCommands(tableRPC);
#endif

    nConnectTimeout = GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
    if (nConnectTimeout <= 0)
        nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

    // Fee-per-kilobyte amount considered the same as "free"
    // If you are mining, be careful setting this:
    // if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    if (mapArgs.count("-minrelaytxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-minrelaytxfee"], n) && n > 0)
            ::minRelayTxFee = CFeeRate(n);
        else
            return InitError(strprintf(_("Invalid amount for -minrelaytxfee=<amount>: '%s'"), mapArgs["-minrelaytxfee"]));
    }

#ifdef ENABLE_WALLET
    if (mapArgs.count("-mintxfee"))
    {
        CAmount n = 0;
        if (ParseMoney(mapArgs["-mintxfee"], n) && n > 0)
            CWallet::minTxFee = CFeeRate(n);
        else
            return InitError(strprintf(_("Invalid amount for -mintxfee=<amount>: '%s'"), mapArgs["-mintxfee"]));
    }
    if (mapArgs.count("-paytxfee"))
    {
        CAmount nFeePerK = 0;
        if (!ParseMoney(mapArgs["-paytxfee"], nFeePerK))
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s'"), mapArgs["-paytxfee"]));
        if (nFeePerK > nHighTransactionFeeWarning)
            InitWarning(_("Warning: -paytxfee is set very high! This is the transaction fee you will pay if you send a transaction."));
        payTxFee = CFeeRate(nFeePerK, 1000);
        if (payTxFee < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -paytxfee=<amount>: '%s' (must be at least %s)"),
                                       mapArgs["-paytxfee"], ::minRelayTxFee.ToString()));
        }
    }
    if (mapArgs.count("-maxtxfee"))
    {
        CAmount nMaxFee = 0;
        if (!ParseMoney(mapArgs["-maxtxfee"], nMaxFee))
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s'"), mapArgs["-maptxfee"]));
        if (nMaxFee > nHighTransactionMaxFeeWarning)
            InitWarning(_("Warning: -maxtxfee is set very high! Fees this large could be paid on a single transaction."));
        maxTxFee = nMaxFee;
        if (CFeeRate(maxTxFee, 1000) < ::minRelayTxFee)
        {
            return InitError(strprintf(_("Invalid amount for -maxtxfee=<amount>: '%s' (must be at least the minrelay fee of %s to prevent stuck transactions)"),
                                       mapArgs["-maxtxfee"], ::minRelayTxFee.ToString()));
        }
    }
    nTxConfirmTarget = GetArg("-txconfirmtarget", DEFAULT_TX_CONFIRM_TARGET);
    expiryDelta = GetArg("-txexpirydelta", DEFAULT_TX_EXPIRY_DELTA);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", true);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", false);

    std::string strWalletFile = GetArg("-wallet", "wallet.dat");
#endif // ENABLE_WALLET

    fIsBareMultisigStd = GetBoolArg("-permitbaremultisig", true);
    nMaxDatacarrierBytes = GetArg("-datacarriersize", nMaxDatacarrierBytes);

    fAlerts = GetBoolArg("-alerts", DEFAULT_ALERTS);

    // Option to startup with mocktime set (used for regression testing):
    SetMockTime(GetArg("-mocktime", 0)); // SetMockTime(0) is a no-op

    if ( KOMODO_NSPV_FULLNODE )
    {
        if (GetBoolArg("-peerbloomfilters", true))
            nLocalServices |= NODE_BLOOM;
    }
    nMaxTipAge = GetArg("-maxtipage", DEFAULT_MAX_TIP_AGE);

#ifdef ENABLE_MINING
    if (mapArgs.count("-mineraddress")) {
        CTxDestination addr = DecodeDestination(mapArgs["-mineraddress"]);
        if (!IsValidDestination(addr)) {
            return InitError(strprintf(
                _("Invalid address for -mineraddress=<addr>: '%s' (must be a transparent address)"),
                mapArgs["-mineraddress"]));
        }
    }
#endif

    // Default value of 0 for mempooltxinputlimit means no limit is applied
    if (mapArgs.count("-mempooltxinputlimit")) {
        int64_t limit = GetArg("-mempooltxinputlimit", 0);
        if (limit < 0) {
            return InitError(_("Mempool limit on transparent inputs to a transaction cannot be negative"));
        } else if (limit > 0) {
            LogPrintf("Mempool configured to reject transactions with greater than %lld transparent inputs\n", limit);
        }
    }

    if (!mapMultiArgs["-nuparams"].empty()) {
        // Allow overriding network upgrade parameters for testing
        if (Params().NetworkIDString() != "regtest") {
            return InitError("Network upgrade parameters may only be overridden on regtest.");
        }
        const vector<string>& deployments = mapMultiArgs["-nuparams"];
        for (auto i : deployments) {
            std::vector<std::string> vDeploymentParams;
            boost::split(vDeploymentParams, i, boost::is_any_of(":"));
            if (vDeploymentParams.size() != 2) {
                return InitError("Network upgrade parameters malformed, expecting hexBranchId:activationHeight");
            }
            int nActivationHeight;
            if (!ParseInt32(vDeploymentParams[1], &nActivationHeight)) {
                return InitError(strprintf("Invalid nActivationHeight (%s)", vDeploymentParams[1]));
            }
            bool found = false;
            // Exclude Sprout from upgrades
            for (auto i = Consensus::BASE_SPROUT + 1; i < Consensus::MAX_NETWORK_UPGRADES; ++i)
            {
                if (vDeploymentParams[0].compare(HexInt(NetworkUpgradeInfo[i].nBranchId)) == 0) {
                    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(i), nActivationHeight);
                    found = true;
                    LogPrintf("Setting network upgrade activation parameters for %s to height=%d\n", vDeploymentParams[0], nActivationHeight);
                    break;
                }
            }
            if (!found) {
                return InitError(strprintf("Invalid network upgrade (%s)", vDeploymentParams[0]));
            }
        }
    }

    // ********************************************************* Step 4: application initialization: dir lock, daemonize, pidfile, debug log

    // Initialize libsodium
    if (init_and_check_sodium() == -1) {
        return false;
    }

    // Initialize elliptic curve code
    std::string sha256_algo = SHA256AutoDetect();
    LogPrintf("Using the '%s' SHA256 implementation\n", sha256_algo);
    ECC_Start();
    globalVerifyHandle.reset(new ECCVerifyHandle());

    // Sanity check
    if (!InitSanityCheck())
        return InitError(_("Initialization sanity check failed. Komodo is shutting down."));

    std::string strDataDir = GetDataDir().string();
#ifdef ENABLE_WALLET
    // Wallet file must be a plain filename without a directory
    if (strWalletFile != boost::filesystem::basename(strWalletFile) + boost::filesystem::extension(strWalletFile))
        return InitError(strprintf(_("Wallet %s resides outside data directory %s"), strWalletFile, strDataDir));
#endif
    // Make sure only a single Bitcoin process is using the data directory.
    boost::filesystem::path pathLockFile = GetDataDir() / ".lock";
    FILE* file = fopen(pathLockFile.string().c_str(), "a"); // empty lock file; created if it doesn't exist.
    if (file) fclose(file);

    try {
        static boost::interprocess::file_lock lock(pathLockFile.string().c_str());
        if (!lock.try_lock())
            return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Komodo is probably already running."), strDataDir));
    } catch(const boost::interprocess::interprocess_exception& e) {
        return InitError(strprintf(_("Cannot obtain a lock on data directory %s. Komodo is probably already running.") + " %s.", strDataDir, e.what()));
    }

#ifndef _WIN32
    CreatePidFile(GetPidFile(), getpid());
#endif
    if (GetBoolArg("-shrinkdebugfile", !fDebug))
        ShrinkDebugFile();
    LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
    LogPrintf("Komodo version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);

    if (fPrintToDebugLog)
        OpenDebugLog();
    LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
#ifdef ENABLE_WALLET
    LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
#endif
    if (!fLogTimestamps)
        LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
    LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
    LogPrintf("Using data directory %s\n", strDataDir);
    LogPrintf("Using config file %s\n", GetConfigFile().string());
    LogPrintf("Using at most %i connections (%i file descriptors available)\n", nMaxConnections, nFD);
    std::ostringstream strErrors;

    LogPrintf("Using %u threads for script verification\n", nScriptCheckThreads);
    if (nScriptCheckThreads) {
        for (int i=0; i<nScriptCheckThreads-1; i++)
            threadGroup.create_thread(&ThreadScriptCheck);
    }

    // Start the lightweight task scheduler thread
    CScheduler::Function serviceLoop = boost::bind(&CScheduler::serviceQueue, &scheduler);
    threadGroup.create_thread(boost::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

    // Count uptime
    MarkStartTime();

    if ((chainparams.NetworkIDString() != "regtest") &&
            GetBoolArg("-showmetrics", 0) &&
            !fPrintToConsole && !GetBoolArg("-daemon", false)) {
        // Start the persistent metrics interface
        ConnectMetricsScreen();
        threadGroup.create_thread(&ThreadShowMetricsScreen);
    }

    // These must be disabled for now, they are buggy and we probably don't
    // want any of libsnark's profiling in production anyway.
    libsnark::inhibit_profiling_info = true;
    libsnark::inhibit_profiling_counters = true;

    if ( KOMODO_NSPV_FULLNODE )
    {
        // Initialize Zcash circuit parameters
        uiInterface.InitMessage(_("Verifying Params..."));
        initalizeMapParam();
        bool paramsVerified = checkParams();
        if(!paramsVerified) {
            downloadFiles("Network Params");
        }
        if (fRequestShutdown)
        {
            LogPrintf("Shutdown requested. Exiting.\n");
            return false;
        }

        ZC_LoadParams(chainparams, paramsVerified);
    }

    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }

    /* Start the RPC server already.  It will be started in "warmup" mode
     * and not really process calls already (but it will signify connections
     * that the server is there and will be ready later).  Warmup mode will
     * be disabled when initialisation is finished.
     */
    if (fServer)
    {
        uiInterface.InitMessage.connect(SetRPCWarmupStatus);
        if (!AppInitServers(threadGroup))
            return InitError(_("Unable to start HTTP server. See debug log for details."));
    }

    int64_t nStart = GetTimeMillis();

    // ********************************************************* Step 5: verify wallet database integrity
#ifdef ENABLE_WALLET
    if (!fDisableWallet) {
        LogPrintf("Using wallet %s\n", strWalletFile);
        uiInterface.InitMessage(_("Verifying wallet..."));

        std::string warningString;
        std::string errorString;

        if (!CWallet::Verify(strWalletFile, warningString, errorString))
            return false;

        if (!warningString.empty())
            InitWarning(warningString);
        if (!errorString.empty())
            return InitError(warningString);

    } // (!fDisableWallet)
#endif // ENABLE_WALLET
    // ********************************************************* Step 6: network initialization

    RegisterNodeSignals(GetNodeSignals());

    //Load peers from peers.dat
    LoadPeers();

    // sanitize comments per BIP-0014, format user agent and check total size
    std::vector<string> uacomments;
    BOOST_FOREACH(string cmt, mapMultiArgs["-uacomment"])
    {
        if (cmt != SanitizeString(cmt, SAFE_CHARS_UA_COMMENT))
            return InitError(strprintf("User Agent comment (%s) contains unsafe characters.", cmt));
        uacomments.push_back(SanitizeString(cmt, SAFE_CHARS_UA_COMMENT));
    }
    strSubVersion = FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, uacomments);
    if (strSubVersion.size() > MAX_SUBVERSION_LENGTH) {
        return InitError(strprintf("Total length of network version string %i exceeds maximum of %i characters. Reduce the number and/or size of uacomments.",
            strSubVersion.size(), MAX_SUBVERSION_LENGTH));
    }

    if (mapArgs.count("-whitelist")) {
        BOOST_FOREACH(const std::string& net, mapMultiArgs["-whitelist"]) {
            CSubNet subnet;
            LookupSubNet(net.c_str(), subnet);
            if (!subnet.IsValid())
                return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
            CNode::AddWhitelistedRange(subnet);
        }
    }

    if (mapArgs.count("-onlynet")) {
        std::set<enum Network> nets;
        BOOST_FOREACH(const std::string& snet, mapMultiArgs["-onlynet"]) {
            enum Network net = ParseNetwork(snet);
            if (net == NET_UNROUTABLE)
                return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
            nets.insert(net);
        }
        for (int n = 0; n < NET_MAX; n++) {
            enum Network net = (enum Network)n;
            if (!nets.count(net)) {
                SetReachable(net, false);
            }
        }
    }

    if (GetBoolArg("-disableipv4", false)) {
        enum Network net = ParseNetwork("ipv4");
        SetReachable(net, false);
    }

    if (GetBoolArg("-disableipv6", false)) {
        enum Network net = ParseNetwork("ipv6");
        SetReachable(net, false);
    }

    bool proxyRandomize = GetBoolArg("-proxyrandomize", true);
    // -proxy sets a proxy for all outgoing network traffic
    // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
    std::string proxyArg = GetArg("-proxy", "");
    SetReachable(NET_ONION,false);
    if (proxyArg != "" && proxyArg != "0") {
        CService resolved(LookupNumeric(proxyArg.c_str(), 9050));
        proxyType addrProxy = proxyType(resolved, proxyRandomize);
        if (!addrProxy.IsValid())
            return InitError(strprintf(_("Invalid -proxy address: '%s'"), proxyArg));

        SetProxy(NET_IPV4, addrProxy);
        SetProxy(NET_IPV6, addrProxy);
        SetProxy(NET_ONION, addrProxy);
        SetNameProxy(addrProxy);
        SetReachable(NET_ONION, true); // by default, -proxy sets onion as reachable, unless -noonion later
    }

    const std::string& i2psam_arg = GetArg("-i2psam", "");
    if (!i2psam_arg.empty()) {
        CService addr;
        if (!Lookup(i2psam_arg.c_str(), addr, 7656, fNameLookup) || !addr.IsValid()) {
            return InitError(strprintf(_("Invalid -i2psam address or hostname: '%s'"), i2psam_arg));
        }
        SetReachable(NET_I2P, true);
        SetProxy(NET_I2P, proxyType{addr});
    } else {
        SetReachable(NET_I2P, false);
    }

    // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
    // -noonion (or -onion=0) disables connecting to .onion entirely
    // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
    std::string onionArg = GetArg("-onion", "");
    if (onionArg != "") {
        if (onionArg == "0") { // Handle -noonion/-onion=0
            SetReachable(NET_ONION,false); // set onions as unreachable
        } else {
            CService resolved(LookupNumeric(onionArg.c_str(), 9050));
            proxyType addrOnion = proxyType(resolved, proxyRandomize);
            if (!addrOnion.IsValid())
                return InitError(strprintf(_("Invalid -onion address: '%s'"), onionArg));
            SetProxy(NET_ONION, addrOnion);
            SetReachable(NET_ONION, true);
        }
    }

    // see Step 2: parameter interactions for more information about these
    fListen = GetBoolArg("-listen", DEFAULT_LISTEN);
    fDiscover = GetBoolArg("-discover", true);
    fNameLookup = GetBoolArg("-dns", true);

    bool fBound = false;
    if (fListen) {
        if (mapArgs.count("-bind") || mapArgs.count("-whitebind")) {
            BOOST_FOREACH(const std::string& strBind, mapMultiArgs["-bind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
                    return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
            }
            BOOST_FOREACH(const std::string& strBind, mapMultiArgs["-whitebind"]) {
                CService addrBind;
                if (!Lookup(strBind.c_str(), addrBind, 0, false))
                    return InitError(strprintf(_("Cannot resolve -whitebind address: '%s'"), strBind));
                if (addrBind.GetPort() == 0)
                    return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
                fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
            }
        }
        else {
            struct in_addr inaddr_any;
            inaddr_any.s_addr = INADDR_ANY;
            fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
            fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
        }
        if (!fBound)
            return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
    }

    if (mapArgs.count("-externalip")) {
        BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-externalip"]) {

            CService addrLocal;
            if (Lookup(strAddr.c_str(), addrLocal, GetListenPort(), fNameLookup) && addrLocal.IsValid()) {
                AddLocal(addrLocal, LOCAL_MANUAL);
            } else {
                return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
            }
        }
    }

    BOOST_FOREACH(const std::string& strDest, mapMultiArgs["-seednode"])
        AddOneShot(strDest);

        if (mapArgs.count("-tlskeypath")) {
        boost::filesystem::path pathTLSKey(GetArg("-tlskeypath", ""));
    if (!boost::filesystem::exists(pathTLSKey))
         return InitError(strprintf(_("Cannot find TLS key file: '%s'"), pathTLSKey.string()));
    }

    if (mapArgs.count("-tlscertpath")) {
        boost::filesystem::path pathTLSCert(GetArg("-tlscertpath", ""));
    if (!boost::filesystem::exists(pathTLSCert))
        return InitError(strprintf(_("Cannot find TLS cert file: '%s'"), pathTLSCert.string()));
    }

    if (mapArgs.count("-tlstrustdir")) {
        boost::filesystem::path pathTLSTrustredDir(GetArg("-tlstrustdir", ""));
        if (!boost::filesystem::exists(pathTLSTrustredDir))
            return InitError(strprintf(_("Cannot find trusted certificates directory: '%s'"), pathTLSTrustredDir.string()));
    }

    if (!usingGUI) {
        SoftSetArg("-tlsenforcement", std::string("0"));
    }

#if ENABLE_ZMQ
    pzmqNotificationInterface = CZMQNotificationInterface::CreateWithArguments(mapArgs);

    if (pzmqNotificationInterface) {
        RegisterValidationInterface(pzmqNotificationInterface);
    }
#endif

#if ENABLE_PROTON
    pAMQPNotificationInterface = AMQPNotificationInterface::CreateWithArguments(mapArgs);

    if (pAMQPNotificationInterface) {

        // AMQP support is currently an experimental feature, so fail if user configured AMQP notifications
        // without enabling experimental features.
        if (!fExperimentalMode) {
            return InitError(_("AMQP support requires -experimentalfeatures."));
        }

        RegisterValidationInterface(pAMQPNotificationInterface);
    }
#endif

    if ( KOMODO_NSPV_SUPERLITE )
    {
        std::vector<boost::filesystem::path> vImportFiles;
        threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));
        StartNode(threadGroup, scheduler);
        pcoinsTip = new CCoinsViewCache(pcoinscatcher);
        InitBlockIndex();
        SetRPCWarmupFinished();
        uiInterface.InitMessage(_("Done loading"));
        pwalletMain = new CWallet("tmptmp.wallet");
        return !ShutdownRequested();
    }
    // ********************************************************* Step 7: load block chain

    fReindex = GetBoolArg("-reindex", false);




    bool useBootstrap = false;
    bool newInstall = false;


    //Parameter set on initial creation of PIRATE.conf.
    newInstall = GetBoolArg("-setup_cold_storage", false);

    //Prompt on new install: Cold storage or normal operation?
    if (newInstall && !IsArgSet("maxconnections")) {
        int fColdStorage_Offline = uiInterface.ThreadSafeMessageBox(
            "\n\n" + _("New install detected.\n\nPress YES to setup this wallet in the tradional online mode, i.e. a full function wallet that can create, authorise (sign) and send transactions.\n\nPress No to setup this instance as a cold storage offline wallet which only authorise (sign) transactions"),
            "", CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL | CClientUIInterface::BTN_YES | CClientUIInterface::BTN_NO );
        if (fColdStorage_Offline==CClientUIInterface::BTN_NO) {
            //Cold storage offline mode
            nMaxConnections=0;
            useBootstrap = false;

            //Note: By this time the GUI configuration files: '~/.config/Pirate Chain/Treasure\ Chest.conf' and
            //      PIRATE.conf are already created. We'll have to update the GUI configuration from
            //      the UI code
        }
    }

    if (nMaxConnections>0) //Online mode
    {
        //Note: 'bootstrapinstall' also set when PIRATE.conf is created
        newInstall = GetBoolArg("-bootstrapinstall", false);
        if (!boost::filesystem::exists(GetDataDir() / "blocks") || !boost::filesystem::exists(GetDataDir() / "chainstate"))
            newInstall = true;

        //Prompt on new install
        if (newInstall && !GetBoolArg("-bootstrap", false)) {
            int fBoot = uiInterface.ThreadSafeMessageBox(
                "\n\n" + _("New install detected.\n\nPress OK to download the blockchain bootstrap (faster, less secure).\n\nPress Cancel to continue on and sync the blockchain from peer nodes (slower, more secure)."),
                "", CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL | CClientUIInterface::BTN_OK | CClientUIInterface::BTN_CANCEL);
            if (fBoot==CClientUIInterface::BTN_OK) {
                useBootstrap = true;
            }
        }

        //Prompt GUI
        if (GetBoolArg("-bootstrap", false) && GetArg("-bootstrap", "1") != "2" && !useBootstrap) {
            int fBoot = uiInterface.ThreadSafeMessageBox(
                "\n\n" + _("Bootstrap option detected.\n\nPress OK to download the blockchain bootstrap (faster, less secure).\n\nPress Cancel to continue on and sync the blockchain from peer nodes (slower, more secure)."),
                "", CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL | CClientUIInterface::BTN_OK | CClientUIInterface::BTN_CANCEL);
            if (fBoot==CClientUIInterface::BTN_OK) {
                useBootstrap = true;
            }
        }

        //Force Download- used for CLI
        if (GetBoolArg("-bootstrap", false) && GetArg("-bootstrap", "1") == "2") {
            useBootstrap = true;
        }

        if (useBootstrap) {
            fReindex = false;
            //wipe transactions from wallet to create a clean slate
            OverrideSetArg("-zappwallettxes","2");
            boost::filesystem::remove_all(GetDataDir() / "blocks");
            boost::filesystem::remove_all(GetDataDir() / "chainstate");
            boost::filesystem::remove_all(GetDataDir() / "notarisations");
            boost::filesystem::remove(GetDataDir() / "komodostate");
            boost::filesystem::remove(GetDataDir() / "signedmasks");
            boost::filesystem::remove(GetDataDir() / "komodostate.ind");
            if (!getBootstrap() && !fRequestShutdown ) {
                int keepRunning = uiInterface.ThreadSafeMessageBox(
                    "\n\n" + _("Bootstrap download failed!!!\n\nPress OK to continue and sync from the network."),
                    "", CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MSG_INFORMATION | CClientUIInterface::MODAL | CClientUIInterface::BTN_OK | CClientUIInterface::BTN_CANCEL);

                if (!keepRunning==CClientUIInterface::BTN_CANCEL) {
                    fRequestShutdown = true;
                }
            }
        }

        if (fRequestShutdown)
        {
            LogPrintf("Shutdown requested. Exiting.\n");
            return false;
        }
    }
    //Create 'blocks' as part of the required directory structure for online and offline mode:
    boost::filesystem::create_directories(GetDataDir() / "blocks");
    
    // block tree db settings
    int dbMaxOpenFiles = GetArg("-dbmaxopenfiles", DEFAULT_DB_MAX_OPEN_FILES);
    bool dbCompression = GetBoolArg("-dbcompression", DEFAULT_DB_COMPRESSION);

    LogPrintf("Block index database configuration:\n");
    LogPrintf("* Using %d max open files\n", dbMaxOpenFiles);
    LogPrintf("* Compression is %s\n", dbCompression ? "enabled" : "disabled");

    // cache size calculations
    int64_t nTotalCache = (GetArg("-dbcache", nDefaultDbCache) << 20);
    nTotalCache = std::max(nTotalCache, nMinDbCache << 20); // total cache cannot be less than nMinDbCache
    nTotalCache = std::min(nTotalCache, nMaxDbCache << 20); // total cache cannot be greated than nMaxDbcache
    int64_t nBlockTreeDBCache = nTotalCache / 8;

    if (GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) || GetBoolArg("-spentindex", DEFAULT_SPENTINDEX)) {
        // enable 3/4 of the cache if addressindex and/or spentindex is enabled
        nBlockTreeDBCache = nTotalCache * 3 / 4;
    } // else {
    //     if (nBlockTreeDBCache > (1 << 21) && !GetBoolArg("-txindex", false)) {
    //         nBlockTreeDBCache = (1 << 21); // block tree db cache shouldn't be larger than 2 MiB
    //     }
    // }
    nTotalCache -= nBlockTreeDBCache;
    int64_t nCoinDBCache = std::min(nTotalCache / 2, (nTotalCache / 4) + (1 << 23)); // use 25%-50% of the remainder for disk cache
    nTotalCache -= nCoinDBCache;
    nCoinCacheUsage = nTotalCache; // the rest goes to in-memory cache
    LogPrintf("Cache configuration:\n");
    LogPrintf("* Max cache setting possible %.1fMiB\n", nMaxDbCache);
    LogPrintf("* Using %.1fMiB for block index database\n", nBlockTreeDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for chain state database\n", nCoinDBCache * (1.0 / 1024 / 1024));
    LogPrintf("* Using %.1fMiB for in-memory UTXO set\n", nCoinCacheUsage * (1.0 / 1024 / 1024));

    if ( !fReindex )
    {
      if (nMaxConnections > 0) //Online mode
      {
            pblocktree = new CBlockTreeDB(nBlockTreeDBCache, false, fReindex, dbCompression, dbMaxOpenFiles);
            bool fAddressIndex = GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
            bool checkval;

            pblocktree->ReadFlag("addressindex", checkval);
            if ( checkval != fAddressIndex && fAddressIndex != 0 )
            {
                pblocktree->WriteFlag("addressindex", fAddressIndex);
                fprintf(stderr,"set addressindex, will reindex. could take a while.\n");
                fReindex = true;
            }
            bool fSpentIndex = GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
            pblocktree->ReadFlag("spentindex", checkval);
            if ( checkval != fSpentIndex && fSpentIndex != 0 )
            {
                pblocktree->WriteFlag("spentindex", fSpentIndex);
                fprintf(stderr,"set spentindex, will reindex. could take a while.\n");
                fReindex = true;
            }
            //One time reindex to enable transaction archiving.
            pblocktree->ReadFlag("archiverule", checkval);
            if (checkval != fArchive)
            {
                pblocktree->WriteFlag("archiverule", fArchive);
                LogPrintf("Transaction archive not set, will reindex. could take a while.\n");
                fReindex = true;
            }
            //One time reindex to enable prooftracking.
            pblocktree->ReadFlag("proofrule", checkval);
            if (checkval != fProof)
            {
                pblocktree->WriteFlag("proofrule", fProof);
                LogPrintf("Transaction proof tracking not set, will reindex. could take a while.\n");
                fReindex = true;
            }
        }
    }

    bool clearWitnessCaches = false;

    bool fLoaded = false;
    if (nMaxConnections==0) {
        //Offline mode: delete unused objects
        try {
            UnloadBlockIndex();
            delete pcoinsTip;
            delete pcoinsdbview;
            delete pcoinscatcher;
            delete pblocktree;
            delete pnotarisations;
        } catch (const std::exception& e) {
            if (fDebug) LogPrintf("%s\n", e.what());
        }
        //Offline mode: skip loading of blockchain blocks
        fLoaded = true;
    }

    // ************
    // Now we're finally able to open the database
    // Results can be:
    // - everything opens fine (AttemptDatabaseOpen == true)
    // - It looks like we are trying to open a database that belongs to another chain (AttemptDatabaseOpen throws)
    // - Some error that is recoverable, perhaps just reindex? If user agrees, reindex and try again
    //
    // AttemptDatabaseOpen is tried until
    // -- returns true
    // -- returns false but user opts out
    // -- throws exception
    // ************

    try
    {
        bool fReset = fReindex;
        std::string strLoadError;
        while(!AttemptDatabaseOpen(nBlockTreeDBCache, dbCompression, dbMaxOpenFiles, nCoinDBCache, strLoadError))
        {
            if (!fReset) // suggest a reindex if we haven't already
            {
                bool fRet = uiInterface.ThreadSafeMessageBox(
                    strLoadError + ".\n\n" + _("error in HDD data, might just need to update to latest, if that doesnt work, then you need to resync"),
                    "", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
                if (fRet)
                {
                    // we should try again, but this time reindex
                    fReindex = true;
                    fRequestShutdown = false;
                } else {
                    LogPrintf("Aborted block database rebuild. Exiting.\n");
                    return false;
                }
            } else {
                return InitError(strLoadError);
            }

        }
    }
    catch(const InvalidGenesisException& ex)
    {
        // We're probably pointing to the wrong data directory
        return InitError(ex.what());
    }
    KOMODO_LOADINGBLOCKS = false;

    // As LoadBlockIndex can take several minutes, it's possible the user
    // requested to kill the GUI during the last operation. If so, exit.
    // As the program has not fully started yet, Shutdown() is possibly overkill.
    if (fRequestShutdown)
    {
        LogPrintf("Shutdown requested. Exiting.\n");
        return false;
    }
    LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

    if (nMaxConnections>0)
    {
        boost::filesystem::path est_path = GetDataDir() / FEE_ESTIMATES_FILENAME;
        CAutoFile est_filein(fopen(est_path.string().c_str(), "rb"), SER_DISK, CLIENT_VERSION);
        // Allowed to fail as this file IS missing on first startup.
        if (!est_filein.IsNull())
            mempool.ReadFeeEstimates(est_filein);
        fFeeEstimatesInitialized = true;
    }
    else
    {
        //Finalise offline mode variable values
        fFeeEstimatesInitialized = false;
        fReindex = 0;
        KOMODO_REWIND = 0;
    }


    // ********************************************************* Step 8: load wallet
#ifdef ENABLE_WALLET
    if (fDisableWallet) {
        pwalletMain = NULL;
        LogPrintf("Wallet disabled!\n");
    } else {

        // needed to restore wallet transaction meta data after -zapwallettxes
        std::vector<CWalletTx> vWtx;

        if (GetBoolArg("-zapwallettxes", false)) {
            uiInterface.InitMessage(_("Zapping all transactions from wallet..."));

            pwalletMain = new CWallet(strWalletFile);
            DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
            if (nZapWalletRet != DB_LOAD_OK) {
                uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
                return false;
            }

            delete pwalletMain;
            pwalletMain = NULL;
        }

        uiInterface.InitMessage(_("Loading wallet..."));

        nStart = GetTimeMillis();
        bool fFirstRun = true;
        pwalletMain = new CWallet(strWalletFile);

        //Check for crypted flag and wait for the wallet password if crypted
        DBErrors nInitalizeCryptedLoad = pwalletMain->InitalizeCryptedLoad();
        if (nInitalizeCryptedLoad == DB_LOAD_CRYPTED) {
            pwalletMain->SetDBCrypted();
            SetRPCNeedsUnlocked(true);
            DBErrors nLoadCryptedSeed = pwalletMain->LoadCryptedSeedFromDB();
            if (nLoadCryptedSeed != DB_LOAD_OK) {
                uiInterface.InitMessage(_("Error loading wallet.dat: Wallet crypted seed corrupted"));
                return false;
            }
            uiInterface.InitNeedUnlockWallet();
        }
        while (pwalletMain->IsLocked()) {
            //wait for response from GUI
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (fRequestShutdown)
            {
                LogPrintf("Shutdown requested. Exiting.\n");
                return false;
            }
        }
        SetRPCNeedsUnlocked(false);

        //A Crypted wallet must have an HDSeed.
        if (pwalletMain->IsCrypted()) {
            // Try to get the seed
            HDSeed seed;
            if (!pwalletMain->GetHDSeed(seed)) {
                LogPrintf("HD seed not found. Exiting.\n");
                return false;
            }

            //Create a uniquie seedFP used to salt encryption hashes, DO NOT SAVE THIS TO THE WALLET!!!!
            //This will be used to salt hashes of know values such as transaction ids and public addresses
            pwalletMain->seedEncyptionFP = seed.EncryptionFingerprint();
        }

        DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
        if (nLoadWalletRet != DB_LOAD_OK)
        {
            if (nLoadWalletRet == DB_CORRUPT)
                strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
            else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
            {
                string msg(_("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                             " or address book entries might be missing or incorrect."));
                InitWarning(msg);
            }
            else if (nLoadWalletRet == DB_TOO_NEW)
                strErrors << _("Error loading wallet.dat: Wallet requires newer version of Pirate") << "\n";
            else if (nLoadWalletRet == DB_NEED_REWRITE)
            {
                strErrors << _("Wallet needed to be rewritten: restart Pirate to complete") << "\n";
                LogPrintf("%s", strErrors.str());
                return InitError(strErrors.str());
            }
            else
                strErrors << _("Error loading wallet.dat") << "\n";
        }

        bool fInitializeArcTx = true;
        if (nLoadWalletRet == DB_LOAD_OK) {
            uiInterface.InitMessage(_("Validating transaction archive..."));
            bool fInitializeArcTx = false;
            {
              LOCK2(cs_main, pwalletMain->cs_wallet);
              if (chainActive.Tip())
                  fInitializeArcTx = pwalletMain->initalizeArcTx();
            }
            if(!fInitializeArcTx && chainActive.Tip()) {
              //ArcTx validation failed, delete wallet point and clear vWtx
              delete pwalletMain;
              pwalletMain = NULL;
              vWtx.clear();

              //Zap All Transactions
              uiInterface.InitMessage(_("Transaction archive not initalized, Zapping all transactions..."));
              LogPrintf("Transaction archive not initalized, Zapping all transactions.\n");
              pwalletMain = new CWallet(strWalletFile);
              DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
              if (nZapWalletRet != DB_LOAD_OK) {
                  uiInterface.InitMessage(_("Error loading wallet.dat: Wallet corrupted"));
                  return false;
              }

              delete pwalletMain;
              pwalletMain = NULL;

              //Reload Wallet
              uiInterface.InitMessage(_("Reloading wallet, set to rescan..."));
              pwalletMain = new CWallet(strWalletFile);

              //Check for crypted flag and wait for the wallet password if crypted
              DBErrors nInitalizeCryptedLoad = pwalletMain->InitalizeCryptedLoad();
              if (nInitalizeCryptedLoad == DB_LOAD_CRYPTED) {
                  pwalletMain->SetDBCrypted();
                  SetRPCNeedsUnlocked(true);
                  DBErrors nLoadCryptedSeed = pwalletMain->LoadCryptedSeedFromDB();
                  if (nLoadCryptedSeed != DB_LOAD_OK) {
                      uiInterface.InitMessage(_("Error loading wallet.dat: Wallet crypted seed corrupted"));
                      return false;
                  }
              }

              //Reopen the wallet
              pwalletMain->OpenWallet(*strOpeningWalletPassphrase);
              delete strOpeningWalletPassphrase;

              SetRPCNeedsUnlocked(false);

              //A Crypted wallet must have an HDSeed.
              if (pwalletMain->IsCrypted()) {
                  // Try to get the seed
                  HDSeed seed;
                  if (!pwalletMain->GetHDSeed(seed)) {
                      LogPrintf("HD seed not found. Exiting.\n");
                      return false;
                  }

                  //Create a uniquie seedFP used to salt encryption hashes, DO NOT SAVE THIS TO THE WALLET!!!!
                  //This will be used to salt hashes of know values such as transaction ids and public addresses
                  pwalletMain->seedEncyptionFP = seed.EncryptionFingerprint();
              }

              DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
              if (nLoadWalletRet != DB_LOAD_OK)
              {
                  if (nLoadWalletRet == DB_CORRUPT)
                      strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
                  else if (nLoadWalletRet == DB_NONCRITICAL_ERROR)
                  {
                      string msg(_("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
                                   " or address book entries might be missing or incorrect."));
                      InitWarning(msg);
                  }
                  else if (nLoadWalletRet == DB_TOO_NEW)
                      strErrors << _("Error loading wallet.dat: Wallet requires newer version of Pirate") << "\n";

                  else if (nLoadWalletRet == DB_NEED_REWRITE)
                  {
                      strErrors << _("Wallet needed to be rewritten: restart Pirate to complete") << "\n";
                      LogPrintf("%s", strErrors.str());
                      return InitError(strErrors.str());
                  }
                  else
                      strErrors << _("Error loading wallet.dat") << "\n";
              }

            } else {
                //Wallet loaded ok and Transaction archive validated true
                DBErrors cleanWallet = pwalletMain->ZapOldRecords();
                if (cleanWallet != DB_LOAD_OK) {
                    LogPrintf("Warning: Wallet cleanup of obsolete records did not complete successfully.");
                }
            }

        } else {
            string msg(_("Warning: error reading wallet.dat! Archive Transaction verification skipped!!!"));
            InitWarning(msg);
        }

        if (GetBoolArg("-upgradewallet", fFirstRun))
        {
            int nMaxVersion = GetArg("-upgradewallet", 0);
            if (nMaxVersion == 0) // the -upgradewallet without argument case
            {
                LogPrintf("Performing wallet upgrade to %i\n", FEATURE_LATEST);
                nMaxVersion = CLIENT_VERSION;
                pwalletMain->SetMinVersion(FEATURE_LATEST); // permanently upgrade the wallet immediately
            }
            else
                LogPrintf("Allowing wallet upgrade up to %i\n", nMaxVersion);
            if (nMaxVersion < pwalletMain->GetVersion())
                strErrors << _("Cannot downgrade wallet") << "\n";
            pwalletMain->SetMaxVersion(nMaxVersion);
        }

        bool recoverWallet = false;
        if (!pwalletMain->HaveHDSeed())
        {

            uiInterface.InitMessage(_(""));
            uiInterface.InitCreateWallet();
            if (usingGUI) {
                while (pwalletMain->createType == UNSET) {
                  //wait for response from GUI
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));

                    if (fRequestShutdown)
                    {
                        LogPrintf("Shutdown requested. Exiting.\n");
                        return false;
                    }
                }
            } else {
                recoverySeedPhrase  = GetArg("-seedphrase", "");
                if (recoverySeedPhrase != "") {
                    pwalletMain->createType = RECOVERY;
                } else {
                    pwalletMain->createType = RANDOM;
                }
            }


            if (pwalletMain->createType == RECOVERY) {
                if (!pwalletMain->RestoreSeedFromPhrase(recoverySeedPhrase)) {
                    LogPrintf("Invalid Seed Phrase - shutting down.\n");
                    return false;
                }
                recoverWallet = true;
            } else {
              // generate a new HD seed
                pwalletMain->GenerateNewSeed();
                pwalletMain->GetSeedPhrase(recoverySeedPhrase);
                if (usingGUI) {
                    uiInterface.InitShowPhrase();
                    while (pwalletMain->createType == RANDOM) {
                      //wait for response from GUI
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        if (fRequestShutdown)
                        {
                            LogPrintf("Shutdown requested. Exiting.\n");
                            return false;
                        }
                    }
                }
            }

            //Write Wallet birthday
            {
                pwalletMain->nBirthday = 0;
                CWalletDB walletdb(strWalletFile);
                walletdb.WriteWalletBirthday(pwalletMain->nBirthday);
            }

            //Write bip39Enabled
            {
                pwalletMain->bip39Enabled = true;
                CWalletDB walletdb(strWalletFile);
                walletdb.WriteWalletBip39Enabled(pwalletMain->bip39Enabled);
            }

            // generate 1 address
            LOCK(pwalletMain->cs_wallet);
            auto zAddress = pwalletMain->GenerateNewSaplingZKey();
            pwalletMain->SetZAddressBook(zAddress, "z-sapling", "");
        }

        //Set Minimum value of incoming notes accepted
        minTxValue = GetArg("-mintxvalue", DEFAULT_MIN_TX_VALUE);

        //Set Sapling Consolidation
        pwalletMain->fSaplingConsolidationEnabled = GetBoolArg("-consolidation", false);
        fConsolidationTxFee  = GetArg("-consolidationtxfee", DEFAULT_CONSOLIDATION_FEE);
        fConsolidationMapUsed = !mapMultiArgs["-consolidatesaplingaddress"].empty();

        //Validate Sapling Addresses
        vector<string>& vaddresses = mapMultiArgs["-consolidatesaplingaddress"];
        for (int i = 0; i < vaddresses.size(); i++) {
            LogPrintf("Consolidating Sapling Address: %s\n", vaddresses[i]);
            auto zAddress = DecodePaymentAddress(vaddresses[i]);
            if (!IsValidPaymentAddress(zAddress)) {
                return InitError("Invalid consolidation address");
            }
        }

        fCleanUpMode = GetBoolArg("-cleanup", false);
        if (fCleanUpMode) {
            pwalletMain->strCleanUpStatus = "Creating cleanup transactions.";
            if (!pwalletMain->fSaplingConsolidationEnabled) {
                return InitError("Consolidation must be enable to enable cleanup mode.");
            }
        }

        //Set Sapling Sweep
        pwalletMain->fSaplingSweepEnabled = GetBoolArg("-sweep", false);

        if (pwalletMain->fSaplingSweepEnabled) {
            fSweepTxFee  = GetArg("-sweeptxfee", DEFAULT_SWEEP_FEE);
            fSweepMapUsed = !mapMultiArgs["-sweepsaplingaddress"].empty();

            //Validate Sapling Addresses
            vector<string>& vSweep = mapMultiArgs["-sweepsaplingaddress"];
            if (vSweep.size() != 1) {
                return InitError("A single sweep address must be specified.");
            }

            for (int i = 0; i < vSweep.size(); i++) {
                LogPrintf("Sweep Sapling Address: %s\n", vSweep[i]);
                auto zSweep = DecodePaymentAddress(vSweep[i]);
                if (!IsValidPaymentAddress(zSweep)) {
                    return InitError("Invalid sweep address");
                }
                auto hasSpendingKey = boost::apply_visitor(HaveSpendingKeyForPaymentAddress(pwalletMain), zSweep);
                if (!hasSpendingKey) {
                    return InitError("Wallet must have the spending key of sweep address");
                }
            }

            if (pwalletMain->fSaplingConsolidationEnabled) {
                //Validate 1 Consolidation address only that matches the sweep address
                vector<string>& vaddresses = mapMultiArgs["-consolidatesaplingaddress"];
                if (vaddresses.size() == 0) {
                    fConsolidationMapUsed = true;
                    mapMultiArgs["-consolidatesaplingaddress"] = vSweep;
                } else {
                    for (int i = 0; i < vaddresses.size(); i++) {
                        if (vSweep[0] != vaddresses[i]) {
                            return InitError("Consolidation can only be used on the sweep address when sweep is enabled.");
                        }
                    }
                }
            }
        }

        //Set Transaction Deletion Options
        fTxDeleteEnabled = GetBoolArg("-deletetx", false);
        fTxConflictDeleteEnabled = GetBoolArg("-deleteconflicttx", true);

        fDeleteInterval = GetArg("-deleteinterval", DEFAULT_TX_DELETE_INTERVAL);
        if (fDeleteInterval < 1)
          return InitError("deleteinterval must be greater than 0");

        fKeepLastNTransactions = GetArg("-keeptxnum", DEFAULT_TX_RETENTION_LASTTX);
        if (fKeepLastNTransactions < 1)
          return InitError("keeptxnum must be greater than 0");

        fDeleteTransactionsAfterNBlocks = GetArg("-keeptxfornblocks", DEFAULT_TX_RETENTION_BLOCKS);
        if (fDeleteTransactionsAfterNBlocks < 1)
          return InitError("keeptxfornblocks must be greater than 0");

        if (fDeleteTransactionsAfterNBlocks < MAX_REORG_LENGTH + 1 ) {
          LogPrintf("keeptxfornblock is less the MAX_REORG_LENGTH, Setting to %i\n", MAX_REORG_LENGTH + 1);
          fDeleteTransactionsAfterNBlocks = MAX_REORG_LENGTH + 1;
        }

        if (fFirstRun)
        {
            useBootstrap = false;
            // Create new keyUser and set as default key
            CPubKey newDefaultKey;
            if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
                pwalletMain->SetDefaultKey(newDefaultKey);
                if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive"))
                    strErrors << _("Cannot write default address") << "\n";
            }

            if (chainActive.Tip()) {
                LOCK(pwalletMain->cs_wallet);
                pwalletMain->SetBestChain(chainActive.GetLocator(), chainActive.Tip()->nHeight);
            }
        }

        LogPrintf("%s", strErrors.str());
        LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

        RegisterValidationInterface(pwalletMain);

        LOCK(cs_main);
        CBlockIndex *pindexRescan = chainActive.Tip();

        //Load Wallet birthday
        {
            CWalletDB walletdb(strWalletFile);
            walletdb.ReadWalletBirthday(pwalletMain->nBirthday);
        }

        //Load bip39Enabled
        {
            CWalletDB walletdb(strWalletFile);
            walletdb.ReadWalletBip39Enabled(pwalletMain->bip39Enabled);
        }

        {
            //Sort Transactions by block and block index, then reorder
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (chainActive.Tip()) {
                LogPrintf("Runnning transaction reorder\n");
                int64_t maxOrderPos = 0;
                std::map<std::pair<int,int>, CWalletTx*> mapSorted;
                pwalletMain->ReorderWalletTransactions(mapSorted, maxOrderPos);
                pwalletMain->UpdateWalletTransactionOrder(mapSorted, true);
            }
        }

        //Scan the last 100 block to ensure proofs are being maintained.
        {
            if (!fReindex) {
                CBlockIndex *pindexProofScan = chainActive.Tip();
                if (pindexProofScan->nHeight > 100) {
                    pindexProofScan = chainActive[pindexProofScan->nHeight-100];
                } else {
                    pindexProofScan = chainActive.Genesis();
                }

                while (pindexProofScan)
                {
                    CBlock proofBlock;
                    ReadBlockFromDisk(proofBlock, pindexProofScan,1);

                    BOOST_FOREACH(CTransaction& tx, proofBlock.vtx)
                    {
                        for (const SpendDescription &spendDescription : tx.vShieldedSpend) {
                            std::set<std::pair<uint256, int>> txids;
                            bool foundProof = pcoinsTip->GetZkProofHash(spendDescription.ProofHash(), SPEND, txids);
                            if (!foundProof) {
                                LogPrintf("Proof not found for tx %s spend ProofHash %s. Restart Treasure Chest to reindex.\n", tx.GetHash().ToString(), spendDescription.ProofHash().ToString());
                                pblocktree->WriteFlag("proofrule", false);
                                return false;
                            }
                        }

                        for (const OutputDescription &outputDescription : tx.vShieldedOutput) {
                            std::set<std::pair<uint256, int>> txids;
                            bool foundProof = pcoinsTip->GetZkProofHash(outputDescription.ProofHash(), OUTPUT, txids);
                            if (!foundProof) {
                                LogPrintf("Proof not found for tx %s output ProofHash %s. Restart Treasure Chest needs to reindex.\n", tx.GetHash().ToString(), outputDescription.ProofHash().ToString());
                                pblocktree->WriteFlag("proofrule", false);
                                return false;
                            }
                        }
                    }
                    pindexProofScan = chainActive.Next(pindexProofScan);
                }
            }
        }

        if (clearWitnessCaches || GetBoolArg("-rescan", false) || !fInitializeArcTx || useBootstrap)
        {
            pwalletMain->ClearNoteWitnessCache();
            pindexRescan = chainActive.Genesis();
            pwalletMain->nBirthday = 0;

            int rescanHeight = GetArg("-rescanheight", 0);
            if (chainActive.Tip() && rescanHeight > 0) {
                if (rescanHeight > chainActive.Tip()->nHeight) {
                    pindexRescan = chainActive.Tip();
                } else {
                    pindexRescan = chainActive[rescanHeight];
                }
            }
        }
        else
        {
            CWalletDB walletdb(strWalletFile);
            CBlockLocator locator;
            if (walletdb.ReadBestBlock(locator))
                pindexRescan = FindForkInGlobalIndex(chainActive, locator);
            else
                pindexRescan = chainActive.Genesis();
        }

        //Always scan from Genesis on wallet Recovery
        if (recoverWallet) {
            pindexRescan = chainActive.Genesis();
        }

        if (chainActive.Tip() && chainActive.Tip() != pindexRescan)
        {
            uiInterface.InitMessage(_("Rescanning..."));
            LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);
            nStart = GetTimeMillis();
            pwalletMain->ScanForWalletTransactions(pindexRescan, true);
            LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);

            // Restore wallet transaction metadata after -zapwallettxes=1
            if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2" && fInitializeArcTx)
            {
                CWalletDB walletdb(strWalletFile);

                BOOST_FOREACH(const CWalletTx& wtxOld, vWtx)
                {
                    uint256 hash = wtxOld.GetHash();
                    std::map<uint256, CWalletTx>::iterator mi = pwalletMain->mapWallet.find(hash);
                    if (mi != pwalletMain->mapWallet.end())
                    {
                        ArchiveTxPoint arcTxPt;
                        const CWalletTx* copyFrom = &wtxOld;
                        CWalletTx* copyTo = &mi->second;
                        copyTo->mapValue = copyFrom->mapValue;
                        copyTo->vOrderForm = copyFrom->vOrderForm;
                        copyTo->nTimeReceived = copyFrom->nTimeReceived;
                        copyTo->nTimeSmart = copyFrom->nTimeSmart;
                        copyTo->fFromMe = copyFrom->fFromMe;
                        copyTo->strFromAccount = copyFrom->strFromAccount;
                        copyTo->nOrderPos = copyFrom->nOrderPos;
                    }
                }
            }
        } else {
            //Rescan at minimum last 1 block
            if (chainActive.Tip() && chainActive.Height() > 0) {
                pindexRescan = chainActive[chainActive.Tip()->nHeight - 1];
                uiInterface.InitMessage(_("Rescanning..."));
                LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight, pindexRescan->nHeight);
                nStart = GetTimeMillis();
                pwalletMain->ScanForWalletTransactions(pindexRescan, true);
                LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
            }
        }

        pwalletMain->SetBroadcastTransactions(GetBoolArg("-walletbroadcast", true));

        vpwallets.push_back(pwalletMain);
    } // (!fDisableWallet)
#else // ENABLE_WALLET
    LogPrintf("No wallet support compiled in!\n");
#endif // !ENABLE_WALLET

    if (GetBoolArg("-largetxthrottle", true)) {
        LogPrintf("Blocktemplate large tx throttle enabled.\n");
    } else {
        LogPrintf("Blocktemplate large tx throttle disabled.\n");
    }

#ifdef ENABLE_MINING
 #ifndef ENABLE_WALLET
    if (GetBoolArg("-minetolocalwallet", false)) {
        return InitError(_("Zcash was not built with wallet support. Set -minetolocalwallet=0 to use -mineraddress, or rebuild Zcash with wallet support."));
    }
    if (GetArg("-mineraddress", "").empty() && GetBoolArg("-gen", false)) {
        return InitError(_("Zcash was not built with wallet support. Set -mineraddress, or rebuild Zcash with wallet support."));
    }
 #endif // !ENABLE_WALLET

    if (mapArgs.count("-mineraddress")) {
 #ifdef ENABLE_WALLET
        bool minerAddressInLocalWallet = false;
        if (pwalletMain) {
            // Address has alreday been validated
            CTxDestination addr = DecodeDestination(mapArgs["-mineraddress"]);
            CKeyID keyID = boost::get<CKeyID>(addr);
            minerAddressInLocalWallet = pwalletMain->HaveKey(keyID);
        }
        if (GetBoolArg("-minetolocalwallet", true) && !minerAddressInLocalWallet) {
            return InitError(_("-mineraddress is not in the local wallet. Either use a local address, or set -minetolocalwallet=0"));
        }
 #endif // ENABLE_WALLET
    }
#endif // ENABLE_MINING

    // ********************************************************* Step 9: data directory maintenance

    // if pruning, unset the service bit and perform the initial blockstore prune
    // after any wallet rescanning has taken place.
    if (fPruneMode) {
        LogPrintf("Unsetting NODE_NETWORK on prune mode\n");
        nLocalServices &= ~NODE_NETWORK;
        if (!fReindex) {
            uiInterface.InitMessage(_("Pruning blockstore..."));
            PruneAndFlush();
        }
    }
    if ( KOMODO_NSPV == 0 )
    {
        if ( GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX) != 0 )
            nLocalServices |= NODE_ADDRINDEX;
        if ( GetBoolArg("-spentindex", DEFAULT_SPENTINDEX) != 0 )
            nLocalServices |= NODE_SPENTINDEX;
        fprintf(stderr,"nLocalServices %llx %d, %d\n",(long long)nLocalServices,GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX),GetBoolArg("-spentindex", DEFAULT_SPENTINDEX));
    }
    // ********************************************************* Step 10: import blocks

    if (mapArgs.count("-blocknotify"))
        uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);
    if ( KOMODO_REWIND >= 0 )
    {
        uiInterface.InitMessage(_("Activating best chain..."));
        // scan for better chains in the block chain database, that are not yet connected in the active best chain
        CValidationState state;
        if ( !ActivateBestChain(true,state))
            strErrors << "Failed to connect best block";
    }
    std::vector<boost::filesystem::path> vImportFiles;
    if (mapArgs.count("-loadblock"))
    {
        BOOST_FOREACH(const std::string& strFile, mapMultiArgs["-loadblock"])
            vImportFiles.push_back(strFile);
    }
    threadGroup.create_thread(boost::bind(&ThreadImport, vImportFiles));
    {
        CBlockIndex *tip = nullptr;
        {
            LOCK(cs_main);
            tip = chainActive.Tip();
        }
        if (tip == nullptr) {
            LogPrintf("Waiting for genesis block to be imported...\n");
            while (!ShutdownRequested() && tip == nullptr)
            {
                MilliSleep(10);
                LOCK(cs_main);
                tip = chainActive.Tip();
            }
            if (ShutdownRequested()) return false;
        }
    }

    // ********************************************************* Step 11: start node

    if (!CheckDiskSpace())
        return false;

    if (!strErrors.str().empty())
        return InitError(strErrors.str());

    //// debug print
    LogPrintf("mapBlockIndex.size() = %u\n",   mapBlockIndex.size());
    {
        LOCK(cs_main);
        LogPrintf("nBestHeight = %d\n",                   chainActive.Height());
    }
#ifdef ENABLE_WALLET
    RescanWallets();

    LogPrintf("setKeyPool.size() = %u\n",      pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    LogPrintf("mapWallet.size() = %u\n",       pwalletMain ? pwalletMain->mapWallet.size() : 0);
    LogPrintf("mapAddressBook.size() = %u\n",  pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
#endif

    // Start the thread that notifies listeners of transactions that have been
    // recently added to the mempool.
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "txnotify", &ThreadNotifyRecentlyAdded));

    // Start the thread that updates komodo internal structures
    threadGroup.create_thread(&ThreadUpdateKomodoInternals);

    if (GetBoolArg("-listenonion", DEFAULT_LISTEN_ONION))
        StartTorControl(threadGroup, scheduler);

    StartNode(threadGroup, scheduler);

#ifdef ENABLE_MINING
    // Generate coins in the background
 #ifdef ENABLE_WALLET
    if (pwalletMain || !GetArg("-mineraddress", "").empty())
        GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", -1));
 #else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", -1));
 #endif
#endif

    // ********************************************************* Step 11: finished

    SetRPCWarmupFinished();
    uiInterface.InitMessage(_("Done loading"));

#ifdef ENABLE_WALLET
    if (pwalletMain) {
        // Add wallet transactions that aren't already in a block to mapTransactions
        pwalletMain->ReacceptWalletTransactions();

        //Lock the wallet if crypted
        if (pwalletMain->IsCrypted()) {
            pwalletMain->Lock();
        }

        // Run a thread to flush wallet periodically
        threadGroup.create_thread(boost::bind(&ThreadFlushWalletDB, boost::ref(pwalletMain->strWalletFile)));
    }
#endif

    // SENDALERT
    threadGroup.create_thread(boost::bind(ThreadSendAlert));

    //Save the load status to check on Shutdown
    loadComplete = true;

    return !ShutdownRequested();
}
