// Copyright (c) 2022-2025 Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_saplingconsolidation.h
 * @brief Asynchronous Sapling note consolidation operation
 * 
 * Implements automatic consolidation of Sapling shielded notes to reduce
 * wallet fragmentation and improve transaction performance.
 * 
 * Purpose: Consolidate fragmented Sapling notes into fewer, larger notes
 * Inputs: Multiple Sapling notes from addresses with fragmentation
 * Outputs: Consolidated Sapling notes at the same addresses
 */

#ifndef ASYNCRPCOPERATION_SAPLINGCONSOLIDATION_H
#define ASYNCRPCOPERATION_SAPLINGCONSOLIDATION_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "rpc/server.h"
#include "univalue.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

/**
 * Default transaction fee for Sapling consolidation operations
 * Set to 10,000 zatoshis (0.0001 ARRR)
 */
static const CAmount DEFAULT_SAPLING_CONSOLIDATION_FEE = 10000;

/**
 * Default interval in minutes between automatic Sapling note consolidation
 * Set to 1 week (10080 minutes)
 */
static const int DEFAULT_SAPLING_CONSOLIDATION_INTERVAL = 10080; // 1 week in minutes

/**
 * Global Sapling consolidation fee setting
 * Can be modified via configuration parameters
 */
extern CAmount fSaplingConsolidationTxFee;

/**
 * Flag indicating whether Sapling consolidation address mapping is used
 * When true, only specific addresses from configuration are consolidated
 */
extern bool fSaplingConsolidationMapUsed;

/**
 * @class AsyncRPCOperation_saplingconsolidation
 * @brief Asynchronous Sapling note consolidation operation
 * 
 * Consolidates multiple Sapling shielded notes to reduce wallet fragmentation.
 * Combines notes from addresses with multiple notes into fewer, larger notes.
 * 
 * Purpose: Reduce note fragmentation for improved wallet performance
 * Inputs: Multiple Sapling notes from fragmented addresses
 * Outputs: Consolidated Sapling notes at the same addresses
 */
class AsyncRPCOperation_saplingconsolidation : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for Sapling consolidation operation
     * 
     * @param targetHeight Blockchain height for transaction targeting
     */
    AsyncRPCOperation_saplingconsolidation(int targetHeight);
    
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_saplingconsolidation();

    // Prevent copying and moving to ensure single ownership of resources
    AsyncRPCOperation_saplingconsolidation(AsyncRPCOperation_saplingconsolidation const&) = delete;            
    AsyncRPCOperation_saplingconsolidation(AsyncRPCOperation_saplingconsolidation&&) = delete;                 
    AsyncRPCOperation_saplingconsolidation& operator=(AsyncRPCOperation_saplingconsolidation const&) = delete; 
    AsyncRPCOperation_saplingconsolidation& operator=(AsyncRPCOperation_saplingconsolidation&&) = delete;      

    /**
     * @brief Main execution entry point for consolidation operation
     * Performs note discovery, transaction construction, and broadcasting
     */
    virtual void main();

    /**
     * @brief Cancel the consolidation operation
     * Sets operation state to cancelled, stopping further processing
     */
    virtual void cancel();

    /**
     * @brief Get current operation status with consolidation details
     * 
     * @return UniValue object containing operation status and progress
     */
    virtual UniValue getStatus() const;

private:
    int targetHeight_;    ///< Target blockchain height for transactions

    /**
     * @brief Core implementation of consolidation logic
     * 
     * @return true if consolidation completed successfully, false otherwise
     */
    bool main_impl();

    /**
     * @brief Set consolidation operation results
     * 
     * @param numTxCreated Number of consolidation transactions created
     * @param amountConsolidated Total amount consolidated across all transactions
     * @param consolidationTxIds Vector of transaction IDs for created transactions
     */
    void setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds);
};

#endif /* ASYNCRPCOPERATION_SAPLINGCONSOLIDATION_H */
