// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionfilterproxy.h"

#include "transactiontablemodel.h"
#include "transactionrecord.h"

#include <cstdlib>

#include <QDateTime>
#include <QApplication>

// Earliest date that can be represented (far in the past)
const QDateTime TransactionFilterProxy::MIN_DATE = QDateTime::fromTime_t(0);
// Last date that can be represented (far in the future)
const QDateTime TransactionFilterProxy::MAX_DATE = QDateTime::fromTime_t(0xFFFFFFFF);

TransactionFilterProxy::TransactionFilterProxy(QObject *parent) :
    QSortFilterProxyModel(parent),
    dateFrom(MIN_DATE),
    dateTo(MAX_DATE),
    addrPrefix(),
    typeFilter(ALL_TYPES),
    watchOnlyFilter(WatchOnlyFilter_All),
    minAmount(0),
    limitRows(-1),
    showInactive(true),
    showParentsOnly(false),
    showAddressOnly(false)
{
}

/**
 * Determine if a row from the source model should be shown in the filtered view.
 * 
 * This filter operates at the proxy level on cachedWallet data that has already
 * been filtered at the source model level. It provides additional display-level
 * filtering such as showing only parent records or address-specific children.
 * 
 * Filter hierarchy:
 * 1. Source model filters decomposedTxCache -> cachedWallet (data-level filtering)
 * 2. This proxy filters cachedWallet -> display (presentation-level filtering)
 */
bool TransactionFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    // Extract record data for filtering
    int type = index.data(TransactionTableModel::TypeRole).toInt();
    int parentType = index.data(TransactionTableModel::ParentTypeRole).toInt();
    QDateTime datetime = index.data(TransactionTableModel::DateRole).toDateTime();
    bool involvesWatchAddress = index.data(TransactionTableModel::WatchonlyRole).toBool();
    QString address = index.data(TransactionTableModel::AddressRole).toString();
    QString label = index.data(TransactionTableModel::LabelRole).toString();
    qint64 amount = llabs(index.data(TransactionTableModel::AmountRole).toLongLong());
    int status = index.data(TransactionTableModel::StatusRole).toInt();
    bool isParent = index.data(TransactionTableModel::IsParentRole).toBool();

    // Display-level filters (not applied at source model level)
    
    // Show only parent records (hide decomposed children)
    if (showParentsOnly && !isParent)
        return false;
    
    // Hide conflicted transactions when inactive filtering is disabled
    if (!showInactive && status == TransactionStatus::Conflicted)
        return false;
    
    // Type filter: Use parent type so children inherit parent's filter status
    // This ensures entire transactions are shown/hidden together
    if (!(TYPE(parentType) & typeFilter))
        return false;
    
    // Watch-only filter
    if (involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_No)
        return false;
    if (!involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_Yes)
        return false;
    
    // Date range filter
    if (datetime < dateFrom || datetime > dateTo)
        return false;
    
    // Address filter with parent-child awareness
    // Two modes: show entire transaction if any child matches, or show only matching children
    if (!addrPrefix.isEmpty()) {
        bool addressMatches = address.contains(addrPrefix, Qt::CaseInsensitive) || 
                             label.contains(addrPrefix, Qt::CaseInsensitive);
        
        // Build cache of transactions with matching children
        updateParentChildMatchCache();
        QString txHash = index.data(TransactionTableModel::TxHashRole).toString();
        bool transactionHasMatchingChild = parentsWithMatchingChildren.contains(txHash);
        
        if (showAddressOnly) {
            // Mode 1: Show only records that actually match the address (hide parents and non-matching children)
            if (!addressMatches)
                return false;
        } else {
            // Mode 2: Show complete transaction (parent + ALL children) if any child matches
            if (!addressMatches && !transactionHasMatchingChild) {
                return false; // Not part of a matching transaction
            }
        }
    }
    
    // Minimum amount filter
    if (amount < minAmount)
        return false;

    return true;
}

/**
 * Update date range filter and trigger source model rebuild.
 * 
 * Architecture: Filter changes trigger the source model to rebuild cachedWallet
 * from decomposedTxCache with the new filters applied. This ensures the proxy
 * works with a pre-filtered dataset, improving performance for large wallets.
 */
void TransactionFilterProxy::setDateRange(const QDateTime &from, const QDateTime &to)
{
    this->dateFrom = from;
    this->dateTo = to;
    triggerSourceRebuild();
}

/**
 * Update address prefix filter and trigger source model rebuild.
 * Clear the parent-child match cache since it depends on the address prefix.
 */
void TransactionFilterProxy::setAddressPrefix(const QString &_addrPrefix)
{
    this->addrPrefix = _addrPrefix;
    parentsWithMatchingChildren.clear(); // Invalidate cache
    triggerSourceRebuild();
}

/**
 * Update transaction type filter and trigger source model rebuild.
 * Types are combined as a bit field (e.g., TYPE(Send) | TYPE(Receive)).
 */
void TransactionFilterProxy::setTypeFilter(quint32 modes)
{
    this->typeFilter = modes;
    triggerSourceRebuild();
}

/**
 * Update minimum amount filter and trigger source model rebuild.
 */
void TransactionFilterProxy::setMinAmount(const CAmount& minimum)
{
    this->minAmount = minimum;
    triggerSourceRebuild();
}

/**
 * Update watch-only filter and trigger source model rebuild.
 */
void TransactionFilterProxy::setWatchOnlyFilter(WatchOnlyFilter filter)
{
    this->watchOnlyFilter = filter;
    triggerSourceRebuild();
}

/**
 * Update row limit and trigger source model rebuild.
 * Limit is applied to parent transactions only (children don't count toward limit).
 */
void TransactionFilterProxy::setLimit(int limit)
{
    this->limitRows = limit;
    triggerSourceRebuild();
}

/**
 * Update inactive (conflicted) transaction visibility and trigger source model rebuild.
 */
void TransactionFilterProxy::setShowInactive(bool _showInactive)
{
    this->showInactive = _showInactive;
    triggerSourceRebuild();
}

/**
 * Trigger the source model to rebuild cachedWallet from decomposedTxCache.
 * 
 * This is the core of the filter architecture:
 * - decomposedTxCache: Master cache containing ALL transactions (never filtered)
 * - cachedWallet: Working cache filtered by current filter settings
 * - Proxy: Additional presentation-level filtering on cachedWallet
 * 
 * By pre-filtering at the source model level, we avoid the performance cost
 * of the proxy filtering 50k+ records on every view update.
 */
void TransactionFilterProxy::triggerSourceRebuild()
{
    TransactionTableModel* sourceTableModel = qobject_cast<TransactionTableModel*>(sourceModel());
    if (!sourceTableModel)
        return;
    
    // Request source model to rebuild cachedWallet from decomposedTxCache
    // with current filter settings applied
    sourceTableModel->requestFilteredRebuild(
        dateFrom, dateTo, typeFilter, watchOnlyFilter,
        addrPrefix, minAmount, showInactive, limitRows
    );
}

/**
 * Show only parent records (hide decomposed children).
 * Triggers source model rebuild to apply filter.
 */
void TransactionFilterProxy::setShowParentsOnly(bool _showParentsOnly)
{
    this->showParentsOnly = _showParentsOnly;
    triggerSourceRebuild();
}

/**
 * Show only child records matching address (hide parent and non-matching children).
 * Triggers source model rebuild to apply filter.
 */
void TransactionFilterProxy::setShowAddressOnly(bool _showAddressOnly)
{
    this->showAddressOnly = _showAddressOnly;
    triggerSourceRebuild();
}

/**
 * Build cache of transaction hashes that have children matching the address filter.
 * 
 * This cache enables efficient "show full transaction if any child matches" filtering
 * without scanning all records for each filterAcceptsRow call. Cache is invalidated
 * when address prefix changes.
 * 
 * Performance: Single-pass scan through source model, built on demand.
 */
void TransactionFilterProxy::updateParentChildMatchCache() const
{
    // Cache is valid if prefix hasn't changed and cache exists
    if (cachedAddrPrefix == addrPrefix && !parentsWithMatchingChildren.isEmpty())
        return;
    
    // Rebuild cache for new prefix
    parentsWithMatchingChildren.clear();
    cachedAddrPrefix = addrPrefix;
    
    if (addrPrefix.isEmpty())
        return;
    
    // Single pass: find all child records matching address and store their transaction hash
    int rowCount = sourceModel()->rowCount();
    for (int i = 0; i < rowCount; i++) {
        QModelIndex index = sourceModel()->index(i, 0);
        bool isChild = index.data(TransactionTableModel::IsChildRole).toBool();
        
        if (isChild) {
            QString address = index.data(TransactionTableModel::AddressRole).toString();
            QString label = index.data(TransactionTableModel::LabelRole).toString();
            
            if (address.contains(addrPrefix, Qt::CaseInsensitive) || 
                label.contains(addrPrefix, Qt::CaseInsensitive)) {
                QString txHash = index.data(TransactionTableModel::TxHashRole).toString();
                parentsWithMatchingChildren.insert(txHash);
            }
        }
        
        // Keep UI responsive during large scans
        if (i % 200 == 0 && i > 0) {
            QApplication::processEvents();
        }
    }
}

/**
 * Return number of rows in the filtered view, respecting the parent transaction limit.
 * 
 * Limit is applied to parent transactions only - if limit is 50, show 50 parent
 * transactions plus all their children. This provides consistent behavior where
 * users always see complete transactions.
 */
int TransactionFilterProxy::rowCount(const QModelIndex &parent) const
{
    if (limitRows != -1) {
        int totalRows = QSortFilterProxyModel::rowCount(parent);
        int parentCount = 0;
        int rowsToShow = 0;
        
        // Count parents until we reach limit, including all their children
        for (int i = 0; i < totalRows; i++) {
            QModelIndex idx = index(i, 0, parent);
            bool isParent = idx.data(TransactionTableModel::IsParentRole).toBool();
            
            if (isParent) {
                parentCount++;
                if (parentCount > limitRows)
                    break; // Reached limit
            }
            rowsToShow++;
        }
        
        return rowsToShow;
    }
    
    return QSortFilterProxyModel::rowCount(parent);
}

/**
 * Compare two rows for sorting, preserving parent-child relationships.
 * 
 * Critical behavior: Records from the same transaction MUST stay in source model order
 * (parent first, then children). Returning false for same-transaction records tells Qt
 * they are "equal" and prevents reordering.
 * 
 * For different transactions, compare their parent records using the sort column.
 */
bool TransactionFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    QString leftHash = sourceModel()->data(left, TransactionTableModel::TxHashRole).toString();
    QString rightHash = sourceModel()->data(right, TransactionTableModel::TxHashRole).toString();
    
    // Same transaction: preserve source order (parent before children)
    // Return false so Qt considers them equal and doesn't reorder
    if (leftHash == rightHash)
        return false;
    
    // Different transactions: find their parent records and compare those
    QModelIndex leftParent = left;
    QModelIndex rightParent = right;
    
    bool leftIsParent = sourceModel()->data(left, TransactionTableModel::IsParentRole).toBool();
    bool rightIsParent = sourceModel()->data(right, TransactionTableModel::IsParentRole).toBool();
    
    // Find parent of left record if it's a child
    if (!leftIsParent) {
        int searchRow = left.row() - 1;
        while (searchRow >= 0) {
            QModelIndex candidate = sourceModel()->index(searchRow, left.column());
            QString candidateHash = sourceModel()->data(candidate, TransactionTableModel::TxHashRole).toString();
            
            // Crossed into different transaction - stop searching
            if (candidateHash != leftHash)
                break;
            
            bool candidateIsParent = sourceModel()->data(candidate, TransactionTableModel::IsParentRole).toBool();
            if (candidateIsParent) {
                leftParent = candidate;
                break;
            }
            searchRow--;
        }
    }
    
    // Find parent of right record if it's a child
    if (!rightIsParent) {
        int searchRow = right.row() - 1;
        while (searchRow >= 0) {
            QModelIndex candidate = sourceModel()->index(searchRow, right.column());
            QString candidateHash = sourceModel()->data(candidate, TransactionTableModel::TxHashRole).toString();
            
            // Crossed into different transaction - stop searching
            if (candidateHash != rightHash)
                break;
            
            bool candidateIsParent = sourceModel()->data(candidate, TransactionTableModel::IsParentRole).toBool();
            if (candidateIsParent) {
                rightParent = candidate;
                break;
            }
            searchRow--;
        }
    }
    
    // Compare parent records using base class sorting logic
    if (leftParent.isValid() && rightParent.isValid())
        return QSortFilterProxyModel::lessThan(leftParent, rightParent);
    
    // Fallback: maintain source order
    return left.row() < right.row();
}
