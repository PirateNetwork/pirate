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

#ifndef BITCOIN_NET_H
#define BITCOIN_NET_H

#include "addrdb.h"
#include "bloom.h"
#include "compat.h"
#include "hash.h"
#include "i2p.h"
#include "limitedmap.h"
#include "mruset.h"
#include "netbase.h"
#include "protocol.h"
#include "random.h"
#include "streams.h"
#include "sync.h"
#include "uint256.h"
#include "util/strencodings.h"
#include "util.h"

#include <deque>
#include <map>
#include <set>
#include <stdint.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include <boost/filesystem/path.hpp>
#include <boost/foreach.hpp>
#include <boost/signals2/signal.hpp>

// Enable OpenSSL Support for Zen
#include <openssl/bio.h>
#include <openssl/ssl.h>

class CAddrMan;
class CBlockIndex;
class CScheduler;
class CNode;

namespace boost {
    class thread_group;
} // namespace boost

/** Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static const int PING_INTERVAL = 2 * 60;
/** Retry Time between pings automatically sent out for latency probing and keepalive (in seconds). */
static const int MAX_PING_RETRY = 20;
/** Time after which to disconnect, after waiting for a ping response (or inactivity). */
static const int TIMEOUT_INTERVAL = 20 * 60;
/** The maximum number of entries in an 'inv' protocol message */
static const unsigned int MAX_INV_SZ = 5000;
/** The maximum number of new addresses to accumulate before announcing. */
static const unsigned int MAX_ADDR_TO_SEND = 1000;
/** The maximum rate of address records we're willing to process on average. Can be bypassed using
 *  the NetPermissionFlags::Addr permission. */
static constexpr double MAX_ADDR_RATE_PER_SECOND{0.1};
/** The soft limit of the address processing token bucket (the regular MAX_ADDR_RATE_PER_SECOND
 *  based increments won't go above this, but the MAX_ADDR_TO_SEND increment following GETADDR
 *  is exempt from this limit. */
static constexpr size_t MAX_ADDR_PROCESSING_TOKEN_BUCKET{MAX_ADDR_TO_SEND};
/** Maximum length of incoming protocol messages (no message over 2 MiB is currently acceptable). */
static const unsigned int MAX_PROTOCOL_MESSAGE_LENGTH = (_MAX_BLOCK_SIZE + 24); // 24 is msgheader size
/** Maximum length of strSubVer in `version` message */
static const unsigned int MAX_SUBVERSION_LENGTH = 256;
/** -listen default */
static const bool DEFAULT_LISTEN = true;
/** The maximum number of entries in mapAskFor */
static const size_t MAPASKFOR_MAX_SZ = MAX_INV_SZ;
/** The maximum number of entries in setAskFor (larger due to getdata latency)*/
static const size_t SETASKFOR_MAX_SZ = 2 * MAX_INV_SZ;
/** The maximum number of peer connections to maintain. */
static const unsigned int DEFAULT_MAX_PEER_CONNECTIONS = 384;
/** The default maximum number of inbound connections accepted from a single source IP (eclipse-attack mitigation). Does not apply to peers accepted via the dedicated Tor listener - see ShouldRejectForInboundFromIPCap. */
static const int DEFAULT_MAX_INBOUND_FROMIP = 2;
/** Whether locally-originated transactions are relayed only to outbound Tor/I2P peers by default, to avoid a clearnet IP address being the first observed source of a transaction. Falls back to relaying via all peers if no such peer is currently connected - see ShouldRestrictRelayToPrivacyPeers. */
static const bool DEFAULT_PRIVATE_TX_RELAY = true;
/** Whether -privatetxrelay is allowed to fall back to relaying via all peers when no privacy peer is currently connected. If disabled, a locally-originated transaction simply isn't relayed this round when no privacy peer is available (it will still be retried automatically once one connects, or via the wallet's periodic rebroadcast) rather than ever touching a clearnet peer. */
static const bool DEFAULT_PRIVATE_TX_RELAY_FALLBACK = true;
/** Whether to maintain a pool of short-lived, burn-after-use I2P identities (see g_i2p_relay_pool) used only for relaying locally-originated transactions, separate from the node's normal permanent I2P identity (m_i2p_sam_session), to prevent a peer from linking multiple transactions to one persistent I2P destination over time. When disabled, locally-originated relay falls back to using the normal I2P identity like any other privacy peer. */
static const bool DEFAULT_I2P_IDENTITY_ROTATION = true;
/** Minimum number of warmed, ready-with-peers I2P relay-pool identities to keep in reserve - see g_i2p_relay_pool. */
static const int DEFAULT_I2P_POOL_MIN_RESERVE = 12;
/** Maximum number of concurrent I2P relay-pool identities. */
static const int DEFAULT_I2P_POOL_MAX_SIZE = 16;
/** Seconds a newly-generated I2P relay-pool identity is given to warm up (build tunnels/publish its leaseset) before it's eligible to transition WARMING -> READY. */
static const int64_t I2P_POOL_WARMUP_PERIOD = 3 * 60;
/** Minimum number of connected peers a WARMING I2P relay-pool identity must have before it can transition to READY - tunnels being built isn't enough on its own, it needs someone to actually relay to. */
static const int I2P_POOL_MIN_PEERS_FOR_READY = 2;
/** Seconds an I2P relay-pool identity is kept alive after being selected for a locally-originated transaction relay, before being retired - long enough for the peer's GETDATA round-trip to complete, comfortably inside the mapRelay cache TTL (15 minutes) so nothing is lost by keeping this shorter. */
static const int64_t I2P_POOL_DRAIN_PERIOD = 10 * 60;
/** Minimum and maximum lifespan, in seconds, for an I2P relay-pool identity's backstop expiry (enforced even if the identity is never used for a relay). The 24h span between them is sized to fit DEFAULT_I2P_POOL_MAX_SIZE identities' backstop expiries at least I2P_POOL_MIN_STAGGER apart without any two being forced to collide at the window's edge (span / stagger + 1 must stay >= the largest pool size expected to run this - see ComputeI2PPoolStaggeredExpiry). */
static const int64_t I2P_POOL_MIN_LIFETIME = 6 * 60 * 60;
static const int64_t I2P_POOL_MAX_LIFETIME = 30 * 60 * 60;
/** Minimum separation, in seconds, enforced between any two I2P relay-pool identities' backstop expiry times, so the pool never has all (or, for a single-interface I2P-only node, its only) identities expire close together. */
static const int64_t I2P_POOL_MIN_STAGGER = 60 * 60;
/** How often the I2P relay-pool maintenance scheduler task runs (retirement, warmup transitions, replenishment). */
static const int64_t I2P_POOL_TICK_INTERVAL = 60;
/** The period before a network upgrade activates, where connections to upgrading peers are preferred (in blocks). */
static const int NETWORK_UPGRADE_PEER_PREFERENCE_BLOCK_PERIOD = 24 * 24 * 3;

extern std::atomic<bool> fNetworkActive;

unsigned int ReceiveFloodSize();
unsigned int SendBufferSize();

void AddOneShot(const std::string& strDest);
void AddressCurrentlyConnected(const CService& addr);
CNode* FindNode(const CNetAddr& ip);
CNode* FindNode(const CSubNet& subNet);
CNode* FindNode(const std::string& addrName);
CNode* FindNode(const CService& ip);
//! i2p_pool_idx: when >= 0 and addrConnect is an I2P address, dial through
//! that index into g_i2p_relay_pool instead of the node's normal/permanent
//! I2P identity (m_i2p_sam_session). Ignored for non-I2P addresses.
CNode* ConnectNode(CAddress addrConnect, const char *pszDest = NULL, bool fAddNode = false, int i2p_pool_idx = -1);
//! i2p_pool_idx: see ConnectNode().
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound = NULL, const char *strDest = NULL, bool fOneShot = false, bool fAddNode = false, int i2p_pool_idx = -1);
unsigned short GetListenPort();
bool BindListenPort(const CService &bindAddr, std::string& strError, bool fWhitelisted = false);
void LoadPeers();
void DumpAddresses();
void StartNode(boost::thread_group& threadGroup, CScheduler& scheduler);
bool StopNode();
void SocketSendData(CNode *pnode);
SSL_CTX* create_context(bool server_side);
EVP_PKEY *generate_key();
X509 *generate_x509(EVP_PKEY *pkey);
bool write_to_disk(EVP_PKEY *pkey, X509 *x509);
void configure_context(SSL_CTX *ctx, bool server_side);

// OpenSSL related variables for metrics.cpp
static std::string routingsecrecy;
static std::string cipherdescription;
static std::string securitylevel;
static std::string validationdescription;

void GetBanned(banmap_t &banmap);
void SetBanned(const banmap_t &banmap);

    //!check is the banlist has unwritten changes
bool BannedSetIsDirty();
    //!set the "dirty" flag for the banlist
void SetBannedSetDirty(bool dirty=true);
    //!clean unused entries (if bantime has expired)
void SweepBanned();

void CreateNodeFromAcceptedSocket(SOCKET hSocket,
                                            bool whitelisted,
                                            const CAddress& addr_bind,
                                            const CAddress& addr,
                                            bool fOnionListener = false,
                                            int i2p_pool_idx = -1,
                                            uint32_t i2p_pool_generation = 0);
//! Binds a dedicated, loopback-only listening socket used exclusively as the
//! local forwarding target for Tor's onion-service connections - see net.cpp.
bool BindOnionListenPort(std::string& strError);
//! Port bound by BindOnionListenPort, or 0 if it hasn't been called/succeeded.
unsigned short GetOnionListenPort();
typedef int NodeId;

enum NumConnections {
    CONNECTIONS_NONE = 0,
    CONNECTIONS_IN = (1U << 0),
    CONNECTIONS_OUT = (1U << 1),
    CONNECTIONS_ALL = (CONNECTIONS_IN | CONNECTIONS_OUT),
};

size_t GetNodeCount(NumConnections num);

bool GetNetworkActive();
void SetNetworkActive(bool active);

class CNodeStats;
void CopyNodeStats(std::vector<CNodeStats>& vstats);

struct CSerializedNetMsg
{
    CSerializedNetMsg() = default;
    CSerializedNetMsg(CSerializedNetMsg&&) = default;
    CSerializedNetMsg& operator=(CSerializedNetMsg&&) = default;
    // No copying, only moves.
    CSerializedNetMsg(const CSerializedNetMsg& msg) = delete;
    CSerializedNetMsg& operator=(const CSerializedNetMsg&) = delete;

    std::vector<unsigned char> data;
    std::string m_type;
};

struct CombinerAll
{
    typedef bool result_type;

    template<typename I>
    bool operator()(I first, I last) const
    {
        while (first != last) {
            if (!(*first)) return false;
            ++first;
        }
        return true;
    }
};

// Signals for message handling
struct CNodeSignals
{
    boost::signals2::signal<int ()> GetHeight;
    boost::signals2::signal<bool (CNode*), CombinerAll> ProcessMessages;
    boost::signals2::signal<bool (CNode*, bool), CombinerAll> SendMessages;
    boost::signals2::signal<void (NodeId, const CNode*)> InitializeNode;
    boost::signals2::signal<void (NodeId)> FinalizeNode;
};


CNodeSignals& GetNodeSignals();


enum
{
    LOCAL_NONE,   // unknown
    LOCAL_IF,     // address a local interface listens on
    LOCAL_BIND,   // address explicit bound to
    LOCAL_UPNP,   // unused (was: address reported by UPnP)
    LOCAL_MANUAL, // address explicitly specified (-externalip=)

    LOCAL_MAX
};

//! Whether two addresses share the same underlying IP, ignoring port - see
//! net.cpp for why this must not compare ports.
bool SameNetAddr(const CNetAddr& a, const CNetAddr& b);
//! Whether a new inbound connection should be rejected by the per-source-IP
//! inbound cap - see net.cpp for the dedicated-Tor-listener exemption.
bool ShouldRejectForInboundFromIPCap(int nInboundThisIP, int nMaxInboundFromIP, bool fOnionListener);
//! Whether an existing inbound peer should count toward the per-source-IP
//! cap for a new connection attempt - see net.cpp for the I2P per-identity
//! scoping rationale.
bool ShouldCountTowardInboundFromIPCap(bool fIsI2P, int existingPoolIdx, uint32_t existingPoolGeneration, int newPoolIdx, uint32_t newPoolGeneration);
//! Whether a new inbound connection should be hard-rejected for being a
//! local (loopback), non-Tor connection - see net.cpp.
bool ShouldRejectLocalNonOnionInbound(const CNetAddr& addr, bool fOnionListener, bool fAllowLocalIp);
bool IsPeerAddrLocalGood(CNode *pnode);
void AdvertizeLocal(CNode *pnode);
bool AddLocal(const CService& addr, int nScore = LOCAL_NONE);
bool AddLocal(const CNetAddr& addr, int nScore = LOCAL_NONE);
bool RemoveLocal(const CService& addr);
bool SeenLocal(const CService& addr);
bool IsLocal(const CService& addr);
bool GetLocal(CService &addr, const CNetAddr *paddrPeer = NULL);
/**
 * Mark a network as reachable or unreachable (no automatic connects to it)
 * @note Networks are reachable by default
 */
void SetReachable(enum Network net, bool reachable);
/** @returns true if the network is reachable, false otherwise */
bool IsReachable(enum Network net);
/** @returns true if the address is in a reachable network, false otherwise */
bool IsReachable(const CNetAddr& addr);
CAddress GetLocalAddress(const CNetAddr *paddrPeer = NULL);

//! Minimum number of outbound connections ThreadOpenConnections tries to keep
//! on every currently-reachable network (IPv4/IPv6/Tor/I2P/CJDNS), so a
//! multi-interface node doesn't end up with all its outbound slots on
//! whichever network happens to be best represented in addrman.
static const int MIN_OUTBOUND_PER_REACHABLE_NETWORK = 2;

//! How many candidates ThreadOpenConnections will search through while
//! restricting itself to under-target networks before giving up on the
//! diversity target for this pass and accepting any reachable candidate -
//! prevents starving all connections when an under-represented network
//! genuinely has no usable addresses in addrman yet.
static const int DIVERSITY_TRY_BUDGET = 40;

/**
 * Given the current outbound connection count per network, return the set of
 * currently-reachable networks (IPv4/IPv6/Tor/I2P/CJDNS) that haven't yet
 * reached MIN_OUTBOUND_PER_REACHABLE_NETWORK outbound connections.
 */
std::set<Network> GetUnderTargetReachableNetworks(const std::map<Network, int>& outboundCountByNetwork,
                                                   int nMinPerNetwork = MIN_OUTBOUND_PER_REACHABLE_NETWORK);

/**
 * Whether a connection candidate on `net` should be skipped this attempt in
 * favor of giving under-represented reachable networks a chance to reach
 * their target first. Always false once `underTargetNetworks` is empty (every
 * reachable network already met its target) or `nTries` has exceeded the
 * diversity search budget (give up enforcing rather than find no candidate
 * at all).
 */
bool ShouldSkipForNetworkDiversity(Network net, const std::set<Network>& underTargetNetworks,
                                    int nTries, int nDiversityTryBudget = DIVERSITY_TRY_BUDGET);


extern bool fDiscover;
extern bool fListen;
extern uint64_t nLocalServices;
extern uint64_t nLocalHostNonce;
extern CAddrMan addrman;
/** Maximum number of connections to simultaneously allow (aka connection slots) */
extern int nMaxConnections;
extern bool bOverrideMaxConnections;
/** Maximum number of inbound connections accepted from a single source IP - see DEFAULT_MAX_INBOUND_FROMIP. */
extern int nMaxInboundFromIP;
/** Whether to restrict relay of locally-originated transactions to outbound Tor/I2P peers - see DEFAULT_PRIVATE_TX_RELAY. */
extern bool fPrivateTxRelay;
/** Whether -privatetxrelay may fall back to clearnet peers - see DEFAULT_PRIVATE_TX_RELAY_FALLBACK. */
extern bool fPrivateTxRelayFallback;
/** Whether to maintain the I2P relay pool - see DEFAULT_I2P_IDENTITY_ROTATION. */
extern bool fI2PIdentityRotation;
extern int nI2PPoolMinReserve;
extern int nI2PPoolMaxSize;

extern std::vector<CNode*> vNodes;
extern CCriticalSection cs_vNodes;
extern std::map<CInv, CDataStream> mapRelay;
extern std::deque<std::pair<int64_t, CInv> > vRelayExpiration;
extern CCriticalSection cs_mapRelay;
extern limitedmap<CInv, int64_t> mapAlreadyAskedFor;

extern std::vector<std::string> vAddedNodes;
extern CCriticalSection cs_vAddedNodes;

extern NodeId nLastNodeId;
extern CCriticalSection cs_nLastNodeId;

extern SSL_CTX *tls_ctx_server;
extern SSL_CTX *tls_ctx_client;

extern std::unique_ptr<i2p::sam::Session> m_i2p_sam_session;

//! State machine for a single I2P relay-pool identity slot - see
//! g_i2p_relay_pool below and net.cpp for the lifecycle this drives.
enum class I2PPoolIdentityState : uint8_t { WARMING, READY, ACTIVE };

//! A single burn-after-use I2P identity used only for relaying our own
//! (locally-originated) transactions - never for general P2P duty, and
//! never the same identity as m_i2p_sam_session (the node's normal,
//! permanent I2P identity). See the "Rotating burn-after-use I2P
//! identities for transaction relay" design: an identity here is generated
//! fresh, given time to warm up and acquire peers, used for at most one
//! locally-originated transaction relay, then retired and replaced.
struct I2PPoolIdentitySlot {
    std::unique_ptr<i2p::sam::Session> session;
    I2PPoolIdentityState state{I2PPoolIdentityState::WARMING};
    //! WARMING -> READY becomes possible once now() >= nWarmupDeadline
    //! (subject also to having enough peers - see HasEnoughPeersForReady).
    int64_t nWarmupDeadline{0};
    //! Backstop retirement time, enforced regardless of state - covers a
    //! slot that warms up but is never selected for use.
    int64_t nHardExpiryTime{0};
    //! Set when the slot transitions to ACTIVE (selected for a locally-
    //! originated transaction relay); the slot is retired once now() is
    //! past this AND state == ACTIVE. Meaningless while WARMING/READY.
    int64_t nDrainDeadline{0};
    //! Number of locally-originated transactions relayed through this
    //! slot since it was last (re)generated. 0 while READY; becomes 1 the
    //! moment the slot is selected (burn-after-use).
    uint32_t nLocalRelayCount{0};
    //! Bumped every time this slot index is retired and regenerated, so a
    //! CNode tagged with a stale (idx, generation) pair from before a
    //! rotation is never confused with the slot's current identity.
    uint32_t nGeneration{0};
    //! Address this slot is currently advertising via AddLocal (if any),
    //! tracked here (rather than re-querying the Session) so retirement
    //! can RemoveLocal() it without needing new Session accessors.
    CService lastAdvertisedAddr;
    //! Number of currently-connected peers (inbound accepted or outbound
    //! dialed) tied to this slot - maintained by the accept/dial paths,
    //! consulted by the WARMING->READY transition and by relay selection.
    int nPeerCount{0};
};

extern std::vector<I2PPoolIdentitySlot> g_i2p_relay_pool;
extern CCriticalSection cs_i2p_relay_pool;

/** Subversion as sent to the P2P network in `version` messages */
extern std::string strSubVersion;

struct LocalServiceInfo {
    int nScore;
    int nPort;
};

extern CCriticalSection cs_mapLocalHost;
extern std::map<CNetAddr, LocalServiceInfo> mapLocalHost;

typedef std::map<std::string, uint64_t> mapMsgCmdSize; //command, total bytes

class CNodeStats
{
public:
    NodeId nodeid;
    uint64_t nServices;
    bool fTLSEstablished;
    bool fTLSVerified;
    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    std::string addrName;
    int nVersion;
    std::string cleanSubVer;
    bool fInbound;
    int nStartingHeight;
    uint64_t nSendBytes;
    uint64_t nRecvBytes;
    bool fWhitelisted;
    bool fAddNode;
    double dPingTime;
    double dPingWait;
    double dMinPing;
    std::string addrLocal;
    std::string addrFromPeer;
    // Whether this inbound connection was accepted on the dedicated
    // Tor-forwarding listener (see BindOnionListenPort in net.cpp) - i.e.
    // is verifiably a Tor onion-service peer, as opposed to just having a
    // loopback addr for some other reason.
    bool m_inbound_onion{false};
    // Index into g_i2p_relay_pool this connection is tied to, or -1 if
    // this isn't an I2P relay-pool connection (either not I2P at all, or
    // I2P via the node's normal/permanent identity rather than the pool).
    int m_i2p_pool_idx{-1};
    uint32_t m_i2p_pool_generation{0};
    uint64_t m_addr_processed{0};
    uint64_t m_addr_rate_limited{0};
    // Network the peer's address belongs to (ipv4, ipv6, onion, i2p, ...)
    std::string m_network;
    // Address of this peer
    CAddress addr;
    // Bind address of our side of the connection
    // CAddress addrBind; // https://github.com/bitcoin/bitcoin/commit/a7e3c2814c8e49197889a4679461be42254e5c51
    uint32_t m_mapped_as;

    /**
     * Whether the peer has signaled support for receiving ADDRv2 (BIP155)
     * messages, implying a preference to receive ADDRv2 instead of ADDR ones.
     */
    bool m_wants_addrv2;
};




class CNetMessage {
public:
    bool in_data;                   // parsing header (false) or data (true)

    CDataStream hdrbuf;             // partially received header
    CMessageHeader hdr;             // complete header
    unsigned int nHdrPos;

    CDataStream vRecv;              // received message data
    unsigned int nDataPos;

    int64_t nTime;                  // time (in microseconds) of message receipt.

    CNetMessage(const CMessageHeader::MessageStartChars& pchMessageStartIn, int nTypeIn, int nVersionIn) : hdrbuf(nTypeIn, nVersionIn), hdr(pchMessageStartIn), vRecv(nTypeIn, nVersionIn) {
        hdrbuf.resize(24);
        in_data = false;
        nHdrPos = 0;
        nDataPos = 0;
        nTime = 0;
    }

    bool complete() const
    {
        if (!in_data)
            return false;
        return (hdr.nMessageSize == nDataPos);
    }

    void SetVersion(int nVersionIn)
    {
        hdrbuf.SetVersion(nVersionIn);
        vRecv.SetVersion(nVersionIn);
    }

    int readHeader(const char *pch, unsigned int nBytes);
    int readData(const char *pch, unsigned int nBytes);
};


/** The TransportSerializer prepares messages for the network transport
 */
class TransportSerializer {
public:
    // prepare message for transport (header construction, error-correction computation, payload encryption, etc.)
    virtual void prepareForTransport(CSerializedNetMsg& msg, std::vector<unsigned char>& header) = 0;
    virtual ~TransportSerializer() {}
};

class V1TransportSerializer  : public TransportSerializer {
public:
    void prepareForTransport(CSerializedNetMsg& msg, std::vector<unsigned char>& header) override;
};


/** Information about a peer */
class CNode
{
public:
    // OpenSSL
    SSL *ssl;

    //Message Transport Serializer
    std::unique_ptr<TransportSerializer> m_serializer;

    // socket
    uint64_t nServices;
    SOCKET hSocket;
    CCriticalSection cs_hSocket;
    CDataStream ssSend;
    size_t nSendSize; // total size of all vSendMsg entries
    size_t nSendOffset; // offset inside the first vSendMsg already sent
    uint64_t nSendBytes;
    std::deque<CSerializeData> vSendMsg;
    CCriticalSection cs_vSend;

    std::deque<CInv> vRecvGetData;
    std::deque<CNetMessage> vRecvMsg;
    CCriticalSection cs_vRecvMsg;
    uint64_t nRecvBytes;
    int nRecvVersion;

    int64_t nLastSend;
    int64_t nLastRecv;
    int64_t nTimeConnected;
    int64_t nTimeOffset;
    uint32_t prevtimes[16];
    // Address of this peer
    CAddress addr;
    // Bind address of our side of the connection
    // const CAddress addrBind; // https://github.com/bitcoin/bitcoin/commit/a7e3c2814c8e49197889a4679461be42254e5c51
    std::string addrName;
    CService addrLocal;
    // Peer's self-reported address, as sent in its VERSION message's
    // addrFrom field. Unauthenticated (self-reported) - never used for
    // addrman/trust decisions, only for display/debugging.
    CService addrFromPeer;
    int nVersion;
    int lasthdrsreq,sendhdrsreq;
    /** Timestamps (seconds) of the last GETBLOCKS and GETHEADERS received from this peer,
     *  used to enforce a minimum inter-request interval. */
    int64_t nLastGetBlocksRecv;
    int64_t nLastGetHeadersRecv;
    /** Timestamp of last ALERT relayed to this peer, for rate limiting. */
    int64_t nLastAlertRelayed;
    // strSubVer is whatever byte array we read from the wire. However, this field is intended
    // to be printed out, displayed to humans in various forms and so on. So we sanitize it and
    // store the sanitized version in cleanSubVer. The original should be used when dealing with
    // the network or wire types and the cleaned string used when displayed or logged.
    std::string strSubVer, cleanSubVer;
    bool fWhitelisted; // This peer can bypass DoS banning.
    bool fOneShot;
    bool fAddNode;     // This peer is from -addnode
    bool fClient;
    bool fInbound;
    bool fNetworkNode;
    bool fSuccessfullyConnected;
    bool fDisconnect;
    // We use fRelayTxes for two purposes -
    // a) it allows us to not relay tx invs before receiving the peer's version message
    // b) the peer may tell us in its version message that we should not relay tx invs
    //    until it has initialized its bloom filter.
    bool fRelayTxes;
    bool fSentAddr;
    CSemaphoreGrant grantOutbound;
    CCriticalSection cs_filter;
    CBloomFilter* pfilter;
    int nRefCount;
    NodeId id;

    /**
     * Whether the peer has signaled support for receiving ADDRv2 (BIP155)
     * messages, implying a preference to receive ADDRv2 instead of ADDR ones.
     */
    bool m_wants_addrv2{false};

    /**
     * Whether this inbound connection was accepted on the dedicated
     * Tor-forwarding listener (see BindOnionListenPort in net.cpp), i.e. is
     * genuinely a Tor onion-service peer rather than some other process
     * connecting via loopback. Always false for outbound connections.
     */
    bool m_inbound_onion{false};

    /**
     * Index into g_i2p_relay_pool this connection (inbound or outbound) is
     * tied to, or -1 if this isn't an I2P relay-pool connection - either
     * not I2P at all, or I2P via the node's normal/permanent identity
     * (m_i2p_sam_session) rather than the burn-after-use pool. Peers
     * tagged here are only ever used as relay targets for locally-
     * originated transactions, never for general P2P duty differently
     * than any other peer.
     */
    int m_i2p_pool_idx{-1};
    uint32_t m_i2p_pool_generation{0};

protected:

    // Denial-of-service detection/prevention
    // Key is IP address, value is banned-until-time
    // static std::map<CSubNet, int64_t> setBanned;
    // static CCriticalSection cs_setBanned;

    // Whitelisted ranges. Any node connecting from these is automatically
    // whitelisted (as well as those connecting to whitelisted binds).
    static std::vector<CSubNet> vWhitelistedRange;
    static CCriticalSection cs_vWhitelistedRange;

    // Basic fuzz-testing
    void Fuzz(int nChance); // modifies ssSend

    enum class eTlsOption {
        FALLBACK_UNSET = 0,
        FALLBACK_FALSE = 1,
        FALLBACK_TRUE = 2
    };
    static eTlsOption tlsFallbackNonTls;
    static eTlsOption tlsValidate;

public:
    uint256 hashContinue;
    int nStartingHeight;

    // flood relay
    std::vector<CAddress> vAddrToSend;
    CRollingBloomFilter addrKnown;
    bool fGetAddr;
    std::set<uint256> setKnown;

    /** Number of addr messages that can be processed from this peer. Start at 1 to
     *  permit self-announcement. */
    double m_addr_token_bucket{1.0};
    /** When m_addr_token_bucket was last updated */
    int64_t m_addr_token_timestamp{GetTimeMicros()};
    /** Total number of addresses that were dropped due to rate limiting. */
    std::atomic<uint64_t> m_addr_rate_limited{0};
    /** Total number of addresses that were processed (excludes rate limited ones). */
    std::atomic<uint64_t> m_addr_processed{0};

    // inventory based relay
    mruset<CInv> setInventoryKnown;
    std::vector<CInv> vInventoryToSend;
    CCriticalSection cs_inventory;
    std::set<uint256> setAskFor;
    std::multimap<int64_t, CInv> mapAskFor;

    // Ping time measurement:
    // The pong reply we're expecting, or 0 if no pong expected.
    uint64_t nPingNonceSent;
    // Time (in usec) the last ping was sent, or 0 if no ping was ever sent.
    int64_t nPingUsecStart;
    // Last measured round-trip time.
    int64_t nPingUsecTime;
    // Best measured round-trip time.
    int64_t nMinPingUsecTime;
    // Whether a ping is requested.
    bool fPingQueued;
    // Times has ping been retried
    int64_t nPingRetry;

    CNode(SOCKET hSocketIn, const CAddress &addrIn, const std::string &addrNameIn = "", bool fInboundIn = false, SSL *sslIn = NULL);
    ~CNode();

private:
    // Network usage totals
    static CCriticalSection cs_totalBytesRecv;
    static CCriticalSection cs_totalBytesSent;
    static uint64_t nTotalBytesRecv;
    static uint64_t nTotalBytesSent;

    CNode(const CNode&);
    void operator=(const CNode&);

    mapMsgCmdSize mapSendBytesPerMsgCmd GUARDED_BY(cs_vSend);

public:

    NodeId GetId() const {
      return id;
    }

    int GetRefCount()
    {
        assert(nRefCount >= 0);
        return nRefCount;
    }

    // requires LOCK(cs_vRecvMsg)
    unsigned int GetTotalRecvSize()
    {
        unsigned int total = 0;
        BOOST_FOREACH(const CNetMessage &msg, vRecvMsg)
            total += msg.vRecv.size() + 24;
        return total;
    }

    // requires LOCK(cs_vRecvMsg)
    bool ReceiveMsgBytes(const char *pch, unsigned int nBytes);

    // requires LOCK(cs_vRecvMsg)
    void SetRecvVersion(int nVersionIn)
    {
        nRecvVersion = nVersionIn;
        BOOST_FOREACH(CNetMessage &msg, vRecvMsg)
            msg.SetVersion(nVersionIn);
    }

    CNode* AddRef()
    {
        nRefCount++;
        return this;
    }

    void Release()
    {
        nRefCount--;
    }



    void AddAddressKnown(const CAddress& _addr)
    {
        addrKnown.insert(_addr.GetKey());
    }

    void PushAddress(const CAddress& _addr)
    {
        // Whether the peer supports the address in `_addr`. For example,
        // nodes that do not implement BIP155 cannot receive Tor v3 addresses
        // because they require ADDRv2 (BIP155) encoding.
        const bool addr_format_supported = m_wants_addrv2 || _addr.IsAddrV1Compatible();

        // Known checking here is only to save space from duplicates.
        // SendMessages will filter it again for knowns that were added
        // after addresses were pushed.
        if (_addr.IsValid() && !addrKnown.contains(_addr.GetKey()) && addr_format_supported) {
            if (vAddrToSend.size() >= MAX_ADDR_TO_SEND) {
                vAddrToSend[insecure_rand() % vAddrToSend.size()] = _addr;
            } else {
                vAddrToSend.push_back(_addr);
            }
        }
    }


    void AddInventoryKnown(const CInv& inv)
    {
        {
            LOCK(cs_inventory);
            setInventoryKnown.insert(inv);
        }
    }

    void PushInventory(const CInv& inv)
    {
        {
            LOCK(cs_inventory);
            if (!setInventoryKnown.count(inv))
                vInventoryToSend.push_back(inv);
        }
    }

    void AskFor(const CInv& inv);

    // TODO: Document the postcondition of this function.  Is cs_vSend locked?
    void BeginMessage(const char* pszCommand) ACQUIRE(cs_vSend);

    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void AbortMessage() RELEASE(cs_vSend);

    // TODO: Document the precondition of this function.  Is cs_vSend locked?
    void EndMessage() RELEASE(cs_vSend);

    void PushAddrMessage(CSerializedNetMsg&& msg);

    void PushVersion();


    void PushMessage(const char* pszCommand)
    {
        //fprintf(stderr,"push.(%s)\n",pszCommand);
        try
        {
            BeginMessage(pszCommand);
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1>
    void PushMessage(const char* pszCommand, const T1& a1)
    {
        //fprintf(stderr,"push.(%s)\n",pszCommand);
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    template<typename T1, typename T2, typename T3, typename T4, typename T5, typename T6, typename T7, typename T8, typename T9>
    void PushMessage(const char* pszCommand, const T1& a1, const T2& a2, const T3& a3, const T4& a4, const T5& a5, const T6& a6, const T7& a7, const T8& a8, const T9& a9)
    {
        try
        {
            BeginMessage(pszCommand);
            ssSend << a1 << a2 << a3 << a4 << a5 << a6 << a7 << a8 << a9;
            EndMessage();
        }
        catch (...)
        {
            AbortMessage();
            throw;
        }
    }

    void CloseSocketDisconnect(bool sendShutDownSSL = true);

    // Denial-of-service detection/prevention
    // The idea is to detect peers that are behaving
    // badly and disconnect/ban them, but do it in a
    // one-coding-mistake-won't-shatter-the-entire-network
    // way.
    // IMPORTANT:  There should be nothing I can give a
    // node that it will forward on that will make that
    // node's peers drop it. If there is, an attacker
    // can isolate a node and/or try to split the network.
    // Dropping a node for sending stuff that is invalid
    // now but might be valid in a later version is also
    // dangerous, because it can cause a network split
    // between nodes running old code and nodes running
    // new code.
    static void ClearBanned(); // needed for unit testing
    static bool IsBanned(CNetAddr ip);
    static bool IsBanned(CSubNet subnet);
    static void Ban(const CNetAddr &ip, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static void Ban(const CSubNet &subNet, const BanReason& reason, int64_t bantimeoffset = 0, bool sinceUnixEpoch = false);
    static bool Unban(const CNetAddr &ip);
    static bool Unban(const CSubNet &ip);
    static void GetBanned(std::map<CSubNet, int64_t> &banmap);

    void copyStats(CNodeStats &stats, const std::vector<bool> &m_asmap);

    static bool IsWhitelistedRange(const CNetAddr &ip);
    static void AddWhitelistedRange(const CSubNet &subnet);

    // Network stats
    static void RecordBytesRecv(uint64_t bytes);
    static void RecordBytesSent(uint64_t bytes);

    static uint64_t GetTotalBytesRecv();
    static uint64_t GetTotalBytesSent();

    // returns the value of the tlsfallbacknontls and tlsvalidate flags set at zend startup (see init.cpp)
    static bool GetTlsFallbackNonTls();
    static bool GetTlsValidate();
};

// Addnode connection management functions
bool HasAvailableAddNodeSlots();
int GetAddNodeConnectionCount();
bool IsAddNodeAddress(const CService& addr);

class CTransaction;
//! fLocalOrigin: true if this transaction was created by this node (wallet
//! send, sendrawtransaction, a CC module, etc.) rather than received from a
//! peer. Locally-originated transactions are, by default, relayed only to
//! outbound Tor/I2P peers - see ShouldRestrictRelayToPrivacyPeers. Leave
//! false (the default) when relaying a transaction that arrived from a
//! peer, so it continues to propagate to all peers as before.
void RelayTransaction(const CTransaction& tx, bool fLocalOrigin = false);
void RelayTransaction(const CTransaction& tx, const CDataStream& ss, bool fLocalOrigin = false);
//! Whether a connection counts as a "privacy" peer - i.e. one we can relay
//! our own transactions to without exposing our clearnet IP address as the
//! observed source. Tor and I2P work differently here: an inbound Tor
//! connection always arrives loopback-forwarded from the local Tor daemon
//! (see ShouldRejectForInboundFromIPCap), so its addr is never a real,
//! checkable .onion address - only outbound Tor peers qualify. I2P inbound
//! connections, by contrast, carry the real remote I2P address (via SAM's
//! STREAM ACCEPT), so both inbound and outbound I2P peers qualify.
bool IsPrivacyPeer(bool fInbound, const CNetAddr& addr);
//! Number of currently-connected privacy peers - see IsPrivacyPeer.
int GetPrivacyPeerCount();
//! Whether a relay of a locally-originated transaction should be
//! restricted to privacy peers only. False whenever the transaction isn't
//! locally-originated or the feature is disabled. True whenever there's at
//! least one privacy peer connected. When there are currently zero privacy
//! peers: true (still restrict - i.e. relay to nobody this round) unless
//! fAllowFallback is set, in which case false, so the caller falls back to
//! relaying via all peers rather than never propagating the transaction at
//! all. Either way, an unrelayed transaction isn't lost - it's retried
//! automatically once a privacy peer connects, or via the wallet's
//! periodic rebroadcast.
bool ShouldRestrictRelayToPrivacyPeers(bool fLocalOrigin, bool fPrivateTxRelayEnabled, int nPrivacyPeers, bool fAllowFallback);

//! Compute a new backstop-expiry timestamp for an I2P relay-pool identity
//! being (re)generated at nNow, drawn from [nNow + I2P_POOL_MIN_LIFETIME,
//! nNow + I2P_POOL_MAX_LIFETIME] using nRandSource, then pushed out if
//! necessary so it lands at least I2P_POOL_MIN_STAGGER seconds from every
//! entry in vOtherExpiries (the current expiry of every other live pool
//! slot) - actively enforced, not left to chance, so the pool can never
//! have multiple identities expire close together. If satisfying the
//! stagger margin would push the result outside the lifetime window, the
//! lifetime floor/ceiling takes priority (documented trade-off - see
//! net.cpp). nRandSource is an injected random value (not read internally)
//! so this function is pure and deterministic for testing.
int64_t ComputeI2PPoolStaggeredExpiry(int64_t nNow, const std::vector<int64_t>& vOtherExpiries, int64_t nRandSource);

//! Per-slot info consulted by SelectI2PPoolIdentityForRelay(); mirrors the
//! fields of I2PPoolIdentitySlot that matter for selection, so the
//! selection algorithm can be tested without constructing real slots.
struct I2PPoolSlotSelectionInfo {
    I2PPoolIdentityState state;
    int nPeerCount;
    //! Only meaningful when state == ACTIVE; used to find the
    //! least-recently-activated slot under degradation (smallest deadline
    //! drained soonest, i.e. was activated first).
    int64_t nDrainDeadline;
};

//! Select which I2P relay-pool slot (by index into vSlots) a locally-
//! originated transaction should be relayed through. Prefers a READY slot
//! with at least one peer (round-robin among ties via nRoundRobinCounter);
//! if none is available, degrades to the least-recently-activated ACTIVE
//! slot with at least one peer (reuse rather than starve); returns -1 if
//! the whole pool currently has no usable slot (caller falls through to
//! the existing ShouldRestrictRelayToPrivacyPeers()/fallback logic).
int SelectI2PPoolIdentityForRelay(const std::vector<I2PPoolSlotSelectionInfo>& vSlots, uint32_t nRoundRobinCounter);

//! Whether an I2P relay-pool identity should be retired now: either its
//! hard backstop expiry has passed (regardless of state - covers a slot
//! that warmed up but was never selected), or it's ACTIVE and past its
//! post-selection drain deadline.
bool ShouldRetireI2PPoolIdentity(I2PPoolIdentityState state, int64_t nHardExpiryTime, int64_t nDrainDeadline, int64_t nNow);

//! Whether a WARMING I2P relay-pool identity has satisfied both conditions
//! to become READY: its warmup deadline has passed, and it has
//! accumulated enough peers to actually be useful for a relay.
bool ShouldPromoteI2PPoolIdentityToReady(int64_t nWarmupDeadline, int nPeerCount, int64_t nNow, int nMinPeersForReady);

//! Whether a CNode tagged with (nodePoolIdx, nodePoolGeneration) should be
//! disconnected as part of retiring pool slot rotatingIdx at its current
//! generation nCurrentGeneration. The generation check (beyond just the
//! index match) documents intent and guards against a CNode carrying a
//! stale tag from a slot's previous lifetime being mismatched with its
//! current one.
bool ShouldDisconnectForI2PPoolRotation(int nodePoolIdx, uint32_t nodePoolGeneration, int rotatingIdx, uint32_t nCurrentGeneration);

/** Access to the (IP) address database (peers.dat) */
class CAddrDB
{
private:
    boost::filesystem::path pathAddr;
public:
    CAddrDB();
    bool Write(const CAddrMan& addr);
    bool Read(CAddrMan& addr);
};

#endif // BITCOIN_NET_H
