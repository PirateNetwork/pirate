// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "asyncrpcoperation_orchardconsolidation.h"
#include "asyncrpcoperation_sweeptoaddress.h"
#include "init.h"
#include "key_io.h"
#include "random.h"
#include "sync.h"
#include "tinyformat.h"
#include "transaction_builder.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"

// Global configuration variables
/**
 * Global variables for Orchard consolidation configuration
 * These are set during initialization from command line parameters
 */
CAmount fOrchardConsolidationTxFee = DEFAULT_ORCHARD_CONSOLIDATION_FEE;
bool fOrchardConsolidationMapUsed = false;

/**
 * Number of blocks to set as expiration delta for consolidation transactions
 * This provides sufficient time for transaction confirmation while preventing
 * transactions from staying in mempool indefinitely
 */
const int ORCHARD_CONSOLIDATION_EXPIRY_DELTA = 40;

/**
 * @brief Constructor for Orchard consolidation operation
 * 
 * @param targetHeight Target blockchain height for consolidation operations
 */
AsyncRPCOperation_orchardconsolidation::AsyncRPCOperation_orchardconsolidation(int targetHeight) : targetHeight_(targetHeight) {}

/**
 * @brief Destructor - automatically cleans up resources
 */
AsyncRPCOperation_orchardconsolidation::~AsyncRPCOperation_orchardconsolidation() {}

/**
 * @brief Main execution wrapper for Orchard consolidation operation
 * 
 * This method is the main entry point for executing the consolidation operation.
 * It performs the following steps:
 * 1. Validates the operation state and handles cancellation
 * 2. Sets up execution timing and state management
 * 3. Calls the core consolidation logic
 * 4. Handles exceptions with detailed error reporting
 * 5. Updates final operation status and logs results
 * 
 * The consolidation process combines multiple Orchard notes into fewer notes
 * to improve wallet performance and reduce transaction complexity.
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

bool AsyncRPCOperation_orchardconsolidation::main_impl() {
    LogPrint("zrpcunsafe", "%s: Beginning AsyncRPCOperation_orchardconsolidation.\n", getId());
    
    // Get consensus parameters and check for network upgrade compatibility
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + ORCHARD_CONSOLIDATION_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Orchard consolidation txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            pwalletMain->nextOrchardConsolidation = pwalletMain->orchardConsolidationInterval + chainActive.Tip()->nHeight;
            pwalletMain->fOrchardConsolidationRunning = false;
        }
        setConsolidationResult(0, 0, std::vector<std::string>());
        return true;
    }

    int numTxCreated = 0;
    std::vector<std::string> consolidationTxIds;
    CAmount amountConsolidated = 0;

    // STEP 1: Read wallet addresses and config from keystore only - no note data loaded.
    // GetOrchardPaymentAddresses reads key metadata only, O(num_keys) not O(num_notes).
    int consolidationTarget = 0;
    std::vector<libzcash::OrchardPaymentAddress> candidateAddresses;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        consolidationTarget = pwalletMain->targetOrchardConsolidationQty;

        if (pwalletMain->fSweepEnabled) {
            // When sweep is active, consolidation is restricted to the sweep destination
            // address only - consolidating into it prepares funds for the sweep.
            if (rpcOrchardSweepAddress.has_value()) {
                candidateAddresses.push_back(rpcOrchardSweepAddress.value());
            } else if (fSweepMapUsed) {
                const vector<string>& v = mapMultiArgs["-sweepaddress"];
                for (int i = 0; i < (int)v.size(); i++) {
                    auto zAddress = DecodePaymentAddress(v[i]);
                    if (std::get_if<libzcash::OrchardPaymentAddress>(&zAddress) != nullptr)
                        candidateAddresses.push_back(std::get<libzcash::OrchardPaymentAddress>(zAddress));
                }
            }
            // If sweep is enabled but no Orchard address is configured, skip consolidation.
        } else if (fOrchardConsolidationMapUsed) {
            // Sweep not active: use the explicit consolidation address filter list.
            const vector<string>& v = mapMultiArgs["-consolidateorchardaddress"];
            for (int i = 0; i < (int)v.size(); i++) {
                auto zAddress = DecodePaymentAddress(v[i]);
                if (std::get_if<libzcash::OrchardPaymentAddress>(&zAddress) != nullptr)
                    candidateAddresses.push_back(std::get<libzcash::OrchardPaymentAddress>(zAddress));
            }
        } else {
            // No filter active: consolidate all wallet Orchard addresses.
            std::set<libzcash::OrchardPaymentAddress> allAddrs;
            pwalletMain->GetOrchardPaymentAddresses(allAddrs);
            candidateAddresses.assign(allAddrs.begin(), allAddrs.end());
        }
    }

    // STEP 2: Per-address: check spending key, probe for threshold, then consolidate.
    for (const auto& addr : candidateAddresses) {
        if (isCancelled() || ShutdownRequested())
            break;
        // Spending key check first - skip watch-only addresses immediately.
        libzcash::OrchardExtendedSpendingKeyPirate extsk;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetOrchardExtendedSpendingKey(addr, extsk))
                continue;
        }

        // Threshold probe: fetch at most consolidationTarget notes under a brief lock.
        // GetFilteredNotes exits early once maxNotes is reached, so lock hold is bounded.
        {
            std::vector<SaplingNoteEntry> saplingProbe;
            std::vector<OrchardNoteEntry> orchardProbe;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                std::set<libzcash::PaymentAddress> filterAddr;
                filterAddr.insert(addr);
                pwalletMain->GetFilteredNotes(saplingProbe, orchardProbe, filterAddr,
                                              11, INT_MAX, true, true, true,
                                              consolidationTarget, 0);
            }
            if ((int)orchardProbe.size() < consolidationTarget)
                continue;
        }

        // Random note count bounds chosen once per address.
        const int maxQuantity = rand() % 35 + 10;                               // [10, 44]
        const int minQuantity = rand() % std::min(9, maxQuantity - 1) + 2;      // [2, min(10, maxQuantity-1)]

        // Per-address inner loop: keep consolidating until fewer than minQuantity
        // unspent notes remain or they cannot cover the fee.
        while (true) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping Orchard consolidation inner loop (cancelled or shutdown).\n", getId());
                break;
            }
            std::vector<SaplingNoteEntry> saplingEntries;
            std::vector<OrchardNoteEntry> orchardEntries;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                std::set<libzcash::PaymentAddress> filterAddresses;
                filterAddresses.insert(addr);
                pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, filterAddresses,
                                              11, INT_MAX, true, true, true,
                                              maxQuantity, fOrchardConsolidationTxFee + 1);
                // Lock immediately so no other async operation can select the same notes.
                for (const auto& e : orchardEntries)
                    pwalletMain->LockNote(e.op);
            }

            auto unlockOrchardEntries = [&]() {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                for (const auto& e : orchardEntries)
                    pwalletMain->UnlockNote(e.op);
            };

            if ((int)orchardEntries.size() < minQuantity) {
                unlockOrchardEntries();
                break;
            }

            CAmount amountToSend = 0;
            for (const auto& e : orchardEntries)
                amountToSend += CAmount(e.note.value());

            if (amountToSend <= fOrchardConsolidationTxFee) {
                unlockOrchardEntries();
                break;
            }

            const CAmount outputAmount = amountToSend - fOrchardConsolidationTxFee;

            std::vector<OrchardOutPoint> ops;
            for (const auto& entry : orchardEntries)
                ops.push_back(entry.op);

            uint256 anchor;
            std::vector<libzcash::MerklePath> orchardMerklePaths;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetOrchardNoteMerklePaths(ops, orchardMerklePaths, anchor)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Orchard note. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
            }

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + ORCHARD_CONSOLIDATION_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Building Orchard consolidation transaction with %d inputs, output amount=%s\n",
                    getId(), orchardEntries.size(), FormatMoney(outputAmount));

            bool buildFailed = false;
            for (size_t i = 0; i < orchardEntries.size(); i++) {
                const auto& entry = orchardEntries[i];
                auto orchardNote = entry.note;
                if (!builder.AddOrchardSpendRaw(entry.op, entry.address,
                                                orchardNote.value(), orchardNote.rho(),
                                                orchardNote.rseed(), orchardMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Failed to add Orchard spend for note %s:%d. Stopping.\n",
                            getId(), entry.op.hash.ToString(), entry.op.n);
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) {
                unlockOrchardEntries();
                break;
            }

            builder.InitializeOrchard(true, true, anchor);

            if (!builder.ConvertRawOrchardSpend(extsk)) {
                LogPrint("zrpcunsafe", "%s: Failed to convert raw Orchard spends. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            builder.SetFee(fOrchardConsolidationTxFee);

            if (!builder.AddOrchardOutputRaw(addr, outputAmount, std::nullopt)) {
                LogPrint("zrpcunsafe", "%s: Failed to add Orchard output. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            libzcash::OrchardFullViewingKey fvk;
            if (!extsk.sk.DeriveFVK(&fvk)) {
                LogPrint("zrpcunsafe", "%s: Failed to get FVK from spending key. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }
            libzcash::OrchardOutgoingViewingKey ovk;
            if (!fvk.DeriveOVK(&ovk)) {
                LogPrint("zrpcunsafe", "%s: Failed to get OVK from FVK. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }
            builder.ConvertRawOrchardOutput(ovk.ovk);

            auto buildResult = builder.Build();
            if (!buildResult.IsTx()) {
                LogPrint("zrpcunsafe", "%s: Failed to build Orchard consolidation transaction. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }
            auto tx = buildResult.GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit Orchard consolidation transaction. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Orchard consolidation transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            unlockOrchardEntries();
            amountConsolidated += outputAmount;
            numTxCreated++;
            consolidationTxIds.push_back(tx.GetHash().ToString());
        }
    }

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->nextOrchardConsolidation = pwalletMain->orchardConsolidationInterval + chainActive.Tip()->nHeight;
        pwalletMain->fOrchardConsolidationRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d Orchard consolidation transactions with total output amount=%s\n", getId(), numTxCreated, FormatMoney(amountConsolidated));
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
 * Sets the operation state to cancelled. The operation will check for
 * cancellation at safe points and stop processing gracefully.
 */
void AsyncRPCOperation_orchardconsolidation::cancel()
{
    set_state(OperationStatus::CANCELLED);
}

UniValue AsyncRPCOperation_orchardconsolidation::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "orchardconsolidation"));
    obj.push_back(Pair("target_height", targetHeight_));
    return obj;
}
