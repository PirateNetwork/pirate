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

//==============================================================================
// GLOBAL CONFIGURATION CONSTANTS
//==============================================================================

/**
 * Global sweep transaction fee amount
 * Can be modified at runtime to adjust fee levels based on network conditions
 */
CAmount fSweepTxFee = DEFAULT_SWEEP_FEE;

/**
 * Flag indicating whether sweep address mapping is configured
 * When true, operations use configured address mapping from command-line arguments
 */
bool fSweepMapUsed = false;

/**
 * Number of blocks before sweep transactions expire
 * Prevents transactions from remaining in mempool indefinitely
 */
const int SWEEP_EXPIRY_DELTA = 40;

/**
 * Maximum number of notes to include in a single sweep transaction
 * Prevents transactions from becoming too large
 */
const int MAX_NOTES_PER_TRANSACTION = 50;

/**
 * Minimum number of notes required to perform consolidation
 * Single notes are not consolidated as there's no efficiency benefit
 */
const int MIN_NOTES_FOR_CONSOLIDATION = 1;

/**
 * @brief Minimum confirmation depth for notes to be swept
 * 
 * Notes must have at least this many confirmations before inclusion
 * in sweep operations to prevent double-spending.
 */
const int MIN_CONFIRMATION_DEPTH = 11;

//==============================================================================
// RPC DESTINATION ADDRESS MANAGEMENT
//==============================================================================

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

//==============================================================================
// CLASS IMPLEMENTATION
//==============================================================================

//==============================================================================
// CONSTRUCTOR AND LIFECYCLE MANAGEMENT
//==============================================================================

/**
 * @brief Constructs a sweep-to-address operation
 * 
 * Creates operation for automatic execution at specified target height.
 * Supports both RPC-initiated and command-line initiated sweeps.
 * 
 * @param targetHeight Blockchain height for sweep execution
 * @param fromRpc True for RPC calls, false for command-line operations
 */
AsyncRPCOperation_sweeptoaddress::AsyncRPCOperation_sweeptoaddress(int targetHeight, bool fromRpc) : targetHeight_(targetHeight), fromRPC_(fromRpc) {}

/**
 * @brief Destructor with automatic resource cleanup
 */
AsyncRPCOperation_sweeptoaddress::~AsyncRPCOperation_sweeptoaddress() {}

/**
 * @brief Main entry point for sweep operation with error handling
 * 
 * Wrapper function providing exception handling and state management
 * for the asynchronous sweep operation. Delegates to main_impl().
 */
void AsyncRPCOperation_sweeptoaddress::main()
{
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

    std::string s = strprintf("%s: Sweep transaction to single destination created. (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", success)\n");
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }

    LogPrintf("%s", s);
}

/**
 * @brief Core implementation of sweep operation
 * 
 * Consolidates shielded notes from multiple protocols (Sapling/Orchard) 
 * into single destination address. Handles cross-protocol transfers,
 * note discovery, transaction building, and network compatibility.
 * 
 * @return true if operation completed successfully
 * @throws JSONRPCError For transaction building failures
 * @throws runtime_error For system/network failures
 */
bool AsyncRPCOperation_sweeptoaddress::main_impl()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
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

    // Read all wallet Sapling addresses from the keystore - no note data loaded.
    // GetSaplingPaymentAddresses is O(num_keys), not O(num_notes).
    std::vector<libzcash::SaplingPaymentAddress> saplingCandidates;
    std::set<libzcash::OrchardPaymentAddress> orchardCandidateSet;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::set<libzcash::SaplingPaymentAddress> allSaplingAddrs;
        pwalletMain->GetSaplingPaymentAddresses(allSaplingAddrs);
        for (const auto& addr : allSaplingAddrs) {
            if (!(hasSaplingTarget && addr == saplingSweepAddress))
                saplingCandidates.push_back(addr);
        }
        // Collect Orchard addresses for Orchard processing pass below.
        pwalletMain->GetOrchardPaymentAddresses(orchardCandidateSet);
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
            }

            if (saplingEntries.empty())
                break;

            CAmount amountToSend = 0;
            for (const auto& e : saplingEntries)
                amountToSend += CAmount(e.note.value());

            if (amountToSend <= fSweepTxFee)
                break;

            CAmount fee = fSweepTxFee;

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
                    break;
                }
            }

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Creating Sapling sweep transaction with output amount=%s\n", getId(), FormatMoney(amountToSend - fee));

            for (size_t i = 0; i < notes.size(); i++) {
                if (!builder.AddSaplingSpendRaw(ops[i], addr, notes[i].value(), notes[i].rcm(), saplingMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Spend failed. Stopping.\n", getId());
                    break;
                }
            }

            if (!builder.ConvertRawSaplingSpend(extsk)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Spends failed. Stopping.\n", getId());
                break;
            }

            builder.SetFee(fee);

            if (hasSaplingTarget) {
                if (!builder.AddSaplingOutputRaw(saplingSweepAddress, amountToSend - fee)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
                if (!builder.ConvertRawSaplingOutput(extsk.expsk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
            } else if (hasOrchardTarget) {
                builder.AddOrchardOutputRaw(orchardSweepAddress, amountToSend - fee, std::nullopt);
                builder.InitializeOrchard(false, true, uint256());
                builder.ConvertRawOrchardOutput(extsk.expsk.ovk);
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Sapling sweep. Stopping.\n", getId());
                break;
            }

            auto tx = builder.Build().GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit sweep transaction, stopping.\n", getId());
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Sapling sweep transaction with txid=%s\n", getId(), tx.GetHash().ToString());
            numTxCreated++;
            amountSwept += amountToSend - fee;
            sweepTxIds.push_back(tx.GetHash().ToString());
        }
    }

    // === Orchard sweep pass ===
    for (const auto& orchardAddr : orchardCandidateSet) {
        if (isCancelled() || ShutdownRequested())
            break;
        if (hasOrchardTarget && orchardAddr == orchardSweepAddress)
            continue; // Skip notes already at the target address

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
            }

            if (orchardEntries.empty())
                break;

            CAmount orchardAmountToSend = 0;
            for (const auto& e : orchardEntries)
                orchardAmountToSend += CAmount(e.note.value());

            if (orchardAmountToSend <= fSweepTxFee)
                break;

            CAmount orchardFee = fSweepTxFee;

            auto orchardBuilder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                orchardBuilder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Creating Orchard sweep transaction with %d notes, output amount=%s\n", getId(), (int)orchardEntries.size(), FormatMoney(orchardAmountToSend - orchardFee));

            uint256 orchardAnchor;
            bool buildFailed = false;
            for (auto& orchardEntry : orchardEntries) {
                libzcash::MerklePath orchardSweepMerklePath;
                if (!pwalletMain->OrchardWalletGetMerklePathOfNote(orchardEntry.op.hash, orchardEntry.op.n, orchardSweepMerklePath)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Orchard note. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }

                uint256 anchor;
                if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardSweepMerklePath, orchardEntry.note.cmx(), anchor)) {
                    LogPrint("zrpcunsafe", "%s: Getting Anchor failed for Orchard note. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }

                if (orchardAnchor.IsNull()) {
                    orchardAnchor = anchor;
                } else if (orchardAnchor != anchor) {
                    LogPrint("zrpcunsafe", "%s: Anchor mismatch for Orchard notes. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }

                libzcash::OrchardNote orchardNote = orchardEntry.note;
                if (!orchardBuilder.AddOrchardSpendRaw(orchardEntry.op, orchardEntry.address, orchardNote.value(), orchardNote.rho(), orchardNote.rseed(), orchardSweepMerklePath, orchardAnchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Orchard Spend failed. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) break;

            orchardBuilder.InitializeOrchard(true, true, orchardAnchor);

            if (!orchardBuilder.ConvertRawOrchardSpend(orchardExtendedSpendingKey)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Orchard Spends failed. Stopping.\n", getId());
                break;
            }

            orchardBuilder.SetFee(orchardFee);

            // Get outgoing viewing key
            uint256 orchardOvk;
            libzcash::OrchardFullViewingKey orchardFvk;
            if (!orchardExtendedSpendingKey.sk.DeriveFVK(&orchardFvk)) {
                LogPrint("zrpcunsafe", "%s: Failed to get FVK from spending key. Stopping.\n", getId());
                break;
            }
            libzcash::OrchardOutgoingViewingKey orchardOvkObj;
            if (!orchardFvk.DeriveOVK(&orchardOvkObj)) {
                LogPrint("zrpcunsafe", "%s: Failed to get OVK from FVK. Stopping.\n", getId());
                break;
            }
            orchardOvk = orchardOvkObj.ovk;

            if (hasOrchardTarget) {
                orchardBuilder.AddOrchardOutputRaw(orchardSweepAddress, orchardAmountToSend - orchardFee, std::nullopt);
                orchardBuilder.ConvertRawOrchardOutput(orchardOvk);
            } else if (hasSaplingTarget) {
                orchardBuilder.AddSaplingOutputRaw(saplingSweepAddress, orchardAmountToSend - orchardFee, std::nullopt);
                orchardBuilder.ConvertRawSaplingOutput(orchardOvk);
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Orchard sweep. Stopping.\n", getId());
                break;
            }

            auto orchardTx = orchardBuilder.Build().GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(orchardTx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit Orchard sweep transaction, stopping.\n", getId());
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Orchard sweep transaction with txid=%s\n", getId(), orchardTx.GetHash().ToString());
            numTxCreated++;
            amountSwept += orchardAmountToSend - orchardFee;
            sweepTxIds.push_back(orchardTx.GetHash().ToString());
        }
    }

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->nextSweep = pwalletMain->sweepInterval + chainActive.Tip()->nHeight;
        pwalletMain->fSweepRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s to single destination\n", getId(), numTxCreated, FormatMoney(amountSwept));
    setSweepResult(numTxCreated, amountSwept, sweepTxIds);
    return true;
}

/**
 * @brief Sets the final result of the sweep operation
 * 
 * Formats and stores sweep operation results in JSON structure
 * for client retrieval via RPC calls.
 * 
 * @param numTxCreated Number of sweep transactions created and broadcast
 * @param amountSwept Total net amount transferred (excluding fees)
 * @param sweepTxIds Vector of transaction IDs for created transactions
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
 * @brief Cancels the sweep operation
 * 
 * Sets operation state to cancelled, preventing further execution.
 * Already-broadcast transactions cannot be cancelled.
 */
void AsyncRPCOperation_sweeptoaddress::cancel()
{
    set_state(OperationStatus::CANCELLED);
}

/**
 * @brief Returns comprehensive status information for the sweep operation
 * 
 * Extends base AsyncRPCOperation status with sweep-specific details
 * including method type and target height.
 * 
 * @return UniValue object with operation status, results, and configuration
 */
UniValue AsyncRPCOperation_sweeptoaddress::getStatus() const
{
    UniValue v = AsyncRPCOperation::getStatus();
    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "sweeptoaddress"));
    obj.push_back(Pair("target_height", targetHeight_));
    return obj;
}

//==============================================================================
// ADDRESS CONFIGURATION FUNCTIONS
//==============================================================================

/**
 * @brief Configures Sapling destination address for RPC-initiated sweeps
 * 
 * Sets target Sapling address and clears any Orchard address to ensure
 * single destination protocol per operation.
 * 
 * @param address Valid Sapling payment address for sweep destination
 */
void AsyncRPCOperation_sweeptoaddress::setSaplingSweepAddress(const libzcash::SaplingPaymentAddress& address) {
    rpcSaplingSweepAddress = address;
    rpcOrchardSweepAddress.reset(); // Clear Orchard address to ensure only one destination
}

/**
 * @brief Configures Orchard destination address for RPC-initiated sweeps
 * 
 * Sets target Orchard address and clears any Sapling address to ensure
 * single destination protocol per operation.
 * 
 * @param address Valid Orchard payment address for sweep destination
 */
void AsyncRPCOperation_sweeptoaddress::setOrchardSweepAddress(const libzcash::OrchardPaymentAddress& address) {
    rpcOrchardSweepAddress = address;
    rpcSaplingSweepAddress.reset(); // Clear Sapling address to ensure only one destination
}
