// Copyright (c) 2025 Pirate Chain Development Team
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_ADDRESS_H
#define ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_ADDRESS_H

/**
 * @file asyncrpcoperation_orchardconsolidation_address.h
 * @brief Asynchronous RPC operation for consolidating Orchard notes to a single address
 * 
 * This file defines the AsyncRPCOperation_orchardconsolidation_address class, which implements
 * an asynchronous operation for consolidating multiple small Orchard notes into fewer, larger
 * notes at the same address. This helps reduce wallet fragmentation and improves transaction
 * performance by reducing the number of notes that need to be processed in future transactions.
 * 
 * The consolidation process:
 * 1. Identifies all notes at the specified Orchard address
 * 2. Groups them into batches (respecting maxNotes and maxTransactions limits)
 * 3. Creates consolidation transactions that spend multiple small notes and create one larger note
 * 4. Uses intelligent note selection to optimize for fee coverage and batch efficiency
 * 
 * @author Pirate Chain Development Team
 * @date 2025
 */

#include "amount.h"
#include "asyncrpcoperation.h"
#include "univalue.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"

/**
 * @class AsyncRPCOperation_orchardconsolidation_address
 * @brief Asynchronous operation for consolidating Orchard notes at a specific address
 * 
 * This class implements an asynchronous RPC operation that consolidates multiple Orchard notes
 * at a given address into fewer, larger notes. The operation runs in the background and can
 * be monitored using the standard async RPC operation status mechanisms.
 * 
 * Key features:
 * - Intelligent two-step note selection algorithm (fee coverage first, then optimization)
 * - Configurable transaction and note limits to control operation scope
 * - Maintains at least one unconsolidated note for immediate spending
 * - Handles wallet locking gracefully by capturing spending keys during construction
 * - Respects network upgrade boundaries to avoid transaction expiry issues
 * 
 * Security considerations:
 * - Requires wallet to be unlocked during operation creation to extract spending keys
 * - Spending keys are held in memory only during operation execution
 * - Uses minimum confirmation depth of 11 blocks for note selection
 * - Validates all cryptographic proofs before transaction commitment
 */
class AsyncRPCOperation_orchardconsolidation_address : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructs a new Orchard address consolidation operation
     * 
     * @param targetHeight The blockchain height at which to create transactions
     * @param address The Orchard payment address to consolidate notes for
     * @param spendingKey The extended spending key for the address (must be valid)
     * @param fee The fee amount in zatoshis to pay per consolidation transaction
     * @param maxNotes Maximum number of notes to include in a single consolidation transaction
     * @param maxTransactions Maximum number of consolidation transactions to create (default: 10)
     * 
     * @note The spending key is captured during construction while the wallet is unlocked,
     *       allowing the operation to proceed even if the wallet is locked afterwards.
     * @note A fee of 10000 zatoshis (0.0001 ARRR) is recommended for reliable network acceptance.
     */
    AsyncRPCOperation_orchardconsolidation_address(int targetHeight, 
                                                   const libzcash::OrchardPaymentAddressPirate& address,
                                                   const libzcash::OrchardExtendedSpendingKeyPirate& spendingKey,
                                                   CAmount fee, 
                                                   int maxNotes,
                                                   int maxTransactions = 10);
    
    /**
     * @brief Destructor - cleans up resources and clears sensitive data
     */
    virtual ~AsyncRPCOperation_orchardconsolidation_address();

    // We don't want to be copied or moved around
    AsyncRPCOperation_orchardconsolidation_address(AsyncRPCOperation_orchardconsolidation_address const&) = delete;
    AsyncRPCOperation_orchardconsolidation_address(AsyncRPCOperation_orchardconsolidation_address&&) = delete;
    AsyncRPCOperation_orchardconsolidation_address& operator=(AsyncRPCOperation_orchardconsolidation_address const&) = delete;
    AsyncRPCOperation_orchardconsolidation_address& operator=(AsyncRPCOperation_orchardconsolidation_address&&) = delete;

    /**
     * @brief Main entry point for the consolidation operation
     * 
     * This method orchestrates the entire consolidation process, including error handling
     * and status reporting. It calls main_impl() to perform the actual work and ensures
     * proper state transitions regardless of success or failure.
     */
    virtual void main();
    
    /**
     * @brief Cancels the consolidation operation
     * 
     * Sets the operation status to CANCELLED. Note that transactions already committed
     * to the blockchain cannot be reversed.
     */
    virtual void cancel();
    
    /**
     * @brief Gets the current status of the consolidation operation
     * 
     * @return UniValue object containing operation status, progress, and configuration details
     */
    virtual UniValue getStatus() const;

private:
    int targetHeight_;                                        ///< Blockchain height for transaction creation
    libzcash::OrchardPaymentAddressPirate address_;          ///< Target address for consolidation
    libzcash::OrchardExtendedSpendingKeyPirate spendingKey_; ///< Spending key (captured during construction)
    CAmount fee_;                                            ///< Fee per transaction in zatoshis
    int maxNotes_;                                           ///< Maximum notes per transaction
    int maxTransactions_;                                    ///< Maximum transactions to create

    /**
     * @brief Core implementation of the consolidation logic
     * 
     * Performs the actual consolidation work:
     * 1. Validates network upgrade timing
     * 2. Retrieves and filters notes for the target address
     * 3. Implements intelligent note selection algorithm
     * 4. Creates and commits consolidation transactions
     * 5. Updates operation result with transaction details
     * 
     * @return true if consolidation completed successfully, false otherwise
     * @throws UniValue on RPC errors, runtime_error on transaction failures
     */
    bool main_impl();
    
    /**
     * @brief Sets the final result of the consolidation operation
     * 
     * @param numTxCreated Number of consolidation transactions created
     * @param amountConsolidated Total amount consolidated (excluding fees)
     * @param consolidationTxIds Vector of transaction IDs created during consolidation
     * @param notesConsolidated Number of notes that were consolidated
     * @param notesRemaining Number of notes remaining after consolidation
     */
    void setConsolidationResult(int numTxCreated, const CAmount& amountConsolidated, const std::vector<std::string>& consolidationTxIds, int notesConsolidated, int notesRemaining);
};

#endif /* ASYNCRPCOPERATION_ORCHARDCONSOLIDATION_ADDRESS_H */
