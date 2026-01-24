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
#include "asyncrpcqueue.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "komodo_bitcoind.h"
#include "komodo_notary.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "netbase.h"
#include "rpc/protocol.h"
#include "rpc/rawtransaction.h"
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

#include <stdint.h>

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "paymentdisclosuredb.h"

using namespace libzcash;

/**
 * @brief Find output index in JoinSplit outputmap
 * 
 * @param obj UniValue object containing the outputmap
 * @param n The output number to find
 * @return int The index in the outputmap where the output number is found
 * @throws JSONRPCError if outputmap is missing
 * @throws std::logic_error if n is not present in outputmap
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
 * @param fromAddress Source address (transparent, Sapling, or Orchard)
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
      saplingOutputs_(saplingOutputs), 
      orchardOutputs_(orchardOutputs), 
      mindepth_(minimumConfirmationDepth), 
      fee_(transactionFee), 
      contextinfo_(contextInfo),
      builder_(TransactionBuilder(consensusParams, blockHeight, pwalletMain))
{
    // =================================================================
    // PARAMETER VALIDATION
    // =================================================================
    
    assert(fee_ >= 0);

    if (minimumConfirmationDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minconf cannot be negative");
    }

    if (fromAddress.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "From address parameter missing");
    }

    if (saplingOutputs.empty() && orchardOutputs.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No recipients");
    }

    // =================================================================
    // ADDRESS TYPE DETECTION AND INITIALIZATION
    // =================================================================

    // Initialize address type flags with descriptive names
    fromtaddr_ = DecodeDestination(fromAddress);
    isFromTransparentAddress_ = IsValidDestination(fromtaddr_);
    isFromSaplingAddress_ = false;
    isFromOrchardAddress_ = false;
    isFromPrivateAddress_ = false;

    // Initialize offline spending key flag
    hasOfflineSpendingKey = false;
    
    // =================================================================
    // SHIELDED ADDRESS PROCESSING
    // =================================================================
    // Process shielded addresses (not transparent)
    if (!isFromTransparentAddress_) {
        // Store the original address string for later use
        fromAddress_ = fromAddress;
        
        auto decodedAddress = DecodePaymentAddress(fromAddress);
        if (IsValidPaymentAddress(decodedAddress)) {
            
            // Check if this is a Sapling payment address
            auto saplingPaymentAddress = std::get_if<libzcash::SaplingPaymentAddress>(&decodedAddress);
            if (saplingPaymentAddress != nullptr) {
                isFromSaplingAddress_ = true;
                isFromPrivateAddress_ = true;
            }

            // Check if this is an Orchard payment address
            auto orchardPaymentAddress = std::get_if<libzcash::OrchardPaymentAddressPirate>(&decodedAddress);
            if (orchardPaymentAddress != nullptr) {
                isFromOrchardAddress_ = true;
                isFromPrivateAddress_ = true;
            }

            // Ensure we have a valid shielded address type
            if (!isFromSaplingAddress_ && !isFromOrchardAddress_) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address");
            }

            frompaymentaddress_ = decodedAddress;
            
            // =================================================================
            // SPENDING KEY VERIFICATION
            // =================================================================
            
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
    }

    // =================================================================
    // FINAL VALIDATION AND LOGGING
    // =================================================================
    
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

    // Enable payment disclosure if experimental mode is active
    paymentDisclosureMode = fExperimentalMode && GetBoolArg("-paymentdisclosure", true);
}

/**
 * @brief Destructor for AsyncRPCOperation_sendmany
 * 
 * Currently performs no cleanup as resources are managed automatically.
 */
AsyncRPCOperation_sendmany::~AsyncRPCOperation_sendmany()
{
    // No explicit cleanup required - all resources are RAII managed
}

/**
 * @brief Main execution wrapper for the sendmany operation
 * 
 * This function handles the overall operation lifecycle including:
 * - State management
 * - Mining control during execution  
 * - Exception handling and error reporting
 * - Payment disclosure processing
 * - Execution timing
 */
void AsyncRPCOperation_sendmany::main()
{
    // Early exit if operation was cancelled
    if (isCancelled()) {
        return;
    }

    // =================================================================
    // EXECUTION SETUP
    // =================================================================
    
    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool operationSuccessful = false;

    // Temporarily disable mining during transaction creation to avoid conflicts
#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
#else
    GenerateBitcoins(false, 0);
#endif
#endif

    // =================================================================
    // MAIN OPERATION EXECUTION WITH EXCEPTION HANDLING
    // =================================================================

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
        set_error_message("runtime error: " + std::string(e.what()));
    } catch (const std::logic_error& e) {
        // Handle logic errors (programming errors, assertions, etc.)
        set_error_code(-1);
        set_error_message("logic error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        // Handle all other standard exceptions
        set_error_code(-1);
        set_error_message("general exception: " + std::string(e.what()));
    } catch (...) {
        // Handle any non-standard exceptions
        set_error_code(-2);
        set_error_message("unknown error");
    }

    // =================================================================
    // EXECUTION CLEANUP AND STATE MANAGEMENT
    // =================================================================
    
    // Re-enable mining if it was previously enabled
#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
#else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1));
#endif
#endif

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

    // =================================================================
    // PAYMENT DISCLOSURE PROCESSING
    // =================================================================
    
    if (operationSuccessful && paymentDisclosureMode && !paymentDisclosureData_.empty()) {
        uint256 transactionHash = tx_.GetHash();
        std::shared_ptr<PaymentDisclosureDB> paymentDisclosureDB = PaymentDisclosureDB::sharedInstance();
        
        for (PaymentDisclosureKeyInfo& disclosureInfo : paymentDisclosureData_) {
            disclosureInfo.first.hash = transactionHash;
            
            if (!paymentDisclosureDB->Put(disclosureInfo.first, disclosureInfo.second)) {
                LogPrint("paymentdisclosure", "%s: Payment Disclosure: Error writing entry to database for key %s\n", 
                        getId(), disclosureInfo.first.ToString());
            } else {
                LogPrint("paymentdisclosure", "%s: Payment Disclosure: Successfully added entry to database for key %s\n", 
                        getId(), disclosureInfo.first.ToString());
            }
        }
    }
}

/**
 * @brief Main implementation of the sendmany operation
 * 
 * This function performs the core logic for sending funds from various address types
 * (transparent, Sapling shielded, Orchard shielded) to multiple recipients.
 * 
 * Known issues (TODO items):
 * 1. #1159 Currently there is no limit set on the number of joinsplits, so size of tx could be invalid.
 * 2. #1360 Note selection is not optimal
 * 3. #1277 Spendable notes are not locked, so an operation running in parallel could also try to use them
 * 
 * @return true if the transaction was successfully created and sent, false otherwise
 * @throws JSONRPCError for various error conditions (insufficient funds, wallet errors, etc.)
 */
bool AsyncRPCOperation_sendmany::main_impl()
{
    // Ensure we have exactly one type of source address (transparent XOR private)
    assert(isFromTransparentAddress_ != isFromPrivateAddress_);
    CAmount minersFee = fee_;

    // =================================================================
    // STEP 1: Find and validate input sources (UTXOs or notes)
    // =================================================================

    // Check if the from address is a taddr, and if so, find UTXOs to spend
    if (isFromTransparentAddress_) {
        bool foundUtxos = find_utxos(true);
        if (!foundUtxos) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds, no UTXOs found for taddr from address.");
        }
    }

    // Check if the from address is a shielded address, and if so, find unspent notes
    if ((isFromSaplingAddress_ || isFromOrchardAddress_) && !find_unspent_notes()) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds, no unspent notes found for from address.");
    }

    // =================================================================
    // STEP 2: Calculate total input and output amounts
    // =================================================================
    
    // Calculate total transparent inputs
    CAmount totalTransparentInputs = 0;
    for (const SendManyInputUTXO& utxo : transparentInputs_) {
        totalTransparentInputs += std::get<2>(utxo);
    }

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

    // =================================================================
    // STEP 3: Validate input/output consistency and sufficiency
    // =================================================================
    
    // Ensure input sources are mutually exclusive
    assert(!isFromTransparentAddress_ || totalSaplingInputs + totalOrchardInputs == 0);
    assert((!isFromSaplingAddress_ && !isFromOrchardAddress_) || totalTransparentInputs == 0);

    // Check if we have sufficient funds for each address type
    if (isFromTransparentAddress_ && (totalTransparentInputs < totalTargetAmount)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient transparent funds, have %s, need %s",
                                     FormatMoney(totalTransparentInputs), FormatMoney(totalTargetAmount)));
    }

    if (isFromSaplingAddress_ && (totalSaplingInputs < totalTargetAmount)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient shielded funds, have %s, need %s",
                                     FormatMoney(totalSaplingInputs), FormatMoney(totalTargetAmount)));
    }

    if (isFromOrchardAddress_ && (totalOrchardInputs < totalTargetAmount)) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient shielded funds, have %s, need %s",
                                     FormatMoney(totalOrchardInputs), FormatMoney(totalTargetAmount)));
    }

    // =================================================================
    // STEP 4: Handle transparent input selection and change logic
    // =================================================================

    // If from address is a taddr, select UTXOs to spend
    CAmount selectedUtxoAmount = 0;
    bool hasSelectedCoinbaseUtxo = false;
    if (isFromTransparentAddress_) {
        // Get dust threshold for change calculation
        CKey temporaryKey;
        temporaryKey.MakeNewKey(true);
        CScript temporaryScriptPubKey = GetScriptForDestination(temporaryKey.GetPubKey().GetID());
        CTxOut temporaryOutput(CAmount(1), temporaryScriptPubKey);
        CAmount dustThreshold = temporaryOutput.GetDustThreshold(minRelayTxFee);
        CAmount changeAmount = -1;

        std::vector<SendManyInputUTXO> selectedTransparentInputs;
        for (SendManyInputUTXO& utxo : transparentInputs_) {
            bool isCoinbaseUtxo = std::get<3>(utxo);
            if (isCoinbaseUtxo) {
                hasSelectedCoinbaseUtxo = true;
            }
            selectedUtxoAmount += std::get<2>(utxo);
            selectedTransparentInputs.push_back(utxo);
            if (selectedUtxoAmount >= totalTargetAmount) {
                // Select another utxo if there is change less than the dust threshold.
                changeAmount = selectedUtxoAmount - totalTargetAmount;
                if (changeAmount == 0 || changeAmount >= dustThreshold) {
                    break;
                }
            }
        }

        // Check if we have sufficient funds
        if (selectedUtxoAmount < totalTargetAmount) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                               strprintf("Insufficient transparent funds, have %s, need %s",
                                         FormatMoney(selectedUtxoAmount), FormatMoney(totalTargetAmount)));
        }

        // If there is transparent change, is it valid or is it dust?
        if (changeAmount < dustThreshold && changeAmount != 0) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                               strprintf("Insufficient transparent funds, have %s, need %s more to avoid creating invalid change output %s (dust threshold is %s)",
                                         FormatMoney(totalTransparentInputs), FormatMoney(dustThreshold - changeAmount), FormatMoney(changeAmount), FormatMoney(dustThreshold)));
        }

        transparentInputs_ = selectedTransparentInputs;
        totalTransparentInputs = selectedUtxoAmount;

        // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
        size_t inputLimit = (size_t)GetArg("-mempooltxinputlimit", 0);
        {
            LOCK(cs_main);
            if (NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
                inputLimit = 0;
            }
        }
        if (inputLimit > 0) {
            size_t numberOfInputs = transparentInputs_.size();
            if (numberOfInputs > inputLimit) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Too many transparent inputs %zu > limit %zu", numberOfInputs, inputLimit));
            }
        }

        // Add the selected transparent inputs to the transaction builder
        CScript inputScriptPubKey;
        for (const auto& utxo : transparentInputs_) {
            inputScriptPubKey = GetScriptForDestination(std::get<4>(utxo));
            uint256 transactionId = std::get<0>(utxo);
            int outputIndex = std::get<1>(utxo);
            CAmount utxoAmount = std::get<2>(utxo);
            builder_.AddTransparentInput(COutPoint(transactionId, outputIndex), inputScriptPubKey, utxoAmount);
        }
        
        // For Komodo, set lock time to accrue interest, for other chains, set
        // locktime to spend time locked coinbases
        if (chainName.isKMD()) {
            if (!komodo_hardfork_active((uint32_t)chainActive.Tip()->nTime)) {
                builder_.SetLockTime((uint32_t)time(NULL) - 60); // set lock time for Komodo interest
            } else {
                builder_.SetLockTime((uint32_t)chainActive.Tip()->GetMedianTimePast());
            }
        }
    }

    // Log transaction composition for debugging purposes
    LogPrint((isFromTransparentAddress_) ? "zrpc" : "zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
             getId(), FormatMoney(totalTargetAmount), FormatMoney(totalSendAmount), FormatMoney(minersFee));
    LogPrint("zrpc", "%s: transparent input: %s (to choose from)\n", getId(), FormatMoney(totalTransparentInputs));
    LogPrint("zrpcunsafe", "%s: shielded input: %s sapling + %s orchard = %s total (to choose from)\n", 
             getId(), FormatMoney(totalSaplingInputs), FormatMoney(totalOrchardInputs), 
             FormatMoney(totalSaplingInputs + totalOrchardInputs));
    LogPrint("zrpcunsafe", "%s: shielded output: %s sapling + %s orchard = %s total\n", 
             getId(), FormatMoney(totalSaplingOutputs), FormatMoney(totalOrchardOutputs), 
             FormatMoney(totalSaplingOutputs + totalOrchardOutputs));
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    // Offline Signing
    // if (bOfflineSpendingKey == true) {
    //     /* Format the necessary data to construct a transaction that can
    //      * be signed with an off-line wallet
    //      */
    //
    //     builder_.SetFee(minersFee);
    //     builder_.SetMinConfirmations(1);
    //
    //     // Select Sapling notes that makes up the total amount to send:
    //     std::vector<SaplingOutPoint> ops;
    //     std::vector<SaplingNote> notes;
    //     CAmount sum = 0;
    //     int iI = 0;
    //     for (auto t : z_sapling_inputs_) {
    //         ops.push_back(t.op);
    //         notes.push_back(t.note);
    //         sum += t.note.value();
    //
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Process z_sapling_inputs_ #%d Value=%ld, Sum=%ld\n",iI, t.note.value(), sum); fflush(stdout);
    //         // iI+=1;
    //         if (sum >= targetAmount) {
    //             // printf("asyncrpcoperation_sendmany.cpp main_impl() Notes exceed targetAmount: %ld>%ld\n",sum,targetAmount);
    //             break;
    //         }
    //     }
    //
    //     // Fetch Sapling anchor and witnesses
    //     // printf("asyncrpcoperation_sendmany.cpp main_impl() Fetch Sapling anchor and witnesses\n"); fflush(stdout);
    //     uint256 anchor;
    //     std::vector<libzcash::MerklePath> saplingMerklePaths;
    //     {
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Fetch Sapling anchor and witnesses - start\n"); fflush(stdout);
    //         LOCK2(cs_main, pwalletMain->cs_wallet);
    //         if (!pwalletMain->GetSaplingNoteMerklePaths(ops, saplingMerklePaths, anchor)) {
    //             throw JSONRPCError(RPC_WALLET_ERROR, "Missing merkle path for Sapling note");
    //         }
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Fetch Sapling anchor and witnesses - done\n"); fflush(stdout);
    //     }
    //
    //     // Add Sapling spends to the transaction builder:
    //     // printf("asyncrpcoperation_sendmany.cpp main_impl() Add sapling spends: #%ld\n",notes.size() ); fflush(stdout);
    //
    //     // Note: expsk is uninitialised - we do not have the spending key!
    //     //     : fvk also garbage?
    //     SaplingExpandedSpendingKey expsk;
    //     auto fvk = expsk.full_viewing_key();
    //     auto ovk = fvk.ovk;
    //     for (size_t i = 0; i < notes.size(); i++) {
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Add sapling spend: %ld of %ld - start\n",i+1,notes.size() ); fflush(stdout);
    //         // Convert witness to a char array:
    //         CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    //         ss << saplingMerklePaths[i];
    //         std::vector<unsigned char> local_witness(ss.begin(), ss.end());
    //         myCharArray_s sWitness;
    //         memcpy(&sWitness.cArray[0], reinterpret_cast<unsigned char*>(local_witness.data()), sizeof(sWitness.cArray));
    //         assert(builder_.AddSaplingSpend_prepare_offline_transaction(fromAddress_, notes[i], anchor, saplingMerklePaths[i].position(), &sWitness.cArray[0]));
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Add sapling spend: %ld of %ld - done\n",i+1,notes.size() ); fflush(stdout);
    //     }
    //
    //     // Add Sapling outputs to the transaction builder
    //     // printf("asyncrpcoperation_sendmany.cpp main_impl() Add sapling outputs\n" ); fflush(stdout);
    //     iI = 0;
    //     for (auto r : sapling_outputs_) {
    //         auto address = std::get<0>(r);
    //         auto value = std::get<1>(r);
    //         auto strMemo = std::get<2>(r);
    //
    //
    //         // Note: transaction builder expectes memo in
    //         //       ASCII encoding, not as a hex string.
    //         std::array<unsigned char, ZC_MEMO_SIZE> caMemo = {0x00};
    //         if (IsHex(strMemo)) {
    //             if (strMemo.length() > (ZC_MEMO_SIZE * 2)) {
    //                 printf("asyncrpcoperation_sendmany.cpp main_impl() Hex encoded memo is larger than maximum allowed %d\n", (ZC_MEMO_SIZE * 2));
    //
    //                 UniValue o(UniValue::VOBJ);
    //                 o.push_back(Pair("Failure", "Memo is too long"));
    //                 set_result(o);
    //
    //                 return false;
    //             }
    //             caMemo = get_memo_from_hex_string(strMemo);
    //         } else {
    //             int iLength = strMemo.length();
    //
    //             if (strMemo.length() > ZC_MEMO_SIZE) {
    //                 printf("asyncrpcoperation_sendmany.cpp main_impl() Memo is larger than maximum allowed %d\n", ZC_MEMO_SIZE);
    //
    //                 UniValue o(UniValue::VOBJ);
    //                 o.push_back(Pair("Failure", "Memo is too long"));
    //                 set_result(o);
    //
    //                 return false;
    //             }
    //
    //             unsigned char cByte;
    //             for (int iI = 0; iI < iLength; iI++) {
    //                 cByte = (unsigned char)strMemo[iI];
    //                 caMemo[iI] = cByte;
    //             }
    //         }
    //
    //         // printf("asyncrpcoperation_sendmany.cpp main_impl() Output #%d:\n  addr=%s, ",iI+1,address.c_str() );
    //         // printf("value=%ld\n",value);
    //         // printf("memo=%s\n"  , strMemo.c_str() );
    //         // fflush(stdout);
    //         iI += 1;
    //         // builder_.AddSaplingOutput_offline_transaction(ovk, address, value, memo);
    //         builder_.AddSaplingOutput_offline_transaction(address, value, caMemo);
    //     }
    //
    //     // Build the off-line transaction
    //     std::string sResult = builder_.Build_offline_transaction();
    //     // printf("AsyncRPCOperation_sendmany::main_impl() %s\n",sResult.c_str() );
    //
    //     // Send result upstream
    //     // printf("AsyncRPCOperation_sendmany::main_impl() Result available\n");
    //     UniValue o(UniValue::VOBJ);
    //     o.push_back(Pair("Success", sResult));
    //     set_result(o);
    //
    //     // printf("AsyncRPCOperation_sendmany::main_impl() Pushed result OBJ back. return true\n");
    //     return true;
    // }

    /**
     * TRANSACTION BUILDING PHASE
     * 
     * All input validation and selection is complete. Now we build the actual transaction.
     * For shielded transactions, we need to use the TransactionBuilder with proper OVK handling.
     */

    builder_.SetFee(minersFee);

    // OVK (Outgoing Viewing Key) will be set based on the from address type
    uint256 ovk;

    // =================================================================
    // STEP 5: Handle transparent address sending and change generation
    // =================================================================
    
    // Set change address if we are using transparent funds
    // TODO: Should we just use fromtaddr_ as the change address?
    if (isFromTransparentAddress_) {

        // Sending from a t-address, which we don't have an ovk for. Instead,
        // generate a common one from the HD seed. This ensures the data is
        // recoverable, while keeping it logically separate from the ZIP 32
        // Sapling key hierarchy, which the user might not be using.
        HDSeed seed;
        if (!pwalletMain->GetHDSeed(seed)) {
            throw JSONRPCError(
                RPC_WALLET_ERROR,
                "AsyncRPCOperation_sendmany::main_impl(): HD seed not found");
        }
        ovk = ovkForShieldingFromTaddr(seed);

        LOCK2(cs_main, pwalletMain->cs_wallet);

        EnsureWalletIsUnlocked();
        CReserveKey keyChange(pwalletMain);
        CPubKey vchPubKey;
        bool ret = keyChange.GetReservedKey(vchPubKey);
        if (!ret) {
            // should never fail, as we just unlocked
            throw JSONRPCError(
                RPC_WALLET_KEYPOOL_RAN_OUT,
                "Could not generate a taddr to use as a change address");
        }

        CTxDestination changeAddr = vchPubKey.GetID();
        assert(builder_.SendChangeTo(changeAddr));
    }

    // =================================================================
    // STEP 6: Handle Sapling shielded address spending
    // =================================================================

    if (isFromSaplingAddress_) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Get various necessary keys
        SaplingExtendedSpendingKey extsk;
        auto extskPtr = std::get_if<libzcash::SaplingExtendedSpendingKey>(&spendingkey_);
        if (!extskPtr) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Invalid Sapling spending key type. Stopping.\n", getId()));
        }
        extsk = *extskPtr;
        ovk = extsk.expsk.full_viewing_key().ovk;

        // Select and process Sapling notes for spending
        CAmount sum = 0;
        for (auto entry : saplingInputs_) {

            libzcash::MerklePath saplingMerklePath;
            if (!pwalletMain->SaplingWalletGetMerklePathOfNote(entry.op.hash, entry.op.n, saplingMerklePath)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Merkle Path not found for Sapling note. Stopping.\n", getId()));
            }

            uint256 anchor;
            if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingMerklePath, entry.note.cmu().value(), anchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Getting Anchor failed. Stopping.\n", getId()));
            }

            if (!builder_.AddSaplingSpendRaw(entry.op, entry.address, entry.note.value(), entry.note.rcm(), saplingMerklePath, anchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Sapling Spend failed. Stopping.\n", getId()));
            }

            sum += entry.note.value();
            if (sum >= totalTargetAmount) {
                break;
            }
        }

        if (!builder_.ConvertRawSaplingSpend(extsk)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Sapling Spends failed.\n", getId()));
        }
    }

    // =================================================================
    // STEP 7: Handle Orchard shielded address spending
    // =================================================================

    if (isFromOrchardAddress_) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Get various necessary keys for Orchard spending
        OrchardExtendedSpendingKeyPirate extsk;
        auto extskPtr = std::get_if<libzcash::OrchardExtendedSpendingKeyPirate>(&spendingkey_);
        if (!extskPtr) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Invalid Orchard spending key type. Stopping.\n", getId()));
        }
        extsk = *extskPtr;

        auto fvkOpt = extsk.GetXFVK();
        if (fvkOpt == std::nullopt) {
          throw JSONRPCError(
              RPC_WALLET_ERROR,
               strprintf("%s: FVK not found for Orchard spending key. Stopping.\n", getId()));
        }

        auto fvk = fvkOpt.value().fvk;
        auto ovkOpt = fvk.GetOVK();
        if (ovkOpt == std::nullopt) {
          throw JSONRPCError(
              RPC_WALLET_ERROR,
               strprintf("%s: OVK not found for Orchard spending key. Stopping.\n", getId()));
        }

        ovk = ovkOpt.value().ovk;

        // Process Orchard notes for spending
        CAmount sum = 0;
        uint256 anchor;
        for (auto entry : orchardInputs_) {

            libzcash::MerklePath orchardMerklePath;
            if (!pwalletMain->OrchardWalletGetMerklePathOfNote(entry.op.hash, entry.op.n, orchardMerklePath)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Merkle Path not found for Orchard note. Stopping.\n", getId()));
            }

            uint256 pathAnchor;
            if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardMerklePath, entry.note.cmx(), pathAnchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Getting Orchard Anchor failed. Stopping.\n", getId()));
            }

            // Set orchard anchor for transaction (use first valid anchor)
            if (anchor.IsNull()) {
                anchor = pathAnchor;
            }

            if (!builder_.AddOrchardSpendRaw(entry.op, entry.address, entry.note.value(), entry.note.rho(), entry.note.rseed(), orchardMerklePath, anchor)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Orchard Spend failed. Stopping.\n", getId()));
            }

            sum += entry.note.value();
            if (sum >= totalTargetAmount) {
                break;
            }
        }

        // Initialize Orchard builder with spending capabilities
        builder_.InitializeOrchard(true, true, anchor);

        if (!builder_.ConvertRawOrchardSpend(extsk)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Orchard Spends failed.\n", getId()));
        }

    } else {
        // Initialize Orchard builder without spending (outputs only)
        if (orchardOutputs_.size() > 0) {
            builder_.InitializeOrchard(false, true, uint256());
        }
    }

    // Ensure we have a valid OVK at this point
    if (ovk.IsNull()) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: OVK was not properly initialized for this address type.\n", getId()));
    }

    // =================================================================
    // STEP 8: Add outputs to the transaction
    // =================================================================

    // Process Sapling outputs
    for (const auto& saplingOutput : saplingOutputs_) {
        std::string recipientAddress = std::get<0>(saplingOutput);
        CAmount outputValue = std::get<1>(saplingOutput);
        std::string hexMemo = std::get<2>(saplingOutput);

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
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Sapling Output failed. Stopping.\n", getId()));
        }
    }

    // Convert raw Sapling outputs with OVK for encryption
    if (!builder_.ConvertRawSaplingOutput(ovk)) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Sapling Outputs failed.\n", getId()));
    }

    // Process Orchard outputs
    for (const auto& orchardOutput : orchardOutputs_) {
        std::string recipientAddress = std::get<0>(orchardOutput);
        CAmount outputValue = std::get<1>(orchardOutput);
        std::string hexMemo = std::get<2>(orchardOutput);

        // Decode and validate the Orchard payment address
        auto decodedAddress = DecodePaymentAddress(recipientAddress);
        assert(std::get_if<libzcash::OrchardPaymentAddressPirate>(&decodedAddress) != nullptr);
        auto orchardPaymentAddress = *(std::get_if<libzcash::OrchardPaymentAddressPirate>(&decodedAddress));

        // Convert hex memo to Memo object or nullopt
        std::optional<libzcash::Memo> memo = std::nullopt;
        if (!hexMemo.empty()) {
            auto memoArray = get_memo_from_hex_string(hexMemo);
            memo = libzcash::Memo(memoArray);
        }

        // Add the raw Orchard output to the transaction builder
        if (!builder_.AddOrchardOutputRaw(orchardPaymentAddress, outputValue, memo)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Orchard Output failed. Stopping.\n", getId()));
        }
    }

    // When we have Orchard outputs, we need to convert them
    // to the final format with the OVK for encryption.
    if (!orchardOutputs_.empty()) {
        if (!builder_.ConvertRawOrchardOutput(ovk)) {
            throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Orchard Outputs failed.\n", getId()));
        }
    }
    
    // =================================================================
    // STEP 9: Build and send the transaction
    // =================================================================
    
    // Build the final transaction from all inputs and outputs
    tx_ = builder_.Build().GetTxOrThrow();

    // Encode transaction for transmission
    auto signedTransactionHex = EncodeHexTx(tx_);
    
    if (!testmode) {
        // Production mode: broadcast transaction to the network
        UniValue rpcParams = UniValue(UniValue::VARR);
        rpcParams.push_back(signedTransactionHex);
        
        UniValue broadcastResult = sendrawtransaction(rpcParams, false, CPubKey());
        if (broadcastResult.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");
        }

        std::string transactionId = broadcastResult.get_str();

        // Set successful result with transaction ID
        UniValue result(UniValue::VOBJ);
        result.push_back(Pair("txid", transactionId));
        set_result(result);
    } else {
        // Test mode: return transaction details without broadcasting  
        UniValue result(UniValue::VOBJ);
        result.push_back(Pair("test", 1));
        result.push_back(Pair("txid", tx_.GetHash().ToString()));
        result.push_back(Pair("hex", signedTransactionHex));
        set_result(result);
    }

    return true;
}

/**
 * @brief Find available UTXOs for transparent address spending
 * 
 * Searches the wallet for unspent transparent outputs that can be used as inputs
 * for the transaction. Applies minimum depth and destination filtering.
 * 
 * @param acceptCoinbaseOutputs Whether to include coinbase outputs in the search
 * @return true if suitable UTXOs were found, false otherwise
 */
bool AsyncRPCOperation_sendmany::find_utxos(bool acceptCoinbaseOutputs)
{
    // Set up destination filter for the from address
    std::set<CTxDestination> allowedDestinations;
    allowedDestinations.insert(fromtaddr_);

    std::vector<COutput> availableOutputs;

    // Retrieve all available coins from the wallet
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(availableOutputs, false, NULL, true, acceptCoinbaseOutputs);
    }

    // Process each available output
    for (const COutput& output : availableOutputs) {
        CTxDestination outputDestination;

        // Skip unspendable outputs
        if (!output.fSpendable) {
            continue;
        }

        // Apply minimum depth requirements with dPoW consideration
        if (mindepth_ > 1) {
            int blockHeight = tx_height(output.tx->GetHash());
            int dPoWConfirmations = komodo_dpowconfs(blockHeight, output.nDepth);
            if (dPoWConfirmations < mindepth_) {
                continue;
            }
        } else {
            if (output.nDepth < mindepth_) {
                continue;
            }
        }

        const CScript& outputScript = output.tx->vout[output.i].scriptPubKey;

        // Apply destination filtering if we have specific destinations
        if (!allowedDestinations.empty()) {
            if (!ExtractDestination(outputScript, outputDestination)) {
                continue;
            }

            if (allowedDestinations.find(outputDestination) == allowedDestinations.end()) {
                continue;
            }
        }

        // Check if this is a coinbase output and apply acceptance policy
        bool isCoinbaseOutput = output.tx->IsCoinBase();
        if (isCoinbaseOutput && !acceptCoinbaseOutputs) {
            continue;
        }

        // Extract the destination for the UTXO record
        if (!ExtractDestination(outputScript, outputDestination, true)) {
            continue;
        }

        CAmount outputValue = output.tx->vout[output.i].nValue;

        // Create UTXO record and add to inputs list
        SendManyInputUTXO utxoRecord(output.tx->GetHash(), output.i, outputValue, isCoinbaseOutput, outputDestination);
        transparentInputs_.push_back(utxoRecord);
    }

    // Sort UTXOs in ascending order by value (smaller UTXOs first)
    // This helps with optimal UTXO selection and change minimization
    std::sort(transparentInputs_.begin(), transparentInputs_.end(), 
        [](const SendManyInputUTXO& utxo1, const SendManyInputUTXO& utxo2) -> bool {
            return std::get<2>(utxo1) < std::get<2>(utxo2);
        });

    return !transparentInputs_.empty();
}


/**
 * @brief Find unspent shielded notes for spending
 * 
 * Searches the wallet for unspent Sapling and Orchard notes that can be used
 * as inputs for the transaction. Handles both online and offline spending modes.
 * 
 * @return true if suitable notes were found, false otherwise
 */
bool AsyncRPCOperation_sendmany::find_unspent_notes()
{
    std::vector<SaplingNoteEntry> saplingNoteEntries;
    std::vector<OrchardNoteEntry> orchardNoteEntries;
    
    // Retrieve filtered notes based on spending mode
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        
        if (hasOfflineSpendingKey) {
            // Offline mode: retrieve notes without requiring spending key validation
            pwalletMain->GetFilteredNotes(saplingNoteEntries, orchardNoteEntries, 
                                        fromaddress_, mindepth_, true, false);
        } else {
            // Online mode: require spending key validation
            pwalletMain->GetFilteredNotes(saplingNoteEntries, orchardNoteEntries, 
                                        fromaddress_, mindepth_, true, true);
        }
    }

    // Process Sapling note entries
    for (const auto& noteEntry : saplingNoteEntries) {
        saplingInputs_.push_back(noteEntry);
        
        // Log note discovery for debugging (truncated for readability)
        std::string memoData(noteEntry.memo.begin(), noteEntry.memo.end());
        LogPrint("zrpcunsafe", "%s: found unspent Sapling note (txid=%s, output=%d, amount=%s, memo=%s)\n",
                 getId(),
                 noteEntry.op.hash.ToString().substr(0, 10),
                 noteEntry.op.n,
                 FormatMoney(noteEntry.note.value()),
                 HexStr(memoData).substr(0, 10));
    }

    // Process Orchard note entries
    for (const auto& noteEntry : orchardNoteEntries) {
        orchardInputs_.push_back(noteEntry);
        
        // Log note discovery for debugging (truncated for readability)
        std::string memoData(noteEntry.memo.begin(), noteEntry.memo.end());
        LogPrint("zrpcunsafe", "%s: found unspent Orchard note (txid=%s, output=%d, amount=%s, memo=%s)\n",
                 getId(),
                 noteEntry.op.hash.ToString().substr(0, 10),
                 noteEntry.op.n,
                 FormatMoney(noteEntry.note.value()),
                 HexStr(memoData).substr(0, 10));
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
 * @brief Convert hexadecimal string to memo byte array
 * 
 * Parses a hexadecimal string and converts it to a fixed-size memo array
 * suitable for use in shielded transactions. Validates hex format and size.
 * 
 * @param hexString Hexadecimal string representation of the memo
 * @return std::array<unsigned char, ZC_MEMO_SIZE> Fixed-size memo array
 * @throws JSONRPCError if hex format is invalid or memo is too large
 */
std::array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_sendmany::get_memo_from_hex_string(std::string hexString)
{
    // Initialize memo to default "no_memo" value (0xF6), per protocol spec section 5.5
    std::array<unsigned char, ZC_MEMO_SIZE> memoArray = {{0xF6}};

    // Parse hexadecimal string to raw bytes
    std::vector<unsigned char> rawMemoBytes = ParseHex(hexString.c_str());

    // Validate hex string format
    size_t hexStringLength = hexString.length();
    if (hexStringLength % 2 != 0 || (hexStringLength > 0 && rawMemoBytes.size() != hexStringLength / 2)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");
    }

    // Validate memo size doesn't exceed maximum
    if (rawMemoBytes.size() > ZC_MEMO_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            strprintf("Memo size of %d is too big, maximum allowed is %d", 
                     rawMemoBytes.size(), ZC_MEMO_SIZE));
    }

    // Copy parsed bytes into the fixed-size memo array
    size_t bytesToCopy = std::min(static_cast<size_t>(ZC_MEMO_SIZE), rawMemoBytes.size());
    for (size_t i = 0; i < bytesToCopy; i++) {
        memoArray[i] = rawMemoBytes[i];
    }
    
    return memoArray;
}

/**
 * @brief Override getStatus() to include operation parameters in status
 * 
 * Extends the base status information with the specific parameters used
 * for this z_sendmany operation, providing better debugging and monitoring.
 * 
 * @return UniValue Status object with method and parameters included
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
    enhancedStatus.push_back(Pair("method", "z_sendmany"));
    enhancedStatus.push_back(Pair("params", contextinfo_));
    
    return enhancedStatus;
}
