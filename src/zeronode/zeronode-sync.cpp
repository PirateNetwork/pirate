// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "zeronode/activezeronode.h"
#include "zeronode/zeronode-sync.h"
#include "zeronode/payments.h"
#include "zeronode/budget.h"
#include "zeronode/zeronode.h"
#include "zeronode/zeronodeman.h"
#include "zeronode/spork.h"
#include "util.h"
#include "addrman.h"
// clang-format on

class CZeronodeSync;
CZeronodeSync zeronodeSync;

CZeronodeSync::CZeronodeSync()
{
    Reset();
}


void CZeronodeSync::Reset()
{
    lastZeronodeList = 0;
    lastZeronodeWinner = 0;
    lastBudgetItem = 0;
    mapSeenSyncZNB.clear();
    mapSeenSyncZNW.clear();
    mapSeenSyncBudget.clear();
    lastFailure = 0;
    nCountFailures = 0;
    sumZeronodeList = 0;
    sumZeronodeWinner = 0;
    sumBudgetItemProp = 0;
    sumBudgetItemFin = 0;
    countZeronodeList = 0;
    countZeronodeWinner = 0;
    countBudgetItemProp = 0;
    countBudgetItemFin = 0;
    RequestedZeronodeAssets = ZERONODE_SYNC_INITIAL;
    RequestedZeronodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CZeronodeSync::AddedZeronodeList(uint256 hash)
{
    if (znodeman.mapSeenZeronodeBroadcast.count(hash)) {
        if (mapSeenSyncZNB[hash] < ZERONODE_SYNC_THRESHOLD) {
            lastZeronodeList = GetTime();
            mapSeenSyncZNB[hash]++;
        }
    } else {
        lastZeronodeList = GetTime();
        mapSeenSyncZNB.insert(make_pair(hash, 1));
    }
}

void CZeronodeSync::AddedZeronodeWinner(uint256 hash)
{
    if (zeronodePayments.mapZeronodePayeeVotes.count(hash)) {
        if (mapSeenSyncZNW[hash] < ZERONODE_SYNC_THRESHOLD) {
            lastZeronodeWinner = GetTime();
            mapSeenSyncZNW[hash]++;
        }
    } else {
        lastZeronodeWinner = GetTime();
        mapSeenSyncZNW.insert(make_pair(hash, 1));
    }
}

void CZeronodeSync::AddedBudgetItem(uint256 hash)
{
    if (budget.mapSeenZeronodeBudgetProposals.count(hash) || budget.mapSeenZeronodeBudgetVotes.count(hash) ||
        budget.mapSeenFinalizedBudgets.count(hash) || budget.mapSeenFinalizedBudgetVotes.count(hash)) {
        if (mapSeenSyncBudget[hash] < ZERONODE_SYNC_THRESHOLD) {
            lastBudgetItem = GetTime();
            mapSeenSyncBudget[hash]++;
        }
    } else {
        lastBudgetItem = GetTime();
        mapSeenSyncBudget.insert(make_pair(hash, 1));
    }
}

bool CZeronodeSync::IsBudgetPropEmpty()
{
    return sumBudgetItemProp == 0 && countBudgetItemProp > 0;
}

bool CZeronodeSync::IsBudgetFinEmpty()
{
    return sumBudgetItemFin == 0 && countBudgetItemFin > 0;
}

void CZeronodeSync::GetNextAsset()
{
    switch (RequestedZeronodeAssets) {
    case (ZERONODE_SYNC_INITIAL):
    case (ZERONODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        ClearFulfilledRequest();
        RequestedZeronodeAssets = ZERONODE_SYNC_SPORKS;
        break;
    case (ZERONODE_SYNC_SPORKS):
        RequestedZeronodeAssets = ZERONODE_SYNC_LIST;
        break;
    case (ZERONODE_SYNC_LIST):
        RequestedZeronodeAssets = ZERONODE_SYNC_ZNW;
        break;
    case (ZERONODE_SYNC_ZNW):
        RequestedZeronodeAssets = ZERONODE_SYNC_BUDGET;
        break;
    case (ZERONODE_SYNC_BUDGET):
        LogPrintf("CZeronodeSync::GetNextAsset - Sync has finished\n");
        RequestedZeronodeAssets = ZERONODE_SYNC_FINISHED;
        break;
    }
    RequestedZeronodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CZeronodeSync::GetSyncStatus()
{
    switch (zeronodeSync.RequestedZeronodeAssets) {
    case ZERONODE_SYNC_INITIAL:
        return _("Synchronization pending...");
    case ZERONODE_SYNC_SPORKS:
        return _("Synchronizing sporks...");
    case ZERONODE_SYNC_LIST:
        return _("Synchronizing zeronodes...");
    case ZERONODE_SYNC_ZNW:
        return _("Synchronizing zeronode winners...");
    case ZERONODE_SYNC_BUDGET:
        return _("Synchronizing budgets...");
    case ZERONODE_SYNC_FAILED:
        return _("Synchronization failed");
    case ZERONODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CZeronodeSync::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (strCommand == "ssc") { //Sync status count
        int nItemID;
        int nCount;
        vRecv >> nItemID >> nCount;

        if (RequestedZeronodeAssets >= ZERONODE_SYNC_FINISHED) return;

        //this means we will receive no further communication
        switch (nItemID) {
        case (ZERONODE_SYNC_LIST):
            if (nItemID != RequestedZeronodeAssets) return;
            sumZeronodeList += nCount;
            countZeronodeList++;
            break;
        case (ZERONODE_SYNC_ZNW):
            if (nItemID != RequestedZeronodeAssets) return;
            sumZeronodeWinner += nCount;
            countZeronodeWinner++;
            break;
        case (ZERONODE_SYNC_BUDGET_PROP):
            if (RequestedZeronodeAssets != ZERONODE_SYNC_BUDGET) return;
            sumBudgetItemProp += nCount;
            countBudgetItemProp++;
            break;
        case (ZERONODE_SYNC_BUDGET_FIN):
            if (RequestedZeronodeAssets != ZERONODE_SYNC_BUDGET) return;
            sumBudgetItemFin += nCount;
            countBudgetItemFin++;
            break;
        }

        LogPrint("zeronode", "CZeronodeSync:ProcessMessage - ssc - got inventory count %d %d\n", nItemID, nCount);
    }
}

void CZeronodeSync::ClearFulfilledRequest()
{
    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        pnode->ClearFulfilledRequest("getspork");
        pnode->ClearFulfilledRequest("znsync");
        pnode->ClearFulfilledRequest("znwsync");
        pnode->ClearFulfilledRequest("busync");
    }
}

void CZeronodeSync::Process()
{
    static int tick = 0;
    static int syncCount = 0;

    if (tick++ % ZERONODE_SYNC_TIMEOUT != 0) return;

    if (IsSynced()) {
        /*
            Resync if we lose all zeronodes from sleep/wake or failure to sync originally
        */
        if (znodeman.CountEnabled() == 0 ) {
			if(syncCount < 2){
				Reset();
				syncCount++;
			}
        } else
            return;
    }

    //try syncing again
    if (RequestedZeronodeAssets == ZERONODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedZeronodeAssets == ZERONODE_SYNC_FAILED) {
        return;
    }

    LogPrint("zeronode", "CZeronodeSync::Process() - tick %d RequestedZeronodeAssets %d\n", tick, RequestedZeronodeAssets);
    LogPrint("zeronode", "lastZeronodeList = %d\n", lastZeronodeList);

    if (RequestedZeronodeAssets == ZERONODE_SYNC_INITIAL) GetNextAsset();

    // sporks synced but blockchain is not, wait until we're almost at a recent block to continue
    if (NetworkIdFromCommandLine() != CBaseChainParams::REGTEST &&
        !IsBlockchainSynced() && RequestedZeronodeAssets > ZERONODE_SYNC_SPORKS) return;

    TRY_LOCK(cs_vNodes, lockRecv);
    if (!lockRecv) return;

    BOOST_FOREACH (CNode* pnode, vNodes) {
        if (NetworkIdFromCommandLine() == CBaseChainParams::REGTEST) {
            if (RequestedZeronodeAttempt <= 2) {
                pnode->PushMessage("getsporks"); //get current network sporks
            } else if (RequestedZeronodeAttempt < 4) {
                znodeman.DsegUpdate(pnode);
            } else if (RequestedZeronodeAttempt < 6) {
                int nMnCount = znodeman.CountEnabled();
                pnode->PushMessage("znget", nMnCount); //sync payees
                uint256 n = uint256();
                pnode->PushMessage("znvs", n); //sync zeronode votes
            } else {
                RequestedZeronodeAssets = ZERONODE_SYNC_FINISHED;
            }
            RequestedZeronodeAttempt++;
            return;
        }

        //set to synced
        if (RequestedZeronodeAssets == ZERONODE_SYNC_SPORKS) {
            if (pnode->HasFulfilledRequest("getspork")) continue;
            pnode->FulfilledRequest("getspork");

            pnode->PushMessage("getsporks"); //get current network sporks
            if (RequestedZeronodeAttempt >= 2) GetNextAsset();
            RequestedZeronodeAttempt++;

            return;
        }

        if (pnode->nVersion >= zeronodePayments.GetMinZeronodePaymentsProto()) {
            if (RequestedZeronodeAssets == ZERONODE_SYNC_LIST) {
                if (lastZeronodeList > 0 && lastZeronodeList < GetTime() - ZERONODE_SYNC_TIMEOUT * 2 && RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("znsync")) continue;
                pnode->FulfilledRequest("znsync");

                // timeout
                if (lastZeronodeList == 0 &&
                    (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > ZERONODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CZeronodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedZeronodeAssets = ZERONODE_SYNC_FAILED;
                        RequestedZeronodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3) return;

                znodeman.DsegUpdate(pnode);
                RequestedZeronodeAttempt++;
                return;
            }

            if (RequestedZeronodeAssets == ZERONODE_SYNC_ZNW) {
                if (lastZeronodeWinner > 0 && lastZeronodeWinner < GetTime() - ZERONODE_SYNC_TIMEOUT * 2 && RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD) { //hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();
                    return;
                }

                if (pnode->HasFulfilledRequest("znwsync")) continue;
                pnode->FulfilledRequest("znwsync");

                // timeout
                if (lastZeronodeWinner == 0 &&
                    (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > ZERONODE_SYNC_TIMEOUT * 5)) {
                    if (IsSporkActive(SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT)) {
                        LogPrintf("CZeronodeSync::Process - ERROR - Sync has failed, will retry later\n");
                        RequestedZeronodeAssets = ZERONODE_SYNC_FAILED;
                        RequestedZeronodeAttempt = 0;
                        lastFailure = GetTime();
                        nCountFailures++;
                    } else {
                        GetNextAsset();
                    }
                    return;
                }

                if (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3) return;

                CBlockIndex* pindexPrev = chainActive.Tip();
                if (pindexPrev == NULL) return;

                int nMnCount = znodeman.CountEnabled();
                pnode->PushMessage("znget", nMnCount); //sync payees
                RequestedZeronodeAttempt++;

                return;
            }
        }

        if (pnode->nVersion >= ActiveProtocol()) {
            if (RequestedZeronodeAssets == ZERONODE_SYNC_BUDGET) {

                // We'll start rejecting votes if we accidentally get set as synced too soon
                if (lastBudgetItem > 0 && lastBudgetItem < GetTime() - ZERONODE_SYNC_TIMEOUT * 2 && RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD) {

                    // Hasn't received a new item in the last five seconds, so we'll move to the
                    GetNextAsset();

                    // Try to activate our zeronode if possible
                    activeZeronode.ManageStatus();

                    return;
                }

                // timeout
                if (lastBudgetItem == 0 &&
                    (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3 || GetTime() - nAssetSyncStarted > ZERONODE_SYNC_TIMEOUT * 5)) {
                    // maybe there is no budgets at all, so just finish syncing
                    GetNextAsset();
                    activeZeronode.ManageStatus();
                    return;
                }

                if (pnode->HasFulfilledRequest("busync")) continue;
                pnode->FulfilledRequest("busync");

                if (RequestedZeronodeAttempt >= ZERONODE_SYNC_THRESHOLD * 3) return;

                uint256 n = uint256();
                pnode->PushMessage("znvs", n); //sync zeronode votes
                RequestedZeronodeAttempt++;

                return;
            }
        }
    }
}
