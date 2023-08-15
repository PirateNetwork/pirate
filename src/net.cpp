// Copyright (c) 2009-2010 Satoshi Nakamoto
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

#include "main.h"
#include "net.h"
#include "init.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "primitives/transaction.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "crypto/common.h"
#include "tls/utiltls.h"
#include "komodo_defs.h"
#include "komodo_globals.h"
#include "notaries_staked.h"

#ifdef _WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#include <boost/filesystem.hpp>
#include <boost/thread.hpp>

#include <openssl/conf.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <tls/tlsmanager.cpp>
using namespace tls;

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef _WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

#define USE_TLS

#if defined(USE_TLS) && !defined(TLS1_3_VERSION)
    // minimum secure protocol is 1.3
    // TLS1_3_VERSION is defined in openssl/tls1.h
    #error "ERROR: Your OpenSSL version does not support TLS v1.3"
#endif

using namespace std;

namespace {
    const int MAX_OUTBOUND_CONNECTIONS = 16;
    const int MAX_INBOUND_FROMIP = 5;

    struct ListenSocket {
        SOCKET socket;
        bool whitelisted;

        ListenSocket(SOCKET _socket, bool _whitelisted) : socket(_socket), whitelisted(_whitelisted) {}
    };
}

bool fDiscover = true;
bool fListen = true;
uint64_t nLocalServices = NODE_NETWORK;
CCriticalSection cs_mapLocalHost;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
static CNode* pnodeLocalHost = NULL;
uint64_t nLocalHostNonce = 0;
static std::vector<ListenSocket> vhListenSocket;
CAddrMan addrman;
int nMaxConnections = DEFAULT_MAX_PEER_CONNECTIONS;
bool bOverrideMaxConnections=false;
bool fAddressesInitialized = false;
TLSManager tlsmanager = TLSManager();
std::atomic<bool> fNetworkActive = { true };
bool setBannedIsDirty = false;
bool GetNetworkActive() { return fNetworkActive; };

std::string strSubVersion;

/**
 * I2P SAM session.
 * Used to accept incoming and make outgoing I2P connections.
 */
std::unique_ptr<i2p::sam::Session> m_i2p_sam_session;

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

static deque<string> vOneShots;
static CCriticalSection cs_vOneShots;

static set<CNetAddr> setservAddNodeAddresses;
static CCriticalSection cs_setservAddNodeAddresses;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;

static CSemaphore *semOutbound = NULL;
static boost::condition_variable messageHandlerCondition;

// Denial-of-service detection/prevention
// Key is IP address, value is banned-until-time
banmap_t setBanned;
CCriticalSection cs_setBanned;

// Signals for message handling
static CNodeSignals g_signals;
CNodeSignals& GetNodeSignals() { return g_signals; }

// OpenSSL server and client contexts
SSL_CTX *tls_ctx_server, *tls_ctx_client;

static bool operator==(_NODE_ADDR a, _NODE_ADDR b)
{
    return (a.ipAddr == b.ipAddr);
}

static std::vector<NODE_ADDR> vNonTLSNodesInbound;
static CCriticalSection cs_vNonTLSNodesInbound;

static std::vector<NODE_ADDR> vNonTLSNodesOutbound;
static CCriticalSection cs_vNonTLSNodesOutbound;

void AddOneShot(const std::string& strDest)
{
    LOCK(cs_vOneShots);
    vOneShots.push_back(strDest);
}

unsigned short GetListenPort()
{
    //printf("Listenport.%u\n",Params().GetDefaultPort());
    return (unsigned short)(GetArg("-port", Params().GetDefaultPort()));
}

// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr *paddrPeer)
{
    if (!fListen)
        return false;

    int nBestScore = -1;
    int nBestReachability = -1;
    {
        LOCK(cs_mapLocalHost);
        for (map<CNetAddr, LocalServiceInfo>::iterator it = mapLocalHost.begin(); it != mapLocalHost.end(); it++)
        {
            int nScore = (*it).second.nScore;
            int nReachability = (*it).first.GetReachabilityFrom(paddrPeer);
            if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore))
            {
                addr = CService((*it).first, (*it).second.nPort);
                nBestReachability = nReachability;
                nBestScore = nScore;
            }
        }
    }
    return nBestScore >= 0;
}

//! Convert the serialized seeds into usable address objects.
static std::vector<CAddress> ConvertSeeds(const std::vector<uint8_t> &vSeedsIn)
{
    // It'll only connect to one or two seed nodes because once it connects,
    // it'll get a pile of addresses with newer timestamps.
    // Seed nodes are given a random 'last seen time' of between one and two
    // weeks ago.
    const int64_t nOneWeek = 7*24*60*60;
    std::vector<CAddress> vSeedsOut;
    CDataStream s(vSeedsIn, SER_NETWORK, PROTOCOL_VERSION | ADDRV2_FORMAT);
    while (!s.eof()) {
        CService endpoint;
        s >> endpoint;
        CAddress addr{endpoint, NODE_NETWORK};
        addr.nTime = GetTime() - nOneWeek;
        LogPrint("net", "Added hardcoded seed: %s\n", addr.ToString());
        vSeedsOut.push_back(addr);
    }
    return vSeedsOut;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService(CNetAddr(),GetListenPort()),0);
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nServices = nLocalServices;
    ret.nTime = GetTime();
    return ret;
}

int GetnScore(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == LOCAL_NONE)
        return 0;
    return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode *pnode)
{
    return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
           IsReachable(pnode->addrLocal.GetNetwork());
}

// pushes our own address to a peer
void AdvertizeLocal(CNode *pnode)
{
    if (fListen && pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
             GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8:2) == 0))
        {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable())
        {
            LogPrintf("AdvertizeLocal: advertizing address %s\n", addrLocal.ToString());
            pnode->PushAddress(addrLocal);
        }
    }
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore)
{
    if (!addr.IsRoutable())
        return false;

    if (!fDiscover && nScore < LOCAL_MANUAL)
        return false;

    if (!IsReachable(addr))
        return false;

    //Add our local addresses to addrman to distribute to other nodes
    addrman.Add(CAddress(addr), addr);
    addrman.Connected(addr);
    addrman.SetLocal(addr);

    LogPrintf("AddLocal(%s,%i)\n", addr.ToString(), nScore);

    {
        LOCK(cs_mapLocalHost);
        bool fAlready = mapLocalHost.count(addr) > 0;
        LocalServiceInfo &info = mapLocalHost[addr];
        if (!fAlready || nScore >= info.nScore) {
            info.nScore = nScore + (fAlready ? 1 : 0);
            info.nPort = addr.GetPort();
        }
    }

    return true;
}

bool AddLocal(const CNetAddr &addr, int nScore)
{
    return AddLocal(CService(addr, GetListenPort()), nScore);
}

bool RemoveLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    LogPrintf("RemoveLocal(%s)\n", addr.ToString());
    mapLocalHost.erase(addr);
    return true;
}

/** vote for a local address */
bool SeenLocal(const CService& addr)
{
    {
        LOCK(cs_mapLocalHost);
        if (mapLocalHost.count(addr) == 0)
            return false;
        mapLocalHost[addr].nScore++;
    }
    return true;
}


/** check whether a given address is potentially local */
bool IsLocal(const CService& addr)
{
    LOCK(cs_mapLocalHost);
    return mapLocalHost.count(addr) > 0;
}

void SetReachable(enum Network net, bool reachable)
{
    if (net == NET_UNROUTABLE || net == NET_INTERNAL)
        return;
    LOCK(cs_mapLocalHost);
    vfLimited[net] = !reachable;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net)
{
    LOCK(cs_mapLocalHost);
    return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr)
{
    enum Network net = addr.GetNetwork();
    return IsReachable(net);
}

void AddressCurrentlyConnected(const CService& addr)
{
    addrman.Add(CAddress(addr),addr); //Add address if not alread in addrman (picks up addnodes)
    addrman.Connected(addr);
}

CNode::eTlsOption CNode::tlsFallbackNonTls = CNode::eTlsOption::FALLBACK_UNSET;
CNode::eTlsOption CNode::tlsValidate       = CNode::eTlsOption::FALLBACK_UNSET;

uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;

CNode* FindNode(const CNetAddr& ip)
{
    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes) {
      if (static_cast<CNetAddr>(pnode->addr) == ip) {
            return pnode;
        }
    }
    return nullptr;
}

CNode* FindNode(const CSubNet& subNet)
{
    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes) {
        if (subNet.Match(static_cast<CNetAddr>(pnode->addr))) {
            return pnode;
        }
    }
    return nullptr;
}

CNode* FindNode(const std::string& addrName)
{
    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes) {
        if (pnode->addrName == addrName) {
            return pnode;
        }
    }
    return nullptr;
}

CNode* FindNode(const CService& addr)
{
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if ((CService)pnode->addr == addr)
            return (pnode);
    return NULL;
}

static CAddress GetBindAddress(SOCKET sock)
{
    CAddress addr_bind;
    struct sockaddr_storage sockaddr_bind;
    socklen_t sockaddr_bind_len = sizeof(sockaddr_bind);
    if (sock != INVALID_SOCKET) {
        if (!getsockname(sock, (struct sockaddr*)&sockaddr_bind, &sockaddr_bind_len)) {
            addr_bind.SetSockAddr((const struct sockaddr*)&sockaddr_bind);
        } else {
            LogPrintf("Warning: getsockname failed\n");
        }
    }
    return addr_bind;
}

CNode* ConnectNode(CAddress addrConnect, const char *pszDest)
{
    if (pszDest == NULL) {
        if (IsLocal(addrConnect))
            return NULL;

        // Look for an existing connection
        CNode* pnode = FindNode(static_cast<CService>(addrConnect));
        if (pnode)
        {
            pnode->AddRef();
            return pnode;
        }
    }

    /// debug print
    LogPrint("net", "trying connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetTime() - addrConnect.nTime)/3600.0);

    // Connect
    SOCKET hSocket;
    bool proxyConnectionFailed = false;
    bool connected = false;
    std::unique_ptr<Sock> sock;

    if (!addrConnect.IsValid()) {
        return NULL;
    }

    if (!IsReachable(addrConnect)) {
        return NULL;
    }

    if (addrConnect.GetNetwork() == NET_I2P && m_i2p_sam_session.get() != nullptr) {
            i2p::Connection conn;
            if (m_i2p_sam_session->Connect(addrConnect, conn, proxyConnectionFailed)) {
                connected = true;
                sock = std::move(conn.sock);
                hSocket = sock->Release();
                // addr_bind = CAddress{conn.me, NODE_NONE};
            }
    } else if (pszDest) {
        connected = ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout, &proxyConnectionFailed);
    }  else {
        connected = ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed);
    }


    if (connected)
    {
        if (!IsSelectableSocket(hSocket)) {
            LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
            CloseSocket(hSocket);
            return NULL;
        }

        addrman.Attempt(addrConnect);

        SSL *ssl = NULL;

#ifdef USE_TLS

        {
            LOCK(cs_vNonTLSNodesOutbound);

            NODE_ADDR nodeAddr(addrConnect.ToStringIP());
            bool bUseTLS = (find(vNonTLSNodesOutbound.begin(),vNonTLSNodesOutbound.end(),nodeAddr) == vNonTLSNodesOutbound.end());
            bool bTlsEnforcement = (GetBoolArg("-tlsenforcement", true) || GetArg("-tlsenforcement", "") == "1");
            unsigned long err_code = 0;

            //Check for plaintext peer
            const std::vector<std::string>& vAllow = mapMultiArgs["-plaintextpeer"];
            bool plaintextpeer = (std::find(vAllow.begin(), vAllow.end(), addrConnect.ToStringIP()) != vAllow.end());

            /* TCP connection is ready. Do client side SSL. */
            if (CNode::GetTlsFallbackNonTls()) {
                LogPrint("tls", "%s():%d - handling connection to %s\n", __func__, __LINE__,  addrConnect.ToStringIP());
                if (plaintextpeer) {
                    LogPrint("tls", "%s():%d - handling plaintext peer connection to %s\n", __func__, __LINE__,  addrConnect.ToStringIP());
                    // Further reconnection will be made in non-TLS (unencrypted) mode
                    if (!bUseTLS)  {// Already in the list
                        vNonTLSNodesOutbound.push_back(NODE_ADDR(addrConnect.ToStringIP(), GetTimeMillis()));
                        LogPrintf("%s():%d - err_code %x, adding plaintextpeer connection to %s vNonTLSNodesOutbound list (sz=%d)\n",__func__, __LINE__, err_code, addrConnect.ToStringIP(), vNonTLSNodesOutbound.size());
                    }
                } else {
                    if (bUseTLS) {
                        ssl = tlsmanager.connect(hSocket, addrConnect, err_code);
                        if (!ssl) {
                            if (err_code == TLSManager::SELECT_TIMEDOUT) {
                                // can fail for timeout in select on fd, that is not a ssl error and we should not
                                // consider this node as non TLS
                                LogPrint("tls", "%s():%d - Connection to %s timedout\n",__func__, __LINE__, addrConnect.ToStringIP());
                            } else {
                                //Do not allow unencrypted connection if TLS Enforcement is set
                                if (!bTlsEnforcement) {
                                    // Further reconnection will be made in non-TLS (unencrypted) mode
                                    vNonTLSNodesOutbound.push_back(NODE_ADDR(addrConnect.ToStringIP(), GetTimeMillis()));
                                    LogPrint("tls", "%s():%d - err_code %x, adding connection to %s vNonTLSNodesOutbound list (sz=%d)\n",__func__, __LINE__, err_code, addrConnect.ToStringIP(), vNonTLSNodesOutbound.size());
                                }
                            }
                            CloseSocket(hSocket);
                            return NULL;
                        }
                    }
                }
            } else {
                if (plaintextpeer) {
                    // Further reconnection will be made in non-TLS (unencrypted) mode
                    if (!bUseTLS)  {// Already in the list
                        vNonTLSNodesOutbound.push_back(NODE_ADDR(addrConnect.ToStringIP(), GetTimeMillis()));
                            LogPrintf("%s():%d - err_code %x, adding plaintextpeer connection to %s vNonTLSNodesOutbound list (sz=%d)\n",__func__, __LINE__, err_code, addrConnect.ToStringIP(), vNonTLSNodesOutbound.size());
                    }
                } else {
                    unsigned long err_code = 0;
                    ssl = tlsmanager.connect(hSocket, addrConnect, err_code);
                    if(!ssl) {
                        LogPrint("tls", "%s():%d - err_code %x, connection to %s failed)\n",__func__, __LINE__, err_code, addrConnect.ToStringIP());
                        CloseSocket(hSocket);
                        return NULL;
                    }
                }
            }
        }
        // certificate validation is disabled by default
        if (CNode::GetTlsValidate())
        {
            if (ssl && !ValidatePeerCertificate(ssl))
            {
                LogPrintf ("TLS: ERROR: Wrong server certificate from %s. Connection will be closed.\n", addrConnect.ToString());

                SSL_shutdown(ssl);
                CloseSocket(hSocket);
                SSL_free(ssl);
                return NULL;
            }
        }
#endif  // USE_TLS

        // Add node
        CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false, ssl);
        pnode->AddRef();

        {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
        }

        pnode->nTimeConnected = GetTime();

        return pnode;
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        addrman.Attempt(addrConnect);
    }

    return NULL;
}

void CNode::CloseSocketDisconnect(bool sendShutDownSSL)
{
    fDisconnect = true;

    {
        LOCK(cs_hSocket);

        if (hSocket != INVALID_SOCKET)
        {
            try
            {
                if (ssl)
                {
                    unsigned long err_code = 0;
                    if (sendShutDownSSL)
                    {
                        tlsmanager.waitFor(SSL_SHUTDOWN, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000), err_code);
                    }
                    SSL_free(ssl);
                    ssl = NULL;
                }
                CloseSocket(hSocket);
                LogPrint("net", "disconnecting peer=%d\n", id);
            }
            catch(std::bad_alloc&)
            {
                // when the node is shutting down, the call above might use invalid memory resulting in a
                // std::bad_alloc exception when instantiating internal objs for handling log category
                LogPrintf("(node is probably shutting down) disconnecting peer=%d\n", id);
            }
        }
    }

    // in case this fails, we'll empty the recv buffer when the CNode is deleted
    TRY_LOCK(cs_vRecvMsg, lockRecv);
    if (lockRecv)
        vRecvMsg.clear();
}

/* TODO remove
#ifndef KOMODO_NSPV_FULLNODE
#define KOMODO_NSPV_FULLNODE (KOMODO_NSPV <= 0)
#endif // !KOMODO_NSPV_FULLNODE

#ifndef KOMODO_NSPV_SUPERLITE
#define KOMODO_NSPV_SUPERLITE (KOMODO_NSPV > 0)
#endif // !KOMODO_NSPV_SUPERLITE
*/

void CNode::PushVersion()
{
    int nBestHeight = g_signals.GetHeight().get_value_or(0);

    int64_t nTime = (fInbound ? GetTime() : GetTime());
    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService(), addr.nServices));
    CAddress addrMe = GetLocalAddress(&addr);
    GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
    if (fLogIPs)
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
    else
        LogPrint("net", "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), id);
    PushMessage(NetMsgType::VERSION, PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
                nLocalHostNonce, strSubVersion, nBestHeight, true);
}





// std::map<CSubNet, int64_t> CNode::setBanned;
// CCriticalSection CNode::cs_setBanned;

void DumpBanlist()
{
    SweepBanned(); // clean unused entries (if bantime has expired)

    if (!BannedSetIsDirty())
        return;

    int64_t nStart = GetTimeMillis();

    CBanDB bandb;
    banmap_t banmap;
    GetBanned(banmap);
    if (bandb.Write(banmap)) {
        SetBannedSetDirty(false);
    }

    LogPrint("net", "Flushed %d banned node ips/subnets to banlist.dat  %dms\n",
        banmap.size(), GetTimeMillis() - nStart);
}

void CNode::ClearBanned()
{
    {
        LOCK(cs_setBanned);
        setBanned.clear();
        setBannedIsDirty = true;
    }
    DumpBanlist(); //store banlist to disk
    uiInterface.BannedListChanged();
}

bool CNode::IsBanned(CNetAddr ip)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        for (banmap_t::iterator it = setBanned.begin(); it != setBanned.end(); it++)
        {
            CSubNet subNet = (*it).first;
            CBanEntry banEntry = (*it).second;

            if(subNet.Match(ip) && GetTime() < banEntry.nBanUntil)
                fResult = true;
        }
    }
    return fResult;
}

bool CNode::IsBanned(CSubNet subnet)
{
    bool fResult = false;
    {
        LOCK(cs_setBanned);
        banmap_t::iterator i = setBanned.find(subnet);
        if (i != setBanned.end())
        {
            CBanEntry banEntry = (*i).second;
            if (GetTime() < banEntry.nBanUntil)
                fResult = true;
        }
    }
    return fResult;
}

void CNode::Ban(const CNetAddr& addr, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch) {
    CSubNet subNet(addr);
    Ban(subNet, banReason, bantimeoffset, sinceUnixEpoch);
}

void CNode::Ban(const CSubNet& subNet, const BanReason &banReason, int64_t bantimeoffset, bool sinceUnixEpoch) {
    CBanEntry banEntry(GetTime()+GetArg("-bantime", 60*60*24));  // Default 24-hour ban
    if (bantimeoffset > 0)
        banEntry.nBanUntil = (sinceUnixEpoch ? 0 : GetTime() )+bantimeoffset;

    {
        LOCK(cs_setBanned);
        if (setBanned[subNet].nBanUntil < banEntry.nBanUntil) {
            setBanned[subNet] = banEntry;
            setBannedIsDirty = true;
        }
        else
            return;
    }
    uiInterface.BannedListChanged();
    {
        LOCK(cs_vNodes);
        for (CNode* pnode : vNodes) {
            if (subNet.Match(static_cast<CNetAddr>(pnode->addr)))
                pnode->fDisconnect = true;
        }
    }
    if(banReason == BanReasonManuallyAdded)
        DumpBanlist(); //store banlist to disk immediately if user requested ban
}

bool CNode::Unban(const CNetAddr &addr) {
    CSubNet subNet(addr);
    return Unban(subNet);
}

bool CNode::Unban(const CSubNet &subNet) {
    {
        LOCK(cs_setBanned);
        if (!setBanned.erase(subNet))
            return false;
        setBannedIsDirty = true;
    }
    uiInterface.BannedListChanged();
    DumpBanlist(); //store banlist to disk immediately
    return true;
}

void GetBanned(banmap_t &banMap)
{
    LOCK(cs_setBanned);
    // Sweep the banlist so expired bans are not returned
    SweepBanned();
    banMap = setBanned; //create a thread safe copy
}

void SetBanned(const banmap_t &banMap)
{
    LOCK(cs_setBanned);
    setBanned = banMap;
    setBannedIsDirty = true;
}

void SweepBanned()
{
    int64_t now = GetTime();

    LOCK(cs_setBanned);
    banmap_t::iterator it = setBanned.begin();
    while(it != setBanned.end())
    {
        CSubNet subNet = (*it).first;
        CBanEntry banEntry = (*it).second;
        if(now > banEntry.nBanUntil)
        {
            setBanned.erase(it++);
            setBannedIsDirty = true;
            LogPrint("net", "%s: Removed banned node ip/subnet from banlist.dat: %s\n", __func__, subNet.ToString());
        }
        else
            ++it;
    }
}

bool BannedSetIsDirty()
{
    LOCK(cs_setBanned);
    return setBannedIsDirty;
}

void SetBannedSetDirty(bool dirty)
{
    LOCK(cs_setBanned); //reuse setBanned lock for the isDirty flag
    setBannedIsDirty = dirty;
}

std::vector<CSubNet> CNode::vWhitelistedRange;
CCriticalSection CNode::cs_vWhitelistedRange;

bool CNode::IsWhitelistedRange(const CNetAddr &addr) {
    LOCK(cs_vWhitelistedRange);
    BOOST_FOREACH(const CSubNet& subnet, vWhitelistedRange) {
        if (subnet.Match(addr))
            return true;
    }
    return false;
}

void CNode::AddWhitelistedRange(const CSubNet &subnet) {
    LOCK(cs_vWhitelistedRange);
    vWhitelistedRange.push_back(subnet);
}

void CNode::copyStats(CNodeStats &stats, const std::vector<bool> &m_asmap)
{
    stats.nodeid = this->GetId();
    stats.nServices = nServices;
    stats.addr = addr;
    // stats.addrBind = addrBind;
    stats.m_mapped_as = addr.GetMappedAS(m_asmap);
    stats.nLastSend = nLastSend;
    stats.nLastRecv = nLastRecv;
    stats.nTimeConnected = nTimeConnected;
    stats.nTimeOffset = nTimeOffset;
    stats.addrName = addrName;
    stats.nVersion = nVersion;
    stats.cleanSubVer = cleanSubVer;
    stats.fInbound = fInbound;
    stats.nStartingHeight = nStartingHeight;
    stats.nSendBytes = nSendBytes;
    stats.nRecvBytes = nRecvBytes;
    stats.fWhitelisted = fWhitelisted;

    // It is common for nodes with good ping times to suddenly become lagged,
    // due to a new block arriving or other large transfer.
    // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
    // since pingtime does not update until the ping is complete, which might take a while.
    // So, if a ping is taking an unusually long time in flight,
    // the caller can immediately detect that this is happening.
    int64_t nPingUsecWait = 0;
    if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) {
        nPingUsecWait = GetTimeMicros() - nPingUsecStart;
    }

    // Raw ping time is in microseconds, but show it to user as whole seconds (Bitcoin users should be well used to small numbers with many decimal places by now :)
    stats.dPingTime = (((double)nPingUsecTime) / 1e6);
    stats.dMinPing  = (((double)nMinPingUsecTime) / 1e6);
    stats.dPingWait = (((double)nPingUsecWait) / 1e6);

    stats.m_addr_processed = m_addr_processed.load();
    stats.m_addr_rate_limited = m_addr_rate_limited.load();

    // Leave string empty if addrLocal invalid (not filled in yet)
    stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";

    // If ssl != NULL it means TLS connection was established successfully
    {
        LOCK(cs_hSocket);
        stats.fTLSEstablished = (ssl != NULL) && (SSL_get_state(ssl) == TLS_ST_OK);
        stats.fTLSVerified = (ssl != NULL) && ValidatePeerCertificate(ssl);
    }

    stats.m_wants_addrv2 = m_wants_addrv2;
}

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char *pch, unsigned int nBytes)
{
    while (nBytes > 0) {

        // get current incomplete message, or create a new one
        if (vRecvMsg.empty() ||
            vRecvMsg.back().complete())
            vRecvMsg.push_back(CNetMessage(Params().MessageStart(), SER_NETWORK, nRecvVersion));

        CNetMessage& msg = vRecvMsg.back();

        // absorb network data
        int handled;
        if (!msg.in_data)
            handled = msg.readHeader(pch, nBytes);
        else
            handled = msg.readData(pch, nBytes);

        if (handled < 0)
                return false;

        if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH) {
            LogPrint("net", "Oversized message from peer=%i, disconnecting\n", GetId());
            return false;
        }

        pch += handled;
        nBytes -= handled;

        if (msg.complete()) {
            msg.nTime = GetTimeMicros();
            messageHandlerCondition.notify_one();
        }
    }

    return true;
}


void V1TransportSerializer::prepareForTransport(CSerializedNetMsg& msg, std::vector<unsigned char>& header) {
    // create dbl-sha256 checksum
    uint256 hash = Hash(msg.data.begin(), msg.data.end());

    // create header
    CMessageHeader hdr(Params().MessageStart(), msg.m_type.c_str(), msg.data.size());
    // memcpy(hdr.nChecksum, hash.begin(), CMessageHeader::CHECKSUM_SIZE);
    memcpy(&hdr.nChecksum, hash.begin(), CMessageHeader::CHECKSUM_SIZE);
    // serialize header
    header.reserve(CMessageHeader::HEADER_SIZE);
    CVectorWriter{SER_NETWORK, INIT_PROTO_VERSION, header, 0, hdr};
}

int CNetMessage::readHeader(const char *pch, unsigned int nBytes)
{
    // copy data to temporary parsing buffer
    unsigned int nRemaining = 24 - nHdrPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    memcpy(&hdrbuf[nHdrPos], pch, nCopy);
    nHdrPos += nCopy;

    // if header incomplete, exit
    if (nHdrPos < 24)
        return nCopy;

    // deserialize to CMessageHeader
    try {
        hdrbuf >> hdr;
    }
    catch (const std::exception&) {
        return -1;
    }

    // reject messages larger than MAX_SIZE
    if (hdr.nMessageSize > MAX_SIZE)
            return -1;

    // switch state to reading message data
    in_data = true;

    return nCopy;
}

int CNetMessage::readData(const char *pch, unsigned int nBytes)
{
    unsigned int nRemaining = hdr.nMessageSize - nDataPos;
    unsigned int nCopy = std::min(nRemaining, nBytes);

    if (vRecv.size() < nDataPos + nCopy) {
        // Allocate up to 256 KiB ahead, but never more than the total message size.
        vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
    }

    memcpy(&vRecv[nDataPos], pch, nCopy);
    nDataPos += nCopy;

    return nCopy;
}



// requires LOCK(cs_vSend)
void SocketSendData(CNode *pnode)
{
    std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

    while (it != pnode->vSendMsg.end())
    {
        const CSerializeData &data = *it;
        assert(data.size() > pnode->nSendOffset);

        bool bIsSSL = false;
        int nBytes = 0, nRet = 0;
        {
            LOCK(pnode->cs_hSocket);

            if (pnode->hSocket == INVALID_SOCKET)
            {
                LogPrint("net", "Send: connection with %s is already closed\n", pnode->addr.ToString());
                break;
            }

            bIsSSL = (pnode->ssl != NULL);

            if (bIsSSL)
            {
                ERR_clear_error(); // clear the error queue, otherwise we may be reading an old error that occurred previously in the current thread
                nBytes = SSL_write(pnode->ssl, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset);
                nRet = SSL_get_error(pnode->ssl, nBytes);
            }
            else
            {
                nBytes = send(pnode->hSocket, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
                nRet = WSAGetLastError();
            }
        }
        if (nBytes > 0)
        {
            pnode->nLastSend = GetTime();
            pnode->nSendBytes += nBytes;
            pnode->nSendOffset += nBytes;
            pnode->RecordBytesSent(nBytes);

            if (pnode->nSendOffset == data.size())
            {
                pnode->nSendOffset = 0;
                pnode->nSendSize -= data.size();
                it++;
            }
            else
            {
                // could not send full message; stop sending more
                break;
            }
        }
        else
        {
            if (nBytes <= 0)
            {
                // error
                //
                if (bIsSSL)
                {
                    if (nRet != SSL_ERROR_WANT_READ && nRet != SSL_ERROR_WANT_WRITE)
                    {
                        LogPrintf("ERROR: SSL_write %s; closing connection\n", ERR_error_string(nRet, NULL));
                        pnode->CloseSocketDisconnect();
                    }
                    else
                    {
                        // preventive measure from exhausting CPU usage
                        //
                        MilliSleep(1);    // 1 msec
                    }
                }
                else
                {
                    if (nRet != WSAEWOULDBLOCK && nRet != WSAEMSGSIZE && nRet != WSAEINTR && nRet != WSAEINPROGRESS)
                    {
                        LogPrintf("ERROR: send %s; closing connection\n", NetworkErrorString(nRet));
                        pnode->CloseSocketDisconnect();
                    }
                }
            }

            // couldn't send anything at all
            break;
        }
    }

    if (it == pnode->vSendMsg.end())
    {
        assert(pnode->nSendOffset == 0);
        assert(pnode->nSendSize == 0);
    }
    pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

static list<CNode*> vNodesDisconnected;

class CNodeRef {
public:
    CNodeRef(CNode *pnode) : _pnode(pnode) {
        LOCK(cs_vNodes);
        _pnode->AddRef();
    }

    ~CNodeRef() {
        LOCK(cs_vNodes);
        _pnode->Release();
    }

    CNode& operator *() const {return *_pnode;};
    CNode* operator ->() const {return _pnode;};

    CNodeRef& operator =(const CNodeRef& other)
    {
        if (this != &other) {
            LOCK(cs_vNodes);

            _pnode->Release();
            _pnode = other._pnode;
            _pnode->AddRef();
        }
        return *this;
    }

    CNodeRef(const CNodeRef& other):
        _pnode(other._pnode)
    {
        LOCK(cs_vNodes);
        _pnode->AddRef();
    }
private:
    CNode *_pnode;
};

static bool ReverseCompareNodeMinPingTime(const CNodeRef &a, const CNodeRef &b)
{
    return a->nMinPingUsecTime > b->nMinPingUsecTime;
}

static bool ReverseCompareNodeTimeConnected(const CNodeRef &a, const CNodeRef &b)
{
    return a->nTimeConnected > b->nTimeConnected;
}

class CompareNetGroupKeyed
{
    std::vector<unsigned char> vchSecretKey;
public:
    CompareNetGroupKeyed()
    {
        vchSecretKey.resize(32, 0);
        GetRandBytes(vchSecretKey.data(), vchSecretKey.size());
    }

    bool operator()(const CNodeRef &a, const CNodeRef &b)
    {
        std::vector<unsigned char> vchGroupA, vchGroupB;
        CSHA256 hashA, hashB;
        std::vector<unsigned char> vchA(32), vchB(32);

        vchGroupA = a->addr.GetGroup(addrman.m_asmap);
        vchGroupB = b->addr.GetGroup(addrman.m_asmap);

        hashA.Write(begin_ptr(vchGroupA), vchGroupA.size());
        hashB.Write(begin_ptr(vchGroupB), vchGroupB.size());

        hashA.Write(begin_ptr(vchSecretKey), vchSecretKey.size());
        hashB.Write(begin_ptr(vchSecretKey), vchSecretKey.size());

        hashA.Finalize(begin_ptr(vchA));
        hashB.Finalize(begin_ptr(vchB));

        return vchA < vchB;
    }
};

static bool AttemptToEvictConnection(bool fPreferNewConnection) {
    std::vector<CNodeRef> vEvictionCandidates;
    {
        LOCK(cs_vNodes);

        BOOST_FOREACH(CNode *node, vNodes) {
            if (node->fWhitelisted)
                continue;
            if (!node->fInbound)
                continue;
            if (node->fDisconnect)
                continue;
            vEvictionCandidates.push_back(CNodeRef(node));
        }
    }

    if (vEvictionCandidates.empty()) return false;

    // Protect connections with certain characteristics

    // Check version of eviction candidates and prioritize nodes which do not support network upgrade.
    std::vector<CNodeRef> vTmpEvictionCandidates;
    int height;
    {
        LOCK(cs_main);
        height = chainActive.Height();
    }

    const Consensus::Params& params = Params().GetConsensus();
    auto nextEpoch = NextEpoch(height, params);
    if (nextEpoch) {
        auto idx = nextEpoch.get();
        int nActivationHeight = params.vUpgrades[idx].nActivationHeight;

        if (nActivationHeight > 0 &&
            height < nActivationHeight &&
            height >= nActivationHeight - NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD)
        {
            // Find any nodes which don't support the protocol version for the next upgrade
            for (const CNodeRef &node : vEvictionCandidates) {
                if (node->nVersion < params.vUpgrades[idx].nProtocolVersion) {
                    vTmpEvictionCandidates.push_back(node);
                }
            }

            // Prioritize these nodes by replacing eviction set with them
            if (vTmpEvictionCandidates.size() > 0) {
                vEvictionCandidates = vTmpEvictionCandidates;
            }
        }
    }

    // Deterministically select 4 peers to protect by netgroup.
    // An attacker cannot predict which netgroups will be protected.
    static CompareNetGroupKeyed comparerNetGroupKeyed;
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), comparerNetGroupKeyed);
    vEvictionCandidates.erase(vEvictionCandidates.end() - std::min(4, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Protect the 8 nodes with the best ping times.
    // An attacker cannot manipulate this metric without physically moving nodes closer to the target.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeMinPingTime);
    vEvictionCandidates.erase(vEvictionCandidates.end() - std::min(8, static_cast<int>(vEvictionCandidates.size())), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Protect the half of the remaining nodes which have been connected the longest.
    // This replicates the existing implicit behavior.
    std::sort(vEvictionCandidates.begin(), vEvictionCandidates.end(), ReverseCompareNodeTimeConnected);
    vEvictionCandidates.erase(vEvictionCandidates.end() - static_cast<int>(vEvictionCandidates.size() / 2), vEvictionCandidates.end());

    if (vEvictionCandidates.empty()) return false;

    // Identify the network group with the most connections and youngest member.
    // (vEvictionCandidates is already sorted by reverse connect time)
    std::vector<unsigned char> naMostConnections;
    unsigned int nMostConnections = 0;
    int64_t nMostConnectionsTime = 0;
    std::map<std::vector<unsigned char>, std::vector<CNodeRef> > mapAddrCounts;
    BOOST_FOREACH(const CNodeRef &node, vEvictionCandidates) {
        mapAddrCounts[node->addr.GetGroup(addrman.m_asmap)].push_back(node);
        int64_t grouptime = mapAddrCounts[node->addr.GetGroup(addrman.m_asmap)][0]->nTimeConnected;
        size_t groupsize = mapAddrCounts[node->addr.GetGroup(addrman.m_asmap)].size();

        if (groupsize > nMostConnections || (groupsize == nMostConnections && grouptime > nMostConnectionsTime)) {
            nMostConnections = groupsize;
            nMostConnectionsTime = grouptime;
            naMostConnections = node->addr.GetGroup(addrman.m_asmap);
        }
    }

    // Reduce to the network group with the most connections
    vEvictionCandidates = mapAddrCounts[naMostConnections];

    // Do not disconnect peers if there is only one unprotected connection from their network group.
    if (vEvictionCandidates.size() <= 1)
        // unless we prefer the new connection (for whitelisted peers)
        if (!fPreferNewConnection)
            return false;

    // Disconnect from the network group with the most connections
    vEvictionCandidates[0]->fDisconnect = true;

    return true;
}

static void AcceptConnection(const ListenSocket& hListenSocket) {
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr*)&sockaddr, &len);
    CAddress addr;

    if (hSocket == INVALID_SOCKET) {
        const int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK) {
            LogPrintf("socket error accept failed: %s\n", NetworkErrorString(nErr));
        }
        return;
    }

    if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr)) {
        LogPrintf("Warning: Unknown socket family\n");
    }

    const CAddress addr_bind = GetBindAddress(hSocket);

    bool whitelisted = hListenSocket.whitelisted || CNode::IsWhitelistedRange(addr);

    CreateNodeFromAcceptedSocket(hSocket, whitelisted, addr_bind, addr);
}

void CreateNodeFromAcceptedSocket(SOCKET hSocket,
                                            bool whitelisted,
                                            const CAddress& addr_bind,
                                            const CAddress& addr)
{
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    int nInboundThisIP = 0;
    int nInbound = 0;
    int nMaxInbound = nMaxConnections - MAX_OUTBOUND_CONNECTIONS;


    {
        LOCK(cs_vNodes);
        struct sockaddr_storage tmpsockaddr;
        socklen_t tmplen = sizeof(sockaddr);
        BOOST_FOREACH(CNode* pnode, vNodes)
        {
            if (pnode->fInbound)
            {
                nInbound++;
                if (pnode->addr.GetSockAddr((struct sockaddr*)&tmpsockaddr, &tmplen) && (tmplen == len) && (memcmp(&sockaddr, &tmpsockaddr, tmplen) == 0))
                    nInboundThisIP++;
            }
        }
    }

    if (!fNetworkActive) {
        LogPrintf("connection from %s dropped: not accepting new connections\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (!IsSelectableSocket(hSocket))
    {
        LogPrintf("connection from %s dropped: non-selectable socket\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (CNode::IsBanned(addr) && !whitelisted)
    {
        LogPrintf("connection from %s dropped (banned)\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    if (!IsReachable(addr))
    {
        CloseSocket(hSocket);
        return;
    }

    if (nInbound >= nMaxInbound)
    {
        if (!AttemptToEvictConnection(whitelisted)) {
            // No connection to evict, disconnect the new connection
            LogPrint("net", "failed to find an eviction candidate - connection dropped (full)\n");
            CloseSocket(hSocket);
            return;
        }
    }

    if (nInboundThisIP >= MAX_INBOUND_FROMIP)
    {
        // No connection to evict, disconnect the new connection
        LogPrint("net", "too many connections from %s, connection refused\n", addr.ToString());
        CloseSocket(hSocket);
        return;
    }

    // According to the internet TCP_NODELAY is not carried into accepted sockets
    // on all platforms.  Set it again here just to be sure.
    int set = 1;
#ifdef _WIN32
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (const char*)&set, sizeof(int));
#else
    setsockopt(hSocket, IPPROTO_TCP, TCP_NODELAY, (void*)&set, sizeof(int));
#endif

SSL *ssl = NULL;

SetSocketNonBlocking(hSocket, true);

#ifdef USE_TLS

    {
        LOCK(cs_vNonTLSNodesInbound);

        NODE_ADDR nodeAddr(addr.ToStringIP());
        bool bTlsEnforcement = (GetBoolArg("-tlsenforcement", true) || GetArg("-tlsenforcement", "") == "1");
        bool bUseTLS = (find(vNonTLSNodesInbound.begin(), vNonTLSNodesInbound.end(), nodeAddr) == vNonTLSNodesInbound.end());
        unsigned long err_code = 0;

        //Check for plaintext peer
        const std::vector<std::string>& vAllow = mapMultiArgs["-plaintextpeer"];
        bool plaintextpeer = (std::find(vAllow.begin(), vAllow.end(), addr.ToStringIP()) != vAllow.end());


        /* TCP connection is ready. Do server side SSL. */
        if (CNode::GetTlsFallbackNonTls()) {
            LogPrint("tls", "%s():%d - handling connection to %s\n", __func__, __LINE__,  addr.ToStringIP());
            if (plaintextpeer) {
                LogPrint("tls", "%s():%d - handling plaintext peer connection to %s\n", __func__, __LINE__,  addr.ToStringIP());
                // Further reconnection will be made in non-TLS (unencrypted) mode
                if (!bUseTLS)  {// Already in the list
                    vNonTLSNodesInbound.push_back(NODE_ADDR(addr.ToStringIP(), GetTimeMillis()));
                    LogPrint("tls", "%s():%d - err_code %x, adding connection from %s vNonTLSNodesInbound list (sz=%d)\n",__func__, __LINE__, err_code, addr.ToStringIP(), vNonTLSNodesInbound.size());
                }
            } else {
                ssl = tlsmanager.accept( hSocket, addr, err_code);
                if(!ssl) {
                    if (err_code == TLSManager::SELECT_TIMEDOUT) {
                        // can fail also for timeout in select on fd, that is not a ssl error and we should not
                        // consider this node as non TLS
                        LogPrint("tls", "%s():%d - Connection from %s timedout\n", __func__, __LINE__, addr.ToStringIP());
                    } else {
                        //Do not allow unencrypted connection if TLS Enforcement is set
                        if (!bTlsEnforcement) {
                            // Further reconnection will be made in non-TLS (unencrypted) mode
                            vNonTLSNodesInbound.push_back(NODE_ADDR(addr.ToStringIP(), GetTimeMillis()));
                            LogPrint("tls", "%s():%d - err_code %x, adding connection from %s vNonTLSNodesInbound list (sz=%d)\n",__func__, __LINE__, err_code, addr.ToStringIP(), vNonTLSNodesInbound.size());
                        }
                    }
                    CloseSocket(hSocket);
                    return;
                }
            }
        } else {
            if (plaintextpeer) {
                // Further reconnection will be made in non-TLS (unencrypted) mode
                if (!bUseTLS)  {// Already in the list
                    vNonTLSNodesInbound.push_back(NODE_ADDR(addr.ToStringIP(), GetTimeMillis()));
                    LogPrint("tls", "%s():%d - err_code %x, adding connection from %s vNonTLSNodesInbound list (sz=%d)\n",__func__, __LINE__, err_code, addr.ToStringIP(), vNonTLSNodesInbound.size());
                }
            } else {
                unsigned long err_code = 0;
                ssl = tlsmanager.accept( hSocket, addr, err_code);
                if(!ssl) {
                    LogPrint("tls", "%s():%d - err_code %x, failure accepting connection from %s\n",__func__, __LINE__, err_code, addr.ToStringIP());
                    CloseSocket(hSocket);
                    return;
                }
            }
        }
    }
    // certificate validation is disabled by default
    if (CNode::GetTlsValidate())
    {
        if (ssl && !ValidatePeerCertificate(ssl))
        {
            LogPrintf ("TLS: ERROR: Wrong client certificate from %s. Connection will be closed.\n", addr.ToString());

            SSL_shutdown(ssl);
            CloseSocket(hSocket);
            SSL_free(ssl);
            return;
        }
    }
#endif // USE_TLS

    CNode* pnode = new CNode(hSocket, addr, "", true, ssl);
    pnode->AddRef();
    pnode->fWhitelisted = whitelisted;

    LogPrint("net", "connection from %s accepted\n", addr.ToString());

    {
        LOCK(cs_vNodes);
        vNodes.push_back(pnode);
    }
}

#if defined(USE_TLS)
void ThreadNonTLSPoolsCleaner()
{
    while (true)
    {
        tlsmanager.cleanNonTLSPool(vNonTLSNodesInbound,  cs_vNonTLSNodesInbound);
        tlsmanager.cleanNonTLSPool(vNonTLSNodesOutbound, cs_vNonTLSNodesOutbound);
        MilliSleep(DEFAULT_CONNECT_TIMEOUT);  // sleep and sleep_for are interruption points, which will throw boost::thread_interrupted
    }
}

#endif // USE_TLS

void ThreadSocketHandler()
{
    unsigned int nPrevNodeCount = 0;
    while (true)
    {
        //
        // Disconnect nodes
        //
        {
            LOCK(cs_vNodes);
            // Disconnect unused nodes
            vector<CNode*> vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
            {
                if (pnode->fDisconnect ||
                    (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() && pnode->nSendSize == 0 && pnode->ssSend.empty()))
                {
                    // remove from vNodes
                    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

                    // release outbound grant (if any)
                    pnode->grantOutbound.Release();

                    // close socket and cleanup
                    pnode->CloseSocketDisconnect();

                    // hold in disconnected pool until all refs are released
                    if (pnode->fNetworkNode || pnode->fInbound)
                        pnode->Release();
                    vNodesDisconnected.push_back(pnode);
                }
            }
        }
        {
            // Delete disconnected nodes
            list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
            BOOST_FOREACH(CNode* pnode, vNodesDisconnectedCopy)
            {
                // wait until threads are done using it
                if (pnode->GetRefCount() <= 0)
                {
                    bool fDelete = false;
                    {
                        TRY_LOCK(pnode->cs_vSend, lockSend);
                        if (lockSend)
                        {
                            TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                            if (lockRecv)
                            {
                                TRY_LOCK(pnode->cs_inventory, lockInv);
                                if (lockInv)
                                    fDelete = true;
                            }
                        }
                    }
                    if (fDelete)
                    {
                        vNodesDisconnected.remove(pnode);
                        delete pnode;
                    }
                }
            }
        }
        if(vNodes.size() != nPrevNodeCount) {
            nPrevNodeCount = vNodes.size();
            uiInterface.NotifyNumConnectionsChanged(nPrevNodeCount);
        }

        //
        // Find which sockets have data to receive
        //
        struct timeval timeout;
        timeout.tv_sec  = 0;
        timeout.tv_usec = 50000; // frequency to poll pnode->vSend

        fd_set fdsetRecv;
        fd_set fdsetSend;
        fd_set fdsetError;
        FD_ZERO(&fdsetRecv);
        FD_ZERO(&fdsetSend);
        FD_ZERO(&fdsetError);
        SOCKET hSocketMax = 0;
        bool have_fds = false;

        BOOST_FOREACH(const ListenSocket& hListenSocket, vhListenSocket) {
            FD_SET(hListenSocket.socket, &fdsetRecv);
            hSocketMax = max(hSocketMax, hListenSocket.socket);
            have_fds = true;
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                LOCK(pnode->cs_hSocket);

                if (pnode->hSocket == INVALID_SOCKET)
                    continue;

                FD_SET(pnode->hSocket, &fdsetError);
                hSocketMax = max(hSocketMax, pnode->hSocket);
                have_fds = true;

                // Implement the following logic:
                // * If there is data to send, select() for sending data. As this only
                //   happens when optimistic write failed, we choose to first drain the
                //   write buffer in this case before receiving more. This avoids
                //   needlessly queueing received data, if the remote peer is not themselves
                //   receiving data. This means properly utilizing TCP flow control signaling.
                // * Otherwise, if there is no (complete) message in the receive buffer,
                //   or there is space left in the buffer, select() for receiving data.
                // * (if neither of the above applies, there is certainly one message
                //   in the receiver buffer ready to be processed).
                // Together, that means that at least one of the following is always possible,
                // so we don't deadlock:
                // * We send some data.
                // * We wait for data to be received (and disconnect after timeout).
                // * We process a message in the buffer (message handler thread).
                {
                    TRY_LOCK(pnode->cs_vSend, lockSend);
                    if (lockSend && !pnode->vSendMsg.empty()) {
                        FD_SET(pnode->hSocket, &fdsetSend);
                        continue;
                    }
                }
                {
                    TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                    if (lockRecv && (
                        pnode->vRecvMsg.empty() || !pnode->vRecvMsg.front().complete() ||
                        pnode->GetTotalRecvSize() <= ReceiveFloodSize()))
                        FD_SET(pnode->hSocket, &fdsetRecv);
                }
            }
        }

        int nSelect = select(have_fds ? hSocketMax + 1 : 0,
                             &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
        boost::this_thread::interruption_point();

        if (nSelect == SOCKET_ERROR)
        {
            if (have_fds)
            {
                int nErr = WSAGetLastError();
                LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
                for (unsigned int i = 0; i <= hSocketMax; i++)
                    FD_SET(i, &fdsetRecv);
            }
            FD_ZERO(&fdsetSend);
            FD_ZERO(&fdsetError);
            MilliSleep(timeout.tv_usec/1000);
        }

        //
        // Accept new connections
        //
        BOOST_FOREACH(const ListenSocket& hListenSocket, vhListenSocket)
        {
            if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv))
            {
                AcceptConnection(hListenSocket);
            }
        }

        //
        // Service each socket
        //
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->AddRef();
        }
        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            boost::this_thread::interruption_point();

            if (tlsmanager.threadSocketHandler(pnode,fdsetRecv,fdsetSend,fdsetError)==-1){
                continue;
            }

            //
            // Inactivity checking
            //
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                {
                    LogPrint("net", "socket no message in first 60 seconds, %d %d from %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL)
                {
                    LogPrintf("socket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? TIMEOUT_INTERVAL : 90*60))
                {
                    LogPrintf("socket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingRetry > MAX_PING_RETRY)
                {
                    LogPrintf("ping max retry exceeded, disconnecting node %i\n", pnode->id);
                    pnode->fDisconnect = true;
                }
            }
        }
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }
    }
}

void ThreadDNSAddressSeed()
{
    // goal: only query DNS seeds if address need is acute
    if ((addrman.size() > 0) &&
        (!GetBoolArg("-forcednsseed", false))) {
        MilliSleep(11 * 1000);

        LOCK(cs_vNodes);
        if (vNodes.size() >= 16) {
            LogPrintf("P2P peers available. Skipped DNS seeding.\n");
            return;
        }
    }

    const vector<CDNSSeedData> &vSeeds = Params().DNSSeeds();
    int found = 0;

    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    BOOST_FOREACH(const CDNSSeedData &seed, vSeeds) {
        if (HaveNameProxy()) {
            AddOneShot(seed.host);
        } else {
            vector<CNetAddr> vIPs;
            vector<CAddress> vAdd;
            if (LookupHost(seed.host.c_str(), vIPs, 256, true))
            {
                BOOST_FOREACH(const CNetAddr& ip, vIPs)
                {
                    int nOneDay = 24*3600;
                    CAddress addr = CAddress(CService(ip, ASSETCHAINS_P2PPORT));
                    addr.nTime = GetTime() - 3*nOneDay - GetRand(4*nOneDay); // use a random age between 3 and 7 days old
                    vAdd.push_back(addr);
                }
            }
            // TODO: The seed name resolve may fail, yielding an IP of [::], which results in
            // addrman assigning the same source to results from different seeds.
            // This should switch to a hard-coded stable dummy IP for each seed name, so that the
            // resolve is not required at all.
            if (!vIPs.empty()) {
                CService seedSource;
                Lookup(seed.name.c_str(), seedSource, 0, true);
                addrman.Add(vAdd, seedSource);
            }
        }
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
}


void DumpAddresses()
{
    int64_t nStart = GetTimeMillis();

    CAddrDB adb;
    adb.Write(addrman);

    LogPrint("net", "Flushed %d addresses to peers.dat  %dms\n",
           addrman.size(), GetTimeMillis() - nStart);
}

void static ProcessOneShot()
{
    string strDest;
    {
        LOCK(cs_vOneShots);
        if (vOneShots.empty())
            return;
        strDest = vOneShots.front();
        vOneShots.pop_front();
    }
    CAddress addr;
    CSemaphoreGrant grant(*semOutbound, true);
    if (grant) {
        if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true))
            AddOneShot(strDest);
    }
}

void ThreadOpenConnections()
{
    // Connect to specific addresses
    if (mapArgs.count("-connect") && mapMultiArgs["-connect"].size() > 0)
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-connect"])
            {
                CAddress addr;
                OpenNetworkConnection(addr, NULL, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    MilliSleep(500);
                }
            }
            MilliSleep(500);
        }
    }

    // Initiate network connections
    int64_t nStart = GetTime();
    while (true)
    {
        if (ShutdownRequested())
            break;

        ProcessOneShot();

        MilliSleep(500);

        CSemaphoreGrant grant(*semOutbound);

        // Add known valid seed at the time this release
        if (GetTime() - nStart > 60) {
            static bool done = false;
            if (!done) {
                LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                std::vector<CAddress> vFixedSeeds = ConvertSeeds(Params().FixedSeeds());
                BOOST_FOREACH(CAddress fixedSeed, vFixedSeeds) {
                    std::vector<CAddress> vFixedSeed;
                    vFixedSeed.push_back(fixedSeed);
                    CService seedSource;
                    Lookup(fixedSeed.ToString().c_str(), seedSource, Params().GetDefaultPort(), false);
                    addrman.Add(vFixedSeed, seedSource);
                }
                done = true;
            }
        }


        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
        int nOutbound = 0;
        set<vector<unsigned char> > setConnected;
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes) {
                if (!pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup(addrman.m_asmap));
                    nOutbound++;
                }
            }
        }

        int64_t nANow = GetTime();

        int nTries = 0;
        while (true)
        {
            if (ShutdownRequested())
                break;

            CAddrInfo addr = addrman.Select();

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup(addrman.m_asmap)) || IsLocal(addr))
                break;

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (!IsReachable(addr)) {
                continue;
            }

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // do not allow non-default ports, unless after 50 invalid addresses selected already
            if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
                continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())
            OpenNetworkConnection(addrConnect, &grant);
    }
}

void ThreadOpenAddedConnections()
{
    {
        LOCK(cs_vAddedNodes);
        vAddedNodes = mapMultiArgs["-addnode"];
    }

    if (HaveNameProxy()) {
        while(true) {
            list<string> lAddresses(0);
            {
                LOCK(cs_vAddedNodes);
                BOOST_FOREACH(const std::string& strAddNode, vAddedNodes)
                    lAddresses.push_back(strAddNode);
            }
            BOOST_FOREACH(const std::string& strAddNode, lAddresses) {
                CAddress addr;
                CSemaphoreGrant grant(*semOutbound);
                OpenNetworkConnection(addr, &grant, strAddNode.c_str());
                MilliSleep(500);
            }
            MilliSleep(120000); // Retry every 2 minutes
        }
    }

    for (unsigned int i = 0; true; i++)
    {
        list<string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            BOOST_FOREACH(const std::string& strAddNode, vAddedNodes)
                lAddresses.push_back(strAddNode);
        }

        list<vector<CService> > lservAddressesToAdd(0);
        BOOST_FOREACH(const std::string& strAddNode, lAddresses) {
            vector<CService> vservNode(0);
            if(Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0))
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeAddresses);
                    BOOST_FOREACH(const CService& serv, vservNode)
                        setservAddNodeAddresses.insert(serv);
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
                for (list<vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                {
                    BOOST_FOREACH(const CService& addrNode, *(it))
                        if (pnode->addr == addrNode)
                        {
                            it = lservAddressesToAdd.erase(it);
                            if ( it != lservAddressesToAdd.begin() )
                                it--;
                            break;
                        }
                    if (it == lservAddressesToAdd.end())
                        break;
                }
        }
        BOOST_FOREACH(vector<CService>& vserv, lservAddressesToAdd)
        {
            CSemaphoreGrant grant(*semOutbound);
            OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
            MilliSleep(500);
        }
        MilliSleep(120000); // Retry every 2 minutes
    }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound, const char *pszDest, bool fOneShot)
{
    //
    // Initiate outbound network connection
    //
    boost::this_thread::interruption_point();
    if (!fNetworkActive) {
        return false;
    }

    if (!IsReachable(addrConnect)) {
        return false;
    }

    if (!pszDest) {
        if (IsLocal(addrConnect) ||
            FindNode(static_cast<CNetAddr>(addrConnect)) || CNode::IsBanned(addrConnect) ||
            FindNode(addrConnect.ToStringIPPort()))
            return false;
    } else if (FindNode(std::string(pszDest)))
        return false;

    CNode* pnode = ConnectNode(addrConnect, pszDest);
    boost::this_thread::interruption_point();

#if defined(USE_TLS)
    if (CNode::GetTlsFallbackNonTls())
    {
        if (!pnode)
        {
            string strDest;
            int port;

            if (!pszDest)
                strDest = addrConnect.ToStringIP();
            else
                SplitHostPort(string(pszDest), port, strDest);

            if (tlsmanager.isNonTLSAddr(strDest, vNonTLSNodesOutbound, cs_vNonTLSNodesOutbound))
            {
                // Attempt to reconnect in non-TLS mode
                pnode = ConnectNode(addrConnect, pszDest);
                boost::this_thread::interruption_point();
            }
        }
    }

#endif

    if (!pnode)
        return false;
    if (grantOutbound)
        grantOutbound->MoveTo(pnode->grantOutbound);
    pnode->fNetworkNode = true;
    if (fOneShot)
        pnode->fOneShot = true;

    return true;
}


void ThreadMessageHandler()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true)
    {
        vector<CNode*> vNodesCopy;
        {
            LOCK(cs_vNodes);
            vNodesCopy = vNodes;
            BOOST_FOREACH(CNode* pnode, vNodesCopy) {
                pnode->AddRef();
            }
        }

        // Poll the connected nodes for messages
        CNode* pnodeTrickle = NULL;
        if (!vNodesCopy.empty())
            pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];

        bool fSleep = true;

        BOOST_FOREACH(CNode* pnode, vNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            // Receive messages
            {
                TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
                if (lockRecv)
                {
                    if (!g_signals.ProcessMessages(pnode))
                        pnode->CloseSocketDisconnect();

                    if (pnode->nSendSize < SendBufferSize())
                    {
                        if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete()))
                        {
                            fSleep = false;
                        }
                    }
                }
            }
            boost::this_thread::interruption_point();

            // Send messages
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)
                    g_signals.SendMessages(pnode, pnode == pnodeTrickle || pnode->fWhitelisted);
            }
            boost::this_thread::interruption_point();
        }

        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodesCopy)
                pnode->Release();
        }

        if (fSleep)
            messageHandlerCondition.timed_wait(lock, boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
    }
}

void ThreadI2PCheck()
{
    const int64_t wait_time = 5000;
    const int64_t err_wait_cap = wait_time * 60;
    auto err_wait = wait_time;

    bool advertising_listen_addr = false;
    i2p::Connection conn;

    while (true) {

        MilliSleep(wait_time);

        boost::this_thread::interruption_point();

        if (!m_i2p_sam_session->Check()) {

            MilliSleep(err_wait);

            if (err_wait < err_wait_cap) {
                err_wait *= 2;
            }

        } else {
            err_wait = wait_time;
        }
    }
}

void ThreadI2PAcceptIncoming()
{
    const int64_t err_wait_begin = 1000;
    const int64_t err_wait_cap = 1000 * 60 * 5;
    auto err_wait = err_wait_begin;

    bool advertising_listen_addr = false;
    i2p::Connection conn;

    while (true) {

        boost::this_thread::interruption_point();

        if (!m_i2p_sam_session->Listen(conn)) {
            if (advertising_listen_addr && conn.me.IsValid()) {
                RemoveLocal(conn.me);
                advertising_listen_addr = false;
            }

            MilliSleep(err_wait);

            if (err_wait < err_wait_cap) {
                err_wait *= 2;
            }

            continue;
        }

        if (!advertising_listen_addr) {
            AddLocal(conn.me, LOCAL_MANUAL);
            advertising_listen_addr = true;
        }

        if (!m_i2p_sam_session->Accept(conn)) {
            continue;
        }

        CreateNodeFromAcceptedSocket(conn.sock->Release(), false,
                                     CAddress{conn.me, NODE_NETWORK}, CAddress{conn.peer, NODE_NETWORK});
    }
}

bool BindListenPort(const CService &addrBind, string& strError, bool fWhitelisted)
{
    strError = "";
    int nOne = 1;

    // Create socket for listening for incoming connections
    struct sockaddr_storage sockaddr;
    socklen_t len = sizeof(sockaddr);
    if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len))
    {
        strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
        LogPrintf("%s\n", strError);
        return false;
    }

    SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
    if (hListenSocket == INVALID_SOCKET)
    {
        strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %s)", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }

    if (!IsSelectableSocket(hListenSocket))
    {
        strError = "Error: Couldn't create a listenable socket for incoming connections";
        LogPrintf("%s\n", strError);
        return false;
    }

    // Allow binding if the port is still in TIME_WAIT state after
    // the program was closed and restarted.
    setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (sockopt_arg_type)&nOne, sizeof(int));

    // Set to non-blocking, incoming connections will also inherit this
    //
    // WARNING!
    // On Linux, the new socket returned by accept() does not inherit file
    // status flags such as O_NONBLOCK and O_ASYNC from the listening
    // socket. http://man7.org/linux/man-pages/man2/accept.2.html
    if (!SetSocketNonBlocking(hListenSocket, true)) {
        strError = strprintf("BindListenPort: Setting listening socket to non-blocking failed, error %s\n", NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        return false;
    }

    // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
    // and enable it by default or not. Try to enable it, if possible.
    if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (sockopt_arg_type)&nOne, sizeof(int));
#endif
#ifdef _WIN32
        int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
        setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
#endif
    }

    if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR)
    {
        int nErr = WSAGetLastError();
        if (nErr == WSAEADDRINUSE)
            strError = strprintf(_("Unable to bind to %s on this computer. Treasure Chest is probably already running."), addrBind.ToString());
        else
            strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %s)"), addrBind.ToString(), NetworkErrorString(nErr));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }
    LogPrintf("Bound to %s\n", addrBind.ToString());

    // Listen for incoming connections
    if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        strError = strprintf(_("Error: Listening for incoming connections failed (listen returned error %s)"), NetworkErrorString(WSAGetLastError()));
        LogPrintf("%s\n", strError);
        CloseSocket(hListenSocket);
        return false;
    }

    vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

    if (addrBind.IsRoutable() && fDiscover && !fWhitelisted)
        AddLocal(addrBind, LOCAL_BIND);

    return true;
}

void static Discover(boost::thread_group& threadGroup)
{
    if (!fDiscover)
        return;

#ifdef _WIN32
    // Get local host IP
    char pszHostName[256] = "";
    if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR)
    {
        vector<CNetAddr> vaddr;
        if (LookupHost(pszHostName, vaddr, 256, false))
        {
            BOOST_FOREACH (const CNetAddr &addr, vaddr)
            {
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: %s - %s\n", __func__, pszHostName, addr.ToString());
            }
        }
    }
#else
    // Get local host ip
    struct ifaddrs* myaddrs;
    if (getifaddrs(&myaddrs) == 0)
    {
        for (struct ifaddrs* ifa = myaddrs; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (strcmp(ifa->ifa_name, "lo") == 0) continue;
            if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
                CNetAddr addr(s4->sin_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
            else if (ifa->ifa_addr->sa_family == AF_INET6)
            {
                struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
                CNetAddr addr(s6->sin6_addr);
                if (AddLocal(addr, LOCAL_IF))
                    LogPrintf("%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
            }
        }
        freeifaddrs(myaddrs);
    }
#endif
}

void LoadPeers() {
      uiInterface.InitMessage(_("Loading addresses..."));
      // Load addresses for peers.dat
      int64_t nStart = GetTimeMillis();
      {
          CAddrDB adb;
          if (!adb.Read(addrman)) {
              addrman.Clear();
              LogPrintf("Invalid or missing peers.dat; recreating\n");
          }
      }
      LogPrintf("Loaded %i addresses from peers.dat  %dms\n",
             addrman.size(), GetTimeMillis() - nStart);
      fAddressesInitialized = true;
}

void StartNode(boost::thread_group& threadGroup, CScheduler& scheduler)
{

    if (GetBoolArg("-nspv_msg", DEFAULT_NSPV_PROCESSING)) {
        nLocalServices |= NODE_NSPV;
        LogPrintf("NSPV messages processing enabled\n");
    }

    proxyType i2p_sam;
    if (GetProxy(NET_I2P, i2p_sam)) {
        m_i2p_sam_session = std::unique_ptr<i2p::sam::Session>(new i2p::sam::Session(GetDataDir() / "i2p_private_key",
                                                                i2p_sam.proxy));
    }

    if (semOutbound == NULL) {
        // initialize semaphore
        int nMaxOutbound = min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
        semOutbound = new CSemaphore(nMaxOutbound);
    }

    if (pnodeLocalHost == NULL) {
        CNetAddr local;
        LookupHost("127.0.0.1", local, false);
        pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService(local, 0), nLocalServices));
    }

    Discover(threadGroup);

#ifdef USE_TLS

    if (!tlsmanager.prepareCredentials())
    {
        LogPrintf("TLS: ERROR: %s: %s: Credentials weren't loaded. Node can't be started.\n", __FILE__, __func__);
        return;
    }

    if (!tlsmanager.initialize())
    {
        LogPrintf("TLS: ERROR: %s: %s: TLS initialization failed. Node can't be started.\n", __FILE__, __func__);
        return;
    }
#else
    LogPrintf("TLS is not used!\n");
#endif

    // skip DNS seeds for staked chains.
    if ( is_STAKED(chainName.symbol()) != 0 )
        SoftSetBoolArg("-dnsseed", false);

    //
    // Start threads
    //

    if (!GetBoolArg("-dnsseed", true))
        LogPrintf("DNS seeding disabled\n");
    else
        threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "dnsseed", &ThreadDNSAddressSeed));

    // Send and receive from sockets, accept connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "net", &ThreadSocketHandler));

    //Listen for I2P connections, or periodically check the i2p control port.
    if (m_i2p_sam_session.get() != nullptr) {
        if (GetBoolArg("-i2pacceptincoming", true) ) {
            threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "i2paccept", &ThreadI2PAcceptIncoming));
        } else {
            threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "i2pcheck", &ThreadI2PCheck));
        }
    }
    // Initiate outbound connections from -addnode
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "addcon", &ThreadOpenAddedConnections));

    // Initiate outbound connections
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "opencon", &ThreadOpenConnections));

    // Process messages
    threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler));

    #if defined(USE_TLS)
        if (CNode::GetTlsFallbackNonTls())
        {
            // Clean pools of addresses for non-TLS connections
            threadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "poolscleaner", &ThreadNonTLSPoolsCleaner));
        }
    #endif

    // Dump network addresses
    scheduler.scheduleEvery(&DumpAddresses, DUMP_ADDRESSES_INTERVAL);
}

bool StopNode()
{
    LogPrintf("StopNode()\n");
    if (semOutbound)
        for (int i=0; i<MAX_OUTBOUND_CONNECTIONS; i++)
            semOutbound->post();

    if (KOMODO_NSPV_FULLNODE && fAddressesInitialized)
    {
        DumpAddresses();
        fAddressesInitialized = false;
    }

    return true;
}

static class CNetCleanup
{
public:
    CNetCleanup() {}

    ~CNetCleanup()
    {
        // Close sockets
        BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->hSocket != INVALID_SOCKET)
                CloseSocket(pnode->hSocket);
        BOOST_FOREACH(ListenSocket& hListenSocket, vhListenSocket)
            if (hListenSocket.socket != INVALID_SOCKET)
                if (!CloseSocket(hListenSocket.socket))
                    LogPrintf("CloseSocket(hListenSocket) failed with error %s\n", NetworkErrorString(WSAGetLastError()));

        // clean up some globals (to help leak detection)
        BOOST_FOREACH(CNode *pnode, vNodes)
            delete pnode;
        BOOST_FOREACH(CNode *pnode, vNodesDisconnected)
            delete pnode;
        vNodes.clear();
        vNodesDisconnected.clear();
        vhListenSocket.clear();
        delete semOutbound;
        semOutbound = NULL;
        delete pnodeLocalHost;
        pnodeLocalHost = NULL;

#ifdef _WIN32
        // Shutdown Windows Sockets
        WSACleanup();
#endif
    }
}
instance_of_cnetcleanup;

void RelayTransaction(const CTransaction& tx)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.reserve(10000);
    ss << tx;
    RelayTransaction(tx, ss);
}

void RelayTransaction(const CTransaction& tx, const CDataStream& ss)
{
    CInv inv(MSG_TX, tx.GetHash());
    {
        LOCK(cs_mapRelay);
        // Expire old relay messages
        while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime())
        {
            mapRelay.erase(vRelayExpiration.front().second);
            vRelayExpiration.pop_front();
        }

        // Save original serialized message so newer versions are preserved
        mapRelay.insert(std::make_pair(inv, ss));
        vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
    }
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
    {
        if(!pnode->fRelayTxes)
            continue;
        LOCK(pnode->cs_filter);
        if (pnode->pfilter)
        {
            if (pnode->pfilter->IsRelevantAndUpdate(tx))
                pnode->PushInventory(inv);
        } else pnode->PushInventory(inv);
    }
}

void CNode::RecordBytesRecv(uint64_t bytes)
{
    LOCK(cs_totalBytesRecv);
    nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes)
{
    LOCK(cs_totalBytesSent);
    nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv()
{
    LOCK(cs_totalBytesRecv);
    return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent()
{
    LOCK(cs_totalBytesSent);
    return nTotalBytesSent;
}

void CNode::Fuzz(int nChance)
{
    if (!fSuccessfullyConnected) return; // Don't fuzz initial handshake
    if (GetRand(nChance) != 0) return; // Fuzz 1 of every nChance messages

    switch (GetRand(3))
    {
    case 0:
        // xor a random byte with a random value:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend[pos] ^= (unsigned char)(GetRand(256));
        }
        break;
    case 1:
        // delete a random byte:
        if (!ssSend.empty()) {
            CDataStream::size_type pos = GetRand(ssSend.size());
            ssSend.erase(ssSend.begin()+pos);
        }
        break;
    case 2:
        // insert a random byte at a random position
        {
            CDataStream::size_type pos = GetRand(ssSend.size());
            char ch = (char)GetRand(256);
            ssSend.insert(ssSend.begin()+pos, ch);
        }
        break;
    }
    // Chance of more than one change half the time:
    // (more changes exponentially less likely):
    Fuzz(2);
}

//
// CAddrDB
//

CAddrDB::CAddrDB()
{
    pathAddr = GetDataDir() / "peers.dat";
}

bool CAddrDB::Write(const CAddrMan& addr)
{
    // Generate random temporary filename
    unsigned short randv = 0;
    GetRandBytes((unsigned char*)&randv, sizeof(randv));
    std::string tmpfn = strprintf("peers.dat.%04x", randv);

    // serialize addresses, checksum data up to that point, then append csum
    CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
    ssPeers << FLATDATA(Params().MessageStart());
    ssPeers << addr;
    uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
    ssPeers << hash;

    // open temp output file, and associate with CAutoFile
    boost::filesystem::path pathTmp = GetDataDir() / tmpfn;
    FILE *file = fopen(pathTmp.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s: Failed to open file %s", __func__, pathTmp.string());

    // Write and commit header, data
    try {
        fileout << ssPeers;
    }
    catch (const std::exception& e) {
        return error("%s: Serialize or I/O error - %s", __func__, e.what());
    }
    FileCommit(fileout.Get());
    fileout.fclose();

    // replace existing peers.dat, if any, with new peers.dat.XXXX
    if (!RenameOver(pathTmp, pathAddr))
        return error("%s: Rename-into-place failed", __func__);

    return true;
}

bool CAddrDB::Read(CAddrMan& addr)
{
    // open input file, and associate with CAutoFile
    FILE *file = fopen(pathAddr.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s: Failed to open file %s", __func__, pathAddr.string());

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathAddr);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char *)&vchData[0], dataSize);
        filein >> hashIn;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }
    filein.fclose();

    CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
    if (hashIn != hashTmp)
        return error("%s: Checksum mismatch, data corrupted", __func__);

    unsigned char pchMsgTmp[4];
    try {
        // de-serialize file header (network specific magic number) and ..
        ssPeers >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
            return error("%s: Invalid network magic number", __func__);

        // de-serialize address data into one CAddrMan object
        ssPeers >> addr;
    }
    catch (const std::exception& e) {
        return error("%s: Deserialize or I/O error - %s", __func__, e.what());
    }

    return true;
}

unsigned int ReceiveFloodSize() { return 1000*GetArg("-maxreceivebuffer", 5*1000); }
unsigned int SendBufferSize() { return 1000*GetArg("-maxsendbuffer", 1*1000); }

CNode::CNode(SOCKET hSocketIn, const CAddress& addrIn, const std::string& addrNameIn, bool fInboundIn, SSL *sslIn) :
    ssSend(SER_NETWORK, INIT_PROTO_VERSION),
    addrKnown(5000, 0.001),
    setInventoryKnown(SendBufferSize() / 1000)
{
    ssl = sslIn;
    nServices = 0;
    hSocket = hSocketIn;
    nRecvVersion = INIT_PROTO_VERSION;
    nLastSend = 0;
    nLastRecv = 0;
    nSendBytes = 0;
    nRecvBytes = 0;
    nTimeConnected = GetTime();
    nTimeOffset = 0;
    addr = addrIn;
    addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
    nVersion = 0;
    strSubVer = "";
    fWhitelisted = false;
    fOneShot = false;
    fClient = false; // set by version message
    fInbound = fInboundIn;
    fNetworkNode = false;
    fSuccessfullyConnected = false;
    fDisconnect = false;
    nRefCount = 0;
    nSendSize = 0;
    nSendOffset = 0;
    hashContinue = uint256();
    nStartingHeight = -1;
    fGetAddr = false;
    fRelayTxes = false;
    fSentAddr = false;
    pfilter = new CBloomFilter();
    nPingNonceSent = 0;
    nPingUsecStart = 0;
    nPingUsecTime = 0;
    fPingQueued = false;
    nPingRetry = 0;
    nMinPingUsecTime = std::numeric_limits<int64_t>::max();

    {
        LOCK(cs_nLastNodeId);
        id = nLastNodeId++;
    }

    if (fLogIPs)
        LogPrint("net", "Added connection to %s peer=%d\n", addrName, id);
    else
        LogPrint("net", "Added connection peer=%d\n", id);

    // Be shy and don't send version until we hear
    if (hSocket != INVALID_SOCKET && !fInbound)
        PushVersion();

    GetNodeSignals().InitializeNode(GetId(), this);

    m_serializer = std::unique_ptr<V1TransportSerializer>(new V1TransportSerializer());
}

bool CNode::GetTlsFallbackNonTls()
{
    if (tlsFallbackNonTls == eTlsOption::FALLBACK_UNSET)
    {
        // one time only setting of static class attribute
        if ( GetBoolArg("-tlsfallbacknontls", true))
        {
            LogPrint("tls", "%s():%d - Non-TLS connections will be used in case of failure of TLS\n",
                __func__, __LINE__);
            tlsFallbackNonTls = eTlsOption::FALLBACK_TRUE;
        }
        else
        {
            LogPrint("tls", "%s():%d - Non-TLS connections will NOT be used in case of failure of TLS\n",
                __func__, __LINE__);
            tlsFallbackNonTls = eTlsOption::FALLBACK_FALSE;
        }
    }
    return (tlsFallbackNonTls == eTlsOption::FALLBACK_TRUE);
}

bool CNode::GetTlsValidate()
{
    if (tlsValidate == eTlsOption::FALLBACK_UNSET)
    {
        // one time only setting of static class attribute
        if ( GetBoolArg("-tlsvalidate", false))
        {
            LogPrint("tls", "%s():%d - TLS certificates will be validated\n",
                __func__, __LINE__);
            tlsValidate = eTlsOption::FALLBACK_TRUE;
        }
        else
        {
            LogPrint("tls", "%s():%d - TLS certificates will NOT be validated\n",
                __func__, __LINE__);
            tlsValidate = eTlsOption::FALLBACK_FALSE;
        }
    }
    return (tlsValidate == eTlsOption::FALLBACK_TRUE);
}

CNode::~CNode()
{
    // No need to make a lock on cs_hSocket, because before deletion CNode object is removed from the vNodes vector, so any other thread hasn't access to it.
    // Removal is synchronized with read and write routines, so all of them will be completed to this moment.

    if (hSocket != INVALID_SOCKET)
    {
        if (ssl)
        {
            unsigned long err_code = 0;
            tlsmanager.waitFor(SSL_SHUTDOWN, hSocket, ssl, (DEFAULT_CONNECT_TIMEOUT / 1000), err_code);

            SSL_free(ssl);
            ssl = NULL;
        }

        CloseSocket(hSocket);
    }

    if (pfilter)
        delete pfilter;

    GetNodeSignals().FinalizeNode(GetId());
}

void CNode::AskFor(const CInv& inv)
{
    if (mapAskFor.size() > MAPASKFOR_MAX_SZ || setAskFor.size() > SETASKFOR_MAX_SZ)
        return;
    // a peer may not have multiple non-responded queue positions for a single inv item
    if (!setAskFor.insert(inv.hash).second)
        return;

    // We're using mapAskFor as a priority queue,
    // the key is the earliest time the request can be sent
    int64_t nRequestTime;
    limitedmap<CInv, int64_t>::const_iterator it = mapAlreadyAskedFor.find(inv);
    if (it != mapAlreadyAskedFor.end())
        nRequestTime = it->second;
    else
        nRequestTime = 0;
    LogPrint("net", "askfor %s  %d (%s) peer=%d\n", inv.ToString(), nRequestTime, DateTimeStrFormat("%H:%M:%S", nRequestTime/1000000), id);

    // Make sure not to reuse time indexes to keep things in the same order
    int64_t nNow = GetTimeMicros() - 1000000;
    static int64_t nLastTime;
    ++nLastTime;
    nNow = std::max(nNow, nLastTime);
    nLastTime = nNow;

    // Each retry is 2 minutes after the last
    nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
    if (it != mapAlreadyAskedFor.end())
        mapAlreadyAskedFor.update(it, nRequestTime);
    else
        mapAlreadyAskedFor.insert(std::make_pair(inv, nRequestTime));
    mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

void CNode::BeginMessage(const char* pszCommand) ACQUIRE(cs_vSend)
{
    ENTER_CRITICAL_SECTION(cs_vSend);
    assert(ssSend.size() == 0);
    ssSend << CMessageHeader(Params().MessageStart(), pszCommand, 0);
    LogPrint("net", "sending: %s ", SanitizeString(pszCommand));
}

void CNode::AbortMessage() RELEASE(cs_vSend)
{
    ssSend.clear();

    LEAVE_CRITICAL_SECTION(cs_vSend);

    LogPrint("net", "(aborted)\n");
}

void CNode::EndMessage() RELEASE(cs_vSend)
{
    // The -*messagestest options are intentionally not documented in the help message,
    // since they are only used during development to debug the networking code and are
    // not intended for end-users.
    if (mapArgs.count("-dropmessagestest") && GetRand(GetArg("-dropmessagestest", 2)) == 0)
    {
        LogPrint("net", "dropmessages DROPPING SEND MESSAGE\n");
        AbortMessage();
        return;
    }
    if (mapArgs.count("-fuzzmessagestest"))
        Fuzz(GetArg("-fuzzmessagestest", 10));

    if (ssSend.size() == 0)
    {
        LEAVE_CRITICAL_SECTION(cs_vSend);
        return;
    }
    // Set the size
    unsigned int nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
    WriteLE32((uint8_t*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], nSize);

    // Set the checksum
    uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
    unsigned int nChecksum = 0;
    memcpy(&nChecksum, &hash, sizeof(nChecksum));
    assert(ssSend.size () >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
    memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

    LogPrint("net", "(%d bytes) peer=%d\n", nSize, id);

    std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
    ssSend.GetAndClear(*it);
    nSendSize += (*it).size();

    // If write queue empty, attempt "optimistic write"
    if (it == vSendMsg.begin())
        SocketSendData(this);

    LEAVE_CRITICAL_SECTION(cs_vSend);
}

void CNode::PushAddrMessage(CSerializedNetMsg&& msg)
{
    size_t nMessageSize = msg.data.size();
    LogPrint("net", "sending %s (%d bytes) peer=%d\n",  SanitizeString(msg.m_type), nMessageSize, GetId());

    // make sure we use the appropriate network transport format
    std::vector<unsigned char> serializedHeader;
    m_serializer->prepareForTransport(msg, serializedHeader);
    size_t nTotalSize = nMessageSize + serializedHeader.size();
    {
        LOCK(cs_vSend);
        //log total amount of bytes per message type
        mapSendBytesPerMsgCmd[msg.m_type] += nTotalSize;
        nSendSize += nTotalSize;

        // if (nSendSize > nSendBufferMaxSize) fPauseSend = true;

        //Add Header
        std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
        CSerializeData &d = *it;
        d.insert(d.end(), serializedHeader.begin(), serializedHeader.end());

        //Add Message
        if (nMessageSize) {
          d.insert(d.end(), msg.data.begin(), msg.data.end());
        }

        if (it == vSendMsg.begin())
            SocketSendData(this);
    }
}

size_t GetNodeCount(NumConnections flags)
{
    LOCK(cs_vNodes);
    if (flags == CONNECTIONS_ALL) // Shortcut if we want total
        return vNodes.size();

    int nNum = 0;
    for (const auto& pnode : vNodes) {
        if (flags & (pnode->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT)) {
            nNum++;
        }
    }

    return nNum;
}

void SetNetworkActive(bool active)
{
    LogPrint("net", "SetNetworkActive: %s\n", active);

    if (fNetworkActive == active) {
        return;
    }

    fNetworkActive = active;

    if (!fNetworkActive) {
        LOCK(cs_vNodes);
        // Close sockets to all nodes
        for (CNode* pnode : vNodes) {
            pnode->CloseSocketDisconnect();
        }
    }

    uiInterface.NotifyNetworkActiveChanged(fNetworkActive);
}

void CopyNodeStats(std::vector<CNodeStats>& vstats)
{
    vstats.clear();

    LOCK(cs_vNodes);
    vstats.reserve(vNodes.size());
    BOOST_FOREACH(CNode* pnode, vNodes) {
        CNodeStats stats;
        pnode->copyStats(stats, addrman.m_asmap);
        vstats.push_back(stats);
    }
}
