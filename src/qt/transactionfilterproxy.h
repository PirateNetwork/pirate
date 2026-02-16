// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_TRANSACTIONFILTERPROXY_H
#define KOMODO_QT_TRANSACTIONFILTERPROXY_H

#include "amount.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <QSortFilterProxyModel>

/**
 * Filter proxy model for the transaction list.
 * 
 * ARCHITECTURE OVERVIEW:
 * This proxy sits between the TransactionTableModel (source) and the QTableView (display),
 * providing two-level filtering:
 * 
 * 1. Source Model Filtering (TransactionTableModel):
 *    - Filters decomposedTxCache (master cache with ALL transactions)
 *    - Produces cachedWallet (working cache with filtered subset)
 *    - Applied when filters change via triggerSourceRebuild()
 *    - Handles: date, type, address, amount, watch-only, inactive, limit
 * 
 * 2. Proxy Filtering (this class):
 *    - Additional display-level filtering on cachedWallet
 *    - Applied via filterAcceptsRow() on each view update
 *    - Handles: showParentsOnly, showAddressOnly
 * 
 * PERFORMANCE STRATEGY:
 * - Pre-filtering at source level reduces proxy filtering overhead
 * - With 50k+ transactions, filtering at proxy level would be too slow
 * - Source model caches filtered data, proxy just displays it
 * 
 * PARENT-CHILD RELATIONSHIPS:
 * - Transactions are decomposed into parent + children records
 * - Filters preserve complete transaction groups (parent + all children)
 * - Sorting maintains parent-before-children order
 */
class TransactionFilterProxy : public QSortFilterProxyModel
{
    Q_OBJECT

public:
    explicit TransactionFilterProxy(QObject *parent = 0);

    /** Earliest date that can be represented (far in the past) */
    static const QDateTime MIN_DATE;
    /** Last date that can be represented (far in the future) */
    static const QDateTime MAX_DATE;
    /** Type filter bit field representing all types */
    static const quint32 ALL_TYPES = 0xFFFFFFFF;

    /** Convert transaction type to bit flag for type filter */
    static quint32 TYPE(int type) { return 1<<type; }

    /** Watch-only address filtering options */
    enum WatchOnlyFilter
    {
        WatchOnlyFilter_All,  ///< Show all transactions
        WatchOnlyFilter_Yes,  ///< Show only watch-only transactions
        WatchOnlyFilter_No    ///< Hide watch-only transactions
    };

    // Source model filter setters (trigger rebuild of cachedWallet)
    
    /** Set date range filter (inclusive) */
    void setDateRange(const QDateTime &from, const QDateTime &to);
    
    /** Set address/label prefix filter (case-insensitive substring match) */
    void setAddressPrefix(const QString &addrPrefix);
    
    /** 
     * Set transaction type filter.
     * @param modes Bit field created with TYPE() or ALL_TYPES
     * Example: TYPE(TransactionRecord::SendToAddress) | TYPE(TransactionRecord::RecvWithAddress)
     */
    void setTypeFilter(quint32 modes);
    
    /** Set minimum absolute amount filter (in satoshis) */
    void setMinAmount(const CAmount& minimum);
    
    /** Set watch-only address filter */
    void setWatchOnlyFilter(WatchOnlyFilter filter);

    /** 
     * Set maximum number of parent transactions to display.
     * Children of displayed parents are always included.
     * @param limit Max parent transactions (-1 for unlimited)
     */
    void setLimit(int limit);

    /** Set whether to show conflicted (double-spend) transactions */
    void setShowInactive(bool showInactive);

    // Proxy-only filter setters (don't trigger source rebuild)
    
    /** Show only parent records, hiding decomposed children */
    void setShowParentsOnly(bool parentsOnly);

    /** 
     * Show only child records matching address filter.
     * When true: hides parent and non-matching children
     * When false: shows complete transaction if any child matches
     */
    void setShowAddressOnly(bool addressOnly);
    
    /**
     * Trigger source model to rebuild cachedWallet from decomposedTxCache.
     * Called when filters change to apply them at the source model level.
     * Public to allow TransactionView to trigger rebuilds directly.
     */
    void triggerSourceRebuild();

    /** Return number of rows in filtered view, respecting parent transaction limit */
    int rowCount(const QModelIndex &parent = QModelIndex()) const;

protected:
    /** Determine if a source model row should be shown in the filtered view */
    bool filterAcceptsRow(int source_row, const QModelIndex & source_parent) const;
    
    /** Compare two rows for sorting while preserving parent-child relationships */
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const;

private:
    // Filter state
    QDateTime dateFrom;            ///< Start of date range filter
    QDateTime dateTo;              ///< End of date range filter
    QString addrPrefix;            ///< Address/label prefix filter
    quint32 typeFilter;            ///< Transaction type bit field
    WatchOnlyFilter watchOnlyFilter; ///< Watch-only filter mode
    CAmount minAmount;             ///< Minimum amount filter (satoshis)
    int limitRows;                 ///< Max parent transactions (-1 = unlimited)
    bool showInactive;             ///< Show conflicted transactions
    bool showParentsOnly;          ///< Hide decomposed children
    bool showAddressOnly;          ///< Show only matching children
    
    // Address filter cache
    /** Transaction hashes that have at least one child matching address filter */
    mutable QSet<QString> parentsWithMatchingChildren;
    /** Cached address prefix to detect cache invalidation */
    mutable QString cachedAddrPrefix;
    
    /** 
     * Build cache of transactions with address-matching children.
     * Cache is invalidated when address prefix changes.
     */
    void updateParentChildMatchCache() const;
};

#endif // KOMODO_QT_TRANSACTIONFILTERPROXY_H
