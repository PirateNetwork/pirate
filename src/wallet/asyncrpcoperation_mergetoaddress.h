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
 * @file asyncrpcoperation_mergetoaddress.h
 * @brief Asynchronous merge-to-address operation
 * 
 * Implements z_mergetoaddress RPC operation for consolidating multiple
 * UTXOs and shielded notes into a single output address.
 * 
 * Purpose: Consolidate fragmented funds into single output
 * Inputs: Multiple UTXOs/notes from various sources
 * Outputs: Single consolidated output at destination address
 */

#ifndef ASYNCRPCOPERATION_MERGETOADDRESS_H
#define ASYNCRPCOPERATION_MERGETOADDRESS_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "paymentdisclosure.h"
#include "primitives/transaction.h"
#include "transaction_builder.h"
#include "wallet.h"
#include "zcash/Address.hpp"
#include "zcash/JoinSplit.hpp"

#include <array>
#include <tuple>
#include <unordered_map>

#include <univalue.h>

// Default transaction fee if caller does not specify one.
#define MERGE_TO_ADDRESS_OPERATION_DEFAULT_MINERS_FEE 10000

using namespace libzcash;

/**
 * Type definitions for merge-to-address operation inputs and outputs
 */

// Input UTXO is a tuple of (outpoint, amount, scriptPubKey)
typedef std::tuple<COutPoint, CAmount, CScript> MergeToAddressInputUTXO;

// Legacy Sprout note input (deprecated, kept for reference)
// typedef std::tuple<JSOutPoint, SproutNote, CAmount, SproutSpendingKey> MergeToAddressInputSproutNote;

// Sapling note input is a tuple of (outpoint, note, amount, extended_spending_key)
typedef std::tuple<SaplingOutPoint, SaplingNote, CAmount, SaplingExtendedSpendingKey> MergeToAddressInputSaplingNote;

// Orchard note input is a tuple of (outpoint, note, amount, extended_spending_key)
typedef std::tuple<OrchardOutPoint, OrchardNote, CAmount, OrchardExtendedSpendingKeyPirate> MergeToAddressInputOrchardNote;

// A recipient is a tuple of (address, memo) where memo is optional for transparent addresses
typedef std::tuple<std::string, std::string> MergeToAddressRecipient;

/**
 * @class AsyncRPCOperation_mergetoaddress
 * @brief Asynchronous merge-to-address operation
 * 
 * Consolidates multiple UTXOs and shielded notes into a single output address.
 * Supports transparent UTXOs, Sapling notes, and Orchard notes as inputs.
 * 
 * Purpose: Consolidate fragmented funds to reduce wallet complexity
 * Inputs: Multiple UTXOs/notes from various sources
 * Outputs: Single consolidated output at destination address
 */
class AsyncRPCOperation_mergetoaddress : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for merge-to-address operation
     * 
     * @param consensusParams Network consensus parameters
     * @param nHeight Current blockchain height
     * @param contextualTx Base transaction context
     * @param utxoInputs Transparent UTXO inputs to merge
     * @param saplingNoteInputs Sapling note inputs to merge
     * @param orchardNoteInputs Orchard note inputs to merge
     * @param recipient Destination address and memo
     * @param fee Transaction fee amount
     * @param contextInfo Context information for status reporting
     */
    AsyncRPCOperation_mergetoaddress(
        const Consensus::Params& consensusParams,
        const int nHeight,
        CMutableTransaction contextualTx,
        std::vector<MergeToAddressInputUTXO> utxoInputs,
        std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs,
        std::vector<MergeToAddressInputOrchardNote> orchardNoteInputs,
        MergeToAddressRecipient recipient,
        CAmount fee = MERGE_TO_ADDRESS_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_mergetoaddress();

    // Prevent copying and moving to ensure single ownership of resources
    AsyncRPCOperation_mergetoaddress(AsyncRPCOperation_mergetoaddress const&) = delete;            
    AsyncRPCOperation_mergetoaddress(AsyncRPCOperation_mergetoaddress&&) = delete;                 
    AsyncRPCOperation_mergetoaddress& operator=(AsyncRPCOperation_mergetoaddress const&) = delete; 
    AsyncRPCOperation_mergetoaddress& operator=(AsyncRPCOperation_mergetoaddress&&) = delete;      

    /**
     * @brief Main execution entry point
     * Handles complete merge operation lifecycle including resource locking and exception handling
     */
    virtual void main();

    /**
     * @brief Get operation status with context information
     * 
     * @return UniValue object containing operation status and parameters
     */
    virtual UniValue getStatus() const;

    // Configuration flags for testing and privacy
    bool testmode = false;                  ///< Set to true to disable sending txs and generating proofs
    bool paymentDisclosureMode = true;      ///< Set to true to save esk for encrypted notes in payment disclosure database

private:
    friend class TEST_FRIEND_AsyncRPCOperation_mergetoaddress; // Friend class for unit testing

    // Context and configuration
    UniValue contextinfo_;                  ///< Optional data to include in return value from getStatus()
    uint32_t consensusBranchId_;           ///< Current consensus branch ID for transaction building
    CAmount fee_;                          ///< Transaction fee amount
    int mindepth_;                         ///< Minimum depth for input confirmation
    
    // Recipient information
    MergeToAddressRecipient recipient_;    ///< Destination address and memo
    bool isToTaddr_;                       ///< True if sending to transparent address
    bool isToZaddr_;                       ///< True if sending to shielded address
    CTxDestination toTaddr_;               ///< Transparent destination address
    PaymentAddress toPaymentAddress_;      ///< Shielded destination address

    // Input collections
    std::vector<MergeToAddressInputUTXO> utxoInputs_;                    ///< Transparent UTXO inputs
    std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs_;      ///< Sapling note inputs
    std::vector<MergeToAddressInputOrchardNote> orchardNoteInputs_;      ///< Orchard note inputs

    // Transaction building
    TransactionBuilder builder_;           ///< Builder for constructing the transaction
    CTransaction tx_;                      ///< Final constructed transaction

    /**
     * @brief Convert hexadecimal string to memo array
     * 
     * @param hexString Hexadecimal representation of the memo
     * @return Fixed-size array containing the memo bytes
     * @throws JSONRPCError if hex string is malformed or too large
     */
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string hexString);
    
    /**
     * @brief Core implementation of the merge operation
     * 
     * @return true if transaction was successfully built and sent, false otherwise
     * @throws JSONRPCError for various error conditions
     */
    bool main_impl();

    // Resource management functions
    
    /**
     * @brief Lock transparent UTXOs to prevent double-spending
     */
    void lock_utxos();

    /**
     * @brief Unlock transparent UTXOs to make them available again
     */
    void unlock_utxos();

    /**
     * @brief Lock Sapling notes to prevent double-spending
     */
    void lock_sapling_notes();

    /**
     * @brief Unlock Sapling notes to make them available again
     */
    void unlock_sapling_notes();

    /**
     * @brief Lock Orchard notes to prevent double-spending
     */
    void lock_orchard_notes();

    /**
     * @brief Unlock Orchard notes to make them available again
     */
    void unlock_orchard_notes();

};

/**
 * @class TEST_FRIEND_AsyncRPCOperation_mergetoaddress
 * @brief Friend class proxy for testing private methods
 * 
 * This class provides controlled access to private methods and members
 * of AsyncRPCOperation_mergetoaddress for unit testing purposes.
 * It acts as a proxy to enable white-box testing without exposing
 * implementation details to production code.
 */
class TEST_FRIEND_AsyncRPCOperation_mergetoaddress
{
public:
    std::shared_ptr<AsyncRPCOperation_mergetoaddress> delegate;

    /**
     * @brief Constructor - takes ownership of the operation instance
     * 
     * @param ptr Shared pointer to the operation instance to test
     */
    TEST_FRIEND_AsyncRPCOperation_mergetoaddress(std::shared_ptr<AsyncRPCOperation_mergetoaddress> ptr) : delegate(ptr) {}

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
     * @brief Test the hex string to memo conversion
     * 
     * @param hexString Hexadecimal string to convert
     * @return Memo array for testing validation
     */
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string hexString)
    {
        return delegate->get_memo_from_hex_string(hexString);
    }

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


#endif // ASYNCRPCOPERATION_MERGETOADDRESS_H
