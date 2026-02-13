// Copyright (c) 2011-2016 The Bitcoin Core developers
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

// Amount column is right-aligned it contains numbers
static int column_alignments[] = {
        Qt::AlignLeft|Qt::AlignVCenter, /* status */
        Qt::AlignLeft|Qt::AlignVCenter, /* watchonly */
        Qt::AlignLeft|Qt::AlignVCenter, /* date */
        Qt::AlignLeft|Qt::AlignVCenter, /* type */
        Qt::AlignLeft|Qt::AlignVCenter, /* address */
        Qt::AlignRight|Qt::AlignVCenter /* amount */
    };

// Comparison operator for sort/binary search of model tx list
struct TxLessThan
{
    bool operator()(const TransactionRecord &a, const TransactionRecord &b) const
    {
        // First sort by hash
        if (a.hash != b.hash) {
            return a.hash < b.hash;
        }
        // For same hash, parent comes before children
        if (a.isParent && !b.isParent) {
            return true;
        }
        if (!a.isParent && b.isParent) {
            return false;
        }
        // For children with same hash, sort by idx
        // CRITICAL: Use row numbers as absolute tiebreaker to prevent
        // descending sort from reversing parent-child order
        if (a.idx != b.idx) {
            return a.idx < b.idx;
        }
        // If everything else is equal, use memory address as last resort
        return &a < &b;
    }
    bool operator()(const TransactionRecord &a, const uint256 &b) const
    {
        return a.hash < b;
    }
    bool operator()(const uint256 &a, const TransactionRecord &b) const
    {
        return a < b.hash;
    }
};

// Private implementation
class TransactionTablePriv
{
public:
    TransactionTablePriv(CWallet *_wallet, TransactionTableModel *_parent) :
        wallet(_wallet),
        parent(_parent),
        cachedSize(-1),
        lazyLoadInProgress(false),
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

    /* Local cache of wallet.
     * As it is in the same order as the CWallet, by definition
     * this is sorted by sha256.
     */
    QList<TransactionRecord> cachedWallet;
    
    // Cached size to avoid recalculating on every call
    mutable int cachedSize;
    
    // Lazy loading state
    bool lazyLoadInProgress;
    int lazyLoadPosition;
    int lazyLoadTotalSize; // Total number of transactions to load
    QTimer* lazyLoadTimer;
    
    // Decomposed transaction cache - mirrors mapWallet/mapArcTxs
    // Key: transaction hash (as QString), Value: list of decomposed records
    QMap<QString, QList<TransactionRecord>> decomposedTxCache;
    
    // Indexes for fast filtering and searching
    QMultiMap<qint64, int> dateIndex;        // date -> cachedWallet index
    QMultiMap<int, int> typeIndex;           // TransactionRecord::Type -> index
    QMultiMap<QString, int> addressIndex;    // address -> index
    QSet<int> watchOnlyIndex;                // indices of watch-only transactions
    QMultiMap<qint64, int> amountIndex;      // amount -> index
    
    /* Build indexes for fast filtering and searching */
    void buildIndexes()
    {
        LogPrintf("Building transaction indexes...\n");
        
        // Clear existing indexes
        dateIndex.clear();
        typeIndex.clear();
        addressIndex.clear();
        watchOnlyIndex.clear();
        amountIndex.clear();
        
        // Build indexes from cachedWallet
        for(int i = 0; i < cachedWallet.size(); i++)
        {
            const TransactionRecord& rec = cachedWallet[i];
            
            // Date index
            dateIndex.insert(rec.time, i);
            
            // Type index
            typeIndex.insert(rec.type, i);
            
            // Address index
            if (!rec.address.empty()) {
                addressIndex.insert(QString::fromStdString(rec.address).toLower(), i);
            }
            
            // Watch-only index
            if (rec.involvesWatchAddress) {
                watchOnlyIndex.insert(i);
            }
            
            // Amount index (use absolute value for range queries)
            qint64 absAmount = qAbs(rec.credit + rec.debit);
            amountIndex.insert(absAmount, i);
            
            // Update UI every 500 records
            if (i % 500 == 0 && i > 0) {
                QApplication::processEvents();
            }
        }
        
        LogPrintf("Indexes built: %d transactions indexed\n", cachedWallet.size());
        LogPrintf("Index statistics:\n");
        LogPrintf("  Date entries: %d\n", dateIndex.size());
        LogPrintf("  Type entries: %d\n", typeIndex.size());
        LogPrintf("  Address entries: %d\n", addressIndex.size());
        LogPrintf("  Watch-only count: %d\n", watchOnlyIndex.size());
        LogPrintf("  Amount entries: %d\n", amountIndex.size());
    }
    
    /* Shift all index values >= fromIndex by delta (used after insertions/deletions) */
    void shiftIndexValues(int fromIndex, int delta)
    {
        // Shift dateIndex
        QList<QPair<qint64, int>> dateUpdates;
        for (QMultiMap<qint64, int>::iterator it = dateIndex.begin(); it != dateIndex.end(); ++it) {
            if (it.value() >= fromIndex) {
                dateUpdates.append(qMakePair(it.key(), it.value()));
            }
        }
        for (int i = 0; i < dateUpdates.size(); ++i) {
            dateIndex.remove(dateUpdates[i].first, dateUpdates[i].second);
            dateIndex.insert(dateUpdates[i].first, dateUpdates[i].second + delta);
        }

        // Shift typeIndex
        QList<QPair<int, int>> typeUpdates;
        for (QMultiMap<int, int>::iterator it = typeIndex.begin(); it != typeIndex.end(); ++it) {
            if (it.value() >= fromIndex) {
                typeUpdates.append(qMakePair(it.key(), it.value()));
            }
        }
        for (int i = 0; i < typeUpdates.size(); ++i) {
            typeIndex.remove(typeUpdates[i].first, typeUpdates[i].second);
            typeIndex.insert(typeUpdates[i].first, typeUpdates[i].second + delta);
        }

        // Shift addressIndex
        QList<QPair<QString, int>> addressUpdates;
        for (QMultiMap<QString, int>::iterator it = addressIndex.begin(); it != addressIndex.end(); ++it) {
            if (it.value() >= fromIndex) {
                addressUpdates.append(qMakePair(it.key(), it.value()));
            }
        }
        for (int i = 0; i < addressUpdates.size(); ++i) {
            addressIndex.remove(addressUpdates[i].first, addressUpdates[i].second);
            addressIndex.insert(addressUpdates[i].first, addressUpdates[i].second + delta);
        }

        // Shift amountIndex
        QList<QPair<qint64, int>> amountUpdates;
        for (QMultiMap<qint64, int>::iterator it = amountIndex.begin(); it != amountIndex.end(); ++it) {
            if (it.value() >= fromIndex) {
                amountUpdates.append(qMakePair(it.key(), it.value()));
            }
        }
        for (int i = 0; i < amountUpdates.size(); ++i) {
            amountIndex.remove(amountUpdates[i].first, amountUpdates[i].second);
            amountIndex.insert(amountUpdates[i].first, amountUpdates[i].second + delta);
        }
        
        // Shift watch-only set
        QSet<int> newWatchOnlyIndex;
        for (int idx : watchOnlyIndex) {
            if (idx >= fromIndex) {
                newWatchOnlyIndex.insert(idx + delta);
            } else {
                newWatchOnlyIndex.insert(idx);
            }
        }
        watchOnlyIndex = newWatchOnlyIndex;
    }
    
    /* Start lazy loading of remaining transactions */
    void startLazyLoad()
    {
        if (lazyLoadInProgress) {
            LogPrintf("Lazy load already in progress\n");
            return;
        }
        
        lazyLoadInProgress = true;
        lazyLoadPosition = 0;
        
        // Calculate total size once at the start for accurate progress tracking
        {
            LOCK2(cs_main, wallet->cs_wallet);
            std::map<std::pair<int,int>, uint256> sortedArchive;
            std::set<uint256> addedTxids;
            
            // Add from mapArcTxs
            for (map<uint256, ArchiveTxPoint>::iterator it = wallet->mapArcTxs.begin(); it != wallet->mapArcTxs.end(); ++it) {
                const ArchiveTxPoint& arcTxPt = it->second;
                if (!arcTxPt.hashBlock.IsNull() && mapBlockIndex.count(arcTxPt.hashBlock)) {
                    const CBlockIndex* pindex = mapBlockIndex[arcTxPt.hashBlock];
                    sortedArchive[make_pair(pindex->nHeight, arcTxPt.nIndex)] = it->first;
                    addedTxids.insert(it->first);
                }
            }
            
            // Add from mapWallet (unconfirmed)
            int nPosUnconfirmed = 0;
            for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it) {
                CWalletTx wtx = (*it).second;
                uint256 txid = wtx.GetHash();
                if (addedTxids.count(txid) == 0 && wtx.GetDepthInMainChain() == 0) {
                    sortedArchive[make_pair(chainActive.Tip()->nHeight + 1, nPosUnconfirmed)] = txid;
                    nPosUnconfirmed++;
                }
            }
            
            lazyLoadTotalSize = sortedArchive.size();
            LogPrintf("Lazy load will process %d total transactions\n", lazyLoadTotalSize);
        }
        
        LogPrintf("Starting lazy load of remaining transactions...\n");
        
        // Create timer if not already created
        if (!lazyLoadTimer) {
            lazyLoadTimer = new QTimer();
            lazyLoadTimer->setInterval(100); // 100ms between batches
            // Connect timer to lazy load slot
            QObject::connect(lazyLoadTimer, &QTimer::timeout, [this]() {
                this->lazyLoadBatch();
            });
        }
        
        lazyLoadTimer->start();
    }
    
    /* Load a batch of transactions in background */
    void lazyLoadBatch()
    {
        const int BATCH_SIZE = 50; // Load 50 transactions per batch
        bool fIncludeWatchonly = true;
        
        QList<RpcArcTransaction> arcTxList;
        int startPos = lazyLoadPosition;
        int endPos = 0;
        int processedCount = 0; // Track how many new transactions we processed
        bool reachedEnd = true; // Track if we reached the end of the archive
        
        {
            LOCK2(cs_main, wallet->cs_wallet);
            
            // Build sorted archive from both mapArcTxs and mapWallet
            // Track txids to avoid duplicates (some transactions may exist in both)
            std::map<std::pair<int,int>, uint256> sortedArchive;
            std::set<uint256> addedTxids; // Track which txids we've already added
            
            // Add from mapArcTxs first - track what's being filtered
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
                addedTxids.insert(txid);
                addedArcTxs++;
            }
            
            LogPrintf("Lazy load: mapArcTxs has %d transactions. Added %d, Skipped: nullBlock=%d, blockNotInIndex=%d\n",
                     totalArcTxs, addedArcTxs, nullBlockHash, blockNotInIndex);

            // Add from mapWallet, skipping duplicates
            int nPosUnconfirmed = 0;
            for (map<uint256, CWalletTx>::iterator it = wallet->mapWallet.begin(); it != wallet->mapWallet.end(); ++it) {
                CWalletTx wtx = (*it).second;
                uint256 txid = wtx.GetHash();
                
                // Skip if already added from mapArcTxs
                if (addedTxids.count(txid) > 0) {
                    continue;
                }
                
                std::pair<int,int> key;

                if (wtx.GetDepthInMainChain() == 0) {
                    key = make_pair(chainActive.Tip()->nHeight + 1,  nPosUnconfirmed);
                    sortedArchive[key] = txid;
                    nPosUnconfirmed++;
                } else if (!wtx.hashBlock.IsNull() && mapBlockIndex.count(wtx.hashBlock) > 0) {
                    key = make_pair(mapBlockIndex[wtx.hashBlock]->nHeight, wtx.nIndex);
                    sortedArchive[key] = txid;
                } else {
                    key = make_pair(chainActive.Tip()->nHeight + 1,  nPosUnconfirmed);
                    sortedArchive[key] = txid;
                    nPosUnconfirmed++;
                }
                addedTxids.insert(txid);
            }
            
            // Get total archive size for this batch (for logging only)
            int totalArchiveSize = sortedArchive.size();
            
            // Skip to our position and load batch
            int currentPos = 0;
            reachedEnd = true; // Assume we'll reach the end unless we break early
            
            for (map<std::pair<int,int>, uint256>::reverse_iterator it = sortedArchive.rbegin(); it != sortedArchive.rend(); ++it)
            {
                // Always increment position - we're iterating through the archive
                currentPos++;
                
                if (currentPos <= startPos) {
                    continue;
                }
                
                uint256 txid = (*it).second;
                
                // Skip if already in cache, but don't count toward batch size
                if (decomposedTxCache.contains(QString::fromStdString(txid.ToString()))) {
                    continue;
                }
                
                // Stop after processing BATCH_SIZE new transactions (but mark that we didn't reach the end)
                if (processedCount >= BATCH_SIZE) {
                    reachedEnd = false;
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
                processedCount++; // Count this as processed
            }
            
            endPos = currentPos;
            
            // Store completion flag for use outside lock
            LogPrintf("Lazy load batch: processed %d/%d transactions, at position %d/%d, reached end: %s\n",
                     processedCount, arcTxList.size(), currentPos, totalArchiveSize, reachedEnd ? "yes" : "no");
        }
        
        // Process batch outside of locks
        // Cache transactions AND add to indexes for filtering (but NOT to cachedWallet for display)
        int cached = 0;
        int indexed = 0;
        
        for (const RpcArcTransaction& arcTx : arcTxList) {
            QString txHashStr = QString::fromStdString(arcTx.txid.ToString());
            
            // Skip transactions that couldn't be loaded (category "not found" means getRpcArcTx returned early)
            if (arcTx.category == "not found") {
                continue;
            }
            
            // Decompose and cache if not already cached
            if (!decomposedTxCache.contains(txHashStr)) {
                QList<TransactionRecord> records = TransactionRecord::decomposeTransaction(arcTx);
                decomposedTxCache[txHashStr] = records;
                cached++;
                
                // Add these records to indexes for fast filtering
                // Use negative indices to indicate they're NOT in cachedWallet yet
                // When filters find them, they'll be added to cachedWallet properly
                int baseIdx = -(decomposedTxCache.size() * 100); // Negative to distinguish from cachedWallet indices
                for (int i = 0; i < records.size(); i++) {
                    const TransactionRecord& rec = records[i];
                    int pseudoIdx = baseIdx - i; // Each record gets unique negative index
                    
                    // Add to indexes using negative pseudo-index
                    dateIndex.insert(rec.time, pseudoIdx);
                    typeIndex.insert(rec.type, pseudoIdx);
                    if (!rec.address.empty()) {
                        addressIndex.insert(QString::fromStdString(rec.address).toLower(), pseudoIdx);
                    }
                    if (rec.involvesWatchAddress) {
                        watchOnlyIndex.insert(pseudoIdx);
                    }
                    qint64 absAmount = qAbs(rec.credit + rec.debit);
                    amountIndex.insert(absAmount, pseudoIdx);
                    indexed++;
                }
            }
        }
        
        lazyLoadPosition = endPos;
        
        if (cached > 0) {
            LogPrintf("Lazy loaded batch: %d transactions cached, %d records indexed (total cache: %d), position: %d\n",
                     cached, indexed, decomposedTxCache.size(), lazyLoadPosition);
        }
        
        // Emit progress update based on position in archive, not cache size
        Q_EMIT parent->lazyLoadProgress(lazyLoadPosition, lazyLoadTotalSize);
        
        // Check if we're done - we've reached the end of the archive
        if (reachedEnd) {
            lazyLoadTimer->stop();
            lazyLoadInProgress = false;
            LogPrintf("Lazy loading complete! Total: %d transactions cached and indexed (available for filtering)\n", 
                     decomposedTxCache.size());
            Q_EMIT parent->lazyLoadComplete();
        }
    }
    
    void stopLazyLoad()
    {
        if (lazyLoadTimer) {
            lazyLoadTimer->stop();
        }
        lazyLoadInProgress = false;
    }
    
    void rebuildFromCache()
    {
        LogPrintf("Rebuilding cachedWallet from decomposedTxCache (%d transactions cached)...\n", decomposedTxCache.size());
        
        // Clear current cachedWallet
        cachedWallet.clear();
        
        // Load all cached transactions into cachedWallet
        for (auto it = decomposedTxCache.begin(); it != decomposedTxCache.end(); ++it) {
            cachedWallet.append(it.value());
        }
        
        LogPrintf("Loaded %d records from cache, now sorting...\n", cachedWallet.size());
        
        // Sort all records
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        LogPrintf("Sorting complete, updating parent indices...\n");
        
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
        
        LogPrintf("Parent index update complete\n");
        
        cachedSize = -1; // Invalidate size cache
        
        // Rebuild indexes for the new cachedWallet
        buildIndexes();
        
        LogPrintf("Rebuild complete: %d records in cachedWallet\n", cachedWallet.size());
    }
    
    void rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                          quint32 typeFilter, int watchOnlyFilter, 
                          const QString &addrPrefix, qint64 minAmount, 
                          bool showInactive, int limitParents)
    {
        LogPrintf("Rebuilding cachedWallet with filters from decomposedTxCache (%d transactions cached, limit=%d parents)...\n", 
                 decomposedTxCache.size(), limitParents);
        
        // Clear current cachedWallet
        cachedWallet.clear();
        
        // First pass: collect all matching parent transactions with their records
        struct MatchedTx {
            qint64 time;
            QList<TransactionRecord> records;
        };
        QList<MatchedTx> matchedTransactions;
        
        for (auto it = decomposedTxCache.begin(); it != decomposedTxCache.end(); ++it) {
            const QList<TransactionRecord>& records = it.value();
            if (records.isEmpty()) continue;
            
            // Check parent record against filters
            bool parentMatches = false;
            qint64 parentTime = 0;
            
            for (const TransactionRecord& rec : records) {
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
                    for (const TransactionRecord& checkRec : records) {
                        QString address = QString::fromStdString(checkRec.address);
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
                matched.records = records;
                matchedTransactions.append(matched);
            }
        }
        
        LogPrintf("Found %d matching parent transactions\n", matchedTransactions.size());
        
        // Sort matched transactions by time (most recent first)
        std::sort(matchedTransactions.begin(), matchedTransactions.end(), 
                 [](const MatchedTx& a, const MatchedTx& b) { return a.time > b.time; });
        
        // Apply limit and add to cachedWallet
        int loadedParents = 0;
        for (const MatchedTx& matched : matchedTransactions) {
            if (limitParents > 0 && loadedParents >= limitParents) {
                break;
            }
            cachedWallet.append(matched.records);
            loadedParents++;
        }
        
        LogPrintf("Loaded %d records from cache (filtered, most recent first), now sorting...\n", cachedWallet.size());
        
        // Sort all records
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        LogPrintf("Sorting complete, updating parent indices...\n");
        
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
        
        LogPrintf("Parent index update complete\n");
        
        cachedSize = -1; // Invalidate size cache
        
        // Rebuild indexes for the new cachedWallet
        buildIndexes();
        
        LogPrintf("Filtered rebuild complete: %d records in cachedWallet\n", cachedWallet.size());
    }
    
    void refreshWallet()
    {
        LogPrintf("TransactionTablePriv::refreshWallet\n");
        LogPrintf("Refreshing GUI Wallet from core\n");

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

            //Get all Archived Transactions - track what's being filtered
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
            
            LogPrintf("Initial load: mapArcTxs has %d transactions. Added %d, Skipped: nullBlock=%d, blockNotInIndex=%d\n",
                     totalArcTxs, addedArcTxs, nullBlockHash, blockNotInIndex);


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
            
            LogPrintf("Initial load: checked %d transactions, collected %d valid ones. Skipped: notFinal=%d, notTrusted=%d, depth=%d, arcNoBlock=%d\\n",
                     checked, arcTxList.size(), skippedNotFinal, skippedNotTrusted, skippedDepth, skippedArcNoBlock);
        } // Release locks here before expensive decomposeTransaction calls
        
        // Remove stale entries from decomposed cache
        QMutableMapIterator<QString, QList<TransactionRecord>> it(decomposedTxCache);
        while (it.hasNext()) {
            it.next();
            if (!validTxHashes.contains(it.key())) {
                it.remove();
            }
        }
        LogPrintf("Decomposed cache size: %d entries\n", decomposedTxCache.size());

        // Process transactions outside of locks - decomposeTransaction is pure computation
        int processedCount = 0;
        int cacheHits = 0;
        int skippedNotFound = 0;
        int totalCount = arcTxList.size();
        for (const RpcArcTransaction& arcTx : arcTxList) {
            QString txHashStr = QString::fromStdString(arcTx.txid.ToString());
            
            // Skip transactions that couldn't be loaded (category "not found" means getRpcArcTx returned early)
            if (arcTx.category == "not found") {
                skippedNotFound++;
                continue;
            }
            
            // Check if we have cached decomposition
            if (decomposedTxCache.contains(txHashStr)) {
                // Use cached decomposition
                cachedWallet.append(decomposedTxCache[txHashStr]);
                cacheHits++;
            } else {
                // Decompose and cache
                QList<TransactionRecord> records = TransactionRecord::decomposeTransaction(arcTx);
                decomposedTxCache[txHashStr] = records;
                cachedWallet.append(records);
            }
            
            // Update UI every 100 transactions to keep it responsive
            processedCount++;
            if (processedCount % 100 == 0) {
                QApplication::processEvents();
            }
        }
        
        LogPrintf("Processed %d transactions (%d cache hits, %d new decompositions, %d skipped not found)\n",
                 processedCount, cacheHits, (processedCount - cacheHits), skippedNotFound);
        LogPrintf("Now sorting...\n");
        
        // Sort all records to ensure proper parent-child ordering
        std::sort(cachedWallet.begin(), cachedWallet.end(), TxLessThan());
        
        LogPrintf("Sorting complete, updating parent indices...\n");
        
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
        
        LogPrintf("Parent index update complete for %d parents\n", parentCount);
        
        cachedSize = -1; // Invalidate size cache after refresh
        
        // Build indexes for fast filtering and searching
        buildIndexes();
        
        LogPrintf("Initial load complete, starting lazy load for remaining transactions...\n");
        
        // Start lazy loading remaining transactions in background for caching and indexing
        // Use QTimer::singleShot to start after returning to event loop
        QTimer::singleShot(500, [this]() {
            this->startLazyLoad();
        });
    }

    /* Update our model of the wallet incrementally, to synchronize our model of the wallet
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


        LogPrintf("    inModel=%d Index=%d-%d showTransaction=%d derivedStatus=%d\n",
                  inModel, lowerIndex, upperIndex, showTransaction, status);

        switch(status)
        {
        case CT_NEW:
            if(inModel)
            {
                LogPrintf("TransactionTablePriv::updateWallet: Warning: Got CT_NEW, but transaction is already in model\n");
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
                    LogPrintf("TransactionTablePriv::updateWallet: Warning: Got CT_NEW, but transaction is not in wallet\n");
                    parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
                    cachedWallet.erase(lower, upper);
                    parent->endRemoveRows();
                    break;
                }

                // Check cache first, then decompose if needed
                QList<TransactionRecord> toInsert;
                QString hashStr = QString::fromStdString(hash.ToString());
                if (decomposedTxCache.contains(hashStr)) {
                    toInsert = decomposedTxCache[hashStr];
                } else {
                    toInsert = TransactionRecord::decomposeTransaction(arcTx);
                    decomposedTxCache[hashStr] = toInsert;
                }
                
                if(!toInsert.isEmpty()) /* only if something to insert */
                {
                    cachedSize = -1; // Invalidate cache BEFORE insertion
                    int insertSize = toInsert.size();
                    
                    LogPrintf("=== CT_NEW: Inserting %d records at lowerIndex=%d\n", insertSize, lowerIndex);
                    LogPrintf("    cachedWallet.size() before: %d\n", cachedWallet.size());
                    
                    // CRITICAL: Fix parentIdx in toInsert BEFORE any other operations
                    // decomposeTransaction returns records with parentIdx relative to the list (0-based)
                    // but they need to be absolute indices in cachedWallet (lowerIndex-based)
                    for (int i = 0; i < toInsert.size(); i++) {
                        LogPrintf("    toInsert[%d]: isParent=%d isChild=%d parentIdx=%d\n", 
                                  i, toInsert[i].isParent, toInsert[i].isChild, toInsert[i].parentIdx);
                        if (toInsert[i].isChild && toInsert[i].parentIdx >= 0) {
                            int oldIdx = toInsert[i].parentIdx;
                            toInsert[i].parentIdx += lowerIndex;
                            LogPrintf("      Adjusted parentIdx from %d to %d\n", oldIdx, toInsert[i].parentIdx);
                        }
                    }
                    
                    // Shift all existing index values >= lowerIndex BEFORE insertion
                    shiftIndexValues(lowerIndex, insertSize);
                    
                    // Update parentIdx for existing children BEFORE insertion
                    for (int i = 0; i < cachedWallet.size(); i++) {
                        if (cachedWallet[i].isChild && cachedWallet[i].parentIdx >= lowerIndex) {
                            cachedWallet[i].parentIdx += insertSize;
                        }
                    }
                    
                    // DO NOT signal row changes - let the view refresh naturally
                    // beginInsertRows causes issues with the proxy model
                    
                    // Insert records directly
                    int insert_idx = lowerIndex;
                    for (const TransactionRecord &rec : toInsert)
                    {
                        cachedWallet.insert(insert_idx, rec);
                        insert_idx += 1;
                    }
                    
                    // Suppress UI updates during batch processing (syncing/reindexing)
                    // This dramatically improves performance when many transactions arrive rapidly
                    cachedSize = -1; // Force recalculation
                    if (parent->fProcessingQueuedTransactions) {
                        // Just mark that we need to refresh when batch completes
                        parent->fPendingRefresh = true;
                    } else {
                        // Single update - refresh immediately with layoutChanged to update row count
                        Q_EMIT parent->layoutChanged();
                    }
                    // Validate inserted records
                    for (int i = lowerIndex; i < lowerIndex + insertSize; i++) {
                        const TransactionRecord& rec = cachedWallet[i];
                        if (rec.isChild) {
                            LogPrintf("    Inserted child at %d: parentIdx=%d\n", i, rec.parentIdx);
                            if (rec.parentIdx < 0 || rec.parentIdx >= cachedWallet.size()) {
                                LogPrintf("    ERROR: Invalid parentIdx!\n");
                            } else if (!cachedWallet[rec.parentIdx].isParent) {
                                LogPrintf("    ERROR: parentIdx points to non-parent!\n");
                            } else if (cachedWallet[rec.parentIdx].hash != rec.hash) {
                                LogPrintf("    ERROR: parentIdx hash mismatch!\n");
                            }
                        }
                    }
                    
                    // Add indexes for new records
                    for (int i = lowerIndex; i < lowerIndex + insertSize; i++) {
                        const TransactionRecord& rec = cachedWallet[i];
                        dateIndex.insert(rec.time, i);
                        typeIndex.insert(rec.type, i);
                        if (!rec.address.empty()) {
                            addressIndex.insert(QString::fromStdString(rec.address).toLower(), i);
                        }
                        if (rec.involvesWatchAddress) {
                            watchOnlyIndex.insert(i);
                        }
                        qint64 absAmount = qAbs(rec.credit + rec.debit);
                        amountIndex.insert(absAmount, i);
                    }
                }
            }
            break;
        case CT_DELETED:
        {
            if(!inModel)
            {
                LogPrintf("TransactionTablePriv::updateWallet: Warning: Got CT_DELETED, but transaction is not in model\n");
                break;
            }
            // Removed -- remove entire transaction from table
            cachedSize = -1; // Invalidate cache BEFORE removal
            
            int removeSize = upperIndex - lowerIndex;
            
            LogPrintf("=== CT_DELETED: Removing %d records at range [%d, %d)\n", removeSize, lowerIndex, upperIndex);
            
            // Remove from decomposed cache
            decomposedTxCache.remove(QString::fromStdString(hash.ToString()));
            
            // Remove from indexes (must be done BEFORE shifting)
            for (int i = lowerIndex; i < upperIndex; i++) {
                const TransactionRecord& rec = cachedWallet[i];
                // Remove from all indexes
                dateIndex.remove(rec.time, i);
                typeIndex.remove(rec.type, i);
                if (!rec.address.empty()) {
                    addressIndex.remove(QString::fromStdString(rec.address).toLower(), i);
                }
                watchOnlyIndex.remove(i);
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
    
    TransactionRecord* getParentRecord(int parentIdx)
    {
        if (parentIdx >= 0 && parentIdx < cachedWallet.size()) {
            return &cachedWallet[parentIdx];
        }
        return nullptr;
    }
    
    // Fast index-based queries
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
    
    QList<int> findByType(int type) const
    {
        return typeIndex.values(type);
    }
    
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
    
    QList<int> findWatchOnly() const
    {
        return watchOnlyIndex.values();
    }
    
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
    
    // Get statistics
    int getTotalTransactionCount() const
    {
        int count = 0;
        for (const TransactionRecord& rec : cachedWallet) {
            if (rec.isParent) count++;
        }
        return count;
    }
    
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
};

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

TransactionTableModel::~TransactionTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

/** Updates the column title to "Amount (DisplayUnit)" and emits headerDataChanged() signal for table headers to react. */
void TransactionTableModel::updateAmountColumnTitle()
{
    columns[Amount] = KomodoUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit());
    Q_EMIT headerDataChanged(Qt::Horizontal,Amount,Amount);
}

void TransactionTableModel::setProcessingQueuedTransactions(bool value)
{
    fProcessingQueuedTransactions = value;
    
    // When batch processing completes, emit pending refresh if needed
    if (!value && fPendingRefresh) {
        LogPrintf("TransactionTableModel: Batch complete, emitting deferred refresh signal\\n");
        Q_EMIT layoutChanged();
        fPendingRefresh = false;
    }
}

void TransactionTableModel::updateTransaction(const QString &hash, int status, bool showTransaction)
{
    uint256 updated;
    updated.SetHex(hash.toStdString());

    priv->updateWallet(updated, status, showTransaction);
}

void TransactionTableModel::updateConfirmations()
{
    // Blocks came in since last poll.
    // Invalidate status (number of confirmations) and (possibly) description
    //  for all rows. Qt is smart enough to only actually request the data for the
    //  visible rows.
    Q_EMIT dataChanged(index(0, Status), index(priv->size()-1, Status));
    Q_EMIT dataChanged(index(0, ToAddress), index(priv->size()-1, ToAddress));
}

void TransactionTableModel::refreshWallet()
{
    return priv->refreshWallet();
}

void TransactionTableModel::rebuildFromCache()
{
    priv->rebuildFromCache();
    // Notify views that the model has been completely restructured
    Q_EMIT layoutAboutToBeChanged();
    Q_EMIT layoutChanged();
}

void TransactionTableModel::rebuildFromCache(const QDateTime &dateFrom, const QDateTime &dateTo, 
                                              quint32 typeFilter, int watchOnlyFilter, 
                                              const QString &addrPrefix, qint64 minAmount, 
                                              bool showInactive, int limitParents)
{
    priv->rebuildFromCache(dateFrom, dateTo, typeFilter, watchOnlyFilter, addrPrefix, minAmount, showInactive, limitParents);
    // Notify views that the model has been completely restructured
    Q_EMIT layoutAboutToBeChanged();
    Q_EMIT layoutChanged();
}

void TransactionTableModel::toggleTransactionExpanded(const QModelIndex &idx)
{
    if(!idx.isValid())
        return;
        
    TransactionRecord *rec = static_cast<TransactionRecord*>(idx.internalPointer());
    if(!rec || !rec->isParent || rec->groupCount == 0)
        return;
    
    // Toggle collapsed state
    rec->collapsed = !rec->collapsed;
    priv->cachedSize = -1; // Invalidate size cache
    
    // Use layoutChanged to refresh the entire view
    // This is safer than insert/remove when dealing with filtered views
    Q_EMIT layoutAboutToBeChanged();
    Q_EMIT layoutChanged();
}

void TransactionTableModel::setAllTransactionsExpanded(bool expanded)
{
    // Set collapsed state for all parent transactions
    for(int i = 0; i < priv->cachedWallet.size(); i++)
    {
        if (priv->cachedWallet[i].isParent)
        {
            priv->cachedWallet[i].collapsed = !expanded;
        }
    }
    
    priv->cachedSize = -1; // Invalidate size cache
    
    // Refresh the view
    Q_EMIT layoutAboutToBeChanged();
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

QVariant TransactionTableModel::addressColor(const TransactionRecord *wtx) const
{
    // Show addresses without label in a less visible color
    // switch(wtx->type)
    // {
    // case TransactionRecord::RecvWithAddress:
    // case TransactionRecord::SendToAddress:
    // case TransactionRecord::Generated:
    //     {
    //     QString label = walletModel->getAddressTableModel()->labelForAddress(QString::fromStdString(wtx->address));
    //     if(label.isEmpty())
    //         return COLOR_BAREADDRESS;
    //     } break;
    // case TransactionRecord::SendToSelf:
    //     return COLOR_BAREADDRESS;
    // default:
    //     break;
    // }
    // return QVariant();

    return COLOR_BLACK;
}

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
    //     // Use the "danger" color for abandoned transactions
    //     if(rec->status.status == TransactionStatus::Abandoned && !rec->archiveType == ARCHIVED)
    //     {
    //         return COLOR_TX_STATUS_DANGER;
    //     }
    //     // Non-confirmed (but not immature) as transactions are grey
    //     if(!rec->status.countsForBalance && rec->status.status != TransactionStatus::Immature  && !rec->archiveType == ARCHIVED)
    //     {
    //         return COLOR_UNCONFIRMED;
    //     }
        // Make child record descriptions (Label column) light blue for readability
        if(rec->isChild && index.column() == ToAddress)
        {
            return QColor(100, 180, 255); // Light blue color for child descriptions
        }
        
        // Amount column color logic
        if(index.column() == Amount)
        {
            QSettings settings;
            CAmount amount;
            
            // Determine the amount to evaluate
            if (rec->isParent) {
                amount = rec->netChange;
            } else {
                amount = rec->credit + rec->debit;
            }
            
            // Special case: external outputs (sends to non-wallet addresses) are white
            if (rec->isChild && rec->type == TransactionRecord::Output && !rec->involvesOwnAddress)
            {
                return QColor(Qt::white);
            }
            
            // Parent and child records: red if negative, green if positive/zero
            if(amount < 0)
            {
                if (settings.value("strTheme", "pirate").toString() == "dark") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "pirate") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "pirateship") {
                    return COLOR_NEGATIVE;
                } else if (settings.value("strTheme", "pirate").toString() == "piratemap") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "armada") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "treasure") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "treasuremap") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "ghostship") {
                    return COLOR_NEGATIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "night") {
                    return COLOR_NEGATIVE_DARK;
                } else {
                    return COLOR_NEGATIVE;
                }
            }
            else // amount >= 0
            {
                if (settings.value("strTheme", "pirate").toString() == "dark") {
                    return COLOR_POSITIVE_DARK;
                } else if (settings.value("strTheme", "pirate").toString() == "pirate") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "piratemap") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "armada") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "treasure") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "treasuremap") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "ghostship") {
                    return COLOR_POSITIVE_PIRATE;
                } else if (settings.value("strTheme", "pirate").toString() == "night") {
                    return COLOR_POSITIVE_PIRATE;
                } else {
                    return COLOR_POSITIVE;
                }
            }
        }
    //     if(index.column() == ToAddress)
    //     {
    //         return addressColor(rec);
    //     }
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

void TransactionTableModel::updateDisplayUnit()
{
    // emit dataChanged to update Amount column with the current unit
    updateAmountColumnTitle();
    Q_EMIT dataChanged(index(0, Amount), index(priv->size()-1, Amount));
}

// queue notifications to show a non freezing progress dialog e.g. for rescan
struct TransactionNotification
{
public:
    TransactionNotification() {}
    TransactionNotification(uint256 _hash, ChangeType _status, bool _showTransaction):
        hash(_hash), status(_status), showTransaction(_showTransaction) {}

    void invoke(QObject *ttm)
    {
        QString strHash = QString::fromStdString(hash.GetHex());
        LogPrintf("NotifyTransactionChanged: %s status=%d\n", strHash.toStdString().c_str(), status);
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

static void ShowProgress(TransactionTableModel *ttm, const std::string &title, int nProgress)
{
    if (nProgress == 0)
        fQueueNotifications = true;

    if (nProgress == 100)
    {
        fQueueNotifications = false;
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

void TransactionTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    wallet->ShowProgress.connect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2));
}

void TransactionTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    wallet->ShowProgress.disconnect(boost::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2));
}
