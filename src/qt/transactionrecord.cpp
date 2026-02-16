// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "consensus/consensus.h"
#include "timedata.h"
#include "main.h"
#include "wallet/wallet.h"
#include "wallet/rpcpiratewallet.h"
#include "key_io.h"

#include <stdint.h>

/**
 * Determine if a transaction should be displayed in the transaction list.
 * 
 * CURRENT BEHAVIOR:
 * All transactions are shown. This function exists as a hook for future filtering
 * requirements, such as:
 * - Hiding Replace-By-Fee (RBF) related transactions
 * - Filtering based on transaction flags or properties
 * - Implementing user-configurable display preferences
 * 
 * @param wtx The wallet transaction to evaluate
 * @return true to show the transaction, false to hide it
 */
bool TransactionRecord::showTransaction(const CWalletTx &wtx)
{
    return true;
}

/**
 * Decompose an archived transaction into a parent-child hierarchy of display records.
 * 
 * ARCHITECTURE:
 * Creates a parent record showing transaction totals, with child records for each
 * input/output address. This provides:
 * - Summary view: Parent shows overall transaction effect (net change)
 * - Detail view: Children show individual address movements (can be expanded)
 * - Grouping: Multiple UTXOs to/from same address are aggregated into one child
 * 
 * TRANSACTION TYPES:
 * - Generated: Coinbase/mining reward transaction
 * - RecvWithAddress: Pure receive (no inputs from wallet)
 * - SendToAddress: Transaction with external outputs (sending money out)
 * - SendToSelf: All outputs belong to wallet (internal transfer/consolidation)
 * - *WithMemo: Variants when transaction contains Zcash memo field
 * 
 * RECORD ORDERING:
 * 1. Parent record (summary with totals)
 * 2. External output children (money sent out)
 * 3. Internal output children (change/received)
 * 4. Input children (spent from addresses)
 * 5. Fee child (if applicable)
 * 
 * ADDRESS GROUPING LOGIC:
 * Multiple operations to/from the same address are consolidated into a single child
 * record with aggregated amounts. This prevents UI clutter when consolidating UTXOs
 * or sending to the same address multiple times in one transaction.
 * 
 * @param arcTx Archived transaction from RPC containing all transparent, Sapling, and Orchard data
 * @return List of transaction records (parent + children) for display
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const RpcArcTransaction &arcTx)
{
    QList<TransactionRecord> parts;
    
    /**
     * AddressGroup: Aggregates multiple inputs/outputs for the same address.
     * 
     * When a transaction has multiple UTXOs from or to the same address,
     * we consolidate them into a single display record to avoid clutter.
     * Example: Consolidation transaction spending 100 UTXOs from one address
     * shows as single input child rather than 100 separate lines.
     */
    struct AddressGroup {
        std::string address;       ///< Address (transparent, Sapling, or Orchard)
        CAmount amount = 0;        ///< Aggregated amount for this address
        int count = 0;             ///< Number of UTXOs consolidated
        std::string memo;          ///< Decoded memo (first found if multiple)
        std::string memohex;       ///< Hex-encoded memo (first found if multiple)
        bool involvesWatchAddress = false; ///< True if any UTXO is watch-only
        bool isInput = false;      ///< true=spent from wallet, false=sent to address
        bool belongsToWallet = false; ///< true=output owned by wallet (change/receive)
    };
    
    std::vector<AddressGroup> inputGroups;  ///< Aggregated inputs by address
    std::vector<AddressGroup> outputGroups; ///< Aggregated outputs by address
    std::map<std::string, int> inputMap;    ///< Address -> index in inputGroups
    std::map<std::string, int> outputMap;   ///< Address -> index in outputGroups
    
    CAmount totalInputs = 0;
    CAmount totalOutputs = 0;

    /**
     * PHASE 1: Process all inputs (money being spent from wallet addresses)
     * 
     * Inputs are identified by spentFrom field in arcTx, which contains all
     * addresses that had UTXOs consumed by this transaction. We process:
     * - Transparent (vTSpend): P2PKH/P2SH inputs
     * - Sapling (vZsSpend): Shielded Sapling notes
     * - Orchard (vZoSpend): Shielded Orchard notes
     * 
     * Aggregation: Multiple UTXOs from same address are grouped together.
     * Watch-only detection: Flag is set if any UTXO is from watch-only address.
     */
    if (arcTx.spentFrom.size() > 0) {
        // Process transparent inputs
        for (int i = 0; i < arcTx.vTSpend.size(); i++) {
            std::string addr = arcTx.vTSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                // Create new address group for first occurrence
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            // Aggregate into existing group
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vTSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vTSpend[i].spendable;
            totalInputs += arcTx.vTSpend[i].amount;
        }

        // Process Sapling shielded inputs
        for (int i = 0; i < arcTx.vZsSpend.size(); i++) {
            std::string addr = arcTx.vZsSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vZsSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vZsSpend[i].spendable;
            totalInputs += arcTx.vZsSpend[i].amount;
        }

        // Process Orchard shielded inputs
        for (int i = 0; i < arcTx.vZoSpend.size(); i++) {
            std::string addr = arcTx.vZoSpend[i].encodedAddress;
            if (inputMap.find(addr) == inputMap.end()) {
                inputMap[addr] = inputGroups.size();
                AddressGroup group;
                group.address = addr;
                group.isInput = true;
                inputGroups.push_back(group);
            }
            int idx = inputMap[addr];
            inputGroups[idx].amount += arcTx.vZoSpend[i].amount;
            inputGroups[idx].count++;
            inputGroups[idx].involvesWatchAddress = inputGroups[idx].involvesWatchAddress || !arcTx.vZoSpend[i].spendable;
            totalInputs += arcTx.vZoSpend[i].amount;
        }
    }

    /**
     * PHASE 2: Process all outputs (money being sent to addresses)
     * 
     * DUAL-SOURCE ARCHITECTURE:
     * Outputs come from two sources:
     * 1. vT/Zs/ZoSend: ALL transaction outputs (external + wallet-owned)
     * 2. vT/Zs/ZoReceived: Wallet-owned outputs only (subset of Send)
     * 
     * For transactions with inputs (sends):
     * - Use *Send vectors (contains all outputs)
     * - Mark wallet-owned via receivedIn map
     * 
     * For transactions without inputs (pure receives):
     * - *Send vectors may be empty
     * - Use *Received vectors to capture wallet outputs
     * - Check outputMap to avoid double-counting
     * 
     * This architecture handles both sent and received transactions correctly
     * while maintaining proper separation of external vs internal outputs.
     */
    
    // Process transparent outputs (all destinations)
    for (int i = 0; i < arcTx.vTSend.size(); i++) {
        std::string addr = arcTx.vTSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            // Wallet ownership: Check if this output address is in receivedIn map
            // receivedIn contains addresses where wallet received funds in this tx
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vTSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vTSend[i].mine;
        totalOutputs += arcTx.vTSend[i].amount;
    }

    // Process transparent receives (pure receive transactions without spends)
    // Only process if not already in outputMap to avoid double-counting
    for (int i = 0; i < arcTx.vTReceived.size(); i++) {
        std::string addr = arcTx.vTReceived[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            // Not in Send vector - this is pure receive transaction
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true; // Received outputs always belong to wallet
            group.amount = arcTx.vTReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vTReceived[i].spendable;
            outputGroups.push_back(group);
            totalOutputs += arcTx.vTReceived[i].amount;
        }
        // else: Already counted from vTSend, skip to avoid double-counting
    }

    // Process Sapling shielded outputs (all destinations)
    for (int i = 0; i < arcTx.vZsSend.size(); i++) {
        std::string addr = arcTx.vZsSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vZsSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vZsSend[i].mine;
        // Memo handling: Store first non-empty memo found for this address
        if (arcTx.vZsSend[i].memoStr.length() != 0 && outputGroups[idx].memo.empty()) {
            outputGroups[idx].memo = arcTx.vZsSend[i].memoStr;
            outputGroups[idx].memohex = arcTx.vZsSend[i].memo;
        }
        totalOutputs += arcTx.vZsSend[i].amount;
    }

    // Process Sapling receives (pure receive transactions without spends)
    // Only process if not already in outputMap to avoid double-counting
    for (int i = 0; i < arcTx.vZsReceived.size(); i++) {
        std::string addr = arcTx.vZsReceived[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true;
            group.amount = arcTx.vZsReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vZsReceived[i].spendable;
            if (arcTx.vZsReceived[i].memoStr.length() != 0) {
                group.memo = arcTx.vZsReceived[i].memoStr;
                group.memohex = arcTx.vZsReceived[i].memo;
            }
            outputGroups.push_back(group);
            totalOutputs += arcTx.vZsReceived[i].amount;
        }
        // else: Already counted from vZsSend, skip
    }

    // Process Orchard shielded outputs (all destinations)
    for (int i = 0; i < arcTx.vZoSend.size(); i++) {
        std::string addr = arcTx.vZoSend[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = (arcTx.receivedIn.find(addr) != arcTx.receivedIn.end());
            outputGroups.push_back(group);
        }
        int idx = outputMap[addr];
        outputGroups[idx].amount += arcTx.vZoSend[i].amount;
        outputGroups[idx].count++;
        outputGroups[idx].involvesWatchAddress = outputGroups[idx].involvesWatchAddress || !arcTx.vZoSend[i].mine;
        if (arcTx.vZoSend[i].memoStr.length() != 0 && outputGroups[idx].memo.empty()) {
            outputGroups[idx].memo = arcTx.vZoSend[i].memoStr;
            outputGroups[idx].memohex = arcTx.vZoSend[i].memo;
        }
        totalOutputs += arcTx.vZoSend[i].amount;
    }

    // Process Orchard receives (pure receive transactions without spends)
    // Only process if not already in outputMap to avoid double-counting
    for (int i = 0; i < arcTx.vZoReceived.size(); i++) {
        std::string addr = arcTx.vZoReceived[i].encodedAddress;
        if (outputMap.find(addr) == outputMap.end()) {
            outputMap[addr] = outputGroups.size();
            AddressGroup group;
            group.address = addr;
            group.isInput = false;
            group.belongsToWallet = true;
            group.amount = arcTx.vZoReceived[i].amount;
            group.count = 1;
            group.involvesWatchAddress = !arcTx.vZoReceived[i].spendable;
            if (arcTx.vZoReceived[i].memoStr.length() != 0) {
                group.memo = arcTx.vZoReceived[i].memoStr;
                group.memohex = arcTx.vZoReceived[i].memo;
            }
            outputGroups.push_back(group);
            totalOutputs += arcTx.vZoReceived[i].amount;
        }
        // else: Already counted from vZoSend, skip
    }

    /**
     * PHASE 3: Calculate wallet balance change and create parent record
     * 
     * NET CHANGE CALCULATION:
     * - Wallet-owned outputs: Money staying in or coming to wallet
     * - Total inputs: Money being spent from wallet (all inputs are wallet-owned)
     * - Net change = walletOutputs - totalInputs
     * 
     * Positive: Net increase (received more than spent)
     * Negative: Net decrease (sent out more than received back as change)
     * Zero: Exact consolidation or fee-only transaction
     */
    
    // Sum all outputs that belong to the wallet (change + received)
    CAmount walletOutputs = 0;
    for (const auto& group : outputGroups) {
        if (group.belongsToWallet) {
            walletOutputs += group.amount;
        }
    }
    
    // Net change is wallet-owned outputs minus all inputs (which are wallet-owned)
    CAmount txTotal = walletOutputs - totalInputs;
    
    // Create parent record showing transaction summary
    TransactionRecord parent;
    parent.hash = arcTx.txid;
    parent.time = arcTx.nTime;
    parent.archiveType = arcTx.archiveType;
    parent.netChange = txTotal;             // Net effect on wallet balance
    parent.debit = totalOutputs;            // Total money sent in transaction
    parent.credit = -totalInputs;           // Total money spent (negative)
    parent.isParent = true;
    parent.isChild = false;
    parent.parentIdx = -1;
    parent.groupCount = inputGroups.size() + outputGroups.size();
    parent.idx = 0;
    parent.collapsed = false;               // Start expanded to show children
    
    /**
     * TRANSACTION TYPE DETERMINATION:
     * 
     * Type affects display color, icon, and filtering:
     * - Generated: Coinbase/mining (green, special icon)
     * - RecvWithAddress: Pure receive, no spends (green, receive icon)
     * - SendToAddress: Has external outputs (red, send icon)
     * - SendToSelf: All outputs to wallet (blue, internal icon)
     * - *WithMemo: Variants when Zcash memo field present (same color, memo indicator)
     * 
     * Decision tree:
     * 1. Coinbase flag set? → Generated
     * 2. No inputs? → Receive (RecvWithAddress / RecvWithAddressWithMemo)
     * 3. All outputs to wallet? → SendToSelf / SendToSelfWithMemo
     * 4. Has external outputs? → SendToAddress / SendToAddressWithMemo
     * 5. Else → Other (shouldn't happen)
     */
    bool hasMemo = false;
    bool allOutputsToWallet = true;
    bool hasExternalOutput = false;
    
    // Check outputs for wallet ownership and presence of memos
    for (const auto& group : outputGroups) {
        if (!group.belongsToWallet) {
            allOutputsToWallet = false;
            hasExternalOutput = true;
        }
        if (!group.memo.empty()) {
            hasMemo = true;
        }
    }
    
    // Check inputs for memos as well
    for (const auto& group : inputGroups) {
        if (!group.memo.empty()) {
            hasMemo = true;
        }
    }
    
    // Apply transaction type determination logic
    if (arcTx.coinbase) {
        // Case 1: Mining/staking reward
        parent.type = Generated;
    } else if (inputGroups.size() == 0) {
        // Case 2: Pure receive - no wallet funds spent
        parent.type = hasMemo ? RecvWithAddressWithMemo : RecvWithAddress;
    } else if (allOutputsToWallet && outputGroups.size() > 0) {
        // Case 3: Internal transfer - all outputs return to wallet (consolidation/reshielding)
        parent.type = hasMemo ? SendToSelfWithMemo : SendToSelf;
    } else if (hasExternalOutput) {
        // Case 4: Sending to external address(es)
        parent.type = hasMemo ? SendToAddressWithMemo : SendToAddress;
    } else {
        // Case 5: Edge case - shouldn't occur in normal operation
        parent.type = Other;
    }
    
    parts.append(parent);
    int parentIndex = 0;
    int childIdx = 1;

    /**
     * PHASE 4: Create child records for detailed view
     * 
     * ORDERING STRATEGY:
     * Display children in order of financial priority:
     * 1. External outputs (money leaving wallet) - most important
     * 2. Internal outputs (change/received) - context for amount
     * 3. Inputs (source addresses) - transparency about origin
     * 4. Fee (network cost) - final accounting line
     * 
     * This ordering helps users quickly see:
     * - Where money went (external outputs first)
     * - How much came back (internal outputs/change)
     * - Where it came from (inputs)
     * - What it cost (fee)
     */

    // Create child records for external outputs first (highest priority)
    for (const auto& group : outputGroups) {
        if (!group.belongsToWallet) {
            // External output - money sent to non-wallet address
            TransactionRecord child;
            child.hash = arcTx.txid;
            child.time = arcTx.nTime;
            child.archiveType = arcTx.archiveType;
            child.type = Output;
            child.address = group.address;
            child.groupCount = group.count;      // Number of UTXOs consolidated
            child.memo = group.memo;
            child.memohex = group.memohex;
            child.involvesWatchAddress = group.involvesWatchAddress;
            child.involvesOwnAddress = false;    // Not owned by wallet
            child.isParent = false;
            child.isChild = true;
            child.parentIdx = parentIndex;
            child.idx = childIdx++;
            child.debit = group.amount;          // Money going out
            child.credit = 0;
            child.netChange = group.amount;
            
            parts.append(child);
        }
    }
    
    // Create child records for internal outputs (change/received)
    for (const auto& group : outputGroups) {
        if (group.belongsToWallet) {
            // Internal output - money staying in or coming to wallet
            TransactionRecord child;
            child.hash = arcTx.txid;
            child.time = arcTx.nTime;
            child.archiveType = arcTx.archiveType;
            child.type = Output;
            child.address = group.address;
            child.groupCount = group.count;
            child.memo = group.memo;
            child.memohex = group.memohex;
            child.involvesWatchAddress = group.involvesWatchAddress;
            child.involvesOwnAddress = true;     // Owned by wallet
            child.isParent = false;
            child.isChild = true;
            child.parentIdx = parentIndex;
            child.idx = childIdx++;
            child.debit = 0;
            child.credit = group.amount;         // Money coming in/staying
            child.netChange = group.amount;
            
            parts.append(child);
        }
    }

    // Create child records for inputs (spent-from addresses)
    for (const auto& group : inputGroups) {
        // Input - funds spent from wallet address
        TransactionRecord child;
        child.hash = arcTx.txid;
        child.time = arcTx.nTime;
        child.archiveType = arcTx.archiveType;
        child.type = Input;
        child.address = group.address;
        child.groupCount = group.count;          // Number of UTXOs spent
        child.memo = group.memo;
        child.memohex = group.memohex;
        child.involvesWatchAddress = group.involvesWatchAddress;
        child.isParent = false;
        child.isChild = true;
        child.parentIdx = parentIndex;
        child.idx = childIdx++;
        child.debit = 0;
        child.credit = -group.amount;            // Negative for spent
        child.netChange = -group.amount;
        
        parts.append(child);
    }
    
    /**
     * Fee calculation for transactions with inputs.
     * 
     * FEE FORMULA:
     * fee = totalOutputs - totalInputs
     * 
     * This represents the value "burned" to pay miners/validators.
     * Always positive (or zero) since outputs cannot exceed inputs.
     * 
     * Note: Pure receive transactions (no inputs) have no fee component.
     */
    if (inputGroups.size() > 0) {
        CAmount fee = totalOutputs - totalInputs;
        if (fee != 0) {
            // Create fee child record
            TransactionRecord feeChild;
            feeChild.hash = arcTx.txid;
            feeChild.time = arcTx.nTime;
            feeChild.archiveType = arcTx.archiveType;
            feeChild.type = Fee;
            feeChild.address = "";               // No address for fee
            feeChild.groupCount = 0;
            feeChild.involvesWatchAddress = false;
            feeChild.involvesOwnAddress = false;
            feeChild.isParent = false;
            feeChild.isChild = true;
            feeChild.parentIdx = parentIndex;
            feeChild.idx = childIdx++;
            feeChild.debit = fee;                // Cost to wallet
            feeChild.credit = 0;
            feeChild.netChange = fee;
            
            parts.append(feeChild);
        }
    }

    return parts;
}

/**
 * Update transaction status based on current blockchain state.
 * 
 * STATUS LIFECYCLE:
 * 1. OpenUntilBlock/OpenUntilDate: Transaction has locktime not yet reached
 * 2. Unconfirmed: In mempool, depth=0
 * 3. Confirming: In block but < RecommendedNumConfirmations (6)
 * 4. Confirmed: Has 6+ confirmations
 * 5. Conflicted: Double-spend detected, depth=-1
 * 6. Abandoned: Manually abandoned from wallet
 * 
 * For generated (mined) transactions:
 * 1. Immature: Waiting for coinbase maturity (100 blocks)
 * 2. MaturesWarning: Orphaned block (won't mature)
 * 3. NotAccepted: Mined but not in main chain
 * 4. Confirmed: Fully matured
 * 
 * SORT KEY:
 * Format: <height>-<is_coinbase>-<received_time>-<idx>
 * - Unconfirmed transactions sort to top (height=max_int)
 * - Coinbase transactions sort after regular at same height
 * - Within same height/type, sort by receive time
 * - Child records sort by idx within parent
 * 
 * @param wtx Core wallet transaction with current block information
 */
void TransactionRecord::updateStatus(const CWalletTx &wtx)
{
    AssertLockHeld(cs_main);
    
    // Locate block containing this transaction (nullptr if unconfirmed)
    CBlockIndex* pindex = nullptr;
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Generate sort key for transaction list ordering
    // Format: height-coinbase-time-idx
    // Unconfirmed transactions use max height to sort to top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        wtx.nTimeReceived,
        idx);
    
    // Balance calculation: trusted transactions that have matured
    // Note: Second assignment overrides first (keeps all transactions in balance)
    status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
    status.countsForBalance = true;
    
    status.depth = wtx.GetDepthInMainChain();
    status.cur_num_blocks = chainActive.Height();

    // Check if transaction has time/block lock preventing finalization
    if (!CheckFinalTx(wtx))
    {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD)
        {
            // Block-based locktime
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainActive.Height();
        }
        else
        {
            // Time-based locktime
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // Handle generated (mined) transaction maturity
    else if(type == TransactionRecord::Generated)
    {
        // Coinbase transactions require maturity period (100 blocks)
        if (wtx.GetBlocksToMaturity() > 0)
        {
            status.status = TransactionStatus::Immature;

            if (wtx.IsInMainChain())
            {
                // In main chain - count down to maturity
                status.matures_in = wtx.GetBlocksToMaturity();
            }
            else
            {
                // Orphaned block - will never mature
                status.status = TransactionStatus::NotAccepted;
            }
        }
        else
        {
            // Fully matured
            status.status = TransactionStatus::Confirmed;
        }
    }
    else
    {
        // Regular (non-generated) transaction status
        if (status.depth < 0)
        {
            // Conflicting transaction (double-spend detected)
            status.status = TransactionStatus::Conflicted;
        }
        else if (status.depth == 0)
        {
            // In mempool (unconfirmed)
            status.status = TransactionStatus::Unconfirmed;
            if (wtx.isAbandoned())
                status.status = TransactionStatus::Abandoned;
        }
        else if (status.depth < RecommendedNumConfirmations)
        {
            // In block but awaiting recommended confirmations (< 6)
            status.status = TransactionStatus::Confirming;
        }
        else
        {
            // Fully confirmed (6+ confirmations)
            status.status = TransactionStatus::Confirmed;
        }
    }
    
    status.needsUpdate = false;
}

/**
 * Determine if transaction status needs refresh.
 * 
 * Status becomes stale when:
 * - Block height changes (confirmations increase)
 * - Explicit update flag set (transaction modified)
 * 
 * This prevents unnecessary status recalculation on every UI update.
 */
bool TransactionRecord::statusUpdateNeeded() const
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height() || status.needsUpdate;
}

/**
 * Get transaction ID as QString for display and identification.
 */
QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

/**
 * Get output index (child position) for this transaction record.
 * Used for sorting and identifying specific child records.
 */
int TransactionRecord::getOutputIndex() const
{
    return idx;
}
