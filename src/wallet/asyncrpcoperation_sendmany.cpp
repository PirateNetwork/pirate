// Copyright (c) 2016 The Zcash developers
// Copyright (c) 2022-2025 The Pirate Network developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "asyncrpcoperation_sendmany.h"
#include "amount.h"
#include "consensus/params.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "rpc/protocol.h"
#include "rpc/rawtransaction.h"
#include "rpc/server.h"
#include "util.h"
#include "utilmoneystr.h"
#include "wallet.h"
#include "zcash/IncrementalMerkleTree.hpp"

#include <stdint.h>

#include <array>
#include <string>

using namespace libzcash;

/**
 * @brief Find the position of output @p n in a JoinSplit outputmap
 *
 * @param obj UniValue object that must contain an "outputmap" array
 * @param n   Output index to locate
 * @return    Position within the outputmap array
 * @throws JSONRPCError     if "outputmap" is absent or not an array
 * @throws std::logic_error if @p n is not present in the map
 */
int find_output(UniValue obj, int n)
{
    UniValue outputMapValue = find_value(obj, "outputmap");
    if (!outputMapValue.isArray()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing outputmap for JoinSplit operation");
    }

    UniValue outputMap = outputMapValue.get_array();
    assert(outputMap.size() == ZC_NUM_JS_OUTPUTS);
    
    for (size_t i = 0; i < outputMap.size(); i++) {
        if (outputMap[i].get_int() == n) {
            return static_cast<int>(i);
        }
    }

    throw std::logic_error("n is not present in outputmap");
}

/**
 * @brief Constructor for AsyncRPCOperation_sendmany
 * 
 * Initializes the sendmany operation with the specified parameters and validates
 * input parameters. Sets up address types and spending keys as needed.
 * 
 * @param consensusParams Consensus parameters for the current network
 * @param blockHeight Current blockchain height
 * @param fromAddress Source address (Sapling or Orchard shielded)
 * @param saplingOutputs Vector of Sapling recipients
 * @param orchardOutputs Vector of Orchard recipients  
 * @param minimumConfirmationDepth Minimum confirmation depth for inputs
 * @param transactionFee Transaction fee amount
 * @param contextInfo Context information for logging and status
 * 
 * @throws JSONRPCError for invalid parameters or addresses
 */
AsyncRPCOperation_sendmany::AsyncRPCOperation_sendmany(
    const Consensus::Params& consensusParams,
    const int blockHeight,
    std::string fromAddress,
    std::vector<SendManyRecipient> saplingOutputs,
    std::vector<SendManyRecipient> orchardOutputs,
    int minimumConfirmationDepth,
    CAmount transactionFee,
    UniValue contextInfo) 
    : fromaddress_(fromAddress),
      saplingOutputs_(std::move(saplingOutputs)),
      orchardOutputs_(std::move(orchardOutputs)),
      mindepth_(minimumConfirmationDepth),
      fee_(transactionFee),
      contextinfo_(contextInfo),
      isFromSaplingAddress_(false),
      isFromOrchardAddress_(false),
      hasOfflineSpendingKey(false),
      builder_(TransactionBuilder(consensusParams, blockHeight, pwalletMain))
{
    assert(fee_ >= 0);
    if (minimumConfirmationDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minconf cannot be negative");
    }

    if (fromAddress.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "From address parameter missing");
    }

    if (saplingOutputs_.empty() && orchardOutputs_.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No recipients");
    }

    // Reject transparent from-addresses — use z_shieldcoinbase instead
    if (IsValidDestination(DecodeDestination(fromAddress))) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
            "Transparent addresses are not supported as a source for z_sendmany. "
            "To shield funds from a transparent address, use z_shieldcoinbase.");
    }

    auto decodedAddress = DecodePaymentAddress(fromAddress);
    if (IsValidPaymentAddress(decodedAddress)) {

        // Check if this is a Sapling payment address
        auto saplingPaymentAddress = std::get_if<libzcash::SaplingPaymentAddress>(&decodedAddress);
        if (saplingPaymentAddress != nullptr) {
            isFromSaplingAddress_ = true;
        }

        // Check if this is an Orchard payment address
        auto orchardPaymentAddress = std::get_if<libzcash::OrchardPaymentAddress>(&decodedAddress);
        if (orchardPaymentAddress != nullptr) {
            isFromOrchardAddress_ = true;
        }

        // Ensure we have a valid shielded address type
        if (!isFromSaplingAddress_ && !isFromOrchardAddress_) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address");
        }

        frompaymentaddress_ = decodedAddress;

        // Check if we have the spending key for this address
        // Wallet spending key methods are thread-safe, so no locking needed
        if (!std::visit(HaveSpendingKeyForPaymentAddress(pwalletMain), decodedAddress)) {
            // Address is valid but we don't have the spending key
            // This enables offline transaction preparation
            hasOfflineSpendingKey = true;
        } else {
            // We have the spending key, retrieve it for transaction building
            spendingkey_ = std::visit(GetSpendingKeyForPaymentAddress(pwalletMain), decodedAddress).value();
            hasOfflineSpendingKey = false;
        }
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address");
    }

    // Shielded addresses require minimum confirmation depth > 0
    if ((isFromSaplingAddress_ || isFromOrchardAddress_) && minimumConfirmationDepth == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minconf cannot be zero when sending from zaddr");
    }

    // Log operation initialization with appropriate detail level
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_sendmany initialized (params=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_sendmany initialized\n", getId());
    }
}

/// Destructor — all resources are managed by RAII members.
AsyncRPCOperation_sendmany::~AsyncRPCOperation_sendmany() = default;

/**
 * @brief Main execution wrapper for the sendmany operation
 *
 * Manages the operation lifecycle: sets state, runs main_impl(), catches all
 * exceptions, stops the clock, and logs the final outcome.
 */
void AsyncRPCOperation_sendmany::main()
{
    // Early exit if operation was cancelled
    if (isCancelled()) {
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool operationSuccessful = false;

    try {
        operationSuccessful = main_impl();
    } catch (const UniValue& objError) {
        // Handle JSON RPC errors with structured error information
        int errorCode = find_value(objError, "code").get_int();
        std::string errorMessage = find_value(objError, "message").get_str();
        set_error_code(errorCode);
        set_error_message(errorMessage);
    } catch (const std::runtime_error& e) {
        // Handle runtime errors (file I/O, network, etc.)
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
        set_error_message("Unknown error occurred during send");
    }

    // Stop timing and set final operation state
    stop_execution_clock();

    if (operationSuccessful) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    // Log operation completion with appropriate details
    std::string logMessage = strprintf("%s: z_sendmany finished (status=%s", getId(), getStateAsString());
    if (operationSuccessful) {
        logMessage += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s", logMessage);
}

/**
 * @brief Core implementation of the z_sendmany operation
 *
 * Selects shielded notes from the source address, optionally consolidates
 * additional small notes, builds the transaction with all outputs, and
 * broadcasts it to the network (or returns raw hex in test mode).
 *
 * Note selection: notes are sorted descending by value; the minimum set
 * satisfying totalTargetAmount is chosen first. If more than 100 notes are
 * available a random number (10–44) of additional small notes are included
 * to opportunistically consolidate the wallet.
 *
 * On any failure the error code/message are set via set_error_code() /
 * set_error_message() and the function returns false instead of throwing.
 *
 * @return true if the transaction was successfully built and sent, false otherwise
 */
bool AsyncRPCOperation_sendmany::main_impl()
{
    CAmount minersFee = fee_;

    // STEP 1: Find and validate input sources (shielded notes).
    if (!find_unspent_notes()) {
        set_error_code(RPC_WALLET_INSUFFICIENT_FUNDS);
        set_error_message("Insufficient funds, no unspent notes found for from address.");
        return false;
    }

    // Unlock all locked notes on any exit path — failure or success.
    // After a successful broadcast the transaction is committed and
    // GetFilteredNotes will naturally exclude the spent notes; for failures
    // the notes must be freed so other operations can select them.
    auto unlock_notes = [&]() {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        for (const auto& e : saplingInputs_)
            pwalletMain->UnlockNote(e.op);
        for (const auto& e : orchardInputs_)
            pwalletMain->UnlockNote(e.op);
    };

    // STEP 2: Calculate total input and output amounts.
    // Calculate total Sapling inputs
    CAmount totalSaplingInputs = 0;
    for (const auto& saplingInput : saplingInputs_) {
        totalSaplingInputs += saplingInput.note.value();
    }

    // Calculate total Orchard inputs
    CAmount totalOrchardInputs = 0;
    for (const auto& orchardInput : orchardInputs_) {
        totalOrchardInputs += orchardInput.note.value();
    }

    // Calculate total Sapling outputs
    CAmount totalSaplingOutputs = 0;
    for (const SendManyRecipient& saplingOutput : saplingOutputs_) {
        totalSaplingOutputs += std::get<1>(saplingOutput);
    }

    // Calculate total Orchard outputs
    CAmount totalOrchardOutputs = 0;
    for (const SendManyRecipient& orchardOutput : orchardOutputs_) {
        totalOrchardOutputs += std::get<1>(orchardOutput);
    }

    CAmount totalSendAmount = totalSaplingOutputs + totalOrchardOutputs;
    CAmount totalTargetAmount = totalSendAmount + minersFee;

    // STEP 3: Validate input/output consistency and sufficiency.
    if (isFromSaplingAddress_ && (totalSaplingInputs < totalTargetAmount)) {
        set_error_code(RPC_WALLET_INSUFFICIENT_FUNDS);
        set_error_message(strprintf("Insufficient shielded funds, have %s, need %s",
                                    FormatMoney(totalSaplingInputs), FormatMoney(totalTargetAmount)));
        unlock_notes();
        return false;
    }

    if (isFromOrchardAddress_ && (totalOrchardInputs < totalTargetAmount)) {
        set_error_code(RPC_WALLET_INSUFFICIENT_FUNDS);
        set_error_message(strprintf("Insufficient shielded funds, have %s, need %s",
                                    FormatMoney(totalOrchardInputs), FormatMoney(totalTargetAmount)));
        unlock_notes();
        return false;
    }

    // Log transaction composition for debugging purposes
    LogPrint("zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
             getId(), FormatMoney(totalTargetAmount), FormatMoney(totalSendAmount), FormatMoney(minersFee));
    LogPrint("zrpcunsafe", "%s: shielded input: %s sapling + %s orchard = %s total (to choose from)\n",
             getId(), FormatMoney(totalSaplingInputs), FormatMoney(totalOrchardInputs),
             FormatMoney(totalSaplingInputs + totalOrchardInputs));
    LogPrint("zrpcunsafe", "%s: shielded output: %s sapling + %s orchard = %s total\n",
             getId(), FormatMoney(totalSaplingOutputs), FormatMoney(totalOrchardOutputs),
             FormatMoney(totalSaplingOutputs + totalOrchardOutputs));
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    builder_.SetFee(minersFee);

    // OVK will be set based on the from address type (Sapling or Orchard spending key).
    uint256 ovk;

    const int maxQuantity = rand() % 35 + 10;

    // STEP 4: Handle Sapling shielded address spending.
    if (isFromSaplingAddress_) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        SaplingExtendedSpendingKey extsk;
        auto extskPtr = std::get_if<libzcash::SaplingExtendedSpendingKey>(&spendingkey_);
        if (!extskPtr) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Invalid Sapling spending key type. Stopping.", getId()));
            unlock_notes();
            return false;
        }
        extsk = *extskPtr;

        libzcash::SaplingFullViewingKey fvk;
        if (!extsk.expsk.DeriveFVK(&fvk)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Failed to derive FVK from Sapling spending key. Stopping.", getId()));
            unlock_notes();
            return false;
        }
        ovk = fvk.ovk;

        // Select notes largest-first until sum meets the target amount.
        std::vector<size_t> selectedIdx;
        CAmount sum = 0;
        for (size_t i = 0; i < saplingInputs_.size(); i++) {
            selectedIdx.push_back(i);
            sum += saplingInputs_[i].note.value();
            if (sum >= totalTargetAmount)
                break;
        }

        // Opportunistic consolidation: if >100 notes are available, backfill with
        // the smallest notes (from the low end of the descending-sorted set) until
        // we reach 50 inputs total. The builder returns excess as change.
        if (saplingInputs_.size() > 100 && (int)selectedIdx.size() < maxQuantity) {
            for (int i = (int)saplingInputs_.size() - 1;
                 i >= (int)selectedIdx.size() && (int)selectedIdx.size() < maxQuantity; i--) {
                selectedIdx.push_back((size_t)i);
            }
        }

        std::vector<SaplingOutPoint> ops;
        std::vector<libzcash::SaplingNote> notes;
        for (size_t idx : selectedIdx) {
            ops.push_back(saplingInputs_[idx].op);
            notes.push_back(saplingInputs_[idx].note);
        }

        uint256 anchor;
        std::vector<libzcash::MerklePath> saplingMerklePaths;
        if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Merkle Path not found for Sapling note. Stopping.", getId()));
            unlock_notes();
            return false;
        }

        for (size_t i = 0; i < notes.size(); i++) {
            if (!builder_.AddSaplingSpendRaw(ops[i], saplingInputs_[selectedIdx[i]].address, notes[i].value(), notes[i].rcm(), saplingMerklePaths[i], anchor)) {
                set_error_code(RPC_WALLET_ERROR);
                set_error_message(strprintf("%s: Adding Raw Sapling Spend failed. Stopping.", getId()));
                unlock_notes();
                return false;
            }
        }

        if (!builder_.ConvertRawSaplingSpend(extsk)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Converting Raw Sapling Spends failed.", getId()));
            unlock_notes();
            return false;
        }
    }

    // STEP 5: Handle Orchard shielded address spending.
    if (isFromOrchardAddress_) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Extract the spending key, derive the FVK, and obtain the OVK for output encryption.
        OrchardExtendedSpendingKeyPirate extsk;
        auto extskPtr = std::get_if<libzcash::OrchardExtendedSpendingKeyPirate>(&spendingkey_);
        if (!extskPtr) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Invalid Orchard spending key type. Stopping.", getId()));
            unlock_notes();
            return false;
        }
        extsk = *extskPtr;

        auto fvkOpt = extsk.GetXFVK();
        if (!fvkOpt) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: FVK not found for Orchard spending key. Stopping.", getId()));
            unlock_notes();
            return false;
        }

        auto fvk = fvkOpt.value().fvk;
        OrchardOutgoingViewingKey ovkObj;
        if (!fvk.DeriveOVK(&ovkObj)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: OVK not found for Orchard spending key. Stopping.", getId()));
            unlock_notes();
            return false;
        }

        ovk = ovkObj.ovk;

        // Select notes largest-first until sum meets the target amount.
        std::vector<size_t> selectedIdx;
        CAmount sum = 0;
        for (size_t i = 0; i < orchardInputs_.size(); i++) {
            selectedIdx.push_back(i);
            sum += orchardInputs_[i].note.value();
            if (sum >= totalTargetAmount)
                break;
        }

        // Opportunistic consolidation: if >100 notes are available, backfill with
        // the smallest notes (from the low end of the descending-sorted set) until
        // we reach 50 inputs total. The builder returns excess as change.
        if (orchardInputs_.size() > 100 && (int)selectedIdx.size() < maxQuantity) {
            for (int i = (int)orchardInputs_.size() - 1;
                 i >= (int)selectedIdx.size() && (int)selectedIdx.size() < maxQuantity; i--) {
                selectedIdx.push_back((size_t)i);
            }
        }

        std::vector<OrchardOutPoint> ops;
        for (size_t idx : selectedIdx) {
            ops.push_back(orchardInputs_[idx].op);
        }

        uint256 anchor;
        std::vector<libzcash::MerklePath> orchardMerklePaths;
        if (!pwalletMain->GetOrchardNoteMerklePaths(ops, orchardMerklePaths, anchor)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Merkle Path not found for Orchard note. Stopping.", getId()));
            unlock_notes();
            return false;
        }

        for (size_t i = 0; i < ops.size(); i++) {
            const auto& entry = orchardInputs_[selectedIdx[i]];
            if (!builder_.AddOrchardSpendRaw(entry.op, entry.address, entry.note.value(), entry.note.rho(), entry.note.rseed(), orchardMerklePaths[i], anchor)) {
                set_error_code(RPC_WALLET_ERROR);
                set_error_message(strprintf("%s: Adding Raw Orchard Spend failed. Stopping.", getId()));
                unlock_notes();
                return false;
            }
        }

        builder_.InitializeOrchard(true, true, anchor);

        if (!builder_.ConvertRawOrchardSpend(extsk)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Converting Raw Orchard Spends failed.", getId()));
            unlock_notes();
            return false;
        }

    } else {
        // Initialize Orchard builder without spending (outputs only)
        if (!orchardOutputs_.empty()) {
            builder_.InitializeOrchard(false, true, uint256());
        }
    }

    // Ensure we have a valid OVK at this point
    if (ovk.IsNull()) {
        set_error_code(RPC_WALLET_ERROR);
        set_error_message(strprintf("%s: OVK was not properly initialized for this address type.", getId()));
        unlock_notes();
        return false;
    }

    // STEP 6: Add outputs to the transaction.
    // Process Sapling outputs
    for (const auto& [recipientAddress, outputValue, hexMemo] : saplingOutputs_) {

        // Decode and validate the Sapling payment address
        auto decodedAddress = DecodePaymentAddress(recipientAddress);
        assert(std::get_if<libzcash::SaplingPaymentAddress>(&decodedAddress) != nullptr);
        auto saplingPaymentAddress = *(std::get_if<libzcash::SaplingPaymentAddress>(&decodedAddress));

        // Convert hex memo to Memo object or nullopt
        std::optional<libzcash::Memo> memo = std::nullopt;
        if (!hexMemo.empty()) {
            auto memoArray = get_memo_from_hex_string(hexMemo);
            memo = libzcash::Memo(memoArray);
        }

        // Add the raw Sapling output to the transaction builder
        if (!builder_.AddSaplingOutputRaw(saplingPaymentAddress, outputValue, memo)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Adding Raw Sapling Output failed. Stopping.", getId()));
            unlock_notes();
            return false;
        }
    }

    // Convert raw Sapling outputs with OVK for encryption
    if (!builder_.ConvertRawSaplingOutput(ovk)) {
        set_error_code(RPC_WALLET_ERROR);
        set_error_message(strprintf("%s: Converting Raw Sapling Outputs failed.", getId()));
        unlock_notes();
        return false;
    }

    // Process Orchard outputs
    for (const auto& [recipientAddress, outputValue, hexMemo] : orchardOutputs_) {

        // Decode and validate the Orchard payment address
        auto decodedAddress = DecodePaymentAddress(recipientAddress);
        assert(std::get_if<libzcash::OrchardPaymentAddress>(&decodedAddress) != nullptr);
        auto orchardPaymentAddress = *(std::get_if<libzcash::OrchardPaymentAddress>(&decodedAddress));

        // Convert hex memo to Memo object or nullopt
        std::optional<libzcash::Memo> memo = std::nullopt;
        if (!hexMemo.empty()) {
            auto memoArray = get_memo_from_hex_string(hexMemo);
            memo = libzcash::Memo(memoArray);
        }

        // Add the raw Orchard output to the transaction builder
        if (!builder_.AddOrchardOutputRaw(orchardPaymentAddress, outputValue, memo)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Adding Raw Orchard Output failed. Stopping.", getId()));
            unlock_notes();
            return false;
        }
    }

    // Convert raw Orchard outputs with OVK for encryption.
    if (!orchardOutputs_.empty()) {
        if (!builder_.ConvertRawOrchardOutput(ovk)) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message(strprintf("%s: Converting Raw Orchard Outputs failed.", getId()));
            unlock_notes();
            return false;
        }
    }
    
    // STEP 7: Build and send the transaction.
    // Guard against building a transaction that would exceed the protocol size limit.
    if (!builder_.IsValidSize()) {
        set_error_code(RPC_WALLET_ERROR);
        set_error_message(strprintf("%s: Transaction too large: estimated size exceeds the protocol maximum.", getId()));
        unlock_notes();
        return false;
    }

    // Build the final transaction from all inputs and outputs
    auto buildResult = builder_.Build();
    if (!buildResult.IsTx()) {
        set_error_code(RPC_WALLET_ERROR);
        set_error_message(strprintf("%s: Failed to build transaction.", getId()));
        unlock_notes();
        return false;
    }
    tx_ = buildResult.GetTxOrThrow();

    // Encode transaction for transmission
    auto signedTransactionHex = EncodeHexTx(tx_);
    
    if (!testmode) {
        // Production mode: broadcast transaction to the network
        UniValue rpcParams = UniValue(UniValue::VARR);
        rpcParams.push_back(signedTransactionHex);
        
        UniValue broadcastResult = sendrawtransaction(rpcParams, false, CPubKey());
        if (broadcastResult.isNull()) {
            set_error_code(RPC_WALLET_ERROR);
            set_error_message("sendrawtransaction did not return an error or a txid.");
            unlock_notes();
            return false;
        }

        std::string transactionId = broadcastResult.get_str();

        // Set successful result with transaction ID
        UniValue result(UniValue::VOBJ);
        result.pushKV("txid", transactionId);
        set_result(result);
    } else {
        // Test mode: return transaction details without broadcasting  
        UniValue result(UniValue::VOBJ);
        result.pushKV("test", 1);
        result.pushKV("txid", tx_.GetHash().ToString());
        result.pushKV("hex", signedTransactionHex);
        set_result(result);
    }

    unlock_notes();
    return true;
}

/**
 * @brief Find and lock unspent shielded notes for use as transaction inputs
 *
 * Queries the wallet for unspent Sapling and Orchard notes belonging to
 * fromaddress_ that meet the minimum confirmation depth. In offline-key mode
 * unconfirmed notes are excluded. All discovered notes are immediately
 * wallet-locked (LockNote) so parallel async operations cannot select the
 * same inputs. Notes are sorted descending by value before returning so that
 * callers can greedily select the minimum-count set.
 *
 * @return true if at least one spendable note was found, false otherwise
 */
bool AsyncRPCOperation_sendmany::find_unspent_notes()
{
    std::vector<SaplingNoteEntry> saplingNoteEntries;
    std::vector<OrchardNoteEntry> orchardNoteEntries;
    
    // Retrieve filtered notes based on spending mode
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        
        if (hasOfflineSpendingKey) {
            pwalletMain->GetFilteredNotes(saplingNoteEntries, orchardNoteEntries,
                                          fromaddress_, mindepth_, true, false);
        } else {
            pwalletMain->GetFilteredNotes(saplingNoteEntries, orchardNoteEntries,
                                          fromaddress_, mindepth_, true, true);
        }
        // Lock immediately so no other async operation can select the same notes.
        for (const auto& e : saplingNoteEntries)
            pwalletMain->LockNote(e.op);
        for (const auto& e : orchardNoteEntries)
            pwalletMain->LockNote(e.op);
    }

    // Process Sapling note entries
    saplingInputs_.reserve(saplingNoteEntries.size());
    for (const auto& entry : saplingNoteEntries) {
        if (LogAcceptCategory("zrpcunsafe")) {
            LogPrint("zrpcunsafe", "%s: found unspent Sapling note (txid=%s, output=%d, amount=%s, memo=%s)\n",
                     getId(),
                     entry.op.hash.ToString().substr(0, 10),
                     entry.op.n,
                     FormatMoney(entry.note.value()),
                     HexStr(entry.memo).substr(0, 10));
        }
        saplingInputs_.push_back(entry);
    }

    // Process Orchard note entries
    orchardInputs_.reserve(orchardNoteEntries.size());
    for (const auto& entry : orchardNoteEntries) {
        if (LogAcceptCategory("zrpcunsafe")) {
            LogPrint("zrpcunsafe", "%s: found unspent Orchard note (txid=%s, output=%d, amount=%s, memo=%s)\n",
                     getId(),
                     entry.op.hash.ToString().substr(0, 10),
                     entry.op.n,
                     FormatMoney(entry.note.value()),
                     HexStr(entry.memo).substr(0, 10));
        }
        orchardInputs_.push_back(entry);
    }

    // Return false if no notes were found
    if (saplingInputs_.empty() && orchardInputs_.empty()) {
        return false;
    }

    // Sort notes in descending order by value (largest notes first)
    // This optimizes note selection by preferring fewer, larger notes
    std::sort(saplingInputs_.begin(), saplingInputs_.end(),
              [](const SaplingNoteEntry& note1, const SaplingNoteEntry& note2) -> bool {
                  return note1.note.value() > note2.note.value();
              });

    std::sort(orchardInputs_.begin(), orchardInputs_.end(),
              [](const OrchardNoteEntry& note1, const OrchardNoteEntry& note2) -> bool {
                  return note1.note.value() > note2.note.value();
              });

    return true;
}

/**
 * @brief Convert a hexadecimal string to a fixed-size memo byte array
 *
 * Parses the hex string into raw bytes, validates format and length, then
 * copies the result into a ZC_MEMO_SIZE array pre-filled with 0xF6
 * (the "no memo" sentinel defined in the Zcash protocol spec §5.5).
 *
 * @param hexString Hexadecimal string representation of the memo data
 * @return Fixed-size memo array padded with 0xF6
 * @throws std::runtime_error if the string is not valid hex or exceeds ZC_MEMO_SIZE bytes
 */
std::array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_sendmany::get_memo_from_hex_string(const std::string& hexString)
{
    // Initialize memo to default "no_memo" value (0xF6), per protocol spec section 5.5
    std::array<unsigned char, ZC_MEMO_SIZE> memoArray = {{0xF6}};

    // Parse hexadecimal string to raw bytes
    std::vector<unsigned char> rawMemoBytes = ParseHex(hexString.c_str());

    // Validate hex string format
    size_t hexStringLength = hexString.length();
    if (hexStringLength % 2 != 0 || (hexStringLength > 0 && rawMemoBytes.size() != hexStringLength / 2)) {
        throw std::runtime_error("Memo must be in hexadecimal format");
    }

    // Validate memo size doesn't exceed maximum
    if (rawMemoBytes.size() > ZC_MEMO_SIZE) {
        throw std::runtime_error(strprintf("Memo size of %d is too big, maximum allowed is %d",
                     rawMemoBytes.size(), ZC_MEMO_SIZE));
    }

    // Copy parsed bytes into the fixed-size memo array
    std::copy(rawMemoBytes.begin(), rawMemoBytes.end(), memoArray.begin());
    
    return memoArray;
}

/**
 * @brief Override getStatus() to include the RPC method name and parameters
 *
 * Appends "method": "z_sendmany" and "params": <contextinfo> to the base
 * status object so that callers can identify and audit in-flight operations.
 *
 * @return UniValue status object; base status is returned unchanged if
 *         no context info was provided at construction time
 */
UniValue AsyncRPCOperation_sendmany::getStatus() const
{
    UniValue baseStatus = AsyncRPCOperation::getStatus();
    
    // Return base status if no context info is available
    if (contextinfo_.isNull()) {
        return baseStatus;
    }

    // Add method name and parameters to the status object
    UniValue enhancedStatus = baseStatus.get_obj();
    enhancedStatus.pushKV("method", "z_sendmany");
    enhancedStatus.pushKV("params", contextinfo_);
    
    return enhancedStatus;
}
