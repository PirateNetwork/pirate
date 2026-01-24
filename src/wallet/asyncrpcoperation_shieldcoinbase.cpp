// Copyright (c) 2017 The Zcash developers
// Copyright (c) 2022-2025 Pirate developers
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

#include "amount.h"
#include "asyncrpcqueue.h"
#include "consensus/upgrades.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "komodo_globals.h"
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

#include <array>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "asyncrpcoperation_shieldcoinbase.h"

#include "paymentdisclosure.h"
#include "paymentdisclosuredb.h"

using namespace libzcash;

/**
 * @brief Helper function to find output index in JoinSplit operation map
 * 
 * This function searches through the outputmap array of a JoinSplit operation
 * to find the index corresponding to a given output number.
 * 
 * @param obj UniValue object containing the JoinSplit operation data
 * @param n Output number to find
 * @return Index in the outputmap array
 * @throws JSONRPCError if outputmap is missing
 * @throws std::logic_error if output number is not found
 */
static int find_output(UniValue obj, int n)
{
    UniValue outputMapValue = find_value(obj, "outputmap");
    if (!outputMapValue.isArray()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing outputmap for JoinSplit operation");
    }

    UniValue outputMap = outputMapValue.get_array();
    assert(outputMap.size() == ZC_NUM_JS_OUTPUTS);
    for (size_t i = 0; i < outputMap.size(); i++) {
        if (outputMap[i].get_int() == n) {
            return i;
        }
    }

    throw std::logic_error("n is not present in outputmap");
}

/**
 * @brief AsyncRPCOperation_shieldcoinbase constructor
 * 
 * Initializes a new shield coinbase operation with validation.
 * 
 * @param consensusParams Consensus parameters for the current network
 * @param nHeight Current blockchain height for transaction building
 * @param contextualTx Base transaction context for building
 * @param inputs Vector of coinbase UTXOs to shield
 * @param toAddress Destination shielded address as string
 * @param fee Transaction fee amount (defaults to SHIELD_COINBASE_DEFAULT_MINERS_FEE)
 * @param contextInfo Additional context information for status reporting
 * 
 * @throws std::runtime_error if fee is negative or recipient address is invalid
 */
AsyncRPCOperation_shieldcoinbase::AsyncRPCOperation_shieldcoinbase(
        const Consensus::Params& consensusParams,
        const int nHeight,
        CMutableTransaction contextualTx,
        std::vector<ShieldCoinbaseUTXO> inputs,
        std::string toAddress,
        CAmount fee,
        UniValue contextInfo) :
    builder_(consensusParams, nHeight, pwalletMain), inputs_(inputs), fee_(fee), contextinfo_(contextInfo)
{
    assert(fee_ >= 0);

    // Parse and validate the destination address
    tozaddr_ = DecodePaymentAddress(toAddress);
    if (!IsValidPaymentAddress(tozaddr_)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid recipient address");
    }
    
    // Validate that we have inputs to shield
    if (inputs_.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No coinbase inputs provided for shielding");
    }
}

AsyncRPCOperation_shieldcoinbase::~AsyncRPCOperation_shieldcoinbase()
{
}

/**
 * @brief Main execution wrapper for shield coinbase operation
 * 
 * This method is the main entry point for executing the shield coinbase operation.
 * It performs the following steps:
 * 1. Validates the operation state
 * 2. Calls the core shielding logic
 * 3. Handles any exceptions and updates operation state
 * 
 * The operation involves moving transparent coinbase UTXOs to a shielded address,
 * providing privacy for mining rewards.
 */
void AsyncRPCOperation_shieldcoinbase::main()
{
    if (isCancelled()) {
        return;
    }

    set_state(OperationStatus::EXECUTING);

    try {
        bool success = main_impl();

        if (success) {
            set_state(OperationStatus::SUCCESS);
        } else {
            set_state(OperationStatus::FAILED);
        }

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
        set_error_code(-2);
        set_error_message("General exception: " + std::string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("Unknown error occurred during shield coinbase operation");
    }
}

/**
 * @brief Core implementation of shield coinbase operation
 * 
 * This method contains the main logic for shielding coinbase UTXOs:
 * 1. Validates input limits and available funds
 * 2. Calculates the amount to shield after fee deduction
 * 3. Delegates to appropriate visitor based on recipient address type
 * 
 * @return true if operation completed successfully, false otherwise
 * @throws JSONRPCError if validation fails or insufficient funds
 */
bool AsyncRPCOperation_shieldcoinbase::main_impl()
{
    CAmount minersFee = fee_;

    size_t numInputs = inputs_.size();

    // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
    size_t limit = static_cast<size_t>(GetArg("-mempooltxinputlimit", 0));
    {
        LOCK(cs_main);
        if (NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
            limit = 0;
        }
    }
    if (limit > 0 && numInputs > limit) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           strprintf("Number of inputs %d is greater than mempooltxinputlimit of %d",
                                     numInputs, limit));
    }

    // Calculate total amount available from coinbase inputs
    CAmount totalInputAmount = 0;
    for (const ShieldCoinbaseUTXO& utxo : inputs_) {
        totalInputAmount += utxo.amount;
    }

    if (totalInputAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient coinbase funds, have %s and miners fee is %s",
                                     FormatMoney(totalInputAmount), FormatMoney(minersFee)));
    }

    CAmount shieldAmount = totalInputAmount - minersFee;
    LogPrint("zrpc", "%s: spending %s to shield %s with fee %s\n",
             getId(), FormatMoney(totalInputAmount), FormatMoney(shieldAmount), FormatMoney(minersFee));

    return std::visit(ShieldToAddress(this, shieldAmount), tozaddr_);
}

/**
 * @brief Handle Sprout payment address (deprecated)
 * 
 * Sprout addresses are deprecated and not supported for shield coinbase operations.
 * 
 * @param zaddr Sprout payment address (unused)
 * @return false - Sprout addresses are not supported
 */
bool ShieldToAddress::operator()(const libzcash::SproutPaymentAddress& zaddr) const
{
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Sprout addresses are not supported for shield coinbase operations");
}

/**
 * @brief Handle Sapling payment address for shield coinbase operation
 * 
 * Creates a transaction that shields coinbase UTXOs to a Sapling shielded address.
 * 
 * @param zaddr Target Sapling payment address
 * @return true if transaction was built and sent successfully
 * @throws JSONRPCError if HD seed is not available or transaction fails
 */
bool ShieldToAddress::operator()(const libzcash::SaplingPaymentAddress& zaddr) const
{
    m_op->builder_.SetFee(m_op->fee_);

    // Sending from a t-address, which we don't have an ovk for. Instead,
    // generate a common one from the HD seed. This ensures the data is
    // recoverable, while keeping it logically separate from the ZIP 32
    // Sapling key hierarchy, which the user might not be using.
    HDSeed seed;
    if (!pwalletMain->GetHDSeed(seed)) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "HD seed not found - required for shielding from transparent address");
    }
    uint256 ovk = ovkForShieldingFromTaddr(seed);

    // Add transparent inputs
    for (const auto& utxo : m_op->inputs_) {
        if (utxo.amount >= ASSETCHAINS_TIMELOCKGTE) {
            m_op->builder_.SetLockTime(static_cast<uint32_t>(chainActive.Height()));
            m_op->builder_.AddTransparentInput(COutPoint(utxo.txid, utxo.vout), utxo.scriptPubKey, utxo.amount, 0xfffffffe);
        } else {
            m_op->builder_.AddTransparentInput(COutPoint(utxo.txid, utxo.vout), utxo.scriptPubKey, utxo.amount);
        }
    }

    // Send all value to the target z-addr
    m_op->builder_.SendChangeTo(zaddr, ovk);

    // Build the transaction
    m_op->tx_ = m_op->builder_.Build().GetTxOrThrow();

    // Send the transaction
    // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
    auto signedTxHex = EncodeHexTx(m_op->tx_);
    if (!m_op->testmode) {
        UniValue params = UniValue(UniValue::VARR);
        params.push_back(signedTxHex);
        UniValue sendResultValue = sendrawtransaction(params, false, CPubKey());
        if (sendResultValue.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");
        }

        auto txid = sendResultValue.get_str();

        UniValue resultObj(UniValue::VOBJ);
        resultObj.push_back(Pair("txid", txid));
        m_op->set_result(resultObj);
    } else {
        // Test mode does not send the transaction to the network.
        UniValue resultObj(UniValue::VOBJ);
        resultObj.push_back(Pair("test", 1));
        resultObj.push_back(Pair("txid", m_op->tx_.GetHash().ToString()));
        resultObj.push_back(Pair("hex", signedTxHex));
        m_op->set_result(resultObj);
    }

    return true;
}

/**
 * @brief Handle Orchard payment address for shield coinbase operation
 * 
 * Creates a transaction that shields coinbase UTXOs to an Orchard shielded address.
 * 
 * @param zaddr Target Orchard payment address
 * @return true if transaction was built and sent successfully
 * @throws JSONRPCError if HD seed is not available or transaction fails
 */
bool ShieldToAddress::operator()(const libzcash::OrchardPaymentAddressPirate& zaddr) const
{
    m_op->builder_.SetFee(m_op->fee_);

    // Sending from a t-address, which we don't have an ovk for. Instead,
    // generate a common one from the HD seed. This ensures the data is
    // recoverable, while keeping it logically separate from the ZIP 32
    // Sapling key hierarchy, which the user might not be using.
    HDSeed seed;
    if (!pwalletMain->GetHDSeed(seed)) {
        throw JSONRPCError(
            RPC_WALLET_ERROR,
            "HD seed not found - required for shielding from transparent address");
    }
    uint256 ovk = ovkForShieldingFromTaddr(seed);

    // Add transparent inputs
    for (const auto& utxo : m_op->inputs_) {
        if (utxo.amount >= ASSETCHAINS_TIMELOCKGTE) {
            m_op->builder_.SetLockTime(static_cast<uint32_t>(chainActive.Height()));
            m_op->builder_.AddTransparentInput(COutPoint(utxo.txid, utxo.vout), utxo.scriptPubKey, utxo.amount, 0xfffffffe);
        } else {
            m_op->builder_.AddTransparentInput(COutPoint(utxo.txid, utxo.vout), utxo.scriptPubKey, utxo.amount);
        }
    }

    // Initialize Orchard for transaction building
    m_op->builder_.InitializeOrchard(false, true, uint256());

    // Send all value to the target z-addr
    m_op->builder_.SendChangeTo(zaddr, ovk);

    // Build the transaction
    m_op->tx_ = m_op->builder_.Build().GetTxOrThrow();

    // Send the transaction
    // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
    auto signedTxHex = EncodeHexTx(m_op->tx_);
    if (!m_op->testmode) {
        UniValue params = UniValue(UniValue::VARR);
        params.push_back(signedTxHex);
        UniValue sendResultValue = sendrawtransaction(params, false, CPubKey());
        if (sendResultValue.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");
        }

        auto txid = sendResultValue.get_str();

        UniValue resultObj(UniValue::VOBJ);
        resultObj.push_back(Pair("txid", txid));
        m_op->set_result(resultObj);
    } else {
        // Test mode does not send the transaction to the network.
        UniValue resultObj(UniValue::VOBJ);
        resultObj.push_back(Pair("test", 1));
        resultObj.push_back(Pair("txid", m_op->tx_.GetHash().ToString()));
        resultObj.push_back(Pair("hex", signedTxHex));
        m_op->set_result(resultObj);
    }

    return true;
}

/**
 * @brief Handle invalid encoding address type
 * 
 * This should never be called if address validation is working correctly.
 * 
 * @param no Invalid encoding (unused)
 * @return false - Invalid addresses are not supported
 * @throws JSONRPCError indicating invalid address type
 */
bool ShieldToAddress::operator()(const libzcash::InvalidEncoding& no) const
{
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address encoding for shield coinbase operation");
}

/**
 * @brief Override getStatus() to append operation context to status
 * 
 * Extends the base AsyncRPCOperation status with shield coinbase specific
 * context information including method name and parameters.
 * 
 * @return UniValue object containing status and context information
 */
UniValue AsyncRPCOperation_shieldcoinbase::getStatus() const
{
    UniValue baseStatus = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return baseStatus;
    }

    UniValue statusObj = baseStatus.get_obj();
    statusObj.push_back(Pair("method", "z_shieldcoinbase"));
    statusObj.push_back(Pair("params", contextinfo_));
    return statusObj;
}

/**
 * @brief Lock input UTXOs to prevent concurrent usage
 * 
 * Locks all coinbase UTXOs used as inputs for this operation to prevent
 * them from being used by other operations concurrently.
 * 
 * Thread-safe: Uses wallet and main chain locks.
 */
void AsyncRPCOperation_shieldcoinbase::lock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto& utxo : inputs_) {
        COutPoint outPoint(utxo.txid, utxo.vout);
        pwalletMain->LockCoin(outPoint);
    }
}

/**
 * @brief Unlock input UTXOs after operation completion
 * 
 * Unlocks all coinbase UTXOs that were locked for this operation,
 * making them available for other operations.
 * 
 * Thread-safe: Uses wallet and main chain locks.
 */
void AsyncRPCOperation_shieldcoinbase::unlock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (const auto& utxo : inputs_) {
        COutPoint outPoint(utxo.txid, utxo.vout);
        pwalletMain->UnlockCoin(outPoint);
    }
}
