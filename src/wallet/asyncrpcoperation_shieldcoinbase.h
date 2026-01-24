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

/**
 * @file asyncrpcoperation_shieldcoinbase.h
 * @brief Asynchronous shield coinbase operation
 * 
 * Implements z_shieldcoinbase RPC operation for moving coinbase funds
 * from transparent addresses to shielded addresses for privacy.
 * 
 * Purpose: Shield transparent coinbase UTXOs to shielded addresses
 * Inputs: Mature coinbase UTXOs
 * Outputs: Shielded notes at destination address
 */

#ifndef ASYNCRPCOPERATION_SHIELDCOINBASE_H
#define ASYNCRPCOPERATION_SHIELDCOINBASE_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "primitives/transaction.h"
#include "transaction_builder.h"
#include "wallet.h"
#include "zcash/Address.hpp"
#include "zcash/JoinSplit.hpp"

#include <tuple>
#include <unordered_map>

#include <univalue.h>

#include "paymentdisclosure.h"

// Default transaction fee if caller does not specify one.
#define SHIELD_COINBASE_DEFAULT_MINERS_FEE 10000

using namespace libzcash;

/**
 * Type definitions and structures for shield coinbase operation
 */

/**
 * @struct ShieldCoinbaseUTXO
 * @brief Structure representing a coinbase UTXO to be shielded
 * 
 * This structure contains all the necessary information to spend a coinbase UTXO
 * and shield its value to a shielded address.
 */
struct ShieldCoinbaseUTXO {
    uint256 txid;           ///< Transaction hash containing the UTXO
    int vout;               ///< Output index within the transaction
    CScript scriptPubKey;   ///< Script that locks the UTXO
    CAmount amount;         ///< Value of the UTXO in zatoshis
};

/**
 * @class AsyncRPCOperation_shieldcoinbase
 * @brief Asynchronous RPC operation for shielding coinbase transactions
 * 
 * This class implements the z_shieldcoinbase RPC operation, which moves
 * coinbase funds from transparent addresses to shielded addresses. This
 * operation is necessary because coinbase outputs are initially transparent
 * and must mature before they can be shielded for privacy.
 * 
 * Supported destination types:
 * - Sapling shielded addresses (z-addresses)
 * - Orchard shielded addresses (z-addresses)
 * 
 * Note: Sprout addresses are no longer supported for new shielding operations.
 */
class AsyncRPCOperation_shieldcoinbase : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for shield coinbase operation
     * 
     * @param consensusParams Consensus parameters for the current network
     * @param nHeight Current blockchain height for transaction building
     * @param contextualTx Base transaction context for building
     * @param inputs Vector of coinbase UTXOs to shield
     * @param toAddress Destination shielded address as string
     * @param fee Transaction fee amount (defaults to SHIELD_COINBASE_DEFAULT_MINERS_FEE)
     * @param contextInfo Additional context information for status reporting
     */
    AsyncRPCOperation_shieldcoinbase(
        const Consensus::Params& consensusParams,
        const int nHeight,
        CMutableTransaction contextualTx,
        std::vector<ShieldCoinbaseUTXO> inputs,
        std::string toAddress,
        CAmount fee = SHIELD_COINBASE_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    
    /**
     * @brief Destructor - performs automatic cleanup of resources
     */
    virtual ~AsyncRPCOperation_shieldcoinbase();

    // Prevent copying and moving to ensure single ownership of resources
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase const&) = delete;            // Copy construct
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase&&) = delete;                 // Move construct
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase const&) = delete; // Copy assign
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase&&) = delete;      // Move assign

    /**
     * @brief Main execution entry point for the operation
     * 
     * This function handles the complete lifecycle of the shield operation including:
     * - State management and cancellation checks
     * - Resource locking and cleanup
     * - Exception handling and error reporting
     * - Mining control during execution
     */
    virtual void main();

    /**
     * @brief Get the current status of the operation with context information
     * 
     * @return UniValue object containing operation status and parameters
     */
    virtual UniValue getStatus() const;

    // Configuration flags for testing
    bool testmode = false;                  ///< Set to true to disable sending txs and generating proofs
    // bool paymentDisclosureMode = true; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class ShieldToAddress;
    friend class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase; // Friend class for unit testing

    // Context and configuration
    UniValue contextinfo_;          ///< Optional data to include in return value from getStatus()
    CAmount fee_;                   ///< Transaction fee amount
    PaymentAddress tozaddr_;        ///< Destination shielded payment address

    // Input collection
    std::vector<ShieldCoinbaseUTXO> inputs_;    ///< Coinbase UTXOs to be shielded

    // Transaction building
    TransactionBuilder builder_;    ///< Builder for constructing the transaction
    CTransaction tx_;               ///< Final constructed transaction

    /**
     * @brief Core implementation of the shield operation
     * 
     * @return true if transaction was successfully built and sent, false otherwise
     * @throws JSONRPCError for various error conditions
     */
    bool main_impl();

    // Resource management functions
    
    /**
     * @brief Lock coinbase UTXOs to prevent double-spending
     */
    void lock_utxos();

    /**
     * @brief Unlock coinbase UTXOs to make them available again
     */
    void unlock_utxos();
};

/**
 * @class ShieldToAddress
 * @brief Visitor pattern implementation for handling different shielded address types
 * 
 * This class uses the boost::static_visitor pattern to handle shielding to different
 * types of shielded addresses (Sapling, Orchard) with type-specific logic for each.
 * The visitor pattern allows for clean separation of address-type-specific code.
 */
class ShieldToAddress : public boost::static_visitor<bool>
{
private:
    AsyncRPCOperation_shieldcoinbase* m_op;     ///< Pointer to the parent operation
    CAmount sendAmount;                         ///< Amount to send after fees

public:
    /**
     * @brief Constructor for the address visitor
     * 
     * @param op Pointer to the parent shield coinbase operation
     * @param sendAmount The amount to send to the shielded address (after fees)
     */
    ShieldToAddress(AsyncRPCOperation_shieldcoinbase* op, CAmount sendAmount) : m_op(op), sendAmount(sendAmount) {}

    /**
     * @brief Handler for Sprout addresses (deprecated)
     * 
     * @param zaddr Sprout payment address
     * @return false (Sprout is no longer supported)
     */
    bool operator()(const libzcash::SproutPaymentAddress& zaddr) const;
    
    /**
     * @brief Handler for Sapling addresses
     * 
     * @param zaddr Sapling payment address
     * @return true if transaction was successfully built and sent
     */
    bool operator()(const libzcash::SaplingPaymentAddress& zaddr) const;
    
    /**
     * @brief Handler for Orchard addresses
     * 
     * @param zaddr Orchard payment address
     * @return true if transaction was successfully built and sent
     */
    bool operator()(const libzcash::OrchardPaymentAddressPirate& zaddr) const;
    
    /**
     * @brief Handler for invalid address encoding
     * 
     * @param no Invalid encoding object
     * @return false (invalid addresses cannot be used)
     */
    bool operator()(const libzcash::InvalidEncoding& no) const;
};


/**
 * @class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase
 * @brief Friend class proxy for testing private methods
 * 
 * This class provides controlled access to private methods and members
 * of AsyncRPCOperation_shieldcoinbase for unit testing purposes.
 * It acts as a proxy to enable white-box testing without exposing
 * implementation details to production code.
 */
class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase
{
public:
    std::shared_ptr<AsyncRPCOperation_shieldcoinbase> delegate;

    /**
     * @brief Constructor - takes ownership of the operation instance
     * 
     * @param ptr Shared pointer to the operation instance to test
     */
    TEST_FRIEND_AsyncRPCOperation_shieldcoinbase(std::shared_ptr<AsyncRPCOperation_shieldcoinbase> ptr) : delegate(ptr) {}

    // Accessor methods for testing
    
    /**
     * @brief Get the constructed transaction for verification
     * 
     * @return The final transaction object
     */
    CTransaction getTx()
    {
        return delegate->tx_;
    }

    /**
     * @brief Set the transaction for testing scenarios
     * 
     * @param tx Transaction to set
     */
    void setTx(CTransaction tx)
    {
        delegate->tx_ = tx;
    }

    // Delegated method proxies for testing private functionality

    /**
     * @brief Test the core implementation logic
     * 
     * @return Success status for testing validation
     */
    bool main_impl()
    {
        return delegate->main_impl();
    }

    /**
     * @brief Set the operation state for testing scenarios
     * 
     * @param state Operation state to set
     */
    void set_state(OperationStatus state)
    {
        delegate->state_.store(state);
    }
};


#endif /* ASYNCRPCOPERATION_SHIELDCOINBASE_H */
