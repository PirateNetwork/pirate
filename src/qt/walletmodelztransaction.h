// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_WALLETMODELZTRANSACTION_H
#define KOMODO_QT_WALLETMODELZTRANSACTION_H

#include "walletmodel.h"
#include "transaction_builder.h"
#include "wallet/asyncrpcoperation_sendmany.h"

#include <QObject>

class SendCoinsRecipient;

class CWallet;

/** Data model for a walletmodel transaction. */
class WalletModelZTransaction
{
public:
    explicit WalletModelZTransaction(const QString &fromaddress, const QList<SendCoinsRecipient> &recipients, const CAmount& fee);
    ~WalletModelZTransaction();

    QString getFromAddress() const;
    QList<SendCoinsRecipient> getRecipients() const;

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;

    void setBuilder(const boost::optional<TransactionBuilder>& newBuilder);
    boost::optional<TransactionBuilder> getBuilder() const;

    void setContextualTx(const CMutableTransaction& newContextualTx);
    CMutableTransaction getContextualTx() const;

    void setTaddrRecipients(const std::vector<SendManyRecipient>& newTaddrRecipients);
    std::vector<SendManyRecipient> getTaddrRecipients() const;

    void setZaddrRecipients(const std::vector<SendManyRecipient>& newZaddrRecipients);
    std::vector<SendManyRecipient> getZaddrRecipients() const;

    void setContextInfo(const UniValue& newContextInfo);
    UniValue getContextInfo() const;

    CAmount getTotalTransactionAmount() const;

    void setOperationId(const AsyncRPCOperationId& newOperationId);
    AsyncRPCOperationId getOperationId() const;

private:
    QString fromaddress;
    QList<SendCoinsRecipient> recipients;
    CAmount fee;
    boost::optional<TransactionBuilder> builder;
    CMutableTransaction contextualTx;
    std::vector<SendManyRecipient> taddrRecipients;
    std::vector<SendManyRecipient> zaddrRecipients;
    UniValue contextInfo;
    AsyncRPCOperationId operationId;
};

#endif // KOMODO_QT_WALLETMODELZTRANSACTION_H
