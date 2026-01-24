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
std::optional<libzcash::OrchardPaymentAddressPirate> rpcOrchardSweepAddress;

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

    std::vector<SaplingNoteEntry> saplingNoteEntries;
    std::vector<OrchardNoteEntry> orchardNoteEntries;
    libzcash::SaplingPaymentAddress saplingSweepAddress;
    libzcash::OrchardPaymentAddressPirate orchardSweepAddress;
    std::map<libzcash::SaplingIncomingViewingKey, std::vector<SaplingNoteEntry>> mapSaplingIvks;
    std::map<libzcash::OrchardPaymentAddressPirate, std::vector<OrchardNoteEntry>> mapOrchardSpendingKeys;
    bool hasSaplingTarget = false;
    bool hasOrchardTarget = false;

    {
        // We set minDepth to 11 to avoid unconfirmed notes and in anticipation of specifying
        // an anchor at height N-10 for each Sprout JoinSplit description
        // Consider, should notes be sorted?
        pwalletMain->GetFilteredNotes(saplingNoteEntries, orchardNoteEntries, "", 11);
        if (!fromRPC_) {
            if (fSweepMapUsed) {
                // Support protocol-agnostic sweep address configuration
                std::vector<std::string> sweepAddresses;
                
                // Use unified sweep address parameter
                const vector<string>& vSweep = mapMultiArgs["-sweepaddress"];
                if (!vSweep.empty()) {
                    sweepAddresses = vSweep;
                }
                
                if (sweepAddresses.empty() || sweepAddresses.size() != 1) {
                    LogPrint("zrpcunsafe", "%s: Exactly one sweep address must be specified.\n", getId());
                    return false;
                }
                
                libzcash::PaymentAddress zAddress = DecodePaymentAddress(sweepAddresses[0]);
                if (std::get_if<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                    saplingSweepAddress = *(std::get_if<libzcash::SaplingPaymentAddress>(&zAddress));
                    hasSaplingTarget = true;
                } else if (std::get_if<libzcash::OrchardPaymentAddressPirate>(&zAddress) != nullptr) {
                    orchardSweepAddress = std::get<libzcash::OrchardPaymentAddressPirate>(zAddress);
                    hasOrchardTarget = true;
                } else {
                    LogPrint("zrpcunsafe", "%s: Invalid sweep address format.\n", getId());
                    return false;
                }
            } else {
                LogPrint("zrpcunsafe", "%s: Sweep address mapping not configured.\n", getId());
                return false;
            }
        } else {
            // For RPC calls, ensure only one destination address is set
            if (rpcSaplingSweepAddress.has_value() && rpcOrchardSweepAddress.has_value()) {
                LogPrint("zrpcunsafe", "%s: Only one destination address (Sapling or Orchard) allowed per sweep operation.\n", getId());
                return false;
            }
            
            if (rpcSaplingSweepAddress.has_value()) {
                saplingSweepAddress = rpcSaplingSweepAddress.value();
                hasSaplingTarget = true;
            } else if (rpcOrchardSweepAddress.has_value()) {
                orchardSweepAddress = rpcOrchardSweepAddress.value();
                hasOrchardTarget = true;
            } else {
                LogPrint("zrpcunsafe", "%s: No destination address specified for sweep operation.\n", getId());
                return false;
            }
        }

        // Map Sapling notes by incoming viewing key (combining diversified addresses with same IVK)
        for (auto& saplingEntry : saplingNoteEntries) {
            if (hasSaplingTarget && saplingSweepAddress == saplingEntry.address) {
                continue; // Skip notes that are already at the target address
            }
            
            libzcash::SaplingIncomingViewingKey saplingIvk;
            if (pwalletMain->GetSaplingIncomingViewingKey(saplingEntry.address, saplingIvk)) {
                auto saplingIvkIt = mapSaplingIvks.find(saplingIvk);
                if (saplingIvkIt != mapSaplingIvks.end()) {
                    saplingIvkIt->second.push_back(saplingEntry);
                } else {
                    std::vector<SaplingNoteEntry> saplingEntries;
                    saplingEntries.push_back(saplingEntry);
                    mapSaplingIvks[saplingIvk] = saplingEntries;
                }
            }
        }

        // Map Orchard notes by address (excluding sweep target address)
        for (auto& orchardEntry : orchardNoteEntries) {
            if (hasOrchardTarget && orchardSweepAddress == orchardEntry.address) {
                continue; // Skip notes that are already at the target address
            }
            
            auto orchardAddrIt = mapOrchardSpendingKeys.find(orchardEntry.address);
            if (orchardAddrIt != mapOrchardSpendingKeys.end()) {
                orchardAddrIt->second.push_back(orchardEntry);
            } else {
                std::vector<OrchardNoteEntry> orchardEntries;
                orchardEntries.push_back(orchardEntry);
                mapOrchardSpendingKeys[orchardEntry.address] = orchardEntries;
            }
        }
    }

    int totalTxCreated = 0;
    std::vector<std::string> allSweepTxIds;
    CAmount totalAmountSwept = 0;
    CCoinsViewCache coinsView(pcoinsTip);
    bool sweepComplete = true;

    // Process Sapling incoming viewing keys (combining notes from diversified addresses)
    for (auto saplingIvkIt = mapSaplingIvks.begin(); saplingIvkIt != mapSaplingIvks.end(); ++saplingIvkIt) {
        auto saplingIvk = saplingIvkIt->first;
        auto saplingNoteEntries = saplingIvkIt->second;

        // Get the spending key for this IVK
        libzcash::SaplingExtendedFullViewingKey saplingExtfvk;
        if (!pwalletMain->GetSaplingFullViewingKey(saplingIvk, saplingExtfvk)) {
            continue;
        }
        
        libzcash::SaplingExtendedSpendingKey saplingExtsk;
        if (!pwalletMain->GetSaplingSpendingKey(saplingExtfvk, saplingExtsk)) {
            continue;
        }

        std::vector<SaplingNoteEntry> saplingSweepInputs;
        CAmount saplingAmountToSend = 0;
        int saplingMaxQuantity = 50;

        // Count all Notes available for this IVK (across all diversified addresses)
        int saplingNoteCount = 0;
        for (const SaplingNoteEntry& saplingNoteEntry : saplingNoteEntries) {
            libzcash::SaplingIncomingViewingKey entrySaplingIvk;
            pwalletMain->GetSaplingIncomingViewingKey(saplingNoteEntry.address, entrySaplingIvk);

            // Verify this note belongs to this IVK (should always be true due to our mapping)
            if (entrySaplingIvk == saplingIvk) {
                ++saplingNoteCount;
            }
        }

        // Don't sweep if only one note (no consolidation benefit)
        if (saplingNoteCount <= 1) {
            continue;
        }

        // If we make it here then we need to sweep and the routine is considered incomplete
        sweepComplete = false;

        // Select all notes that belong to this IVK (from all diversified addresses)
        for (const SaplingNoteEntry& saplingNoteEntry : saplingNoteEntries) {
            libzcash::SaplingIncomingViewingKey entrySaplingIvk;
            pwalletMain->GetSaplingIncomingViewingKey(saplingNoteEntry.address, entrySaplingIvk);

            // Select Notes from this IVK (across all diversified addresses)
            if (entrySaplingIvk == saplingIvk) {
                saplingAmountToSend += CAmount(saplingNoteEntry.note.value());
                saplingSweepInputs.push_back(saplingNoteEntry);
            }

            if (saplingSweepInputs.size() >= saplingMaxQuantity)
                break;
        } // End note selection loop for this IVK

        int saplingMinQuantity = 1;
        if (saplingSweepInputs.size() < saplingMinQuantity)
            continue;

        CAmount saplingFee = fSweepTxFee;
        if (saplingAmountToSend <= fSweepTxFee) {
            saplingFee = 0;
        }
        
        auto saplingBuilder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
        {
            saplingBuilder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);

            LogPrint("zrpcunsafe", "%s: Creating Sapling sweep transaction with output amount=%s from spending key\n", getId(), FormatMoney(saplingAmountToSend - saplingFee));

            for (auto saplingEntry : saplingSweepInputs) {
                    libzcash::MerklePath saplingSweepMerklePath;
                    if (!pwalletMain->SaplingWalletGetMerklePathOfNote(saplingEntry.op.hash, saplingEntry.op.n, saplingSweepMerklePath)) {
                        LogPrint("zrpcunsafe", "%s: Merkle Path not found for Sapling note. Skipping this transaction.\n", getId());
                        sweepComplete = true;
                        break;
                    }

                    uint256 saplingAnchor;
                    if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingSweepMerklePath, saplingEntry.note.cmu().value(), saplingAnchor)) {
                        LogPrint("zrpcunsafe", "%s: Getting Anchor failed for Sapling note. Skipping this transaction.\n", getId());
                        sweepComplete = true;
                        break;
                    }

                    if (!saplingBuilder.AddSaplingSpendRaw(saplingEntry.op, saplingEntry.address, saplingEntry.note.value(), saplingEntry.note.rcm(), saplingSweepMerklePath, saplingAnchor)) {
                        LogPrint("zrpcunsafe", "%s: Adding Raw Sapling Spend failed. Skipping this transaction.\n", getId());
                        sweepComplete = true;
                        break;
                    }
                }

            if (!saplingBuilder.ConvertRawSaplingSpend(saplingExtsk)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Sapling Spends failed. Skipping this transaction.\n", getId());
                continue;
            }
        } // End LOCK2 block
        
        saplingBuilder.SetFee(saplingFee);
        
        // Add output to the single destination address
        if (hasSaplingTarget) {
            saplingBuilder.AddSaplingOutputRaw(saplingSweepAddress, saplingAmountToSend - saplingFee, std::nullopt);
            saplingBuilder.ConvertRawSaplingOutput(saplingExtsk.expsk.ovk);
        } else if (hasOrchardTarget) {
            saplingBuilder.AddOrchardOutputRaw(orchardSweepAddress, saplingAmountToSend - saplingFee, std::nullopt);
            saplingBuilder.InitializeOrchard(false,true,uint256());
            saplingBuilder.ConvertRawOrchardOutput(saplingExtsk.expsk.ovk);
        } else {
            LogPrint("zrpcunsafe", "%s: No target address specified for Sapling sweep. Skipping.\n", getId());
            continue;
        }

        auto saplingBuildResult = saplingBuilder.Build();
        if (!saplingBuildResult.IsTx()) {
            LogPrint("zrpcunsafe", "%s: Failed to build Sapling sweep transaction: %s. Skipping.\n", getId(), saplingBuildResult.GetError());
            continue;
        }
        auto saplingTx = saplingBuildResult.GetTxOrThrow();

        if (isCancelled()) {
            LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
            sweepComplete = true;
            break;
        }

        if (!pwalletMain->CommitAutomatedTx(saplingTx)) {
            LogPrint("zrpcunsafe", "%s: Failed to commit Sapling sweep transaction. Skipping.\n", getId());
            continue;
        }
        
        LogPrint("zrpcunsafe", "%s: Committed Sapling sweep transaction with txid=%s\n", getId(), saplingTx.GetHash().ToString());
        totalAmountSwept += saplingAmountToSend - saplingFee;
        totalTxCreated++;
        allSweepTxIds.push_back(saplingTx.GetHash().ToString());
    } // End Sapling IVK processing loop

    // Process Orchard addresses (one transaction per address)
    for (auto orchardIt = mapOrchardSpendingKeys.begin(); orchardIt != mapOrchardSpendingKeys.end(); ++orchardIt) {
        auto orchardAddr = orchardIt->first;
        auto orchardNoteEntries = orchardIt->second;

        libzcash::OrchardExtendedSpendingKeyPirate orchardExtendedSpendingKey;
        if (!pwalletMain->GetOrchardExtendedSpendingKey(orchardAddr, orchardExtendedSpendingKey)) {
            continue;
        }
        
        CAmount orchardAmountToSend = 0;
        std::vector<OrchardNoteEntry> orchardSweepInputs;
        int orchardMaxQuantity = 50;

        for (const OrchardNoteEntry& orchardNoteEntry : orchardNoteEntries) {
            orchardAmountToSend += CAmount(orchardNoteEntry.note.value());
            orchardSweepInputs.push_back(orchardNoteEntry);

            if (orchardSweepInputs.size() >= orchardMaxQuantity) {
                break;
            }
        }

        int orchardMinQuantity = 1;
        if (orchardSweepInputs.size() < orchardMinQuantity) {
            continue;
        }

        CAmount orchardFee = fSweepTxFee;
        if (orchardAmountToSend <= fSweepTxFee) {
            orchardFee = 0;
        }
        
        auto orchardBuilder = TransactionBuilder(consensusParams, targetHeight_, pwalletMain);
        {
            orchardBuilder.SetExpiryHeight(chainActive.Tip()->nHeight + SWEEP_EXPIRY_DELTA);

            LogPrint("zrpcunsafe", "%s: Creating Orchard sweep transaction with %d notes, output amount=%s\n", getId(), orchardSweepInputs.size(), FormatMoney(orchardAmountToSend - orchardFee));

            uint256 orchardAnchor;
            for (auto orchardEntry : orchardSweepInputs) {
                libzcash::MerklePath orchardSweepMerklePath;
                if (!pwalletMain->OrchardWalletGetMerklePathOfNote(orchardEntry.op.hash, orchardEntry.op.n, orchardSweepMerklePath)) {
                    LogPrint("zrpcunsafe", "%s: Merkle Path not found for Orchard note. Skipping this transaction.\n", getId());
                    sweepComplete = true;
                    break;
                }
                
                uint256 anchor;
                if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardSweepMerklePath, orchardEntry.note.cmx(), anchor)) {
                    LogPrint("zrpcunsafe", "%s: Getting Anchor failed for Orchard note. Skipping this transaction.\n", getId());
                    sweepComplete = true;
                    break;
                }

                //Set the anchor for the first note, it will be used for all subsequent notes
                if (orchardAnchor.IsNull()) {
                    orchardAnchor = anchor;
                } else if (orchardAnchor != anchor) {
                    LogPrint("zrpcunsafe", "%s: Anchor mismatch for Orchard notes. Skipping this transaction.\n", getId());
                    sweepComplete = true;
                    break;
                }

                // Add Orchard spend to the transaction builder
                libzcash::OrchardNote orchardNote = orchardEntry.note;
                if (!orchardBuilder.AddOrchardSpendRaw(orchardEntry.op, orchardEntry.address, orchardNote.value(), orchardNote.rho(), orchardNote.rseed(), orchardSweepMerklePath, orchardAnchor)) {
                    LogPrint("zrpcunsafe", "%s: Adding Raw Orchard Spend failed. Skipping this transaction.\n", getId());
                    sweepComplete = true;
                    break;
                }
            }

            // Initialize Orchard builder with the transaction anchor
            orchardBuilder.InitializeOrchard(true,true,orchardAnchor);

            if (!orchardBuilder.ConvertRawOrchardSpend(orchardExtendedSpendingKey)) {
                LogPrint("zrpcunsafe", "%s: Converting Raw Orchard Spends failed. Skipping this transaction.\n", getId());
                continue;
            }
        }

        orchardBuilder.SetFee(orchardFee);

        // Get outgoing viewing key from extended spending key
        uint256 orchardOvk;
        auto orchardFvk = orchardExtendedSpendingKey.sk.GetFVK();
        if (!orchardFvk) {
            LogPrint("zrpcunsafe", "%s: Failed to get FVK from spending key. Stopping.\n", getId());
            continue;
        }
        auto orchardOvkResult = orchardFvk->GetOVK();
        if (!orchardOvkResult) {
            LogPrint("zrpcunsafe", "%s: Failed to get OVK from FVK. Stopping.\n", getId());
            continue;
        }
        orchardOvk = orchardOvkResult->ovk;

        // Add output to the single destination address
        if (hasOrchardTarget) {
            orchardBuilder.AddOrchardOutputRaw(orchardSweepAddress, orchardAmountToSend - orchardFee, std::nullopt);
            orchardBuilder.ConvertRawOrchardOutput(orchardOvk);
        } else if (hasSaplingTarget) { 
            orchardBuilder.AddSaplingOutputRaw(saplingSweepAddress, orchardAmountToSend - orchardFee, std::nullopt);
            orchardBuilder.ConvertRawSaplingOutput(orchardOvk);
        } else {
            LogPrint("zrpcunsafe", "%s: No target address specified for Orchard sweep. Skipping.\n", getId());
            continue;
        }

        auto orchardBuildResult = orchardBuilder.Build();
        if (!orchardBuildResult.IsTx()) {
            LogPrint("zrpcunsafe", "%s: Failed to build Orchard sweep transaction: %s. Skipping.\n", getId(), orchardBuildResult.GetError());
            continue;
        }
        auto orchardTx = orchardBuildResult.GetTxOrThrow();

        if (isCancelled()) {
            LogPrint("zrpcunsafe", "%s: Canceled. Stopping.\n", getId());
            sweepComplete = true;
            break;
        }

        if (!pwalletMain->CommitAutomatedTx(orchardTx)) {
            LogPrint("zrpcunsafe", "%s: Failed to commit Orchard sweep transaction. Skipping.\n", getId());
            continue;
        }
        
        LogPrint("zrpcunsafe", "%s: Committed Orchard sweep transaction with txid=%s\n", getId(), orchardTx.GetHash().ToString());
        totalAmountSwept += orchardAmountToSend - orchardFee;
        totalTxCreated++;
        allSweepTxIds.push_back(orchardTx.GetHash().ToString());
    }

    if (sweepComplete) {
        pwalletMain->nextSweep = pwalletMain->sweepInterval + chainActive.Tip()->nHeight;
        pwalletMain->fSweepRunning = false;
    }

    LogPrint("zrpcunsafe", "%s: Created %d transactions with total output amount=%s to single destination (consolidating by spending key)\n", getId(), totalTxCreated, FormatMoney(totalAmountSwept));
    setSweepResult(totalTxCreated, totalAmountSwept, allSweepTxIds);
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
void AsyncRPCOperation_sweeptoaddress::setOrchardSweepAddress(const libzcash::OrchardPaymentAddressPirate& address) {
    rpcOrchardSweepAddress = address;
    rpcSaplingSweepAddress.reset(); // Clear Sapling address to ensure only one destination
}
