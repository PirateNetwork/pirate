// Copyright (c) 2016 The Zcash developers
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

// Default transaction fee if caller does not specify one.
#define ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE 10000

using namespace libzcash;

// A recipient is a tuple of address, amount, memo (optional if zaddr)
typedef std::tuple<std::string, CAmount, std::string> SendManyRecipient;

// Input UTXO is a tuple (quadruple) of txid, vout, amount, coinbase)
typedef std::tuple<uint256, int, CAmount, bool, CTxDestination> SendManyInputUTXO;

class AsyncRPCOperation_sendmany : public AsyncRPCOperation
{
public:
    AsyncRPCOperation_sendmany(
        const Consensus::Params& consensusParams,
        const int nHeight,
        std::string fromAddress,
        std::vector<SendManyRecipient> saplingOutputs,
        std::vector<SendManyRecipient> orchardOutputs,
        int minDepth,
        CAmount fee = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE,
        UniValue contextInfo = NullUniValue);
    virtual ~AsyncRPCOperation_sendmany();

    // We don't want to be copied or moved around
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany const&) = delete;            // Copy construct
    AsyncRPCOperation_sendmany(AsyncRPCOperation_sendmany&&) = delete;                 // Move construct
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany const&) = delete; // Copy assign
    AsyncRPCOperation_sendmany& operator=(AsyncRPCOperation_sendmany&&) = delete;      // Move assign

    virtual void main();

    virtual UniValue getStatus() const;

    bool testmode = false; // Set to true to disable sending txs and generating proofs

    bool paymentDisclosureMode = true; // Set to true to save esk for encrypted notes in payment disclosure database.

private:
    friend class TEST_FRIEND_AsyncRPCOperation_sendmany; // class for unit testing

    UniValue contextinfo_; // optional data to include in return value from getStatus()

    uint32_t consensusBranchId_;
    CAmount fee_;
    int mindepth_;
    std::string fromaddress_;
    bool isfromtaddr_;
    bool isfromsaplingaddr_;
    bool isfromorchardaddr_;
    bool isfromprivate_;
    CTxDestination fromtaddr_;
    std::string fromAddress_;
    PaymentAddress frompaymentaddress_;
    SpendingKey spendingkey_;
    bool bOfflineSpendingKey;

    std::vector<SendManyRecipient> sapling_outputs_;
    std::vector<SendManyRecipient> orchard_outputs_;
    std::vector<SendManyInputUTXO> t_inputs_;
    std::vector<SaplingNoteEntry> z_sapling_inputs_;
    std::vector<OrchardNoteEntry> z_orchard_inputs_;

    TransactionBuilder builder_;
    CTransaction tx_;

    void add_taddr_outputs_to_tx();
    bool find_unspent_notes();
    bool find_utxos(bool fAcceptCoinbase);
    std::array<unsigned char, ZC_MEMO_SIZE> get_memo_from_hex_string(std::string s);
    bool main_impl();

    // payment disclosure!
    std::vector<PaymentDisclosureKeyInfo> paymentDisclosureData_;
};

// To test private methods, a friend class can act as a proxy
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
