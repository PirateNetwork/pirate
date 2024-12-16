// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2022 The Zcash developers
// Copyright (c) 2015-2023 The Komodo Platform developers
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

#include "main.h"
#include "sodium.h"

#include "addrman.h"
#include "alert.h"
#include "arith_uint256.h"
#include "importcoin.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "deprecation.h"
#include "init.h"
#include "merkleblock.h"
#include "metrics.h"
#include "notarisationdb.h"
#include "net.h"
#include "netmessagemaker.h"
#include "pow.h"
#include "script/interpreter.h"
#include "txdb.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "undo.h"
#include "util.h"
#include "utilmoneystr.h"
#include "validationinterface.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "wallet/asyncrpcoperation_shieldcoinbase.h"
#include "policy/fees.h"
#include "notaries_staked.h"
#include "komodo_extern_globals.h"
#include "komodo_gateway.h"
#include "komodo.h"
#include "komodo_notary.h"
#include "key_io.h"
#include "komodo_utils.h"
#include "komodo_bitcoind.h"
#include "komodo_interest.h"
#include "rpc/net.h"
#include "cc/CCinclude.h"

#include <cstring>
#include <algorithm>
#include <atomic>
#include <sstream>
#include <map>
#include <unordered_map>
#include <vector>
#include <random>

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/math/distributions/poisson.hpp>
#include <boost/thread.hpp>
#include <boost/static_assert.hpp>

using namespace std;

#if defined(NDEBUG)
# error "Zcash cannot be compiled without assertions."
#endif

#include "librustzcash.h"

/**
 * Global state
 */

#define TMPFILE_START 100000000
CCriticalSection cs_main;
int32_t KOMODO_NEWBLOCKS;

BlockMap mapBlockIndex;
CChain chainActive;
CBlockIndex *pindexBestHeader = NULL;
static int64_t nTimeBestReceived = 0;
CWaitableCriticalSection csBestBlock;
CConditionVariable cvBlockChange;
int nScriptCheckThreads = 0;
bool fExperimentalMode = true;
bool fImporting = false;
bool fReindex = false;
bool fTxIndex = true;
bool fArchive = true;
bool fProof = true;
bool fAddressIndex = false;
bool fTimestampIndex = false;
bool fSpentIndex = false;
bool fHavePruned = false;
bool fPruneMode = false;
bool fIsBareMultisigStd = true;
bool fCheckBlockIndex = false;
bool fCheckpointsEnabled = true;
bool fUnlockedForReporting = false;
bool fCoinbaseEnforcedProtectionEnabled = true;
size_t nCoinCacheUsage = 5000 * 300;
uint64_t nPruneTarget = 0;
bool fAlerts = DEFAULT_ALERTS;
int maxProcessingThreads = 1;
/* If the tip is older than this (in seconds), the node is considered to be in initial block download.
 */
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

unsigned int expiryDelta = DEFAULT_TX_EXPIRY_DELTA;

/** Fees smaller than this (in satoshi) are considered zero fee (for relaying and mining) */
CFeeRate minRelayTxFee = CFeeRate(DEFAULT_MIN_RELAY_TX_FEE);

CTxMemPool mempool(::minRelayTxFee);
CTxMemPool tmpmempool(::minRelayTxFee);

struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};
map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_main);;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);;
void EraseOrphansFor(NodeId peer) REQUIRES(cs_main);

/**
 * Returns true if there are nRequired or more blocks of minVersion or above
 * in the last Consensus::Params::nMajorityWindow blocks, starting at pstart and going backwards.
 */
static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams);

CBlockPolicyEstimator feeEstimator;

static void CheckBlockIndex();

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const string strMessageMagic = "Komodo Signed Message:\n";

// Internal stuff
namespace {

    /** Abort with a message */
    bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
    {
        strMiscWarning = strMessage;
        LogPrintf("*** %s\n", strMessage);
        uiInterface.ThreadSafeMessageBox(
            userMessage.empty() ? _("Error: A fatal internal error occurred, see debug.log for details") : userMessage,
            "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }

    bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
    {
        AbortNode(strMessage, userMessage);
        return state.Error(strMessage);
    }

    struct CBlockIndexWorkComparator
    {
        bool operator()(CBlockIndex *pa, const CBlockIndex *pb) const {
            // First sort by most total work, ...
            if (pa->nChainWork > pb->nChainWork) return false;
            if (pa->nChainWork < pb->nChainWork) return true;

            // ... then by earliest time received, ...
            if (pa->nSequenceId < pb->nSequenceId) return false;
            if (pa->nSequenceId > pb->nSequenceId) return true;

            // Use pointer address as tie breaker (should only happen with blocks
            // loaded from disk, as those all have id 0).
            if (pa < pb) return false;
            if (pa > pb) return true;

            // Identical blocks.
            return false;
        }
    };

    CBlockIndex *pindexBestInvalid;

    /**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though, and pruning nodes may be
     * missing the data for the block.
     */

    //set<CBlockIndex*, CBlockIndexWorkComparator, std::allocator<CBlockIndex*>> setBlockIndexCandidates;
    set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;

    /** Number of nodes with fSyncStarted. */
    int nSyncStarted = 0;

    /** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions.
     * Pruned nodes may have entries where B is missing data.
     */
    multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

    CCriticalSection cs_LastBlockFile;
    std::vector<CBlockFileInfo> vinfoBlockFile,tmpBlockFiles;
    int nLastBlockFile = 0;
    int nLastTmpFile = 0;
    unsigned int maxTempFileSize0 = MAX_TEMPFILE_SIZE;
    unsigned int maxTempFileSize1 = MAX_TEMPFILE_SIZE;
    /** Global flag to indicate we should check to see if there are
     *  block/undo files that should be deleted.  Set on startup
     *  or if we allocate more file space when we're in prune mode
     */
    bool fCheckForPruning = false;

    /**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
    CCriticalSection cs_nBlockSequenceId;
    /** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
    uint32_t nBlockSequenceId = 1;

    /**
     * Sources of received blocks, saved to be able to send them reject
     * messages or ban them when processing happens afterwards. Protected by
     * cs_main.
     */
    map<uint256, NodeId> mapBlockSource;

    /**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
    std::unique_ptr<CRollingBloomFilter> recentRejects;
    uint256 hashRecentRejectsChainTip;

    /** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
    struct QueuedBlock {
        uint256 hash;
        CBlockIndex *pindex;  //! Optional.
        int64_t nTime;  //! Time of "getdata" request in microseconds.
        bool fValidatedHeaders;  //! Whether this block has validated headers at the time of request.
        int64_t nTimeDisconnect; //! The timeout for this block request (for disconnecting a slow peer)
    };
    map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > mapBlocksInFlight;

    /** Number of blocks in flight with validated headers. */
    int nQueuedValidatedHeaders = 0;

    /** Number of preferable block download peers. */
    int nPreferredDownload = 0;

    /** Dirty block index entries. */
    set<CBlockIndex*> setDirtyBlockIndex;

    /** Dirty block file entries. */
    set<int> setDirtyFileInfo;
} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace {

    struct CBlockReject {
        unsigned char chRejectCode;
        string strRejectReason;
        uint256 hashBlock;
    };

    /**
     * Maintain validation-specific state about nodes, protected by cs_main, instead
     * by CNode's own locks. This simplifies asynchronous operation, where
     * processing of incoming data is done after the ProcessMessage call returns,
     * and we're no longer holding the node's locks.
     */
    struct CNodeState {
        //! The peer's address
        CService address;
        //! Whether we have a fully established connection.
        bool fCurrentlyConnected;
        //! Accumulated misbehaviour score for this peer.
        int nMisbehavior;
        //! Whether this peer should be disconnected and banned (unless whitelisted).
        bool fShouldBan;
        //! String name of this peer (debugging/logging purposes).
        std::string name;
        //! List of asynchronously-determined block rejections to notify this peer about.
        std::vector<CBlockReject> rejects;
        //! The best known block we know this peer has announced.
        CBlockIndex *pindexBestKnownBlock;
        //! The hash of the last unknown block this peer has announced.
        uint256 hashLastUnknownBlock;
        //! The last full block we both have.
        CBlockIndex *pindexLastCommonBlock;
        //! Whether we've started headers synchronization with this peer.
        bool fSyncStarted;
        //! Since when we're stalling block download progress (in microseconds), or 0.
        int64_t nStallingSince;
        list<QueuedBlock> vBlocksInFlight;
        int nBlocksInFlight;
        int nBlocksInFlightValidHeaders;
        //! Whether we consider this a preferred download peer.
        bool fPreferredDownload;

        CNodeState() {
            fCurrentlyConnected = false;
            nMisbehavior = 0;
            fShouldBan = false;
            pindexBestKnownBlock = NULL;
            hashLastUnknownBlock.SetNull();
            pindexLastCommonBlock = NULL;
            fSyncStarted = false;
            nStallingSince = 0;
            nBlocksInFlight = 0;
            nBlocksInFlightValidHeaders = 0;
            fPreferredDownload = false;
        }
    };

    /** Map maintaining per-node state. Requires cs_main. */
    map<NodeId, CNodeState> mapNodeState;

    // Requires cs_main.
    CNodeState *State(NodeId pnode) {
        map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
        if (it == mapNodeState.end())
            return NULL;
        return &it->second;
    }

    int GetHeight()
    {
        CBlockIndex *pindex = nullptr;
        {
            LOCK(cs_main);
            pindex = chainActive.Tip();
        }
        if ( pindex != nullptr )
            return pindex->nHeight;
        return 0;
    }

    void UpdatePreferredDownload(CNode* node, CNodeState* state)
    {
        nPreferredDownload -= state->fPreferredDownload;

        // Whether this node should be marked as a preferred download node.
        state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

        nPreferredDownload += state->fPreferredDownload;
    }

    // Returns time at which to timeout block request (nTime in microseconds)
    int64_t GetBlockTimeout(int64_t nTime, int nValidatedQueuedBefore, const Consensus::Params &consensusParams)
    {
        return nTime + 500000 * consensusParams.nPowTargetSpacing * (4 + nValidatedQueuedBefore);
    }

    void InitializeNode(NodeId nodeid, const CNode *pnode) {
        LOCK(cs_main);
        CNodeState &state = mapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
        state.name = pnode->addrName;
        state.address = pnode->addr;
    }

    void FinalizeNode(NodeId nodeid) {
        LOCK(cs_main);
        CNodeState *state = State(nodeid);

        if (state->fSyncStarted)
            nSyncStarted--;

        if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
            AddressCurrentlyConnected(state->address);
        }

        BOOST_FOREACH(const QueuedBlock& entry, state->vBlocksInFlight)
        mapBlocksInFlight.erase(entry.hash);
        EraseOrphansFor(nodeid);
        nPreferredDownload -= state->fPreferredDownload;

        mapNodeState.erase(nodeid);
    }

    void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age)
    {
        /*    int expired = pool.Expire(GetTime() - age);
         if (expired != 0)
         LogPrint("mempool", "Expired %i transactions from the memory pool\n", expired);

         std::vector<uint256> vNoSpendsRemaining;
         pool.TrimToSize(limit, &vNoSpendsRemaining);
         BOOST_FOREACH(const uint256& removed, vNoSpendsRemaining)
         pcoinsTip->Uncache(removed);*/
    }

    // Requires cs_main.
    // Returns a bool indicating whether we requested this block.
    bool MarkBlockAsReceived(const uint256& hash) {
        map<uint256, pair<NodeId, list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
        if (itInFlight != mapBlocksInFlight.end()) {
            CNodeState *state = State(itInFlight->second.first);
            nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
            state->nBlocksInFlightValidHeaders -= itInFlight->second.second->fValidatedHeaders;
            state->vBlocksInFlight.erase(itInFlight->second.second);
            state->nBlocksInFlight--;
            state->nStallingSince = 0;
            mapBlocksInFlight.erase(itInFlight);
            return true;
        }
        return false;
    }

    // Requires cs_main.
    void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, const Consensus::Params& consensusParams, CBlockIndex *pindex = NULL) {
        CNodeState *state = State(nodeid);
        assert(state != NULL);

        // Make sure it's not listed somewhere already.
        MarkBlockAsReceived(hash);

        int64_t nNow = GetTimeMicros();
        QueuedBlock newentry = {hash, pindex, nNow, pindex != NULL, GetBlockTimeout(nNow, nQueuedValidatedHeaders, consensusParams)};
        nQueuedValidatedHeaders += newentry.fValidatedHeaders;
        list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
        state->nBlocksInFlight++;
        state->nBlocksInFlightValidHeaders += newentry.fValidatedHeaders;
        mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
    }

    /** Check whether the last unknown block a peer advertized is not yet known. */
    void ProcessBlockAvailability(NodeId nodeid) {
        CNodeState *state = State(nodeid);
        assert(state != NULL);

        if (!state->hashLastUnknownBlock.IsNull()) {
            BlockMap::iterator itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
            if (itOld != mapBlockIndex.end() && itOld->second != 0 && (itOld->second->nChainWork > 0))
            {
                if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                    state->pindexBestKnownBlock = itOld->second;
                state->hashLastUnknownBlock.SetNull();
            }
        }
    }

    /** Update tracking information about which blocks a peer is assumed to have. */
    void UpdateBlockAvailability(NodeId nodeid, const uint256 &hash) {
        CNodeState *state = State(nodeid);
        assert(state != NULL);

        /*ProcessBlockAvailability(nodeid);

         BlockMap::iterator it = mapBlockIndex.find(hash);
         if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
         // An actually better block was announced.
         if (state->pindexBestKnownBlock == NULL || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
         state->pindexBestKnownBlock = it->second;
         } else*/
        {
            // An unknown block was announced; just assume that the latest one is the best one.
            state->hashLastUnknownBlock = hash;
        }
    }

    /** Find the last common ancestor two blocks have.
     *  Both pa and pb must be non-NULL. */
    CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb) {
        if (pa->nHeight > pb->nHeight) {
            pa = pa->GetAncestor(pb->nHeight);
        } else if (pb->nHeight > pa->nHeight) {
            pb = pb->GetAncestor(pa->nHeight);
        }

        while (pa != pb && pa && pb) {
            pa = pa->pprev;
            pb = pb->pprev;
        }

        // Eventually all chain branches meet at the genesis block.
        assert(pa == pb);
        return pa;
    }

    /** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
     *  at most count entries. */
    void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller) {
        if (count == 0)
            return;

        vBlocks.reserve(vBlocks.size() + count);
        CNodeState *state = State(nodeid);
        assert(state != NULL);

        // Make sure pindexBestKnownBlock is up to date, we'll need it.
        ProcessBlockAvailability(nodeid);

        if (state->pindexBestKnownBlock == NULL || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
            // This peer has nothing interesting.
            return;
        }

        if (state->pindexLastCommonBlock == NULL) {
            // Bootstrap quickly by guessing a parent of our best tip is the forking point.
            // Guessing wrong in either direction is not a problem.
            state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
        }

        // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
        // of its current tip anymore. Go back enough to fix that.
        state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
        if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
            return;

        std::vector<CBlockIndex*> vToFetch;
        CBlockIndex *pindexWalk = state->pindexLastCommonBlock;
        // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
        // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
        // download that next block if the window were 1 larger.
        int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
        int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
        NodeId waitingfor = -1;
        while (pindexWalk->nHeight < nMaxHeight) {
            // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
            // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
            // as iterating over ~100 CBlockIndex* entries anyway.
            int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
            vToFetch.resize(nToFetch);
            pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
            vToFetch[nToFetch - 1] = pindexWalk;
            for (unsigned int i = nToFetch - 1; i > 0; i--) {
                vToFetch[i - 1] = vToFetch[i]->pprev;
            }

            // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
            // are not yet downloaded and not in flight to vBlocks. In the meantime, update
            // pindexLastCommonBlock as long as all ancestors are already downloaded, or if it's
            // already part of our chain (and therefore don't need it even if pruned).
            BOOST_FOREACH(CBlockIndex* pindex, vToFetch) {
                if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                    // We consider the chain that this peer is on invalid.
                    return;
                }
                if (pindex->nStatus & BLOCK_HAVE_DATA || chainActive.Contains(pindex)) {
                    if (pindex->nChainTx)
                        state->pindexLastCommonBlock = pindex;
                } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                    // The block is not already downloaded, and not yet in flight.
                    if (pindex->nHeight > nWindowEnd) {
                        // We reached the end of the window.
                        if (vBlocks.size() == 0 && waitingfor != nodeid) {
                            // We aren't able to fetch anything, but we would be if the download window was one larger.
                            nodeStaller = waitingfor;
                        }
                        return;
                    }
                    vBlocks.push_back(pindex);
                    if (vBlocks.size() == count) {
                        return;
                    }
                } else if (waitingfor == -1) {
                    // This is the first already-in-flight block.
                    waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
                }
            }
        }
    }

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats &stats) {
    LOCK(cs_main);
    CNodeState *state = State(nodeid);
    if (state == NULL)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    BOOST_FOREACH(const QueuedBlock& queue, state->vBlocksInFlight) {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.connect(&GetHeight);
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.GetHeight.disconnect(&GetHeight);
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    BOOST_FOREACH(const uint256& hash, locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end())
        {
            CBlockIndex* pindex = (*mi).second;
            if (pindex != 0 && chain.Contains(pindex))
                return pindex;
            if (pindex != 0 && pindex->GetAncestor(chain.Height()) == chain.Tip()) {
                return chain.Tip();
            }
        }
    }
    return chain.Genesis();
}

CCoinsViewCache *pcoinsTip = nullptr;
CBlockTreeDB *pblocktree = nullptr;

// Komodo globals

#include "komodo.h"

UniValue komodo_snapshot(int top)
{
    LOCK(cs_main);
    int64_t total = -1;
    UniValue result(UniValue::VOBJ);

    if (fAddressIndex) {
	    if ( pblocktree != nullptr ) {
		result = pblocktree->Snapshot(top);
	    } else {
		fprintf(stderr,"null pblocktree start with -addressindex=1\n");
	    }
    } else {
	    fprintf(stderr,"getsnapshot requires -addressindex=1\n");
    }
    return(result);
}

bool komodo_snapshot2(std::map <std::string, CAmount> &addressAmounts)
{
    if ( fAddressIndex && pblocktree != nullptr )
    {
		return pblocktree->Snapshot2(addressAmounts, 0);
    }
    else return false;
}

int32_t lastSnapShotHeight = 0;
std::vector <std::pair<CAmount, CTxDestination>> vAddressSnapshot;

bool komodo_dailysnapshot(int32_t height)
{
    int reorglimit = 100;
    uint256 notarized_hash,notarized_desttxid; int32_t prevMoMheight,notarized_height,undo_height,extraoffset;
    // NOTE: To make this 100% safe under all sync conditions, it should be using a notarized notarization, from the DB.
    // Under heavy reorg attack, its possible `komodo_notarized_height` can return a height that can't be found on chain sync.
    // However, the DB can reorg the last notarization. By using 2 deep, we know 100% that the previous notarization cannot be
    // reorged by online nodes, and as such will always be notarizing the same height. May need to check heights on scan back
    // to make sure they are confirmed in correct order.
    if ( (extraoffset= height % KOMODO_SNAPSHOT_INTERVAL) != 0 )
    {
        // we are on chain init, and need to scan all the way back to the correct height, other wise our node will have a diffrent snapshot to online nodes.
        // use the notarizationsDB to scan back from the consesnus height to get the offset we need.
        Notarisation nota;
        if ( ScanNotarisationsDB(height-extraoffset, chainName.symbol(), 100, nota) == 0 )
            undo_height = height-extraoffset-reorglimit;
        else
            undo_height = nota.second.height;
    }
    else
    {
        // we are at the right height in connect block to scan back to last notarized height.
        notarized_height = komodo_notarized_height(&prevMoMheight,&notarized_hash,&notarized_desttxid);
        notarized_height > height-reorglimit ? undo_height = notarized_height : undo_height = height-reorglimit;
    }
    fprintf(stderr, "doing snapshot for height.%i undo_height.%i\n", height, undo_height);
    // if we already did this height dont bother doing it again, this is just a reorg. The actual snapshot height cannot be reorged.
    if ( undo_height == lastSnapShotHeight )
        return true;
    std::map <std::string, int64_t> addressAmounts;
    if ( !komodo_snapshot2(addressAmounts) )
        return false;

    // undo blocks in reverse order
    for (int32_t n = height; n > undo_height; n--)
    {
        CBlockIndex *pindex; CBlock block;
        if ( (pindex= komodo_chainactive(n)) == 0 || komodo_blockload(block, pindex) != 0 )
            return false;
        // undo transactions in reverse order
        for (int32_t i = block.vtx.size() - 1; i >= 0; i--)
        {
            const CTransaction &tx = block.vtx[i];
            CTxDestination vDest;
            // loop vouts reverse order, remove value recieved.
            for (unsigned int k = tx.vout.size(); k-- > 0;)
            {
                const CTxOut &out = tx.vout[k];
                if ( ExtractDestination(out.scriptPubKey, vDest) )
                {
                    addressAmounts[CBitcoinAddress(vDest).ToString()] -= out.nValue;
                    if ( addressAmounts[CBitcoinAddress(vDest).ToString()] < 1 )
                        addressAmounts.erase(CBitcoinAddress(vDest).ToString());
                }
            }
            // loop vins in reverse order, get prevout and return the sent balance.
            for (unsigned int j = tx.vin.size(); j-- > 0;)
            {
                uint256 blockhash; CTransaction txin;
                if ( !tx.IsCoinImport() && !tx.IsCoinBase() && myGetTransaction(tx.vin[j].prevout.hash,txin,blockhash) )
                {
                    int vout = tx.vin[j].prevout.n;
                    if ( ExtractDestination(txin.vout[vout].scriptPubKey, vDest) )
                    {
                        addressAmounts[CBitcoinAddress(vDest).ToString()] += txin.vout[vout].nValue;
                    }
                }
            }
        }
    }
    vAddressSnapshot.clear(); // clear existing snapshot
    // convert address string to destination for easier conversion to what ever is required, eg, scriptPubKey.
    for ( auto element : addressAmounts)
        vAddressSnapshot.push_back(make_pair(element.second, DecodeDestination(element.first)));
    // sort the vector by amount, highest at top.
    std::sort(vAddressSnapshot.rbegin(), vAddressSnapshot.rend());
    // include only top 3999 address.
    if ( vAddressSnapshot.size() > 3999 ) vAddressSnapshot.resize(3999);
    lastSnapShotHeight = undo_height;
    fprintf(stderr, "vAddressSnapshot.size.%d\n", (int32_t)vAddressSnapshot.size());
    return true;
}

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx, NodeId peer) REQUIRES(cs_main)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = GetSerializeSize(tx, SER_NETWORK, tx.nVersion);
    if (sz > 5000)
    {
        LogPrint("mempool", "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint("mempool", "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
             mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}

void static EraseOrphanTx(uint256 hash) REQUIRES(cs_main)
{
    map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    BOOST_FOREACH(const CTxIn& txin, it->second.tx.vin)
    {
        map<uint256, set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

void EraseOrphansFor(NodeId peer)
{
    int nErased = 0;
    map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end())
    {
        map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer)
        {
            EraseOrphanTx(maybeErase->second.tx.GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint("mempool", "Erased %d orphan tx from peer %d\n", nErased, peer);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) REQUIRES(cs_main)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
            EraseOrphanTx(it->first);
            ++nEvicted;
    }
    return nEvicted;
}


bool IsStandardTx(const CTransaction& tx, string& reason, const int nHeight)
{
    bool overwinterActive = NetworkUpgradeActive(nHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER);
    bool saplingActive = NetworkUpgradeActive(nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);
    bool orchardActive = NetworkUpgradeActive(nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD);

    if (orchardActive) {
        // Sapling standard rules apply
        if (tx.nVersion > CTransaction::ORCHARD_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::ORCHARD_MIN_CURRENT_VERSION) {
            reason = "orchard-version";
            return false;
        }
    } else if (saplingActive) {
        // Sapling standard rules apply
        if (tx.nVersion > CTransaction::SAPLING_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SAPLING_MIN_CURRENT_VERSION) {
            reason = "sapling-version";
            return false;
        }
    } else if (overwinterActive) {
        // Overwinter standard rules apply
        if (tx.nVersion > CTransaction::OVERWINTER_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::OVERWINTER_MIN_CURRENT_VERSION) {
            reason = "overwinter-version";
            return false;
        }
    } else {
        // Sprout standard rules apply
        if (tx.nVersion > CTransaction::SPROUT_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SPROUT_MIN_CURRENT_VERSION) {
            reason = "version";
            return false;
        }
    }

    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650) {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly()) {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int v=0,nDataOut = 0;
    txnouttype whichType;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        if (!::IsStandard(txout.scriptPubKey, whichType))
        {
            reason = "scriptpubkey";
            //fprintf(stderr,">>>>>>>>>>>>>>> vout.%d nDataout.%d\n",v,nDataOut);
            return false;
        }

        if (whichType == TX_NULL_DATA)
        {
            if ( txout.scriptPubKey.size() > IGUANA_MAXSCRIPTSIZE )
            {
                reason = "opreturn too big";
                return(false);
            }
            nDataOut++;
            //fprintf(stderr,"is OP_RETURN\n");
        }
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (whichType != TX_CRYPTOCONDITION && txout.IsDust(::minRelayTxFee)) {
            reason = "dust";
            return false;
        }
        v++;
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1) {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
     if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if ( !komodo_hardfork_active(nBlockTime) && txin.nSequence == 0xfffffffe &&
        //if ( (nBlockTime <= ASSETCHAINS_STAKED_HF_TIMESTAMP ) && txin.nSequence == 0xfffffffe &&
            (
                ((int64_t)tx.nLockTime >= LOCKTIME_THRESHOLD && (int64_t)tx.nLockTime > nBlockTime) ||
                ((int64_t)tx.nLockTime <  LOCKTIME_THRESHOLD && (int64_t)tx.nLockTime > nBlockHeight)
            )
        )
        {

        }
        //else if ( nBlockTime > ASSETCHAINS_STAKED_HF_TIMESTAMP && txin.nSequence == 0xfffffffe &&
        else if ( komodo_hardfork_active(nBlockTime) && txin.nSequence == 0xfffffffe &&
            (
                ((int64_t)tx.nLockTime >= LOCKTIME_THRESHOLD && (int64_t)tx.nLockTime <= nBlockTime) ||
                ((int64_t)tx.nLockTime <  LOCKTIME_THRESHOLD && (int64_t)tx.nLockTime <= nBlockHeight))
            )
        {

        }
        else if (!txin.IsFinal())
        {
            LogPrintf("non-final txin seq.%x locktime.%u vs nTime.%u\n",txin.nSequence,(uint32_t)tx.nLockTime,(uint32_t)nBlockTime);
            return false;
        }
    }
    return true;
}

/**
 * Check if transaction is expired and can be included in a block with the
 * specified height. Consensus critical.
 * @param tx the transaction
 * @param nBlockHeight the current block height
 * @returns true if transaction is expired (mainly tx.expiryHeight > nBlockHeight)
 */
bool IsExpiredTx(const CTransaction &tx, int nBlockHeight)
{
    if (tx.nExpiryHeight == 0 || tx.IsCoinBase()) {
        return false;
    }
    return static_cast<uint32_t>(nBlockHeight) > tx.nExpiryHeight;
}

bool IsExpiringSoonTx(const CTransaction &tx, int nNextBlockHeight)
{
    return IsExpiredTx(tx, nNextBlockHeight + TX_EXPIRING_SOON_THRESHOLD);
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // Timestamps on the other hand don't get any special treatment,
    // because we can't know what timestamp the next block will have,
    // and there aren't timestamp applications where it matters.
    // However this changes once median past time-locks are enforced:
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
    ? chainActive.Tip()->GetMedianTimePast()
    : GetTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs, uint32_t consensusBranchId)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    if (tx.IsCoinImport())
        return tx.vin[0].scriptSig.IsCoinImport();

    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut& prev = mapInputs.GetOutputFor(tx.vin[i]);

        vector<vector<unsigned char> > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        //printf("Previous script: %s\n", prevScript.ToString().c_str());

        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandardTx() will have already returned false
        // and this method isn't called.
        vector<vector<unsigned char> > stack;
        //printf("Checking script: %s\n", tx.vin[i].scriptSig.ToString().c_str());
        if (!EvalScript(stack, tx.vin[i].scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), consensusBranchId))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<vector<unsigned char> > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int GetLegacySigOpCount(const CTransaction& tx)
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& inputs)
{
    if (tx.IsCoinBase() || tx.IsCoinImport())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        const CTxOut &prevout = inputs.GetOutputFor(tx.vin[i]);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(tx.vin[i].scriptSig);
    }
    return nSigOps;
}

/**
 * Ensure that a coinbase transaction is structured according to the consensus rules of the
 * chain
 */
bool ContextualCheckCoinbaseTransaction(const CTransaction& tx, const int nHeight)
{
    // if time locks are on, ensure that this coin base is time locked exactly as it should be
    if (((uint64_t)(tx.GetValueOut()) >= ASSETCHAINS_TIMELOCKGTE) || (komodo_ac_block_subsidy(nHeight) >= ASSETCHAINS_TIMELOCKGTE))
    {
        CScriptID scriptHash;

        // to be valid, it must be a P2SH transaction and have an op_return in vout[1] that
        // holds the full output script, which may include multisig, etc., but starts with
        // the time lock verify of the correct time lock for this block height
        if (tx.vout.size() == 2 &&
            CScriptExt(tx.vout[0].scriptPubKey).IsPayToScriptHash(&scriptHash) &&
            tx.vout[1].scriptPubKey.size() >= 7 && // minimum for any possible future to prevent out of bounds
            tx.vout[1].scriptPubKey[0] == OP_RETURN)
        {
            opcodetype op;
            std::vector<uint8_t> opretData = std::vector<uint8_t>();
            CScript::const_iterator it = tx.vout[1].scriptPubKey.begin() + 1;
            if (tx.vout[1].scriptPubKey.GetOp2(it, op, &opretData))
            {
                if (opretData.size() > 0 && opretData.data()[0] == OPRETTYPE_TIMELOCK)
                {
                    int64_t unlocktime;
                    CScriptExt opretScript = CScriptExt(&opretData[1], &opretData[opretData.size()]);

                    if (CScriptID(opretScript) == scriptHash &&
                        opretScript.IsCheckLockTimeVerify(&unlocktime) &&
                        komodo_block_unlocktime(nHeight) == unlocktime)
                    {
                        return(true);
                    }
                }
            }
        }
        return(false);
    }
    return(true);
}

/* Called from ContextualCheckTransactionMultithreaded for the checks that do not require signficant processing
 *
 * Check a transaction contextually against a set of consensus rules valid at a given block height.
 *
 * Notes:
 * 1. AcceptToMemoryPool calls CheckTransaction and ContextualCheckTransactionMultithreaded.
 * 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
 *    and ContextualCheckBlock (which calls ContextualCheckTransactionMultithreaded).
 * 3. The isInitBlockDownload argument is only to assist with testing.
 */

CheckTransationResults ContextualCheckTransactionSingleThreaded(
    const CTransaction tx,
    const int nHeight,
    const int dosLevel,
    const bool isInitialBlockDownload,
    const uint32_t threadNumber) {

    //Results to be returned
    CheckTransationResults txResults;

    //Get current Consensus Branch ID
    auto consensus = Params().GetConsensus();
    auto consensusBranchId = CurrentEpochBranchId(nHeight, consensus);

    //Set Chain parameters to check against
    bool overwinterActive = NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_OVERWINTER);
    bool saplingActive = NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_SAPLING);
    bool orchardActive = NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_ORCHARD);
    bool isSprout = !overwinterActive;

    //Get Sapling and Orchard bundles
    auto& sapling_bundle = tx.GetSaplingBundle();
    auto& orchard_bundle = tx.GetOrchardBundle();

    //Coinbase rules - allow transaparent addresses only
    if (tx.IsCoinBase()) {
        if (sapling_bundle.IsPresent()) {
            txResults.validationPassed = false;
            txResults.dosLevel = dosLevel;
            txResults.errorString = strprintf("ContextualCheckTransaction(): sapling bundle not allowed in coinbase transaction");
            txResults.reasonString = strprintf("tx-coinbase-sapling-bundle");
            return txResults;
        }

        if (orchard_bundle.IsPresent()) {
            txResults.validationPassed = false;
            txResults.dosLevel = dosLevel;
            txResults.errorString = strprintf("ContextualCheckTransaction(): orchard bundle not allowed in coinbase transaction");
            txResults.reasonString = strprintf("tx-coinbase-orchard-bundle");
            return txResults;
        }

    }

    // If Sprout rules apply, reject transactions which are intended for Overwinter and beyond
    if (isSprout && tx.fOverwintered) {
        int32_t ht = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight;
        if (ht < 0 || nHeight < ht) {
            txResults.dosLevel = 0;
        } else {
            txResults.dosLevel = dosLevel;
        }
        txResults.validationPassed = false;
        txResults.errorString = strprintf("ContextualCheckTransaction(): ht.%d activates.%d dosLevel.%d overwinter is not active yet",nHeight, consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight, txResults.dosLevel);
        txResults.reasonString = strprintf("tx-overwinter-not-active");
        return txResults;
    }

    // Rules that apply to Overwinter or later:
    if (overwinterActive) {
        // Reject transactions intended for Sprout
        if (!tx.fOverwintered) {
            int32_t ht = consensus.vUpgrades[Consensus::UPGRADE_OVERWINTER].nActivationHeight;
            if (ASSETCHAINS_PRIVATE != 0 || ht < 0 || nHeight < ht) {
                txResults.dosLevel = 0;
            } else {
                txResults.dosLevel = dosLevel;
            }
            txResults.validationPassed = false;
            txResults.errorString = strprintf("ContextualCheckTransaction: overwinter is active");
            txResults.reasonString = strprintf("tx-overwinter-active");
            return txResults;
        }

        // Reject transactions with valid version but missing overwinter flag
        if (tx.nVersion >= OVERWINTER_MIN_TX_VERSION && !tx.fOverwintered) {
            txResults.validationPassed = false;
            txResults.dosLevel = dosLevel;
            txResults.errorString = strprintf("ContextualCheckTransaction(): overwinter flag must be set");
            txResults.reasonString = strprintf("tx-overwinter-flag-not-set");
            return txResults;
        }

        // Reject transactions with an overwinter flag and a version below overwinter
        if (tx.nVersion < OVERWINTER_MIN_TX_VERSION && tx.fOverwintered) {
            txResults.validationPassed = false;
            txResults.dosLevel = dosLevel;
            txResults.errorString = strprintf("ContextualCheckTransaction(): tx version is too old");
            txResults.reasonString = strprintf("tx-overwinter-version-too-old");
            return txResults;
        }

        // Check that all transactions are unexpired
        if (IsExpiredTx(tx, nHeight)) {
            // Don't increase banscore if the transaction only just expired
            int expiredDosLevel = IsExpiredTx(tx, nHeight - 1) ? (dosLevel > 10 ? dosLevel : 10) : 0;
            txResults.validationPassed = false;
            txResults.dosLevel = 100;
            txResults.errorString = strprintf("ContextualCheckTransaction(): transaction %s is expired, expiry block %i vs current block %i\n",tx.GetHash().ToString(),tx.nExpiryHeight,nHeight);
            txResults.reasonString = strprintf("tx-overwinter-expired");
            return txResults;
        }

        //Rules that apply before Sapling Activates
        if (!saplingActive) {
            // Reject transactions with non-Overwinter version group ID
            if (tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID) {
                if (isInitialBlockDownload) {
                    txResults.dosLevel = 0;
                } else {
                    txResults.dosLevel = dosLevel;
                }
                txResults.validationPassed = false;
                txResults.errorString = strprintf("CheckTransaction(): invalid Overwinter tx version");
                txResults.reasonString = strprintf("bad-overwinter-tx-version-group-id");
                return txResults;
            }

            // Reject transactions with invalid version
            if (tx.nVersion > OVERWINTER_MAX_TX_VERSION ) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): overwinter version too high");
                txResults.reasonString = strprintf("bad-tx-overwinter-version-too-high");
                return txResults;
            }
        }
    }

    // Rules that apply before Sapling:
    if (!saplingActive) {
        // Size limits
        if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_BEFORE_SAPLING) {
            txResults.validationPassed = false;
            txResults.dosLevel = 100;
            txResults.errorString = strprintf("ContextualCheckTransaction(): size limits failed");
            txResults.reasonString = strprintf("bad-txns-oversize");
            return txResults;
        }
        // Check for presence of a sapling bundle
        if (sapling_bundle.IsPresent()) {
            txResults.validationPassed = false;
            txResults.dosLevel = 100;
            txResults.errorString = strprintf("ContextualCheckTransaction(): pre-Sapling transaction has Sapling components");
            txResults.reasonString = strprintf("bad-tx-has-orchard-actions");
            return txResults;
        }
    }

    // Rules that apply to Sapling or later:
    if (saplingActive) {

        if (tx.nVersionGroupId == SAPLING_VERSION_GROUP_ID) {
            // Reject transactions with invalid version
            if (tx.nVersion < SAPLING_MIN_TX_VERSION ) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): Sapling version too low");
                txResults.reasonString = strprintf("bad-tx-sapling-version-too-low");
                return txResults;
            }

            // Reject transactions with invalid version
            if (tx.nVersion > SAPLING_MAX_TX_VERSION ) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): Sapling version too high");
                txResults.reasonString = strprintf("bad-tx-sapling-version-too-high");
                return txResults;
            }
        }

        // Rules that became inactive after Orchard activation.
        if (!orchardActive) {
            // Reject transactions with non-Sapling version group ID
            if (tx.nVersionGroupId != SAPLING_VERSION_GROUP_ID) {
                if (isInitialBlockDownload) {
                    txResults.dosLevel = 0;
                } else {
                    txResults.dosLevel = dosLevel;
                }
                txResults.validationPassed = false;
                txResults.errorString = strprintf("CheckTransaction(): invalid Sapling tx version");
                txResults.reasonString = strprintf("bad-sapling-tx-version-group-id");
                return txResults;
            }
        }
    }

    // Rules that apply before Orchard:
    if (!orchardActive) {
        // Check of presence of an orchard bundle
        if (orchard_bundle.IsPresent()) {
            txResults.validationPassed = false;
            txResults.dosLevel = 100;
            txResults.errorString = strprintf("ContextualCheckTransaction(): pre-Orchard transaction has Orchard actions");
            txResults.reasonString = strprintf("bad-tx-has-orchard-actions");
            return txResults;
        }
        //This should not be set prior to Orchard being active
        if (tx.GetConsensusBranchId().has_value()) {
            txResults.validationPassed = false;
            txResults.dosLevel = 100;
            txResults.errorString = strprintf("CheckTransaction(): pre-Orchard transaction does has consensus branch id field set");
            txResults.reasonString = strprintf("bad-tx-pre-orchard consensus-branch-id");
        }
    }

    // Rules that apply to Orchard or later:
    if (orchardActive) {
        // Reject transactions with non-Sapling version group ID
        if (!(tx.nVersionGroupId == SAPLING_VERSION_GROUP_ID || tx.nVersionGroupId == ORCHARD_VERSION_GROUP_ID)) {
            if (isInitialBlockDownload) {
                txResults.dosLevel = 0;
            } else {
                txResults.dosLevel = dosLevel;
            }
            txResults.validationPassed = false;
            txResults.errorString = strprintf("CheckTransaction(): invalid Orchard tx version");
            txResults.reasonString = strprintf("bad-orchard-tx-version-group-id");
            return txResults;
        }

        if (tx.nVersionGroupId == ORCHARD_VERSION_GROUP_ID) {
            // Reject transactions with invalid version
            if (tx.fOverwintered && tx.nVersion < ORCHARD_MIN_TX_VERSION ) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): Orchard version too low");
                txResults.reasonString = strprintf("bad-tx-orchard-version-too-low");
                return txResults;
            }

            // Reject transactions with invalid version
            if (tx.fOverwintered && tx.nVersion > ORCHARD_MAX_TX_VERSION ) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): Orchard version too high");
                txResults.reasonString = strprintf("bad-tx-orchard-version-too-high");
                return txResults;
            }

            if (!tx.GetConsensusBranchId().has_value()) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): transaction does not have consensus branch id field set");
                txResults.reasonString = strprintf("bad-tx-consensus-branch-id-missing");
                return txResults;
            }

            // tx.nConsensusBranchId must match the current consensus branch id
            if (tx.GetConsensusBranchId().value() != consensusBranchId) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): transaction's consensus branch id (%08x) does not match the current consensus branch (%08x)", tx.GetConsensusBranchId().value(), consensusBranchId);
                txResults.reasonString = strprintf("bad-tx-consensus-branch-id-mismatch");
                return txResults;
            }

            // v5 transactions must have empty joinSplits
            if (!(tx.vjoinsplit.empty())) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("CheckTransaction(): Sprout JoinSplits not allowed in Orchard transactions.");
                txResults.reasonString = strprintf("bad-tx-has-joinsplits");
                return txResults;
            }
        }
    }

    return txResults;
}

/* Called from ContextualCheckTransactionMultithreaded for the checks that signficant processing
 *
 * Check a transaction contextually against a specific consensus rule valid at a given block height.
 *
 * Notes:
 * 1. AcceptToMemoryPool calls CheckTransaction and ContextualCheckTransactionMultithreaded.
 * 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
 *    and ContextualCheckBlock (which calls ContextualCheckTransactionMultithreaded).
 */
CheckTransationResults ContextualCheckTransactionBindingSigWorker(
    const std::vector<const CTransaction*> vtx,
    const std::vector<uint256> vTxSig,
    const uint32_t threadNumber) {

    //Results to be returned
    CheckTransationResults txResults;

    for (int i = 0; i < vtx.size(); i++) {
        const uint256 dataToBeSigned = vTxSig[i];

        // Queue Sapling bundle to be batch-validated. This also checks some consensus rules.
        if (vtx[i]->GetSaplingBundle().IsPresent()) {

            // This will be a single-transaction batch, which will be more efficient
            // than unbatched if the transaction contains at least one Sapling Spend
            // or at least two Sapling Outputs.
            std::optional<rust::Box<sapling::BatchValidator>> saplingAuth = sapling::init_batch_validator(true);

            if (saplingAuth.has_value()) {
                if (!vtx[i]->GetSaplingBundle().QueueAuthValidation(*saplingAuth.value(), dataToBeSigned)) {
                    txResults.validationPassed = false;
                    txResults.dosLevel = 100;
                    txResults.errorString = strprintf("ContextualCheckTransaction(): Sapling bundle authorization queue failed");
                    txResults.reasonString = strprintf("bad-txns-sapling-bundle-authorization");
                    return txResults;
                }
            }

            if (!saplingAuth.value()->validate()) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("ContextualCheckTransaction(): Sapling bundle authorization validate failed");
                txResults.reasonString = strprintf("bad-txns-sapling-bundle-authorization");
                return txResults;
            }
        }

        //Perform Orchard Checks after Sapling checks are done

        if (vtx[i]->GetOrchardBundle().IsPresent()) {
            // This will be a single-transaction batch, which is still more efficient as every
            // Orchard bundle contains at least two signatures.
            std::optional<rust::Box<orchard::BatchValidator>> orchardAuth = orchard::init_batch_validator(true);

            // Queue Orchard bundle to be batch-validated.
            if (orchardAuth.has_value()) {
                vtx[i]->GetOrchardBundle().QueueAuthValidation(*orchardAuth.value(), dataToBeSigned);
            }

            if (!orchardAuth.value()->validate()) {
                txResults.validationPassed = false;
                txResults.dosLevel = 100;
                txResults.errorString = strprintf("ContextualCheckTransaction(): Orchard bundle authorization invalid");
                txResults.reasonString = strprintf("bad-txns-orchard-bundle-authorization");
                return txResults;
            }
        }
    }

    return txResults;
}

/**
 * Check a transaction contextually against a set of consensus rules valid at a given block height.
 *
 * Notes:
 * 1. AcceptToMemoryPool calls CheckTransaction and this function.
 * 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
 *    and ContextualCheckBlock (which calls this function).
 * 3. The isInitBlockDownload argument is only to assist with testing.
 */
bool ContextualCheckTransactionMultithreaded(
        int32_t slowflag,
        const std::vector<const CTransaction*> vptx,
        const CCoinsViewCache &view,
        CBlockIndex * const previndex,
        CValidationState &state,
        const int nHeight,
        const int dosLevel,
        bool (*isInitBlockDownload)(),int32_t validateprices) {

      //Create a Vector of futures to be collected later
      std::vector<std::future<CheckTransationResults>> vFutures;

      bool isInitialBlockDownload = isInitBlockDownload();

      //Setup tx batches
      std::vector<const CTransaction*> vtx;
      std::vector<std::vector<const CTransaction*>> vvtx;
      std::vector<uint256> vTxSig;
      std::vector<std::vector<uint256>> vvTxSig;

      //Setup spend batches
      // std::vector<const SpendDescription*> vSpend;
      // std::vector<std::vector<const SpendDescription*>> vvSpend;
      std::vector<uint256> vSpendSig;
      std::vector<std::vector<uint256>> vvSpendSig;

      //Setup output batches
      // std::vector<const OutputDescription*> vOutput;
      // std::vector<std::vector<const OutputDescription*>> vvOutput;

      //Create Thread Vectors
      for (int i = 0; i < maxProcessingThreads; i++) {
          vvtx.emplace_back(vtx);
          vvTxSig.emplace_back(vTxSig);
          // vvSpend.emplace_back(vSpend);
          vvSpendSig.emplace_back(vSpendSig);
          // vvOutput.emplace_back(vOutput);
      }

      //Check coinbase transaction and push all transactions to thread batch vectors
      int t = 0;
      int s = 0;
      int o = 0;
      for (uint32_t i = 0; i < vptx.size(); i++) {
          const CTransaction* tx = vptx[i];

          //check the cointbase transaction
          if (tx->IsCoinBase()) {
              if (!ContextualCheckCoinbaseTransaction(*tx, nHeight)) {
                  return state.DoS(100, error("CheckTransaction(): invalid script data for coinbase time lock"),
                                      REJECT_INVALID, "bad-txns-invalid-script-data-for-coinbase-time-lock");
              }
          }

          //Get Signature hash to pass to the sapling verifiers later

          uint256 dataToBeSigned;

          if (!tx->IsMint() &&
              (!tx->vjoinsplit.empty() ||
                tx->GetSaplingBundle().IsPresent() ||
                tx->GetOrchardBundle().IsPresent())) {

              if (!view.HaveInputs(*tx))
                  return state.DoS(100, error("CheckTransaction(): inputs missing/spent"),
                                     REJECT_INVALID, "bad-txns-inputs-missingorspent");

              std::vector<CTxOut> allPrevOutputs;
              for (const auto& input : tx->vin) {
                  allPrevOutputs.push_back(view.GetOutputFor(input));
              }
              PrecomputedTransactionData txdata(*tx, allPrevOutputs);

              auto consensusBranchId = CurrentEpochBranchId(nHeight, Params().GetConsensus());
              // Empty output script.
              CScript scriptCode;
              try {
                  dataToBeSigned = SignatureHash(scriptCode, *tx, NOT_AN_INPUT, SIGHASH_ALL, 0, consensusBranchId, txdata);
              } catch (std::logic_error ex) {
                  return state.DoS(100, error("CheckTransaction(): error computing signature hash"),
                                      REJECT_INVALID, "error-computing-signature-hash");
              }
          }

          if (!(tx->IsMint() || tx->vjoinsplit.empty())) {
              BOOST_STATIC_ASSERT(crypto_sign_PUBLICKEYBYTES == 32);
              // We rely on libsodium to check that the signature is canonical.
              // https://github.com/jedisct1/libsodium/commit/62911edb7ff2275cccd74bf1c8aefcc4d76924e0
              if (crypto_sign_verify_detached(&tx->joinSplitSig[0], dataToBeSigned.begin(), 32,tx->joinSplitPubKey.begin()) != 0) {
                  return state.DoS(isInitialBlockDownload? 0 : 100, error("CheckTransaction(): invalid joinsplit signature"),
                                      REJECT_INVALID, "bad-txns-invalid-joinsplit-signature");
              }
          }

          //Most of the check can be done sigle threaded and do not warrant the additonal overhead required to spin up threads
          CheckTransationResults singleResults = ContextualCheckTransactionSingleThreaded(*tx, nHeight, dosLevel, isInitialBlockDownload, i);

          //Return single threaded results
          if (!singleResults.validationPassed) {
            return state.DoS(singleResults.dosLevel, error(singleResults.errorString.c_str()), REJECT_INVALID, singleResults.reasonString);
          }

          //Skip costly sapling checks on intial download below the hardcoded checkpoints
          if (!fCheckpointsEnabled || nHeight >= Checkpoints::GetTotalBlocksEstimate(Params().Checkpoints())) {
              //Verify Sapling & Orchard
              if (tx->GetSaplingBundle().IsPresent() || tx->GetOrchardBundle().IsPresent()) {
                  //Push tx to thread vector
                  vvtx[t].emplace_back(tx);
                  vvTxSig[t].emplace_back(dataToBeSigned);
                  //Increment thread vector
                  t++;
                  //reset if tread vector is greater qty of threads being used
                  if (t >= vvtx.size()) {
                      t = 0;
                  }
              }
          }
      }

      //Push batches of txs to async threads
      for (int i = 0; i < vvtx.size(); i++) {
          //Perform transaction level checks
          if (!vvtx[i].empty()) {
              vFutures.emplace_back(std::async(std::launch::async, ContextualCheckTransactionBindingSigWorker, vvtx[i], vvTxSig[i], i));
          }
      }

      //Wait for all threads to complete
      for (auto &future : vFutures) {
          future.wait();
      }

      bool checkResults = true;
      CheckTransationResults failedResult;

      //Collect the async results
      for (auto &future : vFutures) {
          auto result = future.get();
          if (!result.validationPassed) {
              checkResults = false;
              //Return the highest dosLevel error, or first error if there are multiple equal dosLevel errors
              if (failedResult.validationPassed || failedResult.dosLevel < result.dosLevel) {
                failedResult = result;
              }
          }
      }

      //Return failed result
      if (!checkResults) {
        return state.DoS(failedResult.dosLevel, error(failedResult.errorString.c_str()), REJECT_INVALID, failedResult.reasonString);
      }

      return true;
}

bool CheckTransaction(uint32_t tiptime,const CTransaction& tx, CValidationState &state,
                      ProofVerifier& verifier,int32_t txIndex, int32_t numTxs)
{
    if (chainName.isKMD())
    {
        // check for banned transaction ids
        static uint256 array[64];
        static int32_t numbanned;
        static int32_t indallvouts;
        if ( *(int32_t *)&array[0] == 0 )
            numbanned = komodo_bannedset(&indallvouts,array,(int32_t)(sizeof(array)/sizeof(*array)));

        for (size_t j=0; j< tx.vin.size(); j++) // for every tx.vin
        {
            for (int32_t k=0; k<numbanned; k++) // go through the array of banned txids
            {
                if ( tx.vin[j].prevout.hash == array[k] && komodo_checkvout(tx.vin[j].prevout.n,k,indallvouts) )
                {
                    // hash matches and the vout.n matches
                    static uint32_t counter;
                    if ( counter++ < 100 )
                        printf("MEMPOOL: banned tx.%d being used at ht.%d vout.%ld\n",k,(int32_t)chainActive.Tip()->nHeight,j);
                    return false;
                }
            }
        }
    }

    uint256 merkleroot;
    if ( ASSETCHAINS_STAKED != 0 && komodo_newStakerActive(0, tiptime) != 0 && tx.vout.size() == 2 && DecodeStakingOpRet(tx.vout[1].scriptPubKey, merkleroot) != 0 )
    {
        if ( numTxs == 0 || txIndex != numTxs-1 )
        {
            return state.DoS(100, error("CheckTransaction(): staking tx is not staking a block"),
                            REJECT_INVALID, "bad-txns-stakingtx");
        }
    }

    // Don't count coinbase transactions because mining skews the count
    if (!tx.IsCoinBase()) {
        transactionsValidated.increment();
    }

    if (!CheckTransactionWithoutProofVerification(tiptime,tx, state)) {
        return false;
    } else {
        // Ensure that zk-SNARKs v|| y
        BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit) {
            if (!verifier.VerifySprout(joinsplit, tx.joinSplitPubKey)) {
                return state.DoS(100, error("CheckTransaction(): joinsplit does not verify"),
                                 REJECT_INVALID, "bad-txns-joinsplit-verification-failed");
            }
        }
        return true;
    }
}

int32_t komodo_acpublic(uint32_t tiptime);

bool CheckTransactionWithoutProofVerification(uint32_t tiptime,const CTransaction& tx, CValidationState &state)
{
    // Basic checks that don't depend on any context
    int32_t invalid_private_taddr=0,z_z=0,z_t=0,t_z=0,acpublic = komodo_acpublic(tiptime), current_season = getacseason(tiptime);
    /**
     * Previously:
     * 1. The consensus rule below was:
     *        if (tx.nVersion < SPROUT_MIN_TX_VERSION) { ... }
     *    which checked if tx.nVersion fell within the range:
     *        INT32_MIN <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The parser allowed tx.nVersion to be negative
     *
     * Now:
     * 1. The consensus rule checks to see if tx.Version falls within the range:
     *        0 <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The previous consensus rule checked for negative values within the range:
     *        INT32_MIN <= tx.nVersion < 0
     *    This is unnecessary for Overwinter transactions since the parser now
     *    interprets the sign bit as fOverwintered, so tx.nVersion is always >=0,
     *    and when Overwinter is not active ContextualCheckTransaction rejects
     *    transactions with fOverwintered set.  When fOverwintered is set,
     *    this function and ContextualCheckTransaction will together check to
     *    ensure tx.nVersion avoids the following ranges:
     *        0 <= tx.nVersion < OVERWINTER_MIN_TX_VERSION
     *        OVERWINTER_MAX_TX_VERSION < tx.nVersion <= INT32_MAX
     */
    if (!tx.fOverwintered && tx.nVersion < SPROUT_MIN_TX_VERSION) {
        return state.DoS(100, error("CheckTransaction(): version too low"),
                         REJECT_INVALID, "bad-txns-version-too-low");
    }
    else if (tx.fOverwintered) {
        if (tx.nVersion < OVERWINTER_MIN_TX_VERSION) {
            return state.DoS(100, error("CheckTransaction(): overwinter version too low"),
                             REJECT_INVALID, "bad-tx-overwinter-version-too-low");
        }
        if (tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID &&
            tx.nVersionGroupId != SAPLING_VERSION_GROUP_ID &&
            tx.nVersionGroupId != ORCHARD_VERSION_GROUP_ID ) {
            return state.DoS(100, error("CheckTransaction(): unknown tx version group id"),
                             REJECT_INVALID, "bad-tx-version-group-id");
        }
        if (tx.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
            return state.DoS(100, error("CheckTransaction(): expiry height is too high"),
                             REJECT_INVALID, "bad-tx-expiry-height-too-high");
        }
    }

    auto orchard_bundle = tx.GetOrchardBundle();

    // Transactions containing empty `vin` must have either non-empty
    // `vjoinsplit` or non-empty `vShieldedSpend`.
    if (tx.vin.empty() &&
        tx.vjoinsplit.empty() &&
        tx.GetSaplingSpendsCount() == 0 &&
        !orchard_bundle.SpendsEnabled())
        return state.DoS(10, error("CheckTransaction(): vin empty"),
                         REJECT_INVALID, "bad-txns-vin-empty");

    // Transactions containing empty `vout` must have either non-empty
    // `vjoinsplit` or non-empty `vShieldedOutput`.
    if (tx.vout.empty() &&
        tx.vjoinsplit.empty() &&
        tx.GetSaplingOutputsCount() == 0 &&
        !orchard_bundle.OutputsEnabled()) {
        return state.DoS(10, error("CheckTransaction(): vout empty"),REJECT_INVALID, "bad-txns-vout-empty");
    }

    // Size limits
    BOOST_STATIC_ASSERT(MAX_TX_SIZE_AFTER_SAPLING > MAX_TX_SIZE_BEFORE_SAPLING); // sanity
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_AFTER_SAPLING)
        return state.DoS(100, error("CheckTransaction(): size limits failed"),
                         REJECT_INVALID, "bad-txns-oversize");

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    int32_t iscoinbase = tx.IsCoinBase();
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        if (txout.nValue < 0) {
            return state.DoS(100, error("CheckTransaction(): txout.nValue negative"),REJECT_INVALID, "bad-txns-vout-negative");
        }
        if (txout.nValue > MAX_MONEY) {
            fprintf(stderr,"%.8f > max %.8f\n",(double)txout.nValue/COIN,(double)MAX_MONEY/COIN);
            return state.DoS(100, error("CheckTransaction(): txout.nValue too high"),REJECT_INVALID, "bad-txns-vout-toolarge");
        }
        if ( ASSETCHAINS_PRIVATE != 0 )
        {
            //fprintf(stderr,"private chain nValue %.8f iscoinbase.%d\n",(double)txout.nValue/COIN,iscoinbase);
            if (iscoinbase == 0 && txout.nValue > 0)
            {
                // TODO: if we are upgraded to Sapling, we can allow Sprout sourced funds to sit in a transparent address
                //
                char destaddr[65];
                Getscriptaddress(destaddr,txout.scriptPubKey);
                if ( komodo_isnotaryvout(destaddr,tiptime) == 0 )
                {
                    invalid_private_taddr = 1;
                    //return state.DoS(100, error("CheckTransaction(): this is a private chain, no public allowed"),REJECT_INVALID, "bad-txns-acprivacy-chain");
                }
            }
        }
        if ( txout.scriptPubKey.size() > IGUANA_MAXSCRIPTSIZE )
            return state.DoS(100, error("CheckTransaction(): txout.scriptPubKey.size() too big"),REJECT_INVALID, "bad-txns-opret-too-big");
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
    }


    // Check for non-zero valueBalanceSapling when there are no Sapling inputs or outputs
    if (tx.GetSaplingSpendsCount() == 0 && tx.GetSaplingOutputsCount() == 0 && tx.GetValueBalanceSapling() != 0)
    {
        return state.DoS(100, error("CheckTransaction(): tx.valueBalanceSapling has no sources or sinks"),
                            REJECT_INVALID, "bad-txns-valuebalance-nonzero");
    }

    //Check for Sapling in a non-private asset chain
    if (acpublic != 0 && (!(tx.GetSaplingSpendsCount() == 0) || !(tx.GetSaplingOutputsCount() == 0))) {
        return state.DoS(100, error("CheckTransaction(): this is a public chain, no sapling allowed"),
                         REJECT_INVALID, "bad-txns-acpublic-chain-sapling");
    }

    if (acpublic != 0 && (orchard_bundle.SpendsEnabled() || orchard_bundle.OutputsEnabled())) {
        return state.DoS(100, error("CheckTransaction(): this is a public chain, no orchard allowed"),
                         REJECT_INVALID, "bad-txns-acpublic-chain-orchard");
    }

    if ( ASSETCHAINS_PRIVATE != 0 && invalid_private_taddr != 0 && tx.GetSaplingSpendsCount() > 0 )
    {
        if ( !( current_season > 5 &&
                tx.vin.size() == 0 &&
                tx.vout.size() == 2 &&
                tx.vout[0].scriptPubKey.IsPayToScriptHash() &&
                tx.vout[0].scriptPubKey.IsRedeemScriptReveal(tx.vout[1].scriptPubKey) )) {
                    return state.DoS(100, error("CheckTransaction(): this is a private chain, no sapling -> taddr"),
                                     REJECT_INVALID, "bad-txns-acprivate-chain");
                } else {
                    invalid_private_taddr = false;
                }
    }

    // Check for overflow valueBalanceSapling
    if (tx.GetValueBalanceSapling() > MAX_MONEY || tx.GetValueBalanceSapling() < -MAX_MONEY) {
        return state.DoS(100, error("CheckTransaction(): abs(tx.valueBalanceSapling) too large"),
                            REJECT_INVALID, "bad-txns-valuebalance-toolarge");
    }

    if (tx.GetValueBalanceSapling() <= 0) {
        // NB: negative valueBalanceSapling "takes" money from the transparent value pool just as outputs do
        nValueOut += -tx.GetValueBalanceSapling();

        if (!MoneyRange(nValueOut)) {
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                                REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
    }

    // nSpendsSapling, nOutputsSapling, and nActionsOrchard MUST all be less than 2^16
    size_t max_elements = (1 << 16) - 1;
    if (tx.GetSaplingSpendsCount() > max_elements) {
        return state.DoS(
            100,
            error("CheckTransaction(): 2^16 or more Sapling spends"),
            REJECT_INVALID, "bad-tx-too-many-sapling-spends");
    }
    if (tx.GetSaplingOutputsCount() > max_elements) {
        return state.DoS(
            100,
            error("CheckTransaction(): 2^16 or more Sapling outputs"),
            REJECT_INVALID, "bad-tx-too-many-sapling-outputs");
    }
    if (orchard_bundle.GetNumActions() > max_elements) {
        return state.DoS(
            100,
            error("CheckTransaction(): 2^16 or more Orchard actions"),
            REJECT_INVALID, "bad-tx-too-many-orchard-actions");
    }

    // Check that if neither Orchard spends nor outputs are enabled, the transaction contains
    // no Orchard actions. This subsumes the check that valueBalanceOrchard must equal zero
    // in the case that both spends and outputs are disabled.
    if (orchard_bundle.GetNumActions() > 0 && !orchard_bundle.OutputsEnabled() && !orchard_bundle.SpendsEnabled()) {
        return state.DoS(
            100,
            error("CheckTransaction(): Orchard actions are present, but flags do not permit Orchard spends or outputs"),
            REJECT_INVALID, "bad-tx-orchard-flags-disable-actions");
    }

    auto valueBalanceOrchard = orchard_bundle.GetValueBalance();

    // Check for overflow valueBalanceOrchard
    if (valueBalanceOrchard > MAX_MONEY || valueBalanceOrchard < -MAX_MONEY) {
        return state.DoS(100, error("CheckTransaction(): abs(tx.valueBalanceOrchard) too large"),
                         REJECT_INVALID, "bad-txns-valuebalance-toolarge");
    }

    if (valueBalanceOrchard <= 0) {
        // NB: negative valueBalanceOrchard "takes" money from the transparent value pool just as outputs do
        nValueOut += -valueBalanceOrchard;

        if (!MoneyRange(nValueOut)) {
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
    }

    // Ensure that joinsplit values are well-formed
    BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit)
    {
        if ( acpublic != 0 )
        {
            return state.DoS(100, error("CheckTransaction(): this is a public chain, no privacy allowed"),
                             REJECT_INVALID, "bad-txns-acpublic-chain");
        }
        if ( tiptime >= KOMODO_SAPLING_DEADLINE )
        {
            return state.DoS(100, error("CheckTransaction(): no more sprout after deadline"),
                             REJECT_INVALID, "bad-txns-sprout-expired");
        }
        if (joinsplit.vpub_old < 0) {
            return state.DoS(100, error("CheckTransaction(): joinsplit.vpub_old negative"),
                             REJECT_INVALID, "bad-txns-vpub_old-negative");
        }

        if (joinsplit.vpub_new < 0) {
            return state.DoS(100, error("CheckTransaction(): joinsplit.vpub_new negative"),
                             REJECT_INVALID, "bad-txns-vpub_new-negative");
        }

        if (joinsplit.vpub_old > MAX_MONEY) {
            return state.DoS(100, error("CheckTransaction(): joinsplit.vpub_old too high"),
                             REJECT_INVALID, "bad-txns-vpub_old-toolarge");
        }

        if (joinsplit.vpub_new > MAX_MONEY) {
            return state.DoS(100, error("CheckTransaction(): joinsplit.vpub_new too high"),
                             REJECT_INVALID, "bad-txns-vpub_new-toolarge");
        }

        if (joinsplit.vpub_new != 0 && joinsplit.vpub_old != 0) {
            return state.DoS(100, error("CheckTransaction(): joinsplit.vpub_new and joinsplit.vpub_old both nonzero"),
                             REJECT_INVALID, "bad-txns-vpubs-both-nonzero");
        }

        nValueOut += joinsplit.vpub_old;
        if (!MoneyRange(nValueOut)) {
            return state.DoS(100, error("CheckTransaction(): txout total out of range"),
                             REJECT_INVALID, "bad-txns-txouttotal-toolarge");
        }
        if ( joinsplit.vpub_new == 0 && joinsplit.vpub_old == 0 )
            z_z++;
        else if ( joinsplit.vpub_new == 0 && joinsplit.vpub_old != 0 )
            t_z++;
        else if ( joinsplit.vpub_new != 0 && joinsplit.vpub_old == 0 )
            z_t++;
    }
    if ( ASSETCHAINS_PRIVATE != 0 && invalid_private_taddr != 0 )
    {
        static uint32_t counter;
        if ( counter++ < 10 )
            fprintf(stderr,"found taddr in private chain: z_z.%d z_t.%d t_z.%d vinsize.%d\n",z_z,z_t,t_z,(int32_t)tx.vin.size());
        if ( z_t == 0 || z_z != 0 || t_z != 0 || tx.vin.size() != 0 )
            return state.DoS(100, error("CheckTransaction(): this is a private chain, only sprout -> taddr allowed until deadline"),REJECT_INVALID, "bad-txns-acprivacy-chain");
    }
    if ( ASSETCHAINS_TXPOW != 0 )
    {
        // genesis coinbase 4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b
        uint256 txid = tx.GetHash();
        if ( ((ASSETCHAINS_TXPOW & 2) != 0 && iscoinbase != 0) || ((ASSETCHAINS_TXPOW & 1) != 0 && iscoinbase == 0) )
        {
            if ( ((uint8_t *)&txid)[0] != 0 || ((uint8_t *)&txid)[31] != 0 )
            {
                uint256 genesistxid = uint256S("4a5e1e4baab89f3a32518a88c31bc87f618f76673e2cc77ab2127b7afdeda33b");
                if ( txid != genesistxid )
                {
                    fprintf(stderr,"private chain iscoinbase.%d invalid txpow.%d txid.%s\n",iscoinbase,ASSETCHAINS_TXPOW,txid.GetHex().c_str());
                    return state.DoS(100, error("CheckTransaction(): this is a txpow chain, must have 0x00 ends"),REJECT_INVALID, "bad-txns-actxpow-chain");
                }
            }
        }
    }

    // Ensure input values do not exceed MAX_MONEY// ARRR notary exception
    // We have not resolved the txin values at this stage,
    // but we do know what the joinsplits claim to add
    // to the value pool.
    {
        CAmount nValueIn = 0;
        for (std::vector<JSDescription>::const_iterator it(tx.vjoinsplit.begin()); it != tx.vjoinsplit.end(); ++it)
        {
            nValueIn += it->vpub_new;

            if (!MoneyRange(it->vpub_new) || !MoneyRange(nValueIn)) {
                return state.DoS(100, error("CheckTransaction(): txin total out of range"),
                                 REJECT_INVALID, "bad-txns-txintotal-toolarge");
            }
        }

        // Also check for Sapling
        if (tx.GetValueBalanceSapling() >= 0) {
            // NB: positive valueBalanceSapling "adds" money to the transparent value pool, just as inputs do
            nValueIn += tx.GetValueBalanceSapling();

            if (!MoneyRange(nValueIn)) {
                return state.DoS(100, error("CheckTransaction(): txin total out of range"),
                                 REJECT_INVALID, "bad-txns-txintotal-toolarge");
            }
        }

        // Also check for Orchard
        if (valueBalanceOrchard >= 0) {
            // NB: positive valueBalanceOrchard "adds" money to the transparent value pool, just as inputs do
            nValueIn += valueBalanceOrchard;

            if (!MoneyRange(nValueIn)) {
                return state.DoS(100, error("CheckTransaction(): txin total out of range"),
                                    REJECT_INVALID, "bad-txns-txintotal-toolarge");
            }
        }
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return state.DoS(100, error("CheckTransaction(): duplicate inputs"),
                             REJECT_INVALID, "bad-txns-inputs-duplicate");
        vInOutPoints.insert(txin.prevout);
    }

    // Check for duplicate joinsplit nullifiers in this transaction
    {
        set<uint256> vJoinSplitNullifiers;
        BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit)
        {
            BOOST_FOREACH(const uint256& nf, joinsplit.nullifiers)
            {
                if (vJoinSplitNullifiers.count(nf))
                    return state.DoS(100, error("CheckTransaction(): duplicate nullifiers"),
                                REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate");

                vJoinSplitNullifiers.insert(nf);
            }
        }
    }

    // Check for duplicate sapling nullifiers in this transaction
    {
      set<uint256> vSaplingNullifiers;
        for (const uint256& nf : tx.GetSaplingBundle().GetNullifiers())
        {
            if (vSaplingNullifiers.count(nf))
                return state.DoS(100, error("CheckTransaction(): duplicate nullifiers"),
                            REJECT_INVALID, "bad-spend-description-nullifiers-duplicate");

            vSaplingNullifiers.insert(nf);
        }
    }

    // Check for duplicate orchard nullifiers in this transaction
    {
        std::set<uint256> vOrchardNullifiers;
        for (const uint256& nf : tx.GetOrchardBundle().GetNullifiers())
        {
            if (vOrchardNullifiers.count(nf))
                return state.DoS(100, error("CheckTransaction(): duplicate nullifiers"),
                            REJECT_INVALID, "bad-orchard-nullifiers-duplicate");

            vOrchardNullifiers.insert(nf);
        }
    }

    if (tx.IsMint())
    {
        // There should be no joinsplits in a coinbase transaction
        if (tx.vjoinsplit.size() > 0)
            return state.DoS(100, error("CheckTransaction(): coinbase has joinsplits"),
                             REJECT_INVALID, "bad-cb-has-joinsplits");

        // A coinbase transaction cannot have spend descriptions or output descriptions
        if (tx.GetSaplingSpendsCount() > 0)
            return state.DoS(100, error("CheckTransaction(): coinbase has spend descriptions"),
                             REJECT_INVALID, "bad-cb-has-spend-description");
        if (tx.GetSaplingOutputsCount() > 0)
            return state.DoS(100, error("CheckTransaction(): coinbase has output descriptions"),
                             REJECT_INVALID, "bad-cb-has-output-description");

        if (orchard_bundle.SpendsEnabled())
            return state.DoS(100, error("CheckTransaction(): coinbase has enableSpendsOrchard set"),
                             REJECT_INVALID, "bad-cb-has-orchard-spend");

        if (orchard_bundle.OutputsEnabled())
           return state.DoS(100, error("CheckTransaction(): coinbase has enableOutputsOrchard set"),
                            REJECT_INVALID, "bad-cb-has-orchard-output");

        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
            return state.DoS(100, error("CheckTransaction(): coinbase script size"),
                             REJECT_INVALID, "bad-cb-length");
    }
    else
    {
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        if (txin.prevout.IsNull())
            return state.DoS(10, error("CheckTransaction(): prevout is null"),
                             REJECT_INVALID, "bad-txns-prevout-null");
    }

    return true;
}

CAmount GetMinRelayFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree)
{
    {
        LOCK(mempool.cs);
        uint256 hash = tx.GetHash();
        double dPriorityDelta = 0;
        CAmount nFeeDelta = 0;
        mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
        if (dPriorityDelta > 0 || nFeeDelta > 0)
            return 0;
    }

    CAmount nMinFee = ::minRelayTxFee.GetFee(nBytes);

    if (fAllowFree)
    {
        // There is a free transaction area in blocks created by most miners,
        // * If we are relaying we allow transactions up to DEFAULT_BLOCK_PRIORITY_SIZE - 1000
        //   to be considered to fall into this category. We don't want to encourage sending
        //   multiple transactions instead of one big transaction to avoid fees.
        if (nBytes < (DEFAULT_BLOCK_PRIORITY_SIZE - 1000))
            nMinFee = 0;
    }

    if (!MoneyRange(nMinFee))
        nMinFee = MAX_MONEY;
    return nMinFee;
}

/*****
 * @brief Try to add transaction to memory pool
 * @param pool
 * @param state
 * @param tx
 * @param fLimitFree
 * @param pfMissingInputs
 * @param fRejectAbsurdFee
 * @param dosLevel
 * @returns true on success
 */
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,bool* pfMissingInputs, bool fRejectAbsurdFee, int dosLevel)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs != nullptr)
        *pfMissingInputs = false;
    uint32_t tiptime;
    int nextBlockHeight = chainActive.Height() + 1;
    auto consensusBranchId = CurrentEpochBranchId(nextBlockHeight, Params().GetConsensus());
    if ( nextBlockHeight <= 1 || chainActive.Tip() == 0 )
        tiptime = (uint32_t)time(NULL);
    else
        tiptime = (uint32_t)chainActive.Tip()->nTime;
//fprintf(stderr,"addmempool 0\n");
    // Node operator can choose to reject tx by number of transparent inputs
    static_assert(std::numeric_limits<size_t>::max() >= std::numeric_limits<int64_t>::max(), "size_t too small");
    size_t limit = (size_t) GetArg("-mempooltxinputlimit", 0);
    if (NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
        limit = 0;
    }
    if (limit > 0) {
        size_t n = tx.vin.size();
        if (n > limit) {
            LogPrint("mempool", "Dropping txid %s : too many transparent inputs %zu > limit %zu\n", tx.GetHash().ToString(), n, limit );
            return false;
        }
    }


    // Check for duplicate sapling zkproofs in this transaction (mempool only) - move to CheckTransaction to enforce at consensus
    {
        set<libzcash::GrothProof> vSaplingOutputProof;
        for (const auto& output : tx.GetSaplingSpends()) {
            libzcash::GrothProof zkproof;
            auto rustZKProok = output.zkproof();
            std::memcpy(&zkproof, &rustZKProok, 192);

            if (vSaplingOutputProof.count(zkproof))
                return state.Invalid(error("AcceptToMemoryPool: duplicate proof requirments requirements not met"),REJECT_DUPLICATE_PROOF, "bad-txns-duplicate-proof-requirements-not-met");

            vSaplingOutputProof.insert(zkproof);
        }
    }

    // Check for duplicate sapling zkproofs in this transaction (mempool only) - move to CheckTransaction to enforce at consensus
    {
        set<libzcash::GrothProof> vSaplingSpendProof;
        for (const auto& spend : tx.GetSaplingSpends()) {
            libzcash::GrothProof zkproof;
            auto rustZKProok = spend.zkproof();
            std::memcpy(&zkproof, &rustZKProok, 192);

            if (vSaplingSpendProof.count(zkproof))
                return state.Invalid(error("AcceptToMemoryPool: duplicate proof requirments requirements not met"),REJECT_DUPLICATE_PROOF, "bad-txns-duplicate-proof-requirements-not-met");

            vSaplingSpendProof.insert(zkproof);
        }
    }

    auto verifier = ProofVerifier::Strict();
    if (chainName.isKMD() && chainActive.Tip() != nullptr
            && !komodo_validate_interest(tx, chainActive.Tip()->nHeight + 1, chainActive.Tip()->GetMedianTimePast() + 777))
    {
        return state.DoS(0, error("%s: komodo_validate_interest failed txid.%s", __func__, tx.GetHash().ToString()), REJECT_INVALID, "komodo-interest-invalid");
    }

    if (!CheckTransaction(tiptime,tx, state, verifier, 0, 0))
    {
        return error("AcceptToMemoryPool: CheckTransaction failed");
    }

    // DoS mitigation: reject transactions expiring soon
    // Note that if a valid transaction belonging to the wallet is in the mempool and the node is shutdown,
    // upon restart, CWalletTx::AcceptToMemoryPool() will be invoked which might result in rejection.
    if (IsExpiringSoonTx(tx, nextBlockHeight)) {
        return state.DoS(0, error("AcceptToMemoryPool(): transaction is expiring soon"), REJECT_INVALID, "tx-expiring-soon");
    }

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
    {
        fprintf(stderr,"AcceptToMemoryPool coinbase as individual tx\n");
        return state.DoS(100, error("AcceptToMemoryPool: coinbase as individual tx"),REJECT_INVALID, "coinbase");
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (Params().RequireStandard() && !IsStandardTx(tx, reason, nextBlockHeight))
    {
        return state.DoS(0,error("AcceptToMemoryPool: nonstandard transaction: %s", reason),REJECT_NONSTANDARD, reason);
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
    {
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");
    }

    // is it already in the memory pool? Do this before the more cpu intesive zkp validations
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
    {
        return state.Invalid(false, REJECT_DUPLICATE, "already in mempool");
    }

    // Check for conflicts with in-memory transactions
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            COutPoint outpoint = tx.vin[i].prevout;
            if (pool.mapNextTx.count(outpoint))
            {
                // Disable replacement feature for now
                return state.Invalid(false, REJECT_INVALID, "mempool conflict");
            }
        }
        //Check for duplicate Nullifers
        for (const JSDescription &joinsplit : tx.vjoinsplit) {
            for (const uint256 &nf : joinsplit.nullifiers) {
                if (pool.nullifierExists(nf, SPROUT)) {
                    return state.Invalid(error("AcceptToMemoryPool: duplicate nullifier requirments requirements not met"),REJECT_DUPLICATE_PROOF, "bad-txns-duplicate-nullifier-requirements-not-met");
                }
            }
        }
        for (const uint256& nf : tx.GetSaplingBundle().GetNullifiers()) {
            if (pool.nullifierExists(nf, SAPLING)) {
                return state.Invalid(error("AcceptToMemoryPool: duplicate nullifier requirments requirements not met"),REJECT_DUPLICATE_PROOF, "bad-txns-duplicate-nullifier-requirements-not-met");
            }
        }
        for (const uint256& nf : tx.GetOrchardBundle().GetNullifiers()) {
            if (pool.nullifierExists(nf, ORCHARDFRONTIER)) {
                return state.Invalid(error("AcceptToMemoryPool: duplicate nullifier requirments requirements not met"),REJECT_DUPLICATE_PROOF, "bad-txns-duplicate-nullifier-requirements-not-met");
            }
        }
    }

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);
        int64_t interest;
        CAmount nValueIn = 0;
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(hash))
            {
                return state.Invalid(false, REJECT_DUPLICATE, "already have coins");
            }

            if (tx.IsCoinImport())
            {
                // Inverse of normal case; if input exists, it's been spent
                if (ExistsImportTombstone(tx, view))
                    return state.Invalid(false, REJECT_DUPLICATE, "import tombstone exists");
            }
            else
            {
                // do all inputs exist?
                // Note that this does not check for the presence of actual outputs (see the next check for that),
                // and only helps with filling in pfMissingInputs (to determine missing vs spent).
                BOOST_FOREACH(const CTxIn txin, tx.vin)
                {
                    if (!view.HaveCoins(txin.prevout.hash))
                    {
                        if (pfMissingInputs)
                            *pfMissingInputs = true;
                        return false;
                        /*
                            https://github.com/zcash/zcash/blob/master/src/main.cpp#L1490
                            state.DoS(0, error("AcceptToMemoryPool: tx inputs not found"),REJECT_INVALID, "bad-txns-inputs-missing");
                        */
                    }
                }
                // are the actual inputs available?
                if (!view.HaveInputs(tx))
                {
                    return state.Invalid(error("AcceptToMemoryPool: inputs already spent"),REJECT_DUPLICATE, "bad-txns-inputs-spent");
                }
            }

            // are the joinsplit's requirements met?
            if (!view.HaveJoinSplitRequirements(tx, maxProcessingThreads))
            {
                return state.Invalid(error("AcceptToMemoryPool: joinsplit requirements not met"),REJECT_DUPLICATE, "bad-txns-joinsplit-requirements-not-met");
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(GetHeight(),interest,tx);
            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        }
        // Check for non-standard pay-to-script-hash in inputs
        if (Params().RequireStandard() && !AreInputsStandard(tx, view, consensusBranchId))
            return error("AcceptToMemoryPool: reject nonstandard transaction input");

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);
        if (nSigOps > MAX_STANDARD_TX_SIGOPS)
        {
            fprintf(stderr,"accept failure.4\n");
            return state.DoS(1, error("AcceptToMemoryPool: too many sigops %s, %d > %d", hash.ToString(), nSigOps, MAX_STANDARD_TX_SIGOPS),REJECT_NONSTANDARD, "bad-txns-too-many-sigops");
        }

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn-nValueOut;
        double dPriority = view.GetPriority(tx, chainActive.Height());
        if ( nValueOut > 777777*COIN && KOMODO_VALUETOOBIG(nValueOut - 777777*COIN) != 0 ) // some room for blockreward and txfees
            return state.DoS(100, error("AcceptToMemoryPool: GetValueOut too big"),REJECT_INVALID,"tx valueout is too big");

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        if (!tx.IsCoinImport()) {
            BOOST_FOREACH(const CTxIn &txin, tx.vin) {
                const CCoins *coins = view.AccessCoins(txin.prevout.hash);
                if (coins->IsCoinBase()) {
                    fSpendsCoinbase = true;
                    break;
                }
            }
        }
        // Grab the branch ID we expect this transaction to commit to. We don't
        // yet know if it does, but if the entry gets added to the mempool, then
        // it has passed ContextualCheckInputs and therefore this is correct.
        auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainActive.Height(), mempool.HasNoInputsOf(tx), fSpendsCoinbase, consensusBranchId);
        unsigned int nSize = entry.GetTxSize();

        // Accept a tx if it contains joinsplits and has at least the default fee specified by z_sendmany.
        if (tx.vjoinsplit.size() > 0 && nFees >= ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE) {
            // In future we will we have more accurate and dynamic computation of fees for tx with joinsplits.
        } else {
            // Don't accept it if it can't get into a block
            CAmount txMinFee = GetMinRelayFee(tx, nSize, true);
            if (fLimitFree && nFees < txMinFee)
            {
                //fprintf(stderr,"accept failure.5\n");
                return state.DoS(0, error("AcceptToMemoryPool: not enough fees %s, %d < %d",hash.ToString(), nFees, txMinFee),REJECT_INSUFFICIENTFEE, "insufficient fee");
            }
        }

        // Require that free transactions have sufficient priority to be mined in the next block.
        if (GetBoolArg("-relaypriority", false) && nFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(view.GetPriority(tx, chainActive.Height() + 1))) {
            fprintf(stderr,"accept failure.6\n");
            return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize) && !tx.IsCoinImport())
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", 15)*10*1000)
            {
                fprintf(stderr,"accept failure.7\n");
                return state.DoS(0, error("AcceptToMemoryPool: free transaction rejected by rate limiter"), REJECT_INSUFFICIENTFEE, "rate limited free transaction");
            }
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
            dFreeCount += nSize;
        }

        if (!tx.IsCoinImport() && fRejectAbsurdFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000 && nFees > nValueOut/19)
        {
            string errmsg = strprintf("absurdly high fees %s, %d > %d",
                                      hash.ToString(),
                                      nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
            LogPrint("mempool", errmsg.c_str());
            return state.Error("AcceptToMemoryPool: " + errmsg);
        }

        // Check against previous transactions
        // This is done near the end to help prevent CPU exhaustion denial-of-service attacks.
        std::vector<CTxOut> allPrevOutputs;
        if (!tx.IsMint()) {
          for (const auto& input : tx.vin) {
              allPrevOutputs.push_back(view.GetOutputFor(input));
          }
        }

        PrecomputedTransactionData txdata(tx, allPrevOutputs);
        if (!ContextualCheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS, true, txdata, Params().GetConsensus(), consensusBranchId))
        {
            //fprintf(stderr,"accept failure.9\n");
            return error("AcceptToMemoryPool: ConnectInputs failed %s", hash.ToString());
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        // XXX: is this neccesary for CryptoConditions?
        bool komodoConnectingSet = false;
        if ( KOMODO_CONNECTING <= 0 && chainActive.Tip() != 0 )
        {
            // set KOMODO_CONNECTING so that ContextualCheckInputs works, (don't forget to reset)
            komodoConnectingSet = true;
            KOMODO_CONNECTING = (1<<30) + (int32_t)chainActive.Tip()->nHeight + 1;
        }

        if (!ContextualCheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, txdata, Params().GetConsensus(), consensusBranchId))
        {
            if ( komodoConnectingSet ) // undo what we did
                KOMODO_CONNECTING = -1;
            return error("AcceptToMemoryPool: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s", hash.ToString());
        }
        if ( komodoConnectingSet )
            KOMODO_CONNECTING = -1;

        // Do this step last before adding it to the mempool - this is the most CPU intensive step and validates the zkproofs.
        // Eliminate transactions with all other types of failures before doing this validation.
        // DoS level set to 10 to be more forgiving.
        // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
        std::vector<const CTransaction*> vptx;
        vptx.emplace_back(&tx);
        if (!ContextualCheckTransactionMultithreaded(0, vptx, view, 0, state, nextBlockHeight, (dosLevel == -1) ? 10 : dosLevel))
        {
            return error("AcceptToMemoryPool: ContextualCheckTransaction failed");
        }

        //Add to mempool
        {
            LOCK(pool.cs);
            // Store transaction in memory
            pool.addUnchecked(hash, entry, !IsInitialBlockDownload());
            if (!tx.IsCoinImport())
            {
                // Add memory address index
                if (fAddressIndex) {
                    pool.addAddressIndex(entry, view);
                }

                // Add memory spent index
                if (fSpentIndex) {
                    pool.addSpentIndex(entry, view);
                }
            }
        }
    }
    return true;
}

/****
 * @brief Add a transaction to the memory pool without the checks of AcceptToMemoryPool
 * @param pool the memory pool to add the transaction to
 * @param tx the transaction
 * @returns true
 */
bool CCTxFixAcceptToMemPoolUnchecked(CTxMemPool& pool, const CTransaction &tx)
{
    // called from CheckBlock which is in cs_main and mempool.cs locks already.
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    CTxMemPoolEntry entry(tx, 0, GetTime(), 0, chainActive.Height(),
            mempool.HasNoInputsOf(tx), false, consensusBranchId);
    pool.addUnchecked(tx.GetHash(), entry, false);
    return true;
}

bool GetTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes)
{
    if (!fTimestampIndex)
        return error("Timestamp index not enabled");

    if (!pblocktree->ReadTimestampIndex(high, low, fActiveOnly, hashes))
        return error("Unable to get hashes for timestamps");

    return true;
}

bool GetSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value)
{
    if (!fSpentIndex)
        return false;

    if (mempool.getSpentIndex(key, value))
        return true;

    if (!pblocktree->ReadSpentIndex(key, value))
        return false;

    return true;
}

bool GetAddressIndex(uint160 addressHash, int type,
                     std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex, int start, int end)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressIndex(addressHash, type, addressIndex, start, end))
        return error("unable to get txids for address");

    return true;
}

bool GetAddressUnspent(uint160 addressHash, int type,
                       std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs)
{
    if (!fAddressIndex)
        return error("address index not enabled");

    if (!pblocktree->ReadAddressUnspentIndex(addressHash, type, unspentOutputs))
        return error("unable to get txids for address");

    return true;
}

/****
 * @brief add a transaction to the mempool
 * @param[in] tx the transaction
 * @param pstate where to store any error (can be nullptr)
 * @param fSkipExpiry set to false to add to pool without many checks
 * @returns true on success
 */
bool myAddtomempool(const CTransaction &tx, CValidationState *pstate, bool fSkipExpiry)
{
    CValidationState state;
    if (pstate == nullptr)
        pstate = &state;

    CTransaction Ltx;
    if ( mempool.lookup(tx.GetHash(),Ltx) == false ) // does not already exist
    {
        if ( !fSkipExpiry )
        {
            bool fMissingInputs;
            bool fOverrideFees = false;
            return(AcceptToMemoryPool(mempool, *pstate, tx, false, &fMissingInputs, !fOverrideFees, -1));
        }
        else
            return(CCTxFixAcceptToMemPoolUnchecked(mempool,tx));
    }
    return true;
}

/*****
 * @brief get a transaction by its hash (without locks)
 * @param[in] hash what to look for
 * @param[out] txOut the found transaction
 * @param[out] hashBlock the hash of the block (all zeros if still in mempool)
 * @returns true if found
 */
bool myGetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock)
{
    memset(&hashBlock,0,sizeof(hashBlock));
    if ( KOMODO_NSPV_SUPERLITE )
    {
        int64_t rewardsum = 0;
        int32_t retval,txheight,currentheight,height=0,vout = 0;
        for (uint16_t i=0; i<NSPV_U.U.numutxos; i++)
            if ( NSPV_U.U.utxos[i].txid == hash )
            {
                height = NSPV_U.U.utxos[i].height;
                break;
            }
        retval = NSPV_gettransaction(1,vout,hash,height,txOut,hashBlock,txheight,currentheight,0,0,rewardsum);
        return(retval != -1);
    }
    // need a GetTransaction without lock so the validation code for assets can run without deadlock
    {
        if (mempool.lookup(hash, txOut))
        {
            return true;
        }
    }

    if (fTxIndex) // if we have a transaction index
    {
        // transaction was not in mempool. Look through the blocks
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx))
        {
            // Found the transaction in the index. Load the block to get the block hash
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }
    return false;
}

bool NSPV_myGetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock, int32_t &txheight, int32_t &currentheight)
{
    memset(&hashBlock,0,sizeof(hashBlock));
    if ( KOMODO_NSPV_SUPERLITE )
    {
        int64_t rewardsum = 0; int32_t i,retval,height=0,vout = 0;
        for (i=0; i<NSPV_U.U.numutxos; i++)
            if ( NSPV_U.U.utxos[i].txid == hash )
            {
                height = NSPV_U.U.utxos[i].height;
                break;
            }
        retval = NSPV_gettransaction(1,vout,hash,height,txOut,hashBlock,txheight,currentheight,0,0,rewardsum);
        return(retval != -1);
    }
    return false;
}

/**
 * @brief Find a transaction (uses locks)
 * @param[in] hash the transaction to look for
 * @param[out] txOut the transaction found
 * @param[out] hashBlock the block where the transaction was found (all zeros if found in mempool)
 * @param[in] fAllowSlow true to continue searching even if there are no transaction indexes
 * @returns true if found
 */
bool GetTransaction(const uint256 &hash, CTransaction &txOut, uint256 &hashBlock, bool fAllowSlow)
{
    memset(&hashBlock,0,sizeof(hashBlock));

    LOCK(cs_main);

    if (mempool.lookup(hash, txOut))
    {
        return true;
    }

    if (fTxIndex) {
        CDiskTxPos postx;
        if (pblocktree->ReadTxIndex(hash, postx)) {
            CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
            if (file.IsNull())
                return error("%s: OpenBlockFile failed", __func__);
            CBlockHeader header;
            try {
                file >> header;
                fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                file >> txOut;
            } catch (const std::exception& e) {
                return error("%s: Deserialize or I/O error - %s", __func__, e.what());
            }
            hashBlock = header.GetHash();
            if (txOut.GetHash() != hash)
                return error("%s: txid mismatch", __func__);
            return true;
        }
    }

    CBlockIndex *pindexSlow = nullptr;
    if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
        int nHeight = -1;
        {
            const CCoins* coins = pcoinsTip->AccessCoins(hash);
            if (coins != nullptr)
            {
                nHeight = coins->nHeight;
                if (nHeight > 0)
                {
                    CBlockIndex *pindexSlow = chainActive[nHeight];
                    if (pindexSlow != nullptr)
                    {
                        CBlock block;
                        if (ReadBlockFromDisk(block, pindexSlow,1))
                        {
                            for(const CTransaction &tx : block.vtx)
                            {
                                if (tx.GetHash() == hash)
                                {
                                    txOut = tx;
                                    hashBlock = pindexSlow->GetBlockHash();
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return false;
}

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos, const CMessageHeader::MessageStartChars& messageStart)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk: OpenBlockFile failed");

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, block);
    fileout << FLATDATA(messageStart) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk: ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}

bool ReadBlockFromDisk(int32_t height,CBlock& block, const CDiskBlockPos& pos,bool checkPOW)
{
    uint8_t pubkey33[33];
    block.SetNull();

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
    {
        //fprintf(stderr,"readblockfromdisk err A\n");
        return error("ReadBlockFromDisk: OpenBlockFile failed for %s", pos.ToString());
    }

    // Read block
    try {
        filein >> block;
    }
    catch (const std::exception& e) {
        fprintf(stderr,"readblockfromdisk err B\n");
        return error("%s: Deserialize or I/O error - %s at %s", __func__, e.what(), pos.ToString());
    }
    // Check the header
    if ( 0 && checkPOW != 0 )
    {
        komodo_block2pubkey33(pubkey33,(CBlock *)&block);
        if (!(CheckEquihashSolution(&block, Params()) && CheckProofOfWork(block, pubkey33, height, Params().GetConsensus())))
        {
            int32_t i; for (i=0; i<33; i++)
                fprintf(stderr,"%02x",pubkey33[i]);
            fprintf(stderr," warning unexpected diff at ht.%d\n",height);

            return error("ReadBlockFromDisk: Errors in block header at %s", pos.ToString());
        }
    }
    return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex,bool checkPOW)
{
    if ( pindex == 0 )
        return false;
    if (!ReadBlockFromDisk(pindex->nHeight,block, pindex->GetBlockPos(),checkPOW))
        return false;
    if (block.GetHash() != pindex->GetBlockHash())
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*): GetHash() doesn't match index for %s at %s",
                     pindex->ToString(), pindex->GetBlockPos().ToString());
    return true;
}

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams)
{
    if (chainName.isKMD()) {
        if (nHeight == 1)
            return 100000000 * COIN; // ICO allocation
        else if (nHeight < nS8HardforkHeight)
            return 3 * COIN;
        else
            return COIN; // KIP-0002, https://github.com/KomodoPlatform/kips/blob/main/kips/kip-0002.mediawiki
    } else {
        return komodo_ac_block_subsidy(nHeight);
    }
}

bool IsInitialBlockDownload()
{
    const CChainParams& chainParams = Params();
    // Once this function has returned false, it must remain false.
    static std::atomic<bool> latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(std::memory_order_relaxed))
        return false;

    if (fImporting || fReindex)
        return true;

    if (fCheckpointsEnabled && chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()))
        return true;

    if (chainActive.Tip() == nullptr)
        return true;

    /* TODO:
       - IBD check uses minimumchain work instead of checkpoints.
         https://github.com/zcash/zcash/commit/e41632c9fb5d32491e7f394b7b3a82f6cb5897cb
       - Consider using hashActivationBlock for Sapling in KMD.
         https://github.com/zcash/zcash/pull/4060/commits/150e3303109047118179f33b1cc5fc63095eb21d
    */

    // if (chainName.isKMD() && chainActive.Tip()->nChainWork < UintToArith256(chainParams.GetConsensus().nMinimumChainWork))
    //     return true;

    bool state = ((chainActive.Height() < chainActive.Tip()->nHeight - 24*60) ||
             chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge));

    if ( KOMODO_INSYNC != 0 )
        state = false;

    if (!state)
    {
        LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
        latchToFalse.store(true, std::memory_order_relaxed);
    }
    return state;
}

// determine if we are in sync with the best chain
int IsNotInSync()
{
    const CChainParams& chainParams = Params();

    LOCK(cs_main);
    if (fImporting || fReindex)
    {
        //fprintf(stderr,"IsNotInSync: fImporting %d || %d fReindex\n",(int32_t)fImporting,(int32_t)fReindex);
        return true;
    }
    if (fCheckpointsEnabled)
    {
        if (fCheckpointsEnabled && chainActive.Height() < Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()))
        {
            //fprintf(stderr,"IsNotInSync: checkpoint -> initialdownload chainActive.Height().%d GetTotalBlocksEstimate(chainParams.Checkpoints().%d\n", chainActive.Height(), Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints()));
            return true;
        }
    }

    CBlockIndex *pbi = chainActive.Tip();

    if ( !pbi ||
         (pindexBestHeader == 0) ||
         ((pindexBestHeader->nHeight - 1) > pbi->nHeight) )
    {
        return (pbi && pindexBestHeader && (pindexBestHeader->nHeight - 1) > pbi->nHeight) ?
                pindexBestHeader->nHeight - pbi->nHeight :
                true;
    }

    return false;
}

static bool fLargeWorkForkFound = false;
static bool fLargeWorkInvalidChainFound = false;
static CBlockIndex *pindexBestForkTip = NULL;
static CBlockIndex *pindexBestForkBase = NULL;

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before finishing our initial sync)
    if (IsInitialBlockDownload())
        return;

    // If our best fork is no longer within 288 blocks (+/- 12 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && chainActive.Height() - pindexBestForkTip->nHeight >= 288)
        pindexBestForkTip = NULL;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork >
            (chainActive.Tip()->nChainWork + (GetBlockProof(*chainActive.Tip()) * 6))))
    {
        if (!fLargeWorkForkFound && pindexBestForkBase)
        {
            std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
            pindexBestForkBase->phashBlock->ToString() + std::string("'");
            CAlert::Notify(warning, true);
        }
        if (pindexBestForkTip && pindexBestForkBase)
        {
            LogPrintf("%s: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n", __func__,
                      pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                      pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
            fLargeWorkForkFound = true;
        }
        else
        {
            std::string warning = std::string("Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.");
            LogPrintf("%s: %s\n", warning.c_str(), __func__);
            CAlert::Notify(warning, true);
            fLargeWorkInvalidChainFound = true;
        }
    }
    else
    {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger)
    {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition where we should warn the user about as a fork of at least 7 blocks
    // with a tip within 72 blocks (+/- 3 hours if no one mines it) of ours
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
        pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
        chainActive.Height() - pindexNewForkTip->nHeight < 72)
    {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch)
{
    if (howmuch == 0)
        return;

    CNodeState *state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", 101);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore)
    {
        LogPrintf("%s: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("%s: %s (%d -> %d)\n", __func__, state->name, state->nMisbehavior-howmuch, state->nMisbehavior);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("%s: invalid block=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
              pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
              log(pindexNew->nChainWork.getdouble())/log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pindexNew->GetBlockTime()));
    CBlockIndex *tip = chainActive.Tip();
    assert (tip);
    LogPrintf("%s:  current best=%s  height=%d  log2_work=%.8g  date=%s\n", __func__,
              tip->GetBlockHash().ToString(), chainActive.Height(),
              log(tip->nChainWork.getdouble())/log(2.0),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", tip->GetBlockTime()));
    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex *pindex, const CValidationState &state) {
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        std::map<uint256, NodeId>::iterator it = mapBlockSource.find(pindex->GetBlockHash());
        if (it != mapBlockSource.end() && State(it->second)) {
            CBlockReject reject = {state.GetRejectCode(), state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), pindex->GetBlockHash()};
            State(it->second)->rejects.push_back(reject);
            if (nDoS > 0)
                Misbehaving(it->second, nDoS);
        }
    }
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo &txundo, int nHeight)
{
    if (!tx.IsMint()) // mark inputs spent
    {
        txundo.vprevout.reserve(tx.vin.size());
        BOOST_FOREACH(const CTxIn &txin, tx.vin) {
            CCoinsModifier coins = inputs.ModifyCoins(txin.prevout.hash);
            unsigned nPos = txin.prevout.n;

            if (nPos >= coins->vout.size() || coins->vout[nPos].IsNull())
                assert(false);
            // mark an outpoint spent, and construct undo information
            txundo.vprevout.push_back(CTxInUndo(coins->vout[nPos]));
            coins->Spend(nPos);
            if (coins->vout.size() == 0) {
                CTxInUndo& undo = txundo.vprevout.back();
                undo.nHeight = coins->nHeight;
                undo.fCoinBase = coins->fCoinBase;
                undo.nVersion = coins->nVersion;
            }
        }
    }

    // spend nullifiers
    inputs.SetNullifiers(tx, true);

    inputs.ModifyCoins(tx.GetHash())->FromTx(tx, nHeight); // add outputs

    // Unorthodox state
    if (tx.IsCoinImport()) {
        // add a tombstone for the burnTx
        AddImportTombstone(tx, inputs, nHeight);
    }
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()() {
    const CScript &scriptSig = ptxTo->vin[nIn].scriptSig;
    ServerTransactionSignatureChecker checker(ptxTo, nIn, amount, cacheStore, *txdata);
    if (!VerifyScript(scriptSig, scriptPubKey, nFlags, checker, consensusBranchId, &error)) {
        return ::error("CScriptCheck(): %s:%d VerifySignature failed: %s", ptxTo->GetHash().ToString(), nIn, ScriptErrorString(error));
    }
    return true;
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    CBlockIndex* pindexPrev = mapBlockIndex.find(inputs.GetBestBlock())->second;
    return pindexPrev->nHeight + 1;
}

namespace Consensus {
    bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, const Consensus::Params& consensusParams)
    {
        // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
        // for an attacker to attempt to split the network.
        if (!inputs.HaveInputs(tx))
            return state.Invalid(error("CheckInputs(): %s inputs unavailable", tx.GetHash().ToString()));

        // are the JoinSplit's requirements met?
        if (!inputs.HaveJoinSplitRequirements(tx, maxProcessingThreads))
            return state.Invalid(error("CheckInputs(): %s JoinSplit requirements not met", tx.GetHash().ToString()));

        CAmount nValueIn = 0;
        CAmount nFees = 0;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            const COutPoint &prevout = tx.vin[i].prevout;
            const CCoins *coins = inputs.AccessCoins(prevout.hash);
            assert(coins);

            if (coins->IsCoinBase()) {
                // ensure that output of coinbases are not still time locked
                if (coins->TotalTxValue() >= ASSETCHAINS_TIMELOCKGTE)
                {
                    uint64_t unlockTime = komodo_block_unlocktime(coins->nHeight);
                    if (nSpendHeight < unlockTime) {
                        return state.DoS(10,
                                        error("CheckInputs(): tried to spend coinbase that is timelocked until block %d", unlockTime),
                                        REJECT_INVALID, "bad-txns-premature-spend-of-coinbase");
                    }
                }

                // Ensure that coinbases are matured, no DoS as retry may work later
                if (nSpendHeight - coins->nHeight < ::Params().CoinbaseMaturity()) {
                    return state.Invalid(
                                         error("CheckInputs(): tried to spend coinbase at depth %d/%d", nSpendHeight - coins->nHeight, (int32_t)::Params().CoinbaseMaturity()),
                                         REJECT_INVALID, "bad-txns-premature-spend-of-coinbase");
                }

                // Ensure that coinbases cannot be spent to transparent outputs
                // Disabled on regtest
                if (fCoinbaseEnforcedProtectionEnabled &&
                    consensusParams.fCoinbaseMustBeProtected &&
                    !tx.vout.empty()) {
                    return state.DoS(100,
                                     error("CheckInputs(): tried to spend coinbase with transparent outputs"),
                                     REJECT_INVALID, "bad-txns-coinbase-spend-has-transparent-outputs");
                }
            }

            // Check for negative or overflow input values
            nValueIn += coins->vout[prevout.n].nValue;
#ifdef KOMODO_ENABLE_INTEREST
            if ( chainName.isKMD() && nSpendHeight > 60000 )//chainActive.Tip() != 0 && chainActive.Tip()->nHeight >= 60000 )
            {
                if ( coins->vout[prevout.n].nValue >= 10*COIN )
                {
                    int64_t interest; int32_t txheight; uint32_t locktime;
                    if ( (interest= komodo_accrued_interest(&txheight,&locktime,prevout.hash,prevout.n,0,coins->vout[prevout.n].nValue,(int32_t)nSpendHeight-1)) != 0 )
                    {
                        nValueIn += interest;
                    }
                }
            }
#endif
            if (!MoneyRange(coins->vout[prevout.n].nValue) || !MoneyRange(nValueIn))
                return state.DoS(100, error("CheckInputs(): txin values out of range"),
                                 REJECT_INVALID, "bad-txns-inputvalues-outofrange");

        }

        nValueIn += tx.GetShieldedValueIn();
        if (!MoneyRange(nValueIn))
            return state.DoS(100, error("CheckInputs(): shielded input to transparent value pool out of range"),
                             REJECT_INVALID, "bad-txns-inputvalues-outofrange");

        if (nValueIn < tx.GetValueOut())
        {
            fprintf(stderr,"spentheight.%d valuein %s vs %s error\n",nSpendHeight,FormatMoney(nValueIn).c_str(), FormatMoney(tx.GetValueOut()).c_str());
            return state.DoS(100, error("CheckInputs(): %s value in (%s) < value out (%s) diff %.8f",
                                        tx.GetHash().ToString(), FormatMoney(nValueIn), FormatMoney(tx.GetValueOut()),((double)nValueIn - tx.GetValueOut())/COIN),REJECT_INVALID, "bad-txns-in-belowout");
        }
        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, error("CheckInputs(): %s nTxFee < 0", tx.GetHash().ToString()),
                             REJECT_INVALID, "bad-txns-fee-negative");
        nFees += nTxFee;
        if (!MoneyRange(nFees))
            return state.DoS(100, error("CheckInputs(): nFees out of range"),
                             REJECT_INVALID, "bad-txns-fee-outofrange");
        return true;
    }
}// namespace Consensus

bool ContextualCheckInputs(
                           const CTransaction& tx,
                           CValidationState &state,
                           const CCoinsViewCache &inputs,
                           bool fScriptChecks,
                           unsigned int flags,
                           bool cacheStore,
                           PrecomputedTransactionData& txdata,
                           const Consensus::Params& consensusParams,
                           uint32_t consensusBranchId,
                           std::vector<CScriptCheck> *pvChecks)
{
    if (!tx.IsMint())
    {
        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs), consensusParams)) {
            return false;
        }

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;
                const CCoins* coins = inputs.AccessCoins(prevout.hash);
                assert(coins);

                // Verify signature
                CScriptCheck check(*coins, tx, i, flags, cacheStore, consensusBranchId, &txdata);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (flags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(*coins, tx, i,
                                            flags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore, consensusBranchId, &txdata);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100,false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    if (tx.IsCoinImport())
    {
        LOCK(cs_main);
        ServerTransactionSignatureChecker checker(&tx, 0, 0, false, txdata);
        return VerifyCoinImport(tx.vin[0].scriptSig, checker, state);
    }

    return true;
}

namespace {

    bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock, const CMessageHeader::MessageStartChars& messageStart)
    {
        // Open history file to append
        CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
        if (fileout.IsNull())
            return error("%s: OpenUndoFile failed", __func__);

        // Write index header
        unsigned int nSize = GetSerializeSize(fileout, blockundo);
        fileout << FLATDATA(messageStart) << nSize;

        // Write undo data
        long fileOutPos = ftell(fileout.Get());
        if (fileOutPos < 0)
            return error("%s: ftell failed", __func__);
        pos.nPos = (unsigned int)fileOutPos;
        fileout << blockundo;

        // calculate & write checksum
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << hashBlock;
        hasher << blockundo;
        fileout << hasher.GetHash();
        return true;
    }

    bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
    {
        // Open history file to read
        CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
        if (filein.IsNull())
            return error("%s: OpenBlockFile failed", __func__);

        // Read block
        uint256 hashChecksum;
        try {
            filein >> blockundo;
            filein >> hashChecksum;
        }
        catch (const std::exception& e) {
            return error("%s: Deserialize or I/O error - %s", __func__, e.what());
        }
        // Verify checksum
        CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
        hasher << hashBlock;
        hasher << blockundo;
        if (hashChecksum != hasher.GetHash())
            return error("%s: %s Checksum mismatch %s vs %s", __func__,hashBlock.GetHex().c_str(),hashChecksum.GetHex().c_str(),hasher.GetHash().GetHex().c_str());

        return true;
    }

} // anon namespace

/**
 * Apply the undo operation of a CTxInUndo to the given chain state.
 * @param undo The undo object.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return True on success.
 */
static bool ApplyTxInUndo(const CTxInUndo& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    CCoinsModifier coins = view.ModifyCoins(out.hash);
    if (undo.nHeight != 0) {
        // undo data contains height: this is the last output of the prevout tx being spent
        if (!coins->IsPruned())
            fClean = fClean && error("%s: undo data overwriting existing transaction", __func__);
        coins->Clear();
        coins->fCoinBase = undo.fCoinBase;
        coins->nHeight = undo.nHeight;
        coins->nVersion = undo.nVersion;
    } else {
        if (coins->IsPruned())
            fClean = fClean && error("%s: undo data adding output to missing transaction", __func__);
    }
    if (coins->IsAvailable(out.n))
        fClean = fClean && error("%s: undo data overwriting existing output", __func__);
    if (coins->vout.size() < out.n+1)
        coins->vout.resize(out.n+1);
    coins->vout[out.n] = undo.txout;

    return fClean;
}


void ConnectNotarisations(const CBlock &block, int height)
{
    // Record Notarisations
    NotarisationsInBlock notarisations = ScanBlockNotarisations(block, height);
    if (notarisations.size() > 0) {
        CDBBatch batch = CDBBatch(*pnotarisations);
        batch.Write(block.GetHash(), notarisations);
        WriteBackNotarisations(notarisations, batch);
        pnotarisations->WriteBatch(batch, true);
        LogPrintf("ConnectBlock: wrote %i block notarisations in block: %s\n",
                notarisations.size(), block.GetHash().GetHex().data());
    }
}


void DisconnectNotarisations(const CBlock &block)
{
    // Delete from notarisations cache
    NotarisationsInBlock nibs;
    if (GetBlockNotarisations(block.GetHash(), nibs)) {
        CDBBatch batch = CDBBatch(*pnotarisations);
        batch.Erase(block.GetHash());
        EraseBackNotarisations(nibs, batch);
        pnotarisations->WriteBatch(batch, true);
        LogPrintf("DisconnectTip: deleted %i block notarisations in block: %s\n",
            nibs.size(), block.GetHash().GetHex().data());
    }
}

int8_t GetAddressType(const CScript &scriptPubKey, CTxDestination &vDest, txnouttype &txType, vector<vector<unsigned char>> &vSols)
{
    int8_t keyType = 0;
    // some non-standard types, like time lock coinbases, don't solve, but do extract
    if ( (Solver(scriptPubKey, txType, vSols) || ExtractDestination(scriptPubKey, vDest)) )
    {
        keyType = 1;
        if (vDest.index() != std::variant_npos)
        {
            // if we failed to solve, and got a vDest, assume P2PKH or P2PK address returned
            CKeyID kid;
            if (CBitcoinAddress(vDest).GetKeyID(kid))
            {
                vSols.push_back(vector<unsigned char>(kid.begin(), kid.end()));
            }
        }
        else if (txType == TX_SCRIPTHASH)
        {
            keyType = 2;
        }
        else if (txType == TX_CRYPTOCONDITION )
        {
            keyType = 3;
        }
    }
    return keyType;
}

bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool* pfClean)
{
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    if (pfClean)
        *pfClean = false;

    bool fClean = true;
    CBlockUndo blockUndo;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull())
        return error("DisconnectBlock(): no undo data available");
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash()))
        return error("DisconnectBlock(): failure reading undo data");

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size())
        return error("DisconnectBlock(): block and undo data inconsistent");
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;

    // undo transactions in reverse order
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction &tx = block.vtx[i];
        uint256 hash = tx.GetHash();
        if (fAddressIndex) {

            for (unsigned int k = tx.vout.size(); k-- > 0;) {
                const CTxOut &out = tx.vout[k];

                vector<vector<unsigned char>> vSols;
                CTxDestination vDest;
                txnouttype txType = TX_PUBKEYHASH;
                int keyType = GetAddressType(out.scriptPubKey, vDest, txType, vSols);
                if ( keyType != 0 )
                {
                    for (auto addr : vSols)
                    {
                        uint160 addrHash = addr.size() == 20 ? uint160(addr) : Hash160(addr);
                        addressIndex.push_back(make_pair(CAddressIndexKey(keyType, addrHash, pindex->nHeight, i, hash, k, false), out.nValue));
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(keyType, addrHash, hash, k), CAddressUnspentValue()));
                    }
                }
            }
        }

        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        {
            CCoinsModifier outs = view.ModifyCoins(hash);
            outs->ClearUnspendable();

            CCoins outsBlock(tx, pindex->nHeight);
            // The CCoins serialization does not serialize negative numbers.
            // No network rules currently depend on the version here, so an inconsistency is harmless
            // but it must be corrected before txout nversion ever influences a network rule.
            if (outsBlock.nVersion < 0)
                outs->nVersion = outsBlock.nVersion;
            if (*outs != outsBlock)
                fClean = fClean && error("DisconnectBlock(): added transaction mismatch? database corrupted");

            // remove outputs
            outs->Clear();
        }

        // unspend nullifiers
        view.SetNullifiers(tx, false);

        // restore inputs
        if (!tx.IsMint()) {
            CTxUndo &txundo = blockUndo.vtxundo[i-1];
            if (txundo.vprevout.size() != tx.vin.size())
                return error("DisconnectBlock(): transaction and undo data inconsistent");
            for (unsigned int j = tx.vin.size(); j-- > 0;) {
                const COutPoint &out = tx.vin[j].prevout;
                const CTxInUndo &undo = txundo.vprevout[j];
                if (!ApplyTxInUndo(undo, view, out))
                    fClean = false;

                const CTxIn input = tx.vin[j];

                if (fSpentIndex) {
                    // undo and delete the spent index
                    spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue()));
                }

                if (fAddressIndex) {
                    const CTxOut &prevout = view.GetOutputFor(tx.vin[j]);

                    vector<vector<unsigned char>> vSols;
                    CTxDestination vDest;
                    txnouttype txType = TX_PUBKEYHASH;
                    int keyType = GetAddressType(prevout.scriptPubKey, vDest, txType, vSols);
                    if ( keyType != 0 )
                    {
                        for (auto addr : vSols)
                        {
                            uint160 addrHash = addr.size() == 20 ? uint160(addr) : Hash160(addr);
                            // undo spending activity
                            addressIndex.push_back(make_pair(CAddressIndexKey(keyType, addrHash, pindex->nHeight, i, hash, j, true), prevout.nValue * -1));
                            // restore unspent index
                            addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(keyType, addrHash, input.prevout.hash, input.prevout.n), CAddressUnspentValue(prevout.nValue, prevout.scriptPubKey, undo.nHeight)));
                        }
                    }
                }
            }
        }
        else if (tx.IsCoinImport())
        {
            RemoveImportTombstone(tx, view);
        }
    }

    // set the old best Sprout anchor back
    view.PopAnchor(blockUndo.old_sprout_tree_root, SPROUT);

    // set the old best Sapling anchor back
    // We can get this from the `hashFinalSaplingRoot` of the last block
    // However, this is only reliable if the last block was on or after
    // the Sapling activation height. Otherwise, the last anchor was the
    // empty root.
    if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        view.PopAnchor(pindex->pprev->hashFinalSaplingRoot, SAPLING);
        view.PopAnchor(pindex->pprev->hashFinalSaplingRoot, SAPLINGFRONTIER);
    } else {
        view.PopAnchor(SaplingMerkleTree::empty_root(), SAPLING);
        view.PopAnchor(SaplingMerkleFrontier::empty_root(), SAPLINGFRONTIER);
    }

    // Set the old best Orchard anchor back. We can get this from the
    // `hashFinalOrchardRoot` of the last block. However, if the last
    // block was not on or after the Orchard activation height, this
    // will be set to `null`. For logical consistency, in this case we
    // set the last anchor to the empty root.
    if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD) && !pindex->pprev->hashFinalOrchardRoot.IsNull()) {
        view.PopAnchor(pindex->pprev->hashFinalOrchardRoot, ORCHARDFRONTIER);
    } else {
        view.PopAnchor(OrchardMerkleFrontier::empty_root(), ORCHARDFRONTIER);
    }

    // This is guaranteed to be filled by LoadBlockIndex.
    assert(pindex->nCachedBranchId);
    auto consensusBranchId = pindex->nCachedBranchId.value();

    if (NetworkUpgradeActive(pindex->pprev->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        view.PopHistoryNode(consensusBranchId);
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());

    if (pfClean) {
        *pfClean = fClean;
        return true;
    }

    if (fAddressIndex) {
        if (!pblocktree->EraseAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to delete address index");
        }
        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    return fClean;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE *fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck() {
    RenameThread("zcash-scriptch");
    scriptcheckqueue.Thread();
}

//
// Called periodically asynchronously; alerts if it smells like
// we're being fed a bad chain (blocks being generated much
// too slowly or too quickly).
//
void PartitionCheck(bool (*initialDownloadCheck)(), CCriticalSection& cs, const CBlockIndex *const &bestHeader,
                    int64_t nPowTargetSpacing)
{
    if (bestHeader == NULL || initialDownloadCheck()) return;

    static int64_t lastAlertTime = 0;
    int64_t now = GetTime();
    if (lastAlertTime > now-60*60*24) return; // Alert at most once per day

    const int SPAN_HOURS=4;
    const int SPAN_SECONDS=SPAN_HOURS*60*60;
    int BLOCKS_EXPECTED = SPAN_SECONDS / nPowTargetSpacing;

    boost::math::poisson_distribution<double> poisson(BLOCKS_EXPECTED);

    std::string strWarning;
    int64_t startTime = GetTime()-SPAN_SECONDS;

    LOCK(cs);
    const CBlockIndex* i = bestHeader;
    int nBlocks = 0;
    while (i->GetBlockTime() >= startTime) {
        ++nBlocks;
        i = i->pprev;
        if (i == NULL) return; // Ran out of chain, we must not be fully synced
    }

    // How likely is it to find that many by chance?
    double p = boost::math::pdf(poisson, nBlocks);

    LogPrint("partitioncheck", "%s : Found %d blocks in the last %d hours\n", __func__, nBlocks, SPAN_HOURS);
    LogPrint("partitioncheck", "%s : likelihood: %g\n", __func__, p);

    // Aim for one false-positive about every fifty years of normal running:
    const int FIFTY_YEARS = 50*365*24*60*60;
    double alertThreshold = 1.0 / (FIFTY_YEARS / SPAN_SECONDS);

    if (bestHeader->nHeight > BLOCKS_EXPECTED)
    {
        if (p <= alertThreshold && nBlocks < BLOCKS_EXPECTED)
        {
            // Many fewer blocks than expected: alert!
            strWarning = strprintf(_("WARNING: check your network connection, %d blocks received in the last %d hours (%d expected)"),
                                nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
        }
        else if (p <= alertThreshold && nBlocks > BLOCKS_EXPECTED)
        {
            // Many more blocks than expected: alert!
            strWarning = strprintf(_("WARNING: abnormally high number of blocks generated, %d blocks received in the last %d hours (%d expected)"),
                                nBlocks, SPAN_HOURS, BLOCKS_EXPECTED);
        }
    }
    if (!strWarning.empty())
    {
        strMiscWarning = strWarning;
        CAlert::Notify(strWarning, true);
        lastAlertTime = now;
    }
}


static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;
bool FindBlockPos(int32_t tmpflag,CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false);
bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos);

/*****
 * Only for testing, DO NOT USE
 * @returns the cs_main mutex
 */
CCriticalSection& get_cs_main()
{
    return cs_main;
}

/*****
 * @brief Apply the effects of this block (with given index) on the UTXO set represented by coins
 * @param block the block to add
 * @param state the result status
 * @param pindex where to insert the block
 * @param view the chain
 * @param fJustCheck do not actually modify, only do checks
 * @param fcheckPOW
 * @returns true on success
 */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck,bool fCheckPOW)
{
    CDiskBlockPos blockPos;
    const CChainParams& chainparams = Params();
    if ( KOMODO_NSPV_SUPERLITE )
        return(true);
    if ( KOMODO_STOPAT != 0 && pindex->nHeight > KOMODO_STOPAT )
        return(false);
    AssertLockHeld(cs_main);
    bool fExpensiveChecks = true;
    bool fCheckAuthDataRoot = true;
    if (fCheckpointsEnabled) {
        CBlockIndex *pindexLastCheckpoint = Checkpoints::GetLastCheckpoint(chainparams.Checkpoints());
        if (pindexLastCheckpoint && pindexLastCheckpoint->GetAncestor(pindex->nHeight) == pindex) {
            // This block is an ancestor of a checkpoint: disable script checks
            fExpensiveChecks = false;
        }
    }
    auto verifier = ProofVerifier::Strict();
    auto disabledVerifier = ProofVerifier::Disabled();
    int32_t futureblock;
    CAmount blockReward = GetBlockSubsidy(pindex->nHeight, chainparams.GetConsensus());
    uint64_t notarypaycheque = 0;
    // Check it again to verify JoinSplit proofs, and in case a previous version let a bad block in
    if ( !CheckBlock(&futureblock,pindex->nHeight,pindex,block, state, fExpensiveChecks ? verifier : disabledVerifier, fCheckPOW, !fJustCheck) || futureblock != 0 )
    {
        return false;
    }
    if ( fCheckPOW != 0 && (pindex->nStatus & BLOCK_VALID_CONTEXT) != BLOCK_VALID_CONTEXT ) // Activate Jan 15th, 2019
    {
        if ( !ContextualCheckBlock(1,block, state, pindex->pprev) )
        {
            fprintf(stderr,"ContextualCheckBlock failed ht.%d\n",(int32_t)pindex->nHeight);
            if ( pindex->nTime > 1547510400 )
                return false;
            fprintf(stderr,"grandfathered exception, until jan 15th 2019\n");
        } else pindex->nStatus |= BLOCK_VALID_CONTEXT;
    }

    // Do this here before the block is moved to the main block files.
    if ( ASSETCHAINS_NOTARY_PAY[0] != 0 && pindex->nHeight > 10 )
    {
        // do a full block scan to get notarisation position and to enforce a valid notarization is in position 1.
        // if notarisation in the block, must be position 1 and the coinbase must pay notaries.
        int32_t notarisationTx = komodo_connectblock(true,pindex,*(CBlock *)&block);
        // -1 means that the valid notarization isnt in position 1 or there are too many notarizations in this block.
        if ( notarisationTx == -1 )
            return state.DoS(100, error("ConnectBlock(): Notarization is not in TX position 1 or block contains more than 1 notarization! Invalid Block!"),
                        REJECT_INVALID, "bad-notarization-position");
        // 1 means this block contains a valid notarisation and its in position 1.
        // its no longer possible for any attempted notarization to be in a block with a valid one!
        // if notaries create a notarisation even if its not in this chain it will need to be mined inside its own block!
        if ( notarisationTx == 1 )
        {
            // Check if the notaries have been paid.
            if ( block.vtx[0].vout.size() == 1 )
                return state.DoS(100, error("ConnectBlock(): Notaries have not been paid!"),
                                REJECT_INVALID, "bad-cb-amount");
            // calculate the notaries compensation and validate the amounts and pubkeys are correct.
            notarypaycheque = komodo_checknotarypay((CBlock *)&block,(int32_t)pindex->nHeight);
            if ( notarypaycheque > 0 )
                blockReward += notarypaycheque;
            else
                return state.DoS(100, error("ConnectBlock(): Notary pay validation failed!"),
                                REJECT_INVALID, "bad-cb-amount");
        }
    }
    // Move the block to the main block file, we need this to create the TxIndex in the following loop.
    if ( (pindex->nStatus & BLOCK_IN_TMPFILE) != 0 )
    {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        if (!FindBlockPos(0,state, blockPos, nBlockSize+8, pindex->nHeight, block.GetBlockTime(),false))
            return error("ConnectBlock(): FindBlockPos failed");
        if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
            return error("ConnectBlock(): FindBlockPos failed");
        pindex->nStatus &= (~BLOCK_IN_TMPFILE);
        pindex->nFile = blockPos.nFile;
        pindex->nDataPos = blockPos.nPos;
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
        setDirtyFileInfo.insert(blockPos.nFile);
    }
    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? uint256() : pindex->pprev->GetBlockHash();
    if ( hashPrevBlock != view.GetBestBlock() )
    {
        fprintf(stderr,"ConnectBlock(): hashPrevBlock != view.GetBestBlock()\n");
        return state.DoS(1, error("ConnectBlock(): hashPrevBlock != view.GetBestBlock()"),
                         REJECT_INVALID, "hashPrevBlock-not-bestblock");
    }
    assert(hashPrevBlock == view.GetBestBlock());

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == chainparams.GetConsensus().hashGenesisBlock) {
        if (!fJustCheck) {
            view.SetBestBlock(pindex->GetBlockHash());
            // Before the genesis block, there was an empty tree
            SproutMerkleTree tree;
            pindex->hashSproutAnchor = tree.root();
            // The genesis block contained no JoinSplits
            pindex->hashFinalSproutRoot = pindex->hashSproutAnchor;
        }
        return true;
    }

    bool fScriptChecks = (!fCheckpointsEnabled || pindex->nHeight >= Checkpoints::GetTotalBlocksEstimate(chainparams.Checkpoints()));
    // Do not allow blocks that contain transactions which 'overwrite' older transactions,
    // unless those are already completely spent.
    BOOST_FOREACH(const CTransaction& tx, block.vtx) {
        const CCoins* coins = view.AccessCoins(tx.GetHash());
        if (coins && !coins->IsPruned())
            return state.DoS(100, error("ConnectBlock(): tried to overwrite transaction"),
                             REJECT_INVALID, "bad-txns-BIP30");
    }

    unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

    // DERSIG (BIP66) is also always enforced, but does not have a flag.

    CBlockUndo blockundo;
    /*
    if ( ASSETCHAINS_CC != 0 )
    {
        if ( scriptcheckqueue.IsIdle() == 0 )
        {
            fprintf(stderr,"scriptcheckqueue isnt idle\n");
            sleep(1);
        }
    }
    */
    CCheckQueueControl<CScriptCheck> control(fExpensiveChecks && nScriptCheckThreads ? &scriptcheckqueue : NULL);

    int64_t nTimeStart = GetTimeMicros();
    CAmount nFees = 0;
    int nInputs = 0;
    uint64_t valueout;
    int64_t voutsum = 0, prevsum = 0, interest, sum = 0, stakeTxValue = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    std::vector<std::pair<CAddressIndexKey, CAmount> > addressIndex;
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > addressUnspentIndex;
    std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> > spentIndex;
    // Construct the incremental merkle tree at the current
    // block position,
    auto old_sprout_tree_root = view.GetBestAnchor(SPROUT);
    // saving the top anchor in the block index as we go.
    if (!fJustCheck) {
        pindex->hashSproutAnchor = old_sprout_tree_root;
    }

    SproutMerkleTree sprout_tree;

    // This should never fail: we should always be able to get the root
    // that is on the tip of our chain
    assert(view.GetSproutAnchorAt(old_sprout_tree_root, sprout_tree));

    {
        // Consistency check: the root of the tree we're given should
        // match what we asked for.
        assert(sprout_tree.root() == old_sprout_tree_root);
    }

    SaplingMerkleTree sapling_tree;
    assert(view.GetSaplingAnchorAt(view.GetBestAnchor(SAPLING), sapling_tree));

    SaplingMerkleFrontier sapling_frontier_tree;
    assert(view.GetSaplingFrontierAnchorAt(view.GetBestAnchor(SAPLINGFRONTIER), sapling_frontier_tree));

    OrchardMerkleFrontier orchard_frontier_tree;
    assert(view.GetOrchardFrontierAnchorAt(view.GetBestAnchor(ORCHARDFRONTIER), orchard_frontier_tree));

    // Grab the consensus branch ID for this block and its parent
    auto consensusBranchId = CurrentEpochBranchId(pindex->nHeight, chainparams.GetConsensus());
    auto prevConsensusBranchId = CurrentEpochBranchId(pindex->nHeight - 1, chainparams.GetConsensus());

    size_t total_sapling_tx = 0;
    size_t total_orchard_tx = 0;

    CAmount chainSupplyDelta = 0;
    CAmount transparentValueDelta = 0;
    CAmount burnedAmountDelta = 0;
    std::vector<PrecomputedTransactionData> txdata;
    txdata.reserve(block.vtx.size()); // Required so that pointers to individual PrecomputedTransactionData don't get invalidated
    for (unsigned int i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction &tx = block.vtx[i];
        const uint256 txhash = tx.GetHash();
        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > MAX_BLOCK_SIGOPS)
            return state.DoS(100, error("ConnectBlock(): too many sigops"),
                             REJECT_INVALID, "bad-blk-sigops");

        std::vector<CTxOut> allPrevOutputs;

        if (!tx.IsMint())
        {
            if (!view.HaveInputs(tx))
            {
                fprintf(stderr, "Connect Block missing inputs tx_number.%d \nvin txid.%s vout.%d \n",i,tx.vin[0].prevout.hash.ToString().c_str(),tx.vin[0].prevout.n);
                return state.DoS(100, error("ConnectBlock(): inputs missing/spent"),
                                 REJECT_INVALID, "bad-txns-inputs-missingorspent");
            }

            for (const auto& input : tx.vin) {
                const auto prevout = view.GetOutputFor(input);
                transparentValueDelta -= prevout.nValue;
                allPrevOutputs.push_back(prevout);
            }

            // are the JoinSplit's requirements met?
            if (!view.HaveJoinSplitRequirements(tx, maxProcessingThreads))
                return state.DoS(100, error("ConnectBlock(): JoinSplit requirements not met"),
                                 REJECT_INVALID, "bad-txns-joinsplit-requirements-not-met");

            if (fAddressIndex || fSpentIndex)
            {
                for (size_t j = 0; j < tx.vin.size(); j++)
                {
                    const CTxIn input = tx.vin[j];
                    const CTxOut &prevout = allPrevOutputs[j];

                    vector<vector<unsigned char>> vSols;
                    CTxDestination vDest;
                    txnouttype txType = TX_PUBKEYHASH;
                    uint160 addrHash;
                    int keyType = GetAddressType(prevout.scriptPubKey, vDest, txType, vSols);
                    if ( keyType != 0 )
                    {
                        for (auto addr : vSols)
                        {
                            addrHash = addr.size() == 20 ? uint160(addr) : Hash160(addr);
                            // record spending activity
                            addressIndex.push_back(make_pair(CAddressIndexKey(keyType, addrHash, pindex->nHeight, i, txhash, j, true), prevout.nValue * -1));

                            // remove address from unspent index
                            addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(keyType, addrHash, input.prevout.hash, input.prevout.n), CAddressUnspentValue()));
                        }

                        if (fSpentIndex) {
                            // add the spent index to determine the txid and input that spent an output
                            // and to find the amount and address from an input
                            spentIndex.push_back(make_pair(CSpentIndexKey(input.prevout.hash, input.prevout.n), CSpentIndexValue(txhash, j, pindex->nHeight, prevout.nValue, keyType, addrHash)));
                        }
                    }
                }
            }
            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, view);
            if (nSigOps > MAX_BLOCK_SIGOPS)
                return state.DoS(100, error("ConnectBlock(): too many sigops"),
                                 REJECT_INVALID, "bad-blk-sigops");
        }

        if (tx.IsMint())
        {
            // Add the output value of the coinbase transaction to the chain supply
            // delta. This includes fees, which are then canceled out by the fee
            // subtractions in the other branch of this conditional.
            chainSupplyDelta += tx.GetValueOut();
        } else {
            const auto txFee = view.GetValueIn(pindex->nHeight,interest,tx) - tx.GetValueOut();
            // Fees from a transaction do not go into an output of the transaction,
            // and therefore decrease the chain supply. If the miner claims them,
            // they will be re-added in the other branch of this conditional.
            chainSupplyDelta -= txFee;
        }

        txdata.emplace_back(tx, allPrevOutputs);

        valueout = tx.GetValueOut();
        if ( KOMODO_VALUETOOBIG(valueout) != 0 )
        {
            fprintf(stderr,"valueout %.8f too big\n",(double)valueout/COIN);
            return state.DoS(100, error("ConnectBlock(): GetValueOut too big"),REJECT_INVALID,"tx valueout is too big");
        }
        if (tx.IsCoinBase())
        {
            // Add the output value of the coinbase transaction to the chain supply
            // delta. This includes fees, which are then canceled out by the fee
            // subtractions in the other branch of this conditional.
            chainSupplyDelta += tx.GetValueOut();
        } else {
            const auto txFee = (stakeTxValue= view.GetValueIn(chainActive.Tip()->nHeight,interest,tx) - valueout);
            nFees += txFee;

            // Fees from a transaction do not go into an output of the transaction,
            // and therefore decrease the chain supply. If the miner claims them,
            // they will be re-added in the other branch of this conditional.
            chainSupplyDelta -= txFee;

            sum += interest;

            std::vector<CScriptCheck> vChecks;
            if (!ContextualCheckInputs(tx, state, view, fExpensiveChecks, flags, false, txdata[i], chainparams.GetConsensus(), consensusBranchId, nScriptCheckThreads ? &vChecks : NULL))
                return false;
            control.Add(vChecks);
        }

        if (fAddressIndex) {
            for (unsigned int k = 0; k < tx.vout.size(); k++) {
                const CTxOut &out = tx.vout[k];

                uint160 addrHash;

                vector<vector<unsigned char>> vSols;
                CTxDestination vDest;
                txnouttype txType = TX_PUBKEYHASH;
                int keyType = GetAddressType(out.scriptPubKey, vDest, txType, vSols);
                if ( keyType != 0 )
                {
                    for (auto addr : vSols)
                    {
                        addrHash = addr.size() == 20 ? uint160(addr) : Hash160(addr);
                        // record receiving activity
                        addressIndex.push_back(make_pair(CAddressIndexKey(keyType, addrHash, pindex->nHeight, i, txhash, k, false), out.nValue));

                        // record unspent output
                        addressUnspentIndex.push_back(make_pair(CAddressUnspentKey(keyType, addrHash, txhash, k), CAddressUnspentValue(out.nValue, out.scriptPubKey, pindex->nHeight)));
                    }
                }
            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.push_back(CTxUndo());
        }
        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        BOOST_FOREACH(const JSDescription &joinsplit, tx.vjoinsplit) {
            BOOST_FOREACH(const uint256 &note_commitment, joinsplit.commitments) {
                // Insert the note commitments into our temporary tree.
                sprout_tree.append(note_commitment);
            }
        }

        //Append Sapling Output to SaplingMerkleTree
        for (const auto& output : tx.GetSaplingOutputs()) {
            auto cmu = uint256::FromRawBytes(output.cmu());
            sapling_tree.append(cmu);
        }

        //Append Sapling Outputs to SaplingMerkleFrontier
        if (tx.GetSaplingBundle().IsPresent()) {
            sapling_frontier_tree.AppendBundle(tx.GetSaplingBundle());
            total_sapling_tx += 1;
        }

        //Append Orchard Outputs to OrchardMerkleFrontier
        if (tx.GetOrchardBundle().IsPresent()) {
            orchard_frontier_tree.AppendBundle(tx.GetOrchardBundle());
            total_orchard_tx += 1;
        }

        for (const auto& out : tx.vout) {
            transparentValueDelta += out.nValue;
        }

        for (const auto& out : tx.vout) {
            if (!out.scriptPubKey.IsUnspendable()) {
                transparentValueDelta += out.nValue;
            } else {
                // If the outputs are unspendable, we should not include them in the transparent pool,
                // but include in the burned amount calculations
                burnedAmountDelta += out.nValue;
            }
        }

        vPos.push_back(std::make_pair(tx.GetHash(), pos));
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }

    // Derive the various block commitments.
    // We only derive them if they will be used for this block.
    std::optional<uint256> hashAuthDataRoot;
    std::optional<uint256> hashChainHistoryRoot;
    if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        hashAuthDataRoot = block.BuildAuthDataMerkleTree();
        hashChainHistoryRoot = view.GetHistoryRoot(prevConsensusBranchId);
    }

    // This is moved from CheckBlock for staking chains, so we can enforce the staking tx value was indeed paid to the coinbase.
    if ( ASSETCHAINS_STAKED != 0 && fCheckPOW && komodo_checkPOW(blockReward+stakeTxValue-notarypaycheque,1,(CBlock *)&block,pindex->nHeight) < 0 )
        return state.DoS(100, error("ConnectBlock: ac_staked chain failed slow komodo_checkPOW"),REJECT_INVALID, "failed-slow_checkPOW");

    view.PushAnchor(sprout_tree);
    view.PushAnchor(sapling_tree);
    view.PushAnchor(sapling_frontier_tree);
    view.PushAnchor(orchard_frontier_tree);
    if (!fJustCheck) {
        // Update pindex with the net change in transparent value and the chain's total
        // transparent value.
        pindex->nChainSupplyDelta = chainSupplyDelta;
        pindex->nTransparentValue = transparentValueDelta;
        pindex->nBurnedAmountDelta = burnedAmountDelta;
        if (pindex->pprev) {
            if (pindex->pprev->nChainTotalSupply) {
                pindex->nChainTotalSupply = *pindex->pprev->nChainTotalSupply + chainSupplyDelta;
            } else {
                pindex->nChainTotalSupply = std::nullopt;
            }

            if (pindex->pprev->nChainTransparentValue) {
                pindex->nChainTransparentValue = *pindex->pprev->nChainTransparentValue + transparentValueDelta;
            } else {
                pindex->nChainTransparentValue = std::nullopt;
            }

            if (pindex->pprev->nChainTotalBurned) {
                pindex->nChainTotalBurned = *pindex->pprev->nChainTotalBurned + burnedAmountDelta;
            } else {
                pindex->nChainTotalBurned = std::nullopt;
            }
        } else {
            pindex->nChainTotalSupply = chainSupplyDelta;
            pindex->nChainTransparentValue = transparentValueDelta;
            pindex->nChainTotalBurned = burnedAmountDelta;
        }

        pindex->hashFinalSproutRoot = sprout_tree.root();
    }

    blockundo.old_sprout_tree_root = old_sprout_tree_root;

    // If Sapling is active, block.hashBlockCommitments must be the
    // same as the root of the Sapling tree
    if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        //Set the hashFinalSaplingRoot in the block index (equal to HahsBlockCommitments before Orchard)
        pindex->hashFinalSaplingRoot = sapling_frontier_tree.root();
    }

    // - If this block is before NU5 activation:
        //   - hashAuthDataRoot and hashFinalOrchardRoot are always null.
        //   - We don't set hashChainHistoryRoot here to maintain the invariant
        //     documented in CBlockIndex (which was ensured in AddToBlockIndex).
        // - If this block is on or after NU5 activation, this is where we set
        //   the correct values of hashAuthDataRoot, hashFinalOrchardRoot, and
        //   hashChainHistoryRoot; in particular, blocks that are never passed
        //   to ConnectBlock() (and thus never on the main chain) will stay with
        //   these set to null.
    if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        pindex->hashAuthDataRoot = hashAuthDataRoot.value();
        pindex->hashFinalOrchardRoot = orchard_frontier_tree.root();
        pindex->hashChainHistoryRoot = hashChainHistoryRoot.value();
    }

    if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        if (fCheckAuthDataRoot) {
            // If NU5 is active, block.hashBlockCommitments must be the top digest
            // of the ZIP 244 block commitments linked list.
            // https://zips.z.cash/zip-0244#block-header-changes
            uint256 hashBlockCommitments = DeriveBlockCommitmentsHash(
                hashChainHistoryRoot.value(),
                hashAuthDataRoot.value());

            // LogPrintf("\n\nValidating Block\n");
            // LogPrintf("hashChainHistoryRoot %s\n", hashChainHistoryRoot.value().ToString());
            // LogPrintf("hashAuthDataRoot %s\n", hashAuthDataRoot.value().ToString());
            // LogPrintf("hashBlockCommitments %s\n\n\n", hashBlockCommitments.ToString());


            if (block.hashBlockCommitments != hashBlockCommitments) {
                return state.DoS(100,
                    error("ConnectBlock(): block's hashBlockCommitments is incorrect (should be ZIP 244 block commitment)"),
                    REJECT_INVALID, "bad-block-commitments-hash");
            }
        }
    } else if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        // If Sapling is active, block.hashBlockCommitments must be the
        // same as the root of the Sapling tree
        if (block.hashBlockCommitments != sapling_frontier_tree.root()) {
            return state.DoS(100,
                error("ConnectBlock(): block's hashBlockCommitments is incorrect (should be Sapling tree root)"),
                REJECT_INVALID, "bad-sapling-root-in-block");
        }
    }

    if (NetworkUpgradeActive(pindex->nHeight, chainparams.GetConsensus(), Consensus::UPGRADE_ORCHARD)) {
        HistoryNode historyNode;
        historyNode = libzcash::NewV2Leaf(
            block.GetHash(),
            block.nTime,
            block.nBits,
            pindex->hashFinalSaplingRoot,
            pindex->hashFinalOrchardRoot,
            ArithToUint256(GetBlockProof(*pindex)),
            pindex->nHeight,
            total_sapling_tx,
            total_orchard_tx
        );
        view.PushHistoryNode(consensusBranchId, historyNode);
    }



    int64_t nTime1 = GetTimeMicros(); nTimeConnect += nTime1 - nTimeStart;
    LogPrint("bench", "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime1 - nTimeStart), 0.001 * (nTime1 - nTimeStart) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime1 - nTimeStart) / (nInputs-1), nTimeConnect * 0.000001);

    blockReward += nFees + sum;
    if ( chainName.isKMD() && pindex->nHeight >= KOMODO_NOTARIES_HEIGHT2)
        blockReward -= sum;

    if ( ASSETCHAINS_COMMISSION != 0 || ASSETCHAINS_FOUNDERS_REWARD != 0 ) //ASSETCHAINS_OVERRIDE_PUBKEY33[0] != 0 &&
    {
        uint64_t checktoshis;
        if ( (checktoshis= komodo_commission((CBlock *)&block,(int32_t)pindex->nHeight)) != 0 )
        {
            if ( block.vtx[0].vout.size() >= 2 && block.vtx[0].vout[1].nValue == checktoshis )
                blockReward += checktoshis;
            else if ( pindex->nHeight > 1 )
                fprintf(stderr,"checktoshis %.8f vs %.8f numvouts %d\n",dstr(checktoshis),dstr(block.vtx[0].vout[1].nValue),(int32_t)block.vtx[0].vout.size());
        }
    }
    if ( !chainName.isKMD() && pindex->nHeight == 1 && block.vtx[0].GetValueOut() != blockReward)
    {
        return state.DoS(100, error("ConnectBlock(): coinbase for block 1 pays wrong amount (actual=%d vs correct=%d)", block.vtx[0].GetValueOut(), blockReward),
                            REJECT_INVALID, "bad-cb-amount");
    }
    if ( block.vtx[0].GetValueOut() > blockReward+KOMODO_EXTRASATOSHI )
    {
        if ( !chainName.isKMD() || pindex->nHeight >= KOMODO_NOTARIES_HEIGHT1 || block.vtx[0].vout[0].nValue > blockReward )
        {
            return state.DoS(100,
                             error("ConnectBlock(): coinbase pays too much (actual=%d vs limit=%d)",
                                   block.vtx[0].GetValueOut(), blockReward),
                             REJECT_INVALID, "bad-cb-amount");
        } else if ( IS_KOMODO_NOTARY )
            fprintf(stderr,"allow nHeight.%d coinbase %.8f vs %.8f interest %.8f\n",(int32_t)pindex->nHeight,dstr(block.vtx[0].GetValueOut()),dstr(blockReward),dstr(sum));
    }
    if (!control.Wait())
        return state.DoS(100, false);
    int64_t nTime2 = GetTimeMicros(); nTimeVerify += nTime2 - nTimeStart;
    LogPrint("bench", "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime2 - nTimeStart), nInputs <= 1 ? 0 : 0.001 * (nTime2 - nTimeStart) / (nInputs-1), nTimeVerify * 0.000001);

    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS))
    {
        if (pindex->GetUndoPos().IsNull())
        {
            CDiskBlockPos pos;
            if (!FindUndoPos(state, pindex->nFile, pos, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock(): FindUndoPos failed");
            if ( pindex->pprev == 0 )
                fprintf(stderr,"ConnectBlock: unexpected null pprev\n");
            if (!UndoWriteToDisk(blockundo, pos, pindex->pprev->GetBlockHash(), chainparams.MessageStart()))
                return AbortNode(state, "Failed to write undo data");
            // update nUndoPos in block index
            pindex->nUndoPos = pos.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        // Now that all consensus rules have been validated, set nCachedBranchId.
        // Move this if BLOCK_VALID_CONSENSUS is ever altered.
        static_assert(BLOCK_VALID_CONSENSUS == BLOCK_VALID_SCRIPTS,
                      "nCachedBranchId must be set after all consensus rules have been validated.");
        if (IsActivationHeightForAnyUpgrade(pindex->nHeight, Params().GetConsensus())) {
            pindex->nStatus |= BLOCK_ACTIVATES_UPGRADE;
            pindex->nCachedBranchId = CurrentEpochBranchId(pindex->nHeight, chainparams.GetConsensus());
        } else if (pindex->pprev) {
            pindex->nCachedBranchId = pindex->pprev->nCachedBranchId;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }

    ConnectNotarisations(block, pindex->nHeight); // MoMoM notarisation DB.

    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");
    if (fAddressIndex) {
        if (!pblocktree->WriteAddressIndex(addressIndex)) {
            return AbortNode(state, "Failed to write address index");
        }

        if (!pblocktree->UpdateAddressUnspentIndex(addressUnspentIndex)) {
            return AbortNode(state, "Failed to write address unspent index");
        }
    }

    if (fSpentIndex)
        if (!pblocktree->UpdateSpentIndex(spentIndex))
            return AbortNode(state, "Failed to write transaction index");

    if (fTimestampIndex)
    {
        unsigned int logicalTS = pindex->nTime;
        unsigned int prevLogicalTS = 0;

        // retrieve logical timestamp of the previous block
        if (pindex->pprev)
            if (!pblocktree->ReadTimestampBlockIndex(pindex->pprev->GetBlockHash(), prevLogicalTS))
                LogPrintf("%s: Failed to read previous block's logical timestamp\n", __func__);

        if (logicalTS <= prevLogicalTS) {
            logicalTS = prevLogicalTS + 1;
            LogPrintf("%s: Previous logical timestamp is newer Actual[%d] prevLogical[%d] Logical[%d]\n", __func__, pindex->nTime, prevLogicalTS, logicalTS);
        }

        if (!pblocktree->WriteTimestampIndex(CTimestampIndexKey(logicalTS, pindex->GetBlockHash())))
            return AbortNode(state, "Failed to write timestamp index");

        if (!pblocktree->WriteTimestampBlockIndex(CTimestampBlockIndexKey(pindex->GetBlockHash()), CTimestampBlockIndexValue(logicalTS)))
            return AbortNode(state, "Failed to write blockhash index");
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    int64_t nTime3 = GetTimeMicros(); nTimeIndex += nTime3 - nTime2;
    LogPrint("bench", "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime4 = GetTimeMicros(); nTimeCallbacks += nTime4 - nTime3;
    LogPrint("bench", "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeCallbacks * 0.000001);

    komodo_connectblock(false,pindex,*(CBlock *)&block);  // dPoW state update.
    if ( ASSETCHAINS_NOTARY_PAY[0] != 0 )
    {
      // Update the notary pay with the latest payment.
      pindex->nNotaryPay = pindex->pprev->nNotaryPay + notarypaycheque;
    }
    return true;
}

enum FlushStateMode {
    FLUSH_STATE_NONE,
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

/**
 * Update the on-disk chain state.
 * The caches and indexes are flushed depending on the mode we're called with
 * if they're too large, if it's been a while since the last write,
 * or always and in all cases if we're in prune mode and are deleting files.
 */
bool static FlushStateToDisk(CValidationState &state, FlushStateMode mode) {
    LOCK2(cs_main, cs_LastBlockFile);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    std::set<int> setFilesToPrune;
    bool fFlushForPrune = false;
    try {
        if (fPruneMode && fCheckForPruning && !fReindex) {
            FindFilesToPrune(setFilesToPrune);
            fCheckForPruning = false;
            if (!setFilesToPrune.empty()) {
                fFlushForPrune = true;
                if (!fHavePruned) {
                    pblocktree->WriteFlag("prunedblockfiles", true);
                    fHavePruned = true;
                }
            }
        }
        int64_t nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (nLastWrite == 0) {
            nLastWrite = nNow;
        }
        if (nLastFlush == 0) {
            nLastFlush = nNow;
        }
        if (nLastSetChain == 0) {
            nLastSetChain = nNow;
        }
        size_t cacheSize = pcoinsTip->DynamicMemoryUsage();
        // The cache is large and close to the limit, but we have time now (not in the middle of a block processing).
        bool fCacheLarge = mode == FLUSH_STATE_PERIODIC && cacheSize * (10.0/9) > nCoinCacheUsage;
        // The cache is over the limit, we have to write now.
        bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && cacheSize > nCoinCacheUsage;
        // It's been a while since we wrote the block index to disk. Do this frequently, so we don't need to redownload after a crash.
        bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
        // Combine all conditions that result in a full cache flush.
        bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush || fFlushForPrune;
        // Write blocks and block index to disk.
        if (fDoFullFlush || fPeriodicWrite) {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!CheckDiskSpace(0))
                return state.Error("out of disk space");
            // First make sure all block and undo data is flushed to disk.
            FlushBlockFile();
            // Then update all block file information (which may refer to block and undo files).
            {
                std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
                vFiles.reserve(setDirtyFileInfo.size());
                for (set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                    if ( *it < TMPFILE_START )
                        vFiles.push_back(make_pair(*it, &vinfoBlockFile[*it]));
                    setDirtyFileInfo.erase(it++);
                }
                std::vector<CBlockIndex*> vBlocks;
                vBlocks.reserve(setDirtyBlockIndex.size());
                for (set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                    vBlocks.push_back(*it);
                    setDirtyBlockIndex.erase(it++);
                }
                if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                    return AbortNode(state, "Files to write to block index database");
                }
                // Now that we have written the block indices to the database, we do not
                // need to store solutions for these CBlockIndex objects in memory.
                // cs_main must be held here.
                for (CBlockIndex *pblockindex : vBlocks) {
                    pblockindex->TrimSolution();
                }
            }
            // Finally remove any pruned files
            if (fFlushForPrune)
                UnlinkPrunedFiles(setFilesToPrune);
            nLastWrite = nNow;
        }
        // Flush best chain related state. This can only be done if the blocks / block index write was also done.
        if (fDoFullFlush) {
            // Typical CCoins structures on disk are around 128 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace(128 * 2 * 2 * pcoinsTip->GetCacheSize()))
                return state.Error("out of disk space");
            // Flush the chainstate (which may refer to block index entries).
            if (!pcoinsTip->Flush())
                return AbortNode(state, "Failed to write to coin database");
            nLastFlush = nNow;
        }
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}

void FlushStateToDisk() {
    CValidationState state;
    if ( KOMODO_NSPV_FULLNODE )
        FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

void FlushStateToDiskPeriodic() {
    CValidationState state;
    if ( KOMODO_NSPV_FULLNODE )
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);
}

void PruneAndFlush() {
    CValidationState state;
    fCheckForPruning = true;
    FlushStateToDisk(state, FLUSH_STATE_NONE);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex *pindexNew) {
    const CChainParams& chainParams = Params();
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);
    KOMODO_NEWBLOCKS++;
    double progress;
    if ( chainName.isKMD() ) {
        progress = Checkpoints::GuessVerificationProgress(chainParams.Checkpoints(), chainActive.Tip());
    } else {
	    int32_t longestchain = komodo_longestchain();
	    progress = (longestchain > 0 ) ? (double) chainActive.Height() / longestchain : 1.0;
    }

    LogPrintf("%s: new best=%s  height=%d  log2_work=%.8g  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utx)\n", __func__,
              chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
              log(chainActive.Tip()->nChainWork.getdouble())/log(2.0),
              (unsigned long)chainActive.Tip()->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()), progress,
              pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());


    LogPrintf("%s: Chainwork %s\n", __func__, chainActive.Tip()->nChainWork.GetHex());

    cvBlockChange.notify_all();

    /*
    // https://github.com/zcash/zcash/issues/3992 -> https://github.com/zcash/zcash/commit/346d11d3eb2f8162df0cb00b1d1f49d542495198

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload() && !fWarned)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = chainActive.Tip();
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("%s: %d of last 100 blocks above version %d\n", __func__, nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100/2)
        {
            // strMiscWarning is read by GetWarnings(), called by the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete; upgrade required!");
            CAlert::Notify(strMiscWarning, true);
            fWarned = true;
        }
    }
    */
}

/**
 * Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and
 * mempool.removeWithoutBranchId after this, with cs_main held.
 */
bool static DisconnectTip(CValidationState &state, bool fBare = false) {
    CBlockIndex *pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete,1))
        return AbortNode(state, "Failed to read block");
    {
        int32_t notarizedht,prevMoMheight; uint256 notarizedhash,txid;
        notarizedht = komodo_notarized_height(&prevMoMheight,&notarizedhash,&txid);
        if ( block.GetHash() == notarizedhash )
        {
            fprintf(stderr,"DisconnectTip trying to disconnect notarized block at ht.%d\n",(int32_t)pindexDelete->nHeight);
            return state.DoS(100, error("AcceptBlock(): DisconnectTip trying to disconnect notarized blockht.%d",(int32_t)pindexDelete->nHeight),
                        REJECT_INVALID, "past-notarized-height");
        }
    }
    // Apply the block atomically to the chain state.
    uint256 sproutAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(SAPLING);
    uint256 saplingFrontierAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(SAPLINGFRONTIER);
    uint256 orchardFrontierAnchorBeforeDisconnect = pcoinsTip->GetBestAnchor(ORCHARDFRONTIER);
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (!DisconnectBlock(block, state, pindexDelete, view))
            return error("DisconnectTip(): DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
        DisconnectNotarisations(block);
    }
    pindexDelete->segid = -2;
    pindexDelete->nNotaryPay = 0;
    pindexDelete->newcoins = 0;
    pindexDelete->zfunds = 0;

    LogPrint("bench", "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    uint256 sproutAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(SPROUT);
    uint256 saplingAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(SAPLING);
    uint256 saplingFrontierAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(SAPLINGFRONTIER);
    uint256 orchardFrontierAnchorAfterDisconnect = pcoinsTip->GetBestAnchor(ORCHARDFRONTIER);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
        return false;

    if (!fBare) {
        // resurrect mempool transactions from the disconnected block.
        for (int i = 0; i < block.vtx.size(); i++)
        {
            // ignore validation errors in resurrected transactions
            CTransaction &tx = block.vtx[i];
            list<CTransaction> removed;
            CValidationState stateDummy;

            // don't keep staking or invalid transactions
            if (tx.IsCoinBase() || (i == block.vtx.size()-1 && komodo_newStakerActive(0, pindexDelete->nTime) == 0 && komodo_isPoS((CBlock *)&block,pindexDelete->nHeight,0) != 0) || !AcceptToMemoryPool(mempool, stateDummy, tx, false, NULL))
            {
                mempool.remove(tx, removed, true);
            }
        }
        if (sproutAnchorBeforeDisconnect != sproutAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(sproutAnchorBeforeDisconnect, SPROUT);
        }
        if (saplingAnchorBeforeDisconnect != saplingAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(saplingAnchorBeforeDisconnect, SAPLING);
        }
        if (saplingFrontierAnchorBeforeDisconnect != saplingFrontierAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(saplingFrontierAnchorBeforeDisconnect, SAPLINGFRONTIER);
        }
        if (orchardFrontierAnchorBeforeDisconnect != orchardFrontierAnchorAfterDisconnect) {
            // The anchor may not change between block disconnects,
            // in which case we don't want to evict from the mempool yet!
            mempool.removeWithAnchor(orchardFrontierAnchorBeforeDisconnect, ORCHARDFRONTIER);
        }
    }

    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);

    // Get the current commitment tree
    SproutMerkleTree newSproutTree;
    SaplingMerkleTree newSaplingTree;
    SaplingMerkleFrontier newSaplingFrontierTree;
    OrchardMerkleFrontier newOrchardFrontierTree;
    assert(pcoinsTip->GetSproutAnchorAt(pcoinsTip->GetBestAnchor(SPROUT), newSproutTree));
    assert(pcoinsTip->GetSaplingAnchorAt(pcoinsTip->GetBestAnchor(SAPLING), newSaplingTree));
    assert(pcoinsTip->GetSaplingFrontierAnchorAt(pcoinsTip->GetBestAnchor(SAPLINGFRONTIER), newSaplingFrontierTree));
    assert(pcoinsTip->GetOrchardFrontierAnchorAt(pcoinsTip->GetBestAnchor(ORCHARDFRONTIER), newOrchardFrontierTree));
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    std::vector<uint256> TxToRemove;
    for (int i = 0; i < block.vtx.size(); i++)
    {
        CTransaction &tx = block.vtx[i];
        //if ((i == (block.vtx.size() - 1)) && ((ASSETCHAINS_STAKED != 0 && (komodo_isPoS((CBlock *)&block) != 0))))
        if ( komodo_newStakerActive(0, pindexDelete->nTime) == 0 && i == block.vtx.size()-1 && komodo_isPoS((CBlock *)&block,pindexDelete->nHeight,0) != 0 )
        {
#ifdef ENABLE_WALLET
             // new staking tx cannot be accepted to mempool and expires in 1 block, so no need for this! :D
             if ( !GetBoolArg("-disablewallet", false) && KOMODO_NSPV_FULLNODE )
                 pwalletMain->EraseFromWallet(tx.GetHash());
#endif
        } else {
            std::vector<CTransaction> vtx;
            vtx.emplace_back(tx);
            SyncWithWallets(vtx, NULL, pindexDelete->nHeight);
        }
    }
    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexDelete, &block, newSproutTree, newSaplingTree, false);

    return true;
}

int32_t komodo_activate_sapling(CBlockIndex *pindex)
{
    uint32_t blocktime,prevtime; CBlockIndex *prev; int32_t i,transition=0,height,prevht;
    int32_t activation = 0;
    if ( pindex == 0 )
    {
        fprintf(stderr,"komodo_activate_sapling null pindex\n");
        return(0);
    }
    height = pindex->nHeight;
    blocktime = (uint32_t)pindex->nTime;
    //fprintf(stderr,"komodo_activate_sapling.%d starting blocktime %u cmp.%d\n",height,blocktime,blocktime > KOMODO_SAPLING_ACTIVATION);

    // avoid trying unless we have at least 30 blocks
    if (height < 30)
        return(0);

    for (i=0; i<30; i++)
    {
        if ( (prev= pindex->pprev) == 0 )
            break;
        pindex = prev;
    }
    if ( i != 30 )
    {
        fprintf(stderr,"couldnt go backwards 30 blocks\n");
        return(0);
    }
    height = pindex->nHeight;
    blocktime = (uint32_t)pindex->nTime;
    //fprintf(stderr,"starting blocktime %u cmp.%d\n",blocktime,blocktime > KOMODO_SAPLING_ACTIVATION);
    if ( blocktime > KOMODO_SAPLING_ACTIVATION ) // find the earliest transition
    {
        while ( (prev= pindex->pprev) != 0 )
        {
            prevht = prev->nHeight;
            prevtime = (uint32_t)prev->nTime;
            //fprintf(stderr,"(%d, %u).%d -> (%d, %u).%d\n",prevht,prevtime,prevtime > KOMODO_SAPLING_ACTIVATION,height,blocktime,blocktime > KOMODO_SAPLING_ACTIVATION);
            if ( prevht+1 != height )
            {
                fprintf(stderr,"komodo_activate_sapling: unexpected non-contiguous ht %d vs %d\n",prevht,height);
                return(0);
            }
            if ( prevtime <= KOMODO_SAPLING_ACTIVATION && blocktime > KOMODO_SAPLING_ACTIVATION )
            {
                activation = height + 60;
                fprintf(stderr,"%s transition at %d (%d, %u) -> (%d, %u)\n",chainName.symbol().c_str(),height,prevht,prevtime,height,blocktime);
            }
            if ( prevtime < KOMODO_SAPLING_ACTIVATION-3600*24 )
                break;
            pindex = prev;
            height = prevht;
            blocktime = prevtime;
        }
    }
    if ( activation != 0 )
    {
        komodo_setactivation(activation);
        fprintf(stderr,"%s sapling activation at %d\n",chainName.symbol().c_str(),activation);
        ASSETCHAINS_SAPLING = activation;
    }
    return activation;
}

int32_t komodo_activate_orchard(CBlockIndex *pindex)
{
    uint32_t blocktime,prevtime; CBlockIndex *prev; int32_t i,transition=0,height,prevht;
    int32_t activation = 0;
    if ( pindex == 0 )
    {
        fprintf(stderr,"komodo_activate_orchard null pindex\n");
        return(0);
    }
    height = pindex->nHeight;
    blocktime = (uint32_t)pindex->nTime;
    fprintf(stderr,"komodo_activate_orchard.%d starting blocktime %u cmp.%d\n",height,blocktime,blocktime > KOMODO_SAPLING_ACTIVATION);

    // avoid trying unless we have at least 30 blocks
    if (height < 30)
        return(0);

    for (i=0; i<30; i++)
    {
        if ( (prev= pindex->pprev) == 0 )
            break;
        pindex = prev;
    }
    if ( i != 30 )
    {
        fprintf(stderr,"couldnt go backwards 30 blocks\n");
        return(0);
    }
    height = pindex->nHeight;
    blocktime = (uint32_t)pindex->nTime;
    fprintf(stderr,"KOMODO_ORCHARD_ACTIVATION %u\n", KOMODO_ORCHARD_ACTIVATION);
    fprintf(stderr,"starting orchard blocktime %u cmp.%d\n",blocktime,blocktime > KOMODO_ORCHARD_ACTIVATION);
    if ( blocktime > KOMODO_ORCHARD_ACTIVATION ) // find the earliest transition
    {
        while ( (prev= pindex->pprev) != 0 )
        {
            prevht = prev->nHeight;
            prevtime = (uint32_t)prev->nTime;
            fprintf(stderr,"(%d, %u).%d -> (%d, %u).%d\n",prevht,prevtime,prevtime > KOMODO_ORCHARD_ACTIVATION,height,blocktime,blocktime > KOMODO_ORCHARD_ACTIVATION);
            if ( prevht+1 != height )
            {
                fprintf(stderr,"komodo_activate_orchard: unexpected non-contiguous ht %d vs %d\n",prevht,height);
                return(0);
            }
            if ( prevtime <= KOMODO_ORCHARD_ACTIVATION && blocktime > KOMODO_ORCHARD_ACTIVATION )
            {
                activation = height + 60;
                fprintf(stderr,"%s transition at %d (%d, %u) -> (%d, %u)\n",chainName.symbol().c_str(),height,prevht,prevtime,height,blocktime);
            }
            if ( prevtime < KOMODO_ORCHARD_ACTIVATION-3600*24 )
                break;
            pindex = prev;
            height = prevht;
            blocktime = prevtime;
        }
    }
    if ( activation != 0 )
    {
        komodo_setorchard(activation);
        fprintf(stderr,"%s orchard activation at %d\n",chainName.symbol().c_str(),activation);
        ASSETCHAINS_ORCHARD = activation;
    }
    return activation;
}
static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/***
 * @brief Connect a new block to chainActive.
 * @note You probably want to call mempool.removeWithoutBranchId after this, with cs_main held.
 * @param[out] state holds the state
 * @param pindexNew the new index
 * @param pblock a pointer to a CBlock (nullptr will load it from disk)
 * @returns true on success
 */
bool ConnectTip(CValidationState &state, CBlockIndex *pindexNew, CBlock *pblock)
{
    assert(pindexNew->pprev == chainActive.Tip());
    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew,1))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    KOMODO_CONNECTING = (int32_t)pindexNew->nHeight;
    // Get the current commitment tree
    SproutMerkleTree oldSproutTree;
    SaplingMerkleTree oldSaplingTree;
    SaplingMerkleFrontier oldSaplingFrontierTree;
    OrchardMerkleFrontier oldOrchardFrontierTree;
    if ( KOMODO_NSPV_FULLNODE )
    {
        assert(pcoinsTip->GetSproutAnchorAt(pcoinsTip->GetBestAnchor(SPROUT), oldSproutTree));
        assert(pcoinsTip->GetSaplingAnchorAt(pcoinsTip->GetBestAnchor(SAPLING), oldSaplingTree));
        assert(pcoinsTip->GetSaplingFrontierAnchorAt(pcoinsTip->GetBestAnchor(SAPLINGFRONTIER), oldSaplingFrontierTree));
        assert(pcoinsTip->GetOrchardFrontierAnchorAt(pcoinsTip->GetBestAnchor(ORCHARDFRONTIER), oldOrchardFrontierTree));
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    LogPrint("bench", "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    int64_t nTime3;
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, false, true);
        KOMODO_CONNECTING = -1;
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
            {
                InvalidBlockFound(pindexNew, state);
            }
            return error("ConnectTip(): ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        mapBlockSource.erase(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros(); nTimeConnectTotal += nTime3 - nTime2;
        LogPrint("bench", "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        if ( KOMODO_NSPV_FULLNODE )
            assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros(); nTimeFlush += nTime4 - nTime3;
    LogPrint("bench", "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);
    // Write the chain state to disk, if necessary.
    if ( KOMODO_NSPV_FULLNODE )
    {
        if (!FlushStateToDisk(state, FLUSH_STATE_IF_NEEDED))
            return false;
    }
    int64_t nTime5 = GetTimeMicros(); nTimeChainState += nTime5 - nTime4;
    LogPrint("bench", "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);
    // Remove conflicting transactions from the mempool.
    list<CTransaction> txConflicted;
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload());

    // Remove transactions that expire at new block height from mempool
    mempool.removeExpired(pindexNew->nHeight);

    // Update chainActive & related variables.
    UpdateTip(pindexNew);
    if ( KOMODO_NSPV_FULLNODE )
    {
        // Tell wallet about transactions that went from mempool
        // to conflicted:
        int64_t nTimeConflicted = GetTimeMicros();
        std::vector<CTransaction> vConflictedTx;
        BOOST_FOREACH(const CTransaction &tx, txConflicted) {
             vConflictedTx.emplace_back(tx);
        }
        SyncWithWallets(vConflictedTx, NULL, pindexNew->nHeight);
        LogPrint("bench", "     - Connect Sync Conflicted Txes with Wallet: %.2fms\n", (GetTimeMicros() - nTimeConflicted) * 0.001);

        // ... and about transactions that got confirmed:
        int64_t nTimeSyncTx = GetTimeMicros();
        SyncWithWallets(pblock->vtx, pblock, pindexNew->nHeight);
        LogPrint("bench", "     - Connect Sync Non-Conflicted Txes with Wallet: %.2fms\n", (GetTimeMicros() - nTimeSyncTx) * 0.001);
    }
    // Update cached incremental witnesses
    GetMainSignals().ChainTip(pindexNew, pblock, oldSproutTree, oldSaplingTree, true);

    EnforceNodeDeprecation(pindexNew->nHeight);

    int64_t nTime6 = GetTimeMicros(); nTimePostConnect += nTime6 - nTime5; nTimeTotal += nTime6 - nTime1;
    LogPrint("bench", "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint("bench", "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    if ( KOMODO_LONGESTCHAIN != 0 && (pindexNew->nHeight == KOMODO_LONGESTCHAIN || pindexNew->nHeight == KOMODO_LONGESTCHAIN+1) )
        KOMODO_INSYNC = (int32_t)pindexNew->nHeight;
    else KOMODO_INSYNC = 0;
    if ( KOMODO_NSPV_FULLNODE )
    {
        if ( ASSETCHAINS_SAPLING <= 0 && pindexNew->nTime > KOMODO_SAPLING_ACTIVATION - 24*3600 )
            komodo_activate_sapling(pindexNew);
        if ( ASSETCHAINS_ORCHARD <= 0 && pindexNew->nTime > KOMODO_ORCHARD_ACTIVATION - 24*3600 )
            komodo_activate_orchard(pindexNew);
        if ( ASSETCHAINS_CC != 0 && KOMODO_SNAPSHOT_INTERVAL != 0 && (pindexNew->nHeight % KOMODO_SNAPSHOT_INTERVAL) == 0 && pindexNew->nHeight >= KOMODO_SNAPSHOT_INTERVAL )
        {
            uint64_t start = time(NULL);
            if ( !komodo_dailysnapshot(pindexNew->nHeight) )
            {
                fprintf(stderr, "daily snapshot failed, please reindex your chain\n");
                StartShutdown();
            }
            fprintf(stderr, "snapshot completed in: %d seconds\n", (int32_t)(time(NULL)-start));
        }
    }
    return true;
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain() {
    do {
        CBlockIndex *pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex *pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex *pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while(true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates() {
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(bool fSkipdpow, CValidationState &state, CBlockIndex *pindexMostWork, CBlock *pblock) {
    AssertLockHeld(cs_main);
    bool fInvalidFound = false;
    const CBlockIndex *pindexOldTip = chainActive.Tip();
    const CBlockIndex *pindexFork = chainActive.FindFork(pindexMostWork);

    // stop trying to reorg if the reorged chain is before last notarized height.
    // stay on the same chain tip!
    int32_t notarizedht,prevMoMheight; uint256 notarizedhash,txid;
    notarizedht = komodo_notarized_height(&prevMoMheight,&notarizedhash,&txid);
    if ( !fSkipdpow && pindexFork != 0 && pindexOldTip->nHeight > notarizedht && pindexFork->nHeight < notarizedht )
    {
        LogPrintf("pindexOldTip->nHeight.%d > notarizedht %d && pindexFork->nHeight.%d is < notarizedht %d, so ignore it\n",(int32_t)pindexOldTip->nHeight,notarizedht,(int32_t)pindexFork->nHeight,notarizedht);
        // *** DEBUG ***
        if (1)
        {
            const CBlockIndex *pindexLastNotarized = mapBlockIndex[notarizedhash];
            auto msg = "- " + strprintf(_("Current tip : %s, height %d, work %s"),
                                pindexOldTip->phashBlock->GetHex(), pindexOldTip->nHeight, pindexOldTip->nChainWork.GetHex()) + "\n" +
                "- " + strprintf(_("New tip     : %s, height %d, work %s"),
                                pindexMostWork->phashBlock->GetHex(), pindexMostWork->nHeight, pindexMostWork->nChainWork.GetHex()) + "\n" +
                "- " + strprintf(_("Fork point  : %s, height %d"),
                                pindexFork->phashBlock->GetHex(), pindexFork->nHeight) + "\n" +
                "- " + strprintf(_("Last ntrzd  : %s, height %d"),
                                pindexLastNotarized->phashBlock->GetHex(), pindexLastNotarized->nHeight);
            LogPrintf("[ Debug ]\n%s\n",msg);

            int nHeight = pindexFork ? pindexFork->nHeight : -1;
            int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);

            LogPrintf("[ Debug ] nHeight = %d, nTargetHeight = %d\n", nHeight, nTargetHeight);

            CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
            while (pindexIter && pindexIter->nHeight != nHeight) {
                LogPrintf("[ Debug -> New blocks list ] %s, height %d\n", pindexIter->phashBlock->GetHex(), pindexIter->nHeight);
                pindexIter = pindexIter->pprev;
            }
        }
// ARRR notary exception
        CValidationState tmpstate;
        InvalidateBlock(tmpstate,pindexMostWork); // trying to invalidate longest chain, which tried to reorg notarized chain (in case of fork point below last notarized block)
        return state.DoS(100, error("ActivateBestChainStep(): pindexOldTip->nHeight.%d > notarizedht %d && pindexFork->nHeight.%d is < notarizedht %d, so ignore it",(int32_t)pindexOldTip->nHeight,notarizedht,(int32_t)pindexFork->nHeight,notarizedht),
                REJECT_INVALID, "past-notarized-height");
    }
    // - On ChainDB initialization, pindexOldTip will be null, so there are no removable blocks.
    // - If pindexMostWork is in a chain that doesn't have the same genesis block as our chain,
    //   then pindexFork will be null, and we would need to remove the entire chain including
    //   our genesis block. In practice this (probably) won't happen because of checks elsewhere.
    auto reorgLength = pindexOldTip ? pindexOldTip->nHeight - (pindexFork ? pindexFork->nHeight : -1) : 0;
    assert(MAX_REORG_LENGTH > 0);//, "We must be able to reorg some distance");
    if ( reorgLength > MAX_REORG_LENGTH)
    {
        auto msg = strprintf(_(
                               "A block chain reorganization has been detected that would roll back %d blocks! "
                               "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
                               ), reorgLength, MAX_REORG_LENGTH) + "\n\n" +
        _("Reorganization details") + ":\n" +
        "- " + strprintf(_("Current tip: %s, height %d, work %s\n"),
                         pindexOldTip->phashBlock->GetHex(), pindexOldTip->nHeight, pindexOldTip->nChainWork.GetHex()) +
        "- " + strprintf(_("New tip:     %s, height %d, work %s\n"),
                         pindexMostWork->phashBlock->GetHex(), pindexMostWork->nHeight, pindexMostWork->nChainWork.GetHex()) +
        "- " + strprintf(_("Fork point:  %s %s, height %d"),
                         chainName.symbol().c_str(),pindexFork->phashBlock->GetHex(), pindexFork->nHeight) + "\n\n" +
        _("Please help, human!");
        LogPrintf("*** %s\nif you launch with -maxreorg=%d it might be able to resolve this automatically", msg,reorgLength+10);
        fprintf(stderr,"*** %s\nif you launch with -maxreorg=%d it might be able to resolve this automatically", msg.c_str(),reorgLength+10);
        uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;

    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state))
            return false;
        fBlocksDisconnected = true;
    }
    if ( KOMODO_REWIND != 0 )
    {
        CBlockIndex *tipindex;
        fprintf(stderr,">>>>>>>>>>> rewind start ht.%d -> KOMODO_REWIND.%d\n",chainActive.Tip()->nHeight,KOMODO_REWIND);
        while ( KOMODO_REWIND > 0 && (tipindex= chainActive.Tip()) != 0 && tipindex->nHeight > KOMODO_REWIND )
        {
            fBlocksDisconnected = true;
            fprintf(stderr,"%d ",(int32_t)tipindex->nHeight);
            InvalidateBlock(state,tipindex);
            if ( !DisconnectTip(state) )
                break;
        }
        fprintf(stderr,"reached rewind.%d, best to do: ./komodo-cli -ac_name=%s stop\n",KOMODO_REWIND,chainName.symbol().c_str());
        sleep(20);
        fprintf(stderr,"resuming normal operations\n");
        KOMODO_REWIND = 0;
        //return(true);
    }
    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex *pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH(CBlockIndex *pindexConnect, vpindexToConnect)
        {
            if (!ConnectTip(state, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL))
            {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    }
    mempool.removeWithoutBranchId(
                                  CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, Params().GetConsensus()));
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}

static void NotifyHeaderTip() {
    bool fNotify = false;
    bool fInitialBlockDownload = false;
    static CBlockIndex* pindexHeaderOld = nullptr;
    CBlockIndex* pindexHeader = nullptr;
    {
        LOCK(cs_main);
        pindexHeader = pindexBestHeader;

        if (pindexHeader != pindexHeaderOld) {
            fNotify = true;
            fInitialBlockDownload = IsInitialBlockDownload();
            pindexHeaderOld = pindexHeader;
        }
    }
    // Send block tip changed notifications without cs_main
    if (fNotify) {
        uiInterface.NotifyHeaderTip(fInitialBlockDownload, pindexHeader);
    }
}

/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(bool fSkipdpow, CValidationState &state, bool fNotifyUI, CBlock *pblock) {
    CBlockIndex *pindexNewTip = NULL;
    CBlockIndex *pindexMostWork = NULL;
    const CChainParams& chainParams = Params();
    do {
        boost::this_thread::interruption_point();

        if (ShutdownRequested())
            break;

        const CBlockIndex *pindexFork;

        bool fInitialDownload;
        {
            LOCK(cs_main);
            CBlockIndex *pindexOldTip = chainActive.Tip();

            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip())
                return true;

            if (!ActivateBestChainStep(fSkipdpow, state, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL))
                return false;
            pindexNewTip = chainActive.Tip();
            fInitialDownload = IsInitialBlockDownload();

            //Notify UI Startup screen
            if (pindexNewTip->nHeight % 100 == 0 && fNotifyUI)
            {
                uiInterface.InitMessage(_(("Activating best chain - Currently on block " + std::to_string(pindexNewTip->nHeight)).c_str()));
            }
        }
        // When we reach this point, we switched to a new tip (stored in pindexNewTip).

        // Notifications/callbacks that can run without cs_main
        if (!fInitialDownload) {
            uint256 hashNewTip = pindexNewTip->GetBlockHash();
            // Relay inventory, but don't relay old inventory during initial block download.
            int nBlockEstimate = 0;
            if (fCheckpointsEnabled)
                nBlockEstimate = Checkpoints::GetTotalBlocksEstimate(chainParams.Checkpoints());
            // Don't relay blocks if pruning -- could cause a peer to try to download, resulting
            // in a stalled download if the block file is pruned before the request.
            if (nLocalServices & NODE_NETWORK)
            {
                int ht = 0;
                {
                    LOCK(cs_main);
                    ht = chainActive.Height();
                }
                LOCK(cs_vNodes);
                for(CNode* pnode : vNodes)
                    if (ht > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate))
                        pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
            }
            // Notify external listeners about the new tip.
            GetMainSignals().UpdatedBlockTip(pindexNewTip);

            //Notify UI Startup screen
            if (pindexNewTip->nHeight % 100 == 0)
            {
                uiInterface.InitMessage(_(("Activating best chain - Currently on block " + std::to_string(pindexNewTip->nHeight)).c_str()));
            }
        }

        //Notify UI
        uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);

    } while(pindexMostWork != pindexNewTip);
    CheckBlockIndex();

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}

bool InvalidateBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex)) {
        CBlockIndex *pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state)) {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            mempool.removeWithoutBranchId(
                                          CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, Params().GetConsensus()));
            return false;
        }
    }
    //LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if ((it->second != 0) && it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
    mempool.removeWithoutBranchId(
                                  CurrentEpochBranchId(chainActive.Tip()->nHeight + 1, Params().GetConsensus()));
    return true;
}

bool ReconsiderBlock(CValidationState& state, CBlockIndex *pindex) {
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if ((it->second != 0) && !it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}

CBlockIndex* AddToBlockIndex(const CBlockHeader& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);

    // the following block is for debugging, comment when not needed
    /*
    std::vector<BlockMap::iterator> vrit;
    for (BlockMap::iterator bit = mapBlockIndex.begin(); bit != mapBlockIndex.end(); bit++)
    {
        if (bit->second == NULL)
            vrit.push_back(bit);
    }
    if (!vrit.empty())
    {
        printf("found %d NULL blocks in mapBlockIndex\n", vrit.size());
    }
    */

    if (it != mapBlockIndex.end())
    {
        if ( it->second != 0 ) // vNodes.size() >= KOMODO_LIMITED_NETWORKSIZE, change behavior to allow komodo_ensure to work
        {
            // this is the strange case where somehow the hash is in the mapBlockIndex via as yet undetermined process, but the pindex for the hash is not there. Theoretically it is due to processing the block headers, but I have seen it get this case without having received it from the block headers or anywhere else... jl777
            //fprintf(stderr,"addtoblockindex already there %p\n",it->second);
            return it->second;
        }
        if ( miPrev != mapBlockIndex.end() && (*miPrev).second == 0 )
        {
            //fprintf(stderr,"edge case of both block and prevblock in the strange state\n");
            return(0); // return here to avoid the state of pindex->nHeight not set and pprev NULL
        }
    }
    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    if (miPrev != mapBlockIndex.end())
    {
        if ( (pindexNew->pprev = (*miPrev).second) != 0 )
            pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        else fprintf(stderr,"unexpected null pprev %s\n",hash.ToString().c_str());
        pindexNew->BuildSkip();
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if (pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork)
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);
    //fprintf(stderr,"added to block index %s %p\n",hash.ToString().c_str(),pindexNew);
    mi->second = pindexNew;
    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock &block, CValidationState& state, CBlockIndex *pindexNew, const CDiskBlockPos& pos)
{
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;

    // the following values are computed here only for the genesis block
    CAmount chainSupplyDelta = 0;
    CAmount transparentValueDelta = 0;
    CAmount burnedAmountDelta = 0;

    CAmount sproutValue = 0;
    CAmount saplingValue = 0;
    CAmount orchardValue = 0;
    for (auto tx : block.vtx) {
        // For the genesis block only, compute the chain supply delta and the transparent
        // output total.
        if (pindexNew->pprev == nullptr) {
            chainSupplyDelta = tx.GetValueOut();
            for (const auto& out : tx.vout) {
                if (!out.scriptPubKey.IsUnspendable()) {
                    transparentValueDelta += out.nValue;
                } else {
                    burnedAmountDelta += out.nValue;
                }
            }
        }
        // Negative valueBalance "takes" money from the transparent value pool
        // and adds it to the Sapling value pool. Positive valueBalance "gives"
        // money to the transparent value pool, removing from the Sapling value
        // pool. So we invert the sign here.
        saplingValue += -tx.GetValueBalanceSapling();

        // valueBalanceOrchard behaves the same way as valueBalanceSapling.
        orchardValue += -tx.GetOrchardBundle().GetValueBalance();

        for (auto js : tx.vjoinsplit) {
            sproutValue += js.vpub_old;
            sproutValue -= js.vpub_new;
        }
    }

    // These values can only be computed here for the genesis block.
    // For all other blocks, we update them in ConnectBlock instead.
    if (pindexNew->pprev == nullptr) {
        pindexNew->nChainSupplyDelta = chainSupplyDelta;
        pindexNew->nTransparentValue = transparentValueDelta;
        pindexNew->nBurnedAmountDelta = burnedAmountDelta;
    } else {
        pindexNew->nChainSupplyDelta = std::nullopt;
        pindexNew->nTransparentValue = std::nullopt;
        pindexNew->nBurnedAmountDelta = std::nullopt;
    }

    pindexNew->nChainTotalSupply = std::nullopt;
    pindexNew->nChainTransparentValue = std::nullopt;
    pindexNew->nChainTotalBurned = std::nullopt;

    pindexNew->nSproutValue = sproutValue;
    pindexNew->nChainSproutValue = std::nullopt;
    pindexNew->nSaplingValue = saplingValue;
    pindexNew->nChainSaplingValue = std::nullopt;
    pindexNew->nOrchardValue = orchardValue;
    pindexNew->nChainOrchardValue = std::nullopt;

    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex *pindex = queue.front();
            queue.pop_front();
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;

            if (pindex->pprev) {
                // Transparent value and chain total supply are added to the
                // block index only in `ConnectBlock`, because that's the only
                // place that we have a valid coins view with which to compute
                // the transparent input value and fees.

                // Calculate the block's effect on the Sprout chain value pool balance.
                if (pindex->pprev->nChainSproutValue && pindex->nSproutValue) {
                    pindex->nChainSproutValue = *pindex->pprev->nChainSproutValue + *pindex->nSproutValue;
                } else {
                    pindex->nChainSproutValue = std::nullopt;
                }

                // calculate the block's effect on the chain's net Sapling value
                if (pindex->pprev->nChainSaplingValue) {
                    pindex->nChainSaplingValue = *pindex->pprev->nChainSaplingValue + pindex->nSaplingValue;
                } else {
                    pindex->nChainSaplingValue = std::nullopt;
                }

                // Calculate the block's effect on the Orchard chain value pool balance.
                if (pindex->pprev->nChainOrchardValue) {
                    pindex->nChainOrchardValue = *pindex->pprev->nChainOrchardValue + pindex->nOrchardValue;
                } else {
                    pindex->nChainOrchardValue = std::nullopt;
                }
            } else {
                pindex->nChainTotalSupply = pindex->nChainSupplyDelta;
                pindex->nChainTransparentValue = pindex->nTransparentValue;
                pindex->nChainTotalBurned = pindex->nBurnedAmountDelta;
                pindex->nChainSproutValue = pindex->nSproutValue;
                pindex->nChainSaplingValue = pindex->nSaplingValue;
                pindex->nChainOrchardValue = pindex->nOrchardValue;
            }

            // Fall back to hardcoded Sprout value pool balance
            //FallbackSproutValuePoolBalance(pindex, chainparams);

            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}

bool FindBlockPos(int32_t tmpflag,CValidationState &state, CDiskBlockPos &pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown)
{
    std::vector<CBlockFileInfo> *ptr; int *lastfilep;
    LOCK(cs_LastBlockFile);

    unsigned int nFile,maxTempFileSize;

    if ( tmpflag != 0 )
    {
        ptr = &tmpBlockFiles;
        nFile = nLastTmpFile;
        lastfilep = &nLastTmpFile;
        if (tmpBlockFiles.size() <= nFile) {
            tmpBlockFiles.resize(nFile + 1);
        }
        if ( nFile == 0 )
            maxTempFileSize = maxTempFileSize0;
        else if ( nFile == 1 )
            maxTempFileSize = maxTempFileSize1;
    }
    else
    {
        ptr = &vinfoBlockFile;
        lastfilep = &nLastBlockFile;
        nFile = fKnown ? pos.nFile : nLastBlockFile;
        if (vinfoBlockFile.size() <= nFile) {
            vinfoBlockFile.resize(nFile + 1);
        }
    }

    if (!fKnown) {
        bool tmpfileflag = false;
        while ( (*ptr)[nFile].nSize + nAddSize >= ((tmpflag != 0) ? maxTempFileSize : MAX_BLOCKFILE_SIZE) ) {
            if ( tmpflag != 0 && tmpfileflag )
                break;
            nFile++;
            if ((*ptr).size() <= nFile) {
                (*ptr).resize(nFile + 1);
            }
            tmpfileflag = true;
        }
        pos.nFile = nFile + tmpflag*TMPFILE_START;
        pos.nPos = (*ptr)[nFile].nSize;
    }
    if (nFile != *lastfilep) {
        if (!fKnown) {
            LogPrintf("Leaving block file %i: %s\n", nFile, (*ptr)[nFile].ToString());
        }
        FlushBlockFile(!fKnown);
        //fprintf(stderr, "nFile = %i size.%li maxTempFileSize0.%u maxTempFileSize1.%u\n",nFile,tmpBlockFiles.size(),maxTempFileSize0,maxTempFileSize1);
        if ( tmpflag != 0 && tmpBlockFiles.size() >= 3 )
        {
            if ( nFile == 1 ) // Trying to get to second temp file.
            {
                if (!PruneOneBlockFile(true,TMPFILE_START+1))
                {
                    // file 1 is not ready to be used yet increase file 0's size.
                    fprintf(stderr, "Cant clear file 1!\n");
                    // We will reset the position to the end of the first file, even if its over max size.
                    nFile = 0;
                    pos.nFile = TMPFILE_START;
                    pos.nPos = (*ptr)[0].nSize;
                    // Increase temp file one's max size by a chunk, so we wait a reasonable time to recheck the other file.
                    maxTempFileSize0 += BLOCKFILE_CHUNK_SIZE;
                }
                else
                {
                    // The file 1 is able to be used now. Reset max size, and set nfile to use file 1.
                    fprintf(stderr, "CLEARED file 1!\n");
                    maxTempFileSize0 = MAX_TEMPFILE_SIZE;
                    nFile = 1;
                    tmpBlockFiles[1].SetNull();
                    pos.nFile = TMPFILE_START+1;
                    pos.nPos = (*ptr)[1].nSize;
                    boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
                    LogPrintf("Prune: deleted temp blk (%05u)\n",nFile);
                }
                if ( 0 && tmpflag != 0 )
                    fprintf(stderr,"pos.nFile %d nPos %u\n",pos.nFile,pos.nPos);
            }
            else if ( nFile == 2 ) // Trying to get to third temp file.
            {
                if (!PruneOneBlockFile(true,TMPFILE_START))
                {
                    fprintf(stderr, "Cant clear file 0!\n");
                    // We will reset the position to the end of the second block file, even if its over max size.
                    nFile = 1;
                    pos.nFile = TMPFILE_START+1;
                    pos.nPos = (*ptr)[1].nSize;
                    // Increase temp file one's max size by a chunk, so we wait a reasonable time to recheck the other file.
                    maxTempFileSize1 += BLOCKFILE_CHUNK_SIZE;
                }
                else
                {
                    // The file 0 is able to be used now. Reset max size, and set nfile to use file 0.
                    fprintf(stderr, "CLEARED file 0!\n");
                    maxTempFileSize1 = MAX_TEMPFILE_SIZE;
                    nFile = 0;
                    tmpBlockFiles[0].SetNull();
                    pos.nFile = TMPFILE_START;
                    pos.nPos = (*ptr)[0].nSize;
                    boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
                    LogPrintf("Prune: deleted temp blk (%05u)\n",nFile);
                }
                if ( 0 && tmpflag != 0 )
                    fprintf(stderr,"pos.nFile %d nPos %u\n",pos.nFile,pos.nPos);
            }
            //sleep(30);
        }
        //fprintf(stderr, "nFile = %i size.%li maxTempFileSize0.%u maxTempFileSize1.%u\n",nFile,tmpBlockFiles.size(),maxTempFileSize0,maxTempFileSize1); sleep(30);
        *lastfilep = nFile;
        //fprintf(stderr, "*lastfilep = %i\n",*lastfilep);
    }

    (*ptr)[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        (*ptr)[nFile].nSize = std::max(pos.nPos + nAddSize, (*ptr)[nFile].nSize);
    else
        (*ptr)[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = ((*ptr)[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (fPruneMode)
                fCheckForPruning = true;
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE *file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            }
            else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile + tmpflag*TMPFILE_START);
    return true;
}

bool FindUndoPos(CValidationState &state, int nFile, CDiskBlockPos &pos, unsigned int nAddSize)
{
    std::vector<CBlockFileInfo> *ptr; int *lastfilep;
    LOCK(cs_LastBlockFile);
    pos.nFile = nFile;
    if ( nFile >= TMPFILE_START )
    {
        fprintf(stderr,"skip tmp undo\n");
        return(false);
        nFile %= TMPFILE_START;
        ptr = &tmpBlockFiles;
    } else ptr = &vinfoBlockFile;

    unsigned int nNewSize;
    pos.nPos = (*ptr)[nFile].nUndoSize;
    nNewSize = (*ptr)[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (fPruneMode)
            fCheckForPruning = true;
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE *file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        }
        else
            return state.Error("out of disk space");
    }

    return true;
}

bool CheckBlockHeader(int32_t *futureblockp,int32_t height,CBlockIndex *pindex, const CBlockHeader& blockhdr, CValidationState& state, bool fCheckPOW)
{
    // Check timestamp
    if ( 0 )
    {
        uint256 hash; int32_t i;
        hash = blockhdr.GetHash();
        for (i=31; i>=0; i--)
            fprintf(stderr,"%02x",((uint8_t *)&hash)[i]);
        fprintf(stderr," <- CheckBlockHeader\n");
        if ( chainActive.Tip() != 0 )
        {
            hash = chainActive.Tip()->GetBlockHash();
            for (i=31; i>=0; i--)
                fprintf(stderr,"%02x",((uint8_t *)&hash)[i]);
            fprintf(stderr," <- chainTip\n");
        }
    }
    *futureblockp = 0;
    if ( ASSETCHAINS_ADAPTIVEPOW > 0 )
    {
        if (blockhdr.GetBlockTime() > GetTime() + 4)
        {
            //LogPrintf("CheckBlockHeader block from future %d error",blockhdr.GetBlockTime() - GetAdjustedTime());
            return false;
        }
    }
    else if (blockhdr.GetBlockTime() > GetTime() + 60)
    {
        /*CBlockIndex *tipindex;
        //fprintf(stderr,"ht.%d future block %u vs time.%u + 60\n",height,(uint32_t)blockhdr.GetBlockTime(),(uint32_t)GetAdjustedTime());
        if ( (tipindex= chainActive.Tip()) != 0 && tipindex->GetBlockHash() == blockhdr.hashPrevBlock && blockhdr.GetBlockTime() < GetAdjustedTime() + 60 + 5 )
        {
            //fprintf(stderr,"it is the next block, let's wait for %d seconds\n",GetAdjustedTime() + 60 - blockhdr.GetBlockTime());
            while ( blockhdr.GetBlockTime() > GetAdjustedTime() + 60 )
                sleep(1);
            //fprintf(stderr,"now its valid\n");
        }
        else*/
        {
            if (blockhdr.GetBlockTime() < GetTime() + 300)
                *futureblockp = 1;
            //LogPrintf("CheckBlockHeader block from future %d error",blockhdr.GetBlockTime() - GetAdjustedTime());
            return false; //state.Invalid(error("CheckBlockHeader(): block timestamp too far in the future"),REJECT_INVALID, "time-too-new");
        }
    }
    // Check block version
    if (height > 0 && blockhdr.nVersion < MIN_BLOCK_VERSION)
        return state.DoS(100, error("CheckBlockHeader(): block version too low"),REJECT_INVALID, "version-too-low");

    // Check Equihash solution is valid
    if ( fCheckPOW )
    {
        if ( !CheckEquihashSolution(&blockhdr, Params()) )
            return state.DoS(100, error("CheckBlockHeader(): Equihash solution invalid"),REJECT_INVALID, "invalid-solution");
    }
    // Check proof of work matches claimed amount
    /*komodo_index2pubkey33(pubkey33,pindex,height);
     if ( fCheckPOW && !CheckProofOfWork(height,pubkey33,blockhdr.GetHash(), blockhdr.nBits, Params().GetConsensus(),blockhdr.nTime) )
     return state.DoS(50, error("CheckBlockHeader(): proof of work failed"),REJECT_INVALID, "high-hash");*/
    return true;
}

int32_t komodo_checkPOW(int64_t stakeTxValue,int32_t slowflag,CBlock *pblock,int32_t height);

/****
 * @brief various checks of block validity
 * @param[out] futureblockp pointer to the future block
 * @param[in] height the new height
 * @param[out] pindex the block index
 * @param[in] block the block to check
 * @param[out] state stores results
 * @param[in] verifier verification routine
 * @param[in] fCheckPOW pass true to check PoW
 * @param[in] fCheckMerkleRoot pass true to check merkle root
 * @returns true on success, on error, state will contain info
 */
bool CheckBlock(int32_t *futureblockp, int32_t height, CBlockIndex *pindex, const CBlock& block,
        CValidationState& state, ProofVerifier& verifier, bool fCheckPOW,
        bool fCheckMerkleRoot)
{
    uint8_t pubkey33[33];
    uint32_t tiptime = (uint32_t)block.nTime;
    // These are checks that are independent of context.
    uint256 hash = block.GetHash();
    // Check that the header is valid (particularly PoW).  This is mostly redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(futureblockp,height,pindex,block,state,fCheckPOW))
    {
        if ( *futureblockp == 0 )
        {
            LogPrintf("CheckBlock header error");
            return false;
        }
    }
    if ( pindex != nullptr && pindex->pprev != nullptr )
        tiptime = (uint32_t)pindex->pprev->nTime;
    if ( fCheckPOW )
    {
        komodo_block2pubkey33(pubkey33,(CBlock *)&block);
        if ( !CheckProofOfWork(block,pubkey33,height,Params().GetConsensus()) )
        {
            for (int32_t z = 31; z >= 0; z--)
                fprintf(stderr,"%02x",((uint8_t *)&hash)[z]);
            fprintf(stderr," failed hash ht.%d\n",height);
            return state.DoS(50, error("CheckBlock: proof of work failed"),REJECT_INVALID, "high-hash");
        }
        if ( ASSETCHAINS_STAKED == 0 && komodo_checkPOW(0,1,(CBlock *)&block,height) < 0 ) // checks Equihash
            return state.DoS(100, error("CheckBlock: failed slow_checkPOW"),REJECT_INVALID,
                    "failed-slow_checkPOW");
    }
    if ( height > nDecemberHardforkHeight && chainName.isKMD() ) // December 2019 hardfork
    {
        int32_t notaryid;
        int32_t special = komodo_chosennotary(&notaryid,height,pubkey33,tiptime);
        if (notaryid > 0 || ( notaryid == 0 && height > nS5HardforkHeight ) ) {
            CScript merkleroot = CScript();
            CBlock blockcopy = block; // block shouldn't be changed below, so let's make it's copy
            CBlock *pblockcopy = (CBlock *)&blockcopy;
            if (!komodo_checkopret(pblockcopy, merkleroot)) {
                fprintf(stderr, "failed or missing merkleroot expected.%s != merkleroot.%s\n",
                        komodo_makeopret(pblockcopy, false).ToString().c_str(), merkleroot.ToString().c_str());
                return state.DoS(100, error("CheckBlock: failed or missing merkleroot opret in easy-mined"),
                        REJECT_INVALID, "failed-merkle-opret-in-easy-mined");
            }
        }
    }

    // Check the merkle root.
    if (fCheckMerkleRoot)
    {
        bool mutated;
        uint256 hashMerkleRoot2 = block.BuildMerkleTree(&mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, error("CheckBlock: hashMerkleRoot mismatch"),
                             REJECT_INVALID, "bad-txnmrklroot", true);

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, error("CheckBlock: duplicate transaction"),
                             REJECT_INVALID, "bad-txns-duplicate", true);
    }

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Size limits
    if (block.vtx.empty() || block.vtx.size() > MAX_BLOCK_SIZE(height)
            || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE(height))
        return state.DoS(100, error("CheckBlock: size limits failed"),
                         REJECT_INVALID, "bad-blk-length");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, error("CheckBlock: first tx is not coinbase"),
                         REJECT_INVALID, "bad-cb-missing");

    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, error("CheckBlock: more than one coinbase"),
                             REJECT_INVALID, "bad-cb-multiple");

    // Check transactions
    if ( ASSETCHAINS_CC != 0 && !fCheckPOW )
        return true;

    // CC contracts might refer to transactions in the current block, from a
    // CC spend within the same block and out of order
    if ( ASSETCHAINS_CC != 0 )
    {
        int32_t i,j,rejects=0,lastrejects=0;
        // Copy all non Z-txs in mempool to temporary mempool because there can
        // be tx in local mempool that make the block invalid.
        LOCK2(cs_main,mempool.cs);
        list<CTransaction> transactionsToRemove;
        for(const CTxMemPoolEntry& e : mempool.mapTx)
        {
            const CTransaction &tx = e.GetTx();
            if ( tx.vjoinsplit.empty() && tx.GetSaplingSpendsCount() == 0)
            {
                transactionsToRemove.push_back(tx);
                tmpmempool.addUnchecked(tx.GetHash(),e,true);
            }
        }
        for(const CTransaction& tx : transactionsToRemove) {
            list<CTransaction> removed;
            mempool.remove(tx, removed, false);
        }
        // add all the txs in the block to the (somewhat) empty mempool.
        // CC validation shouldn't (can't) depend on the state of mempool!
        while ( true )
        {
            list<CTransaction> removed;
            for (i=0; i<block.vtx.size(); i++)
            {
                CValidationState state; CTransaction Tx;
                const CTransaction &tx = (CTransaction)block.vtx[i];
                if ( tx.IsCoinBase() || !tx.vjoinsplit.empty() || tx.GetSaplingSpendsCount() > 0
                        || (i == block.vtx.size()-1 && komodo_isPoS((CBlock *)&block,height,0) != 0) )
                    continue;
                Tx = tx;
                if ( myAddtomempool(Tx, &state, true) == false )
                {
                    //LogPrintf("Rejected by mempool, reason: .%s.\n", state.GetRejectReason().c_str());
                    // take advantage of other checks, but if we were only rejected because it is a valid staking
                    // transaction, sync with wallets and don't mark as a reject
                    rejects++;
                }
                // here we remove any txs in the temp mempool that were included in the block.
                tmpmempool.remove(tx, removed, false);
            }
            if ( rejects == 0 || rejects == lastrejects )
            {
                break;
            }
            lastrejects = rejects;
            rejects = 0;
        }
    }

    for (uint32_t i = 0; i < block.vtx.size(); i++)
    {
        const CTransaction& tx = block.vtx[i];
        if (!CheckTransaction(tiptime,tx, state, verifier, i, (int32_t)block.vtx.size()))
            return error("CheckBlock: CheckTransaction failed");
    }

    unsigned int nSigOps = 0;
    for(const CTransaction& tx : block.vtx)
    {
        nSigOps += GetLegacySigOpCount(tx);
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
        return state.DoS(100, error("CheckBlock: out-of-bounds SigOpCount"),
                         REJECT_INVALID, "bad-blk-sigops", true);
    if ( fCheckPOW && komodo_check_deposit(height,block) < 0 )
    {
        LogPrintf("CheckBlockHeader komodo_check_deposit error");
        return(false);
    }

    if ( ASSETCHAINS_CC != 0 )
    {
        LOCK2(cs_main,mempool.cs);
        // here we add back all txs from the temp mempool to the main mempool.
        for(const CTxMemPoolEntry& e : tmpmempool.mapTx)
        {
            const CTransaction &tx = e.GetTx();
            const uint256 &hash = tx.GetHash();
            mempool.addUnchecked(hash,e,true);
        }
        // empty the temp mempool for next time.
        tmpmempool.clear();
    }
    return true;
}

bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const CChainParams& chainParams = Params();
    const Consensus::Params& consensusParams = chainParams.GetConsensus();
    uint256 hash = block.GetHash();
    if (hash == consensusParams.hashGenesisBlock)
        return true;

    assert(pindexPrev);

    int nHeight = pindexPrev->nHeight+1;

    // Check proof of work
    if ( (!chainName.isKMD() || nHeight < 235300 || nHeight > 236000) && block.nBits != GetNextWorkRequired(pindexPrev, &block, consensusParams))
    {
        cout << block.nBits << " block.nBits vs. calc " << GetNextWorkRequired(pindexPrev, &block, consensusParams) <<
                               " for block #" << nHeight << endl;
        return state.DoS(100, error("%s: incorrect proof of work", __func__),
                        REJECT_INVALID, "bad-diffbits");
    }

    // Check timestamp against prev
    if ( ASSETCHAINS_ADAPTIVEPOW <= 0 || nHeight < 30 )
    {
        if (block.GetBlockTime() <= pindexPrev->GetMedianTimePast() )
        {
            fprintf(stderr,"ht.%d too early %u vs %u\n",(int32_t)nHeight,(uint32_t)block.GetBlockTime(),(uint32_t)pindexPrev->GetMedianTimePast());
            return state.Invalid(error("%s: block's timestamp is too early", __func__),
                                 REJECT_INVALID, "time-too-old");
        }
    }
    else
    {
        if ( block.GetBlockTime() <= pindexPrev->nTime )
        {
            fprintf(stderr,"ht.%d too early2 %u vs %u\n",(int32_t)nHeight,(uint32_t)block.GetBlockTime(),(uint32_t)pindexPrev->nTime);
            return state.Invalid(error("%s: block's timestamp is too early2", __func__),
                                 REJECT_INVALID, "time-too-old");
        }
    }

    // Check that timestamp is not too far in the future
    if (block.GetBlockTime() > GetTime() + consensusParams.nMaxFutureBlockTime)
    {
        return state.Invalid(error("%s: block timestamp too far in the future", __func__),
                        REJECT_INVALID, "time-too-new");
    }

    if (fCheckpointsEnabled)
    {
        // Check that the block chain matches the known block chain up to a checkpoint
        if (!Checkpoints::CheckBlock(chainParams.Checkpoints(), nHeight, hash))
        {
            return state.DoS(100, error("%s: rejected by checkpoint lock-in at %d", __func__, nHeight),REJECT_CHECKPOINT, "checkpoint mismatch");
        }

        // Don't accept any forks from the main chain prior to last checkpoint
        CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint(chainParams.Checkpoints());
        int32_t notarized_height;
        if ( nHeight == 1 && chainActive.Tip() != 0 && chainActive.Tip()->nHeight > 1 )
        {
            CBlockIndex *heightblock = chainActive[nHeight];
            if ( heightblock != 0 && heightblock->GetBlockHash() == hash )
                return true;
            return state.DoS(1, error("%s: trying to change height 1 forbidden", __func__));
        }
        if ( nHeight != 0 )
        {
            if ( pcheckpoint != 0 && nHeight < pcheckpoint->nHeight )
                return state.DoS(1, error("%s: forked chain older than last checkpoint (height %d) vs %d", __func__, nHeight,pcheckpoint->nHeight));
            if ( !komodo_checkpoint(&notarized_height,nHeight,hash) )
            {
                CBlockIndex *heightblock = chainActive[nHeight];
                if ( heightblock != 0 && heightblock->GetBlockHash() == hash )
                    return true;
                else
                    return state.DoS(1, error("%s: forked chain %d older than last notarized (height %d) vs %d", __func__,
                            nHeight, notarized_height));
            }
        }
    }
    // Reject block.nVersion < 4 blocks
    if (block.nVersion < 4)
        return state.Invalid(error("%s : rejected nVersion<4 block", __func__),
                             REJECT_OBSOLETE, "bad-version");

    return true;
}

bool ContextualCheckBlock(int32_t slowflag,const CBlock& block, CValidationState& state, CBlockIndex * const pindexPrev)
{
    const int nHeight = pindexPrev == NULL ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensusParams = Params().GetConsensus();
    bool sapling = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_SAPLING);

    uint32_t cmptime = block.nTime;
    const int32_t txheight = nHeight == 0 ? komodo_block2height((CBlock *)&block) : nHeight;

    /* HF22 - check interest validation against pindexPrev->GetMedianTimePast() + 777 */
    if (chainName.isKMD() &&
        consensusParams.nHF22Height != std::nullopt && txheight > consensusParams.nHF22Height.value()
    ) {
        if (pindexPrev) {
            uint32_t cmptime_old = cmptime;
            cmptime = pindexPrev->GetMedianTimePast() + 777;
            LogPrint("hfnet","%s[%d]: cmptime.%lu -> %lu\n", __func__, __LINE__, cmptime_old, cmptime);
            LogPrint("hfnet","%s[%d]: ht.%ld, hash.%s\n", __func__, __LINE__, txheight, block.GetHash().ToString());
        } else
            LogPrint("hfnet","%s[%d]: STRANGE! pindexPrev == nullptr, ht.%ld, hash.%s!\n", __func__, __LINE__, txheight, block.GetHash().ToString());
    }


    // Check that all transactions are finalized, also validate interest in each tx
    std::vector<const CTransaction*> vptx;

    for (uint32_t i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];
        vptx.emplace_back(&block.vtx[i]);

        // Interest validation
        if (!komodo_validate_interest(tx, txheight, cmptime))
        {
            fprintf(stderr, "validate interest failed for txnum.%i tx.%s\n", i, tx.ToString().c_str());
            return state.DoS(0, error("%s: komodo_validate_interest failed", __func__), REJECT_INVALID, "komodo-interest-invalid");
        }

        int nLockTimeFlags = 0;
        int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST)
        ? pindexPrev->GetMedianTimePast()
        : block.GetBlockTime();
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff)) {
            return state.DoS(10, error("%s: contains a non-final transaction", __func__), REJECT_INVALID, "bad-txns-nonfinal");
        }
    }

    // Check transaction contextually against consensus rules at block height
    CCoinsViewCache view(pcoinsTip);
    if (!ContextualCheckTransactionMultithreaded(slowflag,vptx,view,pindexPrev, state, nHeight, 100)) {
        return false; // Failure reason has been set in validation state object
    }

    // Enforce BIP 34 rule that the coinbase starts with serialized block height.
    // In Zcash this has been enforced since launch, except that the genesis
    // block didn't include the height in the coinbase (see Zcash protocol spec
    // section '6.8 Bitcoin Improvement Proposals').
    if (nHeight > 0)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
            !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
            return state.DoS(100, error("%s: block height mismatch in coinbase", __func__), REJECT_INVALID, "bad-cb-height");
        }
    }
    return true;
}

bool AcceptBlockHeader(int32_t *futureblockp,const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex)
{
    static uint256 zero;
    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex *pindex = NULL;
    if (miSelf != mapBlockIndex.end())
    {
        // Block header is already known.
        if ( (pindex = miSelf->second) == 0 )
            miSelf->second = pindex = AddToBlockIndex(block);
        if (ppindex)
            *ppindex = pindex;
        if ( pindex != 0 && (pindex->nStatus & BLOCK_FAILED_MASK) != 0 )
        {
            if ( ASSETCHAINS_CC == 0 )//&& (ASSETCHAINS_PRIVATE == 0 || KOMODO_INSYNC >= Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight) )
                return state.Invalid(error("%s: block is marked invalid", __func__), 0, "duplicate");
            else
            {
                fprintf(stderr,"reconsider block %s\n",hash.GetHex().c_str());
                pindex->nStatus &= ~BLOCK_FAILED_MASK;
            }
        }
        /*if ( pindex != 0 && hash == komodo_requestedhash )
        {
            fprintf(stderr,"AddToBlockIndex A komodo_requestedhash %s\n",komodo_requestedhash.ToString().c_str());
            memset(&komodo_requestedhash,0,sizeof(komodo_requestedhash));
            komodo_requestedcount = 0;
        }*/

        //if ( pindex == 0 )
        //    fprintf(stderr,"accepthdr %s already known but no pindex\n",hash.ToString().c_str());
        return true;
    }
    if (!CheckBlockHeader(futureblockp,*ppindex!=0?(*ppindex)->nHeight:0,*ppindex, block, state,0))
    {
        if ( *futureblockp == 0 )
        {
            LogPrintf("AcceptBlockHeader CheckBlockHeader error\n");
            return false;
        }
    }
    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (hash != chainparams.GetConsensus().hashGenesisBlock)
    {
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
        {
            LogPrintf("AcceptBlockHeader hashPrevBlock %s not found\n",block.hashPrevBlock.ToString().c_str());
            //*futureblockp = 1;
            return(false);
            //return state.DoS(10, error("%s: prev block not found", __func__), 0, "bad-prevblk");
        }
        pindexPrev = (*mi).second;
        if (pindexPrev == 0 )
        {
            LogPrintf("AcceptBlockHeader hashPrevBlock %s no pindexPrev\n",block.hashPrevBlock.ToString().c_str());
            return(false);
        }
        if ( (pindexPrev->nStatus & BLOCK_FAILED_MASK) )
            return state.DoS(100, error("%s: prev block invalid", __func__), REJECT_INVALID, "bad-prevblk");
    }
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
    {
        //fprintf(stderr,"AcceptBlockHeader ContextualCheckBlockHeader failed\n");
        LogPrintf("AcceptBlockHeader ContextualCheckBlockHeader failed\n");
        return false;
    }
    if (pindex == NULL)
    {
        if ( (pindex= AddToBlockIndex(block)) != 0 )
        {
            miSelf = mapBlockIndex.find(hash);
            if (miSelf != mapBlockIndex.end())
                miSelf->second = pindex;
            //fprintf(stderr,"AcceptBlockHeader couldnt add to block index\n");
        }
    }
    if (ppindex)
        *ppindex = pindex;
    /*if ( pindex != 0 && hash == komodo_requestedhash )
    {
        fprintf(stderr,"AddToBlockIndex komodo_requestedhash %s\n",komodo_requestedhash.ToString().c_str());
        memset(&komodo_requestedhash,0,sizeof(komodo_requestedhash));
        komodo_requestedcount = 0;
    }*/
    return true;
}

uint256 Queued_reconsiderblock;

/*****
 * @brief
 * @param futureblockp
 * @param block
 * @param state
 * @param ppindex
 * @param fRequested
 * @param dbp
 * @returns true if block accepted
 */
bool AcceptBlock(int32_t *futureblockp,CBlock& block, CValidationState& state, CBlockIndex** ppindex,
        bool fRequested, CDiskBlockPos* dbp)
{
    const CChainParams& chainparams = Params();
    AssertLockHeld(cs_main);

    CBlockIndex *&pindex = *ppindex;
    if (!AcceptBlockHeader(futureblockp, block, state, &pindex))
    {
        if ( *futureblockp == 0 )
        {
            LogPrintf("AcceptBlock AcceptBlockHeader error\n");
            return false;
        }
    }
    if ( pindex == 0 )
    {
        LogPrintf("AcceptBlock null pindex\n");
        *futureblockp = true;
        return false;
    }
    // Try to process all requested blocks that we don't have, but only
    // process an unrequested block if it's new and has enough work to
    // advance our tip, and isn't too many blocks ahead.
    bool fAlreadyHave = pindex->nStatus & BLOCK_HAVE_DATA;
    bool fHasMoreWork = (chainActive.Tip() ? pindex->nChainWork > chainActive.Tip()->nChainWork : true);
    // Blocks that are too out-of-order needlessly limit the effectiveness of
    // pruning, because pruning will not delete block files that contain any
    // blocks which are too close in height to the tip.  Apply this test
    // regardless of whether pruning is enabled; it should generally be safe to
    // not process unrequested blocks.
    bool fTooFarAhead = (pindex->nHeight > int(chainActive.Height() + BLOCK_DOWNLOAD_WINDOW)); //MIN_BLOCKS_TO_KEEP));

    // TODO: deal better with return value and error conditions for duplicate
    // and unrequested blocks.
    //fprintf(stderr,"Accept %s flags already.%d requested.%d morework.%d farahead.%d\n",pindex->GetBlockHash().ToString().c_str(),fAlreadyHave,fRequested,fHasMoreWork,fTooFarAhead);
    if (fAlreadyHave) return true;
    if (!fRequested) {  // If we didn't ask for it:
        if (pindex->nTx != 0) return true;  // This is a previously-processed block that was pruned
        if (!fHasMoreWork) return true;     // Don't process less-work chains
        if (fTooFarAhead) return true;      // Block height is too high
    }

    // See method docstring for why this is always disabled
    auto verifier = ProofVerifier::Disabled();
    bool fContextualCheckBlock = ContextualCheckBlock(0,block, state, pindex->pprev);
    if ( (!CheckBlock(futureblockp,pindex->nHeight,pindex,block, state, verifier,0)) || !fContextualCheckBlock )
    {
        static int32_t saplinght = -1;
        CBlockIndex *tmpptr;
        if ( saplinght == -1 )
            saplinght = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
        if ( saplinght < 0 )
            *futureblockp = 1;
        // the problem is when a future sapling block comes in before we detected saplinght
        if ( saplinght > 0 && (tmpptr= chainActive.Tip()) != 0 )
        {
            fprintf(stderr,"saplinght.%d tipht.%d blockht.%d cmp.%d\n",saplinght,(int32_t)tmpptr->nHeight,pindex->nHeight,pindex->nHeight < 0 || (pindex->nHeight >= saplinght && pindex->nHeight < saplinght+50000) || (tmpptr->nHeight > saplinght-720 && tmpptr->nHeight < saplinght+720));
            if ( pindex->nHeight < 0 || (pindex->nHeight >= saplinght && pindex->nHeight < saplinght+50000) || (tmpptr->nHeight > saplinght-720 && tmpptr->nHeight < saplinght+720) )
                *futureblockp = 1;
        }
        if ( *futureblockp == 0 )
        {
            if (state.IsInvalid() && !state.CorruptionPossible()) {
                pindex->nStatus |= BLOCK_FAILED_VALID;
                setDirtyBlockIndex.insert(pindex);
            }
            LogPrintf("AcceptBlock CheckBlock or ContextualCheckBlock error\n");
            return false;
        }
    }
    if ( fContextualCheckBlock )
        pindex->nStatus |= BLOCK_VALID_CONTEXT;

    int nHeight = pindex->nHeight;
    // Temp File fix. LABS has been using this for ages with no bad effects.
    // Disabled here. Set use tmp to whatever you need to use this for.
    int32_t usetmp = 0;
    if ( IsInitialBlockDownload() )
        usetmp = 0;

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(usetmp,state, blockPos, nBlockSize+8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock(): FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock(): ReceivedBlockTransactions failed");
        if ( usetmp != 0 ) // not during initialdownload or if futureflag==0 and contextchecks ok
            pindex->nStatus |= BLOCK_IN_TMPFILE;
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    if (fCheckForPruning)
        FlushStateToDisk(state, FLUSH_STATE_NONE); // we just allocated more disk space for block files
    if ( *futureblockp == 0 )
        return true;
    LogPrintf("AcceptBlock block from future error\n");
    return false;
}

static bool IsSuperMajority(int minVersion, const CBlockIndex* pstart, unsigned nRequired, const Consensus::Params& consensusParams)
{
    unsigned int nFound = 0;
    for (int i = 0; i < consensusParams.nMajorityWindow && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

CBlockIndex *komodo_ensure(CBlock *pblock, uint256 hash)
{
    CBlockIndex *pindex = 0;
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    if ( miSelf != mapBlockIndex.end() )
    {
        if ( (pindex = miSelf->second) == 0 ) // create pindex so first Accept block doesnt fail
        {
            miSelf->second = AddToBlockIndex(*pblock);
            //fprintf(stderr,"Block header %s is already known, but without pindex -> ensured %p\n",hash.ToString().c_str(),miSelf->second);
        }
        /*if ( hash != Params().GetConsensus().hashGenesisBlock )
        {
            miSelf = mapBlockIndex.find(pblock->hashPrevBlock);
            if ( miSelf != mapBlockIndex.end() )
            {
                if ( miSelf->second == 0 )
                {
                    miSelf->second = InsertBlockIndex(pblock->hashPrevBlock);
                    fprintf(stderr,"autocreate previndex %s\n",pblock->hashPrevBlock.ToString().c_str());
                }
            }
        }*/
    }
    return(pindex);
}

CBlockIndex *oldkomodo_ensure(CBlock *pblock, uint256 hash)
{
    CBlockIndex *pindex=0,*previndex=0;
    if ( (pindex = komodo_getblockindex(hash)) == 0 )
    {
        pindex = new CBlockIndex();
        if (!pindex)
            throw runtime_error("komodo_ensure: new CBlockIndex failed");
        BlockMap::iterator mi = mapBlockIndex.insert(make_pair(hash, pindex)).first;
        pindex->phashBlock = &((*mi).first);
    }
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    if ( miSelf == mapBlockIndex.end() )
    {
        LogPrintf("komodo_ensure unexpected missing hash %s\n",hash.ToString().c_str());
        return(0);
    }
    if ( miSelf->second == 0 ) // create pindex so first Accept block doesnt fail
    {
        if ( pindex == 0 )
        {
            pindex = AddToBlockIndex(*pblock);
            fprintf(stderr,"ensure call addtoblockindex, got %p\n",pindex);
        }
        if ( pindex != 0 )
        {
            miSelf->second = pindex;
            LogPrintf("Block header %s is already known, but without pindex -> ensured %p\n",hash.ToString().c_str(),miSelf->second);
        } else LogPrintf("komodo_ensure unexpected null pindex\n");
    }
    /*if ( hash != Params().GetConsensus().hashGenesisBlock )
        {
            miSelf = mapBlockIndex.find(pblock->hashPrevBlock);
            if ( miSelf == mapBlockIndex.end() )
                previndex = InsertBlockIndex(pblock->hashPrevBlock);
            if ( (miSelf= mapBlockIndex.find(pblock->hashPrevBlock)) != mapBlockIndex.end() )
            {
                if ( miSelf->second == 0 ) // create pindex so first Accept block doesnt fail
                {
                    if ( previndex == 0 )
                        previndex = InsertBlockIndex(pblock->hashPrevBlock);
                    if ( previndex != 0 )
                    {
                        miSelf->second = previndex;
                        LogPrintf("autocreate previndex %s\n",pblock->hashPrevBlock.ToString().c_str());
                    } else LogPrintf("komodo_ensure unexpected null previndex\n");
                }
            } else LogPrintf("komodo_ensure unexpected null miprev\n");
        }
     }*/
    return(pindex);
}

/*****
 * @brief Process a new block
 * @note can come from the network or locally mined
 * @note This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 * @param from_miner no longer used
 * @param height the new height
 * @param[out] state the results
 * @param pfrom the node that produced the block (nullptr for local)
 * @param pblock the block to process
 * @param fForceProcessing Process this block even if unrequested; used for non-network block sources and whitelisted peers.
 * @param[out] dbp set to position on disk for block
 * @returns true on success
 */
bool ProcessNewBlock(bool from_miner, int32_t height, CValidationState &state, CNode* pfrom,
        CBlock* pblock, bool fForceProcessing, CDiskBlockPos *dbp)
{
    // Preliminary checks
    bool checked;
    int32_t futureblock=0;
    auto verifier = ProofVerifier::Disabled();
    uint256 hash = pblock->GetHash();
    {
        LOCK(cs_main);
        if ( chainActive.Tip() != 0 )
            komodo_currentheight_set(chainActive.Tip()->nHeight);
        checked = CheckBlock(&futureblock,height!=0?height:komodo_block2height(pblock),0,*pblock, state, verifier,0);
        bool fRequested = MarkBlockAsReceived(hash);
        fRequested |= fForceProcessing;
        if ( checked && komodo_checkPOW(0,0,pblock,height) < 0 )
        {
            checked = false;
        }
        if (!checked && futureblock == 0)
        {
            if ( pfrom != nullptr )
            {
                Misbehaving(pfrom->GetId(), 1);
            }
            return error("%s: CheckBlock FAILED", __func__);
        }
        // Store to disk
        CBlockIndex *pindex = NULL;

        bool accepted = AcceptBlock(&futureblock,*pblock, state, &pindex, fRequested, dbp);
        if (pindex && pfrom) {
            mapBlockSource[pindex->GetBlockHash()] = pfrom->GetId();
        }
        CheckBlockIndex();
        if (!accepted && futureblock == 0)
        {
            komodo_longestchain();
            return error("%s: AcceptBlock FAILED", __func__);
        }
    }

    NotifyHeaderTip();

    if (futureblock == 0 && !ActivateBestChain(false, state, false, pblock))
        return error("%s: ActivateBestChain failed", __func__);

    return true;
}

bool TestBlockValidity(CValidationState &state, const CBlock& block, CBlockIndex * const pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev == chainActive.Tip());

    CCoinsViewCache viewNew(pcoinsTip);
    CBlockIndex indexDummy(block);
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;
    // JoinSplit proofs are verified in ConnectBlock
    auto verifier = ProofVerifier::Disabled();
    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
    {
        return false;
    }
    int32_t futureblock;
    if (!CheckBlock(&futureblock,indexDummy.nHeight,0,block, state, verifier, fCheckPOW, fCheckMerkleRoot))
    {
        return false;
    }
    if (!ContextualCheckBlock(0,block, state, pindexPrev))
    {
        return false;
    }
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true,fCheckPOW))
    {
        return false;
    }
    assert(state.IsValid());
    if ( futureblock != 0 )
        return(false);
    return true;
}

/**
 * BLOCK PRUNING CODE
 */

/* Calculate the amount of disk space the block & undo files currently use */
uint64_t CalculateCurrentUsage()
{
    uint64_t retval = 0;
    BOOST_FOREACH(const CBlockFileInfo &file, vinfoBlockFile) {
        retval += file.nSize + file.nUndoSize;
    }
    return retval;
}

/* Prune a block file (modify associated database entries)*/
bool PruneOneBlockFile(bool tempfile, const int fileNumber)
{
    uint256 notarized_hash,notarized_desttxid; int32_t prevMoMheight,notarized_height;
    notarized_height = komodo_notarized_height(&prevMoMheight,&notarized_hash,&notarized_desttxid);
    //fprintf(stderr, "pruneblockfile.%i\n",fileNumber); sleep(15);
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); ++it)
    {
        CBlockIndex* pindex = it->second;
        if (pindex && pindex->nFile == fileNumber)
        {
            if ( tempfile && ( (pindex->nStatus & BLOCK_IN_TMPFILE) != 0) )
            {
                if ( chainActive.Contains(pindex) )
                {
                    // Block is in main chain so we cant clear this file!
                    return(false);
                }
                fprintf(stderr, "pindex height.%i notarized height.%i \n", pindex->nHeight, notarized_height);
                if ( pindex->nHeight > notarized_height ) // Need to check this, does an invalid block have a height?
                {
                    // This blocks height is not older than last notarization so it can be reorged into the main chain.
                    // We cant clear this file!
                    return(false);
                }
                else
                {
                    // Block is not in main chain and is older than last notarised block so its safe for removal.
                    fprintf(stderr, "Block [%i] in tempfile.%i We can clear this block!\n",pindex->nHeight,fileNumber);
                    // Add index to list and remove after loop?
                }
            }
            pindex->nStatus &= ~BLOCK_HAVE_DATA;
            pindex->nStatus &= ~BLOCK_HAVE_UNDO;
            pindex->nFile = 0;
            pindex->nDataPos = 0;
            pindex->nUndoPos = 0;
            setDirtyBlockIndex.insert(pindex);
            // Prune from mapBlocksUnlinked -- any block we prune would have
            // to be downloaded again in order to consider its chain, at which
            // point it would be considered as a candidate for
            // mapBlocksUnlinked or setBlockIndexCandidates.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex->pprev);
            while (range.first != range.second)
            {
                std::multimap<CBlockIndex *, CBlockIndex *>::iterator it = range.first;
                range.first++;
                if (it->second == pindex)
                {
                    mapBlocksUnlinked.erase(it);
                }
            }
        }
    }
    if (!tempfile)
        vinfoBlockFile[fileNumber].SetNull();
    setDirtyFileInfo.insert(fileNumber);
    return(true);
}


void UnlinkPrunedFiles(std::set<int>& setFilesToPrune)
{
    for (set<int>::iterator it = setFilesToPrune.begin(); it != setFilesToPrune.end(); ++it) {
        CDiskBlockPos pos(*it, 0);
        boost::filesystem::remove(GetBlockPosFilename(pos, "blk"));
        boost::filesystem::remove(GetBlockPosFilename(pos, "rev"));
        LogPrintf("Prune: %s deleted blk/rev (%05u)\n", __func__, *it);
    }
}

/* Calculate the block/rev files that should be deleted to remain under target*/
void FindFilesToPrune(std::set<int>& setFilesToPrune)
{
    LOCK2(cs_main, cs_LastBlockFile);
    if (chainActive.Tip() == NULL || nPruneTarget == 0) {
        return;
    }
    if (chainActive.Tip()->nHeight <= Params().PruneAfterHeight()) {
        return;
    }
    unsigned int nLastBlockWeCanPrune = chainActive.Tip()->nHeight - MIN_BLOCKS_TO_KEEP;
    uint64_t nCurrentUsage = CalculateCurrentUsage();
    // We don't check to prune until after we've allocated new space for files
    // So we should leave a buffer under our target to account for another allocation
    // before the next pruning.
    uint64_t nBuffer = BLOCKFILE_CHUNK_SIZE + UNDOFILE_CHUNK_SIZE;
    uint64_t nBytesToPrune;
    int count=0;

    if (nCurrentUsage + nBuffer >= nPruneTarget) {
        for (int fileNumber = 0; fileNumber < nLastBlockFile; fileNumber++) {
            nBytesToPrune = vinfoBlockFile[fileNumber].nSize + vinfoBlockFile[fileNumber].nUndoSize;

            if (vinfoBlockFile[fileNumber].nSize == 0)
                continue;

            if (nCurrentUsage + nBuffer < nPruneTarget)  // are we below our target?
                break;

            // don't prune files that could have a block within MIN_BLOCKS_TO_KEEP of the main chain's tip but keep scanning
            if (vinfoBlockFile[fileNumber].nHeightLast > nLastBlockWeCanPrune)
                continue;

            PruneOneBlockFile(false, fileNumber);
            // Queue up the files for removal
            setFilesToPrune.insert(fileNumber);
            nCurrentUsage -= nBytesToPrune;
            count++;
        }
    }

    LogPrint("prune", "Prune: target=%dMiB actual=%dMiB diff=%dMiB max_prune_height=%d removed %d blk/rev pairs\n",
             nPruneTarget/1024/1024, nCurrentUsage/1024/1024,
             ((int64_t)nPruneTarget - (int64_t)nCurrentUsage)/1024/1024,
             nLastBlockWeCanPrune, count);
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}

/****
 * Open a file
 * @param pos where to position for the next read
 * @param prefix the type of file ("blk" or "rev")
 * @param fReadOnly open in read only mode
 * @returns the file pointer with the position set to pos.nPos, or NULL on error
 */
FILE* OpenDiskFile(const CDiskBlockPos &pos, const char *prefix, bool fReadOnly)
{
    static int32_t didinit[256]; // keeps track of which files have been initialized
    if (pos.IsNull())
        return NULL;
    boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
    boost::filesystem::create_directories(path.parent_path()); // create directory if necessary
    FILE* file = fopen(path.string().c_str(), "rb+"); // open existing file for reading and writing
    if (!file && !fReadOnly) // problem. Try opening read only if that is what was requested
        file = fopen(path.string().c_str(), "wb+"); // create an empty file for reading and writing
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    // the file was successfully opened
    if ( pos.nFile < sizeof(didinit)/sizeof(*didinit) // if pos.nFile doesn't go beyond our array
            && didinit[pos.nFile] == 0 // we have not initialized this file
            && strcmp(prefix,(char *)"blk") == 0 ) // we are attempting to read a block file
    {
        komodo_prefetch(file);
        didinit[pos.nFile] = 1;
    }

    if (pos.nPos) // it has been asked to move to a specific location within the file
    {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

/***
 * Open a block file
 * @param pos where to position for the next read
 * @param fReadOnly true to open the file in read only mode
 * @returns the file pointer or NULL on error
 */
FILE* OpenBlockFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "blk", fReadOnly);
}

/***
 * Open an undo ("rev") file
 * @param pos where to position for the next read
 * @param fReadOnly true to open the file in read only mode
 * @returns the file pointer or NULL on error
 */
FILE* OpenUndoFile(const CDiskBlockPos &pos, bool fReadOnly) {
    return OpenDiskFile(pos, "rev", fReadOnly);
}

/***
 * Get the full filename (including path) or a specific .dat file
 * @param pos the block position
 * @param prefix the prefix (i.e. "blk" or "rev")
 * @returns the filename with the complete path
 */
boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos &pos, const char *prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex * InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end() && mi->second != NULL)
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw runtime_error("LoadBlockIndex(): new CBlockIndex failed");
    mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    pindexNew->phashBlock = &((*mi).first);
    //fprintf(stderr,"inserted to block index %s\n",hash.ToString().c_str());

    return pindexNew;
}

/****
 * Load the block index database
 * @returns true on success
 */
bool static LoadBlockIndexDB()
{
    const CChainParams& chainparams = Params();
    LogPrintf("%s: start loading guts\n", __func__);
    {
        LOCK(cs_main);
        if (!pblocktree->LoadBlockIndexGuts())
            return false;
    }
    LogPrintf("%s: loaded guts\n", __func__);
    boost::this_thread::interruption_point();

    // Calculate nChainWork
    vector<pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const std::pair<uint256, CBlockIndex*>& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(make_pair(pindex->nHeight, pindex));
    }
    sort(vSortedByHeight.begin(), vSortedByHeight.end());

    uiInterface.ShowProgress(_("Loading block index DB..."), 0, false);
    int cur_height_num = 0;

    for (const std::pair<int, CBlockIndex*>& item : vSortedByHeight)
    {
        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        // We can link the chain of blocks for which we've received transactions at some point.
        // Pruned nodes may have deleted the block.
        if (pindex->nTx > 0) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;

                    if (pindex->pprev->nChainTotalSupply && pindex->nChainSupplyDelta) {
                        pindex->nChainTotalSupply = *pindex->pprev->nChainTotalSupply + *pindex->nChainSupplyDelta;
                    } else {
                        pindex->nChainTotalSupply = std::nullopt;
                    }

                    if (pindex->pprev->nChainTransparentValue && pindex->nTransparentValue) {
                        pindex->nChainTransparentValue = *pindex->pprev->nChainTransparentValue + *pindex->nTransparentValue;
                    } else {
                        pindex->nChainTransparentValue = std::nullopt;
                    }

                    if (pindex->pprev->nChainTotalBurned && pindex->nBurnedAmountDelta) {
                        pindex->nChainTotalBurned = *pindex->pprev->nChainTotalBurned + *pindex->nBurnedAmountDelta;
                    } else {
                        pindex->nChainTotalBurned = std::nullopt;
                    }

                    if (pindex->pprev->nChainSproutValue && pindex->nSproutValue) {
                        pindex->nChainSproutValue = *pindex->pprev->nChainSproutValue + *pindex->nSproutValue;
                    } else {
                        pindex->nChainSproutValue = std::nullopt;
                    }

                    if (pindex->pprev->nChainSaplingValue) {
                        pindex->nChainSaplingValue = *pindex->pprev->nChainSaplingValue + pindex->nSaplingValue;
                    } else {
                        pindex->nChainSaplingValue = std::nullopt;
                    }

                    if (pindex->pprev->nChainOrchardValue) {
                        pindex->nChainOrchardValue = *pindex->pprev->nChainOrchardValue + pindex->nOrchardValue;
                    } else {
                        pindex->nChainOrchardValue = std::nullopt;
                    }
                } else {
                    pindex->nChainTx = 0;
                    pindex->nChainTotalSupply = std::nullopt;
                    pindex->nChainTransparentValue = std::nullopt;
                    pindex->nChainTotalBurned = std::nullopt;
                    pindex->nChainSproutValue = std::nullopt;
                    pindex->nChainSaplingValue = std::nullopt;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
                pindex->nChainTotalSupply = pindex->nChainSupplyDelta;
                pindex->nChainTransparentValue = pindex->nTransparentValue;
                pindex->nChainTotalBurned = pindex->nBurnedAmountDelta;
                pindex->nChainSproutValue = pindex->nSproutValue;
                pindex->nChainSaplingValue = pindex->nSaplingValue;
                pindex->nChainOrchardValue = pindex->nOrchardValue;
            }

            // Fall back to hardcoded Sprout value pool balance
            // FallbackSproutValuePoolBalance(pindex, chainparams);

            // If developer option -developersetpoolsizezero has been enabled,
            // override and set the in-memory size of shielded pools to zero.  An unshielding transaction
            // can then be used to trigger and test the handling of turnstile violations.
            // if (fExperimentalDeveloperSetPoolSizeZero) {
            //     pindex->nChainSproutValue = 0;
            //     pindex->nChainSaplingValue = 0;
            //     pindex->nChainOrchardValue = 0;
            // }
        }
        // Construct in-memory chain of branch IDs.
        // Relies on invariant: a block that does not activate a network upgrade
        // will always be valid under the same consensus rules as its parent.
        // Genesis block has a branch ID of zero by definition, but has no
        // validity status because it is side-loaded into a fresh chain.
        // Activation blocks will have branch IDs set (read from disk).
        if (pindex->pprev) {
            if (pindex->IsValid(BLOCK_VALID_CONSENSUS) && !pindex->nCachedBranchId) {
                pindex->nCachedBranchId = pindex->pprev->nCachedBranchId;
            }
        } else {
            pindex->nCachedBranchId = SPROUT_BRANCH_ID;
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;

        uiInterface.ShowProgress(_("Loading block index DB..."), (int)((double)(cur_height_num*100)/(double)(vSortedByHeight.size())), false);
        cur_height_num++;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    tmpBlockFiles.resize(nLastTmpFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    set<int> setBlkDataFiles;
    for(const auto& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }

    int64_t count = 0; int reportDone = 0;
    uiInterface.ShowProgress(_("Checking all blk files are present..."), 0, false);
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++)
    {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
        count++;
        int percentageDone = (int)(count * 100.0 / setBlkDataFiles.size() + 0.5);
        if (reportDone < percentageDone/10) {
            // report max. every 10% step
            LogPrintf("[%d%%]...", percentageDone); /* Continued */
            uiInterface.ShowProgress(_("Checking all blk files are present..."), percentageDone, false);
            reportDone = percentageDone/10;
        }
    }
    LogPrintf("[%s].\n", "DONE");
    uiInterface.ShowProgress("", 100, false);

    // Check whether we have ever pruned block & undo files
    pblocktree->ReadFlag("prunedblockfiles", fHavePruned);
    if (fHavePruned)
        LogPrintf("LoadBlockIndexDB(): Block files have previously been pruned\n");

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    fReindex |= fReindexing;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("%s: transaction index %s\n", __func__, fTxIndex ? "enabled" : "disabled");
    // Check whether we have an address index
    pblocktree->ReadFlag("addressindex", fAddressIndex);
    LogPrintf("%s: address index %s\n", __func__, fAddressIndex ? "enabled" : "disabled");

    // Check whether we have a timestamp index
    pblocktree->ReadFlag("timestampindex", fTimestampIndex);
    LogPrintf("%s: timestamp index %s\n", __func__, fTimestampIndex ? "enabled" : "disabled");

    // Check whether we have a spent index
    pblocktree->ReadFlag("spentindex", fSpentIndex);
    LogPrintf("%s: spent index %s\n", __func__, fSpentIndex ? "enabled" : "disabled");

    // Fill in-memory data
    for(const auto& item : mapBlockIndex)
    {
        CBlockIndex* pindex = item.second;
        // - This relationship will always be true even if pprev has multiple
        //   children, because hashSproutAnchor is technically a property of pprev,
        //   not its children.
        // - This will miss chain tips; we handle the best tip below, and other
        //   tips will be handled by ConnectTip during a re-org.
        if (pindex->pprev) {
            pindex->pprev->hashFinalSproutRoot = pindex->hashSproutAnchor;
        }
    }

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;

    LOCK(cs_main);
    chainActive.SetTip(it->second);

    // Set hashFinalSproutRoot for the end of best chain
    it->second->hashFinalSproutRoot = pcoinsTip->GetBestAnchor(SPROUT);

    PruneBlockIndexCandidates();

    double progress;
    if ( chainName.isKMD() ) {
        progress = Checkpoints::GuessVerificationProgress(chainparams.Checkpoints(), chainActive.Tip());
    } else {
        int32_t longestchain = komodo_longestchain();
        // TODO: komodo_longestchain does not have the data it needs at the time LoadBlockIndexDB
        // runs, which makes it return 0, so we guess 50% for now
        progress = (longestchain > 0 ) ? (double) chainActive.Height() / longestchain : 0.5;
    }
    LogPrintf("%s: hashBestChain=%s height=%d date=%s progress=%f\n", __func__,
              chainActive.Tip()->GetBlockHash().ToString(), chainActive.Height(),
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", chainActive.Tip()->GetBlockTime()),
	      progress);

    EnforceNodeDeprecation(chainActive.Height(), true);
    CBlockIndex *pindex;
    if ( (pindex= chainActive.Tip()) != 0 )
    {
        if ( ASSETCHAINS_SAPLING <= 0 )
        {
            fprintf(stderr,"set sapling height, if possible from ht.%d %u\n",(int32_t)pindex->nHeight,(uint32_t)pindex->nTime);
            komodo_activate_sapling(pindex);
        }
        if ( ASSETCHAINS_ORCHARD <= 0 )
        {
            fprintf(stderr,"set orchard height, if possible from ht.%d %u\n",(int32_t)pindex->nHeight,(uint32_t)pindex->nTime);
            komodo_activate_orchard(pindex);
        }
    }
    return true;
}

CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0, false);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100, false);
}

bool CVerifyDB::VerifyDB(CCoinsView *coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainActive.Height())
        nCheckDepth = chainActive.Height();
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    // No need to verify JoinSplits twice
    auto verifier = ProofVerifier::Disabled();
    //fprintf(stderr,"start VerifyDB %u\n",(uint32_t)time(NULL));
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev)
    {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))), false);
        if (pindex->nHeight < chainActive.Height()-nCheckDepth)
            break;

        //Pre-check 0: Read hashFinalSaplingRoot and verify it against the database
        uint256 dbRoot = coinsview->GetBestAnchor(SAPLINGFRONTIER);
        if (dbRoot !=  chainActive.Tip()->hashFinalSaplingRoot) {
            LogPrintf("Chain Tip sapling root %s\n", chainActive.Tip()->hashFinalSaplingRoot.ToString());
            LogPrintf("dbroot %s\n", dbRoot.ToString());
            if (!(dbRoot == SaplingMerkleTree::empty_root() && chainActive.Tip()->hashFinalSaplingRoot == uint256())) {
                return error("VerifyDB(): ***Obsolete block database detected, reindexing the blockchain data required.\n");
            }
        }


        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex,0))
            return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        int32_t futureblock;
        if (nCheckLevel >= 1 && !CheckBlock(&futureblock,pindex->nHeight,pindex,block, state, verifier,0) )
            return error("VerifyDB(): *** found bad block at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("VerifyDB(): *** found bad undo data at %d, hash=%s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            bool fClean = true;
            if (!DisconnectBlock(block, state, pindex, coins, &fClean))
                return error("VerifyDB(): *** irrecoverable inconsistency in block data at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            pindexState = pindex->pprev;
            if (!fClean) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else
                nGoodTransactions += block.vtx.size();
        }
        if (ShutdownRequested())
            return true;
    }
    //fprintf(stderr,"end VerifyDB %u\n",(uint32_t)time(NULL));
    if (pindexFailure)
        return error("VerifyDB(): *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", chainActive.Height() - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex *pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainActive.Height() - pindex->nHeight)) / (double)nCheckDepth * 50))), false);
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex,0))
                return error("VerifyDB(): *** ReadBlockFromDisk failed at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins,false, true))
                return error("VerifyDB(): *** found unconnectable block at %d, hash=%s", pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainActive.Height() - pindexState->nHeight, nGoodTransactions);

    return true;
}

/**
 * When there are blocks in the active chain with missing data (e.g. if the
 * activation height and branch ID of a particular upgrade have been altered),
 * rewind the chainstate and remove them from the block index.
 * @param params the chain parameters
 * @returns true on success
 */
bool RewindBlockIndex(const CChainParams& params)
{
    LOCK(cs_main);

    // RewindBlockIndex is called after LoadBlockIndex, so at this point every block
    // index will have nCachedBranchId set based on the values previously persisted
    // to disk. By definition, a set nCachedBranchId means that the block was
    // fully-validated under the corresponding consensus rules. Thus we can quickly
    // identify whether the current active chain matches our expected sequence of
    // consensus rule changes, with two checks:
    //
    // - BLOCK_ACTIVATES_UPGRADE is set only on blocks that activate upgrades.
    // - nCachedBranchId for each block matches what we expect.
    auto sufficientlyValidated = [&params](const CBlockIndex* pindex) {
        auto consensus = params.GetConsensus();
        bool fFlagSet = pindex->nStatus & BLOCK_ACTIVATES_UPGRADE;
        bool fFlagExpected = IsActivationHeightForAnyUpgrade(pindex->nHeight, consensus);
        return fFlagSet == fFlagExpected &&
        pindex->nCachedBranchId &&
        *pindex->nCachedBranchId == CurrentEpochBranchId(pindex->nHeight, consensus);
    };

    int nHeight = 1;
    while (nHeight <= chainActive.Height()) {
        if (!sufficientlyValidated(chainActive[nHeight])) {
            break;
        }
        nHeight++;
    }

    // nHeight is now the height of the first insufficiently-validated block, or tipheight + 1
    auto rewindLength = chainActive.Height() - nHeight;
    if (rewindLength > 0 && rewindLength > MAX_REORG_LENGTH)
    {
        auto pindexOldTip = chainActive.Tip();
        auto pindexRewind = chainActive[nHeight - 1];
        auto msg = strprintf(_(
                               "A block chain rewind has been detected that would roll back %d blocks! "
                               "This is larger than the maximum of %d blocks, and so the node is shutting down for your safety."
                               ), rewindLength, MAX_REORG_LENGTH) + "\n\n" +
        _("Rewind details") + ":\n" +
        "- " + strprintf(_("Current tip:   %s, height %d"),
                         pindexOldTip->phashBlock->GetHex(), pindexOldTip->nHeight) + "\n" +
        "- " + strprintf(_("Rewinding to:  %s, height %d"),
                         pindexRewind->phashBlock->GetHex(), pindexRewind->nHeight) + "\n\n" +
        _("Please help, human!");
        LogPrintf("*** %s\n", msg);
        uiInterface.ThreadSafeMessageBox(msg, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return false;
    }

    CValidationState state;
    CBlockIndex* pindex = chainActive.Tip();
    while (chainActive.Height() >= nHeight) {
        if (fPruneMode && !(chainActive.Tip()->nStatus & BLOCK_HAVE_DATA)) {
            // If pruning, don't try rewinding past the HAVE_DATA point;
            // since older blocks can't be served anyway, there's
            // no need to walk further, and trying to DisconnectTip()
            // will fail (and require a needless reindex/redownload
            // of the blockchain).
            break;
        }
        if (!DisconnectTip(state, true)) {
            return error("RewindBlockIndex: unable to disconnect block at height %i", pindex->nHeight);
        }
        // Occasionally flush state to disk.
        if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC))
            return false;
    }

    // Reduce validity flag and have-data flags.

    // Collect blocks to be removed (blocks in mapBlockIndex must be at least BLOCK_VALID_TREE).
    // We do this after actual disconnecting, otherwise we'll end up writing the lack of data
    // to disk before writing the chainstate, resulting in a failure to continue if interrupted.
    std::vector<const CBlockIndex*> vBlocks;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        CBlockIndex* pindexIter = it->second;

        // Note: If we encounter an insufficiently validated block that
        // is on chainActive, it must be because we are a pruning node, and
        // this block or some successor doesn't HAVE_DATA, so we were unable to
        // rewind all the way.  Blocks remaining on chainActive at this point
        // must not have their validity reduced.
        if (pindexIter && !sufficientlyValidated(pindexIter) && !chainActive.Contains(pindexIter)) {
            // Reduce validity
            pindexIter->nStatus =
            std::min<unsigned int>(pindexIter->nStatus & BLOCK_VALID_MASK, BLOCK_VALID_TREE) |
            (pindexIter->nStatus & ~BLOCK_VALID_MASK);
            // Remove have-data flags
            pindexIter->nStatus &= ~(BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO);
            // Remove branch ID
            pindexIter->nStatus &= ~BLOCK_ACTIVATES_UPGRADE;
            pindexIter->nCachedBranchId = std::nullopt;
            // Remove storage location
            pindexIter->nFile = 0;
            pindexIter->nDataPos = 0;
            pindexIter->nUndoPos = 0;
            // Remove various other things
            pindexIter->nTx = 0;
            pindexIter->nChainTx = 0;
            pindexIter->nChainSupplyDelta = std::nullopt;
            pindexIter->nChainTotalSupply = std::nullopt;
            pindexIter->nTransparentValue = std::nullopt;
            pindexIter->nChainTransparentValue = std::nullopt;
            pindexIter->nSproutValue = std::nullopt;
            pindexIter->nChainSproutValue = std::nullopt;
            pindexIter->nSaplingValue = 0;
            pindexIter->nChainSaplingValue = std::nullopt;
            pindexIter->nSequenceId = 0;
            pindexIter->nOrchardValue = 0;
            pindexIter->nChainOrchardValue = std::nullopt;

            // Make sure it gets written
            /* corresponds to commented out block below as an alternative to setDirtyBlockIndex
            vBlocks.push_back(pindexIter);
            */
            setDirtyBlockIndex.insert(pindexIter);
            if (pindexIter == pindexBestInvalid)
            {
                //fprintf(stderr,"Reset invalid block marker if it was pointing to this block\n");
                pindexBestInvalid = NULL;
            }

            // Update indices
            setBlockIndexCandidates.erase(pindexIter);
            auto ret = mapBlocksUnlinked.equal_range(pindexIter->pprev);
            while (ret.first != ret.second) {
                if (ret.first->second == pindexIter) {
                    mapBlocksUnlinked.erase(ret.first++);
                } else {
                    ++ret.first;
                }
            }
        } else if (pindexIter->IsValid(BLOCK_VALID_TRANSACTIONS) && pindexIter->nChainTx) {
            setBlockIndexCandidates.insert(pindexIter);
        }
    }

    PruneBlockIndexCandidates();

    CheckBlockIndex();

    if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS)) {
        return false;
    }

    return true;
}

/***
 * Clear all values related to the block index
 */
void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(NULL);
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
    nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    tmpBlockFiles.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nQueuedValidatedHeaders = 0;
    nPreferredDownload = 0;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    mapNodeState.clear();
    recentRejects.reset(NULL);

    for(BlockMap::value_type& entry : mapBlockIndex)
    {
        delete entry.second;
    }
    mapBlockIndex.clear();
    fHavePruned = false;
}

/******
 * @brief Load the block tree and coins database from disk
 * @param reindex true if we will be reindexing (will skip the load if we are)
 * @returns true on success
 */
bool LoadBlockIndex(bool reindex)
{
    // Load block index from databases
    KOMODO_LOADINGBLOCKS = true;
    if (!reindex && !LoadBlockIndexDB())
    {
        KOMODO_LOADINGBLOCKS = false;
        return false;
    }
    fprintf(stderr,"finished loading blocks %s\n",chainName.symbol().c_str());
    return true;
}

/**
 * Initialize a new block tree database + block data on disk
 * @returns true on success
 */
bool InitBlockIndex()
{
    const CChainParams& chainparams = Params();
    LOCK(cs_main);
    tmpBlockFiles.clear();

    // Initialize global variables that cannot be constructed at startup.
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));
    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
    {
        return true;
    }
    if ( pblocktree != 0 )
    {
        pblocktree->WriteFlag("archiverule", fArchive);
        // Use the provided setting for -txindex in the new database
        // fTxIndex = GetBoolArg("-txindex", true);
        pblocktree->WriteFlag("txindex", fTxIndex);
        // Use the provided setting for -addressindex in the new database
        fAddressIndex = GetBoolArg("-addressindex", DEFAULT_ADDRESSINDEX);
        pblocktree->WriteFlag("addressindex", fAddressIndex);

        // Use the provided setting for -timestampindex in the new database
        fTimestampIndex = GetBoolArg("-timestampindex", DEFAULT_TIMESTAMPINDEX);
        pblocktree->WriteFlag("timestampindex", fTimestampIndex);

        fSpentIndex = GetBoolArg("-spentindex", DEFAULT_SPENTINDEX);
        pblocktree->WriteFlag("spentindex", fSpentIndex);
        fprintf(stderr,"fAddressIndex.%d/%d fSpentIndex.%d/%d\n",fAddressIndex,DEFAULT_ADDRESSINDEX,fSpentIndex,DEFAULT_SPENTINDEX);
        LogPrintf("Initializing databases...\n");
    }
    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            CBlock &block = const_cast<CBlock&>(Params().GenesisBlock());
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(0,state, blockPos, nBlockSize+8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex(): FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos, chainparams.MessageStart()))
                return error("LoadBlockIndex(): writing genesis block to disk failed");
            CBlockIndex *pindex = AddToBlockIndex(block);
            if ( pindex == 0 )
                return error("LoadBlockIndex(): couldnt add to block index");
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("LoadBlockIndex(): genesis block not accepted");
            if (!ActivateBestChain(true, state, false, &block))
                return error("LoadBlockIndex(): genesis block cannot be activated");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesn't check stale data
            if ( KOMODO_NSPV_FULLNODE )
                return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
            else return(true);
        } catch (const std::runtime_error& e) {
            return error("LoadBlockIndex(): failed to initialize block database: %s", e.what());
        }
    }

    return true;
}

bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos *dbp)
{
    const CChainParams& chainparams = Params();
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        //CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE, MAX_BLOCK_SIZE+8, SER_DISK, CLIENT_VERSION);
        CBufferedFile blkdat(fileIn, 2*MAX_BLOCK_SIZE(10000000), MAX_BLOCK_SIZE(10000000)+8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++; // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos()+1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE(10000000))
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                CBlock block;
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                blkdat >> block;

                nRewind = blkdat.GetPos();
                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != chainparams.GetConsensus().hashGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint("reindex", "%s: Out of order block %s, parent %s not known\n", __func__, hash.ToString(),
                             block.hashPrevBlock.ToString());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state;
                    if (ProcessNewBlock(0,0,state, NULL, &block, true, dbp))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != chainparams.GetConsensus().hashGenesisBlock && komodo_blockheight(hash) % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), komodo_blockheight(hash));
                }

                NotifyHeaderTip();

                // Recursively process earlier encountered successors of this block
                deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;

                        if (ReadBlockFromDisk(mapBlockIndex.count(hash)!=0?mapBlockIndex[hash]->nHeight:0,block, it->second,1))
                        {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                      head.ToString());
                            CValidationState dummy;
                            if (ProcessNewBlock(0,0,dummy, NULL, &block, true, &it->second))
                            {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                        NotifyHeaderTip();
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s: Deserialize or I/O error - %s\n", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void static CheckBlockIndex()
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*,CBlockIndex*> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        if ( it->second != 0 )
            forward.insert(std::make_pair(it->second->pprev, it->second));
    }
    if ( Params().NetworkIDString() != "regtest" )
        assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex *pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL; // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL; // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNeverProcessed = NULL; // Oldest ancestor of pindex for which nTx == 0.
    CBlockIndex* pindexFirstNotTreeValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotTransactionsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_TRANSACTIONS (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindexFirstNeverProcessed == NULL && pindex->nTx == 0) pindexFirstNeverProcessed = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTransactionsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TRANSACTIONS) pindexFirstNotTransactionsValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == consensusParams.hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis()); // The current active chain's genesis block must be this block.
        }
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0);  // nSequenceId can't be set for blocks that aren't linked
        // VALID_TRANSACTIONS is equivalent to nTx > 0 for all nodes (whether or not pruning has occurred).
        // HAVE_DATA is only equivalent to nTx > 0 (or VALID_TRANSACTIONS) if no pruning has occurred.
        if (!fHavePruned) {
            // If we've never pruned, then HAVE_DATA should be equivalent to nTx > 0
            assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
            assert(pindexFirstMissing == pindexFirstNeverProcessed);
        } else {
            // If we have pruned, then we can only say that HAVE_DATA implies nTx > 0
            if (pindex->nStatus & BLOCK_HAVE_DATA) assert(pindex->nTx > 0);
        }
        if (pindex->nStatus & BLOCK_HAVE_UNDO) assert(pindex->nStatus & BLOCK_HAVE_DATA);
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0)); // This is pruning-independent.
        // All parents having had data (at some point) is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstNeverProcessed != NULL) == (pindex->nChainTx == 0)); // nChainTx != 0 is used to signal that all parent blocks have been processed (but may have been pruned).
        assert((pindexFirstNotTransactionsValid != NULL) == (pindex->nChainTx == 0));
        assert(pindex->nHeight == nHeight); // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork); // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight))); // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL); // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL); // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL); // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstNeverProcessed == NULL) {
            if (pindexFirstInvalid == NULL) {
                // If this block sorts at least as good as the current tip and
                // is valid and we have all data for its parents, it must be in
                // setBlockIndexCandidates.  chainActive.Tip() must also be there
                // even if some data has been pruned.
                if (pindexFirstMissing == NULL || pindex == chainActive.Tip()) {
                    assert(setBlockIndexCandidates.count(pindex));
                }
                // If some parent is missing, then it could be that this block was in
                // setBlockIndexCandidates but had to be removed because of the missing data.
                // In this case it must be in mapBlocksUnlinked -- see test below.
            }
        } else { // If this block sorts worse than the current tip or some ancestor's block has never been seen, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed != NULL && pindexFirstInvalid == NULL) {
            // If this block has block data available, some parent was never received, and has no invalid parents, it must be in mapBlocksUnlinked.
            assert(foundInUnlinked);
        }
        if (!(pindex->nStatus & BLOCK_HAVE_DATA)) assert(!foundInUnlinked); // Can't be in mapBlocksUnlinked if we don't HAVE_DATA
        if (pindexFirstMissing == NULL) assert(!foundInUnlinked); // We aren't missing data for any parent -- cannot be in mapBlocksUnlinked.
        if (pindex->pprev && (pindex->nStatus & BLOCK_HAVE_DATA) && pindexFirstNeverProcessed == NULL && pindexFirstMissing != NULL) {
            // We HAVE_DATA for this block, have received data for all parents at some point, but we're currently missing data for some parent.
            assert(fHavePruned); // We must have pruned.
            // This block may have entered mapBlocksUnlinked if:
            //  - it has a descendant that at some point had more work than the
            //    tip, and
            //  - we tried switching to that descendant but were missing
            //    data for some intermediate block between chainActive and the
            //    tip.
            // So if this block is itself better than chainActive.Tip() and it wasn't in
            // setBlockIndexCandidates, then it must be in mapBlocksUnlinked.
            if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && setBlockIndexCandidates.count(pindex) == 0) {
                if (pindexFirstInvalid == NULL) {
                    assert(foundInUnlinked);
                }
            }
        }
        // try {
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // } catch (const runtime_error&) {
        //     assert(!"Failed to read index entry");
        // }
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNeverProcessed) pindexFirstNeverProcessed = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotTransactionsValid) pindexFirstNotTransactionsValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*,CBlockIndex*>::iterator,std::multimap<CBlockIndex*,CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}

//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

std::string GetWarnings(const std::string& strFor)
{
  int nPriority = 0;
  string strStatusBar;
  string strRPC;
  string strGUI;
  const std::string uiAlertSeperator = "<hr />";

  if (!CLIENT_VERSION_IS_RELEASE) {
      strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
      strGUI = _("This is a pre-release test build - use at your own risk - do not use for mining or merchant applications");
  }

  if (GetBoolArg("-testsafemode", false))
      strStatusBar = strRPC = "testsafemode enabled";

  // Misc warnings like out of disk space and clock is wrong
  if (strMiscWarning != "")
  {
      nPriority = 1000;
      strStatusBar = strMiscWarning;
      strGUI += (strGUI.empty() ? "" : uiAlertSeperator) + strMiscWarning;
  }

  if (fLargeWorkForkFound)
  {
      nPriority = 2000;
      strStatusBar = strRPC = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
      strGUI += (strGUI.empty() ? "" : uiAlertSeperator) + _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
  }
  else if (fLargeWorkInvalidChainFound)
  {
      nPriority = 2000;
      strStatusBar = strRPC = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
      strGUI += (strGUI.empty() ? "" : uiAlertSeperator) + _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
  }

  // Alerts
  {
      LOCK(cs_mapAlerts);
      BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
      {
          const CAlert& alert = item.second;
          if (alert.AppliesToMe() && alert.nPriority > nPriority)
          {
              nPriority = alert.nPriority;
              strStatusBar = alert.strStatusBar;
              if (alert.nPriority >= ALERT_PRIORITY_SAFE_MODE) {
                  strRPC = alert.strRPCError;
              }
          }
      }
  }

  if (strFor == "gui")
      return strGUI;
  else if (strFor == "statusbar")
      return strStatusBar;
  else if (strFor == "rpc")
      return strRPC;
  assert(!"GetWarnings(): invalid parameter");
  return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(const CInv& inv) REQUIRES(cs_main)
{
    switch (inv.type)
    {
        case MSG_TX:
        {
            assert(recentRejects);
            if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip)
            {
                // If the chain tip has changed previously rejected transactions
                // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
                // or a double-spend. Reset the rejects filter and give those
                // txs a second chance.
                hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
                recentRejects->reset();
            }

            return recentRejects->contains(inv.hash) ||
            mempool.exists(inv.hash) ||
            mapOrphanTransactions.count(inv.hash) ||
            pcoinsTip->HaveCoins(inv.hash);
        }
        case MSG_BLOCK:
            return mapBlockIndex.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

void static ProcessGetData(CNode* pfrom)
{
    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();

    vector<CInv> vNotFound;

    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        const CInv &inv = *it;
        {
            boost::this_thread::interruption_point();
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
            {
                bool send = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    if (chainActive.Contains(mi->second)) {
                        send = true;
                    } else {
                        static const int nOneMonth = 30 * 24 * 60 * 60;
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a month older (both in time, and in
                        // best equivalent proof of work) than the best header chain we know about.
                        send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                        (pindexBestHeader->GetBlockTime() - mi->second->GetBlockTime() < nOneMonth) &&
                        (GetBlockProofEquivalentTime(*pindexBestHeader, *mi->second, *pindexBestHeader, Params().GetConsensus()) < nOneMonth);
                        if (!send) {
                            LogPrintf("%s: ignoring request from peer=%i for old block that isn't in the main chain\n", __func__, pfrom->GetId());
                        }
                    }
                }
                // Pruned nodes may have deleted the block, so check whether
                // it's available before trying to send.
                if (send && (mi->second->nStatus & BLOCK_HAVE_DATA))
                {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, (*mi).second,1))
                    {
                        assert(!"cannot load block from disk");
                    }
                    else
                    {
                        if (inv.type == MSG_BLOCK)
                        {
                            //uint256 hash; int32_t z;
                            //hash = block.GetHash();
                            //for (z=31; z>=0; z--)
                            //    fprintf(stderr,"%02x",((uint8_t *)&hash)[z]);
                            //fprintf(stderr," send block %d\n",komodo_block2height(&block));
                            pfrom->PushMessage(NetMsgType::BLOCK, block);
                        }
                        else // MSG_FILTERED_BLOCK)
                        {
                            LOCK(pfrom->cs_filter);
                            if (pfrom->pfilter)
                            {
                                CMerkleBlock merkleBlock(block, *pfrom->pfilter);
                                pfrom->PushMessage(NetMsgType::MERKLEBLOCK, merkleBlock);
                                // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                                // This avoids hurting performance by pointlessly requiring a round-trip
                                // Note that there is currently no way for a node to request any single transactions we didn't send here -
                                // they must either disconnect and retry or request the full block.
                                // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                                // however we MUST always provide at least what the remote peer needs
                                typedef std::pair<unsigned int, uint256> PairType;
                                BOOST_FOREACH(PairType& pair, merkleBlock.vMatchedTxn)
                                if (!pfrom->setInventoryKnown.count(CInv(MSG_TX, pair.second)))
                                    pfrom->PushMessage(NetMsgType::TX, block.vtx[pair.first]);
                            }
                            // else
                            // no response
                        }
                    }
                    // Trigger the peer node to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        pfrom->PushMessage(NetMsgType::INV, vInv);
                        pfrom->hashContinue.SetNull();
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_TX) {
                    CTransaction tx;
                    if (mempool.lookup(inv.hash, tx)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage(NetMsgType::TX, ss);
                        pushed = true;
                    }
                }
                if (!pushed) {
                    vNotFound.push_back(inv);
                }
            }

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        pfrom->PushMessage(NetMsgType::NOTFOUND, vNotFound);
    }
}

#include "komodo_nSPV_defs.h"
#include "komodo_nSPV.h"            // shared defines, structs, serdes, purge functions
#include "komodo_nSPV_fullnode.h"   // nSPV fullnode handling of the getnSPV request messages
#include "komodo_nSPV_superlite.h"  // nSPV superlite client, issuing requests and handling nSPV responses
#include "komodo_nSPV_wallet.h"     // nSPV_send and support functions, really all the rest is to support this

void komodo_netevent(std::vector<uint8_t> payload);

bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{
    int32_t nProtocolVersion;
    const CChainParams& chainparams = Params();
    LogPrint("net", "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    //if ( KOMODO_NSPV_SUPERLITE )
    //if ( strCommand != "version" && strCommand != "verack" )
    //    fprintf(stderr, "recv: %s (%u bytes) peer=%d\n", SanitizeString(strCommand).c_str(), (int32_t)vRecv.size(), (int32_t)pfrom->GetId());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    //fprintf(stderr,"netmsg: %s\n", strCommand.c_str());

    if (strCommand == NetMsgType::VERSION)
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        int nVersion;           // use temporary for version, don't set version number until validated as connected
        int minVersion = MIN_PEER_PROTO_VERSION;
        if ( is_STAKED(chainName.symbol()) != 0 )
            minVersion = STAKEDMIN_PEER_PROTO_VERSION;
        vRecv >> nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (nVersion == 10300)
            nVersion = 300;
        if (nVersion < minVersion)
        {
            // disconnect from peers older than this proto version
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater", minVersion));
            pfrom->fDisconnect = true;
            return false;
        }

        // Reject incoming connections from nodes that don't know about the current epoch
        const Consensus::Params& params = Params().GetConsensus();
        auto currentEpoch = CurrentEpoch(GetHeight(), params);
        if (nVersion < params.vUpgrades[currentEpoch].nProtocolVersion)
        {
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, nVersion);
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater",
                            params.vUpgrades[currentEpoch].nProtocolVersion));
            pfrom->fDisconnect = true;
            return false;
        }

        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->nVersion = nVersion;

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // Potentially mark this peer as a preferred download peer.
        UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        //Ask for Address Format Version 2
        pfrom->PushMessage(NetMsgType::SENDADDRV2);

        // Change version
        pfrom->PushMessage(NetMsgType::VERACK);
        pfrom->ssSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    LogPrintf("ProcessMessages: advertizing address %s\n", addr.ToString());
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage(NetMsgType::GETADDR);
                pfrom->fGetAddr = true;
                // When requesting a getaddr, accept an additional MAX_ADDR_TO_SEND addresses in response
                // (bypassing the MAX_ADDR_PROCESSING_TOKEN_BUCKET limit).
                pfrom->m_addr_token_bucket += MAX_ADDR_TO_SEND;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
            item.second.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;

        string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
                  pfrom->cleanSubVer, pfrom->nVersion,
                  pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
                  remoteAddr);

        pfrom->nTimeOffset = timeWarning.AddTimeData(pfrom->addr, nTime, GetTime());
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }
    else if ( strCommand == NetMsgType::EVENTS )
    {
        if ( ASSETCHAINS_CCLIB != "gamescc" )
        {
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }
        std::vector<uint8_t> payload;
        vRecv >> payload;
        komodo_netevent(payload);
        return(true);
    }
    else if (strCommand == NetMsgType::VERACK)
    {
        pfrom->SetRecvVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if ( KOMODO_NSPV_SUPERLITE )
        {
            if ( (pfrom->nServices & NODE_NSPV) == 0 )
            {
                // fprintf(stderr,"invalid nServices.%llx nSPV peer.%d\n",(long long)pfrom->nServices,pfrom->id);
                pfrom->fDisconnect = true;
                return false;
            }
        }
        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
            AddressCurrentlyConnected(State(pfrom->GetId())->address);
        }
    }


    // Disconnect existing peer connection when:
    // 1. The version message has been received
    // 2. Peer version is below the minimum version for the current epoch
    else if (pfrom->nVersion < chainparams.GetConsensus().vUpgrades[
        CurrentEpoch(GetHeight(), chainparams.GetConsensus())].nProtocolVersion)
    {
        LogPrintf("peer=%d using obsolete version %i vs %d; disconnecting\n",
                  pfrom->id, pfrom->nVersion,(int32_t)chainparams.GetConsensus().vUpgrades[
                  CurrentEpoch(GetHeight(), chainparams.GetConsensus())].nProtocolVersion);
        pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_OBSOLETE,
                  strprintf("Version must be %d or greater",chainparams.GetConsensus().vUpgrades[
                  CurrentEpoch(GetHeight(), chainparams.GetConsensus())].nProtocolVersion));
        pfrom->fDisconnect = true;
        return false;
    }

    else if (strCommand == NetMsgType::SENDADDRV2) {
        pfrom->m_wants_addrv2 = true;
        return true;
    }

    else if (strCommand == NetMsgType::ADDR || strCommand == NetMsgType::ADDRV2)
    {
        int stream_version = vRecv.GetVersion();
        int tempStream_version = vRecv.GetVersion();
        tempStream_version |= ADDRV2_FORMAT;

        if (strCommand == NetMsgType::ADDRV2) {
            // Add ADDRV2_FORMAT to the version so that the CNetAddr and CAddress
            // unserialize methods know that an address in v2 format is coming.
            stream_version |= ADDRV2_FORMAT;
        }

        OverrideStream<CDataStream> s(&vRecv, vRecv.GetType(), stream_version);
        vector<CAddress> vAddr;
        s >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("%s message size() = %u", strCommand, vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetTime();
        int64_t nSince = nNow - 10 * 60;

        // Update/increment addr rate limiting bucket.
        const int64_t current_time = GetTimeMicros();
        if (pfrom->m_addr_token_bucket < MAX_ADDR_PROCESSING_TOKEN_BUCKET) {
            // Don't increment bucket if it's already full
            const auto time_diff = std::max(current_time - pfrom->m_addr_token_timestamp, (int64_t) 0);
            const double increment = (time_diff / 1000000) * MAX_ADDR_RATE_PER_SECOND;
            pfrom->m_addr_token_bucket = std::min<double>(pfrom->m_addr_token_bucket + increment, MAX_ADDR_PROCESSING_TOKEN_BUCKET);
        }
        pfrom->m_addr_token_timestamp = current_time;

        uint64_t num_proc = 0; /* nProcessedAddrs */
        uint64_t num_rate_limit = 0; /* nRatelimitedAddrs */

        std::mt19937 g(std::time(nullptr));
        std::shuffle(vAddr.begin(), vAddr.end(), g);

        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            boost::this_thread::interruption_point();

            // Apply rate limiting if the address is not whitelisted
            if (pfrom->m_addr_token_bucket < 1.0) {
                if (!pfrom->IsWhitelistedRange(addr)) {
                    ++num_rate_limit;
                    continue;
                }
            } else {
                pfrom->m_addr_token_bucket -= 1.0;
            }

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            ++num_proc;
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the addrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
            pfrom->m_addr_processed += num_proc;
            pfrom->m_addr_rate_limited += num_rate_limit;
        }

        LogPrint("net", "ProcessMessage: Received addr: %u addresses (%u processed, %u rate-limited) from peer=%d%s\n",
            vAddr.size(),
            num_proc,
            num_rate_limit,
            pfrom->GetId(),
            fLogIPs ? ", peeraddr=" + pfrom->addr.ToString() : ""
        );

        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);

        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }
    else if (strCommand == NetMsgType::PING)
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage(NetMsgType::PONG, nonce);
        }
    }


    else if (strCommand == NetMsgType::PONG)
    {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime, pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere; cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere; cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint("net", "pong peer=%d %s: %s, %x expected, %x received, %u bytes\n",
                     pfrom->id,
                     pfrom->cleanSubVer,
                     sProblem,
                     pfrom->nPingNonceSent,
                     nonce,
                     nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
            pfrom->nPingRetry = 0;
        }
    }

    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making nodes which are behind NAT and can only make outgoing connections ignore
    // the getaddr message mitigates the attack.
    else if ((strCommand == NetMsgType::GETADDR) && (pfrom->fInbound))
    {
        // Only send one GetAddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        if (pfrom->fSentAddr) {
            LogPrint("net", "Ignoring repeated \"getaddr\". peer=%d\n", pfrom->id);
            return true;
        }
        pfrom->fSentAddr = true;

        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr(pfrom->m_wants_addrv2);
        BOOST_FOREACH(const CAddress &addr, vAddr)
        pfrom->PushAddress(addr);
    }
    // temporary optional nspv message processing
    else if (GetBoolArg("-nspv_msg", DEFAULT_NSPV_PROCESSING) &&
            (strCommand == NetMsgType::GETNSPV || strCommand == NetMsgType::NSPV)) {

        std::vector<uint8_t> payload;
        vRecv >> payload;

        if (strCommand == NetMsgType::GETNSPV && KOMODO_NSPV == 0) {
            komodo_nSPVreq(pfrom, payload);
        } else if (strCommand == NetMsgType::NSPV && KOMODO_NSPV_SUPERLITE) {
            komodo_nSPVresp(pfrom, payload);
        }
        return (true);
    }
    else if ( KOMODO_NSPV_SUPERLITE )
        return(true);
    else if (strCommand == NetMsgType::INV)
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        LOCK(cs_main);

        std::vector<CInv> vToFetch;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            boost::this_thread::interruption_point();
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint("net", "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);

            if (!fAlreadyHave && !fImporting && !fReindex && inv.type != MSG_BLOCK)
                pfrom->AskFor(inv);

            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                    // First request the headers preceding the announced block. In the normal fully-synced
                    // case where a new block is announced that succeeds the current tip (no reorganization),
                    // there are no such headers.
                    // Secondly, and only when we are close to being synced, we request the announced block directly,
                    // to avoid an extra round-trip. Note that we must *first* ask for the headers, so by the
                    // time the block arrives, the header chain leading up to it is already validated. Not
                    // doing this will result in the received block being rejected as an orphan in case it is
                    // not a direct successor.
                    pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexBestHeader), inv.hash);
                    CNodeState *nodestate = State(pfrom->GetId());
                    if (chainActive.Tip()->GetBlockTime() > GetTime() - chainparams.GetConsensus().nPowTargetSpacing * 20 &&
                        nodestate->nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
                        vToFetch.push_back(inv);
                        // Mark block as in flight already, even though the actual "getdata" message only goes out
                        // later (within the same cs_main lock, though).
                        MarkBlockAsInFlight(pfrom->GetId(), inv.hash, chainparams.GetConsensus());
                    }
                    LogPrint("net", "getheaders (%d) %s to peer=%d\n", pindexBestHeader->nHeight, inv.hash.ToString(), pfrom->id);
                }
            }

            if (pfrom->nSendSize > (SendBufferSize() * 2)) {
                Misbehaving(pfrom->GetId(), 50);
                return error("send buffer size() = %u", pfrom->nSendSize);
            }
        }

        if (!vToFetch.empty())
            pfrom->PushMessage(NetMsgType::GETDATA, vToFetch);
    }


    else if (strCommand == NetMsgType::GETDATA)
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (fDebug || (vInv.size() != 1))
            LogPrint("net", "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

        if ((fDebug && vInv.size() > 0) || (vInv.size() == 1))
            LogPrint("net", "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);

        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom);
    }


    else if (strCommand == NetMsgType::GETBLOCKS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint("net", "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex))
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                LogPrint("net", "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll
                // trigger the peer to getblocks the next batch of inventory.
                LogPrint("net", "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::GETHEADERS)
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        LOCK(cs_main);

        if (chainActive.Tip() != 0 && chainActive.Tip()->nHeight > 100000 && IsInitialBlockDownload())
        {
            //fprintf(stderr,"dont process getheaders during initial download\n");
            return true;
        }
        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            BlockMap::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
            {
                //fprintf(stderr,"mi == end()\n");
                return true;
            }
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex)
                pindex = chainActive.Next(pindex);
        }

        // we must use CNetworkBlockHeader, as CBlockHeader won't include the 0x00 nTx count at the end for compatibility
        vector<CNetworkBlockHeader> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrint("net", "getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
        //if ( pfrom->lasthdrsreq >= chainActive.Height()-MAX_HEADERS_RESULTS || pfrom->lasthdrsreq != (int32_t)(pindex ? pindex->nHeight : -1) )// no need to ever suppress this
        {
            pfrom->lasthdrsreq = (int32_t)(pindex ? pindex->nHeight : -1);
            for (; pindex; pindex = chainActive.Next(pindex))
            {
                CBlockHeader h = pindex->GetBlockHeader();
                //printf("size.%i, solution size.%i\n", (int)sizeof(h), (int)h.nSolution.size());
                //printf("hash.%s prevhash.%s nonce.%s\n", h.GetHash().ToString().c_str(), h.hashPrevBlock.ToString().c_str(), h.nNonce.ToString().c_str());
                vHeaders.push_back(pindex->GetBlockHeader());
                if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                    break;
            }
            pfrom->PushMessage(NetMsgType::HEADERS, vHeaders);
        }
    }


    else if (strCommand == NetMsgType::TX)
    {
        if (IsInitialBlockDownload())
            return true;

        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        pfrom->setAskFor.erase(inv.hash);
        mapAlreadyAskedFor.erase(inv);

        if (!AlreadyHave(inv) && AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs))
        {
            mempool.check(pcoinsTip);
            RelayTransaction(tx);
            vWorkQueue.push_back(inv.hash);

            LogPrint("mempool", "AcceptToMemoryPool: peer=%d %s: accepted %s (poolsz %u)\n",
                     pfrom->id, pfrom->cleanSubVer,
                     tx.GetHash().ToString(),
                     mempool.mapTx.size());

            // Recursively process any orphan transactions that depended on this one
            set<NodeId> setMisbehaving;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                map<uint256, set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
                if (itByPrev == mapOrphanTransactionsByPrev.end())
                    continue;
                for (set<uint256>::iterator mi = itByPrev->second.begin();
                     mi != itByPrev->second.end();
                     ++mi)
                {
                    const uint256& orphanHash = *mi;
                    const CTransaction& orphanTx = mapOrphanTransactions[orphanHash].tx;
                    NodeId fromPeer = mapOrphanTransactions[orphanHash].fromPeer;
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if (setMisbehaving.count(fromPeer))
                        continue;
                    if (AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2))
                    {
                        LogPrint("mempool", "   accepted orphan tx %s\n", orphanHash.ToString());
                        RelayTransaction(orphanTx);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        int nDos = 0;
                        if (stateDummy.IsInvalid(nDos) && nDos > 0)
                        {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(fromPeer, nDos);
                            setMisbehaving.insert(fromPeer);
                            LogPrint("mempool", "   invalid orphan tx %s\n", orphanHash.ToString());
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LogPrint("mempool", "   removed orphan tx %s\n", orphanHash.ToString());
                        vEraseQueue.push_back(orphanHash);
                        assert(recentRejects);
                        recentRejects->insert(orphanHash);
                    }
                    mempool.check(pcoinsTip);
                }
            }

            BOOST_FOREACH(uint256 hash, vEraseQueue)
            EraseOrphanTx(hash);
        }
        // TODO: currently, prohibit joinsplits and shielded spends/outputs from entering mapOrphans
        else if (fMissingInputs &&
                 tx.vjoinsplit.empty() &&
                 tx.GetSaplingSpendsCount() == 0 &&
                 tx.GetSaplingOutputsCount() == 0)
        {
            // valid stake transactions end up in the orphan tx bin
            AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint("mempool", "mapOrphan overflow, removed %u tx\n", nEvicted);
        } else {
            assert(recentRejects);
            recentRejects->insert(tx.GetHash());

            if (pfrom->fWhitelisted) {
                // Always relay transactions received from whitelisted peers, even
                // if they were already in the mempool or rejected from it due
                // to policy, allowing the node to function as a gateway for
                // nodes hidden behind it.
                //
                // Never relay transactions that we would assign a non-zero DoS
                // score for, as we expect peers to do the same with us in that
                // case.
                int nDoS = 0;
                if (!state.IsInvalid(nDoS) || nDoS == 0) {
                    LogPrintf("Force relaying tx %s from whitelisted peer=%d\n", tx.GetHash().ToString(), pfrom->id);
                    RelayTransaction(tx);
                } else {
                    LogPrintf("Not relaying invalid transaction %s from whitelisted peer=%d (%s (code %d))\n",
                              tx.GetHash().ToString(), pfrom->id, state.GetRejectReason(), state.GetRejectCode());
                }
            }
        }
        int nDoS = 0;
        if (state.IsInvalid(nDoS))
        {
            LogPrint("mempool", "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.GetHash().ToString(),
                     pfrom->id, pfrom->cleanSubVer,
                     state.GetRejectReason());
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
    }

    else if (strCommand == NetMsgType::HEADERS && !fImporting && !fReindex) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
            Misbehaving(pfrom->GetId(), 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        if (nCount == 0) {
            // Nothing interesting. Stop asking this peers for more headers.
            return true;
        }

        bool hasNewHeaders = true;

        // only KMD have checkpoints in sources, so, using IsInitialBlockDownload() here is
        // not applicable for assetchains (!)
        if (GetBoolArg("-fixibd", false) && chainName.isKMD() && IsInitialBlockDownload()) {

            /**
             * This is experimental feature avaliable only for KMD during initial block download running with
             * -fixibd arg. Fix was offered by domob1812 here:
             * https://github.com/bitcoin/bitcoin/pull/8054/files#diff-7ec3c68a81efff79b6ca22ac1f1eabbaR5099
             * but later it was reverted bcz of synchronization stuck issues.
             * Explanation:
             * https://github.com/bitcoin/bitcoin/pull/8306#issuecomment-231584578
             * Limiting this fix only to IBD and with special command line arg makes it safe, bcz
             * default behaviour is to request new headers anyway.
            */

            // If we already know the last header in the message, then it contains
            // no new information for us.  In this case, we do not request
            // more headers later.  This prevents multiple chains of redundant
            // getheader requests from running in parallel if triggered by incoming
            // blocks while the node is still in initial headers sync.
            hasNewHeaders = (mapBlockIndex.count(headers.back().GetHash()) == 0);
        }

        CBlockIndex *pindexLast = NULL;
        BOOST_FOREACH(const CBlockHeader& header, headers) {
            //printf("size.%i, solution size.%i\n", (int)sizeof(header), (int)header.nSolution.size());
            //printf("hash.%s prevhash.%s nonce.%s\n", header.GetHash().ToString().c_str(), header.hashPrevBlock.ToString().c_str(), header.nNonce.ToString().c_str());

            CValidationState state;
            if (pindexLast != NULL && header.hashPrevBlock != pindexLast->GetBlockHash()) {
                Misbehaving(pfrom->GetId(), 20);
                return error("non-continuous headers sequence");
            }
            int32_t futureblock;
            if (!AcceptBlockHeader(&futureblock,header, state, &pindexLast)) {
                int nDoS;
                if (state.IsInvalid(nDoS) && futureblock == 0)
                {
                    if (nDoS > 0 && futureblock == 0)
                        Misbehaving(pfrom->GetId(), nDoS/nDoS);
                    return error("invalid header received");
                }
            }
        }

        if (pindexLast)
            UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        /* debug log */
        // if (!hasNewHeaders && nCount == MAX_HEADERS_RESULTS && pindexLast) {
        //         static int64_t bytes_saved;
        //         bytes_saved += MAX_HEADERS_RESULTS * (CBlockHeader::HEADER_SIZE + 1348);
        //         LogPrintf("[%d] don't request getheaders (%d) from peer=%d, bcz it's IBD and (%s) is already known!\n",
        //             bytes_saved, pindexLast->nHeight, pfrom->id, headers.back().GetHash().ToString());
        // }

        if (nCount == MAX_HEADERS_RESULTS && pindexLast && hasNewHeaders) {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            if ( pfrom->sendhdrsreq >= chainActive.Height()-MAX_HEADERS_RESULTS || pindexLast->nHeight != pfrom->sendhdrsreq )
            {
                pfrom->sendhdrsreq = (int32_t)pindexLast->nHeight;
                LogPrint("net", "more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
                pfrom->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), uint256());
            }
        }

        CheckBlockIndex();
    }

    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;

        CInv inv(MSG_BLOCK, block.GetHash());
        LogPrint("net", "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);

        pfrom->AddInventoryKnown(inv);

        CValidationState state;
        // Process all blocks from whitelisted peers, even if not requested,
        // unless we're still syncing with the network.
        // Such an unrequested block may still be processed, subject to the
        // conditions in AcceptBlock().
        bool forceProcessing = pfrom->fWhitelisted && !IsInitialBlockDownload();
        ProcessNewBlock(0,0,state, pfrom, &block, forceProcessing, NULL);
        int nDoS;
        if (state.IsInvalid(nDoS)) {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, state.GetRejectCode(),
                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash);
            if (nDoS > 0) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), nDoS);
            }
        }

    }


    else if (strCommand == NetMsgType::MEMPOOL)
    {
        LOCK2(cs_main, pfrom->cs_filter);

        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        BOOST_FOREACH(uint256& hash, vtxid) {
            CInv inv(MSG_TX, hash);
            if (pfrom->pfilter) {
                CTransaction tx;
                bool fInMemPool = mempool.lookup(hash, tx);
                if (!fInMemPool) continue; // another thread removed since queryHashes, maybe...
                if (!pfrom->pfilter->IsRelevantAndUpdate(tx)) continue;
            }
            vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ) {
                pfrom->PushMessage(NetMsgType::INV, vInv);
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            pfrom->PushMessage(NetMsgType::INV, vInv);
    }
    else if (fAlerts && strCommand == NetMsgType::ALERT)
    {
        CAlert alert;
        vRecv >> alert;

        uint256 alertHash = alert.GetHash();
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert(Params().AlertKey()))
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                Misbehaving(pfrom->GetId(), 10);
            }
        }
    }

    else if (!(nLocalServices & NODE_BLOOM) &&
              (strCommand == NetMsgType::FILTERLOAD ||
               strCommand == NetMsgType::FILTERADD))
    {
        if (pfrom->nVersion >= NO_BLOOM_VERSION) {
            Misbehaving(pfrom->GetId(), 100);
            return false;
        } else if (GetBoolArg("-enforcenodebloom", false)) {
            pfrom->fDisconnect = true;
            return false;
        }
    }


    else if (strCommand == NetMsgType::FILTERLOAD)
    {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints())
            // There is no excuse for sending a too-large filter
            Misbehaving(pfrom->GetId(), 100);
        else
        {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::FILTERADD)
    {
        vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE)
        {
            Misbehaving(pfrom->GetId(), 100);
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter)
                pfrom->pfilter->insert(vData);
            else
                Misbehaving(pfrom->GetId(), 100);
        }
    }


    else if (strCommand == NetMsgType::FILTERCLEAR)
    {
        LOCK(pfrom->cs_filter);
        if (nLocalServices & NODE_BLOOM) {
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter();
        }
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::REJECT)
    {
        if (fDebug) {
            try {
                string strMsg; unsigned char ccode; string strReason;
                vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

                ostringstream ss;
                ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

                if (strMsg == "block" || strMsg == "tx")
                {
                    uint256 hash;
                    vRecv >> hash;
                    ss << ": hash " << hash.ToString();
                }
                LogPrint("net", "Reject %s\n", SanitizeString(ss.str()));
            } catch (const std::ios_base::failure&) {
                // Avoid feedback loops by preventing reject messages from triggering a new reject message.
                LogPrint("net", "Unparseable reject message received\n");
            }
        }
    }
    else if (strCommand == NetMsgType::NOTFOUND) {
        // We do not care about the NOTFOUND message, but logging an Unknown Command
        // message would be undesirable as we transmit it ourselves.
    }

    else {
        // Ignore unknown commands for extensibility
        LogPrint("net", "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
    }



    return true;
}

// requires LOCK(cs_vRecvMsg)
bool ProcessMessages(CNode* pfrom)
{
    //if (fDebug)
    //    LogPrintf("%s(%u messages)\n", __func__, pfrom->vRecvMsg.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fOk = true;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom);

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return fOk;

    std::deque<CNetMessage>::iterator it = pfrom->vRecvMsg.begin();
    while (!pfrom->fDisconnect && it != pfrom->vRecvMsg.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->nSendSize >= SendBufferSize())
            break;

        // get next message
        CNetMessage& msg = *it;

        //if (fDebug)
        //    LogPrintf("%s(message %u msgsz, %u bytes, complete:%s)\n", __func__,
        //            msg.hdr.nMessageSize, msg.vRecv.size(),
        //            msg.complete() ? "Y" : "N");

        // end, if an incomplete message is found
        if (!msg.complete())
            break;

        // at this point, any failure means we can delete the current message
        it++;

        // Scan for message start
        if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
            LogPrintf("PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->id);
            fOk = false;
            break;
        }

        // Read header
        CMessageHeader& hdr = msg.hdr;
        if (!hdr.IsValid(Params().MessageStart()))
        {
            LogPrintf("PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->id);
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum
        CDataStream& vRecv = msg.vRecv;
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = ReadLE32((unsigned char*)&hash);
        if (nChecksum != hdr.nChecksum)
        {
            LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n", __func__,
                      SanitizeString(strCommand), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Process message
        bool fRet = false;
        try
        {
            fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime);
            boost::this_thread::interruption_point();
        }
        catch (const std::ios_base::failure& e)
        {
            pfrom->PushMessage(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, string("error parsing message"));
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                LogPrintf("%s(%s, %u bytes): Exception '%s' caught\n", __func__, SanitizeString(strCommand), nMessageSize, e.what());
            }
            else
            {
                //PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (const boost::thread_interrupted&) {
            throw;
        }
        catch (const std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            LogPrintf("%s(%s, %u bytes) FAILED peer=%d\n", __func__, SanitizeString(strCommand), nMessageSize, pfrom->id);

        break;
    }

    // In case the connection got shut down, its receive buffer was wiped
    if (!pfrom->fDisconnect)
        pfrom->vRecvMsg.erase(pfrom->vRecvMsg.begin(), it);

    return fOk;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    const Consensus::Params& consensusParams = Params().GetConsensus();
    {
        // Don't send anything until we get its version message
        if (pto->nVersion == 0)
            return true;

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
            if (pto->nPingNonceSent != 0) {
                pto->nPingRetry++;
            }

        }

        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                pto->PushMessage(NetMsgType::PING, nonce);
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            LOCK(cs_vNodes);
            BOOST_FOREACH(CNode* pnode, vNodes)
            {
                // Periodically clear addrKnown to allow refresh broadcasts
                if (nLastRebroadcast)
                    pnode->addrKnown.reset();

                // Rebroadcast our address
                AdvertizeLocal(pnode);
            }
            if (!vNodes.empty())
                nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            BOOST_FOREACH(const CAddress& addr, pto->vAddrToSend)
            {
                if (!pto->addrKnown.contains(addr.GetKey()))
                {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);

                    if (vAddr.size() >= MAX_ADDR_TO_SEND)
                    {
                      // Should be impossible since we always check size before adding to
                      // vAddrToSend. Recover by trimming the vector.
                      vAddr.resize(MAX_ADDR_TO_SEND);
                    }
                    const char* msg_type;
                    int make_flags;
                    if (pto->m_wants_addrv2) {
                        msg_type = NetMsgType::ADDRV2;
                        make_flags = ADDRV2_FORMAT;
                    } else {
                        msg_type = NetMsgType::ADDR;
                        make_flags = 0;
                    }
                    pto->PushAddrMessage(CNetMsgMaker(std::min(pto->nVersion, PROTOCOL_VERSION)).Make(make_flags, msg_type, pto->vAddrToSend));

                }
            }

            pto->vAddrToSend.clear();
            vAddr.clear();
        }

        CNodeState &state = *State(pto->GetId());
        if (state.fShouldBan) {
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;
                if (pto->addr.IsLocal())
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else
                {
                    CNode::Ban(pto->addr, BanReasonNodeMisbehaving);
                }
            }
            state.fShouldBan = false;
        }
        if ( KOMODO_NSPV_SUPERLITE )
        {
            komodo_nSPV(pto);
            return(true);
        }
        BOOST_FOREACH(const CBlockReject& reject, state.rejects)
        pto->PushMessage(NetMsgType::REJECT, (string)"block", reject.chRejectCode, reject.strRejectReason, reject.hashBlock);
        state.rejects.clear();

        // Start block sync
        if (pindexBestHeader == NULL)
            pindexBestHeader = chainActive.Tip();
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (!state.fSyncStarted && !pto->fClient && !fImporting && !fReindex && pindexBestHeader!=0) {
            // Only actively request headers from a single peer, unless we're close to today.
            if ((nSyncStarted == 0 && fFetch) || pindexBestHeader->GetBlockTime() > GetTime() - 24 * 60 * 60) {
                state.fSyncStarted = true;
                nSyncStarted++;
                CBlockIndex *pindexStart = pindexBestHeader->pprev ? pindexBestHeader->pprev : pindexBestHeader;
                LogPrint("net", "initial getheaders (%d) to peer=%d (startheight:%d)\n", pindexStart->nHeight, pto->id, pto->nStartingHeight);
                pto->PushMessage(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexStart), uint256());
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload())
        {
            GetMainSignals().Broadcast(nTimeBestReceived);
        }

        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            BOOST_FOREACH(const CInv& inv, pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(inv.hash) ^ UintToArith256(hashSalt));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((UintToArith256(hashRand) & 3) != 0);

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage(NetMsgType::INV, vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage(NetMsgType::INV, vInv);

        // Detect whether we're stalling
        int64_t nNow = GetTimeMicros();
        if (!pto->fDisconnect && state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
            pto->fDisconnect = true;
        }
        // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
        // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
        // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise non-existing block hashes
        // to unreasonably increase our timeout.
        // We also compare the block download timeout originally calculated against the time at which we'd disconnect
        // if we assumed the block were being requested now (ignoring blocks we've requested from this peer, since we're
        // only looking at this peer's oldest request).  This way a large queue in the past doesn't result in a
        // permanently large window for this block to be delivered (ie if the number of blocks in flight is decreasing
        // more quickly than once every 5 minutes, then we'll shorten the download window for this block).
        if (!pto->fDisconnect && state.vBlocksInFlight.size() > 0) {
            QueuedBlock &queuedBlock = state.vBlocksInFlight.front();
            int64_t nTimeoutIfRequestedNow = GetBlockTimeout(nNow, nQueuedValidatedHeaders - state.nBlocksInFlightValidHeaders, consensusParams);
            if (queuedBlock.nTimeDisconnect > nTimeoutIfRequestedNow) {
                LogPrint("net", "Reducing block download timeout for peer=%d block=%s, orig=%d new=%d\n", pto->id, queuedBlock.hash.ToString(), queuedBlock.nTimeDisconnect, nTimeoutIfRequestedNow);
                queuedBlock.nTimeDisconnect = nTimeoutIfRequestedNow;
            }
            if (queuedBlock.nTimeDisconnect < nNow) {
                LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", queuedBlock.hash.ToString(), pto->id);
                pto->fDisconnect = true;
            }
        }

        //
        // Message: getdata (blocks)
        //
        static uint256 zero;
        vector<CInv> vGetData;
        if (!pto->fDisconnect && !pto->fClient && (fFetch || !IsInitialBlockDownload()) && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            BOOST_FOREACH(CBlockIndex *pindex, vToDownload) {
                vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), consensusParams, pindex);
                LogPrint("net", "Requesting block %s (%d) peer=%d\n", pindex->GetBlockHash().ToString(),
                         pindex->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                if (State(staller)->nStallingSince == 0) {
                    State(staller)->nStallingSince = nNow;
                    LogPrint("net", "Stall started peer=%d\n", staller);
                }
            }
        }
        /*CBlockIndex *pindex;
        if ( komodo_requestedhash != zero && komodo_requestedcount < 16 && (pindex= komodo_getblockindex(komodo_requestedhash)) != 0 )
        {
            LogPrint("net","komodo_requestedhash.%d request %s to nodeid.%d\n",komodo_requestedcount,komodo_requestedhash.ToString().c_str(),pto->GetId());
            fprintf(stderr,"komodo_requestedhash.%d request %s to nodeid.%d\n",komodo_requestedcount,komodo_requestedhash.ToString().c_str(),pto->GetId());
            vGetData.push_back(CInv(MSG_BLOCK, komodo_requestedhash));
            MarkBlockAsInFlight(pto->GetId(), komodo_requestedhash, consensusParams, pindex);
            komodo_requestedcount++;
            if ( komodo_requestedcount > 16 )
            {
                memset(&komodo_requestedhash,0,sizeof(komodo_requestedhash));
                komodo_requestedcount = 0;
            }
        }*/

        //
        // Message: getdata (non-blocks)
        //
        while (!pto->fDisconnect && !pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv))
            {
                if (fDebug)
                    LogPrint("net", "Requesting %s peer=%d\n", inv.ToString(), pto->id);
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage(NetMsgType::GETDATA, vGetData);
                    vGetData.clear();
                }
            } else {
                //If we're not going to ask, don't expect a response.
                pto->setAskFor.erase(inv.hash);
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage(NetMsgType::GETDATA, vGetData);

    }
    return true;
}

std::string CBlockFileInfo::ToString() const {
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}



static class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup() {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();

        // orphan transactions
        mapOrphanTransactions.clear();
        mapOrphanTransactionsByPrev.clear();
    }
} instance_of_cmaincleanup;

extern "C" const char* getDataDir()
{
    return GetDataDir().string().c_str();
}


// Set default values of new CMutableTransaction based on consensus rules at given height.
CMutableTransaction CreateNewContextualCMutableTransaction(const Consensus::Params& consensusParams, int nHeight)
{
    CMutableTransaction mtx;

    auto txVersionInfo = CurrentTxVersionInfo(consensusParams, nHeight);
    mtx.fOverwintered   = txVersionInfo.fOverwintered;
    mtx.nVersionGroupId = txVersionInfo.nVersionGroupId;
    mtx.nVersion        = txVersionInfo.nVersion;

    if (mtx.fOverwintered)
    {
        if (mtx.nVersion >= ORCHARD_TX_VERSION) {
            mtx.nConsensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
        }


        // mtx.nExpiryHeight == 0 is valid for coinbase transactions
        mtx.nExpiryHeight = nHeight + expiryDelta;
        if (mtx.nExpiryHeight <= 0 || mtx.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
            throw new std::runtime_error("CreateNewContextualCMutableTransaction: invalid expiry height");
        }

        // NOTE: If the expiry height crosses into an incompatible consensus epoch, and it is changed to the last block
        // of the current epoch, the transaction will be rejected if it falls within the expiring soon threshold of
        // TX_EXPIRING_SOON_THRESHOLD (3) blocks (for DoS mitigation) based on the current height.
        auto nextActivationHeight = NextActivationHeight(nHeight, consensusParams);
        if (nextActivationHeight) {
            mtx.nExpiryHeight = std::min(mtx.nExpiryHeight, static_cast<uint32_t>(nextActivationHeight.value()) - 1);
        }
    }

    return mtx;
}
