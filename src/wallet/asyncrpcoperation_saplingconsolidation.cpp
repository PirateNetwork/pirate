// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "asyncrpcoperation_saplingconsolidation.h"
#include "assert.h"
#include "boost/variant/static_visitor.hpp"
#include "init.h"
#include "key_io.h"
#include "random.h"
#include "rpc/protocol.h"
#include "sync.h"
#include "tinyformat.h"
#include "transaction_builder.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"

// Global configuration variables
/**
 * Global variables for Sapling consolidation configuration
 * These are set during initialization from command line parameters
 */
CAmount fSaplingConsolidationTxFee = DEFAULT_SAPLING_CONSOLIDATION_FEE;
bool fSaplingConsolidationMapUsed = false;

/**
 * Number of blocks to set as expiration delta for consolidation transactions
 * This provides sufficient time for transaction confirmation while preventing
 * transactions from staying in mempool indefinitely
 */
const int SAPLING_CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Sapling consolidation operation
 * 
 * @param targetHeight Target blockchain height for consolidation operations
 */
AsyncRPCOperation_saplingconsolidation::AsyncRPCOperation_saplingconsolidation(int targetHeight) : targetHeight_(targetHeight) {}

/**
 * @brief Destructor - automatically cleans up resources
 */
AsyncRPCOperation_saplingconsolidation::~AsyncRPCOperation_saplingconsolidation() {}

/**
 * @brief Main execution wrapper for Sapling consolidation operation
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
void AsyncRPCOperation_saplingconsolidation::main()
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

    std::string logMessage = strprintf("%s: Sapling Consolidation routine complete. (status=%s", getId(), getStateAsString());
    if (success) {
        logMessage += strprintf(", success)\n");
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", logMessage);
}

/**
 * @brief Core implementation of Sapling consolidation logic
 * 
 * This method performs the actual consolidation work including:
 * 1. Network upgrade compatibility validation
 * 2. Note discovery and address mapping
 * 3. Transaction building and commitment
 * 4. Progress tracking and scheduling
 * 
 * The consolidation process runs in rounds, with each round potentially
 * creating multiple transactions to combine fragmented notes.
 * 
 * @return true if consolidation completed successfully, false otherwise
 */
bool AsyncRPCOperation_saplingconsolidation::main_impl()
{
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_saplingconsolidation.\n", getId());
    
    // Get consensus parameters and check for network upgrade compatibility
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + SAPLING_CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Consolidation txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        setConsolidationResult(0, 0, std::vector<std::string>());
        return true;
    }

    // Initialize consolidation tracking variables
    int64_t startTime = GetTime();
    bool roundComplete = false;
    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;
    int consolidationTarget = 0;

    // Main consolidation loop - process up to 50 rounds
    for (int roundIndex = 0; roundIndex < 50; roundIndex++) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        // Reset flag for each round
        roundComplete = false;
        
        // Data structures for current round
        std::vector<SaplingNoteEntry> saplingEntries;
        std::vector<OrchardNoteEntry> orchardEntries;
        std::set<libzcash::SaplingPaymentAddress> targetAddresses;
        std::map<libzcash::SaplingPaymentAddress, std::vector<SaplingNoteEntry>> addressToNotesMap;
        std::map<std::pair<int, int>, SaplingNoteEntry> sortedEntriesMap;
        
        {
            
            consolidationTarget = pwalletMain->targetSaplingConsolidationQty;
            
            // Get filtered notes with minimum depth of 11 for stability
            // This avoids unconfirmed notes and provides buffer for anchor selection
            pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, "", 11);
            
            // Build target address set if consolidation mapping is used
            if (fSaplingConsolidationMapUsed) {
                const std::vector<std::string>& configuredAddresses = mapMultiArgs["-consolidatesaplingaddress"];
                for (size_t i = 0; i < configuredAddresses.size(); i++) {
                    libzcash::PaymentAddress paymentAddr = DecodePaymentAddress(configuredAddresses[i]);
                    if (std::get_if<libzcash::SaplingPaymentAddress>(&paymentAddr) != nullptr) {
                        libzcash::SaplingPaymentAddress saplingAddr = *(std::get_if<libzcash::SaplingPaymentAddress>(&paymentAddr));
                        targetAddresses.insert(saplingAddr);
                    }
                }
            }

            // Sort entries by confirmation count and output index for consistent processing
            for (const auto& entry : saplingEntries) {
                auto sortKey = std::make_pair(entry.confirmations, entry.op.n);
                sortedEntriesMap.insert({sortKey, entry});
            }

            // Group notes by address - process in reverse order (most confirmed first)
            for (auto rit = sortedEntriesMap.rbegin(); rit != sortedEntriesMap.rend(); ++rit) {
                const SaplingNoteEntry& entry = rit->second;
                
                // Only process notes from target addresses (or all if no mapping)
                if (targetAddresses.count(entry.address) > 0 || !fSaplingConsolidationMapUsed) {
                    auto it = addressToNotesMap.find(entry.address);
                    if (it != addressToNotesMap.end()) {
                        it->second.push_back(entry);
                    } else {
                        std::vector<SaplingNoteEntry> noteList;
                        noteList.push_back(entry);
                        addressToNotesMap[entry.address] = noteList;
                    }
                }
            }
        }

        // Initialize coins view for transaction validation
        CCoinsViewCache coinsView(pcoinsTip);

        // Check if we have enough notes to justify consolidation
        if (saplingEntries.size() < static_cast<size_t>(consolidationTarget)) {
            // Not enough notes to consolidate - exit the loop
            LogPrint("zrpcunsafe", "%s: Not enough Sapling notes to consolidate (%zu < %d). Exiting round.\n", 
                     getId(), saplingEntries.size(), consolidationTarget);
            roundComplete = true;
            break;
        } else {

            // Process each address that has notes
            for (auto it = addressToNotesMap.begin(); it != addressToNotesMap.end(); ++it) {
                const auto& address = it->first;
                const auto& addressNotes = it->second;

                // Get the extended spending key for this address
                libzcash::SaplingExtendedSpendingKey extendedSpendingKey;
                if (!pwalletMain->GetSaplingExtendedSpendingKey(address, extendedSpendingKey)) {
                    // Skip addresses we don't have spending keys for
                    continue;
                }

                std::vector<SaplingNoteEntry> selectedInputs;
                CAmount totalInputAmount = 0;

                // Determine consolidation parameters
                int minNoteQuantity = rand() % 10 + 2;      // 2-11 notes minimum
                int maxNoteQuantity = rand() % 35 + 10;     // 10-44 notes maximum

                // Select notes from this address for consolidation
                for (const SaplingNoteEntry& noteEntry : addressNotes) {
                    libzcash::SaplingIncomingViewingKey incomingViewKey;
                    if (!pwalletMain->GetSaplingIncomingViewingKey(noteEntry.address, incomingViewKey)) {
                        continue; // Skip notes we can't get viewing keys for
                    }

                    // Verify this note belongs to the address we're processing
                    if (incomingViewKey == extendedSpendingKey.expsk.full_viewing_key().in_viewing_key() && 
                        noteEntry.address == address) {
                        totalInputAmount += CAmount(noteEntry.note.value());
                        selectedInputs.push_back(noteEntry);
                    }

                    // Limit the number of inputs to avoid oversized transactions
                    if (selectedInputs.size() >= static_cast<size_t>(maxNoteQuantity)) {
                        break;
                    }
                }

                // Skip if we don't have enough notes to consolidate
                if (selectedInputs.size() < static_cast<size_t>(minNoteQuantity)) {
                    continue;
                }

                // Calculate fee and output amount
                CAmount transactionFee = fSaplingConsolidationTxFee;
                if (totalInputAmount <= fSaplingConsolidationTxFee) {
                    transactionFee = 0; // Waive fee if input amount is too small
                }
                CAmount outputAmount = totalInputAmount - transactionFee;

                // Skip if output amount would be zero or negative
                if (outputAmount <= 0) {
                    LogPrint("zrpcunsafe", "%s: Output amount would be zero or negative (%s). Skipping this address.\n", 
                            getId(), FormatMoney(outputAmount));
                    continue;
                }

                // Build the consolidation transaction
                auto transactionBuilder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
                int expirationHeight = 0;
                
                {
                    expirationHeight = chainActive.Tip()->nHeight + SAPLING_CONSOLIDATION_EXPIRY_DELTA;
                    transactionBuilder.SetExpiryHeight(expirationHeight);

                    LogPrint("zrpcunsafe", "%s: Building consolidation transaction with %d inputs, output amount=%s\n", 
                            getId(), selectedInputs.size(), FormatMoney(outputAmount));

                    // Add all selected notes as transaction inputs
                    for (const auto& inputEntry : selectedInputs) {
                        // Get Merkle path for this note
                        libzcash::MerklePath merklePath;
                        if (!pwalletMain->SaplingWalletGetMerklePathOfNote(inputEntry.op.hash, inputEntry.op.n, merklePath)) {
                            LogPrint("zrpcunsafe", "%s: Failed to get Merkle path for note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            roundComplete = true;
                            break;
                        }

                        // Get anchor for this note
                        uint256 anchor;
                        if (!pwalletMain->SaplingWalletGetPathRootWithCMU(merklePath, inputEntry.note.cmu().value(), anchor)) {
                            LogPrint("zrpcunsafe", "%s: Failed to get anchor for note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            roundComplete = true;
                            break;
                        }

                        // Add the spend to the transaction builder
                        if (!transactionBuilder.AddSaplingSpendRaw(inputEntry.op, inputEntry.address, 
                                                                  inputEntry.note.value(), inputEntry.note.rcm(), 
                                                                  merklePath, anchor)) {
                            LogPrint("zrpcunsafe", "%s: Failed to add Sapling spend for note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            roundComplete = true;
                            break;
                        }
                    }

                    // Convert raw spends to signed spends
                    if (!transactionBuilder.ConvertRawSaplingSpend(extendedSpendingKey)) {
                        LogPrint("zrpcunsafe", "%s: Failed to convert raw Sapling spends. Stopping.\n", getId());
                        roundComplete = true;
                        break;
                    }
                }

                // Set transaction fee and add output
                transactionBuilder.SetFee(transactionFee);
                transactionBuilder.AddSaplingOutputRaw(address, outputAmount, std::nullopt);
                transactionBuilder.ConvertRawSaplingOutput(extendedSpendingKey.expsk.ovk);

                // Build and commit the transaction
                auto buildResult = transactionBuilder.Build();
                if (!buildResult.IsTx()) {
                    LogPrint("zrpcunsafe", "%s: Failed to build Sapling consolidation transaction: %s\n", 
                            getId(), buildResult.GetError());
                    roundComplete = true;
                    break;
                }
                auto consolidationTx = buildResult.GetTxOrThrow();

                // Check for cancellation before committing
                if (isCancelled()) {
                    LogPrint("zrpcunsafe", "%s: Operation cancelled. Stopping.\n", getId());
                    roundComplete = true;
                    break;
                }

                // Commit the transaction to the wallet
                if (!pwalletMain->CommitAutomatedTx(consolidationTx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit consolidation transaction. Stopping.\n", getId());
                    roundComplete = true;
                    break;
                }

                LogPrint("zrpcunsafe", "%s: Committed consolidation transaction with txid=%s, consolidated %d notes\n", 
                        getId(), consolidationTx.GetHash().ToString(), selectedInputs.size());

                // Update consolidation tracking
                amountConsolidated += outputAmount;
                numTxCreated++;
                consolidationTxIds.push_back(consolidationTx.GetHash().ToString());
            }
        }

        // Exit conditions for the consolidation loop
        if (roundComplete) {
            break;
        }
    }

    // Check if consolidation is complete (no more addresses with multiple notes)
    if (roundComplete) {
        // Schedule next consolidation at regular interval
        pwalletMain->nextSaplingConsolidation = pwalletMain->saplingConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fSaplingConsolidationRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d consolidation transactions with total output amount=%s\n", 
             getId(), numTxCreated, FormatMoney(amountConsolidated));
    setConsolidationResult(numTxCreated, amountConsolidated, consolidationTxIds);
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
 */
void AsyncRPCOperation_saplingconsolidation::setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("num_tx_created", numTxCreated));
    result.push_back(Pair("amount_consolidated", FormatMoney(amountConsolidated)));
    
    UniValue txIdArray(UniValue::VARR);
    for (const std::string& txId : consolidationTxIds) {
        txIdArray.push_back(txId);
    }
    result.push_back(Pair("consolidation_txids", txIdArray));
    
    set_result(result);
}

/**
 * @brief Cancel the consolidation operation
 * 
 * Sets the operation state to cancelled. The operation will check for
 * cancellation at safe points and stop processing gracefully.
 */
void AsyncRPCOperation_saplingconsolidation::cancel()
{
    set_state(OperationStatus::CANCELLED);
}

/**
 * @brief Get current operation status with consolidation-specific information
 * 
 * @return UniValue object containing standard operation status plus
 *         consolidation-specific fields like method name and target height
 */
UniValue AsyncRPCOperation_saplingconsolidation::getStatus() const
{
    UniValue baseStatus = AsyncRPCOperation::getStatus();
    UniValue statusObj = baseStatus.get_obj();
    statusObj.push_back(Pair("method", "saplingconsolidation"));
    statusObj.push_back(Pair("target_height", targetHeight_));
    return statusObj;
}
