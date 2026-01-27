// Copyright (c) 2025 Pirate Chain Development Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_orchardconsolidation_address.cpp
 * @brief Implementation of Orchard address consolidation operations
 * 
 * This file implements the AsyncRPCOperation_orchardconsolidation_address class that provides
 * asynchronous consolidation of Orchard notes. The consolidation process helps reduce wallet
 * fragmentation by combining multiple small notes into fewer, larger notes.
 * 
 * Algorithm Overview:
 * The consolidation uses a two-step intelligent selection process:
 * 1. Fee Coverage Phase: Select smallest notes first until total value exceeds the required fee
 * 2. Optimization Phase: Add additional notes up to maxNotes limit to maximize consolidation efficiency
 * 
 * This approach ensures that:
 * - Consolidation transactions can always pay their fees
 * - Small "dust" notes are prioritized for cleanup
 * - Batch sizes are optimized within memory and processing constraints
 * 
 * Security Features:
 * - Spending keys are captured during construction while wallet is unlocked
 * - Minimum 11-block confirmation requirement for note selection
 * - Merkle path validation for all spent notes
 * - Proper handling of network upgrade boundaries
 * 
 * @author Pirate Chain Development Team
 * @date 2025
 */

#include "assert.h"
#include "boost/variant/static_visitor.hpp"
#include "asyncrpcoperation_orchardconsolidation_address.h"
#include "init.h"
#include "key_io.h"
#include "rpc/protocol.h"
#include "random.h"
#include "sync.h"
#include "tinyformat.h"
#include "transaction_builder.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"

/// @brief Expiry delta for consolidation transactions (blocks)
/// @details Consolidation transactions expire 40 blocks after creation to ensure
///          they don't become stale during network upgrade periods
const int ORCHARD_CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Orchard address consolidation operation
 * 
 * Creates a new consolidation operation with the specified parameters. The spending key
 * is captured during construction while the wallet is unlocked, allowing the operation
 * to proceed even if the wallet is locked afterwards.
 * 
 * @param targetHeight Blockchain height for transaction creation timing
 * @param address Orchard payment address to consolidate notes for
 * @param spendingKey Extended spending key for the address
 * @param fee Fee amount in zatoshis per consolidation transaction
 * @param maxNotes Maximum number of notes per consolidation transaction
 * @param maxTransactions Maximum number of transactions to create
 */
AsyncRPCOperation_orchardconsolidation_address::AsyncRPCOperation_orchardconsolidation_address(
    int targetHeight, 
    const libzcash::OrchardPaymentAddressPirate& address,
    const libzcash::OrchardExtendedSpendingKeyPirate& spendingKey,
    CAmount fee, 
    int maxNotes,
    int maxTransactions) 
    : targetHeight_(targetHeight), address_(address), spendingKey_(spendingKey), fee_(fee), maxNotes_(maxNotes), maxTransactions_(maxTransactions) {}

/**
 * @brief Destructor - clears sensitive data from memory
 */
AsyncRPCOperation_orchardconsolidation_address::~AsyncRPCOperation_orchardconsolidation_address() {}

/**
 * @brief Main orchestration method for the consolidation operation
 * 
 * This method provides error handling and state management around the core
 * consolidation logic implemented in main_impl(). It ensures proper state
 * transitions and error reporting regardless of success or failure.
 */
void AsyncRPCOperation_orchardconsolidation_address::main() {
    if (isCancelled())
        return;

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

    try {
        success = main_impl();
    } catch (const UniValue& objError) {
        int code = find_value(objError, "code").get_int();
        std::string message = find_value(objError, "message").get_str();
        set_error_code(code);
        set_error_message(message);
    } catch (const runtime_error& e) {
        set_error_code(-1);
        set_error_message("runtime error: " + string(e.what()));
    } catch (const logic_error& e) {
        set_error_code(-1);
        set_error_message("logic error: " + string(e.what()));
    } catch (const exception& e) {
        set_error_code(-1);
        set_error_message("general exception: " + string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("unknown error");
    }

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string s = strprintf("%s: Orchard Address Consolidation complete. (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", success)\n");
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", s);
}

/**
 * @brief Core consolidation implementation
 * 
 * Implements the complete consolidation algorithm:
 * 1. Validates timing against network upgrades
 * 2. Retrieves and filters notes for the target address
 * 3. Implements intelligent two-step note selection:
 *    a) Select smallest notes to cover fee requirements
 *    b) Add additional notes up to maxNotes limit for efficiency
 * 4. Creates consolidation transactions using TransactionBuilder
 * 5. Commits transactions and tracks results
 * 
 * The algorithm prioritizes small notes for cleanup while ensuring each
 * transaction can pay its required fees. It respects both note count and
 * transaction count limits to prevent resource exhaustion.
 * 
 * @return true if consolidation completed successfully, false on error
 * @throws Various exceptions on transaction building or commitment failures
 */
bool AsyncRPCOperation_orchardconsolidation_address::main_impl() {
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_orchardconsolidation_address for address %s.\n", 
             getId(), EncodePaymentAddress(address_));

    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + ORCHARD_CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>(), 0, 0);
        return true;
    }

    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;

    // === STEP 1: Note Discovery and Filtering ===
    // Get all confirmed notes and filter to target address only
    std::vector<SaplingNoteEntry> saplingEntries;
    std::vector<OrchardNoteEntry> orchardEntries;
    std::set<libzcash::PaymentAddress> filterAddresses;
    filterAddresses.insert(address_);

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        // We set minDepth to 11 to avoid unconfirmed notes and ensure stability
        pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, "", 11);
    }

    // Filter entries to only include notes for our target address
    std::vector<OrchardNoteEntry> targetEntries;
    for (const auto& entry : orchardEntries) {
        if (entry.address == address_) {
            targetEntries.push_back(entry);
        }
    }

    LogPrint("zrpcunsafe", "%s: Found %d notes for address %s\n", 
             getId(), targetEntries.size(), EncodePaymentAddress(address_));

    // === STEP 2: Consolidation Feasibility Check ===
    // Check if we have enough notes to consolidate
    if (targetEntries.size() < 2) {
        LogPrint("zrpcunsafe", "%s: Not enough notes to consolidate (need at least 2, found %d)\n", 
                 getId(), targetEntries.size());
        setConsolidationResult(0, 0, std::vector<std::string>(), 0, targetEntries.size());
        return true;
    }

    // === STEP 3: Note Preparation and Sorting ===
    // Sort notes by value (smallest first) for optimal consolidation algorithm
    // This ensures small "dust" notes are prioritized for cleanup
    std::sort(targetEntries.begin(), targetEntries.end(), 
              [](const OrchardNoteEntry& a, const OrchardNoteEntry& b) {
                  return a.note.value() < b.note.value();
              });

    // Use the spending key that was retrieved when the operation was created (while wallet was unlocked)
    // This ensures the operation can proceed even if the wallet gets locked afterwards

    CCoinsViewCache coinsView(pcoinsTip);

    // === STEP 4: Intelligent Batch Processing Loop ===
    // Process notes in batches using the two-step intelligent selection algorithm
    size_t processedNotes = 0;
    while (processedNotes < targetEntries.size() - 1 && numTxCreated < maxTransactions_) { // Leave at least one note unconsolidated and respect transaction limit
        std::vector<OrchardNoteEntry> batchEntries;
        CAmount batchAmount = 0;
        
        // Step 1: Collect enough notes to exceed the fee amount (smallest first)
        size_t remainingNotes = targetEntries.size() - processedNotes;
        if (remainingNotes < 2) break; // Need at least 2 notes to consolidate
        
        // Check transaction limit to prevent resource exhaustion
        if (numTxCreated >= maxTransactions_) {
            LogPrint("zrpcunsafe", "%s: Reached maximum number of transactions (%d), stopping\n", 
                     getId(), maxTransactions_);
            break;
        }
        
        // === SUBSTEP 4A: Fee Coverage Phase ===
        // Start with smallest notes until we have enough to cover the fee
        for (size_t i = processedNotes; i < targetEntries.size() && batchAmount <= fee_; i++) {
            batchEntries.push_back(targetEntries[i]);
            batchAmount += CAmount(targetEntries[i].note.value());
        }
        
        // Validate fee coverage - abort if insufficient funds remain
        if (batchAmount <= fee_) {
            LogPrint("zrpcunsafe", "%s: Remaining notes (%d) insufficient to cover fee %s (total: %s), stopping\n", 
                     getId(), remainingNotes, FormatMoney(fee_), FormatMoney(batchAmount));
            break;
        }
        
        // === SUBSTEP 4B: Optimization Phase ===
        // Add remaining notes up to maxNotes_ limit for maximum consolidation efficiency
        size_t currentBatchSize = batchEntries.size();
        for (size_t i = processedNotes + currentBatchSize; 
             i < targetEntries.size() && batchEntries.size() < (size_t)maxNotes_; 
             i++) {
            batchEntries.push_back(targetEntries[i]);
            batchAmount += CAmount(targetEntries[i].note.value());
        }

        LogPrint("zrpcunsafe", "%s: Selected %d notes for batch (total amount=%s, fee=%s, net=%s)\n", 
                 getId(), batchEntries.size(), FormatMoney(batchAmount), 
                 FormatMoney(fee_), FormatMoney(batchAmount - fee_));

        // === SUBSTEP 4C: Cryptographic Preparation ===
        // Select Orchard notes for spending and prepare cryptographic data
        std::vector<OrchardOutPoint> ops;
        std::vector<libzcash::OrchardNote> notes;
        for (const auto& entry : batchEntries) {
            ops.push_back(entry.op);
            notes.push_back(entry.note);
        }

        // Fetch Orchard anchor and merkle paths for cryptographic validation
        uint256 anchor;
        std::vector<libzcash::MerklePath> orchardMerklePaths;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            
            // Get merkle paths for all notes
            for (size_t i = 0; i < ops.size(); i++) {
                libzcash::MerklePath orchardMerklePath;
                if (!pwalletMain->OrchardWalletGetMerklePathOfNote(ops[i].hash, ops[i].n, orchardMerklePath)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Orchard note. Stopping.\n", getId());
                    break;
                }
                orchardMerklePaths.push_back(orchardMerklePath);
            }
            
            // Verify we got all paths
            if (orchardMerklePaths.size() != ops.size()) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Failed to get merkle paths for all notes. Stopping.\n", getId()));
            }
            
            // Get anchor from first note
            if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardMerklePaths[0], notes[0].cmx(), anchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Getting Anchor failed. Stopping.\n", getId()));
            }
        }

        // === SUBSTEP 4D: Transaction Construction ===
        // Build the consolidation transaction using TransactionBuilder
        auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            int nExpires = chainActive.Tip()->nHeight + ORCHARD_CONSOLIDATION_EXPIRY_DELTA;
            builder.SetExpiryHeight(nExpires);
        }

        LogPrint("zrpcunsafe", "%s: Creating consolidation transaction with input amount=%s, fee=%s, output amount=%s\n", 
                 getId(), FormatMoney(batchAmount), FormatMoney(fee_), FormatMoney(batchAmount - fee_));

        // Initialize Orchard with spending enabled
        builder.InitializeOrchard(true, true, anchor);

        // Add Orchard spends (inputs) using two-step process
        // Step 1: Add raw spends with note data and merkle paths
        for (size_t i = 0; i < notes.size(); i++) {
            if (!builder.AddOrchardSpendRaw(ops[i], address_, notes[i].value(), notes[i].rho(), notes[i].rseed(), orchardMerklePaths[i], anchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Orchard Spend failed. Stopping.\n", getId()));
            }
        }

        // Step 2: Convert raw spends using spending key
        if (!builder.ConvertRawOrchardSpend(spendingKey_)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Orchard Spends failed.\n", getId()));
        }

        // Set transaction fee and create single consolidated output
        builder.SetFee(fee_);
        
        // Add consolidated output using raw method then convert
        if (!builder.AddOrchardOutputRaw(address_, batchAmount - fee_)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Orchard Output failed. Stopping.\n", getId()));
        }
        
        // Get OVK from spending key for output conversion
        auto fvkOpt = spendingKey_.GetXFVK();
        if (fvkOpt == std::nullopt) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: FVK not found for Orchard spending key. Stopping.\n", getId()));
        }
        
        auto fvk = fvkOpt.value().fvk;
        auto ovkOpt = fvk.GetOVK();
        if (ovkOpt == std::nullopt) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: OVK not found for Orchard spending key. Stopping.\n", getId()));
        }
        
        if (!builder.ConvertRawOrchardOutput(ovkOpt.value().ovk)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Orchard Output failed.\n", getId()));
        }

        // === SUBSTEP 4E: Transaction Finalization ===
        // Build and commit the consolidation transaction to the blockchain
        auto tx = builder.Build().GetTxOrThrow();

        // Check for cancellation before committing
        if (isCancelled()) {
            LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
            break;
        }

        // Commit transaction to wallet and broadcast to network
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->CommitAutomatedTx(tx)) {
                throw runtime_error("Failed to commit consolidation transaction");
            }
        }

        LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n", 
                 getId(), tx.GetHash().ToString());

        amountConsolidated += batchAmount - fee_;
        numTxCreated++;
        consolidationTxIds.push_back(tx.GetHash().ToString());
        processedNotes += batchEntries.size();
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s\n", 
             getId(), numTxCreated, FormatMoney(amountConsolidated));

    // Count remaining unspent notes after consolidation
    int remainingNotes = 0;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::vector<SaplingNoteEntry> saplingEntries;
        std::vector<OrchardNoteEntry> orchardEntries;
        pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, "", 11);
        
        for (const auto& entry : orchardEntries) {
            if (entry.address == address_) {
                remainingNotes++;
            }
        }
    }

    setConsolidationResult(numTxCreated, amountConsolidated, consolidationTxIds, processedNotes, remainingNotes);
    return true;
}

/**
 * @brief Sets the final result of the consolidation operation
 * 
 * Creates a structured result object containing all relevant information about
 * the consolidation operation including transaction count, amounts, and IDs.
 * This result can be retrieved via z_getoperationstatus or z_getoperationresult.
 * 
 * @param numTxCreated Number of consolidation transactions successfully created
 * @param amountConsolidated Total amount consolidated (excluding fees paid)
 * @param consolidationTxIds Vector of transaction IDs for tracking on blockchain
 */
void AsyncRPCOperation_orchardconsolidation_address::setConsolidationResult(
    int numTxCreated, 
    const CAmount& amountConsolidated, 
    const std::vector<std::string>& consolidationTxIds,
    int notesConsolidated,
    int notesRemaining) {
    
    UniValue res(UniValue::VOBJ);
    res.push_back(Pair("num_tx_created", numTxCreated));
    res.push_back(Pair("amount_consolidated", FormatMoney(amountConsolidated)));
    res.push_back(Pair("notes_consolidated", notesConsolidated));
    res.push_back(Pair("notes_remaining", notesRemaining));
    res.push_back(Pair("address", EncodePaymentAddress(address_)));
    res.push_back(Pair("fee_per_tx", FormatMoney(fee_)));
    res.push_back(Pair("max_notes_per_tx", maxNotes_));
    res.push_back(Pair("max_transactions", maxTransactions_));
    
    UniValue txIds(UniValue::VARR);
    for (const std::string& txId : consolidationTxIds) {
        txIds.push_back(txId);
    }
    res.push_back(Pair("consolidation_txids", txIds));
    set_result(res);
}

/**
 * @brief Cancels the consolidation operation
 * 
 * @note Transactions already committed to the blockchain cannot be reversed
 */
void AsyncRPCOperation_orchardconsolidation_address::cancel() {
    set_state(OperationStatus::CANCELLED);
}

/**
 * @brief Gets detailed status information for the operation
 * 
 * @return UniValue object with operation status, configuration, and progress details
 */
UniValue AsyncRPCOperation_orchardconsolidation_address::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "orchardconsolidation_address"));
    obj.push_back(Pair("target_height", targetHeight_));
    obj.push_back(Pair("address", EncodePaymentAddress(address_)));
    obj.push_back(Pair("fee", FormatMoney(fee_)));
    obj.push_back(Pair("max_notes", maxNotes_));
    obj.push_back(Pair("max_transactions", maxTransactions_));
    obj.push_back(Pair("has_spending_key", true)); // We always have it since we got it during construction
    return obj;
}
