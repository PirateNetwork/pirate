// Copyright (c) 2016 The Zcash developers
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
 * @file asyncrpcoperation_sendmany.h
 * @brief Asynchronous sendmany operation for multi-recipient transactions
 * 
 * Implements z_sendmany RPC operation for sending funds to multiple recipients.
 * Supports transparent, Sapling, and Orchard addresses with memo support.
 * 
 * Purpose: Send funds to multiple recipients in a single transaction
 * Inputs: Source address funds (transparent/Sapling/Orchard)
 * Outputs: Multiple recipient outputs with optional memos
 */

#ifndef ASYNCRPCOPERATION_SENDMANY_H
#define ASYNCRPCOPERATION_SENDMANY_H

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

// Default transaction fee for sendmany operations
#define ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE 10000

using namespace libzcash;

// Recipient tuple: (address, amount, memo)
typedef std::tuple<std::string, CAmount, std::string> SendManyRecipient;

// Input UTXO tuple: (txid, vout, amount, coinbase, destination)
typedef std::tuple<uint256, int, CAmount, bool, CTxDestination> SendManyInputUTXO;

/**
 * @class AsyncRPCOperation_sendmany
 * @brief Asynchronous sendmany operation for multi-recipient transactions
 * 
 * Implements the z_sendmany RPC operation, allowing users to send funds
 * from a single source address to multiple recipients in one transaction.
 * Supports all address types (transparent, Sapling, Orchard) with memos.
 * 
 * Purpose: Send funds to multiple recipients efficiently
 * Inputs: Source address funds (any protocol)
 * Outputs: Multiple recipient outputs with optional encrypted memos
 */
class AsyncRPCOperation_sendmany : public AsyncRPCOperation
{
public:
    /**
     * @brief Constructor for sendmany operation
     * 
     * @param consensusParams Consensus parameters for the current network
     * @param nHeight Current blockchain height
     * @param fromAddress Source address for funds
     * @param saplingOutputs Vector of Sapling recipients
     * @param orchardOutputs Vector of Orchard recipients
     * @param minDepth Minimum confirmation depth for inputs
     * @param fee Transaction fee amount
     * @param contextInfo Additional context information
     */
    AsyncRPCOperation_sendmany(
        const Consensus::Params& consensusParams,
        const int nHeight,
        std::string fromAddress,
        std::vector<SendManyRecipient> saplingOutputs,
        std::vector<SendManyRecipient> orchardOutputs,
        int minDepth,
        CAmount fee = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
        
    /**
     * @brief Destructor with automatic resource cleanup
     */
    virtual ~AsyncRPCOperation_sendmany();

    // Prevent copying and moving to ensure single ownership of transaction resources
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany const&) = delete;            
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany&&) = delete;                 
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany const&) = delete; 
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany&&) = delete;      

    /**
     * @brief Main execution entry point for sendmany operation
     * Constructs and broadcasts the multi-recipient transaction
     */
    virtual void main();

    /**
     * @brief Get current operation status with transaction details
     * 
     * @return UniValue object containing operation status and transaction info
     */
    virtual UniValue getStatus() const;

    bool testmode = false;                ///< Enable test mode for debugging
    bool paymentDisclosureMode = true;    ///< Enable payment disclosure tracking

private:
    friend class TEST_FRIEND_AsyncRPCOperation_sendmany;

    UniValue contextinfo_;                  ///< Additional context information
    uint32_t consensusBranchId_;           ///< Consensus branch ID for transaction
    CAmount fee_;                          ///< Transaction fee amount
    int mindepth_;                         ///< Minimum confirmation depth for inputs
    std::string fromaddress_;              ///< Source address string

    // Source address type flags
    bool isFromTransparentAddress_;        ///< True if source is transparent address (t-addr)
    bool isFromSaplingAddress_;            ///< True if source is Sapling shielded address 
    bool isFromOrchardAddress_;            ///< True if source is Orchard shielded address
    bool isFromPrivateAddress_;            ///< True if source is any shielded address type

    CTxDestination fromtaddr_;             ///< Transparent source address destination
    std::string fromAddress_;              ///< Formatted source address string
    PaymentAddress frompaymentaddress_;    ///< Shielded source payment address
    SpendingKey spendingkey_;              ///< Spending key for shielded sources
    bool hasOfflineSpendingKey;            ///< True if offline spending key is available

    // Output recipients
    std::vector<SendManyRecipient> saplingOutputs_;   ///< Sapling recipient outputs
    std::vector<SendManyRecipient> orchardOutputs_;   ///< Orchard recipient outputs

    // Input collections
    std::vector<SendManyInputUTXO> transparentInputs_; ///< Transparent input UTXOs
    std::vector<SaplingNoteEntry> saplingInputs_;      ///< Sapling input notes
    std::vector<OrchardNoteEntry> orchardInputs_;      ///< Orchard input notes

    TransactionBuilder builder_;           ///< Transaction builder instance
    CTransaction tx_;                      ///< Constructed transaction

    /**
     * @brief Add transparent outputs to transaction
     */
    void add_taddr_outputs_to_tx();

    /**
     * @brief Find unspent shielded notes for inputs
     * 
     * @return true if sufficient notes found, false otherwise
     */
    bool find_unspent_notes();

    /**
     * @brief Find transparent UTXOs for inputs
     * 
     * @param fAcceptCoinbase Whether to accept coinbase UTXOs
     * @return true if sufficient UTXOs found, false otherwise
     */
    bool find_utxos(bool fAcceptCoinbase);

    /**
     * @brief Convert hex string to memo array
     * 
     * @param s Hex string representation of memo
     * @return Fixed-size memo array
     */
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s);

    /**
     * @brief Core implementation of sendmany logic
     * 
     * @return true if operation completed successfully, false otherwise
     */
    bool main_impl();

    std::vector<PaymentDisclosureKeyInfo> paymentDisclosureData_; ///< Payment disclosure data
};

/**
 * @class TEST_FRIEND_AsyncRPCOperation_sendmany
 * @brief Test proxy class for accessing private methods
 */
class TEST_FRIEND_AsyncRPCOperation_sendmany
{
public:
    std::shared_ptr<AsyncRPCOperation_sendmany> delegate;

    TEST_FRIEND_AsyncRPCOperation_sendmany(std::shared_ptr<AsyncRPCOperation_sendmany> ptr) : delegate(ptr) {}

    CTransaction getTx()
    {
        return delegate->tx_;
    }

    void setTx(CTransaction tx)
    {
        delegate->tx_ = tx;
    }

    // Delegated methods

    void add_taddr_outputs_to_tx()
    {
        delegate->add_taddr_outputs_to_tx();
    }

    bool find_unspent_notes()
    {
        return delegate->find_unspent_notes();
    }

    bool find_utxos(bool fAcceptCoinbase)
    {
        return delegate->find_utxos(fAcceptCoinbase);
    }

    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s)
    {
        return delegate->get_memo_from_hex_string(s);
    }

    bool main_impl()
    {
        return delegate->main_impl();
    }

    void set_state(OperationStatus state)
    {
        delegate->state_.store(state);
    }
};


#endif /* ASYNCRPCOPERATION_SENDMANY_H */
