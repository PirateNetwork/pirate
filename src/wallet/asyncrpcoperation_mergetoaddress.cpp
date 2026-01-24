// Copyright (c) 2017 The Zcash developers
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

/**
 * @file asyncrpcoperation_mergetoaddress.cpp
 * @brief Implementation of asynchronous merge-to-address operation
 * 
 * Implements z_mergetoaddress RPC operation for consolidating multiple
 * UTXOs and shielded notes into a single output address.
 * 
 * Purpose: Consolidate fragmented funds from multiple sources
 * Inputs: Transparent UTXOs, Sapling notes, Orchard notes
 * Outputs: Single consolidated output at destination address
 */

#include "asyncrpcoperation_mergetoaddress.h"

#include "amount.h"
#include "asyncrpcqueue.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "komodo_bitcoind.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "netbase.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/interpreter.h"
#include "sodium.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "wallet.h"
#include "walletdb.h"
#include "zcash/IncrementalMerkleTree.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "paymentdisclosuredb.h"

using namespace libzcash;



//==============================================================================
// CONSTRUCTOR AND INITIALIZATION
//==============================================================================

/**
 * @brief Constructor for merge-to-address operation
 * 
 * Initializes merge operation with multi-protocol input support and resource locking.
 * 
 * @param consensusParams Network consensus parameters
 * @param nHeight Current blockchain height
 * @param contextualTx Base transaction context
 * @param utxoInputs Transparent UTXO inputs to merge
 * @param saplingNoteInputs Sapling note inputs to merge
 * @param orchardNoteInputs Orchard note inputs to merge
 * @param recipient Destination address and memo
 * @param fee Transaction fee amount
 * @param contextInfo Context information for logging
 * @throws JSONRPCError for invalid parameters or addresses
 */
AsyncRPCOperation_mergetoaddress::AsyncRPCOperation_mergetoaddress(
    const Consensus::Params& consensusParams,
    const int nHeight,
    CMutableTransaction contextualTx,
    std::vector<MergeToAddressInputUTXO> utxoInputs,
    std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs,
    std::vector<MergeToAddressInputOrchardNote> orchardNoteInputs,
    MergeToAddressRecipient recipient,
    CAmount fee,
    UniValue contextInfo) : tx_(contextualTx), 
                            utxoInputs_(utxoInputs),
                            saplingNoteInputs_(saplingNoteInputs), 
                            orchardNoteInputs_(orchardNoteInputs), 
                            recipient_(recipient), 
                            fee_(fee), 
                            contextinfo_(contextInfo),
                            builder_(TransactionBuilder(consensusParams, nHeight, pwalletMain))
{
    // =================================================================
    // PARAMETER VALIDATION
    // =================================================================
    
    if (fee < 0 || fee > MAX_MONEY) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee is out of range");
    }

    if (utxoInputs.empty() && saplingNoteInputs.empty() && orchardNoteInputs.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No inputs");
    }

    if (std::get<0>(recipient).size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Recipient parameter missing");
    }

    // =================================================================
    // RECIPIENT ADDRESS TYPE DETECTION AND VALIDATION
    // =================================================================
    
    // Initialize address type flags
    isToTaddr_ = false;
    isToZaddr_ = false;
    
    // Try to decode as transparent address first
    toTaddr_ = DecodeDestination(std::get<0>(recipient));
    isToTaddr_ = IsValidDestination(toTaddr_);

    // If not a transparent address, try shielded address
    if (!isToTaddr_) {
        auto address = DecodePaymentAddress(std::get<0>(recipient));
        if (IsValidPaymentAddress(address)) {
            isToZaddr_ = true;
            toPaymentAddress_ = address;
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
        }
    }
    
    // Ensure exactly one address type is set (transparent XOR shielded)
    assert(isToTaddr_ != isToZaddr_);
    assert(isToTaddr_ || isToZaddr_);

    // =================================================================
    // LOGGING AND RESOURCE MANAGEMENT
    // =================================================================
    
    // Log operation initialization with appropriate detail level
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_mergetoaddress initialized (params=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_mergetoaddress initialized\n", getId());
    }

    // Lock input resources to prevent concurrent operations from using them
    lock_utxos();
    lock_sapling_notes();
    lock_orchard_notes();
}

//==============================================================================
// DESTRUCTOR AND RESOURCE CLEANUP
//==============================================================================

/**
 * @brief Destructor with automatic resource cleanup
 * 
 * Resources are unlocked explicitly in main() for immediate availability
 * rather than in destructor following RAII principles.
 */
AsyncRPCOperation_mergetoaddress::~AsyncRPCOperation_mergetoaddress()
{
    // No explicit cleanup required - resources unlocked in main()
}

//==============================================================================
// MAIN EXECUTION FRAMEWORK
//==============================================================================

/**
 * @brief Main execution entry point for merge-to-address operation
 * 
 * Handles complete operation lifecycle including state management,
 * mining control, error handling, and resource cleanup.
 */
void AsyncRPCOperation_mergetoaddress::main()
{
    // Early exit if operation was cancelled
    if (isCancelled()) {
        unlock_utxos();
        unlock_sapling_notes();
        unlock_orchard_notes();
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

    // Temporarily disable mining during transaction building
#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
#else
    GenerateBitcoins(false, 0);
#endif
#endif

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

    // Re-enable mining if it was previously enabled
#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
#else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1));
#endif
#endif

    stop_execution_clock();

    // Update operation state based on success/failure
    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    // Log completion status with transaction ID or error details
    std::string logMessage = strprintf("%s: z_mergetoaddress finished (status=%s", getId(), getStateAsString());
    if (success) {
        logMessage += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        logMessage += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s", logMessage);

    // Clean up locked resources
    unlock_utxos();
    unlock_sapling_notes();
    unlock_orchard_notes();
}

// Known issues (TODO items):
// 1. #1277 Spendable notes are not locked, so an operation running in parallel could also try to use them.

/**
 * @brief Core implementation of multi-protocol merge operation
 * 
 * Consolidates inputs from transparent, Sapling, and Orchard protocols
 * into a single output destination with proper cryptographic handling.
 * 
 * @return true if transaction built and broadcast successfully
 * @throws JSONRPCError for validation failures or transaction building errors
 */
bool AsyncRPCOperation_mergetoaddress::main_impl()
{
    // Ensure exactly one output type (transparent XOR shielded) 
    assert(isToTaddr_ != isToZaddr_);

    // Determine if this is a pure transparent-only transaction (affects logging sensitivity)
    bool isPureTaddrOnlyTx = (saplingNoteInputs_.empty() && orchardNoteInputs_.empty() && isToTaddr_);
    CAmount minersFee = fee_;

    // =================================================================
    // STEP 1: Validate transaction input limits
    // =================================================================
    
    size_t numInputs = utxoInputs_.size();

    // Check mempooltxinputlimit to avoid creating a transaction the local mempool rejects
    size_t inputLimit = (size_t)GetArg("-mempooltxinputlimit", 0);
    {
        LOCK(cs_main);
        if (NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
            inputLimit = 0; // No limit after Overwinter upgrade
        }
    }
    if (inputLimit > 0 && numInputs > inputLimit) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           strprintf("Number of transparent inputs %d is greater than mempooltxinputlimit of %d",
                                     numInputs, inputLimit));
    }

    // =================================================================
    // STEP 2: Calculate input totals from all sources
    // =================================================================
    
    // Calculate total value from transparent inputs
    CAmount totalTransparentInputs = 0;
    for (const MergeToAddressInputUTXO& utxo : utxoInputs_) {
        totalTransparentInputs += std::get<1>(utxo);
    }

    // Calculate total value from shielded inputs (Sapling + Orchard)
    CAmount totalShieldedInputs = 0;

    for (const MergeToAddressInputSaplingNote& saplingNote : saplingNoteInputs_) {
        totalShieldedInputs += std::get<2>(saplingNote);
    }

    for (const MergeToAddressInputOrchardNote& orchardNote : orchardNoteInputs_) {
        totalShieldedInputs += std::get<2>(orchardNote);
    }

    // Calculate total input value and validate against fees
    CAmount totalInputAmount = totalShieldedInputs + totalTransparentInputs;

    // Validate that we have positive amounts
    if (totalInputAmount <= 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Total input amount must be positive");
    }
    
    if (minersFee < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Miners fee cannot be negative");
    }

    if (totalInputAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient funds, have %s and miners fee is %s",
                                     FormatMoney(totalInputAmount), FormatMoney(minersFee)));
    }

    CAmount sendAmount = totalInputAmount - minersFee;

    // =================================================================
    // STEP 3: Log transaction composition for debugging
    // =================================================================
    
    LogPrint(isPureTaddrOnlyTx ? "zrpc" : "zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
             getId(), FormatMoney(totalInputAmount), FormatMoney(sendAmount), FormatMoney(minersFee));
    LogPrint("zrpc", "%s: transparent input: %s\n", getId(), FormatMoney(totalTransparentInputs));
    LogPrint("zrpcunsafe", "%s: private input: %s\n", getId(), FormatMoney(totalShieldedInputs));
    if (isToTaddr_) {
        LogPrint("zrpc", "%s: transparent output: %s\n", getId(), FormatMoney(sendAmount));
    } else {
        LogPrint("zrpcunsafe", "%s: private output: %s\n", getId(), FormatMoney(sendAmount));
    }
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    // =================================================================
    // STEP 4: Configure transaction builder and consensus parameters
    // =================================================================
    
    // Grab the current consensus branch ID for the transaction
    {
        LOCK(cs_main);
        consensusBranchId_ = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    }

    /**
     * Configure the transaction builder with the calculated miner's fee.
     * This is based on code from AsyncRPCOperation_sendmany::main_impl() and should be refactored.
     */
    builder_.SetFee(minersFee);

    // =================================================================
    // STEP 5: Add transparent inputs (UTXOs) to transaction builder
    // =================================================================
    
    for (const MergeToAddressInputUTXO& transparentInput : utxoInputs_) {
        COutPoint outPoint = std::get<0>(transparentInput);
        CAmount amount = std::get<1>(transparentInput);
        CScript scriptPubKey = std::get<2>(transparentInput);
        
        builder_.AddTransparentInput(outPoint, scriptPubKey, amount);
    }

    // =================================================================
    // STEP 6: Process Sapling shielded inputs
    // =================================================================
    
    std::optional<uint256> outgoingViewingKey;
    
    // Collect Sapling note information for batch processing
    std::vector<SaplingOutPoint> saplingOutPoints;
    std::vector<SaplingNote> saplingNotes;
    std::vector<SaplingExtendedSpendingKey> saplingExtendedKeys;
    std::set<SaplingExtendedSpendingKey> uniqueExtendedKeys;
    
    for (const MergeToAddressInputSaplingNote& saplingNoteInput : saplingNoteInputs_) {
        saplingOutPoints.push_back(std::get<0>(saplingNoteInput));
        saplingNotes.push_back(std::get<1>(saplingNoteInput));
        auto extendedSpendingKey = std::get<3>(saplingNoteInput);
        saplingExtendedKeys.push_back(extendedSpendingKey);
        uniqueExtendedKeys.insert(extendedSpendingKey);
        
        // Set outgoing viewing key from first available extended spending key
        if (!outgoingViewingKey) {
            outgoingViewingKey = extendedSpendingKey.expsk.full_viewing_key().ovk;
        }
    }

    // Add Sapling notes to the transaction builder
    // Iterate through all the selected notes and add them to the transaction
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        
        for (std::set<SaplingExtendedSpendingKey>::iterator keyIterator = uniqueExtendedKeys.begin(); 
             keyIterator != uniqueExtendedKeys.end(); keyIterator++) {
            
            auto currentExtendedKey = *keyIterator;

            // Process each note that uses this extended spending key
            for (int noteIndex = 0; noteIndex < saplingExtendedKeys.size(); noteIndex++) {
                if (currentExtendedKey == saplingExtendedKeys[noteIndex]) {
                    
                    // Get the Merkle path for this note
                    libzcash::MerklePath saplingMerklePath;
                    if (!pwalletMain->SaplingWalletGetMerklePathOfNote(saplingOutPoints[noteIndex].hash, 
                                                                       saplingOutPoints[noteIndex].n, 
                                                                       saplingMerklePath)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Merkle Path not found for Sapling note. Stopping.\n", getId()));
                    }

                    // Get the anchor for this note
                    uint256 anchor;
                    if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingMerklePath, 
                                                                      saplingNotes[noteIndex].cmu().value(), 
                                                                      anchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Getting Anchor failed. Stopping.\n", getId()));
                    }

                    // Create recipient address from note data
                    libzcash::SaplingPaymentAddress recipient(saplingNotes[noteIndex].d, 
                                                              saplingNotes[noteIndex].pk_d);
                    
                    // Add the raw Sapling spend to the builder
                    if (!builder_.AddSaplingSpendRaw(saplingOutPoints[noteIndex], 
                                                     recipient, 
                                                     saplingNotes[noteIndex].value(), 
                                                     saplingNotes[noteIndex].rcm(), 
                                                     saplingMerklePath, 
                                                     anchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Adding Raw Sapling Spend failed. Stopping.\n", getId()));
                    }
                }
            }
            
            // Convert the raw Sapling spend using the extended spending key (once per key)
            if (!builder_.ConvertRawSaplingSpend(currentExtendedKey)) {
                throw JSONRPCError(RPC_WALLET_ERROR, 
                                   strprintf("%s: Converting Raw Sapling Spends failed.\n", getId()));
            }
        }
    }

    // =================================================================
    // STEP 7: Process Orchard shielded inputs
    // =================================================================
    
    // Collect Orchard note information for batch processing
    std::vector<OrchardOutPoint> orchardOutPoints;
    std::vector<OrchardNote> orchardNotes;
    std::vector<OrchardExtendedSpendingKeyPirate> orchardExtendedKeys;
    std::set<OrchardExtendedSpendingKeyPirate> uniqueOrchardKeys;
    
    for (const MergeToAddressInputOrchardNote& orchardNoteInput : orchardNoteInputs_) {
        orchardOutPoints.push_back(std::get<0>(orchardNoteInput));
        orchardNotes.push_back(std::get<1>(orchardNoteInput));
        auto extendedSpendingKey = std::get<3>(orchardNoteInput);
        orchardExtendedKeys.push_back(extendedSpendingKey);
        uniqueOrchardKeys.insert(extendedSpendingKey);
        
        // Set outgoing viewing key from first available Orchard extended spending key
        if (!outgoingViewingKey) {
            auto fullViewingKeyOpt = extendedSpendingKey.GetXFVK();
            if (fullViewingKeyOpt == std::nullopt) {
                throw JSONRPCError(RPC_WALLET_ERROR,
                                   strprintf("%s: FVK not found for Orchard spending key. Stopping.\n", getId()));
            }

            auto fullViewingKey = fullViewingKeyOpt.value().fvk;
            auto outgoingViewingKeyOpt = fullViewingKey.GetOVK();
            if (outgoingViewingKeyOpt == std::nullopt) {
                throw JSONRPCError(RPC_WALLET_ERROR,
                                   strprintf("%s: OVK not found for Orchard spending key. Stopping.\n", getId()));
            }

            outgoingViewingKey = outgoingViewingKeyOpt.value().ovk;
        }
    }
    
    // Add Orchard notes to the transaction builder
    // Iterate through all the selected notes and add them to the transaction
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        bool orchardInitialized = false;
        uint256 orchardAnchor;

        for (std::set<OrchardExtendedSpendingKeyPirate>::iterator keyIterator = uniqueOrchardKeys.begin(); 
             keyIterator != uniqueOrchardKeys.end(); keyIterator++) {
            
            auto currentExtendedKey = *keyIterator;

            // Process each note that uses this extended spending key
            for (int noteIndex = 0; noteIndex < orchardExtendedKeys.size(); noteIndex++) {
                if (currentExtendedKey == orchardExtendedKeys[noteIndex]) {
                    
                    // Get the Merkle path for this Orchard note
                    libzcash::MerklePath orchardMerklePath;
                    if (!pwalletMain->OrchardWalletGetMerklePathOfNote(orchardOutPoints[noteIndex].hash, 
                                                                       orchardOutPoints[noteIndex].n, 
                                                                       orchardMerklePath)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Merkle Path not found for Orchard note. Stopping.\n", getId()));
                    }

                    // Get the anchor for this Orchard note
                    uint256 pathAnchor;
                    if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardMerklePath, 
                                                                      orchardNotes[noteIndex].cmx(), 
                                                                      pathAnchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Getting Anchor failed. Stopping.\n", getId()));
                    }

                    // Set orchard anchor for transaction (use first anchor found)
                    if (orchardAnchor.IsNull()) {
                        orchardAnchor = pathAnchor;
                    }

                    // Initialize Orchard builder only once when we have the first anchor
                    if (!orchardInitialized) {
                        builder_.InitializeOrchard(true, true, orchardAnchor);
                        orchardInitialized = true;
                    }

                    // Add the raw Orchard spend to the builder
                    if (!builder_.AddOrchardSpendRaw(orchardOutPoints[noteIndex], 
                                                     orchardNotes[noteIndex].address, 
                                                     orchardNotes[noteIndex].value(), 
                                                     orchardNotes[noteIndex].rho(), 
                                                     orchardNotes[noteIndex].rseed(), 
                                                     orchardMerklePath, 
                                                     orchardAnchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, 
                                           strprintf("%s: Adding Raw Orchard Spend failed. Stopping.\n", getId()));
                    }
                }
            }
            
            // Convert the raw Orchard spend using the extended spending key (once per key)
            if (!builder_.ConvertRawOrchardSpend(currentExtendedKey)) {
                throw JSONRPCError(RPC_WALLET_ERROR, 
                                   strprintf("%s: Converting Raw Orchard Spends failed.\n", getId()));
            }
        }
        
        // If we have any Orchard payment addresses, ensure Orchard is initialized
        // This is necessary to handle the case where we are sending to an Orchard address
        if (std::get_if<libzcash::OrchardPaymentAddressPirate>(&toPaymentAddress_) != nullptr) {
            if (!orchardInitialized) {
                builder_.InitializeOrchard(false, true, uint256());
            }
        }
    }

    // =================================================================
    // STEP 8: Add output to transaction (transparent or shielded)
    // =================================================================

    if (isToTaddr_) {
        // Add transparent output
        if (!builder_.AddTransparentOutput(toTaddr_, sendAmount)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                               "Invalid output address, not a valid taddr.");
        }
    } else {
        // Add shielded output with memo
        std::string destinationAddress = std::get<0>(recipient_);
        std::string memoString = std::get<1>(recipient_);

        // Note: transaction builder expects memo in ASCII encoding, not as a hex string.
        std::array<unsigned char, ZC_MEMO_SIZE> memoArray = {0x00};
        
        if (IsHex(memoString)) {
            if (memoString.length() > (ZC_MEMO_SIZE * 2)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                                   strprintf("Invalid parameter, size of hex encoded memo is larger than maximum allowed %d", 
                                             (ZC_MEMO_SIZE * 2)));
            }
            memoArray = get_memo_from_hex_string(memoString);
        } else {
            if (memoString.length() > ZC_MEMO_SIZE) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, 
                                   strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", 
                                             ZC_MEMO_SIZE));
            }

            int memoLength = memoString.length();
            unsigned char currentByte;
            for (int charIndex = 0; charIndex < memoLength; charIndex++) {
                currentByte = (unsigned char)memoString[charIndex];
                memoArray[charIndex] = currentByte;
            }
        }

        // Determine the payment address type (Sapling or Orchard)
        auto saplingPaymentAddress = std::get_if<libzcash::SaplingPaymentAddress>(&toPaymentAddress_);
        auto orchardPaymentAddress = std::get_if<libzcash::OrchardPaymentAddressPirate>(&toPaymentAddress_);

        if (saplingPaymentAddress == nullptr && orchardPaymentAddress == nullptr) {
            // This should never happen as we have already determined that the payment is to a shielded address
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                               "Could not get Sapling or Orchard payment address.");
        }

        // Generate outgoing viewing key for transparent-to-shielded transactions
        if (saplingNoteInputs_.size() == 0 && orchardNoteInputs_.size() == 0 && utxoInputs_.size() > 0) {
            // Sending from t-addresses, which we don't have ovks for. Instead,
            // generate a common one from the HD seed. This ensures the data is
            // recoverable, while keeping it logically separate from the ZIP 32
            // Sapling key hierarchy, which the user might not be using.
            HDSeed seed;
            if (!pwalletMain->GetHDSeed(seed)) {
                throw JSONRPCError(RPC_WALLET_ERROR,
                                   "AsyncRPCOperation_mergetoaddress: HD seed not found");
            }
            outgoingViewingKey = ovkForShieldingFromTaddr(seed);
        }
        
        // Validate that we have an outgoing viewing key for shielded outputs
        if (!outgoingViewingKey) {
            throw JSONRPCError(RPC_WALLET_ERROR, 
                               "Sending to a Sapling or Orchard address requires an ovk.");
        }

        // Add the appropriate shielded output based on address type
        auto memo = libzcash::Memo::FromBytes(memoArray);
        if (saplingPaymentAddress != nullptr) {
            builder_.AddSaplingOutputRaw(*saplingPaymentAddress, sendAmount, memo);
            builder_.ConvertRawSaplingOutput(outgoingViewingKey.value());
        } else if (orchardPaymentAddress != nullptr) {
            builder_.AddOrchardOutputRaw(*orchardPaymentAddress, sendAmount, memo);
            builder_.ConvertRawOrchardOutput(outgoingViewingKey.value());
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, 
                               "Invalid output address, not a valid zaddr.");
        }
    }

    // =================================================================
    // STEP 9: Build and send the transaction
    // =================================================================

    // Build the transaction using the configured builder
    tx_ = builder_.Build().GetTxOrThrow();

    // Send the transaction to the network (or test mode)
    // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
    auto signedTransactionHex = EncodeHexTx(tx_);
    
    if (!testmode) {
        // Production mode: send transaction to network
        UniValue rpcParams = UniValue(UniValue::VARR);
        rpcParams.push_back(signedTransactionHex);
        UniValue sendResult = sendrawtransaction(rpcParams, false, CPubKey());
        
        if (sendResult.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, 
                               "sendrawtransaction did not return an error or a txid.");
        }

        auto transactionId = sendResult.get_str();

        UniValue resultObject(UniValue::VOBJ);
        resultObject.push_back(Pair("txid", transactionId));
        set_result(resultObject);
    } else {
        // Test mode: do not send transaction to network, return transaction details
        UniValue resultObject(UniValue::VOBJ);
        resultObject.push_back(Pair("test", 1));
        resultObject.push_back(Pair("txid", tx_.GetHash().ToString()));
        resultObject.push_back(Pair("hex", signedTransactionHex));
        set_result(resultObject);
    }

    return true;
}


//==============================================================================
// UTILITY FUNCTIONS FOR MEMO AND STATUS HANDLING
//==============================================================================

/**
 * @brief Convert hexadecimal string to fixed-size memo array
 * 
 * Converts hex string representation to memo array with validation
 * and zero-padding for shielded transaction protocols.
 * 
 * @param hexString Hexadecimal string representation of memo
 * @return Fixed-size memo array (ZC_MEMO_SIZE bytes)
 * @throws JSONRPCError for invalid hex format or oversized input
 */
std::array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_mergetoaddress::get_memo_from_hex_string(std::string hexString)
{
    std::array<unsigned char, ZC_MEMO_SIZE> memoArray = {{0x00}};

    std::vector<unsigned char> rawMemoData = ParseHex(hexString.c_str());

    // Validate hex string format
    // If ParseHex comes across a non-hex char, it will stop but still return results so far.
    size_t stringLength = hexString.length();
    if (stringLength % 2 != 0 || (stringLength > 0 && rawMemoData.size() != stringLength / 2)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
                           "Memo must be in hexadecimal format");
    }

    // Validate memo size
    if (rawMemoData.size() > ZC_MEMO_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
                           strprintf("Memo size of %d is too big, maximum allowed is %d", 
                                     rawMemoData.size(), ZC_MEMO_SIZE));
    }

    // Copy vector data into memo array
    int memoLength = rawMemoData.size();
    for (int byteIndex = 0; byteIndex < ZC_MEMO_SIZE && byteIndex < memoLength; byteIndex++) {
        memoArray[byteIndex] = rawMemoData[byteIndex];
    }
    return memoArray;
}

/**
 * @brief Get current operation status with merge-specific information
 * 
 * @return UniValue object containing operation status, method name, and parameters if available
 */
UniValue AsyncRPCOperation_mergetoaddress::getStatus() const
{
    UniValue baseStatus = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return baseStatus;
    }

    UniValue statusObject = baseStatus.get_obj();
    statusObject.push_back(Pair("method", "z_mergetoaddress"));
    statusObject.push_back(Pair("params", contextinfo_));
    return statusObject;
}

//==============================================================================
// RESOURCE MANAGEMENT - INPUT LOCKING AND UNLOCKING
//==============================================================================

/**
 * @brief Lock transparent UTXOs to prevent double-spending during merge operation
 * 
 * Prevents concurrent operations from using the same UTXOs by locking them
 * in the wallet until the merge operation completes.
 */
void AsyncRPCOperation_mergetoaddress::lock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto transparentInput : utxoInputs_) {
        pwalletMain->LockCoin(std::get<0>(transparentInput));
    }
}

/**
 * @brief Unlock transparent UTXOs to restore availability for other operations
 * 
 * Releases locks on previously locked UTXOs, making them available for other
 * wallet operations. Should be called after operation completion.
 */
void AsyncRPCOperation_mergetoaddress::unlock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto transparentInput : utxoInputs_) {
        pwalletMain->UnlockCoin(std::get<0>(transparentInput));
    }
}

/**
 * @brief Lock Sapling notes to prevent double-spending during merge operation
 * 
 * Prevents concurrent operations from using the same Sapling notes by
 * locking them in the wallet until the merge operation completes.
 */
void AsyncRPCOperation_mergetoaddress::lock_sapling_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto saplingNote : saplingNoteInputs_) {
        pwalletMain->LockNote(std::get<0>(saplingNote));
    }
}

/**
 * @brief Unlock Sapling notes to restore availability for other operations
 * 
 * Releases locks on previously locked Sapling notes, making them available
 * for other wallet operations. Should be called after operation completion.
 */
void AsyncRPCOperation_mergetoaddress::unlock_sapling_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto saplingNote : saplingNoteInputs_) {
        pwalletMain->UnlockNote(std::get<0>(saplingNote));
    }
}

/**
 * @brief Lock Orchard notes to prevent double-spending during merge operation
 * 
 * Prevents concurrent operations from using the same Orchard notes by
 * locking them in the wallet until the merge operation completes.
 */
void AsyncRPCOperation_mergetoaddress::lock_orchard_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto orchardNote : orchardNoteInputs_) {
        pwalletMain->LockNote(std::get<0>(orchardNote));
    }
}

/**
 * @brief Unlock Orchard notes to restore availability for other operations
 * 
 * Releases locks on previously locked Orchard notes, making them available
 * for other wallet operations. Should be called after operation completion.
 */
void AsyncRPCOperation_mergetoaddress::unlock_orchard_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto orchardNote : orchardNoteInputs_) {
        pwalletMain->UnlockNote(std::get<0>(orchardNote));
    }
}