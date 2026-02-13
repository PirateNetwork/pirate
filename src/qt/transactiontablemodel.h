// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_TRANSACTIONTABLEMODEL_H
#define KOMODO_QT_TRANSACTIONTABLEMODEL_H

#include "komodounits.h"

#include <QAbstractTableModel>
#include <QStringList>

class PlatformStyle;
class TransactionRecord;
class TransactionTablePriv;
class WalletModel;

class CWallet;

/** UI model for the transaction table of a wallet.
 */
class TransactionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    // Initial transaction load limit for quick startup (remaining loaded via lazy load)
    static const int INITIAL_TX_LIMIT = 50;
    
    explicit TransactionTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);
    ~TransactionTableModel();

    enum ColumnIndex {
        Status = 0,
        Watchonly = 1,
        Date = 2,
        Type = 3,
        ToAddress = 4,
        Amount = 5
    };

    /** Roles to get specific information from a transaction row.
        These are independent of column.
    */
    enum RoleIndex {
        /** Type of transaction */
        TypeRole = Qt::UserRole,
        /** Date and time this transaction was created */
        DateRole,
        /** Watch-only boolean */
        WatchonlyRole,
        /** Watch-only icon */
        WatchonlyDecorationRole,
        /** Long description (HTML format) */
        LongDescriptionRole,
        /** Memo str */
        MemoDescriptionRole,
        /** Address of transaction */
        AddressRole,
        /** Label of address related to transaction */
        LabelRole,
        /** Net amount of transaction */
        AmountRole,
        /** Unique identifier */
        TxIDRole,
        /** Transaction hash */
        TxHashRole,
        /** Transaction data, hex-encoded */
        TxHexRole,
        /** Whole transaction as plain text */
        TxPlainTextRole,
        /** Is transaction confirmed? */
        ConfirmedRole,
        /** Formatted amount, without brackets when unconfirmed */
        FormattedAmountRole,
        /** Transaction status (TransactionRecord::Status) */
        StatusRole,
        /** Unprocessed icon */
        RawDecorationRole,
        /** Is this a parent record? */
        IsParentRole,
        /** Is this a child record? */
        IsChildRole,
        /** Index within transaction group (parent=0, children=1,2,3...) */
        IdxRole,
        /** Parent transaction type (for filtering children by parent type) */
        ParentTypeRole,
    };

    void refreshWallet();
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    bool processingQueuedTransactions() const { return fProcessingQueuedTransactions; }
    
    /** Toggle expand/collapse state for a transaction's children */
    void toggleTransactionExpanded(const QModelIndex &index);
    
    /** Expand or collapse all parent transactions */
    void setAllTransactionsExpanded(bool expanded);
    
    /** Rebuild cachedWallet from decomposedTxCache (loads all cached transactions for filtering) */
    void rebuildFromCache();
    void rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                          quint32 typeFilter, int watchOnlyFilter, 
                          const QString &addrPrefix, qint64 minAmount, 
                          bool showInactive, int limitParents = -1);
    
    // Fast index-based queries for filtering
    QList<int> findTransactionsByDateRange(qint64 from, qint64 to) const;
    QList<int> findTransactionsByType(int type) const;
    QList<int> findTransactionsByAddress(const QString& address) const;
    QList<int> findWatchOnlyTransactions() const;
    QList<int> findTransactionsByMinAmount(qint64 minAmount) const;
    
    // Statistics
    int getTotalTransactionCount() const;
    QMap<int, int> getTypeDistribution() const;

private:
    CWallet* wallet;
    WalletModel *walletModel;
    QStringList columns;
    TransactionTablePriv *priv;
    bool fProcessingQueuedTransactions;
    bool fPendingRefresh;
    const PlatformStyle *platformStyle;

    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    QString lookupAddress(const std::string &address, bool tooltip) const;
    QVariant addressColor(const TransactionRecord *wtx) const;
    QString formatTxStatus(const TransactionRecord *wtx) const;
    QString formatTxDate(const TransactionRecord *wtx) const;
    QString formatTxType(const TransactionRecord *wtx) const;
    QString formatTxToAddress(const TransactionRecord *wtx, bool tooltip) const;
    QString formatTxAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, KomodoUnits::SeparatorStyle separators=KomodoUnits::separatorStandard) const;
    QString formatTooltip(const TransactionRecord *rec) const;
    QVariant txStatusDecoration(const TransactionRecord *wtx) const;
    QVariant txWatchonlyDecoration(const TransactionRecord *wtx) const;
    QVariant txAddressDecoration(const TransactionRecord *wtx) const;

public Q_SLOTS:
    /* New transaction, or transaction changed status */
    void updateTransaction(const QString &hash, int status, bool showTransaction);
    void updateConfirmations();
    void updateDisplayUnit();
    /** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
    void updateAmountColumnTitle();
    /* Needed to update fProcessingQueuedTransactions through a QueuedConnection */
    void setProcessingQueuedTransactions(bool value);

Q_SIGNALS:
    void lazyLoadComplete();
    void lazyLoadProgress(int loaded, int total);

    friend class TransactionTablePriv;
};

#endif // KOMODO_QT_TRANSACTIONTABLEMODEL_H
