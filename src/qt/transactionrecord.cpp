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
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const RpcArcTransaction &arcTx)
{
    QList<TransactionRecord> parts;
    QList<TransactionRecord> partsChange;

    if (arcTx.spentFrom.size() > 0) {

        for (int i = 0; i < arcTx.vTSend.size(); i++) {
            auto tx = TransactionRecord();
            tx.archiveType = arcTx.archiveType;
            tx.hash = arcTx.txid;
            tx.time = arcTx.nTime;
            tx.address = arcTx.vTSend[i].encodedAddress;
            tx.debit = arcTx.vTSend[i].amount;
            tx.idx = arcTx.vTSend[i].vout;
            tx.involvesWatchAddress = !arcTx.vTSend[i].mine;

            bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vTSend[i].encodedAddress) != arcTx.spentFrom.end();
            if (change) {
                tx.type = TransactionRecord::SendToSelf;
                partsChange.append(tx);
            } else {
                tx.type = TransactionRecord::SendToAddress;
                parts.append(tx);
            }
        }

        for (int i = 0; i < arcTx.vZsSend.size(); i++) {
            auto tx = TransactionRecord();
            tx.archiveType = arcTx.archiveType;
            tx.hash = arcTx.txid;
            tx.time = arcTx.nTime;
            tx.address = arcTx.vZsSend[i].encodedAddress;
            tx.credit = -arcTx.vZsSend[i].amount;
            tx.idx = arcTx.vZsSend[i].shieldedOutputIndex;
            tx.involvesWatchAddress = !arcTx.vZsSend[i].mine;
            tx.memohex = arcTx.vZsSend[i].memo;

            if (arcTx.vZsSend[i].memoStr.length() != 0) {
                tx.memo = arcTx.vZsSend[i].memoStr;
            }

            bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZsSend[i].encodedAddress) != arcTx.spentFrom.end();
            if (change) {
                if (arcTx.vZsSend[i].memoStr.length() == 0) {
                    tx.type = TransactionRecord::SendToSelf;
                } else {
                    tx.type = TransactionRecord::SendToSelfWithMemo;
                }
                partsChange.append(tx);
            } else {
                if (arcTx.vZsSend[i].memoStr.length() == 0) {
                    tx.type = TransactionRecord::SendToAddress;
                } else {
                    tx.type = TransactionRecord::SendToAddressWithMemo;
                }
                parts.append(tx);
            }
        }
    }


    for (int i = 0; i < arcTx.vTReceived.size(); i++) {
        auto tx = TransactionRecord();
        tx.archiveType = arcTx.archiveType;
        tx.hash = arcTx.txid;
        tx.time = arcTx.nTime;
        tx.address = arcTx.vTReceived[i].encodedAddress;
        tx.debit = arcTx.vTReceived[i].amount;
        tx.idx = arcTx.vTReceived[i].vout;
        tx.involvesWatchAddress = !arcTx.vTReceived[i].spendable;

        bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vTReceived[i].encodedAddress) != arcTx.spentFrom.end();
        if (change) {
            tx.type = TransactionRecord::SendToSelf;
            partsChange.append(tx);
        } else {
            if (arcTx.coinbase) {
                tx.type = TransactionRecord::Generated;
            } else {
                tx.type = TransactionRecord::RecvWithAddress;
            }
            parts.append(tx);
        }
    }

    for (int i = 0; i < arcTx.vZsReceived.size(); i++) {
        auto tx = TransactionRecord();
        tx.archiveType = arcTx.archiveType;
        tx.hash = arcTx.txid;
        tx.time = arcTx.nTime;
        tx.address = arcTx.vZsReceived[i].encodedAddress;
        tx.debit = arcTx.vZsReceived[i].amount;
        tx.idx = arcTx.vZsReceived[i].shieldedOutputIndex;
        tx.involvesWatchAddress = !arcTx.vZsReceived[i].spendable;
        tx.memohex = arcTx.vZsReceived[i].memo;

        if (arcTx.vZsReceived[i].memoStr.length() != 0) {
            tx.memo = arcTx.vZsReceived[i].memoStr;
        }

        bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZsReceived[i].encodedAddress) != arcTx.spentFrom.end();
        if (change) {
            if (arcTx.vZsReceived[i].memoStr.length() == 0) {
                tx.type = TransactionRecord::SendToSelf;
            } else {
                tx.type = TransactionRecord::SendToSelfWithMemo;
            }
            partsChange.append(tx);
        } else {
            if (arcTx.vZsReceived[i].memoStr.length() == 0) {
                tx.type = TransactionRecord::RecvWithAddress;
            } else {
                tx.type = TransactionRecord::RecvWithAddressWithMemo;
            }

            parts.append(tx);
        }
    }

    if (parts.size() == 0) {
        parts.append(partsChange);
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
        (pindex ? pindex->GetHeight() : std::numeric_limits<int>::max()),
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

                // Check if the block was requested by anyone
                if (GetTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
                    status.status = TransactionStatus::MaturesWarning;
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
        else if (GetTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
        {
            status.status = TransactionStatus::Offline;
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
