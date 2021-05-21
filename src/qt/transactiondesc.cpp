// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "transactiondesc.h"

#include "komodounits.h"
#include "guiutil.h"
#include "paymentserver.h"
#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "script/script.h"
#include "timedata.h"
#include "util.h"
#include "main.h"
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/rpcpiratewallet.h"
#include "key_io.h"

#include <stdint.h>
#include <string>

QString TransactionDesc::FormatTxStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
            return tr("Open for %n more block(s)", "", wtx.nLockTime - chainActive.Height());
        else
            return tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx.nLockTime));
    }
    else
    {
        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < 0)
            return tr("conflicted with a transaction with %1 confirmations").arg(-nDepth);
        else if (GetTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
            return tr("%1/offline").arg(nDepth);
        else if (nDepth == 0)
            return tr("0/unconfirmed, %1").arg((wtx.InMempool() ? tr("in memory pool") : tr("not in memory pool"))) + (wtx.isAbandoned() ? ", "+tr("abandoned") : "");
        else if (nDepth < 6)
            return tr("%1/unconfirmed").arg(nDepth);
        else
            return tr("%1 confirmations").arg(nDepth);
    }
}

QString TransactionDesc::toHTML(CWallet *wallet, TransactionRecord *rec, int unit)
{
    QString strHTML;
    QString sendHTML;
    QString sendChangeHTML;
    QString recHTML;
    QString recChangeHTML;
    bool allChange = true;

    LOCK2(cs_main, wallet->cs_wallet);
    strHTML.reserve(4000);
    strHTML += "<html><font face='verdana, arial, helvetica, sans-serif'>";

    //Get the ArcTx
    uint256 txid = rec->hash;
    RpcArcTransaction arcTx;
    bool fIncludeWatchonly = true;

    if (wallet->mapWallet.count(txid)) {
        CWalletTx& wtx = wallet->mapWallet[txid];
        getRpcArcTx(wtx, arcTx, fIncludeWatchonly, false);
    } else {
        getRpcArcTx(txid, arcTx, fIncludeWatchonly, false);
    }

    if (arcTx.blockHash.IsNull() || mapBlockIndex[arcTx.blockHash] == nullptr) {
        strHTML += "</font></html>";
        return strHTML;
    }

    strHTML += "<b>" + tr("Date") + ":</b> " + (GUIUtil::dateTimeStr(arcTx.nTime)) + "<br>";
    strHTML += "<b>" + tr("Txid") + ":</b> " + GUIUtil::HtmlEscape(arcTx.txid.ToString()) + "<br>";
    if (arcTx.coinbase) {
        strHTML += "<b>" + tr("coinbase") + ":</b> True<br>";
    } else {
        strHTML += "<b>" + tr("coinbase") + ":</b> False<br>";
    }
    strHTML += "<b>" + tr("category") + ":</b> " + GUIUtil::HtmlEscape(arcTx.category) + "<br>";
    strHTML += "<b>" + tr("blockhash") + ":</b> " + GUIUtil::HtmlEscape(arcTx.blockHash.ToString()) + "<br>";
    strHTML += "<b>" + tr("blockindex") + ":</b> " + QString::number(arcTx.blockIndex) + "<br>";
    strHTML += "<b>" + tr("blocktime") + ":</b> " + GUIUtil::dateTimeStr(arcTx.nBlockTime)  + "<br>";
    strHTML += "<b>" + tr("rawconfirmations") + ":</b> " + QString::number(arcTx.rawconfirmations) + "<br>";
    strHTML += "<b>" + tr("confirmations") + ":</b> " + QString::number(arcTx.confirmations) + "<br>";
    strHTML += "<b>" + tr("expiryHeight") + ":</b> " + QString::number(arcTx.expiryHeight) + "<br>";
    strHTML += "<b>" + tr("fee") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, -(arcTx.transparentValue + arcTx.sproutValue + arcTx.saplingValue)) + "<br>";



    if (arcTx.spentFrom.size() > 0) {
        for (int i = 0; i < arcTx.vTSend.size(); i++) {
            QString tempHTML;
            bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vTSend[i].encodedAddress) != arcTx.spentFrom.end();
            tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Transparent Send")  + "<br>";
            tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vTSend[i].encodedAddress)  + "<br>";
            tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vTSend[i].amount)) + "<br>";
            if (change) {
                tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
                sendChangeHTML += tempHTML;
            } else {
                allChange = false;
                tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
                sendHTML += tempHTML;
            }
        }

        CAmount sproutValueReceived = 0;
        for (int i = 0; i < arcTx.vZcReceived.size(); i++) {
            QString tempHTML;
            bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZcReceived[i].encodedAddress) != arcTx.spentFrom.end();
            sproutValueReceived += arcTx.vZcReceived[i].amount;
            tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Sprout Send")  + "<br>";
            tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZcReceived[i].encodedAddress)  + "<br>";
            tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vZcReceived[i].amount)) + "<br>";
            if (change) {
                tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
                sendChangeHTML += tempHTML;
            } else {
                allChange = false;
                tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
                sendHTML += tempHTML;
            }
        }

        if (arcTx.sproutValue - arcTx.sproutValueSpent - sproutValueReceived != 0) {
            QString tempHTML;
            tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Sprout Send")  + "<br>";
            tempHTML += "<b>" + tr("    Address") + ":</b> " + "Shielded"  + "<br>";
            tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.sproutValue - arcTx.sproutValueSpent)) + "<br>";
            tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
            sendHTML += tempHTML;
        }

        for (int i = 0; i < arcTx.vZsSend.size(); i++) {
            QString tempHTML;
            bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZsSend[i].encodedAddress) != arcTx.spentFrom.end();
            tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Sapling Send")  + "<br>";
            tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZsSend[i].encodedAddress)  + "<br>";
            tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vZsSend[i].amount)) + "<br>";
            if (change) {
                tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
            } else {
                allChange = false;
                tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
            }
            tempHTML += "<b>" + tr("    Memo") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZsSend[i].memoStr)  + "<br>";
            if (change) {
                sendChangeHTML += tempHTML;
            } else {
                sendHTML += tempHTML;
            }
        }
    }

    for (int i = 0; i < arcTx.vTReceived.size(); i++) {
        QString tempHTML;
        bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vTReceived[i].encodedAddress) != arcTx.spentFrom.end();
        tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Transparent Received")  + "<br>";
        tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vTReceived[i].encodedAddress)  + "<br>";
        tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vTReceived[i].amount)) + "<br>";
        if (change) {
            tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
            recChangeHTML += tempHTML;
        } else {
            allChange = false;
            tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
            recHTML += tempHTML;
        }
    }

    for (int i = 0; i < arcTx.vZcReceived.size(); i++) {
        QString tempHTML;
        bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZcReceived[i].encodedAddress) != arcTx.spentFrom.end();
        tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Sprout Received")  + "<br>";
        tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZcReceived[i].encodedAddress)  + "<br>";
        tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vZcReceived[i].amount)) + "<br>";
        if (change) {
            tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
            recChangeHTML += tempHTML;
        } else {
            allChange = false;
            tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
            recHTML += tempHTML;
        }
    }

    for (int i = 0; i < arcTx.vZsReceived.size(); i++) {
        QString tempHTML;
        bool change = arcTx.spentFrom.size() > 0 && arcTx.spentFrom.find(arcTx.vZsReceived[i].encodedAddress) != arcTx.spentFrom.end();
        tempHTML += "<br><b>" + tr("   Type") + ":</b> " + tr("Sapling Received")  + "<br>";
        tempHTML += "<b>" + tr("    Address") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZsReceived[i].encodedAddress)  + "<br>";
        tempHTML += "<b>" + tr("    Value") + ":</b> " + KomodoUnits::formatHtmlWithUnit(unit, CAmount(arcTx.vZsReceived[i].amount)) + "<br>";
        if (change) {
            tempHTML += "<b>" + tr("    Change") + ":</b> True<br>";
        } else {
            allChange = false;
            tempHTML += "<b>" + tr("    Change") + ":</b> False<br>";
        }
        tempHTML += "<b>" + tr("    Memo") + ":</b> " + GUIUtil::HtmlEscape(arcTx.vZsReceived[i].memoStr)  + "<br>";
        if (change) {
            recChangeHTML += tempHTML;
        } else {
            allChange = false;
            recHTML += tempHTML;
        }
    }

    if (allChange) {
        strHTML += sendHTML + sendChangeHTML + recHTML + recChangeHTML;
    } else {
        strHTML += sendHTML + recHTML;
    }


    strHTML += "</font></html>";
    return strHTML;
}
