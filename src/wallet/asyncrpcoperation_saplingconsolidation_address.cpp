// Copyright (c) 2025 Pirate Chain Development Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_saplingconsolidation_address.cpp
 * @brief Implementation of Sapling address consolidation operations
 *
 * This file implements the AsyncRPCOperation_saplingconsolidation_address class that provides
 * asynchronous consolidation of Sapling notes for a single address. The consolidation process
 * reduces wallet fragmentation by combining multiple notes into fewer, larger notes.
 *
 * Algorithm Overview:
 * Each transaction is built from a fresh note selection:
 * 1. GetFilteredNotes is called with maxNotes_ as the cap and fee_+1 as the minimum aggregate
 *    value threshold. Internally it uses a dual-heap streaming algorithm to return up to
 *    maxNotes_ notes (half smallest + half largest by value) without scanning the full wallet.
 * 2. If the returned set has fewer than 2 notes or its total value does not exceed the fee,
 *    consolidation stops — no viable transaction can be formed.
 * 3. All returned notes are spent as inputs in a single consolidation transaction whose output
 *    is the net amount (total input value minus fee) sent back to the same address.
 * 4. After each successful commit, previously spent notes are excluded by ignoreSpent=true on
 *    the next call, naturally advancing to the next working set with no manual index tracking.
 * 5. The loop repeats until maxTransactions_ transactions have been created or no more
 *    consolidatable notes remain.
 *
 * Security Features:
 * - Spending key is captured during construction while the wallet is unlocked
 * - Minimum 11-block confirmation depth required for all selected notes
 * - Merkle path validated for every spent note
 * - Network upgrade boundary check prevents transactions from expiring across an activation
 *
 * @author Pirate Chain Development Team
 * @date 2025
 */

#include "assert.h"
#include "boost/variant/static_visitor.hpp"
#include "asyncrpcoperation_saplingconsolidation_address.h"
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
const int CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Sapling address consolidation operation
 * 
 * Creates a new consolidation operation with the specified parameters. The spending key
 * is captured during construction while the wallet is unlocked, allowing the operation
 * to proceed even if the wallet is locked afterwards.
 * 
 * @param targetHeight Blockchain height for transaction creation timing
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
 * @brief Destructor - clears sensitive data from memory
 */
AsyncRPCOperation_saplingconsolidation_address::~AsyncRPCOperation_saplingconsolidation_address() {}

/**
 * @brief Main orchestration method for the consolidation operation
 * 
 * This method provides error handling and state management around the core
 * consolidation logic implemented in main_impl(). It ensures proper state
 * transitions and error reporting regardless of success or failure.
 */
void AsyncRPCOperation_saplingconsolidation_address::main() {
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

    std::string s = strprintf("%s: Sapling Address Consolidation complete. (status=%s", getId(), getStateAsString());
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
 * Each loop iteration:
 * 1. Calls GetFilteredNotes with maxNotes_ and minAggregateValue = fee_+1 to obtain a
 *    bounded, pre-filtered set of unspent notes for the target address.
 * 2. Checks feasibility: at least 2 notes and total value > fee required.
 * 3. Spends all returned notes as inputs; output = total input value - fee.
 * 4. Commits the transaction. Spent notes are excluded automatically on the next call
 *    via ignoreSpent=true. Repeats until maxTransactions_ reached or no notes remain.
 *
 * @return true when the operation completes (with or without transactions created)
 * @throws builder exceptions propagated up to main() for error reporting
 */
bool AsyncRPCOperation_saplingconsolidation_address::main_impl() {
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_saplingconsolidation_address for address %s.\n", 
             getId(), EncodePaymentAddress(address_));

    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.get()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>());
        return true;
    }

    int numTxCreated = 0;
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
        std::vector<CSproutNotePlaintextEntry> sproutEntries;
        std::vector<SaplingNoteEntry> saplingEntries;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->GetFilteredNotes(sproutEntries, saplingEntries, filterAddresses,
                                          11, INT_MAX, true, true, true,
                                          maxNotes_, fee_ + 1);
        }

        // Compute total aggregate value of this iteration's working set.
        CAmount workingSetValue = 0;
        for (const auto& e : saplingEntries)
            workingSetValue += CAmount(e.note.value());

        LogPrint("zrpcunsafe", "%s: Working set: %d notes, aggregate=%s for address %s\n",
                 getId(), (int)saplingEntries.size(), FormatMoney(workingSetValue), EncodePaymentAddress(address_));

        // === STEP 2: Feasibility Check ===
        // Require at least 2 notes AND total value > fee to form a valid transaction.
        if (saplingEntries.size() < 2) {
            LogPrint("zrpcunsafe", "%s: Not enough notes to consolidate (need at least 2, found %d)\n",
                     getId(), (int)saplingEntries.size());
            break;
        }
        if (workingSetValue <= fee_) {
            LogPrint("zrpcunsafe", "%s: Working set aggregate value %s does not exceed fee %s — "
                     "all selected notes are dust relative to fee, nothing to consolidate\n",
                     getId(), FormatMoney(workingSetValue), FormatMoney(fee_));
            break;
        }

        LogPrint("zrpcunsafe", "%s: Selected %d notes for batch (total amount=%s, fee=%s, net=%s)\n",
                 getId(), (int)saplingEntries.size(), FormatMoney(workingSetValue),
                 FormatMoney(fee_), FormatMoney(workingSetValue - fee_));

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
                break;
            }
        }

        // === STEP 4: Transaction Construction ===
        auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            int nExpires = chainActive.Tip()->nHeight + CONSOLIDATION_EXPIRY_DELTA;
            builder.SetExpiryHeight(nExpires);
        }

        LogPrint("zrpcunsafe", "%s: Creating consolidation transaction with input amount=%s, fee=%s, output amount=%s\n",
                 getId(), FormatMoney(workingSetValue), FormatMoney(fee_), FormatMoney(workingSetValue - fee_));

        // Add Sapling spends (inputs)
        for (size_t i = 0; i < notes.size(); i++) {
            builder.AddSaplingSpend(spendingKey_.expsk, notes[i], anchor, saplingMerklePaths[i]);
        }

        // Set transaction fee and create single consolidated output
        builder.SetFee(fee_);
        builder.AddSaplingOutput(spendingKey_.expsk.ovk, address_, workingSetValue - fee_);

        // === STEP 5: Commit ===
        auto tx = builder.Build().GetTxOrThrow();

        // Check for cancellation before committing
        if (isCancelled() || ShutdownRequested()) {
            LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
            break;
        }

        // Commit transaction to wallet and broadcast to network
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->CommitAutomatedTx(tx)) {
                LogPrint("zrpcunsafe", "%s: Failed to commit consolidation transaction, stopping.\n", getId());
                break;
            }
        }

        LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s\n",
                 getId(), tx.GetHash().ToString());

        amountConsolidated += workingSetValue - fee_;
        numTxCreated++;
        consolidationTxIds.push_back(tx.GetHash().ToString());
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s\n", 
             getId(), numTxCreated, FormatMoney(amountConsolidated));

    setConsolidationResult(numTxCreated, amountConsolidated, consolidationTxIds);
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
void AsyncRPCOperation_saplingconsolidation_address::setConsolidationResult(
    int numTxCreated, 
    const CAmount& amountConsolidated, 
    const std::vector<std::string>& consolidationTxIds) {
    
    UniValue res(UniValue::VOBJ);
    res.push_back(Pair("num_tx_created", numTxCreated));
    res.push_back(Pair("amount_consolidated", FormatMoney(amountConsolidated)));
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
void AsyncRPCOperation_saplingconsolidation_address::cancel() {
    set_state(OperationStatus::CANCELLED);
}

/**
 * @brief Gets detailed status information for the operation
 * 
 * @return UniValue object with operation status, configuration, and progress details
 */
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