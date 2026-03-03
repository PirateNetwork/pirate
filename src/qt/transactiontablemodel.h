// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
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

/**
 * Qt model for the transaction table of a wallet.
 * 
 * THREE-TIER CACHING ARCHITECTURE:
 * 1. decomposedTxCache (Master): ALL transactions, indexed for fast filtering
 * 2. cachedWallet (Working): Filtered subset for display
 * 3. TransactionFilterProxy: Additional display-level filtering
 * 
 * LAZY LOADING:
 * - Initial startup: Load only 50 transactions for quick display
 * - Background: Load remaining in batches of 10 every 100ms via timer
 * - After completion: Build indexes and populate cachedWallet
 * 
 * PARENT-CHILD HIERARCHY:
 * Transactions decomposed into:
 * - Parent: Summary with overall effect (net balance change)
 * - Children: Individual inputs, outputs, and fee
 * This enables compact view (parents only) with expandable detail.
 * 
 * FILTERING PERFORMANCE:
 * Uses five indexes (date, type, address, watchOnly, amount) for O(1) lookups.
 * Filter changes rebuild cachedWallet from decomposedTxCache via index queries,
 * avoiding O(N) scans of entire cache.
 * 
 * SIGNALS:
 * - lazyLoadProgress: Batch completion updates during background loading
 * - lazyLoadComplete: All transactions loaded and indexed
 */
class TransactionTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    /**
     * Initial transaction load limit for quick startup.
     * 
     * Only first 50 transactions loaded during initialization for fast display.
     * Remaining transactions loaded via lazy load timer in background batches.
     * Balances startup speed (< 100ms) vs immediate data availability.
     */
    static const int INITIAL_TX_LIMIT = 50;
    
    explicit TransactionTableModel(const PlatformStyle *platformStyle, CWallet* wallet, WalletModel *parent = 0);
    ~TransactionTableModel();

    /** Table column indices */
    enum ColumnIndex {
        Status = 0,     ///< Transaction status icon
        Watchonly = 1,  ///< Watch-only indicator icon
        Date = 2,       ///< Transaction date/time
        Type = 3,       ///< Transaction type (send/receive/etc.)
        ToAddress = 4,  ///< Destination or source address
        Amount = 5      ///< Amount (right-aligned)
    };

    /**
     * Data roles for querying specific transaction information.
     * 
     * Roles are independent of column and allow retrieving any transaction
     * property regardless of which column is being queried. Used by views,
     * filters, and delegates to access transaction data.
     */
    enum RoleIndex {
        TypeRole = Qt::UserRole,          ///< Transaction type (TransactionRecord::Type)
        DateRole,                         ///< Unix timestamp as qint64
        WatchonlyRole,                    ///< Boolean: involves watch-only address
        WatchonlyDecorationRole,          ///< Watch-only icon (QIcon)
        LongDescriptionRole,              ///< HTML formatted transaction details
        LongDescriptionNoDisclosureRole,  ///< HTML formatted transaction details without payment disclosure
        MemoDescriptionRole,              ///< Zcash memo string (decoded)
        AddressRole,                      ///< Transaction address (QString)
        LabelRole,                        ///< Address label from address book
        AmountRole,                       ///< Net amount (CAmount as qint64)
        TxIDRole,                         ///< Transaction ID (QString hex)
        TxHashRole,                       ///< Transaction hash (uint256)
        TxHexRole,                        ///< Raw transaction hex encoding
        TxPlainTextRole,                  ///< Plain text transaction summary
        ConfirmedRole,                    ///< Boolean: has 6+ confirmations
        FormattedAmountRole,              ///< Formatted amount without brackets
        StatusRole,                       ///< Transaction status enum value
        RawDecorationRole,                ///< Unprocessed status icon
        IsParentRole,                     ///< Boolean: this is parent record
        IsChildRole,                      ///< Boolean: this is child record
        IdxRole,                          ///< Child index within parent group
        ParentTypeRole                    ///< Parent's transaction type (for filtering)
    };

    /** Refresh entire transaction list from wallet */
    void refreshWallet();
    
    // QAbstractTableModel interface implementation
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex & parent = QModelIndex()) const;
    
    /** Check if transaction queue is being processed */
    bool processingQueuedTransactions() const { return fProcessingQueuedTransactions; }
    
    /**
     * Toggle expand/collapse state for a parent transaction.
     * Shows or hides child records (inputs, outputs, fee).
     */
    void toggleTransactionExpanded(const QModelIndex &index);
    
    /**
     * Expand or collapse all parent transactions in view.
     * @param expanded true to show all children, false to hide all
     */
    void setAllTransactionsExpanded(bool expanded);
    
    /**
     * Rebuild cachedWallet from decomposedTxCache without filtering.
     * Copies all cached transactions to working cache for display.
     */
    void rebuildFromCache();
    
    /**
     * Rebuild cachedWallet with filtering applied.
     * 
     * Uses search indexes to efficiently find matching transactions in
     * decomposedTxCache and copies complete transaction groups (parent +
     * all children) to cachedWallet for display.
     * 
     * @param dateFrom Start date filter (inclusive)
     * @param dateTo End date filter (inclusive)
     * @param typeFilter Transaction type bit field
     * @param watchOnlyFilter Watch-only filter mode (0=all, 1=yes, 2=no)
     * @param addrPrefix Address prefix filter (case-insensitive)
     * @param minAmount Minimum absolute amount filter
     * @param showInactive Include conflicted/abandoned transactions
     * @param limitParents Maximum parent transactions (-1=unlimited)
     */
    void rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                          quint32 typeFilter, int watchOnlyFilter, 
                          const QString &addrPrefix, qint64 minAmount, 
                          bool showInactive, int limitParents = -1);
    
    /**
     * Request filtered rebuild (called by filter proxy).
     * Delegates to rebuildFromCache with same parameters.
     */
    void requestFilteredRebuild(const QDateTime &dateFrom, const QDateTime &dateTo,
                                quint32 typeFilter, int watchOnlyFilter,
                                const QString &addrPrefix, qint64 minAmount,
                                bool showInactive, int limitParents = -1);
    
    /** @name Fast index-based queries for filtering
     * These methods use search indexes for O(1) lookups.
     * @{
     */
    QList<int> findTransactionsByDateRange(qint64 from, qint64 to) const;
    QList<int> findTransactionsByType(int type) const;
    QList<int> findTransactionsByAddress(const QString& address) const;
    QList<int> findWatchOnlyTransactions() const;
    QList<int> findTransactionsByMinAmount(qint64 minAmount) const;
    /**@}*/
    
    /** @name Transaction statistics
     * @{
     */
    int getTotalTransactionCount() const;            ///< Total decomposed records
    QMap<int, int> getTypeDistribution() const;      ///< Transaction type counts
    /**@}*/

private:
    CWallet* wallet;                        ///< Core wallet pointer
    WalletModel *walletModel;               ///< Parent wallet model
    QStringList columns;                    ///< Column header strings
    TransactionTablePriv *priv;             ///< Private implementation (caching, indexing, lazy load)
    bool fProcessingQueuedTransactions;     ///< True during batch transaction processing
    bool fPendingRefresh;                   ///< True when full refresh is queued
    const PlatformStyle *platformStyle;     ///< Platform-specific styling (icons, colors)

    /** Core signal subscription for wallet notifications */
    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();

    /** @name Data formatting helpers
     * Convert raw transaction data to display-friendly strings and icons.
     * @{
     */
    QString lookupAddress(const std::string &address, bool tooltip) const;
    QVariant addressColor(const TransactionRecord *wtx) const;
    QString formatTxStatus(const TransactionRecord *wtx) const;
    QString formatTxDate(const TransactionRecord *wtx) const;
    QString formatTxType(const TransactionRecord *wtx) const;
    QString formatTxToAddress(const TransactionRecord *wtx, bool tooltip) const;
    QString formatTxAmount(const TransactionRecord *wtx, bool showUnconfirmed=true, 
                          KomodoUnits::SeparatorStyle separators=KomodoUnits::separatorStandard) const;
    QString formatTooltip(const TransactionRecord *rec) const;
    QVariant txStatusDecoration(const TransactionRecord *wtx) const;
    QVariant txWatchonlyDecoration(const TransactionRecord *wtx) const;
    QVariant txAddressDecoration(const TransactionRecord *wtx) const;
    /**@}*/

public Q_SLOTS:
    /**
     * Handle new or status-changed transaction.
     * @param hash Transaction ID
     * @param status Update trigger status code
     * @param showTransaction Whether transaction should be visible
     */
    void updateTransaction(const QString &hash, int status, bool showTransaction);
    
    /** Update confirmation counts for all transactions (called on new block) */
    void updateConfirmations();
    
    /** Refresh amount formatting when display unit changes */
    void updateDisplayUnit();
    
    /** Update "Amount (ARRR)" column header when display unit changes */
    void updateAmountColumnTitle();
    
    /** Set transaction processing flag (used via QueuedConnection for thread safety) */
    void setProcessingQueuedTransactions(bool value);
    
    /** Reset all caches and restart lazy loading (called after rescan) */
    void resetAndRestartLazyLoad();

Q_SIGNALS:
    /** Emitted when lazy loading of all transactions completes */
    void lazyLoadComplete();
    
    /** Emitted during lazy loading to report progress */
    void lazyLoadProgress(int loaded, int total);

    friend class TransactionTablePriv;
};

#endif // KOMODO_QT_TRANSACTIONTABLEMODEL_H
