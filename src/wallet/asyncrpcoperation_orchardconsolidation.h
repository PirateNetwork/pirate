// Copyright (c) 2022-2025 Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file asyncrpcoperation_orchardconsolidation.h
 * @brief Asynchronous Orchard note consolidation operation
 * 
 * Implements automatic consolidation of Orchard shielded notes to reduce
 * wallet fragmentation and improve transaction performance. Combines multiple
 * notes from the same addresses into fewer, larger notes while preserving
 * Orchard protocol privacy properties.
 * 
 * Purpose: Optimize wallet performance by reducing note fragmentation
 * Inputs: Multiple Orchard notes from addresses with excessive fragmentation
 * Outputs: Fewer, consolidated Orchard notes at the same addresses
 */

#ifndef ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_H
#define ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "rpc/server.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

// Forward declarations
struct OrchardNoteEntry;

/**
 * Default transaction fee for Orchard consolidation operations
 * Set to 10,000 zatoshis (0.0001 ARRR)
 */
static const CAmount DEFAULT_ORCHARD_CONSOLIDATION_FEE = 10000;

/**
 * Default interval in minutes between automatic Orchard note consolidation
 * Set to 1 week (10080 minutes)
 */
static const int DEFAULT_ORCHARD_CONSOLIDATION_INTERVAL = 10080; // 1 week in minutes

/**
 * Global Orchard consolidation fee setting
 * Can be modified via configuration parameters
 */
extern CAmount fOrchardConsolidationTxFee;

/**
 * Flag indicating whether Orchard consolidation address mapping is used
 * When true, only specific addresses from configuration are consolidated
 */
extern bool fOrchardConsolidationMapUsed;

/**
 * @class AsyncRPCOperation_orchardconsolidation
 * @brief Asynchronous RPC operation for consolidating Orchard notes
 * 
 * Consolidates multiple Orchard shielded notes into fewer, larger notes
 * to improve wallet performance and reduce transaction sizes.
 * 
 * Purpose: Reduce wallet fragmentation by consolidating Orchard notes
 * Inputs: Multiple fragmented Orchard notes from addresses
 * Outputs: Fewer consolidated Orchard notes at same addresses
 */
class AsyncRPCOperation_orchardconsolidation : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for Orchard consolidation operation
     * 
     * @param targetHeight Blockchain height for transaction targeting and expiration
     */
    AsyncRPCOperation_orchardconsolidation(int targetHeight);
    
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_orchardconsolidation();

    // Prevent copying and moving to ensure single ownership of resources
    AsyncRPCOperation_orchardconsolidation(AsyncRPCOperation_orchardconsolidation const&) = delete;            // Copy construct
    AsyncRPCOperation_orchardconsolidation(AsyncRPCOperation_orchardconsolidation&&) = delete;                 // Move construct
    AsyncRPCOperation_orchardconsolidation& operator=(AsyncRPCOperation_orchardconsolidation const&) = delete; // Copy assign
    AsyncRPCOperation_orchardconsolidation& operator=(AsyncRPCOperation_orchardconsolidation&&) = delete;      // Move assign

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
     * @brief Core implementation of Orchard consolidation logic
     * 
     * Performs note scanning, transaction building, and consolidation
     * with Orchard protocol support and cleanup mode handling.
     * 
     * @return true if consolidation completed successfully, false otherwise
     * @throws JSONRPCError for various error conditions
     */
    bool main_impl();

    /**
     * @brief Set the Orchard consolidation operation results
     * 
     * @param numTxCreated Number of consolidation transactions created
     * @param amountConsolidated Total amount consolidated in zatoshis
     * @param consolidationTxIds Vector of transaction IDs created during consolidation
     */
    void setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds);

    /**
     * @brief Get Orchard extended spending key for address
     * 
     * Retrieves the extended spending key associated with an Orchard address
     * for note detection and spending operations.
     * 
     * @param address The Orchard payment address
     * @param spendingKey Output parameter for the extended spending key
     * @return true if key was found, false otherwise
     */
    bool getOrchardExtendedSpendingKey(const libzcash::OrchardPaymentAddressPirate& address, 
                                     libzcash::OrchardExtendedSpendingKeyPirate& spendingKey);

private:
    /**
     * @brief Lock Orchard notes to prevent double-spending
     * 
     * Locks selected Orchard notes to prevent concurrent operations
     * from using them during consolidation.
     * 
     * @param selectedInputs Vector of Orchard note entries to lock
     */
    void lockOrchardNotes(const std::vector<OrchardNoteEntry>& selectedInputs);

    /**
     * @brief Unlock Orchard notes after operation completion
     * 
     * Releases locks on Orchard notes once consolidation is complete
     * or cancelled, making them available for other operations.
     * 
     * @param selectedInputs Vector of Orchard note entries to unlock
     */
    void unlockOrchardNotes(const std::vector<OrchardNoteEntry>& selectedInputs);
};

#endif /* ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_H */
