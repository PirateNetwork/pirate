// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionfilterproxy.h"

#include "transactiontablemodel.h"
#include "transactionrecord.h"
#include "util.h"

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

bool TransactionFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    int type = index.data(TransactionTableModel::TypeRole).toInt();
    int parentType = index.data(TransactionTableModel::ParentTypeRole).toInt();
    QDateTime datetime = index.data(TransactionTableModel::DateRole).toDateTime();
    bool involvesWatchAddress = index.data(TransactionTableModel::WatchonlyRole).toBool();
    QString address = index.data(TransactionTableModel::AddressRole).toString();
    QString label = index.data(TransactionTableModel::LabelRole).toString();
    qint64 amount = llabs(index.data(TransactionTableModel::AmountRole).toLongLong());
    int status = index.data(TransactionTableModel::StatusRole).toInt();
    bool isParent = index.data(TransactionTableModel::IsParentRole).toBool();

    if(showParentsOnly && !isParent)
        return false;
    if(!showInactive && status == TransactionStatus::Conflicted)
        return false;
    // Use parent type for filtering so children are included when parent matches
    if(!(TYPE(parentType) & typeFilter))
        return false;
    if (involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_No)
        return false;
    if (!involvesWatchAddress && watchOnlyFilter == WatchOnlyFilter_Yes)
        return false;
    if(datetime < dateFrom || datetime > dateTo)
        return false;
    
    // Handle address filtering with parent-child awareness
    if (!addrPrefix.isEmpty()) {
        bool addressMatches = address.contains(addrPrefix, Qt::CaseInsensitive) || label.contains(addrPrefix, Qt::CaseInsensitive);
        
        if (showAddressOnly) {
            // Show only child records that match the address
            if (isParent || !addressMatches)
                return false;
        } else {
            // Show full transaction (parent + ALL children) if any child matches
            if (!addressMatches) {
                // Record doesn't match - check if it belongs to a matching transaction
                // Update cache once (it will return early if already valid)
                updateParentChildMatchCache();
                
                QString txHash = index.data(TransactionTableModel::TxHashRole).toString();
                if (!parentsWithMatchingChildren.contains(txHash))
                    return false;
                // This record belongs to a transaction with matching children - show it
            }
            // Record matches or belongs to matching transaction - show it
        }
    }
    
    if(amount < minAmount)
        return false;

    return true;
}

void TransactionFilterProxy::setDateRange(const QDateTime &from, const QDateTime &to)
{
    this->dateFrom = from;
    this->dateTo = to;
    
    // Rebuild source model from cache with current filters
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm) {
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitRows);
    }
    
    invalidateFilter();
    invalidate(); // Force complete re-evaluation
}

void TransactionFilterProxy::setAddressPrefix(const QString &_addrPrefix)
{
    this->addrPrefix = _addrPrefix;
    // Only clear cache, keep cachedAddrPrefix so updateParentChildMatchCache can detect change
    parentsWithMatchingChildren.clear();
    
    // Rebuild source model from cache with current filters
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm) {
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitRows);
    }
    
    invalidateFilter();
}

void TransactionFilterProxy::setTypeFilter(quint32 modes)
{
    this->typeFilter = modes;
    
    // Rebuild source model from cache with current filters
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm) {
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitRows);
    }
    
    invalidateFilter();
    invalidate(); // Force complete re-evaluation
}

void TransactionFilterProxy::setMinAmount(const CAmount& minimum)
{
    this->minAmount = minimum;
    
    // Rebuild source model from cache with current filters
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm) {
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitRows);
    }
    
    invalidateFilter();
}

void TransactionFilterProxy::setWatchOnlyFilter(WatchOnlyFilter filter)
{
    this->watchOnlyFilter = filter;
    
    // Rebuild source model from cache with current filters
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm) {
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitRows);
    }
    
    invalidateFilter();
    invalidate(); // Force complete re-evaluation
}

void TransactionFilterProxy::setLimit(int limit)
{
    LogPrintf("TransactionFilterProxy::setLimit called with limit=%d (current limitRows=%d)\n", limit, this->limitRows);
    this->limitRows = limit;
    
    // If increasing limit, rebuild from cache to load more transactions
    TransactionTableModel* ttm = qobject_cast<TransactionTableModel*>(sourceModel());
    if (ttm && limit > 50) { // 50 is INITIAL_TX_LIMIT
        // Always rebuild with filters when requesting more than initial load
        LogPrintf("Calling rebuildFromCache because limit > 50\n");
        ttm->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limit);
    }
    
    invalidateFilter();
    invalidate(); // Force complete re-evaluation
}

void TransactionFilterProxy::setShowInactive(bool _showInactive)
{
    this->showInactive = _showInactive;
    
    // Don't call rebuildFromCache here - it modifies the shared source model
    // Just invalidate the filter to re-apply filtering on the proxy level
    invalidateFilter();
}

void TransactionFilterProxy::setShowParentsOnly(bool _showParentsOnly)
{
    this->showParentsOnly = _showParentsOnly;
    // Don't rebuild from cache - this is just a display filter
    invalidateFilter();
}

void TransactionFilterProxy::setShowAddressOnly(bool _showAddressOnly)
{
    this->showAddressOnly = _showAddressOnly;
    // Don't rebuild from cache - this is just a display filter
    invalidateFilter();
}

void TransactionFilterProxy::updateParentChildMatchCache() const
{
    // Check if cache is still valid for current prefix
    if (cachedAddrPrefix == addrPrefix && !parentsWithMatchingChildren.isEmpty())
        return; // Cache is still valid
    
    // Rebuild cache
    parentsWithMatchingChildren.clear();
    cachedAddrPrefix = addrPrefix;
    
    if (addrPrefix.isEmpty())
        return;
    
    // Build cache of parents that have matching children - single pass
    int rowCount = sourceModel()->rowCount();
    for (int i = 0; i < rowCount; i++) {
        QModelIndex index = sourceModel()->index(i, 0);
        bool isChild = index.data(TransactionTableModel::IsChildRole).toBool();
        
        if (isChild) {
            QString address = index.data(TransactionTableModel::AddressRole).toString();
            QString label = index.data(TransactionTableModel::LabelRole).toString();
            
            if (address.contains(addrPrefix, Qt::CaseInsensitive) || label.contains(addrPrefix, Qt::CaseInsensitive)) {
                QString txHash = index.data(TransactionTableModel::TxHashRole).toString();
                parentsWithMatchingChildren.insert(txHash);
            }
        }
        
        // Keep UI responsive for large datasets
        if (i % 200 == 0 && i > 0) {
            QApplication::processEvents();
        }
    }
}

int TransactionFilterProxy::rowCount(const QModelIndex &parent) const
{
    if(limitRows != -1)
    {
        // Count parent transactions only for limiting
        int totalRows = QSortFilterProxyModel::rowCount(parent);
        int parentCount = 0;
        int rowsToShow = 0;
        
        for (int i = 0; i < totalRows; i++) {
            QModelIndex idx = index(i, 0, parent);
            bool isParent = idx.data(TransactionTableModel::IsParentRole).toBool();
            
            if (isParent) {
                parentCount++;
                if (parentCount > limitRows) {
                    break; // Stop once we've reached the parent limit
                }
            }
            rowsToShow++;
        }
        
        return rowsToShow;
    }
    else
    {
        return QSortFilterProxyModel::rowCount(parent);
    }
}

bool TransactionFilterProxy::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Get transaction hashes
    QString leftHash = sourceModel()->data(left, TransactionTableModel::TxHashRole).toString();
    QString rightHash = sourceModel()->data(right, TransactionTableModel::TxHashRole).toString();
    
    // CRITICAL: If they're from the same transaction, prevent Qt from reordering them
    // by making them compare as equal (return false) which maintains source model order
    if (leftHash == rightHash) {
        // Same transaction - always return false so they're considered equal
        // This prevents Qt's sort from reordering them, maintaining source order
        // where parent is always before children (guaranteed by TxLessThan)
        return false;
    }
    
    // Different transactions - we need to compare the parent records
    QModelIndex leftParent = left;
    QModelIndex rightParent = right;
    
    bool leftIsParent = sourceModel()->data(left, TransactionTableModel::IsParentRole).toBool();
    bool rightIsParent = sourceModel()->data(right, TransactionTableModel::IsParentRole).toBool();
    
    // If left is a child, find its parent
    if (!leftIsParent) {
        int searchRow = left.row() - 1;
        while (searchRow >= 0) {
            QModelIndex candidate = sourceModel()->index(searchRow, left.column());
            QString candidateHash = sourceModel()->data(candidate, TransactionTableModel::TxHashRole).toString();
            bool candidateIsParent = sourceModel()->data(candidate, TransactionTableModel::IsParentRole).toBool();
            
            if (candidateHash == leftHash && candidateIsParent) {
                leftParent = candidate;
                break;
            }
            
            // If we hit a different transaction, stop searching
            if (candidateHash != leftHash) {
                break;
            }
            searchRow--;
        }
    }
    
    // If right is a child, find its parent
    if (!rightIsParent) {
        int searchRow = right.row() - 1;
        while (searchRow >= 0) {
            QModelIndex candidate = sourceModel()->index(searchRow, right.column());
            QString candidateHash = sourceModel()->data(candidate, TransactionTableModel::TxHashRole).toString();
            bool candidateIsParent = sourceModel()->data(candidate, TransactionTableModel::IsParentRole).toBool();
            
            if (candidateHash == rightHash && candidateIsParent) {
                rightParent = candidate;
                break;
            }
            
            // If we hit a different transaction, stop searching
            if (candidateHash != rightHash) {
                break;
            }
            searchRow--;
        }
    }
    
    // Now compare the parent records using the base class implementation
    if (leftParent.isValid() && rightParent.isValid()) {
        return QSortFilterProxyModel::lessThan(leftParent, rightParent);
    }
    
    // Fallback to row comparison
    return left.row() < right.row();
}
