// Copyright (c) 2022-2025 Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_ironwoodconsolidation.h
 * @brief Asynchronous Ironwood note consolidation operation
 * 
 * Implements automatic consolidation of Ironwood shielded notes to reduce
 * wallet fragmentation and improve transaction performance. Combines multiple
 * notes from the same addresses into fewer, larger notes while preserving
 * Ironwood protocol privacy properties.
 * 
 * Purpose: Optimize wallet performance by reducing note fragmentation
 * Inputs: Multiple Ironwood notes from addresses with excessive fragmentation
 * Outputs: Fewer, consolidated Ironwood notes at the same addresses
 */

#ifndef ASYNCRPCOPERATION_IRONWOODCONSOLIDATION_H
#define ASYNCRPCOPERATION_IRONWOODCONSOLIDATION_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "rpc/server.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

// Forward declarations
struct IronwoodNoteEntry;

/**
 * Default transaction fee for Ironwood consolidation operations
 * Set to 10,000 zatoshis (0.0001 ARRR)
 */
static const CAmount DEFAULT_IRONWOOD_CONSOLIDATION_FEE = 10000;

/**
 * Default interval in minutes between automatic Ironwood note consolidation
 * Set to 1 week (10080 minutes)
 */
static const int DEFAULT_IRONWOOD_CONSOLIDATION_INTERVAL = 10080; // 1 week in minutes

/**
 * Global Ironwood consolidation fee setting
 * Can be modified via configuration parameters
 */
extern CAmount fIronwoodConsolidationTxFee;

/**
 * Flag indicating whether Ironwood consolidation address mapping is used
 * When true, only specific addresses from configuration are consolidated
 */
extern bool fIronwoodConsolidationMapUsed;

/**
 * @class AsyncRPCOperation_ironwoodconsolidation
 * @brief Asynchronous RPC operation for consolidating Ironwood notes
 * 
 * Consolidates multiple Ironwood shielded notes into fewer, larger notes
 * to improve wallet performance and reduce transaction sizes.
 * 
 * Purpose: Reduce wallet fragmentation by consolidating Ironwood notes
 * Inputs: Multiple fragmented Ironwood notes from addresses
 * Outputs: Fewer consolidated Ironwood notes at same addresses
 */
class AsyncRPCOperation_ironwoodconsolidation : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for Ironwood consolidation operation
     * 
     * @param targetHeight Blockchain height for transaction targeting and expiration
     */
    AsyncRPCOperation_ironwoodconsolidation(int targetHeight);
    
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_ironwoodconsolidation();

    // Prevent copying and moving to ensure single ownership of resources
    AsyncRPCOperation_ironwoodconsolidation(AsyncRPCOperation_ironwoodconsolidation const&) = delete;            // Copy construct
    AsyncRPCOperation_ironwoodconsolidation(AsyncRPCOperation_ironwoodconsolidation&&) = delete;                 // Move construct
    AsyncRPCOperation_ironwoodconsolidation& operator=(AsyncRPCOperation_ironwoodconsolidation const&) = delete; // Copy assign
    AsyncRPCOperation_ironwoodconsolidation& operator=(AsyncRPCOperation_ironwoodconsolidation&&) = delete;      // Move assign

    /**
     * @brief Main execution entry point for the consolidation operation
     * 
     * Handles complete lifecycle including state management, exception handling,
     * timing, and result compilation.
     */
    virtual void main();

    /**
     * @brief Cancel the consolidation operation
     * 
     * Sets operation state to cancelled, stopping further processing.
     * Safe to call at any time during operation execution.
     */
    virtual void cancel();

    /**
     * @brief Get the current status of the operation with context information
     * 
     * @return UniValue object containing operation status, method name, and target height
     */
    virtual UniValue getStatus() const;

private:
    // Configuration
    int targetHeight_;      ///< Target blockchain height for consolidation transactions

    // Core implementation methods

    /**
     * @brief Core implementation of Ironwood consolidation logic
     * 
     * Performs note scanning, transaction building, and consolidation
     * with Ironwood protocol support and cleanup mode handling.
     * 
     * @return true if consolidation completed successfully, false otherwise
     * @throws JSONRPCError for various error conditions
     */
    bool main_impl();

    /**
     * @brief Set the Ironwood consolidation operation results
     * 
     * @param numTxCreated Number of consolidation transactions created
     * @param amountConsolidated Total amount consolidated in zatoshis
     * @param consolidationTxIds Vector of transaction IDs created during consolidation
     */
    void setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds);

private:
};

#endif /* ASYNCRPCOPERATION_IRONWOODCONSOLIDATION_H */
