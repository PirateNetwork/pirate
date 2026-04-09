// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "asyncrpcoperation_saplingconsolidation_address.h"
#include "init.h"
#include "key_io.h"
#include "sync.h"
#include "tinyformat.h"
#include "transaction_builder.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"

// Global configuration variables
/**
 * Number of blocks to set as expiration delta for consolidation transactions
 * This provides sufficient time for transaction confirmation while preventing
 * transactions from staying in mempool indefinitely
 */
const int CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Sapling address consolidation operation
 * 
 * @param targetHeight Target blockchain height for consolidation operations
 * @param address Sapling payment address to consolidate notes for
 * @param spendingKey Extended spending key for the address
 * @param fee Fee amount in zatoshis per consolidation transaction
 * @param maxNotes Maximum number of notes per consolidation transaction
 * @param maxTransactions Maximum number of transactions to create
 */
AsyncRPCOperation_saplingconsolidation_address::AsyncRPCOperation_saplingconsolidation_address(
    int targetHeight, 
    const libzcash::SaplingPaymentAddress& address,
    const libzcash::SaplingExtendedSpendingKey& spendingKey,
    CAmount fee, 
    int maxNotes,
    int maxTransactions) 
    : targetHeight_(targetHeight), address_(address), spendingKey_(spendingKey), fee_(fee), maxNotes_(maxNotes), maxTransactions_(maxTransactions) {}

/**
 * @brief Destructor - automatically cleans up resources
 */
AsyncRPCOperation_saplingconsolidation_address::~AsyncRPCOperation_saplingconsolidation_address() {}

/**
 * @brief Main execution wrapper for Sapling address consolidation operation
 * 
 * This method is the main entry point for executing the consolidation operation.
 * It performs the following steps:
 * 1. Validates the operation state and handles cancellation
 * 2. Sets up execution timing and state management
 * 3. Calls the core consolidation logic
 * 4. Handles exceptions with detailed error reporting
 * 5. Updates final operation status and logs results
 * 
 * The consolidation process combines multiple Sapling notes into fewer notes
 * to improve wallet performance and reduce transaction complexity.
 */
void AsyncRPCOperation_saplingconsolidation_address::main()
{
    if (isCancelled()) {
        return;
    }

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
    } catch (const std::runtime_error& e) {
        set_error_code(-1);
        set_error_message("Runtime error: " + std::string(e.what()));
    } catch (const std::logic_error& e) {
        set_error_code(-1);
        set_error_message("Logic error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        set_error_code(-1);
        set_error_message("General exception: " + std::string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("Unknown error occurred during consolidation");
    }

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string logMessage = strprintf("%s: Sapling Address Consolidation complete. (status=%s", getId(), getStateAsString());
    if (success) {
        logMessage += strprintf(", success)\n");
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", logMessage);
}

bool AsyncRPCOperation_saplingconsolidation_address::main_impl() {
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_saplingconsolidation_address for address %s.\n",
             getId(), EncodePaymentAddress(address_));
    
    // Get consensus parameters and check for network upgrade compatibility
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>(), 0, 0);
        return true;
    }

    int numTxCreated = 0;
    int processedNotes = 0;
    int txAttempted = 0; // incremented after feasibility passes, before commit
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;

    // filterAddresses is fixed for the lifetime of this operation.
    std::set<libzcash::PaymentAddress> filterAddresses;
    filterAddresses.insert(address_);

    // === Batch Processing Loop ===
    // Each iteration fetches a fresh bounded set of unspent notes for this address.
    // After CommitAutomatedTx the newly spent notes are excluded by ignoreSpent=true,
    // so the next call naturally returns a different working set without any manual
    // index tracking.
    while (numTxCreated < maxTransactions_) {
        if (isCancelled() || ShutdownRequested()) {
            LogPrint("zrpcunsafe", "%s: Stopping consolidation loop (cancelled or shutdown).\n", getId());
            break;
        }
        // === STEP 1: Note Discovery ===
        // GetFilteredNotes uses a dual-heap streaming algorithm to return up to maxNotes_
        // notes (half smallest + half largest by value) for this address, exiting early
        // as soon as the aggregate value meets the fee_ + 1 threshold.
        std::vector<SaplingNoteEntry> saplingEntries;
        std::vector<OrchardNoteEntry> orchardEntries;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, filterAddresses,
                                          11, INT_MAX, true, true, true,
                                          maxNotes_, fee_ + 1);
            // Lock immediately so no other async operation can select the same notes.
            for (const auto& e : saplingEntries)
                pwalletMain->LockNote(e.op);
        }

        auto unlockSaplingEntries = [&]() {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            for (const auto& e : saplingEntries)
                pwalletMain->UnlockNote(e.op);
        };

        // Compute total aggregate value of this iteration's working set.
        CAmount amountToSend = 0;
        for (const auto& e : saplingEntries)
            amountToSend += CAmount(e.note.value());

        LogPrint("zrpcunsafe", "%s: Working set: %d notes, aggregate=%s for address %s\n",
                 getId(), (int)saplingEntries.size(), FormatMoney(amountToSend), EncodePaymentAddress(address_));

        // === STEP 2: Feasibility Check ===
        // Require at least 2 notes AND total value > fee to form a valid transaction.
        if (saplingEntries.size() < 2) {
            LogPrint("zrpcunsafe", "%s: Not enough notes to consolidate (need at least 2, found %d)\n",
                     getId(), (int)saplingEntries.size());
            unlockSaplingEntries();
            break;
        }
        if (amountToSend <= fee_) {
            LogPrint("zrpcunsafe", "%s: Working set aggregate value %s does not exceed fee %s — "
                     "all selected notes are dust relative to fee, nothing to consolidate\n",
                     getId(), FormatMoney(amountToSend), FormatMoney(fee_));
            unlockSaplingEntries();
            break;
        }

        const CAmount outputAmount = amountToSend - fee_;

        LogPrint("zrpcunsafe", "%s: Selected %d notes for batch (total amount=%s, fee=%s, net=%s)\n",
                 getId(), (int)saplingEntries.size(), FormatMoney(amountToSend),
                 FormatMoney(fee_), FormatMoney(outputAmount));

        txAttempted++;

        // === STEP 3: Cryptographic Preparation ===
        // Build ops and notes vectors from the full working set returned by GetFilteredNotes.
        std::vector<SaplingOutPoint> ops;
        std::vector<libzcash::SaplingNote> notes;
        for (const auto& entry : saplingEntries) {
            ops.push_back(entry.op);
            notes.push_back(entry.note);
        }

        // Fetch Sapling anchor and merkle paths for cryptographic validation
        uint256 anchor;
        std::vector<libzcash::MerklePath> saplingMerklePaths;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
                LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling notes. Stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }
        }

        // === STEP 4: Transaction Construction ===
        auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            builder.SetExpiryHeight(chainActive.Tip()->nHeight + CONSOLIDATION_EXPIRY_DELTA);
        }

        LogPrint("zrpcunsafe", "%s: Creating consolidation transaction with input amount=%s, fee=%s, output amount=%s\n",
                 getId(), FormatMoney(amountToSend), FormatMoney(fee_), FormatMoney(outputAmount));

        // Add Sapling spends (inputs) using two-step process
        // Step 1: Add raw spends with note data and merkle paths
        bool buildFailed = false;
        for (size_t i = 0; i < notes.size(); i++) {
            if (!builder.AddSaplingSpendRaw(ops[i], address_, notes[i].value(), notes[i].rcm(), saplingMerklePaths[i], anchor)) {
                LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Spend failed. Stopping.\n", getId());
                buildFailed = true;
                break;
            }
        }
        if (buildFailed) {
            unlockSaplingEntries();
            break;
        }

        // Step 2: Convert raw spends using spending key
        if (!builder.ConvertRawSaplingSpend(spendingKey_)) {
            LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Spends failed. Stopping.\n", getId());
            unlockSaplingEntries();
            break;
        }

        // Set transaction fee and create single consolidated output
        builder.SetFee(fee_);

        // Add consolidated output using raw method then convert
        if (!builder.AddSaplingOutputRaw(address_, outputAmount)) {
            LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
            unlockSaplingEntries();
            break;
        }

        libzcash::SaplingFullViewingKey fvk;
        spendingKey_.expsk.DeriveFVK(&fvk);
        if (!builder.ConvertRawSaplingOutput(fvk.ovk)) {
            LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
            unlockSaplingEntries();
            break;
        }

        // === STEP 5: Commit ===
        auto buildResult = builder.Build();
        if (!buildResult.IsTx()) {
            LogPrint("zrpcunsafe", "%s: Failed to build consolidation transaction: %s. Stopping.\n", getId(), buildResult.GetError());
            unlockSaplingEntries();
            break;
        }
        auto tx = buildResult.GetTxOrThrow();

        // Check for cancellation before committing
        if (isCancelled() || ShutdownRequested()) {
            LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
            unlockSaplingEntries();
            break;
        }

        // Commit transaction to wallet and broadcast to network
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->CommitAutomatedTx(tx)) {
                LogPrint("zrpcunsafe", "%s: Failed to commit consolidation transaction, stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }
        }

        LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n",
                 getId(), tx.GetHash().ToString());
        unlockSaplingEntries();
        amountConsolidated += outputAmount;
        numTxCreated++;
        processedNotes += (int)saplingEntries.size();
        consolidationTxIds.push_back(tx.GetHash().ToString());
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s\n", 
             getId(), numTxCreated, FormatMoney(amountConsolidated));

    // Count remaining unspent notes for this address after consolidation.
    // Use filterAddresses (already scoped to address_) to avoid a full-wallet scan.
    int remainingNotes = 0;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::vector<SaplingNoteEntry> remainingSapling;
        std::vector<OrchardNoteEntry> remainingOrchard;
        pwalletMain->GetFilteredNotes(remainingSapling, remainingOrchard, filterAddresses, 11, INT_MAX, true, true, false);
        remainingNotes = (int)remainingSapling.size();
    }

    setConsolidationResult(numTxCreated, amountConsolidated, consolidationTxIds, processedNotes, remainingNotes);

    // Return false if transactions were attempted (feasibility passed) but none committed.
    // A clean stop (nothing to consolidate) still returns true.
    if (txAttempted > 0 && numTxCreated == 0)
        return false;

    return true;
}

/**
 * @brief Set consolidation operation results
 * 
 * Formats and stores the results of the consolidation operation for reporting
 * via the getStatus() method and operation completion callbacks.
 * 
 * @param numTxCreated Number of consolidation transactions successfully created
 * @param amountConsolidated Total amount consolidated across all transactions (in zatoshis)
 * @param consolidationTxIds Vector of transaction IDs for all created consolidation transactions
 * @param notesConsolidated Number of individual notes spent across all transactions
 * @param notesRemaining Number of unspent notes remaining after consolidation
 */
void AsyncRPCOperation_saplingconsolidation_address::setConsolidationResult(
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
 * @brief Cancel the consolidation operation
 * 
 * Sets the operation state to cancelled. The operation will check for
 * cancellation at safe points and stop processing gracefully.
 */
void AsyncRPCOperation_saplingconsolidation_address::cancel() {
    set_state(OperationStatus::CANCELLED);
}

UniValue AsyncRPCOperation_saplingconsolidation_address::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "saplingconsolidation_address"));
    obj.push_back(Pair("target_height", targetHeight_));
    obj.push_back(Pair("address", EncodePaymentAddress(address_)));
    obj.push_back(Pair("fee", FormatMoney(fee_)));
    obj.push_back(Pair("max_notes", maxNotes_));
    obj.push_back(Pair("max_transactions", maxTransactions_));
    obj.push_back(Pair("has_spending_key", true)); // We always have it since we got it during construction
    return obj;
}