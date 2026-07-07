// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_sweeptoaddress.cpp
 * @brief Implementation of asynchronous sweep-to-address operation
 * 
 * Implements consolidation of funds from multiple addresses into a single
 * destination address. Supports both Sapling and Ironwood protocols.
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
 * Target address for Sapling sweeps or cross-protocol sweeps from Ironwood.
 * Mutually exclusive with rpcIronwoodSweepAddress.
 */
std::optional<libzcash::SaplingPaymentAddress> rpcSaplingSweepAddress;

/**
 * @brief Ironwood destination address for RPC-initiated sweep operations
 * 
 * Target address for Ironwood sweeps or cross-protocol sweeps from Sapling.
 * Mutually exclusive with rpcSaplingSweepAddress.
 */
std::optional<libzcash::IronwoodPaymentAddress> rpcIronwoodSweepAddress;

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
 * destination address across Sapling and Ironwood protocols.
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
    libzcash::IronwoodPaymentAddress ironwoodSweepAddress;
    bool hasSaplingTarget = false;
    bool hasIronwoodTarget = false;
    {
        if (rpcSaplingSweepAddress.has_value()) {
            saplingSweepAddress = rpcSaplingSweepAddress.value();
            hasSaplingTarget = true;
        } else if (rpcIronwoodSweepAddress.has_value()) {
            ironwoodSweepAddress = rpcIronwoodSweepAddress.value();
            hasIronwoodTarget = true;
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
            } else if (std::get_if<libzcash::IronwoodPaymentAddress>(&zAddress) != nullptr) {
                ironwoodSweepAddress = *std::get_if<libzcash::IronwoodPaymentAddress>(&zAddress);
                hasIronwoodTarget = true;
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
    std::set<libzcash::IronwoodPaymentAddress> ironwoodCandidates;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->GetSaplingPaymentAddresses(saplingCandidates);
        if (hasSaplingTarget)
            saplingCandidates.erase(saplingSweepAddress);

        pwalletMain->GetIronwoodPaymentAddresses(ironwoodCandidates);
        if (hasIronwoodTarget)
            ironwoodCandidates.erase(ironwoodSweepAddress);
    }

    int numTxCreated = 0;
    std::vector<std::string> sweepTxIds;
    CAmount amountSwept = 0;
    const int maxQuantity = 50;

    struct AddressSweepWork {
        libzcash::SaplingPaymentAddress addr;
        libzcash::SaplingExtendedSpendingKey extsk;
        std::vector<SaplingNoteEntry> saplingEntries;
    };

    struct IronwoodSweepWork {
        libzcash::IronwoodPaymentAddress addr;
        libzcash::IronwoodExtendedSpendingKeyPirate extsk;
        std::vector<IronwoodNoteEntry> ironwoodEntries;
    };

    std::set<libzcash::PaymentAddress> allFilterAddresses;
    for (const auto& addr : saplingCandidates) {
        allFilterAddresses.insert(addr);
    }
    for (const auto& addr : ironwoodCandidates) {
        allFilterAddresses.insert(addr);
    }

    std::vector<SaplingNoteEntry> allSaplingEntries;
    std::vector<IronwoodNoteEntry> allIronwoodEntries;
    if (!allFilterAddresses.empty()) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->GetFilteredNotes(allSaplingEntries, allIronwoodEntries,
                                      allFilterAddresses,
                                      11, INT_MAX, true, true, true,
                                      0, 0);
    }

    auto unlockAllFetchedNotes = [&]() {
        if (allSaplingEntries.empty() && allIronwoodEntries.empty()) {
            return;
        }

        LOCK2(cs_main, pwalletMain->cs_wallet);
        for (const auto& entry : allSaplingEntries)
            pwalletMain->UnlockNote(entry.op);
        for (const auto& entry : allIronwoodEntries)
            pwalletMain->UnlockNote(entry.op);
    };

    std::vector<AddressSweepWork> saplingWork;
    std::map<libzcash::SaplingPaymentAddress, size_t> saplingWorkIndexByAddress;
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

        saplingWorkIndexByAddress[addr] = saplingWork.size();
        saplingWork.push_back({addr, extsk, {}});
    }

    for (const auto& entry : allSaplingEntries) {
        auto it = saplingWorkIndexByAddress.find(entry.address);
        if (it != saplingWorkIndexByAddress.end()) {
            saplingWork[it->second].saplingEntries.push_back(entry);
        }
    }

    std::vector<IronwoodSweepWork> ironwoodWork;
    std::map<libzcash::IronwoodPaymentAddress, size_t> ironwoodWorkIndexByAddress;
    for (const auto& addr : ironwoodCandidates) {
        if (isCancelled() || ShutdownRequested())
            break;

        libzcash::IronwoodExtendedSpendingKeyPirate ironwoodExtendedSpendingKey;
        {
            LOCK2(cs_main, pwalletMain->cs_wallet);
            if (!pwalletMain->GetIronwoodExtendedSpendingKey(addr, ironwoodExtendedSpendingKey))
                continue;
        }

        ironwoodWorkIndexByAddress[addr] = ironwoodWork.size();
        ironwoodWork.push_back({addr, ironwoodExtendedSpendingKey, {}});
    }

    for (const auto& entry : allIronwoodEntries) {
        auto it = ironwoodWorkIndexByAddress.find(entry.address);
        if (it != ironwoodWorkIndexByAddress.end()) {
            ironwoodWork[it->second].ironwoodEntries.push_back(entry);
        }
    }

    // === Sapling sweep pass ===
    for (auto& work : saplingWork) {
        if (work.saplingEntries.empty())
            continue;

        size_t cursor = 0;

        while (cursor < work.saplingEntries.size()) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping sweep inner loop (cancelled or shutdown).\n", getId());
                break;
            }

            const CAmount fee = fSweepTxFee;
            std::vector<SaplingOutPoint> ops;
            std::vector<libzcash::SaplingNote> notes;
            CAmount amountToSend = 0;

            int selected = 0;
            while (cursor < work.saplingEntries.size() && selected < maxQuantity) {
                const auto& entry = work.saplingEntries[cursor++];
                ops.push_back(entry.op);
                notes.push_back(entry.note);
                amountToSend += CAmount(entry.note.value());
                selected++;
            }

            if (ops.empty())
                break;

            if (amountToSend <= fee)
                break;

            const CAmount outputAmount = amountToSend - fee;

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
            LogPrint("zrpcunsafe", "%s: Creating Sapling sweep transaction with output amount=%s\n", getId(), FormatMoney(outputAmount));

            bool buildFailed = false;
            const auto& selectedSaplingOps = ops;
            for (size_t i = 0; i < notes.size(); i++) {
                if (!builder.AddSaplingSpendRaw(selectedSaplingOps[i], work.addr, notes[i].value(), notes[i].rcm(), saplingMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Spend failed. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) {
                break;
            }

            builder.InitializeSapling(anchor);
            if (!builder.ConvertRawSaplingSpend(work.extsk)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Spends failed. Stopping.\n", getId());
                break;
            }

            builder.SetFee(fee);

            if (hasSaplingTarget) {
                if (!builder.AddSaplingOutputRaw(saplingSweepAddress, outputAmount)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
                if (!builder.ConvertRawSaplingOutput(work.extsk.expsk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
            } else if (hasIronwoodTarget) {
                if (!builder.AddIronwoodOutputRaw(ironwoodSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Ironwood Output failed. Stopping.\n", getId());
                    break;
                }
                builder.InitializeIronwood(false, true, uint256());
                if (!builder.ConvertRawIronwoodOutput(work.extsk.expsk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Ironwood Output failed. Stopping.\n", getId());
                    break;
                }
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Sapling sweep. Stopping.\n", getId());
                break;
            }

            auto buildResult = builder.Build();
            if (!buildResult.IsTx()) {
                LogPrint("zrpcunsafe", "%s: Failed to build sweep transaction. Stopping.\n", getId());
                break;
            }
            auto tx = buildResult.GetTxOrThrow();

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
            amountSwept += outputAmount;
            sweepTxIds.push_back(tx.GetHash().ToString());
        }
    }

    // === Ironwood sweep pass ===
    for (auto& work : ironwoodWork) {
        if (work.ironwoodEntries.empty())
            continue;

        size_t cursor = 0;

        while (cursor < work.ironwoodEntries.size()) {
            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Stopping Ironwood sweep inner loop (cancelled or shutdown).\n", getId());
                break;
            }

            const CAmount fee = fSweepTxFee;
            std::vector<IronwoodNoteEntry> selectedIronwoodEntries;
            std::vector<IronwoodOutPoint> ops;
            CAmount amountToSend = 0;

            int selected = 0;
            while (cursor < work.ironwoodEntries.size() && selected < maxQuantity) {
                const auto& entry = work.ironwoodEntries[cursor++];
                selectedIronwoodEntries.push_back(entry);
                ops.push_back(entry.op);
                amountToSend += CAmount(entry.note.value());
                selected++;
            }

            if (ops.empty())
                break;

            if (amountToSend <= fee)
                break;

            const CAmount outputAmount = amountToSend - fee;

            auto builder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                builder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);
            }
            LogPrint("zrpcunsafe", "%s: Creating Ironwood sweep transaction with %d notes, output amount=%s\n",
                     getId(), (int)selectedIronwoodEntries.size(), FormatMoney(outputAmount));

            uint256 anchor;
            std::vector<libzcash::MerklePath> ironwoodMerklePaths;
            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->GetIronwoodNoteMerklePaths(ops, ironwoodMerklePaths, anchor)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Ironwood note. Stopping.\n", getId());
                    break;
                }
            }

            bool buildFailed = false;
            for (size_t i = 0; i < selectedIronwoodEntries.size(); i++) {
                const auto& entry = selectedIronwoodEntries[i];
                auto ironwoodNote = entry.note;
                if (!builder.AddIronwoodSpendRaw(entry.op, work.addr,
                                                ironwoodNote.value(), ironwoodNote.rho(),
                                                ironwoodNote.rseed(), ironwoodMerklePaths[i], anchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Ironwood Spend failed. Stopping.\n", getId());
                    buildFailed = true;
                    break;
                }
            }
            if (buildFailed) {
                break;
            }

            builder.InitializeIronwood(true, true, anchor);

            if (!builder.ConvertRawIronwoodSpend(work.extsk)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Ironwood Spends failed. Stopping.\n", getId());
                break;
            }

            builder.SetFee(fee);

            libzcash::IronwoodFullViewingKey fvk;
            if (!work.extsk.sk.DeriveFVK(&fvk)) {
                LogPrint("zrpcunsafe", "%s: Failed to get FVK from spending key. Stopping.\n", getId());
                break;
            }
            libzcash::IronwoodOutgoingViewingKey ovk;
            if (!fvk.DeriveOVK(&ovk)) {
                LogPrint("zrpcunsafe", "%s: Failed to get OVK from FVK. Stopping.\n", getId());
                break;
            }

            if (hasIronwoodTarget) {
                if (!builder.AddIronwoodOutputRaw(ironwoodSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Ironwood Output failed. Stopping.\n", getId());
                    break;
                }
                if (!builder.ConvertRawIronwoodOutput(ovk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Ironwood Output failed. Stopping.\n", getId());
                    break;
                }
            } else if (hasSaplingTarget) {
                if (!builder.AddSaplingOutputRaw(saplingSweepAddress, outputAmount, std::nullopt)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
                builder.InitializeSapling(uint256()); // Dummy anchor for cross-protocol output conversion
                if (!builder.ConvertRawSaplingOutput(ovk.ovk)) {
                    LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Output failed. Stopping.\n", getId());
                    break;
                }
            } else {
                LogPrint("zrpcunsafe", "%s: No target address specified for Ironwood sweep. Stopping.\n", getId());
                break;
            }

            auto buildResult = builder.Build();
            if (!buildResult.IsTx()) {
                LogPrint("zrpcunsafe", "%s: Failed to build Ironwood sweep transaction. Stopping.\n", getId());
                break;
            }
            auto tx = buildResult.GetTxOrThrow();

            if (isCancelled() || ShutdownRequested()) {
                LogPrint("zrpcunsafe", "%s: Canceled or shutdown. Stopping.\n", getId());
                break;
            }

            {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                if (!pwalletMain->CommitAutomatedTx(tx)) {
                    LogPrint("zrpcunsafe", "%s: Failed to commit Ironwood sweep transaction, stopping.\n", getId());
                    break;
                }
            }
            LogPrint("zrpcunsafe", "%s: Committed Ironwood sweep transaction with txid=%s\n",
                     getId(), tx.GetHash().ToString());
            numTxCreated++;
            amountSwept += outputAmount;
            sweepTxIds.push_back(tx.GetHash().ToString());
        }
    }

    unlockAllFetchedNotes();

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
    rpcIronwoodSweepAddress.reset(); // Clear Ironwood address to ensure only one destination
}

/**
 * @brief Configure Ironwood destination address for RPC-initiated sweeps
 * 
 * @param address Valid Ironwood payment address for sweep destination
 */
void AsyncRPCOperation_sweeptoaddress::setIronwoodSweepAddress(const libzcash::IronwoodPaymentAddress& address) {
    rpcIronwoodSweepAddress = address;
    rpcSaplingSweepAddress.reset(); // Clear Sapling address to ensure only one destination
}
