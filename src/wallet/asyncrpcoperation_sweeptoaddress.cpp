// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_sweeptoaddress.cpp
 * @brief Implementation of asynchronous sweep-to-address operation
 * 
 * Implements consolidation of funds from multiple addresses into a single
 * destination address. Supports both Sapling and Orchard protocols.
 */

#include "asyncrpcoperation_sweeptoaddress.h"
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
 * Global variables for sweep configuration
 * These are set during initialization from command line parameters
 */
CAmount fSweepTxFee = DEFAULT_SWEEP_FEE;
bool fSweepMapUsed = false;

/**
 * Number of blocks to set as expiration delta for sweep transactions
 * This provides sufficient time for transaction confirmation while preventing
 * transactions from staying in mempool indefinitely
 */
const int SWEEP_EXPIRY_DELTA = 40;

/**
 * @brief Sapling destination address for RPC-initiated sweep operations
 * 
 * Target address for Sapling sweeps or cross-protocol sweeps from Orchard.
 * Mutually exclusive with rpcOrchardSweepAddress.
 */
std::optional<libzcash::SaplingPaymentAddress> rpcSaplingSweepAddress;

/**
 * @brief Orchard destination address for RPC-initiated sweep operations
 * 
 * Target address for Orchard sweeps or cross-protocol sweeps from Sapling.
 * Mutually exclusive with rpcSaplingSweepAddress.
 */
std::optional<libzcash::OrchardPaymentAddress> rpcOrchardSweepAddress;

/**
 * @brief Constructor for sweep-to-address operation
 * 
 * @param targetHeight Target blockchain height for sweep operations
 * @param fromRpc True for RPC calls, false for command-line operations
 */
AsyncRPCOperation_sweeptoaddress::AsyncRPCOperation_sweeptoaddress(int targetHeight, bool fromRpc) : targetHeight_(targetHeight), fromRPC_(fromRpc) {}

/**
 * @brief Destructor - automatically cleans up resources
 */
AsyncRPCOperation_sweeptoaddress::~AsyncRPCOperation_sweeptoaddress() {}

/**
 * @brief Main execution wrapper for sweep-to-address operation
 * 
 * This method is the main entry point for executing the sweep operation.
 * It performs the following steps:
 * 1. Validates the operation state and handles cancellation
 * 2. Sets up execution timing and state management
 * 3. Calls the core sweep logic
 * 4. Handles exceptions with detailed error reporting
 * 5. Updates final operation status and logs results
 * 
 * The sweep process moves funds from multiple addresses into a single
 * destination address across Sapling and Orchard protocols.
 */
void AsyncRPCOperation_sweeptoaddress::main()
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
        set_error_message("Unknown error occurred during sweep");
    }

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string logMessage = strprintf("%s: Sweep transaction to single destination created. (status=%s", getId(), getStateAsString());
    if (success) {
        logMessage += strprintf(", success)\n");
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", logMessage);
}

bool AsyncRPCOperation_sweeptoaddress::main_impl()
{
    LogPrint("zrpcunsafe", "%s: Beginning asyncrpcoperation_sweeptoaddress.\n", getId());
    auto consensusParams = Params().GetConsensus();
    auto nextActivationHeight = NextActivationHeight(targetHeight_, consensusParams);
    if (nextActivationHeight && targetHeight_ + SWEEP_EXPIRY_DELTA >= nextActivationHeight.value()) {
        LogPrint("zrpcunsafe", "%s: Sweep txs would be created before a NU activation but may expire after. Skipping this round.\n", getId());
        setSweepResult(0, 0, std::vector<std::string>());
        return true;
    }

    // Resolve sweep destination address — RPC-set address takes priority over config.
    libzcash::SaplingPaymentAddress saplingSweepAddress;
    libzcash::OrchardPaymentAddress orchardSweepAddress;
    bool hasSaplingTarget = false;
    bool hasOrchardTarget = false;
    {
        if (rpcSaplingSweepAddress.has_value()) {
            saplingSweepAddress = rpcSaplingSweepAddress.value();
            hasSaplingTarget = true;
        } else if (rpcOrchardSweepAddress.has_value()) {
            orchardSweepAddress = rpcOrchardSweepAddress.value();
            hasOrchardTarget = true;
        } else if (!fromRPC_ && fSweepMapUsed) {
            const vector<string>& v = mapMultiArgs["-sweepaddress"];
            if (v.size() != 1) {
                LogPrint("zrpcunsafe", "%s: Exactly one sweep address must be specified.\n", getId());
                return false;
            }
            libzcash::PaymentAddress zAddress = DecodePaymentAddress(v[0]);
            if (std::get_if<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                saplingSweepAddress = *std::get_if<libzcash::SaplingPaymentAddress>(&zAddress);
                hasSaplingTarget = true;
            } else if (std::get_if<libzcash::OrchardPaymentAddress>(&zAddress) != nullptr) {
                orchardSweepAddress = *std::get_if<libzcash::OrchardPaymentAddress>(&zAddress);
                hasOrchardTarget = true;
            } else {
                LogPrint("zrpcunsafe", "%s: Invalid sweep address format.\n", getId());
                return false;
            }
        } else {
            LogPrint("zrpcunsafe", "%s: No destination address specified for sweep operation.\n", getId());
            return false;
        }
    }

    // Read all wallet addresses from the keystore - no note data loaded.
    // GetXPaymentAddresses is O(num_keys), not O(num_notes).
    std::set<libzcash::SaplingPaymentAddress> saplingCandidates;
    std::set<libzcash::OrchardPaymentAddress> orchardCandidates;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        // Fill then erase target: O(n) fill + O(log n) erase, no per-element branch.
        pwalletMain->GetSaplingPaymentAddresses(saplingCandidates);
        if (hasSaplingTarget)
            saplingCandidates.erase(saplingSweepAddress);

        // Collect Orchard addresses for Orchard processing pass below,
        // excluding the sweep target so we never sweep from target to itself.
        pwalletMain->GetOrchardPaymentAddresses(orchardCandidates);
        if (hasOrchardTarget)
            orchardCandidates.erase(orchardSweepAddress);
    }

    int numTxCreated = 0;
    std::vector<std::string> sweepTxIds;
    CAmount amountSwept = 0;
    const int maxQuantity = 50;

    // === Sapling sweep pass ===
    for (const auto& addr : saplingCandidates) {
        if (isCancelled() || ShutdownRequested())
            break;
        // Skip watch-only addresses - spending key required to build spends.
        libzcash::SaplingExtendedSpendingKey extsk;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk))
                continue;
        }

        // Per-address inner loop: keep sweeping until no more eligible notes remain.
        // Committed notes are excluded by ignoreSpent=true on the next call.
        while (true) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping sweep inner loop (cancelled or shutdown).", getId());
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
                                              maxQuantity, fSweepTxFee + 1);
                // Lock immediately so no other async operation can select the same notes.
                for (const auto& e : saplingEntries)
                    pwalletMain->LockNote(e.op);
            }

            auto unlockSaplingEntries = [&]() {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                for (const auto& e : saplingEntries)
                    pwalletMain->UnlockNote(e.op);
            };

            if (saplingEntries.empty()) {
                unlockSaplingEntries();
                break;
            }

            CAmount amountToSend = 0;
            for (const auto& e : saplingEntries)
                amountToSend += CAmount(e.note.value());

            if (amountToSend <= fSweepTxFee) {
                unlockSaplingEntries();
                break;
            }

            const CAmount fee = fSweepTxFee;
            const CAmount outputAmount = amountToSend - fee;

            std::vector<SaplingOutPoint> ops;
            std::vector<libzcash::SaplingNote> notes;
            for (const auto& entry : saplingEntries) {
                ops.push_back(entry.op);
                notes.push_back(entry.note);
            }

            uint256 anchor;
            std::vector<libzcash::MerklePath> saplingMerklePaths;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling note. Stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
            }

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Creating Sapling sweep transaction with output amount=%s\n", getId(), FormatMoney(amountToSend - fee));

            bool buildFailed = false;
            for (size_t i = 0; i < notes.size(); i++) {
                if (!builder.AddSaplingSpendRaw(ops[i], addr, notes[i].value(), notes[i].rcm(), saplingMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Spend failed. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) {
                unlockSaplingEntries();
                break;
            }

            if (!builder.ConvertRawSaplingSpend(extsk)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Spends failed. Stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }

            builder.SetFee(fee);

            if (hasSaplingTarget) {
                if (!builder.AddSaplingOutputRaw(saplingSweepAddress, outputAmount)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
                if (!builder.ConvertRawSaplingOutput(extsk.expsk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
            } else if (hasOrchardTarget) {
                if (!builder.AddOrchardOutputRaw(orchardSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Orchard Output failed. Stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
                builder.InitializeOrchard(false, true, uint256());
                if (!builder.ConvertRawOrchardOutput(extsk.expsk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Orchard Output failed. Stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Sapling sweep. Stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }

            auto buildResult = builder.Build();
            if (!buildResult.IsTx()) {
                LogPrint("zrpcunsafe", "%s: Failed to build sweep transaction. Stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }
            auto tx = buildResult.GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                unlockSaplingEntries();
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit sweep transaction, stopping.\n", getId());
                    unlockSaplingEntries();
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Sapling sweep transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            unlockSaplingEntries();
            numTxCreated++;
            amountSwept += outputAmount;
            sweepTxIds.push_back(tx.GetHash().ToString());
        }
    }

    // === Orchard sweep pass ===
    for (const auto& orchardAddr : orchardCandidates) {
        if (isCancelled() || ShutdownRequested())
            break;

        libzcash::OrchardExtendedSpendingKeyPirate orchardExtendedSpendingKey;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetOrchardExtendedSpendingKey(orchardAddr, orchardExtendedSpendingKey))
                continue;
        }

        // Per-address inner loop for Orchard
        while (true) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping Orchard sweep inner loop (cancelled or shutdown).\n", getId());
                break;
            }

            std::vector<SaplingNoteEntry> saplingEntries;
            std::vector<OrchardNoteEntry> orchardEntries;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                std::set<libzcash::PaymentAddress> filterAddresses;
                filterAddresses.insert(orchardAddr);
                pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, filterAddresses,
                                              11, INT_MAX, true, true, true,
                                              maxQuantity, fSweepTxFee + 1);
                // Lock immediately so no other async operation can select the same notes.
                for (const auto& e : orchardEntries)
                    pwalletMain->LockNote(e.op);
            }

            auto unlockOrchardEntries = [&]() {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                for (const auto& e : orchardEntries)
                    pwalletMain->UnlockNote(e.op);
            };

            if (orchardEntries.empty()) {
                unlockOrchardEntries();
                break;
            }

            CAmount amountToSend = 0;
            for (const auto& e : orchardEntries)
                amountToSend += CAmount(e.note.value());

            if (amountToSend <= fSweepTxFee) {
                unlockOrchardEntries();
                break;
            }

            const CAmount fee = fSweepTxFee;
            const CAmount outputAmount = amountToSend - fee;

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Creating Orchard sweep transaction with %d notes, output amount=%s\n",
                     getId(), (int)orchardEntries.size(), FormatMoney(outputAmount));

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

            bool buildFailed = false;
            for (size_t i = 0; i < orchardEntries.size(); i++) {
                const auto& entry = orchardEntries[i];
                auto orchardNote = entry.note;
                if (!builder.AddOrchardSpendRaw(entry.op, entry.address,
                                                orchardNote.value(), orchardNote.rho(),
                                                orchardNote.rseed(), orchardMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Orchard Spend failed. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) {
                unlockOrchardEntries();
                break;
            }

            builder.InitializeOrchard(true, true, anchor);

            if (!builder.ConvertRawOrchardSpend(orchardExtendedSpendingKey)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Orchard Spends failed. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            builder.SetFee(fee);

            libzcash::OrchardFullViewingKey fvk;
            if (!orchardExtendedSpendingKey.sk.DeriveFVK(&fvk)) {
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

            if (hasOrchardTarget) {
                if (!builder.AddOrchardOutputRaw(orchardSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Orchard Output failed. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
                if (!builder.ConvertRawOrchardOutput(ovk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Orchard Output failed. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
            } else if (hasSaplingTarget) {
                if (!builder.AddSaplingOutputRaw(saplingSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
                if (!builder.ConvertRawSaplingOutput(ovk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Orchard sweep. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            auto buildResult = builder.Build();
            if (!buildResult.IsTx()) {
                LogPrint("zrpcunsafe", "%s: Failed to build Orchard sweep transaction. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }
            auto tx = buildResult.GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                unlockOrchardEntries();
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit Orchard sweep transaction, stopping.\n", getId());
                    unlockOrchardEntries();
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Orchard sweep transaction with txid=%s\n",
                     getId(), tx.GetHash().ToString());
            unlockOrchardEntries();
            numTxCreated++;
            amountSwept += outputAmount;
            sweepTxIds.push_back(tx.GetHash().ToString());
        } // while (true) — Orchard inner loop
    } // for orchardAddr

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s to single destination\n", getId(), numTxCreated, FormatMoney(amountSwept));
    setSweepResult(numTxCreated, amountSwept, sweepTxIds);
    return true;
}

/**
 * @brief Set sweep operation results
 * 
 * Formats and stores the results of the sweep operation for reporting
 * via the getStatus() method and operation completion callbacks.
 * 
 * @param numTxCreated Number of sweep transactions successfully created
 * @param amountSwept Total amount swept across all transactions (in zatoshis)
 * @param sweepTxIds Vector of transaction IDs for all created sweep transactions
 */
void AsyncRPCOperation_sweeptoaddress::setSweepResult(int numTxCreated, const CAmount& amountSwept, const std::vector<std::string>& sweepTxIds)
{
    UniValue res(UniValue::VOBJ);
    res.push_back(Pair("num_tx_created", numTxCreated));
    res.push_back(Pair("amount_swept", FormatMoney(amountSwept)));
    UniValue txIds(UniValue::VARR);
    for (const std::string& txId : sweepTxIds) {
        txIds.push_back(txId);
    }
    res.push_back(Pair("sweep_txids", txIds));
    set_result(res);
}

/**
 * @brief Cancel the sweep operation
 * 
 * Sets the operation state to cancelled. The operation will check for
 * cancellation at safe points and stop processing gracefully.
 */
void AsyncRPCOperation_sweeptoaddress::cancel()
{
    set_state(OperationStatus::CANCELLED);
}

UniValue AsyncRPCOperation_sweeptoaddress::getStatus() const {
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "sweeptoaddress"));
    obj.push_back(Pair("target_height", targetHeight_));
    return obj;
}

/**
 * @brief Configure Sapling destination address for RPC-initiated sweeps
 * 
 * @param address Valid Sapling payment address for sweep destination
 */
void AsyncRPCOperation_sweeptoaddress::setSaplingSweepAddress(const libzcash::SaplingPaymentAddress& address) {
    rpcSaplingSweepAddress = address;
    rpcOrchardSweepAddress.reset(); // Clear Orchard address to ensure only one destination
}

/**
 * @brief Configure Orchard destination address for RPC-initiated sweeps
 * 
 * @param address Valid Orchard payment address for sweep destination
 */
void AsyncRPCOperation_sweeptoaddress::setOrchardSweepAddress(const libzcash::OrchardPaymentAddress& address) {
    rpcOrchardSweepAddress = address;
    rpcSaplingSweepAddress.reset(); // Clear Sapling address to ensure only one destination
}
