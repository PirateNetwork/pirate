// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletmodeltransaction.h"

#include "policy/policy.h"
#include "wallet/wallet.h"

WalletModelZTransaction::WalletModelZTransaction(const QString &_fromaddress, const QList<SendCoinsRecipient> &_recipients, const CAmount& _fee, const bool &_bIsMine) :
    fromaddress(_fromaddress),
    recipients(_recipients),
    fee(_fee),
    bIsMine(_bIsMine)
{
  //Initialise private variables:
  sZSignOfflineTransaction="";
}

WalletModelZTransaction::~WalletModelZTransaction()
{
}

QString WalletModelZTransaction::getFromAddress() const
{
    return fromaddress;
}

bool WalletModelZTransaction::getIsMine() const
{
    return bIsMine;
}


QList<SendCoinsRecipient> WalletModelZTransaction::getRecipients() const
{
    return recipients;
}

CAmount WalletModelZTransaction::getTransactionFee() const
{
    return fee;
}

void WalletModelZTransaction::setTransactionFee(const CAmount& newFee)
{
    fee = newFee;
}

boost::optional<TransactionBuilder> WalletModelZTransaction::getBuilder() const
{
    return builder;
}

void WalletModelZTransaction::setBuilder(const boost::optional<TransactionBuilder>& newBuilder)
{
    builder = newBuilder;
}

CMutableTransaction WalletModelZTransaction::getContextualTx() const
{
    return contextualTx;
}

void WalletModelZTransaction::setContextualTx(const CMutableTransaction& newContextualTx)
{
    contextualTx = newContextualTx;
}

std::vector<SendManyRecipient> WalletModelZTransaction::getTaddrRecipients() const
{
    return taddrRecipients;
}

void WalletModelZTransaction::setTaddrRecipients(const std::vector<SendManyRecipient>& newTaddrRecipients)
{
    taddrRecipients = newTaddrRecipients;
}

std::vector<SendManyRecipient> WalletModelZTransaction::getZaddrRecipients() const
{
    return zaddrRecipients;
}

void WalletModelZTransaction::setZaddrRecipients(const std::vector<SendManyRecipient>& newZaddrRecipients)
{
    zaddrRecipients = newZaddrRecipients;
}

UniValue WalletModelZTransaction::getContextInfo() const
{
    return contextInfo;
}

void WalletModelZTransaction::setContextInfo(const UniValue& newContextInfo)
{
    contextInfo = newContextInfo;
}

CAmount WalletModelZTransaction::getTotalTransactionAmount() const
{
    CAmount totalTransactionAmount = 0;
    for (const SendCoinsRecipient &rcp : recipients)
    {
        totalTransactionAmount += rcp.amount;
    }
    return totalTransactionAmount;
}

void WalletModelZTransaction::setOperationId(const AsyncRPCOperationId& newOperationId)
{
    operationId = newOperationId;
}

AsyncRPCOperationId WalletModelZTransaction::getOperationId() const
{
    return operationId;
}

void WalletModelZTransaction::setZSignOfflineTransaction(const string& sNewTransaction)
{
    sZSignOfflineTransaction = sNewTransaction;
}

string WalletModelZTransaction::getZSignOfflineTransaction() const
{
    return sZSignOfflineTransaction;
}
