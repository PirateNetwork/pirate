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
 * @brief Asynchronous sendmany operation for multi-recipient shielded transactions
 *
 * Implements the z_sendmany RPC operation: sends funds from a single Sapling or
 * Ironwood source address to one or more Sapling/Ironwood recipients in a single
 * transaction. Transparent source addresses are not accepted; use z_shieldcoinbase
 * to move transparent funds into the shielded pool first.
 *
 * Inputs:  Sapling or Ironwood shielded notes owned by the from-address
 * Outputs: Sapling and/or Ironwood outputs with optional encrypted memos
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

/**
 * @class AsyncRPCOperation_sendmany
 * @brief Asynchronous z_sendmany operation
 *
 * Builds and broadcasts a shielded multi-recipient transaction. The source
 * address must be a Sapling or Ironwood payment address for which the wallet
 * holds the spending key (or a watch-only address for offline signing).
 *
 * Note selection: notes are sorted largest-first. The minimum set that covers
 * (outputs + fee) is selected; if the wallet holds more than 100 notes an
 * additional random batch of small notes is included for opportunistic
 * consolidation.
 *
 * Errors are reported through set_error_code() / set_error_message() rather
 * than exceptions; call getStatus() to inspect the outcome.
 */
class AsyncRPCOperation_sendmany : public AsyncRPCOperation
{
public:
    /**
     * @brief Construct a z_sendmany operation
     *
     * Validates all parameters, decodes and classifies the from-address
     * (Sapling or Ironwood only — transparent addresses are rejected),
     * and loads the spending key or sets the offline flag if the key is
     * not held locally.
     *
     * @param consensusParams  Network consensus parameters
     * @param nHeight          Block height used to initialise the transaction builder
     * @param fromAddress      Sapling or Ironwood payment address to spend from
     * @param saplingOutputs   Sapling recipients: (address, amount, hex-memo)
     * @param ironwoodOutputs   Ironwood recipients:  (address, amount, hex-memo)
     * @param minDepth         Minimum confirmation depth for input notes (must be > 0)
     * @param fee              Miner fee in zatoshis (default ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE)
     * @param contextInfo      Arbitrary context stored in the operation status
     *
     * @throws JSONRPCError    On any invalid parameter or unsupported address type
     */
    AsyncRPCOperation_sendmany(
        const Consensus::Params& consensusParams,
        const int nHeight,
        std::string fromAddress,
        std::vector<SendManyRecipient> saplingOutputs,
        std::vector<SendManyRecipient> ironwoodOutputs,
        int minDepth,
        CAmount fee = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
        
    /// Destructor — all members are RAII-managed; no explicit cleanup needed.
    virtual ~AsyncRPCOperation_sendmany();

    // Prevent copying and moving to ensure single ownership of transaction resources
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany const&) = delete;            
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany&&) = delete;                 
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany const&) = delete; 
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany&&) = delete;      

    /**
     * @brief Main execution entry point
     *
     * Transitions state to EXECUTING, calls main_impl(), handles all
     * exceptions, updates state to SUCCESS or FAILED, and logs the outcome.
     */
    virtual void main();

    /**
     * @brief Get operation status including RPC method and parameters
     *
     * @return UniValue status object with "method" and "params" fields appended
     */
    virtual UniValue getStatus() const;

    bool testmode = false;                ///< Enable test mode for debugging

private:
    friend class TEST_FRIEND_AsyncRPCOperation_sendmany;

    UniValue contextinfo_;                  ///< Context passed at construction; included in getStatus()
    CAmount fee_;                           ///< Miner fee in zatoshis
    int mindepth_;                          ///< Minimum note confirmation depth
    std::string fromaddress_;              ///< Source payment address string (Sapling or Ironwood)

    // Source address type flags (exactly one is true after construction)
    bool isFromSaplingAddress_ = false;    ///< True when the source is a Sapling payment address
    bool isFromIronwoodAddress_ = false;    ///< True when the source is an Ironwood payment address

    PaymentAddress frompaymentaddress_;    ///< Decoded source payment address
    SpendingKey spendingkey_;              ///< Spending key (empty when hasOfflineSpendingKey is true)
    bool hasOfflineSpendingKey = false;    ///< True when the wallet does not hold the spending key locally

    // Output recipients
    std::vector<SendManyRecipient> saplingOutputs_;   ///< Sapling recipients supplied at construction
    std::vector<SendManyRecipient> ironwoodOutputs_;   ///< Ironwood recipients supplied at construction

    // Input notes (populated by find_unspent_notes(), sorted descending by value)
    std::vector<SaplingNoteEntry> saplingInputs_;      ///< Selected Sapling input notes
    std::vector<IronwoodNoteEntry> ironwoodInputs_;      ///< Selected Ironwood input notes

    TransactionBuilder builder_;           ///< Builds the transaction incrementally
    CTransaction tx_;                      ///< Final constructed transaction

    /**
     * @brief Find, lock, and sort unspent shielded notes for spending
     *
     * Populates saplingInputs_ and ironwoodInputs_ from wallet notes meeting
     * mindepth_. Notes are immediately wallet-locked to prevent duplicate
     * selection by concurrent operations. Both collections are sorted
     * descending by value before returning.
     *
     * @return true if at least one spendable note was found
     */
    bool find_unspent_notes();

    /**
     * @brief Convert a hex string to a fixed-size memo byte array
     *
     * @param s   Hexadecimal memo string (may be empty)
     * @return    ZC_MEMO_SIZE-byte array; bytes beyond the input are 0xF6
     * @throws std::runtime_error if @p s is not valid hex or exceeds ZC_MEMO_SIZE bytes
     */
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(const std::string& s);

    /**
     * @brief Core transaction-building logic
     *
     * @return true on success; on failure sets error code/message and returns false
     */
    bool main_impl();
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

    bool find_unspent_notes()
    {
        return delegate->find_unspent_notes();
    }

    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(const std::string& s)
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
