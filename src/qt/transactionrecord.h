// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_TRANSACTIONRECORD_H
#define KOMODO_QT_TRANSACTIONRECORD_H

#include "amount.h"
#include "uint256.h"

#include <QList>
#include <QString>

class CWallet;
class CWalletTx;
class RpcArcTransaction;

/**
 * UI model for transaction status.
 * 
 * Represents the dynamic state of a transaction that changes over time as blocks
 * are mined and confirmations accumulate. Separate from static transaction data
 * to allow efficient status updates without reprocessing transaction details.
 * 
 * STATUS PROGRESSION:
 * Regular transactions: Unconfirmed → Confirming → Confirmed
 * Generated transactions: Immature → Confirmed
 * Failed: Conflicted (double-spend) or Abandoned (manually removed)
 */
class TransactionStatus
{
public:
    TransactionStatus():
        countsForBalance(false), sortKey(""),
        matures_in(0), status(Offline), depth(0), open_for(0), cur_num_blocks(-1)
    { }

    /** Transaction confirmation and maturity states */
    enum Status {
        Confirmed,          ///< Has 6+ confirmations (normal) or fully mature (mined, 100+ blocks)
        
        // Regular transaction states
        OpenUntilDate,      ///< Locked until specific timestamp (nLockTime >= 500000000)
        OpenUntilBlock,     ///< Locked until specific block height (nLockTime < 500000000)
        Offline,            ///< Not broadcast to network
        Unconfirmed,        ///< In mempool, awaiting first confirmation
        Confirming,         ///< 1-5 confirmations (< RecommendedNumConfirmations)
        Conflicted,         ///< Double-spend detected, will not confirm
        Abandoned,          ///< Manually removed from wallet
        
        // Generated (mined/staked) transaction states
        Immature,           ///< Coinbase waiting for 100 block maturity period
        MaturesWarning,     ///< In orphaned block, will likely not mature
        NotAccepted         ///< Mined but not in main chain
    };

    bool countsForBalance;      ///< Whether transaction affects available balance
    std::string sortKey;        ///< Sorting key: height-coinbase-time-idx

    /** @name Generated (mined) transaction fields
       @{*/
    int matures_in;             ///< Blocks remaining until coinbase maturity
    /**@}*/

    /** @name Reported status fields
       @{*/
    Status status;              ///< Current transaction state
    qint64 depth;               ///< Confirmations (>0) or conflict indicator (<0)
    qint64 open_for;            ///< Timestamp (OpenUntilDate) or blocks (OpenUntilBlock) remaining
    /**@}*/

    int cur_num_blocks;         ///< Cached chain height for staleness detection
    bool needsUpdate;           ///< Explicit flag to force status recalculation
};

/**
 * UI model for a transaction record with parent-child hierarchy.
 * 
 * ARCHITECTURE:
 * Transactions are decomposed into a parent-child structure:
 * - Parent: Summary record showing overall transaction effect (net balance change)
 * - Children: Detailed records for each input/output address
 * 
 * BENEFITS:
 * - Compact view: Show only parents for transaction list
 * - Expandable detail: Show children when user wants specifics
 * - Address grouping: Multiple UTXOs to/from same address aggregated
 * - Performance: Filter/sort on parents, lazy-load children
 * 
 * HIERARCHY EXAMPLE:
 * Parent (Send 100 ARRR to Alice, -100.001 net)
 *   ├─ Output: Alice's address, 100 ARRR
 *   ├─ Output: Change address (internal), 50 ARRR
 *   ├─ Input: My address A, -120 ARRR
 *   ├─ Input: My address B, -30 ARRR
 *   └─ Fee: 0.001 ARRR
 * 
 * TRANSPARENT, SAPLING, AND ORCHARD:
 * All three privacy pool types (transparent P2PKH/P2SH, Sapling z-addresses,
 * Orchard z-addresses) are handled uniformly. Memos are preserved for
 * shielded transactions.
 */
class TransactionRecord
{
public:
    /** 
     * Transaction type for display categorization.
     * 
     * Types affect:
     * - Display color (green=receive, red=send, blue=self, yellow=generated)
     * - Icon selection (arrows, mining icon, self-transfer icon)
     * - Transaction filtering
     * - Balance calculation rules
     */
    enum Type
    {
        Other,                      ///< Unknown/uncategorized (shouldn't occur)
        Generated,                  ///< Coinbase/staking reward
        SendToAddress,              ///< Sending to external address(es)
        SendToAddressWithMemo,      ///< Sending with Zcash memo field
        SendToOther,                ///< Legacy: sending to non-address output
        RecvWithAddress,            ///< Receiving from external source
        RecvWithAddressWithMemo,    ///< Receiving with Zcash memo field
        RecvFromOther,              ///< Legacy: receiving from non-address source
        SendToSelf,                 ///< Internal transfer (all outputs to wallet)
        SendToSelfWithMemo,         ///< Internal transfer with memo
        Input,                      ///< Child record: spent from address
        Output,                     ///< Child record: sent to address
        Fee                         ///< Child record: transaction fee
    };

    /** Recommended confirmations before considering transaction secure (6 blocks) */
    static const int RecommendedNumConfirmations = 6;

    /** Default constructor - initializes all fields to safe defaults */
    TransactionRecord():
            hash(), time(0), type(Other), address(""), debit(0), credit(0), idx(0), memo(""),
            isParent(false), isChild(false), parentIdx(-1), groupCount(0), netChange(0), collapsed(true)
    {
    }

    /** Constructor with transaction hash and timestamp */
    TransactionRecord(uint256 _hash, qint64 _time):
            hash(_hash), time(_time), type(Other), address(""), debit(0),
            credit(0), idx(0), memo(""),
            isParent(false), isChild(false), parentIdx(-1), groupCount(0), netChange(0), collapsed(true)
    {
    }

    /** Constructor with full transaction details */
    TransactionRecord(uint256 _hash, qint64 _time,
                Type _type, const std::string &_address,
                const CAmount& _debit, const CAmount& _credit):
            hash(_hash), time(_time), type(_type), address(_address), debit(_debit), credit(_credit),
            idx(0), memo(""),
            isParent(false), isChild(false), parentIdx(-1), groupCount(0), netChange(0), collapsed(true)
    {
    }

    /**
     * Check if transaction should be displayed in list.
     * 
     * Currently always returns true. Future uses may include:
     * - Hiding Replace-By-Fee related transactions
     * - Filtering based on user preferences
     * - Excluding certain transaction types
     */
    static bool showTransaction(const CWalletTx &wtx);
    
    /**
     * Decompose archived transaction into parent-child record hierarchy.
     * 
     * Creates parent record showing transaction summary (net balance change,
     * total debit/credit) plus child records for each input/output address.
     * Aggregates multiple UTXOs from same address into single child record.
     * Handles transparent, Sapling, and Orchard uniformly.
     * 
     * @param arcTx Archived transaction with all pool components
     * @return List: parent first, then children (external outputs, internal outputs, inputs, fee)
     */
    static QList<TransactionRecord> decomposeTransaction(const RpcArcTransaction &arcTx);

    /** @name Immutable transaction attributes
      @{*/
    uint256 hash;               ///< Transaction ID (txid)
    qint64 time;                ///< Transaction timestamp (block time or receive time)
    Type type;                  ///< Transaction type for display categorization
    std::string address;        ///< Address string (for child records)
    CAmount debit;              ///< Amount sent out (positive value)
    CAmount credit;             ///< Amount received in (positive for receive, negative for spent)
    int archiveType;            ///< Archive type identifier
    std::string memo;           ///< Decoded Zcash memo (UTF-8)
    std::string memohex;        ///< Hex-encoded Zcash memo
    
    /**
     * @name Hierarchical grouping support
     * 
     * Parent-child relationships allow compact transaction list (parents only)
     * with expandable detail view (showing children on demand).
     * @{
     */
    bool isParent;              ///< True for summary record at transaction level
    bool isChild;               ///< True for detail record (input/output/fee)
    int parentIdx;              ///< Position of parent in list (-1 for parent records)
    int groupCount;             ///< Number of children for parent; UTXOs for child
    CAmount netChange;          ///< Net wallet balance change (walletOutputs - inputs)
    bool collapsed;             ///< UI state: whether children are hidden
    /**@}*/

    int idx;                    ///< Child index for sorting within parent group

    TransactionStatus status;   ///< Dynamic status updated as blocks are mined

    /** @name Address ownership flags
      @{*/
    bool involvesWatchAddress;  ///< True if any input/output is watch-only address
    bool involvesOwnAddress;    ///< True if address belongs to wallet (not external)
    /**@}*/

    /**
     * Get transaction ID as QString.
     * Converts internal uint256 hash to hex string format for display.
     */
    QString getTxID() const;

    /**
     * Get child index within parent group.
     * Used for sorting children and identifying specific input/output records.
     */
    int getOutputIndex() const;

    /**
     * Update transaction status based on current blockchain state.
     * 
     * Recalculates:
     * - Confirmation depth
     * - Maturity state (for generated transactions)
     * - Sort key for transaction ordering
     * - Balance contribution flag
     * 
     * Should be called when:
     * - New block arrives (confirmations change)
     * - Transaction becomes conflicted
     * - needsUpdate flag is set
     * 
     * @param wtx Core wallet transaction with current block information
     */
    void updateStatus(const CWalletTx &wtx);

    /**
     * Check if status needs refresh.
     * 
     * Returns true if:
     * - Chain height changed since last update (confirmations may have increased)
     * - Explicit needsUpdate flag is set
     * 
     * This allows efficient status updates - only recalculate when necessary.
     */
    bool statusUpdateNeeded() const;
};

#endif // KOMODO_QT_TRANSACTIONRECORD_H
