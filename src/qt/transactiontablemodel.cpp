// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiontablemodel.h"

#include "addresstablemodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactiondesc.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "walletmodel.h"

#include "core_io.h"
#include "sync.h"
#include "uint256.h"
#include "util.h"
#include "main.h"
#include "wallet/wallet.h"
#include "wallet/rpcpiratewallet.h"

#include <QColor>
#include <QDateTime>
#include <QIcon>
#include <QList>
#include <QSettings>
#include <QApplication>
#include <QTimer>

/**
 * Column text alignment configuration.
 * 
 * Amount column is right-aligned since it contains numeric values,
 * following financial display conventions. All other columns are
 * left-aligned for readability.
 */
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignRight|Qt::AlignVCenter /* amount */
    };

/**
 * Comparison operator for sorting and binary search of transaction list.
 * 
 * SORTING HIERARCHY:
 * 1. Transaction hash (txid) - groups related records together
 * 2. Parent before children - parent record always comes first
 * 3. Child index (idx) - preserves decomposition order
 * 4. Memory address - absolute tiebreaker to ensure stable sort
 * 
 * CRITICAL DESIGN:
 * Parent-child ordering MUST be preserved even when sort direction is
 * reversed (descending). Using idx as tiebreaker ensures children never
 * appear before their parent regardless of sort direction.
 * 
 * Memory address tiebreaker prevents unstable sorts that could corrupt
 * Qt model/view consistency when multiple records are otherwise equal.
 */
struct TxLessThan
{
    bool operator()(const TransactionRecord &a, const TransactionRecord &b) const
    {
        // Primary sort: group by transaction hash
        if (a.hash != b.hash) {
            return a.hash < b.hash;
        }
        // Within same transaction: parent always comes before children
        if (a.isParent && !b.isParent) {
            return true;
        }
        if (!a.isParent && b.isParent) {
            return false;
        }
        // Among children: sort by decomposition order (idx)
        if (a.idx != b.idx) {
            return a.idx < b.idx;
        }
        // Final tiebreaker: memory address for stable sort
        return &a < &b;
    }
    
    // Overload for binary search by hash
    bool operator()(const TransactionRecord &a, const uint256 &b) const
    {
        return a.hash < b;
    }
    bool operator()(const uint256 &a, const TransactionRecord &b) const
    {
        return a < b.hash;
    }
};

/**
 * Private implementation for TransactionTableModel.
 * 
 * THREE-TIER CACHING ARCHITECTURE:
 * 
 * 1. decomposedTxCache (Master Cache):
 *    - Contains ALL transactions ever loaded
 *    - Never filtered, never shrinks (unless full refresh)
 *    - Populated incrementally: initial 50 tx + lazy load remainder
 *    - Indexed for fast filtering (dateIndex, typeIndex, addressIndex, etc.)
 *    - All indexes point to positions in this cache
 * 
 * 2. cachedWallet (Working Cache):
 *    - Filtered subset of decomposedTxCache for display
 *    - Rebuilt via rebuildFromCache() when filters change
 *    - Source model for TransactionFilterProxy
 *    - Can be limited to N parent transactions (limitParents)
 * 
 * 3. TransactionFilterProxy:
 *    - Additional display-level filtering (showParentsOnly, showAddressOnly)
 *    - Final data source for QTableView
 * 
 * LAZY LOADING:
 * Initial startup loads only INITIAL_TX_LIMIT (50) transactions for quick display.
 * Remaining transactions loaded in batches of BATCH_SIZE (50) every 100ms via timer.
 * After lazy load completes, indexes are built and cachedWallet is populated.
 * 
 * PERFORMANCE BENEFITS:
 * - Fast startup: 50 tx loaded in <100ms even with 100k+ wallet
 * - Responsive UI: Batch processing with QApplication::processEvents()
 * - Efficient filtering: Index-based queries avoid full cache scans
 * - Memory efficient: Single decomposed cache, filtered view cached separately
 */
class TransactionTablePriv
{
public:
    TransactionTablePriv(CWallet *_wallet, TransactionTableModel *_parent) :
        wallet(_wallet),
        parent(_parent),
        cachedSize(-1),
        lazyLoadInProgress(false),
        initialLoadComplete(false),
        lazyLoadPosition(0),
        lazyLoadTotalSize(0),
        lazyLoadTimer(nullptr)
    {
    }
    
    ~TransactionTablePriv()
    {
        if (lazyLoadTimer) {
            lazyLoadTimer->stop();
            delete lazyLoadTimer;
        }
    }

    CWallet *wallet;
    TransactionTableModel *parent;

    /**
     * Working cache of filtered transactions for display.
     * 
     * Populated by rebuildFromCache() which filters decomposedTxCache
     * based on current filter settings. This is the data source for
     * the proxy model and ultimately the table view.
     * 
     * Sort order maintained via TxLessThan to group by txid with
     * parent-child ordering preserved.
     */
    QList<TransactionRecord> cachedWallet;
    
    mutable int cachedSize;  ///< Cached row count to avoid recalculation
    
    /** @name Lazy loading state
     * @{
     */
    bool lazyLoadInProgress;     ///< True while timer is running
    bool initialLoadComplete;    ///< True after all transactions cached
    int lazyLoadPosition;        ///< Current position in lazySortedArchive
    int lazyLoadTotalSize;       ///< Total transactions to lazy load
    QTimer* lazyLoadTimer;       ///< Timer for batch processing
    QList<uint256> lazySortedArchive; ///< Sorted list of transaction hashes to load
    /**@}*/
    
    /**
     * Master transaction cache - decomposed records for ALL loaded transactions.
     * 
     * DESIGN RATIONALE:
     * This cache is populated once and never filtered. All filter operations
     * build cachedWallet from this master cache using index-based queries.
     * This approach:
     * - Prevents O(N) operations on every filter change
     * - Enables fast filter changes (just rebuild cachedWallet)
     * - Supports multiple simultaneous filtered views
     * - Maintains stable positions for index references
     * 
     * Each transaction is decomposed into parent + children (inputs/outputs/fee).
     * Indexes (dateIndex, typeIndex, etc.) map to positions in this list.
     */
    QList<TransactionRecord> decomposedTxCache;
    
    /**
     * Transaction hash to position mapping.
     * 
     * Maps txid -> (start_index, record_count) in decomposedTxCache.
     * Enables fast lookup of all records for a given transaction.
     * Example: {"abc123..." -> (100, 5)} means txid starts at index 100
     * and has 5 records (1 parent + 4 children).
     */
    QMap<QString, QPair<int, int>> txHashIndex;
    
    /** @name Fast filtering indexes
     * All point to positions in decomposedTxCache for O(1) lookups.
     * @{
     */
    QMultiMap<qint64, int> dateIndex;        ///< Timestamp -> cache position
    QMultiMap<int, int> typeIndex;           ///< Transaction type -> cache position
    QMultiMap<QString, int> addressIndex;    ///< Address (lowercase) -> cache position
    QSet<int> watchOnlyIndex;                ///< Cache positions of watch-only transactions
    QMultiMap<qint64, int> amountIndex;      ///< Absolute amount -> cache position
    /**@}*/
    
    /** @name Last used filter settings
     * Stored so new transactions can be added with the same filters applied.
     * @{
     */
    QDateTime lastDateFrom;
    QDateTime lastDateTo;
    quint32 lastTypeFilter;
    int lastWatchOnlyFilter;
    QString lastAddrPrefix;
    qint64 lastMinAmount;
    bool lastShowInactive;
    int lastLimitParents;
    /**@}*/
    
    /**
     * @brief Shift all index values >= startIdx by offset
     * 
     * After removing records from decomposedTxCache, all indexes pointing to positions
     * >= startIdx need to be adjusted by the offset (negative for deletion).
     * 
     * @param startIdx Starting index position
     * @param offset Amount to shift (negative for deletion, positive for insertion)
     */
    void shiftIndexValues(int startIdx, int offset)
    {
        // Helper to shift values in a QMultiMap
        auto shiftMultiMap = [startIdx, offset](auto& map) {
            QList<typename std::remove_reference<decltype(map)>::type::key_type> keys = map.uniqueKeys();
            for (const auto& key : keys) {
                QList<int> values = map.values(key);
                map.remove(key);
                for (int val : values) {
                    if (val >= startIdx) {
                        map.insert(key, val + offset);
                    } else {
                        map.insert(key, val);
                    }
                }
            }
        };
        
        // Shift all index types
        shiftMultiMap(dateIndex);
        shiftMultiMap(typeIndex);
        shiftMultiMap(addressIndex);
        shiftMultiMap(amountIndex);
        
        // Shift watchOnlyIndex (QSet)
        QSet<int> newWatchOnlyIndex;
        for (int val : watchOnlyIndex) {
            if (val >= startIdx) {
                newWatchOnlyIndex.insert(val + offset);
            } else {
                newWatchOnlyIndex.insert(val);
            }
        }
        watchOnlyIndex = newWatchOnlyIndex;
    }

    /**
     * Build search indexes from decomposedTxCache.
     * 
     * Creates five indexes for fast filtering without scanning entire cache:
     * - dateIndex: Find transactions in date range
     * - typeIndex: Find transactions by type (send/receive/etc.)
     * - addressIndex: Find transactions involving specific address
     * - watchOnlyIndex: Find watch-only transactions
     * - amountIndex: Find transactions above minimum amount
     * 
     * Also builds txHashIndex for mapping transaction ID to cache positions.
     * 
     * Called after:
     * - Initial load completes
     * - Lazy load completes
     * - Full wallet refresh
     */
    void buildIndexes()
    {
        // Clear existing indexes
        dateIndex.clear();
        typeIndex.clear();
        addressIndex.clear();
        watchOnlyIndex.clear();
        amountIndex.clear();
        txHashIndex.clear();
        
        // Build indexes from decomposedTxCache (master cache)
        for(int i = 0; i < decomposedTxCache.size(); i++)
        {
            const TransactionRecord& rec = decomposedTxCache[i];
            
            // Update txHashIndex - track start position and count for each txid
            QString txHashStr = QString::fromStdString(rec.hash.ToString());
            if (!txHashIndex.contains(txHashStr)) {
                // First record of this transaction - set start position
                txHashIndex[txHashStr] = QPair<int, int>(i, 1);
            } else {
                // Additional record of same transaction - increment count
                txHashIndex[txHashStr].second++;
            }
            
            // Date index - for date range filtering
            dateIndex.insert(rec.time, i);
            
            // Type index - for transaction type filtering
            typeIndex.insert(rec.type, i);
            
            // Address index - case-insensitive address search
            if (!rec.address.empty()) {
                addressIndex.insert(QString::fromStdString(rec.address).toLower(), i);
            }
            
            // Watch-only index - flag watch-only transactions
            if (rec.involvesWatchAddress) {
                watchOnlyIndex.insert(i);
            }
            
            // Amount index - absolute value for range queries
            qint64 absAmount = qAbs(rec.credit + rec.debit);
            amountIndex.insert(absAmount, i);
            
            // Keep UI responsive during long index builds
            if (i % 500 == 0 && i > 0) {
                QApplication::processEvents();
            }
        }
    }
    
    /**
     * Initialize and start lazy loading of transactions.
     * 
     * LAZY LOAD STRATEGY:
     * Initial startup loads only INITIAL_TX_LIMIT (50) transactions for quick display.
     * This function queues remaining transactions for background loading in batches.
     * 
     * ARCHITECTURE:
     * 1. Build sorted transaction list (newest first): mapArcTxs + mapWallet
     * 2. Filter out invalid/missing transactions (null blocks, unknown blocks)
     * 3. Start 100ms timer to process BATCH_SIZE (50) transactions per cycle
     * 4. Each batch adds to decomposedTxCache without updating cachedWallet
     * 5. After completion, build indexes and populate cachedWallet
     * 
     * PERFORMANCE:
     * - Non-blocking: Runs on timer to keep UI responsive
     * - Deduplication: Skips transactions already in cache
     * - Batched: Processes small chunks with QApplication::processEvents()
     * - Memory efficient: Builds lazySortedArchive once, reuses for all batches
     */
    void startLazyLoad()
    {
        if (initialLoadComplete) {
            return; // Already completed
        }
        
        if (lazyLoadInProgress) {
            return; // Already running
        }
        
        lazyLoadInProgress = true;
        lazyLoadPosition = 0;
        lazySortedArchive.clear();
        
        // Build sorted archive of all transactions (newest first)
        {
            LOCK2(cs_main, wallet->cs_wallet);
            std::map<std::pair<int,int>, uint256> sortedArchive; // (height, index) -> txid
            std::set<uint256> addedTxids;
            
            // Phase 1: Add archived transactions (mapArcTxs)
            for (map<uint256, ArchiveTxPoint>::iterator it = wallet->mapArcTxs.begin(); 
                 it != wallet->mapArcTxs.end(); ++it) {
                uint256 txid = it->first;
                const ArchiveTxPoint& arcTxPt = it->second;
                
                // Skip transactions with missing/unknown blocks
                if (arcTxPt.hashBlock.IsNull() || mapBlockIndex.count(arcTxPt.hashBlock) == 0) {
                    continue;
                }
                
                const CBlockIndex* pindex = mapBlockIndex[arcTxPt.hashBlock];
                sortedArchive[make_pair(pindex->nHeight, arcTxPt.nIndex)] = txid;
                addedTxids.insert(txid);
            }
            
            // Phase 2: Add unconfirmed transactions and those not in archive
            int nPosUnconfirmed = 0;
            for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); 
                 it != wallet->mapWallet.end(); ++it) {
                CWalletTx wtx = (*it).second;
                uint256 txid = wtx.GetHash();
                
                // Skip if already added from archive
                if (addedTxids.count(txid) > 0) {
                    continue;
                }
                
                // Add unconfirmed transactions to top (after chain tip)
                if (wtx.GetDepthInMainChain() == 0) {
                    sortedArchive[make_pair(chainActive.Tip()->nHeight + 1, nPosUnconfirmed)] = txid;
                    nPosUnconfirmed++;
                } else if (!wtx.hashBlock.IsNull() && mapBlockIndex.count(wtx.hashBlock) > 0) {
                    sortedArchive[make_pair(mapBlockIndex[wtx.hashBlock]->nHeight, wtx.nIndex)] = txid;
                } else {
                    sortedArchive[make_pair(chainActive.Tip()->nHeight + 1, nPosUnconfirmed)] = txid;
                    nPosUnconfirmed++;
                }
            }
            
            // Convert sorted map to flat list (reverse = newest first)
            lazySortedArchive.reserve(sortedArchive.size());
            for (map<std::pair<int,int>, uint256>::reverse_iterator it = sortedArchive.rbegin(); 
                 it != sortedArchive.rend(); ++it) {
                lazySortedArchive.append(it->second);
            }
            
            lazyLoadTotalSize = lazySortedArchive.size();
        }
        
        // Create timer for batch processing (100ms interval)
        if (!lazyLoadTimer) {
            lazyLoadTimer = new QTimer(parent);
            lazyLoadTimer->setInterval(100);
            lazyLoadTimer->setSingleShot(false);
            QObject::connect(lazyLoadTimer, &QTimer::timeout, [this]() {
                this->lazyLoadBatch();
            });
        }
        
        // Start batch processing if there are transactions to load
        if (lazyLoadTotalSize > 0) {
            lazyLoadTimer->start();
        } else {
            lazyLoadInProgress = false;
            initialLoadComplete = true;
        }
    }
    
    /**
     * Process one batch of lazy-loaded transactions.
     * 
     * BATCH PROCESSING:
     * - Loads BATCH_SIZE (50) new transactions per call
     * - Checks up to BATCH_SIZE*2 to handle already-cached transactions
     * - Adds to decomposedTxCache without updating cachedWallet
     * - Stops when batch complete or end of archive reached
     * 
     * COMPLETION:
     * When all transactions loaded:
     * 1. Stop timer
     * 2. Build search indexes
     * 3. Rebuild cachedWallet with default filters
     * 4. Set initialLoadComplete flag
     * 
     * DEDUPLICATION:
     * Uses txHashIndex to skip already-cached transactions,
     * which can occur when transactions are added during lazy load.
     */
    void lazyLoadBatch()
    {
        const int BATCH_SIZE = 50; // Process 50 new transactions per batch
        bool fIncludeWatchonly = true;
        
        QList<RpcArcTransaction> arcTxList;
        int startPos = lazyLoadPosition;
        int processedCount = 0; // Track how many new transactions we processed
        int checkedCount = 0; // Track how many we've checked (including skipped)
        int currentPos = startPos; // Track actual position in archive
        bool reachedEnd = false;
        
        // Process transactions from pre-built sorted archive
        {
            LOCK2(cs_main, wallet->cs_wallet);
            
            for (int i = startPos; i < lazySortedArchive.size(); i++)
            {
                currentPos = i; // Track where we are in the archive
                uint256 txid = lazySortedArchive[i];
                QString txHashStr = QString::fromStdString(txid.ToString());
                
                // Stop after checking BATCH_SIZE * 2 transactions (to prevent infinite loop if all cached)
                if (checkedCount >= BATCH_SIZE * 2) {
                    break;
                }
                checkedCount++;
                
                // Skip if already in cache
                if (txHashIndex.contains(txHashStr)) {
                    continue;
                }
                
                // Stop after processing BATCH_SIZE new transactions
                if (processedCount >= BATCH_SIZE) {
                    break;
                }
                
                RpcArcTransaction arcTx;
                
                std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(txid);
                if (mi != wallet->mapWallet.end()) {
                    CWalletTx& wtx = mi->second;
                    getRpcArcTx(wtx, arcTx, fIncludeWatchonly, false);
                } else {
                    getRpcArcTx(txid, arcTx, fIncludeWatchonly, false);
                }

                arcTxList.append(arcTx);
                processedCount++;
            }
            
            // Update position to continue from where we left off
            // If we broke early (processed BATCH_SIZE), currentPos is where we stopped
            // If we reached the end, currentPos is the last index
            lazyLoadPosition = currentPos + 1; // Next time start from the next position
            reachedEnd = (lazyLoadPosition >= lazySortedArchive.size());
        }
        
        // Process batch outside of locks - add to decomposedTxCache master list
        int cached = 0;
        int recordsAdded = 0;
        
        for (const RpcArcTransaction& arcTx : arcTxList) {
            QString txHashStr = QString::fromStdString(arcTx.txid.ToString());
            
            // Skip transactions that couldn't be loaded
            if (arcTx.category == "not found") {
                continue;
            }
            
            // Check if already in cache
            if (!txHashIndex.contains(txHashStr)) {
                // Decompose transaction
                QList<TransactionRecord> records = TransactionRecord::decomposeTransaction(arcTx);
                
                // Track position in decomposedTxCache
                int startIdx = decomposedTxCache.size();
                txHashIndex[txHashStr] = QPair<int, int>(startIdx, records.size());
                
                // Append to master cache
                decomposedTxCache.append(records);
                cached++;
                recordsAdded += records.size();
            }
        }
        
        // Emit progress update
        Q_EMIT parent->lazyLoadProgress(lazyLoadPosition, lazyLoadTotalSize);
        
        // Check if we're done
        if (reachedEnd) {
            lazyLoadTimer->stop();
            lazyLoadInProgress = false;
            initialLoadComplete = true; // Mark that initial load is complete
            lazySortedArchive.clear(); // Free memory
            
            // Build indexes now that all data is loaded
            buildIndexes();
            
            LogPrintf("Lazy loading complete: %d records from %d transactions cached and indexed\n", 
                     decomposedTxCache.size(), txHashIndex.size());
            
            // Rebuild cachedWallet with default filters (show all transactions)
            // This ensures the initial presentation is correct
            QDateTime minDate = QDateTime::fromTime_t(0);
            QDateTime maxDate = QDateTime::fromTime_t(0xFFFFFFFF);
            quint32 allTypes = 0xFFFFFFFF;
            int watchOnlyAll = 0; // WatchOnlyFilter_All
            
            requestFilteredRebuild(minDate, maxDate, allTypes, watchOnlyAll, 
                                  QString(), 0, true, TransactionTableModel::INITIAL_TX_LIMIT);
            
            Q_EMIT parent->lazyLoadComplete();
        }
    }
    
    /**
     * @brief Stop the lazy loading process
     * 
     * Stops the lazy loading timer and marks lazy loading as not in progress.
     * Called when lazy loading needs to be interrupted (e.g., wallet closing).
     */
    void stopLazyLoad()
    {
        if (lazyLoadTimer) {
            lazyLoadTimer->stop();
        }
        lazyLoadInProgress = false;
    }
    
    /**
     * @brief Reset all caches and restart lazy loading from scratch
     * 
     * Called after operations that invalidate the cache like rescans.
     * Clears all data structures and restarts the lazy loading process.
     */
    void resetAndRestartLazyLoad()
    {
        // Stop any ongoing lazy load
        stopLazyLoad();
        
        // Clear all caches and indexes
        cachedWallet.clear();
        decomposedTxCache.clear();
        txHashIndex.clear();
        dateIndex.clear();
        typeIndex.clear();
        addressIndex.clear();
        watchOnlyIndex.clear();
        amountIndex.clear();
        lazySortedArchive.clear();
        
        // Reset state
        initialLoadComplete = false;
        lazyLoadPosition = 0;
        lazyLoadTotalSize = 0;
        cachedSize = -1;
        
        // Notify views that everything changed
        parent->beginResetModel();
        parent->endResetModel();
        
        // Restart lazy loading
        startLazyLoad();
    }
    
    /**
     * @brief Rebuild cachedWallet from decomposedTxCache without filters
     * 
     * Copies all records from the master cache to the display cache, sorts them,
     * and updates parent-child relationships. Used when no filtering is needed.
     * 
     * Process:
     * 1. Copy all records from decomposedTxCache to cachedWallet
     * 2. Sort using TxLessThan comparator (preserves parent-child ordering)
     * 3. Update parentIdx for all children to reflect new positions
     */
    void rebuildFromCache()
    {
        // Clear current cachedWallet
        cachedWallet.clear();
        
        // Copy all records from decomposedTxCache to cachedWallet
        cachedWallet = decomposedTxCache;
        
        // Sort all records
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        // Update parentIdx for all children after sorting
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            if (cachedWallet[i].isParent)
            {
                // Update all children of this parent
                uint256 parentHash = cachedWallet[i].hash;
                for(int j = i + 1; j < cachedWallet.size() && cachedWallet[j].hash == parentHash; j++)
                {
                    if (cachedWallet[j].isChild)
                    {
                        cachedWallet[j].parentIdx = i;
                    }
                }
            }
        }
        
        cachedSize = -1; // Invalidate size cache
    }
    
    /**
     * @brief Request a filtered rebuild of cachedWallet from decomposedTxCache
     * 
     * Applies filters to the master cache and rebuilds the display cache, then
     * notifies Qt views of the model reset.
     * 
     * @param dateFrom Start date for filtering
     * @param dateTo End date for filtering
     * @param typeFilter Bitmask of transaction types to include
     * @param watchOnlyFilter Watch-only filter mode
     * @param addrPrefix Address prefix to filter by
     * @param minAmount Minimum transaction amount
     * @param showInactive Whether to show conflicted transactions
     * @param limitParents Maximum number of parent transactions to display
     */
    void requestFilteredRebuild(const QDateTime &dateFrom, const QDateTime &dateTo,
                                quint32 typeFilter, int watchOnlyFilter,
                                const QString &addrPrefix, qint64 minAmount,
                                bool showInactive, int limitParents)
    {
        // Store filter settings for reuse when new transactions arrive
        lastDateFrom = dateFrom;
        lastDateTo = dateTo;
        lastTypeFilter = typeFilter;
        lastWatchOnlyFilter = watchOnlyFilter;
        lastAddrPrefix = addrPrefix;
        lastMinAmount = minAmount;
        lastShowInactive = showInactive;
        lastLimitParents = limitParents;
        
        // Rebuild from the master cache with filters
        rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, 
                        addrPrefix, minAmount, showInactive, limitParents);
        
        // Notify views that all data has changed
        parent->beginResetModel();
        parent->endResetModel();
    }
    
    void rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                          quint32 typeFilter, int watchOnlyFilter, 
                          const QString &addrPrefix, qint64 minAmount, 
                          bool showInactive, int limitParents)
    {
        LogPrintf("Rebuilding cachedWallet with filters from decomposedTxCache (%d records from %d txs, limit=%d parents)...\n", 
                 decomposedTxCache.size(), txHashIndex.size(), limitParents);
        
        // Clear current cachedWallet
        cachedWallet.clear();
        
        // First pass: collect all matching parent transactions with their records
        // This two-pass approach prevents O(N^2) sorting of the entire cache
        struct MatchedTx {
            qint64 time;
            int startIdx;
            int count;
        };
        QList<MatchedTx> matchedTransactions;
        
        // Iterate through all transactions using txHashIndex
        for (auto it = txHashIndex.begin(); it != txHashIndex.end(); ++it) {
            const QPair<int, int>& pos = it.value();
            int startIdx = pos.first;
            int recordCount = pos.second;
            
            if (recordCount == 0) continue;
            
            // Check parent record against filters
            bool parentMatches = false;
            qint64 parentTime = 0;
            
            // Find the parent record
            for (int i = startIdx; i < startIdx + recordCount; i++) {
                const TransactionRecord& rec = decomposedTxCache[i];
                if (!rec.isParent) continue;
                
                parentTime = rec.time;
                
                // Check showInactive filter (conflicted transactions)
                if (!showInactive && rec.status.status == TransactionStatus::Conflicted)
                    continue;
                
                // Apply filters
                QDateTime txDateTime = QDateTime::fromTime_t(rec.time);
                if (txDateTime < dateFrom || txDateTime > dateTo)
                    continue;
                    
                if (!(TransactionFilterProxy::TYPE(rec.type) & typeFilter))
                    continue;
                    
                if (watchOnlyFilter == TransactionFilterProxy::WatchOnlyFilter_Yes && !rec.involvesWatchAddress)
                    continue;
                if (watchOnlyFilter == TransactionFilterProxy::WatchOnlyFilter_No && rec.involvesWatchAddress)
                    continue;
                    
                qint64 absAmount = qAbs(rec.credit + rec.debit);
                if (absAmount < minAmount)
                    continue;
                
                // Check address filter (parent or any child must match)
                bool addressMatches = true;
                if (!addrPrefix.isEmpty()) {
                    addressMatches = false;
                    for (int j = startIdx; j < startIdx + recordCount; j++) {
                        QString address = QString::fromStdString(decomposedTxCache[j].address);
                        if (address.contains(addrPrefix, Qt::CaseInsensitive)) {
                            addressMatches = true;
                            break;
                        }
                    }
                }
                
                if (addressMatches) {
                    parentMatches = true;
                    break;
                }
            }
            
            // If parent matches, add to matched list
            if (parentMatches) {
                MatchedTx matched;
                matched.time = parentTime;
                matched.startIdx = startIdx;
                matched.count = recordCount;
                matchedTransactions.append(matched);
            }
        }
        
        // Sort matched transactions by time (most recent first)
        std::sort(matchedTransactions.begin(), matchedTransactions.end(), 
                 [](const MatchedTx& a, const MatchedTx& b) { return a.time > b.time; });
        
        // Apply limit and add to cachedWallet
        int loadedParents = 0;
        for (const MatchedTx& matched : matchedTransactions) {
            if (limitParents > 0 && loadedParents >= limitParents) {
                break;
            }
            // Copy records from decomposedTxCache to cachedWallet
            for (int i = matched.startIdx; i < matched.startIdx + matched.count; i++) {
                cachedWallet.append(decomposedTxCache[i]);
            }
            loadedParents++;
        }
        
        // Sort all records using TxLessThan (preserves parent-child ordering)
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        // Update parentIdx for all children after sorting
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            if (cachedWallet[i].isParent)
            {
                // Update all children of this parent
                uint256 parentHash = cachedWallet[i].hash;
                for(int j = i + 1; j < cachedWallet.size() && cachedWallet[j].hash == parentHash; j++)
                {
                    if (cachedWallet[j].isChild)
                    {
                        cachedWallet[j].parentIdx = i;
                    }
                }
            }
        }
        
        cachedSize = -1; // Invalidate size cache
    }
    
    /**
     * @brief Refresh wallet data from core and rebuild caches
     * 
     * This is the main initialization method that:
     * 1. Collects transaction data from wallet under locks
     * 2. Filters out invalid transactions (not final, not trusted, insufficient depth)
     * 3. Decomposes transactions and updates master cache
     * 4. Loads initial INITIAL_TX_LIMIT transactions for display
     * 5. Starts lazy loading remaining transactions in background
     * 
     * Performance: Loads exactly INITIAL_TX_LIMIT (50) transactions initially,
     * then lazy loads remaining in batches of 10 every 100ms.
     */
    /**
     * @brief Refresh wallet data from core and rebuild caches
     * 
     * This is the main initialization method that:
     * 1. Collects transaction data from wallet under locks
     * 2. Filters out invalid transactions (not final, not trusted, insufficient depth)
     * 3. Decomposes transactions and updates master cache
     * 4. Loads initial INITIAL_TX_LIMIT transactions for display
     * 5. Starts lazy loading remaining transactions in background
     * 
     * Performance: Loads exactly INITIAL_TX_LIMIT (50) transactions initially,
     * then lazy loads remaining in batches of 10 every 100ms.
     */
    void refreshWallet()
    {
        cachedWallet.clear();
        // Keep decomposedTxCache - we'll reuse cached decompositions

        bool fIncludeWatchonly = true;
        
        // Track which transactions are still valid (using string keys for Qt containers)
        QSet<QString> validTxHashes;
        
        // Declare arcTxList outside lock scope so we can use it after releasing locks
        QList<RpcArcTransaction> arcTxList;
        arcTxList.reserve(TransactionTableModel::INITIAL_TX_LIMIT); // Pre-allocate for performance
        
        {
            LOCK2(cs_main, wallet->cs_wallet);

            // Get all Archived Transactions - track what's being filtered
            uint256 ut;
            std::map<std::pair<int,int>, uint256> sortedArchive;
            int totalArcTxs = wallet->mapArcTxs.size();
            int nullBlockHash = 0;
            int blockNotInIndex = 0;
            int addedArcTxs = 0;
            
            for (map<uint256, ArchiveTxPoint>::iterator it = wallet->mapArcTxs.begin(); it != wallet->mapArcTxs.end(); ++it)
            {
                uint256 txid = (*it).first;
                ArchiveTxPoint arcTxPt = (*it).second;
                std::pair<int,int> key;

                if (arcTxPt.hashBlock.IsNull()) {
                    nullBlockHash++;
                    continue; // Skip - no block hash
                }
                
                if (mapBlockIndex.count(arcTxPt.hashBlock) == 0) {
                    blockNotInIndex++;
                    continue; // Skip - block not in our index
                }
                
                // Normal case - block is in index
                key = make_pair(mapBlockIndex[arcTxPt.hashBlock]->nHeight, arcTxPt.nIndex);
                sortedArchive[key] = txid;
                addedArcTxs++;
            }

            // Process unconfirmed transactions from mapWallet
            int nPosUnconfirmed = 0;
            for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it) {
                CWalletTx wtx = (*it).second;
                std::pair<int,int> key;

                if (wtx.GetDepthInMainChain() == 0) {
                    ut = wtx.GetHash();
                    key = make_pair(chainActive.Tip()->nHeight + 1,  nPosUnconfirmed);
                    sortedArchive[key] = wtx.GetHash();
                    nPosUnconfirmed++;
                } else if (!wtx.hashBlock.IsNull() && mapBlockIndex.count(wtx.hashBlock) > 0) {
                    key = make_pair(mapBlockIndex[wtx.hashBlock]->nHeight, wtx.nIndex);
                    sortedArchive[key] = wtx.GetHash();
                } else {
                    key = make_pair(chainActive.Tip()->nHeight + 1,  nPosUnconfirmed);
                    sortedArchive[key] = wtx.GetHash();
                    nPosUnconfirmed++;
                }
            }


            // Collect RpcArcTransaction data under lock, then process outside
            // The validation filters in initial load ensure only displayable transactions are loaded initially
            int targetSize = TransactionTableModel::INITIAL_TX_LIMIT; // Load exactly INITIAL_TX_LIMIT for initial display
            int checked = 0;
            int skippedNotFinal = 0;
            int skippedNotTrusted = 0;
            int skippedDepth = 0;
            int skippedArcNoBlock = 0;
            
            for (map<std::pair<int,int>, uint256>::reverse_iterator it = sortedArchive.rbegin(); it != sortedArchive.rend(); ++it)
            {
                checked++;
                
                // Limit initial load for performance - stop after we have enough valid transactions
                if (arcTxList.size() >= targetSize) break;
                
                uint256 txid = (*it).second;
                RpcArcTransaction arcTx;

                // Use find() instead of count() + operator[] to avoid duplicate lookup
                std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(txid);
                if (mi != wallet->mapWallet.end()) {
                    CWalletTx& wtx = mi->second;

                    if (!CheckFinalTx(wtx)) {
                        skippedNotFinal++;
                        continue;
                    }

                    if (wtx.mapSaplingNoteData.size() == 0 && wtx.mapOrchardNoteData.size() == 0 && !wtx.IsTrusted()) {
                        skippedNotTrusted++;
                        continue;
                    }

                    //Exclude transactions with less confirmations than required
                    if (wtx.GetDepthInMainChain() < 0 ) {
                        skippedDepth++;
                        continue;
                    }

                    getRpcArcTx(wtx, arcTx, fIncludeWatchonly, false);

                } else {
                    //Archived Transactions
                    getRpcArcTx(txid, arcTx, fIncludeWatchonly, false);

                    if (arcTx.blockHash.IsNull() || mapBlockIndex.count(arcTx.blockHash) == 0) {
                        skippedArcNoBlock++;
                        continue;
                    }
                }

                arcTxList.append(arcTx);
                validTxHashes.insert(QString::fromStdString(txid.ToString()));
            }
        } // Release locks here before expensive decomposeTransaction calls
        
        // Clear caches for fresh load
        decomposedTxCache.clear();
        txHashIndex.clear();

        // Process transactions outside of locks - decomposeTransaction is pure computation
        int processedCount = 0;
        int skippedNotFound = 0;
        int addedToCache = 0;
        int addedToDisplay = 0;
        
        for (const RpcArcTransaction& arcTx : arcTxList) {
            QString txHashStr = QString::fromStdString(arcTx.txid.ToString());
            
            // Skip transactions that couldn't be loaded
            if (arcTx.category == "not found") {
                skippedNotFound++;
                continue;
            }
            
            // Check if already in decomposedTxCache
            bool alreadyCached = txHashIndex.contains(txHashStr);
            
            if (!alreadyCached) {
                // Decompose and add to master cache
                QList<TransactionRecord> records = TransactionRecord::decomposeTransaction(arcTx);
                
                // Track position in decomposedTxCache
                int startIdx = decomposedTxCache.size();
                txHashIndex[txHashStr] = QPair<int, int>(startIdx, records.size());
                
                // Append to master cache
                decomposedTxCache.append(records);
                addedToCache++;
                
                // Also add to display cache for initial load
                cachedWallet.append(records);
                addedToDisplay++;
            } else {
                // Already cached - just add to display
                QPair<int, int> pos = txHashIndex[txHashStr];
                for (int i = pos.first; i < pos.first + pos.second; i++) {
                    cachedWallet.append(decomposedTxCache[i]);
                }
                addedToDisplay++;
            }
            
            // Update UI every 100 transactions
            processedCount++;
            if (processedCount % 100 == 0) {
                QApplication::processEvents();
            }
        }
        
        // Sort all records to ensure proper parent-child ordering
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        // Update parentIdx for all children after sorting
        int parentCount = 0;
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            if (cachedWallet[i].isParent)
            {
                parentCount++;
                // Update all children of this parent to point to the new parent index
                uint256 parentHash = cachedWallet[i].hash;
                for(int j = i + 1; j < cachedWallet.size() && cachedWallet[j].hash == parentHash; j++)
                {
                    if (cachedWallet[j].isChild)
                    {
                        cachedWallet[j].parentIdx = i;
                    }
                }
                
                // Update UI every 50 parents
                if (parentCount % 50 == 0) {
                    QApplication::processEvents();
                }
            }
        }
        
        cachedSize = -1; // Invalidate size cache after refresh
        
        // Start lazy loading remaining transactions in background for caching and indexing
        // Indexes will be built after lazy load completes
        // Use QTimer::singleShot to start after returning to event loop
        QTimer::singleShot(500, [this]() {
            this->startLazyLoad();
        });
    }

    /**
     * @brief Update wallet incrementally, to synchronize our model of the wallet
       with that of the core.

       Call with transaction that was added, removed or changed.
     */
    void updateWallet(const uint256 &hash, int status, bool showTransaction)
    {

        // Find bounds of this transaction in model using binary search
        // This is O(log n) instead of O(n) linear search
        QList<TransactionRecord>::iterator lower = std::lower_bound(
            cachedWallet.begin(), cachedWallet.end(), hash, TxLessThan());
        QList<TransactionRecord>::iterator upper = std::upper_bound(
            cachedWallet.begin(), cachedWallet.end(), hash, TxLessThan());
        int lowerIndex = (lower - cachedWallet.begin());
        int upperIndex = (upper - cachedWallet.begin());
        
        // Transaction is in model if lower != upper (binary search found it)
        bool inModel = (lower != upper);

        if(showTransaction && inModel)
            status = CT_UPDATED; /* In model, but want to show, do nothing */
        if(showTransaction && !inModel)
            status = CT_NEW; /* Not in model, but want to show, treat as new */
        if(!showTransaction && inModel)
            status = CT_DELETED; /* In model, but want to hide, treat as deleted */

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                break;
            }
            if(showTransaction)
            {
                // Collect data with locks held in correct order (cs_main before cs_wallet)
                // Then release before GUI operations to avoid blocking
                bool isActiveTx = false;
                bool isArchiveTx = false;
                RpcArcTransaction arcTx;
                bool fIncludeWatchonly = true;

                {
                    LOCK2(cs_main, wallet->cs_wallet);
                    
                    //Try mapWallet first
                    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
                    if(mi != wallet->mapWallet.end()) {
                        isActiveTx = true;
                        CWalletTx& wtx = wallet->mapWallet[hash];
                        getRpcArcTx(wtx, arcTx, fIncludeWatchonly, false);
                    }

                    //Try ArcTx if not found in mapWallet
                    if (!isActiveTx) {
                        std::map<uint256, ArchiveTxPoint>::iterator ami = wallet->mapArcTxs.find(hash);
                        if(ami != wallet->mapArcTxs.end()) {
                            uint256 txid = hash;
                            getRpcArcTx(txid, arcTx, fIncludeWatchonly, false);
                            // Check block index while we have cs_main
                            if (!arcTx.blockHash.IsNull() && mapBlockIndex.count(arcTx.blockHash) > 0) {
                                isArchiveTx = true;
                            }
                        }
                    }
                } // Release both locks before GUI operations

                if (!isActiveTx && !isArchiveTx) {
                    parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
                    cachedWallet.erase(lower, upper);
                    parent->endRemoveRows();
                    break;
                }

                // Check cache first, then decompose if needed
                QList<TransactionRecord> toInsert;
                QString hashStr = QString::fromStdString(hash.ToString());
                
                if (txHashIndex.contains(hashStr)) {
                    // Already in decomposedTxCache - copy records from it
                    QPair<int, int> pos = txHashIndex[hashStr];
                    for (int i = pos.first; i < pos.first + pos.second; i++) {
                        toInsert.append(decomposedTxCache[i]);
                    }
                } else {
                    // Not in cache - decompose and add to master cache
                    toInsert = TransactionRecord::decomposeTransaction(arcTx);
                    
                    // Add to decomposedTxCache and update index
                    int startIdx = decomposedTxCache.size();
                    txHashIndex[hashStr] = QPair<int, int>(startIdx, toInsert.size());
                    decomposedTxCache.append(toInsert);
                    
                    // Rebuild indexes if initial load is complete
                    if (initialLoadComplete) {
                        // Add new records to indexes
                        for (int i = 0; i < toInsert.size(); i++) {
                            const TransactionRecord& rec = toInsert[i];
                            int cacheIdx = startIdx + i;
                            dateIndex.insert(rec.time, cacheIdx);
                            typeIndex.insert(rec.type, cacheIdx);
                            if (!rec.address.empty()) {
                                addressIndex.insert(QString::fromStdString(rec.address).toLower(), cacheIdx);
                            }
                            if (rec.involvesWatchAddress) {
                                watchOnlyIndex.insert(cacheIdx);
                            }
                            qint64 absAmount = qAbs(rec.credit + rec.debit);
                            amountIndex.insert(absAmount, cacheIdx);
                        }
                    }
                }
                
                // Transaction added to decomposedTxCache
                // Instead of rebuilding entire cache, just insert new records
                if(!toInsert.isEmpty() && initialLoadComplete)
                {
                    // Check if transaction matches current filters before inserting
                    bool shouldDisplay = true;
                    
                    if (lastDateFrom.isValid() || lastDateTo.isValid()) {
                        QDateTime txTime = QDateTime::fromTime_t(toInsert[0].time);
                        QDateTime dateFrom = lastDateFrom.isValid() ? lastDateFrom : QDateTime::fromTime_t(0);
                        QDateTime dateTo = lastDateTo.isValid() ? lastDateTo : QDateTime::fromTime_t(0xFFFFFFFF);
                        if (txTime < dateFrom || txTime > dateTo) {
                            shouldDisplay = false;
                        }
                    }
                    
                    if (shouldDisplay && lastTypeFilter > 0) {
                        if (!(TransactionFilterProxy::TYPE(toInsert[0].type) & lastTypeFilter)) {
                            shouldDisplay = false;
                        }
                    }
                    
                    if (shouldDisplay && !lastAddrPrefix.isEmpty()) {
                        bool hasMatchingAddress = false;
                        for (const TransactionRecord& rec : toInsert) {
                            QString address = QString::fromStdString(rec.address);
                            if (address.contains(lastAddrPrefix, Qt::CaseInsensitive)) {
                                hasMatchingAddress = true;
                                break;
                            }
                        }
                        if (!hasMatchingAddress) {
                            shouldDisplay = false;
                        }
                    }
                    
                    if (shouldDisplay && lastMinAmount > 0) {
                        qint64 absAmount = qAbs(toInsert[0].credit + toInsert[0].debit);
                        if (absAmount < lastMinAmount) {
                            shouldDisplay = false;
                        }
                    }
                    
                    // If transaction matches filters, insert it into cachedWallet
                    if (shouldDisplay) {
                        // Find insertion point (maintain sorted order)
                        int insertPos = cachedWallet.size();
                        for (int i = 0; i < cachedWallet.size(); i++) {
                            if (TxLessThan()(toInsert[0], cachedWallet[i])) {
                                insertPos = i;
                                break;
                            }
                        }
                        
                        // Insert new records
                        parent->beginInsertRows(QModelIndex(), insertPos, insertPos + toInsert.size() - 1);
                        for (int i = 0; i < toInsert.size(); i++) {
                            cachedWallet.insert(insertPos + i, toInsert[i]);
                        }
                        parent->endInsertRows();
                        
                        // Update parentIdx for the inserted records
                        for (int i = insertPos; i < insertPos + toInsert.size(); i++) {
                            if (cachedWallet[i].isParent) {
                                // Update children to point to this parent
                                for (int j = i + 1; j < cachedWallet.size() && cachedWallet[j].hash == cachedWallet[i].hash; j++) {
                                    if (cachedWallet[j].isChild) {
                                        cachedWallet[j].parentIdx = i;
                                    }
                                }
                                break;
                            }
                        }
                        
                        cachedSize = -1; // Invalidate size cache
                    }
                }
            }
            break;
        case CT_DELETED:
        {
            if(!inModel)
            {
                break;
            }
            // Removed -- remove entire transaction from table
            cachedSize = -1; // Invalidate cache BEFORE removal
            
            int removeSize = upperIndex - lowerIndex;
            
            // Note: We don't remove from decomposedTxCache (it's the master archive)
            // But we do remove from txHashIndex to mark it as deleted
            QString hashStr = QString::fromStdString(hash.ToString());
            if (txHashIndex.contains(hashStr)) {
                QPair<int, int> pos = txHashIndex[hashStr];
                // Remove from indexes if initial load complete
                if (initialLoadComplete) {
                    for (int i = pos.first; i < pos.first + pos.second; i++) {
                        const TransactionRecord& rec = decomposedTxCache[i];
                        dateIndex.remove(rec.time, i);
                        typeIndex.remove(rec.type, i);
                        if (!rec.address.empty()) {
                            addressIndex.remove(QString::fromStdString(rec.address).toLower(), i);
                        }
                        watchOnlyIndex.remove(i);
                        qint64 absAmount = qAbs(rec.credit + rec.debit);
                        amountIndex.remove(absAmount, i);
                    }
                }
                // Remove from hash index
                txHashIndex.remove(hashStr);
            }
            
            // Remove from cachedWallet indexes (display cache)
            for (int i = lowerIndex; i < upperIndex; i++) {
                // These indexes were for cachedWallet, they're rebuilt on filter changes
                // So we don't need to maintain them here
            }
            
            // Erase the records
            cachedWallet.erase(lower, upper);
            
            // Signal complete refresh with layoutChanged to update row count
            cachedSize = -1;
            Q_EMIT parent->layoutChanged();
            
            // Shift all index values >= upperIndex down by removeSize
            shiftIndexValues(upperIndex, -removeSize);
            
            // CRITICAL: After deletion, all parentIdx values >= upperIndex have shifted down by removeSize
            // We need to update ALL children that reference parents at or after the deletion point
            for (int i = 0; i < cachedWallet.size(); i++) {
                if (cachedWallet[i].isChild) {
                    if (cachedWallet[i].parentIdx >= upperIndex) {
                        // Parent was after deleted range, shift index down
                        cachedWallet[i].parentIdx -= removeSize;
                    } else if (cachedWallet[i].parentIdx >= lowerIndex) {
                        // Parent was in deleted range, invalidate this child
                        cachedWallet[i].parentIdx = -1;
                    }
                }
            }
            break;
        }
        case CT_UPDATED:
            // Miscellaneous updates -- nothing to do, status update will take care of this, and is only computed for
            // visible transactions.
            for (int i = lowerIndex; i < upperIndex; i++) {
                TransactionRecord *rec = &cachedWallet[i];
                rec->status.needsUpdate = true;
            }
            break;
        }
    }

    /**
     * @brief Get the number of visible records in the model
     * 
     * Counts only visible records (non-collapsed parents and their visible children).
     * Uses cached count if valid (cachedSize >= 0), otherwise recalculates.
     * 
     * Visibility rules:
     * - Parent records: always visible
     * - Standalone records: always visible
     * - Child records: visible only if parent exists, is valid, and not collapsed
     * 
     * @return Number of visible records
     */
    int size()
    {
        // Use cached size if valid
        if (cachedSize >= 0) {
            return cachedSize;
        }
        
        // Count visible records (hide collapsed children and invalid records)
        int count = 0;
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            const TransactionRecord& rec = cachedWallet[i];
            
            if (!rec.isChild) {
                // Parent or standalone records are always visible
                count++;
            } else if (rec.isChild) {
                // Child records are only visible if:
                // 1. parentIdx is valid (within bounds)
                // 2. The parent at parentIdx is actually a parent record
                // 3. The parent has the same hash as this child
                // 4. The parent is not collapsed
                if (rec.parentIdx >= 0 && rec.parentIdx < cachedWallet.size()) {
                    const TransactionRecord& parent = cachedWallet[rec.parentIdx];
                    if (parent.isParent && parent.hash == rec.hash && !parent.collapsed) {
                        count++;
                    }
                }
            }
            // Children with invalid or mismatched parentIdx are hidden (not counted)
        }
        cachedSize = count;
        return count;
    }
    
    /**
     * @brief Convert visible row index to actual cachedWallet index
     * 
     * Maps from the visible row number (as seen in UI) to the actual position
     * in cachedWallet array. Skips over hidden (collapsed) children.
     * 
     * @param visibleIdx Visible row index (0-based)
     * @return Actual index in cachedWallet, or -1 if out of bounds
     */
    int visibleIndexToActualIndex(int visibleIdx)
    {
        // Convert visible row index to actual cachedWallet index
        int visibleCount = 0;
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            const TransactionRecord& rec = cachedWallet[i];
            bool isVisible = false;
            
            if (!rec.isChild) {
                // Parent or standalone records are always visible
                isVisible = true;
            } else if (rec.isChild) {
                // Child records are only visible if:
                // 1. parentIdx is valid (within bounds)
                // 2. The parent at parentIdx is actually a parent record (not collapsed)
                // 3. The parent has the same hash as this child
                if (rec.parentIdx >= 0 && rec.parentIdx < cachedWallet.size()) {
                    const TransactionRecord& parent = cachedWallet[rec.parentIdx];
                    if (parent.isParent && parent.hash == rec.hash && !parent.collapsed) {
                        isVisible = true;
                    }
                }
            }
            // Children with invalid or mismatched parentIdx are hidden
            
            if (isVisible) {
                if (visibleCount == visibleIdx) {
                    return i;
                }
                visibleCount++;
            }
        }
        return -1;
    }
    
    /**
     * @brief Convert actual cachedWallet index to visible row index
     * 
     * Maps from actual position in cachedWallet array to visible row number.
     * Returns -1 if the record is not currently visible (e.g., collapsed child).
     * 
     * @param actualIdx Index in cachedWallet array
     * @return Visible row index, or -1 if record is hidden
     */
    int actualIndexToVisibleIndex(int actualIdx)
    {
        // Convert actual cachedWallet index to visible row index
        // Returns -1 if the record at actualIdx is not visible
        if (actualIdx < 0 || actualIdx >= cachedWallet.size()) {
            return -1;
        }
        
        int visibleCount = 0;
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            const TransactionRecord& rec = cachedWallet[i];
            bool isVisible = false;
            
            if (!rec.isChild) {
                isVisible = true;
            } else if (rec.isChild) {
                if (rec.parentIdx >= 0 && rec.parentIdx < cachedWallet.size()) {
                    const TransactionRecord& parent = cachedWallet[rec.parentIdx];
                    if (parent.isParent && parent.hash == rec.hash && !parent.collapsed) {
                        isVisible = true;
                    }
                }
            }
            
            if (i == actualIdx) {
                return isVisible ? visibleCount : -1;
            }
            
            if (isVisible) {
                visibleCount++;
            }
        }
        return -1;
    }

    /**
     * @brief Get transaction record at visible index with status update
     * 
     * Retrieves the TransactionRecord at the specified visible index. If locks
     * can be acquired, updates the record's status from the wallet. Uses TRY_LOCK
     * to avoid blocking the GUI if core is busy (e.g., during rescan).
     * 
     * @param idx Visible index (0-based)
     * @return Pointer to TransactionRecord, or nullptr if invalid index
     */
    TransactionRecord *index(int idx)
    {
        // Bounds check on visible index
        if (idx < 0 || idx >= size()) {
            return 0;
        }
        
        // Convert visible index to actual index
        int actualIdx = visibleIndexToActualIndex(idx);
        if(actualIdx >= 0 && actualIdx < cachedWallet.size())
        {
            TransactionRecord *rec = &cachedWallet[actualIdx];

            // Get required locks upfront. This avoids the GUI from getting
            // stuck if the core is holding the locks for a longer time - for
            // example, during a wallet rescan.
            //
            // If a status update is needed (blocks came in since last check),
            //  update the status of this transaction from the wallet. Otherwise,
            // simply re-use the cached status.
            TRY_LOCK(cs_main, lockMain);
            if(lockMain)
            {
                TRY_LOCK(wallet->cs_wallet, lockWallet);
                if(lockWallet && rec->statusUpdateNeeded())
                {
                    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);

                    if(mi != wallet->mapWallet.end())
                    {
                        rec->updateStatus(mi->second);
                    }
                }
            }
            return rec;
        }
        return 0;
    }

    /**
     * @brief Generate HTML description of transaction for tooltip/details view
     * 
     * @param rec Transaction record to describe
     * @param unit Display unit for amounts
     * @return HTML formatted transaction description
     */
    QString describe(TransactionRecord *rec, int unit)
    {
        {
            LOCK2(cs_main, wallet->cs_wallet);
            std::map<uint256, ArchiveTxPoint>::iterator mi = wallet->mapArcTxs.find(rec->hash);
            if(mi != wallet->mapArcTxs.end())
            {
                return TransactionDesc::toHTML(wallet, rec, unit);
            }
        }
        return QString();
    }

    /**
     * @brief Get raw transaction hex string
     * 
     * @param rec Transaction record
     * @return Hex-encoded raw transaction, or empty string if not found
     */
    QString getTxHex(TransactionRecord *rec)
    {
        LOCK2(cs_main, wallet->cs_wallet);
        std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(rec->hash);
        if(mi != wallet->mapWallet.end())
        {
            std::string strHex = EncodeHexTx(static_cast<CTransaction>(mi->second));
            return QString::fromStdString(strHex);
        }
        return QString();
    }
    
    /**
     * @brief Get parent record by index
     * 
     * @param parentIdx Index of parent in cachedWallet
     * @return Pointer to parent TransactionRecord, or nullptr if invalid
     */
    TransactionRecord* getParentRecord(int parentIdx)
    {
        if (parentIdx >= 0 && parentIdx < cachedWallet.size()) {
            return &cachedWallet[parentIdx];
        }
        return nullptr;
    }
    
    /**
     * @name Fast index-based query methods
     * 
     * These methods use pre-built indexes for O(1) or O(log N) lookups
     * instead of O(N) linear scans of the cache. All indexes point to
     * positions in decomposedTxCache.
     * @{
     */
    
    /**
     * @brief Find transactions within a date range
     * @param from Start timestamp (Unix epoch)
     * @param to End timestamp (Unix epoch)
     * @return List of cache positions matching date range
     */
    QList<int> findByDateRange(qint64 from, qint64 to) const
    {
        QList<int> results;
        auto it = dateIndex.lowerBound(from);
        auto end = dateIndex.upperBound(to);
        while (it != end) {
            results.append(it.value());
            ++it;
        }
        return results;
    }
    
    /**
     * @brief Find all transactions of a specific type
     * @param type Transaction type (Send, Receive, Mined, etc.)
     * @return List of cache positions matching type
     */
    QList<int> findByType(int type) const
    {
        return typeIndex.values(type);
    }
    
    /**
     * @brief Find transactions involving a specific address
     * @param address Address to search (case-insensitive substring match)
     * @return List of cache positions matching address
     */
    QList<int> findByAddress(const QString& address) const
    {
        QList<int> results;
        QString searchLower = address.toLower();
        
        // Search address index
        for (auto it = addressIndex.begin(); it != addressIndex.end(); ++it) {
            if (it.key().contains(searchLower)) {
                results.append(it.value());
            }
        }
        
        return results;
    }
    
    /**
     * @brief Find all watch-only transactions
     * @return List of cache positions for watch-only transactions
     */
    QList<int> findWatchOnly() const
    {
        return watchOnlyIndex.values();
    }
    
    /**
     * @brief Find transactions with amount >= minimum
     * @param minAmount Minimum absolute amount
     * @return List of cache positions matching amount criteria
     */
    QList<int> findByMinAmount(qint64 minAmount) const
    {
        QList<int> results;
        auto it = amountIndex.lowerBound(minAmount);
        while (it != amountIndex.end()) {
            results.append(it.value());
            ++it;
        }
        return results;
    }
    /**@}*/
    
    /**
     * @name Transaction statistics methods
     * @{
     */
    
    /**
     * @brief Count parent transactions in cachedWallet
     * @return Number of parent transactions (excludes children)
     */
    int getTotalTransactionCount() const
    {
        int count = 0;
        for (const TransactionRecord& rec : cachedWallet) {
            if (rec.isParent) count++;
        }
        return count;
    }
    
    /**
     * @brief Get distribution of parent transactions by type
     * @return Map of transaction type to count
     */
    QMap<int, int> getTypeDistribution() const
    {
        QMap<int, int> distribution;
        for (const TransactionRecord& rec : cachedWallet) {
            if (rec.isParent) {
                distribution[rec.type]++;
            }
        }
        return distribution;
    }
    /**@}*/
};

/**
 * @brief Construct transaction table model
 * 
 * Initializes the model and starts loading transaction data:
 * 1. Sets up column headers
 * 2. Calls refreshWallet() to load initial transactions
 * 3. Connects to display unit change signals
 * 4. Subscribes to core wallet signals
 * 
 * @param _platformStyle Platform-specific styling
 * @param _wallet Wallet to display transactions from
 * @param parent Parent WalletModel
 */
TransactionTableModel::TransactionTableModel(const PlatformStyle *_platformStyle, CWallet* _wallet, WalletModel *parent):
        QAbstractTableModel(parent),
        wallet(_wallet),
        walletModel(parent),
        priv(new TransactionTablePriv(_wallet, this)),
        fProcessingQueuedTransactions(false),
        fPendingRefresh(false),
        platformStyle(_platformStyle)
{
    columns << QString() << QString() << tr("Date") << tr("Type") << tr("Label") << KomodoUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());
    priv->refreshWallet();

    connect(walletModel->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

    subscribeToCoreSignals();
}

/**
 * @brief Destructor - cleans up resources
 * 
 * Unsubscribes from core signals and deletes private implementation.
 */
TransactionTableModel::~TransactionTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

/**
 * @brief Update amount column header with current display unit
 * 
 * Called when user changes display unit (ARRR, mARRR, zatoshi).
 * Updates column title to "Amount (ARRR)" format and notifies views.
 */
void TransactionTableModel::updateAmountColumnTitle()
{
    columns[Amount] = KomodoUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());
    Q_EMIT headerDataChanged(Qt::Horizontal,Amount,Amount);
}

/**
 * @brief Set batch processing state for queued transactions
 * 
 * When batch processing completes (value=false) and there's a pending refresh,
 * emits layoutChanged() signal that was deferred during batch processing.
 * This prevents UI freezing during large transaction imports/rescans.
 * 
 * @param value true if batch processing, false if complete
 */
void TransactionTableModel::setProcessingQueuedTransactions(bool value)
{
    fProcessingQueuedTransactions = value;
    
    // When batch processing completes, emit pending refresh if needed
    if (!value && fPendingRefresh) {
        Q_EMIT layoutChanged();
        fPendingRefresh = false;
    }
}

/**
 * @brief Reset all caches and restart lazy loading
 * 
 * Public slot called after operations that invalidate the cache (e.g., rescan).
 * Delegates to TransactionTablePriv::resetAndRestartLazyLoad().
 */
void TransactionTableModel::resetAndRestartLazyLoad()
{
    priv->resetAndRestartLazyLoad();
}

/**
 * @brief Update a single transaction
 * 
 * Called by core when a transaction is added, removed, or modified.
 * 
 * @param hash Transaction ID as hex string
 * @param status Change type (CT_NEW, CT_UPDATED, CT_DELETED)
 * @param showTransaction Whether transaction should be visible
 */
void TransactionTableModel::updateTransaction(const QString &hash, int status, bool showTransaction)
{
    uint256 updated;
    updated.SetHex(hash.toStdString());

    priv->updateWallet(updated, status, showTransaction);
}

/**
 * @brief Invalidate transaction confirmations after new blocks
 * 
 * Called when new blocks arrive. Invalidates Status and ToAddress columns,
 * causing Qt to re-request data for visible rows (confirmation counts update).
 */
void TransactionTableModel::updateConfirmations()
{
    // Blocks came in since last poll.
    // Invalidate status (number of confirmations) and (possibly) description
    //  for all rows. Qt is smart enough to only actually request the data for the
    //  visible rows.
    Q_EMIT dataChanged(index(0, Status), index(priv->size()-1, Status));
    Q_EMIT dataChanged(index(0, ToAddress), index(priv->size()-1, ToAddress));
}

/**
 * @brief Refresh all wallet data from core
 * 
 * Delegates to TransactionTablePriv::refreshWallet() which:
 * - Loads INITIAL_TX_LIMIT transactions for display
 * - Starts lazy loading remaining transactions
 * - Rebuilds caches and indexes
 */
void TransactionTableModel::refreshWallet()
{
    return priv->refreshWallet();
}

/**
 * @brief Rebuild display cache from master cache (no filters)
 * 
 * Copies all records from decomposedTxCache to cachedWallet and sorts.
 * Notifies views of complete model reset.
 */
void TransactionTableModel::rebuildFromCache()
{
    // Notify views that the model is being completely reset
    beginResetModel();
    priv->rebuildFromCache();
    // Notify views that the model reset is complete
    endResetModel();
}

/**
 * @brief Rebuild display cache with filters applied
 * 
 * Applies date, type, amount, and address filters to master cache.
 * 
 * @param dateFrom Start date filter
 * @param dateTo End date filter
 * @param typeFilter Bitmask of transaction types to include
 * @param watchOnlyFilter Watch-only filter mode
 * @param addrPrefix Address prefix to match
 * @param minAmount Minimum transaction amount
 * @param showInactive Show conflicted transactions
 * @param limitParents Maximum parent transactions to display
 */
void TransactionTableModel::rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                                              quint32 typeFilter, int watchOnlyFilter, 
                                              const QString &addrPrefix, qint64 minAmount, 
                                              bool showInactive, int limitParents)
{
    // Notify views that the model is being completely reset
    beginResetModel();
    priv->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitParents);
    // Notify views that the model reset is complete
    endResetModel();
}

/**
 * @brief Toggle expand/collapse state of a parent transaction
 * 
 * Expands or collapses the child records (inputs/outputs/fee) of a parent transaction.
 * Only works for parent records with children (groupCount > 0).
 * 
 * @param idx Model index of the transaction to toggle
 */
void TransactionTableModel::toggleTransactionExpanded(const QModelIndex &idx)
{
    if(!idx.isValid())
        return;
        
    TransactionRecord *rec = static_cast<TransactionRecord*>(idx.internalPointer());
    if(!rec || !rec->isParent || rec->groupCount == 0)
        return;
    
    // Notify that layout is about to change
    Q_EMIT layoutAboutToBeChanged();
    
    // Toggle collapsed state
    rec->collapsed = !rec->collapsed;
    priv->cachedSize = -1; // Invalidate size cache
    
    // Notify that layout has changed
    Q_EMIT layoutChanged();
}

/**
 * @brief Expand or collapse all parent transactions
 * 
 * Sets the collapsed state for all parent transactions in the model.
 * Useful for "Expand All" / "Collapse All" functionality.
 * 
 * @param expanded true to expand all, false to collapse all
 */
void TransactionTableModel::setAllTransactionsExpanded(bool expanded)
{
    // Notify that layout is about to change
    Q_EMIT layoutAboutToBeChanged();
    
    // Set collapsed state for all parent transactions
    for(int i = 0; i < priv->cachedWallet.size(); i++)
    {
        if (priv->cachedWallet[i].isParent)
        {
            priv->cachedWallet[i].collapsed = !expanded;
        }
    }
    
    priv->cachedSize = -1; // Invalidate size cache
    
    // Notify that layout has changed
    Q_EMIT layoutChanged();
}

// Fast index-based query implementations
QList<int> TransactionTableModel::findTransactionsByDateRange(qint64 from, qint64 to) const
{
    return priv->findByDateRange(from, to);
}

QList<int> TransactionTableModel::findTransactionsByType(int type) const
{
    return priv->findByType(type);
}

QList<int> TransactionTableModel::findTransactionsByAddress(const QString& address) const
{
    return priv->findByAddress(address);
}

QList<int> TransactionTableModel::findWatchOnlyTransactions() const
{
    return priv->findWatchOnly();
}

QList<int> TransactionTableModel::findTransactionsByMinAmount(qint64 minAmount) const
{
    return priv->findByMinAmount(minAmount);
}

int TransactionTableModel::getTotalTransactionCount() const
{
    return priv->getTotalTransactionCount();
}

void TransactionTableModel::requestFilteredRebuild(const QDateTime &dateFrom, const QDateTime &dateTo,
                                                    quint32 typeFilter, int watchOnlyFilter,
                                                    const QString &addrPrefix, qint64 minAmount,
                                                    bool showInactive, int limitParents)
{
    if (priv) {
        priv->requestFilteredRebuild(dateFrom, dateTo, typeFilter, watchOnlyFilter,
                                     addrPrefix, minAmount, showInactive, limitParents);
    }
}

QMap<int, int> TransactionTableModel::getTypeDistribution() const
{
    return priv->getTypeDistribution();
}

int TransactionTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int TransactionTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QString TransactionTableModel::formatTxStatus(const TransactionRecord *wtx) const
{
    QString status;
    if (!(wtx->archiveType == ARCHIVED)) {
        switch(wtx->status.status)
        {
        case TransactionStatus::OpenUntilBlock:
            status = tr("Open for %n more block(s)","",wtx->status.open_for);
            break;
        case TransactionStatus::OpenUntilDate:
            status = tr("Open until %1").arg(GUIUtil::dateTimeStr(wtx->status.open_for));
            break;
        case TransactionStatus::Offline:
            status = tr("Offline");
            break;
        case TransactionStatus::Unconfirmed:
            status = tr("Unconfirmed");
            break;
        case TransactionStatus::Abandoned:
            status = tr("Abandoned");
            break;
        case TransactionStatus::Confirming:
            status = tr("Confirming (%1 of %2 recommended confirmations)").arg(wtx->status.depth).arg(TransactionRecord::RecommendedNumConfirmations);
            break;
        case TransactionStatus::Confirmed:
            status = tr("Confirmed (%1 confirmations)").arg(wtx->status.depth);
            break;
        case TransactionStatus::Conflicted:
            status = tr("Conflicted");
            break;
        case TransactionStatus::Immature:
            status = tr("Immature (%1 confirmations, will be available after %2)").arg(wtx->status.depth).arg(wtx->status.depth + wtx->status.matures_in);
            break;
        case TransactionStatus::MaturesWarning:
            status = tr("This block was not received by any other nodes and will probably not be accepted!");
            break;
        case TransactionStatus::NotAccepted:
            status = tr("Generated but not accepted");
            break;
        }
    } else {
        status = tr("Archived");
    }

    return status;
}

QString TransactionTableModel::formatTxDate(const TransactionRecord *wtx) const
{
    if(wtx->time)
    {
        return GUIUtil::dateTimeStr(wtx->time);
    }
    return QString();
}

/* Look up address in address book, if found return label (address)
   otherwise just return (address)
 */
QString TransactionTableModel::lookupAddress(const std::string &address, bool tooltip) const
{
    QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(address));
    QString description;
    if(!label.isEmpty())
    {
        description += label;
    }
    if(label.isEmpty() || tooltip)
    {
        description += QString(" (") + QString::fromStdString(address) + QString(")");
    }
    return description;
}

/**
 * @brief Format transaction type for display
 * 
 * Returns user-friendly type strings with special handling for parent-child hierarchy:
 * - Parent records: "Send", "Receive", "Mined", etc.
 * - Child records: Indented "Input", "Output", "Fee" with context-aware labels
 *   (e.g., "Sent To" vs "Change" for outputs based on parent type)
 * 
 * @param wtx Transaction record
 * @return Formatted type string
 */
QString TransactionTableModel::formatTxType(const TransactionRecord *wtx) const
{
    // Child records show Input or Output with indentation
    if (wtx->isChild) {
        // Check parent type for special labeling
        TransactionRecord::Type parentType = TransactionRecord::Other;
        TransactionRecord* parentRec = priv->getParentRecord(wtx->parentIdx);
        if (parentRec) {
            parentType = parentRec->type;
        }
        
        switch(wtx->type)
        {
        case TransactionRecord::Input:
            return QString("    ") + tr("Input");
        case TransactionRecord::Output:
            // For receive transactions, label as "Received In"
            if (parentType == TransactionRecord::RecvWithAddress || 
                parentType == TransactionRecord::RecvWithAddressWithMemo ||
                parentType == TransactionRecord::RecvFromOther) {
                return QString("    ") + tr("Received In");
            }
            // For internal transfers, label outputs as "Transfer To"
            if (parentType == TransactionRecord::SendToSelf || 
                parentType == TransactionRecord::SendToSelfWithMemo) {
                return QString("    ") + tr("Transfered To");
            }
            // For send transactions, distinguish between external sends and change
            if (parentType == TransactionRecord::SendToAddress || 
                parentType == TransactionRecord::SendToAddressWithMemo ||
                parentType == TransactionRecord::SendToOther) {
                if (wtx->involvesOwnAddress) {
                    return QString("    ") + tr("Change");
                } else {
                    return QString("    ") + tr("Sent To");
                }
            }
            // For mined transactions, label as "Mined In"
            if (parentType == TransactionRecord::Generated) {
                return QString("    ") + tr("Mined In");
            }
            return QString("    ") + tr("Output");
        case TransactionRecord::Fee:
            return QString("    ") + tr("Fee");
        default:
            return QString("    ") + tr("(n/a)");
        }
    }
    
    // Parent records show transaction type
    switch(wtx->type)
    {
    case TransactionRecord::RecvWithAddress:
        return tr("Receive");
    case TransactionRecord::RecvWithAddressWithMemo:
        return tr("Receive with Memo");
    case TransactionRecord::RecvFromOther:
        return tr("Receive");
    case TransactionRecord::SendToAddress:
        return tr("Send");
    case TransactionRecord::SendToAddressWithMemo:
        return tr("Send with Memo");
    case TransactionRecord::SendToOther:
        return tr("Send");
    case TransactionRecord::SendToSelf:
        return tr("Internal Transfer");
    case TransactionRecord::SendToSelfWithMemo:
        return tr("Internal Transfer with Memo");
    case TransactionRecord::Generated:
        return tr("Mined");
    case TransactionRecord::Other:
        return tr("Unknown");
    default:
        return QString();
    }
}

QVariant TransactionTableModel::txAddressDecoration(const TransactionRecord *wtx) const
{
    switch(wtx->type)
    {
    case TransactionRecord::Generated:
        return QIcon(":/icons/tx_mined");
    case TransactionRecord::RecvWithAddress:
        return QIcon(":/icons/tx_input");
    case TransactionRecord::RecvWithAddressWithMemo:
        return QIcon(":/icons/tx_input");
    case TransactionRecord::RecvFromOther:
        return QIcon(":/icons/tx_input");
    case TransactionRecord::SendToAddress:
        return QIcon(":/icons/tx_output");
    case TransactionRecord::SendToAddressWithMemo:
        return QIcon(":/icons/tx_output");
    case TransactionRecord::SendToOther:
        return QIcon(":/icons/tx_output");
    default:
        return QIcon(":/icons/tx_inout");
    }
}

/**
 * @brief Format transaction address/description for display
 * 
 * Handles hierarchical display:
 * - Parent records: Show "Txid: {hash}"
 * - Child records: Show indented address with note count (e.g., "    addr (3 notes)")
 * - Fee records: Show "    Transaction Fee"
 * 
 * @param wtx Transaction record
 * @param tooltip true to include watch-only indicator
 * @return Formatted address/description string
 */
QString TransactionTableModel::formatTxToAddress(const TransactionRecord *wtx, bool tooltip) const
{
    QString watchAddress;
    if (tooltip) {
        // Mark transactions involving watch-only addresses by adding " (watch-only)"
        watchAddress = wtx->involvesWatchAddress ? QString(" (") + tr("watch-only") + QString(")") : "";
    }

    // Handle hierarchical parent/child structure
    if (wtx->isParent) {
        // Parent shows transaction ID with prefix
        QString txid = QString::fromStdString(wtx->hash.ToString());
        return QString("Txid: ") + txid;
    } else if (wtx->isChild) {
        // Special case for Fee records
        if (wtx->type == TransactionRecord::Fee) {
            return QString("    Transaction Fee");
        }
        // Child shows full address with note count, indented
        QString addr = QString::fromStdString(wtx->address);
        if (wtx->groupCount > 1) {
            return QString("    %1 (%2 notes)").arg(addr).arg(wtx->groupCount) + watchAddress;
        } else {
            return QString("    %1 (1 note)").arg(addr) + watchAddress;
        }
    }

    // Original behavior for non-hierarchical records
    switch(wtx->type)
    {
    case TransactionRecord::RecvFromOther:
        return QString::fromStdString(wtx->address) + watchAddress;
    case TransactionRecord::RecvWithAddress:
        return QString::fromStdString(wtx->address) + watchAddress;
    case TransactionRecord::RecvWithAddressWithMemo:
        return QString::fromStdString(wtx->address) + watchAddress;
    case TransactionRecord::SendToAddress:
        return QString::fromStdString(wtx->address);
    case TransactionRecord::SendToAddressWithMemo:
        return QString::fromStdString(wtx->address);
    case TransactionRecord::Generated:
        return QString::fromStdString(wtx->address);
    case TransactionRecord::SendToOther:
        return QString::fromStdString(wtx->address);
    case TransactionRecord::SendToSelf:
        return QString::fromStdString(wtx->address) + watchAddress;
    case TransactionRecord::SendToSelfWithMemo:
        return QString::fromStdString(wtx->address) + watchAddress;
    default:
        return tr("(n/a)") + watchAddress;
    }
}

/**
 * @brief Get address color (currently disabled)
 * 
 * Returns black for all addresses. Previous implementation showed
 * bare addresses (no label) in a different color, but this is disabled.
 * 
 * @param wtx Transaction record
 * @return COLOR_BLACK for all addresses
 */
QVariant TransactionTableModel::addressColor(const TransactionRecord *wtx) const
{
    Q_UNUSED(wtx);
    return COLOR_BLACK;
}

/**
 * @brief Format transaction amount for display
 * 
 * Amount display logic:
 * - Parent records: Show netChange (total effect on balance)
 * - Child records: Show individual input/output/fee amount
 * - Unconfirmed amounts shown in brackets [amount]
 * 
 * @param wtx Transaction record
 * @param showUnconfirmed true to bracket unconfirmed amounts
 * @param separators Separator style for formatting
 * @return Formatted amount string
 */
QString TransactionTableModel::formatTxAmount(const TransactionRecord *wtx, bool showUnconfirmed, KomodoUnits::SeparatorStyle separators) const
{
    CAmount amount;
    
    if (wtx->isParent) {
        // Parent record shows total (sum of all inputs and outputs)
        amount = wtx->netChange;
    } else if (wtx->isChild) {
        // Child record shows individual amount
        amount = wtx->credit + wtx->debit;
    } else {
        // Original behavior
        amount = wtx->credit + wtx->debit;
    }
    
    QString str = KomodoUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), amount, false, separators);
    
    if(showUnconfirmed && !(wtx->archiveType == ARCHIVED))
    {
        if(!wtx->status.countsForBalance)
        {
            str = QString("[") + str + QString("]");
        }
    }
    return QString(str);
}

QVariant TransactionTableModel::txStatusDecoration(const TransactionRecord *wtx) const
{
    if (!(wtx->archiveType == ARCHIVED)) {
        switch(wtx->status.status)
        {
        case TransactionStatus::OpenUntilBlock:
        case TransactionStatus::OpenUntilDate:
            return COLOR_TX_STATUS_OPENUNTILDATE;
        case TransactionStatus::Offline:
            return COLOR_TX_STATUS_OFFLINE;
        case TransactionStatus::Unconfirmed:
            return QIcon(":/icons/transaction_0");
        case TransactionStatus::Abandoned:
            return QIcon(":/icons/transaction_abandoned");
        case TransactionStatus::Confirming:
            switch(wtx->status.depth)
            {
            case 1: return QIcon(":/icons/transaction_1");
            case 2: return QIcon(":/icons/transaction_2");
            case 3: return QIcon(":/icons/transaction_3");
            case 4: return QIcon(":/icons/transaction_4");
            default: return QIcon(":/icons/transaction_5");
            };
        case TransactionStatus::Confirmed:
            return QIcon(":/icons/transaction_confirmed");
        case TransactionStatus::Conflicted:
            return QIcon(":/icons/transaction_conflicted");
        case TransactionStatus::Immature: {
            int total = wtx->status.depth + wtx->status.matures_in;
            int part = (wtx->status.depth * 4 / total) + 1;
            return QIcon(QString(":/icons/transaction_%1").arg(part));
            }
        case TransactionStatus::MaturesWarning:
        case TransactionStatus::NotAccepted:
            return QIcon(":/icons/transaction_0");
        default:
            return COLOR_BLACK;
        }
    }
    return COLOR_BLACK;
}

QVariant TransactionTableModel::txWatchonlyDecoration(const TransactionRecord *wtx) const
{
    if (wtx->involvesWatchAddress)
        return QIcon(":/icons/eye");
    else
        return QVariant();
}

QString TransactionTableModel::formatTooltip(const TransactionRecord *rec) const
{
    QString tooltip = formatTxStatus(rec) + QString("\n") + formatTxType(rec);
    if(rec->type==TransactionRecord::RecvFromOther || rec->type==TransactionRecord::SendToOther ||
       rec->type==TransactionRecord::SendToAddress || rec->type==TransactionRecord::RecvWithAddress ||
       rec->type==TransactionRecord::SendToAddressWithMemo || rec->type==TransactionRecord::RecvWithAddressWithMemo)
    {
        tooltip += QString(" ") + formatTxToAddress(rec, true);
    }
    return tooltip;
}

/**
 * @brief Get data for a model index and role
 * 
 * Implements Qt Model/View data() method. Handles multiple roles:
 * - DisplayRole: User-visible formatted data
 * - EditRole: Unformatted data for sorting
 * - DecorationRole: Icons and colors
 * - Custom roles: TypeRole, DateRole, AmountRole, etc.
 * 
 * Special handling:
 * - Parent/child hierarchy affects Amount column (netChange vs individual)
 * - Amount colors: red for negative, green for positive (theme-aware)
 * - Child records use italic font
 * - External sends (non-wallet outputs) shown in white
 * 
 * @param index Model index (row/column)
 * @param role Data role to retrieve
 * @return QVariant containing requested data
 */
QVariant TransactionTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();
    
    // Bounds check
    if(index.row() < 0 || index.row() >= priv->size())
        return QVariant();
        
    TransactionRecord *rec = static_cast<TransactionRecord*>(index.internalPointer());
    
    // Additional null check
    if(!rec)
        return QVariant();

    switch(role)
    {
    case RawDecorationRole:
        switch(index.column())
        {
        case Status:
            return txStatusDecoration(rec);
        case Watchonly:
            return txWatchonlyDecoration(rec);
        case ToAddress:
            // Show expand/collapse icon for parent records
            if (rec->isParent && rec->groupCount > 0) {
                return rec->collapsed ? QIcon(":/icons/tx_output") : QIcon(":/icons/tx_input");
            }
            return txAddressDecoration(rec);
        }
        break;
    case Qt::DecorationRole:
    {
        QIcon icon = qvariant_cast<QIcon>(index.data(RawDecorationRole));
        return platformStyle->TextColorIcon(icon);
    }
    case Qt::DisplayRole:
        switch(index.column())
        {
        case Date:
            return formatTxDate(rec);
        case Type:
            return formatTxType(rec);
        case ToAddress:
            return formatTxToAddress(rec, false);
        case Amount:
            return formatTxAmount(rec, true, KomodoUnits::separatorAlways);
        }
        break;
    case Qt::EditRole:
        // Edit role is used for sorting, so return the unformatted values
        switch(index.column())
        {
        case Status:
            return QString::fromStdString(rec->status.sortKey);
        case Date:
            return rec->time;
        case Type:
            return formatTxType(rec);
        case Watchonly:
            return (rec->involvesWatchAddress ? 1 : 0);
        case ToAddress:
            return formatTxToAddress(rec, true);
        case Amount:
            return qint64(rec->credit + rec->debit);
        }
        break;
    case Qt::FontRole:
        if(rec->isChild)
        {
            QFont font;
            font.setItalic(true);
            return font;
        }
        break;
    case Qt::ToolTipRole:
        return formatTooltip(rec);
    case Qt::TextAlignmentRole:
        return column_alignments[index.column()];
    case Qt::ForegroundRole:
        // Child record addresses shown in light blue for readability
        if(rec->isChild && index.column() == ToAddress)
        {
            return QColor(100, 180, 255);
        }
        
        // Amount column: theme-aware red/green coloring
        if(index.column() == Amount)
        {
            QSettings settings;
            CAmount amount;
            
            // Determine amount to evaluate (parent uses netChange, child uses individual)
            if (rec->isParent) {
                amount = rec->netChange;
            } else {
                amount = rec->credit + rec->debit;
            }
            
            // External sends (non-wallet outputs) shown in white
            if (rec->isChild && rec->type == TransactionRecord::Output && !rec->involvesOwnAddress)
            {
                return QColor(Qt::white);
            }
            
            // Theme-aware color selection for negative/positive amounts
            QString theme = settings.value("strTheme", "pirate").toString();
            
            if(amount < 0)
            {
                // Negative amounts (outgoing): Dark themes use dark red, light themes use standard red
                if (theme == "dark" || theme == "pirate" || theme == "piratemap" || 
                    theme == "armada" || theme == "treasure" || theme == "treasuremap" || 
                    theme == "ghostship" || theme == "night") {
                    return COLOR_NEGATIVE_DARK;
                } else if (theme == "pirateship") {
                    return COLOR_NEGATIVE;
                } else {
                    return COLOR_NEGATIVE;
                }
            }
            else // amount >= 0
            {
                // Positive/zero amounts (incoming): Most themes use pirate green, dark theme uses dark variant
                if (theme == "dark") {
                    return COLOR_POSITIVE_DARK;
                } else {
                    return COLOR_POSITIVE_PIRATE;
                }
            }
        }
        break;
    case TypeRole:
        return rec->type;
    case DateRole:
        return QDateTime::fromTime_t(static_cast<uint>(rec->time));
    case WatchonlyRole:
        return rec->involvesWatchAddress;
    case WatchonlyDecorationRole:
        return txWatchonlyDecoration(rec);
    case LongDescriptionRole:
        return priv->describe(rec, walletModel->getOptionsModel()->getDisplayUnit());
    case MemoDescriptionRole:
        return QString::fromStdString("Memo:<br>" + rec->memo + "<br><br>Memo Hex:<br>" + rec->memohex);
    case AddressRole:
        return QString::fromStdString(rec->address);
    case LabelRole:
        return walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));
    case AmountRole:
        return qint64(rec->credit + rec->debit);
    case TxIDRole:
        return rec->getTxID();
    case TxHashRole:
        return QString::fromStdString(rec->hash.ToString());
    case TxHexRole:
        return priv->getTxHex(rec);
    case TxPlainTextRole:
        {
            QString details;
            QDateTime date = QDateTime::fromTime_t(static_cast<uint>(rec->time));
            QString txLabel = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(rec->address));

            details.append(date.toString("M/d/yy HH:mm"));
            details.append(" ");
            details.append(formatTxStatus(rec));
            details.append(". ");
            if(!formatTxType(rec).isEmpty()) {
                details.append(formatTxType(rec));
                details.append(" ");
            }
            if(!rec->address.empty()) {
                if(txLabel.isEmpty())
                    details.append(tr("(no label)") + " ");
                else {
                    details.append("(");
                    details.append(txLabel);
                    details.append(") ");
                }
                details.append(QString::fromStdString(rec->address));
                details.append(" ");
            }
            details.append(formatTxAmount(rec, false, KomodoUnits::separatorNever));
            return details;
        }
    case ConfirmedRole:
        if (rec->archiveType == ARCHIVED) {
            return true;
        }
        return rec->status.countsForBalance;
    case FormattedAmountRole:
        // Used for copy/export, so don't include separators
        return formatTxAmount(rec, false, KomodoUnits::separatorNever);
    case StatusRole:
        return rec->status.status;
    case IsParentRole:
        return rec->isParent;
    case IsChildRole:
        return rec->isChild;
    case IdxRole:
        return rec->idx;
    case ParentTypeRole:
        // For child records, return the parent's type for filtering
        if (rec->isChild && rec->parentIdx >= 0) {
            TransactionRecord *parentRec = priv->index(priv->actualIndexToVisibleIndex(rec->parentIdx));
            if (parentRec && parentRec->isParent) {
                return parentRec->type;
            }
        }
        // For parent records or if parent not found, return own type
        return rec->type;
    }
    return QVariant();
}

/**
 * @brief Get header data for columns
 * 
 * Returns column titles, alignment, and tooltips for table headers.
 * 
 * @param section Column index
 * @param orientation Qt::Horizontal or Qt::Vertical
 * @param role Data role (DisplayRole, TextAlignmentRole, ToolTipRole)
 * @return Header data as QVariant
 */
QVariant TransactionTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        // Validate section index
        if(section < 0 || section >= columns.length())
            return QVariant();
            
        if(role == Qt::DisplayRole)
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        } else if (role == Qt::ToolTipRole)
        {
            switch(section)
            {
            case Status:
                return tr("Transaction status. Hover over this field to show number of confirmations.");
            case Date:
                return tr("Date and time that the transaction was received.");
            case Type:
                return tr("Type of transaction.");
            case Watchonly:
                return tr("Whether or not a watch-only address is involved in this transaction.");
            case ToAddress:
                return tr("User-defined intent/purpose of the transaction.");
            case Amount:
                return tr("Amount removed from or added to balance.");
            }
        }
    }
    return QVariant();
}

/**
 * @brief Create model index for given row and column
 * 
 * Standard Qt Model/View method. Bounds-checks row/column and returns
 * a QModelIndex with pointer to the TransactionRecord.
 * 
 * @param row Visible row index
 * @param column Column index
 * @param parent Parent index (unused, model is flat)
 * @return QModelIndex for the cell, or invalid index if out of bounds
 */
QModelIndex TransactionTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    
    // Bounds check
    if(row < 0 || row >= priv->size() || column < 0 || column >= columns.length())
        return QModelIndex();
    
    TransactionRecord *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    return QModelIndex();
}

/**
 * @brief Update display when unit changes (ARRR, mARRR, zatoshi)
 * 
 * Called when user changes display unit in options. Updates Amount column
 * header title and invalidates Amount column data to trigger re-rendering.
 */
void TransactionTableModel::updateDisplayUnit()
{
    // emit dataChanged to update Amount column with the current unit
    updateAmountColumnTitle();
    Q_EMIT dataChanged(index(0, Amount), index(priv->size()-1, Amount));
}

/**
 * @brief Helper struct for queuing transaction change notifications
 * 
 * Used during batch operations (e.g., wallet rescan) to queue notifications
 * and process them after completion, preventing UI freezing.
 */
struct TransactionNotification
{
public:
    TransactionNotification() {}
    TransactionNotification(uint256 _hash, ChangeType _status, bool _showTransaction):
        hash(_hash), status(_status), showTransaction(_showTransaction) {}

    void invoke(QObject *ttm)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        QMetaObject::invokeMethod(ttm, "updateTransaction", Qt::QueuedConnection,
                                  Q_ARG(QString, strHash),
                                  Q_ARG(int, status),
                                  Q_ARG(bool, showTransaction));
    }
private:
    uint256 hash;
    ChangeType status;
    bool showTransaction;
};

static bool fQueueNotifications = false;
static std::vector< TransactionNotification > vQueueNotifications;

/**
 * @brief Callback from wallet core when transaction changes
 * 
 * Called by wallet when transaction is added/updated/deleted. Determines
 * visibility and queues notification or invokes immediately.
 * 
 * @param ttm TransactionTableModel to notify
 * @param wallet Wallet containing the transaction
 * @param hash Transaction ID
 * @param status Change type (CT_NEW, CT_UPDATED, CT_DELETED)
 */
static void NotifyTransactionChanged(TransactionTableModel *ttm, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    // Find transaction in wallet
    std::map<uint256, CWalletTx>::iterator mi = wallet->mapWallet.find(hash);
    // Determine whether to show transaction or not (determine this here so that no relocking is needed in GUI thread)
    bool inWallet = mi != wallet->mapWallet.end();
    bool showTransaction = (inWallet && TransactionRecord::showTransaction(mi->second));

    TransactionNotification notification(hash, status, showTransaction);

    if (fQueueNotifications)
    {
        vQueueNotifications.push_back(notification);
        return;
    }
    notification.invoke(ttm);
}

/**
 * @brief Callback for wallet operation progress (rescan, etc.)
 * 
 * Handles transaction notification queuing during long operations:
 * - nProgress=0: Start queuing notifications
 * - nProgress=100: Stop queuing, process queued notifications OR reset cache
 * 
 * For rescan operations, instead of processing thousands of individual
 * transaction notifications, we skip them and trigger a complete cache
 * reset and lazy reload. This is much more efficient than updating
 * each transaction individually.
 * 
 * @param ttm TransactionTableModel to notify
 * @param title Operation title (e.g., "Rescanning...")
 * @param nProgress Progress percentage (0-100)
 */
static void ShowProgress(TransactionTableModel *ttm, const std::string &title, int nProgress)
{
    if (nProgress == 0)
        fQueueNotifications = true;

    if (nProgress == 100)
    {
        fQueueNotifications = false;
        
        // Check if this is a rescan operation
        bool isRescan = (title == "Rescanning..." || title.find("Rescan") != std::string::npos);
        
        if (isRescan) {
            // For rescan: skip processing individual notifications, just reset everything
            // Clear queued notifications without processing them
            std::vector<TransactionNotification >().swap(vQueueNotifications);
            
            // Trigger complete cache reset and lazy reload
            QMetaObject::invokeMethod(ttm, "resetAndRestartLazyLoad", Qt::QueuedConnection);
        } else {
            // For other operations: process queued notifications normally
            if (vQueueNotifications.size() > 10) // prevent balloon spam, show maximum 10 balloons
                QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, true));
            for (unsigned int i = 0; i < vQueueNotifications.size(); ++i)
            {
                if (vQueueNotifications.size() - i <= 10)
                    QMetaObject::invokeMethod(ttm, "setProcessingQueuedTransactions", Qt::QueuedConnection, Q_ARG(bool, false));

                vQueueNotifications[i].invoke(ttm);
            }
            std::vector<TransactionNotification >().swap(vQueueNotifications); // clear
        }
    }
}

/**
 * @brief Connect to wallet core signals
 * 
 * Registers callbacks for:
 * - NotifyTransactionChanged: Transaction add/update/delete events
 * - ShowProgress: Long operation progress (rescan, etc.)
 */
void TransactionTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2));
}

/**
 * @brief Disconnect from wallet core signals
 * 
 * Called in destructor to clean up signal connections.
 */
void TransactionTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2));
}
