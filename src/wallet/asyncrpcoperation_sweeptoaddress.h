// Copyright (c) 2022-2025 Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_sweeptoaddress.h
 * @brief Asynchronous sweep-to-address operation
 * 
 * Implements consolidation of funds from multiple addresses into a single
 * destination address. Supports both Sapling and Orchard protocols with
 * intelligent consolidation strategies.
 * 
 * Purpose: Consolidate funds from multiple addresses to single destination
 * Inputs: Multiple addresses with fragmented funds
 * Outputs: Consolidated funds at single destination address
 */

#ifndef ASYNCRPCOPERATION_SWEEPTOADDRESS_H
#define ASYNCRPCOPERATION_SWEEPTOADDRESS_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "univalue.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

// Default fee used for sweep transactions
static const CAmount DEFAULT_SWEEP_FEE = 10000;
extern CAmount fSweepTxFee;
extern bool fSweepMapUsed;
extern std::optional<libzcash::SaplingPaymentAddress> rpcSaplingSweepAddress;
extern std::optional<libzcash::OrchardPaymentAddressPirate> rpcOrchardSweepAddress;

/**
 * @class AsyncRPCOperation_sweeptoaddress
 * @brief Asynchronous sweep-to-address operation
 * 
 * Consolidates funds from multiple addresses into a single destination address.
 * Supports Sapling and Orchard protocols with spending-key-based consolidation.
 * 
 * Purpose: Sweep fragmented funds to single destination
 * Inputs: Multiple addresses with notes/UTXOs
 * Outputs: Consolidated funds at destination address
 */
class AsyncRPCOperation_sweeptoaddress : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for sweep-to-address operation
     * 
     * @param targetHeight Blockchain height for transaction targeting
     * @param fromRpc True if initiated from RPC call, false for background operation
     */
    AsyncRPCOperation_sweeptoaddress(int targetHeight, bool fromRpc = false);
    
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_sweeptoaddress();

    // We don't want to be copied or moved around
    AsyncRPCOperation_sweeptoaddress(AsyncRPCOperation_sweeptoaddress const&) = delete;            // Copy construct
    AsyncRPCOperation_sweeptoaddress(AsyncRPCOperation_sweeptoaddress&&) = delete;                 // Move construct
    AsyncRPCOperation_sweeptoaddress& operator=(AsyncRPCOperation_sweeptoaddress const&) = delete; // Copy assign
    AsyncRPCOperation_sweeptoaddress& operator=(AsyncRPCOperation_sweeptoaddress&&) = delete;      // Move assign

    /**
     * @brief Main execution entry point for sweep operation
     * 
     * Handles complete sweep lifecycle including note discovery,
     * transaction construction, and broadcasting.
     */
    virtual void main();

    /**
     * @brief Cancel the sweep operation
     * 
     * Sets operation state to cancelled, stopping further processing.
     */
    virtual void cancel();

    /**
     * @brief Get current operation status
     * 
     * @return UniValue object containing operation status and progress
     */
    virtual UniValue getStatus() const;

    /**
     * @brief Set Sapling destination address for RPC-initiated sweeps
     * 
     * @param address Sapling payment address to sweep funds to
     */
    void setSaplingSweepAddress(const libzcash::SaplingPaymentAddress& address);
    
    /**
     * @brief Set Orchard destination address for RPC-initiated sweeps
     * 
     * @param address Orchard payment address to sweep funds to
     */
    void setOrchardSweepAddress(const libzcash::OrchardPaymentAddressPirate& address);

private:
    int targetHeight_;    ///< Target blockchain height for transactions
    bool fromRPC_;        ///< True if initiated from RPC call

    /**
     * @brief Core implementation of sweep logic
     * 
     * @return true if sweep completed successfully, false otherwise
     */
    bool main_impl();

    /**
     * @brief Set sweep operation results
     * 
     * @param numTxCreated Number of sweep transactions created
     * @param amountSwept Total amount swept across all transactions
     * @param sweepTxIds Vector of transaction IDs for created transactions
     */
    void setSweepResult(int numTxCreated, const CAmount& amountSwept, const std::vector<std::string>& sweepTxIds);
};

#endif /* ASYNCRPCOPERATION_SWEEPTOADDRESS_H */
