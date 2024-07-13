// Copyright (c) 2017 The Zcash developers
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

struct ShieldCoinbaseUTXO {
    uint256 txid;
    int vout;
    CScript scriptPubKey;
    CAmount amount;
};

class AsyncRPCOperation_shieldcoinbase : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_shieldcoinbase(
        const Consensus::Params& consensusParams,
        const int nHeight,
        CMutableTransaction contextualTx,
        std::vector<ShieldCoinbaseUTXO> inputs,
        std::string toAddress,
        CAmount fee = SHIELD_COINBASE_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    virtual ~AsyncRPCOperation_shieldcoinbase();

    // We don't want to be copied or moved around
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase const&) = delete;            // Copy construct
    AsyncRPCOperation_shieldcoinbase(AsyncRPCOperation_shieldcoinbase&&) = delete;                 // Move construct
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase const&) = delete; // Copy assign
    AsyncRPCOperation_shieldcoinbase& operator=(AsyncRPCOperation_shieldcoinbase&&) = delete;      // Move assign

    virtual void main();

    virtual UniValue getStatus() const;

    bool testmode = false; // Set to true to disable sending txs and generating proofs
    // bool paymentDisclosureMode = true; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class ShieldToAddress;
    friend class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase; // class for unit testing

    UniValue contextinfo_; // optional data to include in return value from getStatus()

    CAmount fee_;
    PaymentAddress tozaddr_;

    std::vector<ShieldCoinbaseUTXO> inputs_;

    TransactionBuilder builder_;
    CTransaction tx_;

    bool main_impl();

    void lock_utxos();

    void unlock_utxos();
};

class ShieldToAddress : public boost::static_visitor<bool>
{
private:
    AsyncRPCOperation_shieldcoinbase* m_op;
    CAmount sendAmount;

public:
    ShieldToAddress(AsyncRPCOperation_shieldcoinbase* op, CAmount sendAmount) : m_op(op), sendAmount(sendAmount) {}

    bool operator()(const libzcash::SproutPaymentAddress& zaddr) const;
    bool operator()(const libzcash::SaplingPaymentAddress& zaddr) const;
    bool operator()(const libzcash::OrchardPaymentAddressPirate& zaddr) const;
    bool operator()(const libzcash::InvalidEncoding& no) const;
};


// To test private methods, a friend class can act as a proxy
class TEST_FRIEND_AsyncRPCOperation_shieldcoinbase
{
public:
    std::shared_ptr<AsyncRPCOperation_shieldcoinbase> delegate;

    TEST_FRIEND_AsyncRPCOperation_shieldcoinbase(std::shared_ptr<AsyncRPCOperation_shieldcoinbase> ptr) : delegate(ptr) {}

    CTransaction getTx()
    {
        return delegate->tx_;
    }

    void setTx(CTransaction tx)
    {
        delegate->tx_ = tx;
    }

    // Delegated methods

    bool main_impl()
    {
        return delegate->main_impl();
    }

    void set_state(OperationStatus state)
    {
        delegate->state_.store(state);
    }
};


#endif /* ASYNCRPCOPERATION_SHIELDCOINBASE_H */
