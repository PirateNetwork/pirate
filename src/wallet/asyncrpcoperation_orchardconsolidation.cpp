// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_orchardconsolidation.cpp
 * @brief Implementation of asynchronous Orchard note consolidation operation
 * 
 * Implements automatic consolidation of Orchard shielded notes to reduce
 * wallet fragmentation and improve transaction performance.
 */

#include "asyncrpcoperation_orchardconsolidation.h"
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

/**
 * Global Orchard consolidation fee setting
 * Can be modified via configuration parameters
 */
CAmount fOrchardConsolidationTxFee = DEFAULT_ORCHARD_CONSOLIDATION_FEE;

/**
 * Flag indicating whether Orchard consolidation address mapping is used
 * When true, only specific addresses from configuration are consolidated
 */
bool fOrchardConsolidationMapUsed = false;

/**
 * Number of blocks before Orchard consolidation transactions expire
 * Provides sufficient time for confirmation while preventing mempool buildup
 */
const int ORCHARD_CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Orchard consolidation operation
 * 
 * @param targetHeight Blockchain height for transaction targeting and expiration
 */
AsyncRPCOperation_orchardconsolidation::AsyncRPCOperation_orchardconsolidation(int targetHeight) : targetHeight_(targetHeight) {}

/**
 * @brief Destructor with automatic resource cleanup
 */
AsyncRPCOperation_orchardconsolidation::~AsyncRPCOperation_orchardconsolidation() {}

/**
 * @brief Main execution entry point for Orchard consolidation operation
 * 
 * Handles complete consolidation lifecycle including state management,
 * error handling, timing control, and result reporting.
 */
void AsyncRPCOperation_orchardconsolidation::main()
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
        set_error_message("Unknown error occurred during Orchard consolidation");
    }

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string logMessage = strprintf("%s: Orchard Consolidation routine complete. (status=%s", getId(), getStateAsString());
    if (success) {
        logMessage += strprintf(", success)\n");
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", logMessage);
}

/**
 * @brief Core implementation of Orchard note consolidation logic
 * 
 * Performs multi-round consolidation of Orchard notes using intelligent
 * selection strategies and action-based transactions.
 * 
 * @return true if consolidation completed successfully, false otherwise
 * @throws JSONRPCError For wallet access or transaction building errors
 */
bool AsyncRPCOperation_orchardconsolidation::main_impl()
{
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_orchardconsolidation.\n", getId());
    
    // Get consensus parameters and check for network upgrade compatibility
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + ORCHARD_CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Orchard consolidation txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        
        //Set Next Orchard consolidation time
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->nextOrchardConsolidation = pwalletMain->orchardConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fOrchardConsolidationRunning = false;
        
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
        std::set<libzcash::OrchardPaymentAddressPirate> targetOrchardAddresses;
        std::map<libzcash::OrchardPaymentAddressPirate, std::vector<OrchardNoteEntry>> addressToNotesMap;
        std::map<std::pair<int, int>, OrchardNoteEntry> sortedEntriesMap;
        
        {
            consolidationTarget = pwalletMain->targetOrchardConsolidationQty;
            
            // Get filtered Orchard notes with minimum depth of 11 for stability
            // This avoids unconfirmed notes and provides buffer for anchor selection
            pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, "", 11);
            
            // Build target address set if Orchard consolidation mapping is used
            if (fOrchardConsolidationMapUsed) {
                const std::vector<std::string>& configuredAddresses = mapMultiArgs["-consolidateorchardaddress"];
                for (size_t i = 0; i < configuredAddresses.size(); i++) {
                    libzcash::PaymentAddress paymentAddr = DecodePaymentAddress(configuredAddresses[i]);
                    if (std::get_if<libzcash::OrchardPaymentAddressPirate>(&paymentAddr) != nullptr) {
                        libzcash::OrchardPaymentAddressPirate orchardAddr = std::get<libzcash::OrchardPaymentAddressPirate>(paymentAddr);
                        targetOrchardAddresses.insert(orchardAddr);
                    }
                }
            }

            // Sort Orchard entries by confirmation count and output index for consistent processing
            for (const auto& entry : orchardEntries) {
                auto sortKey = std::make_pair(entry.confirmations, entry.op.n);
                sortedEntriesMap.insert({sortKey, entry});
            }

            // Group Orchard notes by address - process in reverse order (most confirmed first)
            for (auto rit = sortedEntriesMap.rbegin(); rit != sortedEntriesMap.rend(); ++rit) {
                const OrchardNoteEntry& entry = rit->second;
                
                // Only process notes from target addresses (or all if no mapping)
                if (targetOrchardAddresses.count(entry.address) > 0 || !fOrchardConsolidationMapUsed) {
                    auto it = addressToNotesMap.find(entry.address);
                    if (it != addressToNotesMap.end()) {
                        it->second.push_back(entry);
                    } else {
                        std::vector<OrchardNoteEntry> noteList;
                        noteList.push_back(entry);
                        addressToNotesMap[entry.address] = noteList;
                    }
                }
            }
        }

        // Initialize coins view for transaction validation
        CCoinsViewCache coinsView(pcoinsTip);

        // Check if we have enough Orchard notes to justify consolidation
        if (orchardEntries.size() < static_cast<size_t>(consolidationTarget)) {
            // Not enough notes to consolidate - exit this round
            LogPrint("zrpcunsafe", "%s: Not enough Orchard notes to consolidate (%zu < %d). Exiting round.\n", 
                     getId(), orchardEntries.size(), consolidationTarget);
            roundComplete = true;
            break;
        } else {
            // Process each Orchard address that has notes
            for (auto it = addressToNotesMap.begin(); it != addressToNotesMap.end(); ++it) {
                const auto& address = it->first;
                const auto& addressNotes = it->second;

                // Get the extended spending key for this Orchard address
                libzcash::OrchardExtendedSpendingKeyPirate orchardSpendingKey;
                if (!getOrchardExtendedSpendingKey(address, orchardSpendingKey)) {
                    // Skip addresses we don't have spending keys for
                    LogPrint("zrpcunsafe", "%s: No extended spending key found for Orchard address. Skipping.\n", getId());
                    continue;
                }

                std::vector<OrchardNoteEntry> selectedInputs;
                CAmount totalInputAmount = 0;

                // Determine consolidation parameters for Orchard
                int minNoteQuantity = rand() % 10 + 2;      // 2-11 notes minimum
                int maxNoteQuantity = rand() % 35 + 10;     // 10-44 notes maximum

                // Select Orchard notes from this address for consolidation
                for (const OrchardNoteEntry& noteEntry : addressNotes) {
                    // Verify this note belongs to the address we're processing
                    if (noteEntry.address == address) {
                        selectedInputs.push_back(noteEntry);
                        totalInputAmount += noteEntry.note.value();
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

                // Lock the selected Orchard notes to prevent double-spending
                lockOrchardNotes(selectedInputs);

                // Calculate fee and output amount for Orchard transaction
                CAmount transactionFee = fOrchardConsolidationTxFee;
                if (totalInputAmount <= fOrchardConsolidationTxFee) {
                    transactionFee = 0; // Waive fee if input amount is too small
                }
                CAmount outputAmount = totalInputAmount - transactionFee;

                // Skip if output amount would be zero or negative
                if (outputAmount <= 0) {
                    LogPrint("zrpcunsafe", "%s: Output amount would be zero or negative (%s). Skipping this address.\n", 
                            getId(), FormatMoney(outputAmount));
                    unlockOrchardNotes(selectedInputs);
                    continue;
                }

                // Build the Orchard consolidation transaction
                auto transactionBuilder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
                int expirationHeight = 0;
                
                {
                    expirationHeight = chainActive.Tip()->nHeight + ORCHARD_CONSOLIDATION_EXPIRY_DELTA;
                    transactionBuilder.SetExpiryHeight(expirationHeight);

                    LogPrint("zrpcunsafe", "%s: Building Orchard consolidation transaction with %d inputs, output amount=%s\n", 
                            getId(), selectedInputs.size(), FormatMoney(outputAmount));

                    // Add all selected Orchard notes as transaction inputs (actions)
                    uint256 orchardAnchor;
                    for (const auto& inputEntry : selectedInputs) {
                        // Get Merkle path for this Orchard note
                        libzcash::MerklePath merklePath;
                        if (!pwalletMain->OrchardWalletGetMerklePathOfNote(inputEntry.op.hash, inputEntry.op.n, merklePath)) {
                            LogPrint("zrpcunsafe", "%s: Failed to get Merkle path for Orchard note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            unlockOrchardNotes(selectedInputs);
                            roundComplete = true;
                            break;
                        }

                        // Get CMX and anchor for this Orchard note (create mutable copy for method calls)
                        auto orchardNote = inputEntry.note;
                        auto cmx = orchardNote.cmx();
                        uint256 anchor;
                        if (!pwalletMain->OrchardWalletGetPathRootWithCMU(merklePath, cmx, anchor)) {
                            LogPrint("zrpcunsafe", "%s: Failed to get anchor for Orchard note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            unlockOrchardNotes(selectedInputs);
                            roundComplete = true;
                            break;
                        }

                        // Store the anchor for later use
                        if (orchardAnchor.IsNull()) {
                            orchardAnchor = anchor; // Use the first anchor found
                        } else if (orchardAnchor != anchor) {
                            LogPrint("zrpcunsafe", "%s: Multiple anchors found for Orchard notes. Stopping.\n", getId());
                            unlockOrchardNotes(selectedInputs);
                            roundComplete = true;
                            break;
                        }

                        // Add the Orchard spend action to the transaction builder
                        if (!transactionBuilder.AddOrchardSpendRaw(inputEntry.op, inputEntry.address, 
                                                                  orchardNote.value(), orchardNote.rho(), 
                                                                  orchardNote.rseed(), merklePath, anchor)) {
                            LogPrint("zrpcunsafe", "%s: Failed to add Orchard spend for note %s:%d. Stopping.\n", 
                                    getId(), inputEntry.op.hash.ToString(), inputEntry.op.n);
                            unlockOrchardNotes(selectedInputs);
                            roundComplete = true;
                            break;
                        }
                    }

                    // Initialize Orchard builder with the anchor, consolidation transactions will always have inputs and outputs
                    transactionBuilder.InitializeOrchard(true, true, orchardAnchor);

                    // Convert raw Orchard spends to signed spends using extended spending key
                    if (!transactionBuilder.ConvertRawOrchardSpend(orchardSpendingKey)) {
                        LogPrint("zrpcunsafe", "%s: Failed to convert raw Orchard spends. Stopping.\n", getId());
                        unlockOrchardNotes(selectedInputs);
                        roundComplete = true;
                        break;
                    }
                }

                // Set transaction fee and add Orchard output action
                transactionBuilder.SetFee(transactionFee);
                
                transactionBuilder.AddOrchardOutputRaw(address, outputAmount, std::nullopt);
                
                // Convert raw Orchard output using outgoing viewing key from extended spending key
                auto orchardFvk = orchardSpendingKey.sk.GetFVK();
                if (!orchardFvk) {
                    LogPrint("zrpcunsafe", "%s: Failed to get FVK from spending key. Stopping.\n", getId());
                    unlockOrchardNotes(selectedInputs);
                    roundComplete = true;
                    break;
                }
                auto orchardOvk = orchardFvk->GetOVK();
                if (!orchardOvk) {
                    LogPrint("zrpcunsafe", "%s: Failed to get OVK from FVK. Stopping.\n", getId());
                    unlockOrchardNotes(selectedInputs);
                    roundComplete = true;
                    break;
                }
                transactionBuilder.ConvertRawOrchardOutput(orchardOvk->ovk);

                // Build and commit the Orchard consolidation transaction
                auto buildResult = transactionBuilder.Build();
                if (!buildResult.IsTx()) {
                    LogPrint("zrpcunsafe", "%s: Failed to build Orchard consolidation transaction: %s\n", 
                            getId(), buildResult.GetError());
                    unlockOrchardNotes(selectedInputs);
                    roundComplete = true;
                    break;
                }
                auto consolidationTx = buildResult.GetTxOrThrow();

                // Check for cancellation before committing
                if (isCancelled()) {
                    LogPrint("zrpcunsafe", "%s: Operation cancelled. Stopping.\n", getId());
                    unlockOrchardNotes(selectedInputs);
                    roundComplete = true;
                    break;
                }

                // Commit the transaction to the wallet
                if (!pwalletMain->CommitAutomatedTx(consolidationTx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit Orchard consolidation transaction. Stopping.\n", getId());
                    unlockOrchardNotes(selectedInputs);
                    roundComplete = true;
                    break;
                }

                LogPrint("zrpcunsafe", "%s: Committed Orchard consolidation transaction with txid=%s, consolidated %d notes\n", 
                        getId(), consolidationTx.GetHash().ToString(), selectedInputs.size());

                // Unlock the notes after successful transaction commit
                unlockOrchardNotes(selectedInputs);

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

    // Check if Orchard consolidation is complete (no more addresses with multiple notes)
    if (roundComplete) {
        // All addresses now have single notes - consolidation complete
        // No more consolidation is possible or beneficial
        pwalletMain->nextOrchardConsolidation = pwalletMain->orchardConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fOrchardConsolidationRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d Orchard consolidation transactions with total output amount=%s\n", 
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
 * @param numTxCreated Number of consolidation transactions created
 * @param amountConsolidated Total amount consolidated across all transactions
 * @param consolidationTxIds Vector of transaction IDs for created transactions
 */
void AsyncRPCOperation_orchardconsolidation::setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds)
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
 * Sets the operation state to cancelled, stopping further processing gracefully.
 * Already-broadcast transactions will complete normally.
 */
void AsyncRPCOperation_orchardconsolidation::cancel()
{
    set_state(OperationStatus::CANCELLED);
}

/**
 * @brief Get current operation status with consolidation-specific information
 * 
 * @return UniValue object containing operation status, method name, and target height
 */
UniValue AsyncRPCOperation_orchardconsolidation::getStatus() const
{
    UniValue baseStatus = AsyncRPCOperation::getStatus();
    UniValue statusObj = baseStatus.get_obj();
    statusObj.push_back(Pair("method", "orchardconsolidation"));
    statusObj.push_back(Pair("target_height", targetHeight_));
    return statusObj;
}

/**
 * @brief Get Orchard extended spending key for address
 * 
 * Retrieves the extended spending key associated with an Orchard payment address.
 * Required for note detection and spending operations.
 * 
 * @param address The Orchard payment address to look up
 * @param spendingKey Output parameter for the retrieved spending key
 * @return true if key found, false if address not owned by wallet
 */
bool AsyncRPCOperation_orchardconsolidation::getOrchardExtendedSpendingKey(const libzcash::OrchardPaymentAddressPirate& address, 
                                                                          libzcash::OrchardExtendedSpendingKeyPirate& spendingKey)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    
    // Search through Orchard extended spending keys for this address
    if (pwalletMain->GetOrchardExtendedSpendingKey(address, spendingKey)) {
        return true;
    }
    
    LogPrint("zrpcunsafe", "%s: No extended spending key found for Orchard address\n", getId());
    return false;
}

/**
 * @brief Lock Orchard notes to prevent double-spending during consolidation
 * 
 * Locks selected Orchard notes to ensure exclusive access during consolidation
 * operations, preventing concurrent operations from using the same notes.
 * 
 * @param selectedInputs Vector of Orchard note entries to lock
 */
void AsyncRPCOperation_orchardconsolidation::lockOrchardNotes(const std::vector<OrchardNoteEntry>& selectedInputs)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto& inputEntry : selectedInputs) {
        pwalletMain->LockNote(inputEntry.op);
    }
}

/**
 * @brief Unlock Orchard notes to restore availability for other operations
 * 
 * Releases exclusive locks on previously locked Orchard notes, making them
 * available for other wallet operations. Should be called regardless of
 * consolidation outcome to prevent resource leaks.
 * 
 * @param selectedInputs Vector of Orchard note entries to unlock
 */
void AsyncRPCOperation_orchardconsolidation::unlockOrchardNotes(const std::vector<OrchardNoteEntry>& selectedInputs)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto& inputEntry : selectedInputs) {
        pwalletMain->UnlockNote(inputEntry.op);
    }
}
