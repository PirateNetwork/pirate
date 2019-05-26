// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZERONODE_SYNC_H
#define ZERONODE_SYNC_H

#define ZERONODE_SYNC_INITIAL 0
#define ZERONODE_SYNC_SPORKS 1
#define ZERONODE_SYNC_LIST 2
#define ZERONODE_SYNC_ZNW 3
#define ZERONODE_SYNC_BUDGET 4
#define ZERONODE_SYNC_BUDGET_PROP 10
#define ZERONODE_SYNC_BUDGET_FIN 11
#define ZERONODE_SYNC_FAILED 998
#define ZERONODE_SYNC_FINISHED 999

#define ZERONODE_SYNC_TIMEOUT 5
#define ZERONODE_SYNC_THRESHOLD 2

class CZeronodeSync;
extern CZeronodeSync zeronodeSync;

//
// CZeronodeSync : Sync zeronode assets in stages
//

class CZeronodeSync
{
public:
    std::map<uint256, int> mapSeenSyncZNB;
    std::map<uint256, int> mapSeenSyncZNW;
    std::map<uint256, int> mapSeenSyncBudget;

    int64_t lastZeronodeList;
    int64_t lastZeronodeWinner;
    int64_t lastBudgetItem;
    int64_t lastFailure;
    int nCountFailures;

    // sum of all counts
    int sumZeronodeList;
    int sumZeronodeWinner;
    int sumBudgetItemProp;
    int sumBudgetItemFin;
    // peers that reported counts
    int countZeronodeList;
    int countZeronodeWinner;
    int countBudgetItemProp;
    int countBudgetItemFin;

    // Count peers we've requested the list from
    int RequestedZeronodeAssets;
    int RequestedZeronodeAttempt;

    // Time when current zeronode asset sync started
    int64_t nAssetSyncStarted;

    CZeronodeSync();

    void AddedZeronodeList(uint256 hash);
    void AddedZeronodeWinner(uint256 hash);
    void AddedBudgetItem(uint256 hash);
    void GetNextAsset();
    std::string GetSyncStatus();
    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
    bool IsBudgetFinEmpty();
    bool IsBudgetPropEmpty();

    void Reset();
    void Process();
    bool IsFailed() { return RequestedZeronodeAssets == ZERONODE_SYNC_FAILED; }
    bool IsBlockchainSynced() { return RequestedZeronodeAssets > ZERONODE_SYNC_SPORKS; }
    bool IsZeronodeListSynced() { return RequestedZeronodeAssets > ZERONODE_SYNC_LIST; }
    bool IsWinnersListSynced() { return RequestedZeronodeAssets > ZERONODE_SYNC_ZNW; }
    bool IsSynced() { return RequestedZeronodeAssets == ZERONODE_SYNC_FINISHED; }
    void ClearFulfilledRequest();
};

#endif
