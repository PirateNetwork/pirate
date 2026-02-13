// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "timedata.h"
#include "main.h"
#include "wallet/wallet.h"
#include "wallet/rpcpiratewallet.h"
#include "key_io.h"

#include <stdint.h>
/**
 * @todo Add z bits
 * @body Need to use key(s) to view tx and populate data.
 */

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    // There are currently no cases where we hide transactions, but
    // we may want to use this in the future for things like RBF.
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 * Creates one parent record per txid showing total, with child records for each input/output.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const RpcArcTransaction &arcTx)
{
    QList<TransactionRecord> parts;
    
    // Structure to track inputs/outputs by address
    struct AddressGroup {
        std::string address;
        CAmount amount = 0;
        int count = 0;
        std::string memo;
        std::string memohex;
        bool involvesWatchAddress = false;
        bool isInput = false; // true for inputs (spent from), false for outputs (received)
        bool belongsToWallet = false; // true if this output belongs to the wallet (change/internal)
    };
    
    std::vector<AddressGroup> inputGroups;
    std::vector<AddressGroup> outputGroups;
    std::map<std::string, int> inputMap; // address -> index in inputGroups
    std::map<std::string, int> outputMap; // address -> index in outputGroups
    
    CAmount totalInputs = 0;
    CAmount totalOutputs = 0;

    // Process inputs (money being spent)
    if (arcTx.spentFrom.size() > 0) {
        // Transparent spends are inputs
        for (int i = 0; i < arcTx.vTSpend.size(); i++) {
            std::string addr = arcTx.vTSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vTSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vTSpend[i].spendable;
            totalInputs += arcTx.vTSpend[i].amount;
        }

        // Sapling spends are inputs
        for (int i = 0; i < arcTx.vZsSpend.size(); i++) {
            std::string addr = arcTx.vZsSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vZsSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vZsSpend[i].spendable;
            totalInputs += arcTx.vZsSpend[i].amount;
        }

        // Orchard spends are inputs
        for (int i = 0; i < arcTx.vZoSpend.size(); i++) {
            std::string addr = arcTx.vZoSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vZoSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vZoSpend[i].spendable;
            totalInputs += arcTx.vZoSpend[i].amount;
        }
    }

    // Process outputs (money being sent to addresses)
    // Transparent sends
    for (int i = 0; i < arcTx.vTSend.size(); i++) {
        std::string addr = arcTx.vTSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            // Check if this output belongs to the wallet
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vTSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vTSend[i].mine;
        totalOutputs += arcTx.vTSend[i].amount;
    }

    // Transparent receives (for pure receive transactions without spends)
    for (int i = 0; i < arcTx.vTReceived.size(); i++) {
        std::string addr = arcTx.vTReceived[i].encodedAddress;
        // Only add if not already in outputMap (avoid double-counting)
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true; // Received outputs always belong to wallet
            group.amount = arcTx.vTReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vTReceived[i].spendable;
            outputGroups.push_back(group);
            totalOutputs += arcTx.vTReceived[i].amount;
        }
        // Skip if already in map - already counted from vTSend
    }

    // Sapling sends (outputs to addresses)
    for (int i = 0; i < arcTx.vZsSend.size(); i++) {
        std::string addr = arcTx.vZsSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            // Check if this output belongs to the wallet
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vZsSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vZsSend[i].mine;
        if (arcTx.vZsSend[i].memoStr.length() != 0 && outputGroups[idx].memo.empty()) {
            outputGroups[idx].memo = arcTx.vZsSend[i].memoStr;
            outputGroups[idx].memohex = arcTx.vZsSend[i].memo;
        }
        totalOutputs += arcTx.vZsSend[i].amount;
    }

    // Sapling receives (for pure receive transactions without spends)
    for (int i = 0; i < arcTx.vZsReceived.size(); i++) {
        std::string addr = arcTx.vZsReceived[i].encodedAddress;
        // Only add if not already in outputMap (avoid double-counting)
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true; // Received outputs always belong to wallet
            group.amount = arcTx.vZsReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vZsReceived[i].spendable;
            if (arcTx.vZsReceived[i].memoStr.length() != 0) {
                group.memo = arcTx.vZsReceived[i].memoStr;
                group.memohex = arcTx.vZsReceived[i].memo;
            }
            outputGroups.push_back(group);
            totalOutputs += arcTx.vZsReceived[i].amount;
        }
        // Skip if already in map - already counted from vZsSend
    }

    // Orchard sends (outputs to addresses)
    for (int i = 0; i < arcTx.vZoSend.size(); i++) {
        std::string addr = arcTx.vZoSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            // Check if this output belongs to the wallet
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vZoSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vZoSend[i].mine;
        if (arcTx.vZoSend[i].memoStr.length() != 0 && outputGroups[idx].memo.empty()) {
            outputGroups[idx].memo = arcTx.vZoSend[i].memoStr;
            outputGroups[idx].memohex = arcTx.vZoSend[i].memo;
        }
        totalOutputs += arcTx.vZoSend[i].amount;
    }

    // Orchard receives (for pure receive transactions without spends)
    for (int i = 0; i < arcTx.vZoReceived.size(); i++) {
        std::string addr = arcTx.vZoReceived[i].encodedAddress;
        // Only add if not already in outputMap (avoid double-counting)
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true; // Received outputs always belong to wallet
            group.amount = arcTx.vZoReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vZoReceived[i].spendable;
            if (arcTx.vZoReceived[i].memoStr.length() != 0) {
                group.memo = arcTx.vZoReceived[i].memoStr;
                group.memohex = arcTx.vZoReceived[i].memo;
            }
            outputGroups.push_back(group);
            totalOutputs += arcTx.vZoReceived[i].amount;
        }
        // Skip if already in map - already counted from vZoSend
    }

    // Calculate wallet balance change (outputs belonging to wallet minus inputs)
    CAmount walletOutputs = 0;
    for (const auto& group : outputGroups) {
        if (group.belongsToWallet) {
            walletOutputs += group.amount;
        }
    }
    
    // Net change is wallet outputs minus inputs (all inputs are from wallet)
    CAmount txTotal = walletOutputs - totalInputs;
    
    TransactionRecord parent;
    parent.hash = arcTx.txid;
    parent.time = arcTx.nTime;
    parent.archiveType = arcTx.archiveType;
    parent.netChange = txTotal;
    parent.debit = totalOutputs;
    parent.credit = -totalInputs;
    parent.isParent = true;
    parent.isChild = false;
    parent.parentIdx = -1;
    parent.groupCount = inputGroups.size() + outputGroups.size();
    parent.idx = 0;
    parent.collapsed = false; // Start with children visible
    
    // Determine parent type based on transaction characteristics
    bool hasMemo = false;
    bool allOutputsToWallet = true;
    bool hasExternalOutput = false;
    
    // Check outputs for wallet ownership and memos
    for (const auto& group : outputGroups) {
        if (!group.belongsToWallet) {
            allOutputsToWallet = false;
            hasExternalOutput = true;
        }
        if (!group.memo.empty()) {
            hasMemo = true;
        }
    }
    
    // Also check input memos
    for (const auto& group : inputGroups) {
        if (!group.memo.empty()) {
            hasMemo = true;
        }
    }
    
    // Determine transaction type based on rules:
    if (arcTx.coinbase) {
        // Coinbase transaction
        parent.type = Generated;
    } else if (inputGroups.size() == 0) {
        // No inputs - this is a received transaction
        parent.type = hasMemo ? RecvWithAddressWithMemo : RecvWithAddress;
    } else if (allOutputsToWallet && outputGroups.size() > 0) {
        // All outputs belong to wallet - internal transfer
        parent.type = hasMemo ? SendToSelfWithMemo : SendToSelf;
    } else if (hasExternalOutput) {
        // Has at least one external output - sending transaction
        parent.type = hasMemo ? SendToAddressWithMemo : SendToAddress;
    } else {
        // Unknown case (should not happen)
        parent.type = Other;
    }
    
    parts.append(parent);
    int parentIndex = 0;
    int childIdx = 1;

    // Create child records for outputs first (sends before change)
    // First, external outputs (sends)
    for (const auto& group : outputGroups) {
        if (!group.belongsToWallet) {
            TransactionRecord child;
            child.hash = arcTx.txid;
            child.time = arcTx.nTime;
            child.archiveType = arcTx.archiveType;
            child.type = Output;
            child.address = group.address;
            child.groupCount = group.count;
            child.memo = group.memo;
            child.memohex = group.memohex;
            child.involvesWatchAddress = group.involvesWatchAddress;
            child.involvesOwnAddress = false;
            child.isParent = false;
            child.isChild = true;
            child.parentIdx = parentIndex;
            child.idx = childIdx++;
            child.debit = group.amount;
            child.credit = 0;
            child.netChange = group.amount;
            
            parts.append(child);
        }
    }
    
    // Then, outputs belonging to wallet (change or received)
    for (const auto& group : outputGroups) {
        if (group.belongsToWallet) {
            TransactionRecord child;
            child.hash = arcTx.txid;
            child.time = arcTx.nTime;
            child.archiveType = arcTx.archiveType;
            child.type = Output;
            child.address = group.address;
            child.groupCount = group.count;
            child.memo = group.memo;
            child.memohex = group.memohex;
            child.involvesWatchAddress = group.involvesWatchAddress;
            child.involvesOwnAddress = true;
            child.isParent = false;
            child.isChild = true;
            child.parentIdx = parentIndex;
            child.idx = childIdx++;
            child.debit = 0;
            child.credit = group.amount;
            child.netChange = group.amount;
            
            parts.append(child);
        }
    }

    // Create child records for inputs
    for (const auto& group : inputGroups) {
        TransactionRecord child;
        child.hash = arcTx.txid;
        child.time = arcTx.nTime;
        child.archiveType = arcTx.archiveType;
        child.type = Input;
        child.address = group.address;
        child.groupCount = group.count;
        child.memo = group.memo;
        child.memohex = group.memohex;
        child.involvesWatchAddress = group.involvesWatchAddress;
        child.isParent = false;
        child.isChild = true;
        child.parentIdx = parentIndex;
        child.idx = childIdx++;
        child.debit = 0;
        child.credit = -group.amount; // Negative for spent
        child.netChange = -group.amount;
        
        parts.append(child);
    }
    
    // Add fee record if there are inputs and fee is non-zero (fee = total outputs - total inputs)
    if (inputGroups.size() > 0) {
        CAmount fee = totalOutputs - totalInputs;
        if (fee != 0) {
            TransactionRecord feeChild;
            feeChild.hash = arcTx.txid;
            feeChild.time = arcTx.nTime;
            feeChild.archiveType = arcTx.archiveType;
            feeChild.type = Fee;
            feeChild.address = "";
            feeChild.groupCount = 0;
            feeChild.involvesWatchAddress = false;
            feeChild.involvesOwnAddress = false;
            feeChild.isParent = false;
            feeChild.isChild = true;
            feeChild.parentIdx = parentIndex;
            feeChild.idx = childIdx++;
            feeChild.debit = fee;
            feeChild.credit = 0;
            feeChild.netChange = fee;
            
            parts.append(feeChild);
        }
    }

    return parts;
}

void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    // Determine transaction status

    // Find the block the tx is in
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.countsForBalance = true;
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
        {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainActive.Height();
        }
        else
        {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if(type == TransactionRecord::Generated)
    {
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                status.matures_in = wtx.GetBlocksToMaturity();
            }
            else
            {
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        if (status.depth < 0)
        {
            status.status = TransactionStatus::Conflicted;
        }
        else if (status.depth == 0)
        {
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            status.status = TransactionStatus::Confirmed;
        }
    }
    status.needsUpdate = false;
}

bool TransactionRecord::statusUpdateNeeded() const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate;
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}
