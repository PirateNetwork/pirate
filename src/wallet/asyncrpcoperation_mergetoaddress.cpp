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

#include "asyncrpcoperation_mergetoaddress.h"

#include "amount.h"
#include "asyncrpcqueue.h"
#include "core_io.h"
#include "init.h"
#include "key_io.h"
#include "komodo_bitcoind.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "netbase.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "script/interpreter.h"
#include "sodium.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "wallet.h"
#include "walletdb.h"
#include "zcash/IncrementalMerkleTree.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "paymentdisclosuredb.h"

using namespace libzcash;

// extern UniValue sendrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);

int mta_find_output(UniValue obj, int n)
{
    UniValue outputMapValue = find_value(obj, "outputmap");
    if (!outputMapValue.isArray()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Missing outputmap for JoinSplit operation");
    }

    UniValue outputMap = outputMapValue.get_array();
    assert(outputMap.size() == ZC_NUM_JS_OUTPUTS);
    for (size_t i = 0; i < outputMap.size(); i++) {
        if (outputMap[i].get_int() == n) {
            return i;
        }
    }

    throw std::logic_error("n is not present in outputmap");
}

AsyncRPCOperation_mergetoaddress::AsyncRPCOperation_mergetoaddress(
    const Consensus::Params& consensusParams,
    const int nHeight,
    CMutableTransaction contextualTx,
    std::vector<MergeToAddressInputUTXO> utxoInputs,
    std::vector<MergeToAddressInputSaplingNote> saplingNoteInputs,
    MergeToAddressRecipient recipient,
    CAmount fee,
    UniValue contextInfo) : tx_(contextualTx), utxoInputs_(utxoInputs),
                            saplingNoteInputs_(saplingNoteInputs), recipient_(recipient), fee_(fee), contextinfo_(contextInfo),
                            builder_(TransactionBuilder(consensusParams, nHeight, pwalletMain))
{
    if (fee < 0 || fee > MAX_MONEY) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Fee is out of range");
    }

    if (utxoInputs.empty() && saplingNoteInputs.empty()) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No inputs");
    }

    if (std::get<0>(recipient).size() == 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Recipient parameter missing");
    }

    toTaddr_ = DecodeDestination(std::get<0>(recipient));
    isToTaddr_ = IsValidDestination(toTaddr_);
    isToZaddr_ = false;

    if (!isToTaddr_) {
        auto address = DecodePaymentAddress(std::get<0>(recipient));
        if (IsValidPaymentAddress(address)) {
            isToZaddr_ = true;
            toPaymentAddress_ = address;
        } else {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid recipient address");
        }
    }

    // Log the context info i.e. the call parameters to z_mergetoaddress
    if (LogAcceptCategory("zrpcunsafe")) {
        LogPrint("zrpcunsafe", "%s: z_mergetoaddress initialized (params=%s)\n", getId(), contextInfo.write());
    } else {
        LogPrint("zrpc", "%s: z_mergetoaddress initialized\n", getId());
    }

    // Lock UTXOs
    lock_utxos();
    lock_notes();
}

AsyncRPCOperation_mergetoaddress::~AsyncRPCOperation_mergetoaddress()
{
}

void AsyncRPCOperation_mergetoaddress::main()
{
    if (isCancelled()) {
        unlock_utxos(); // clean up
        unlock_notes();
        return;
    }

    set_state(OperationStatus::EXECUTING);
    start_execution_clock();

    bool success = false;

#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(false, NULL, 0);
#else
    GenerateBitcoins(false, 0);
#endif
#endif

    try {
        success = main_impl();
    } catch (const UniValue& objError) {
        int code = find_value(objError, "code").get_int();
        std::string message = find_value(objError, "message").get_str();
        set_error_code(code);
        set_error_message(message);
    } catch (const runtime_error& e) {
        set_error_code(-1);
        set_error_message("runtime error: " + string(e.what()));
    } catch (const logic_error& e) {
        set_error_code(-1);
        set_error_message("logic error: " + string(e.what()));
    } catch (const exception& e) {
        set_error_code(-1);
        set_error_message("general exception: " + string(e.what()));
    } catch (...) {
        set_error_code(-2);
        set_error_message("unknown error");
    }

#ifdef ENABLE_MINING
#ifdef ENABLE_WALLET
    GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
#else
    GenerateBitcoins(GetBoolArg("-gen", false), GetArg("-genproclimit", 1));
#endif
#endif

    stop_execution_clock();

    if (success) {
        set_state(OperationStatus::SUCCESS);
    } else {
        set_state(OperationStatus::FAILED);
    }

    std::string s = strprintf("%s: z_mergetoaddress finished (status=%s", getId(), getStateAsString());
    if (success) {
        s += strprintf(", txid=%s)\n", tx_.GetHash().ToString());
    } else {
        s += strprintf(", error=%s)\n", getErrorMessage());
    }
    LogPrintf("%s", s);

    unlock_utxos(); // clean up
    unlock_notes(); // clean up
}

// Notes:
// 1. #1359 Currently there is no limit set on the number of joinsplits, so size of tx could be invalid.
// 2. #1277 Spendable notes are not locked, so an operation running in parallel could also try to use them.
bool AsyncRPCOperation_mergetoaddress::main_impl()
{
    assert(isToTaddr_ != isToZaddr_);

    bool isPureTaddrOnlyTx = (saplingNoteInputs_.empty() && isToTaddr_);
    CAmount minersFee = fee_;

    size_t numInputs = utxoInputs_.size();

    // Check mempooltxinputlimit to avoid creating a transaction which the local mempool rejects
    size_t limit = (size_t)GetArg("-mempooltxinputlimit", 0);
    {
        LOCK(cs_main);
        if (NetworkUpgradeActive(chainActive.Height() + 1, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
            limit = 0;
        }
    }
    if (limit > 0 && numInputs > limit) {
        throw JSONRPCError(RPC_WALLET_ERROR,
                           strprintf("Number of transparent inputs %d is greater than mempooltxinputlimit of %d",
                                     numInputs, limit));
    }

    CAmount t_inputs_total = 0;
    for (MergeToAddressInputUTXO& t : utxoInputs_) {
        t_inputs_total += std::get<1>(t);
    }

    CAmount z_inputs_total = 0;

    for (const MergeToAddressInputSaplingNote& t : saplingNoteInputs_) {
        z_inputs_total += std::get<2>(t);
    }

    CAmount targetAmount = z_inputs_total + t_inputs_total;

    if (targetAmount <= minersFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("Insufficient funds, have %s and miners fee is %s",
                                     FormatMoney(targetAmount), FormatMoney(minersFee)));
    }

    CAmount sendAmount = targetAmount - minersFee;

    LogPrint(isPureTaddrOnlyTx ? "zrpc" : "zrpcunsafe", "%s: spending %s to send %s with fee %s\n",
             getId(), FormatMoney(targetAmount), FormatMoney(sendAmount), FormatMoney(minersFee));
    LogPrint("zrpc", "%s: transparent input: %s\n", getId(), FormatMoney(t_inputs_total));
    LogPrint("zrpcunsafe", "%s: private input: %s\n", getId(), FormatMoney(z_inputs_total));
    if (isToTaddr_) {
        LogPrint("zrpc", "%s: transparent output: %s\n", getId(), FormatMoney(sendAmount));
    } else {
        LogPrint("zrpcunsafe", "%s: private output: %s\n", getId(), FormatMoney(sendAmount));
    }
    LogPrint("zrpc", "%s: fee: %s\n", getId(), FormatMoney(minersFee));

    // Grab the current consensus branch ID
    {
        LOCK(cs_main);
        consensusBranchId_ = CurrentEpochBranchId(chainActive.Height() + 1, Params().GetConsensus());
    }

    /**
     * This is based on code from AsyncRPCOperation_sendmany::main_impl() and should be refactored.
     */
    builder_.SetFee(minersFee);


    for (const MergeToAddressInputUTXO& t : utxoInputs_) {
        COutPoint outPoint = std::get<0>(t);
        CAmount amount = std::get<1>(t);
        CScript scriptPubKey = std::get<2>(t);
        builder_.AddTransparentInput(outPoint, scriptPubKey, amount);
    }

    std::optional<uint256> ovk;
    // Select Sapling notes
    std::vector<SaplingOutPoint> saplingOPs;
    std::vector<SaplingNote> saplingNotes;
    std::vector<SaplingExtendedSpendingKey> extsks;
    std::set<SaplingExtendedSpendingKey> setExtsks;
    for (const MergeToAddressInputSaplingNote& saplingNoteInput : saplingNoteInputs_) {
        saplingOPs.push_back(std::get<0>(saplingNoteInput));
        saplingNotes.push_back(std::get<1>(saplingNoteInput));
        auto extsk = std::get<3>(saplingNoteInput);
        extsks.push_back(extsk);
        setExtsks.insert(extsk);
        if (!ovk) {
            ovk = extsk.expsk.full_viewing_key().ovk;
        }
    }

    //Iterate thru all the selected notes and add them to the transactions
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        for (std::set<SaplingExtendedSpendingKey>::iterator it = setExtsks.begin(); it != setExtsks.end(); it++) {
            auto currentExtsk = *it;

            for (int i = 0; i < extsks.size(); i++) {
                if (currentExtsk == extsks[i]) {
                    libzcash::MerklePath saplingMerklePath;
                    if (!pwalletMain->SaplingWalletGetMerklePathOfNote(saplingOPs[i].hash, saplingOPs[i].n, saplingMerklePath)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Merkle Path not found for Sapling note. Stopping.\n", getId()));
                    }

                    uint256 anchor;
                    if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingMerklePath, saplingNotes[i].cmu().value(), anchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Getting Anchor failed. Stopping.\n", getId()));
                    }

                    libzcash::SaplingPaymentAddress recipient(saplingNotes[i].d, saplingNotes[i].pk_d);
                    if (!builder_.AddSaplingSpendRaw(saplingOPs[i], recipient, saplingNotes[i].value(), saplingNotes[i].rcm(), saplingMerklePath, anchor)) {
                        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Adding Raw Sapling Spend failed. Stopping.\n", getId()));
                    }
                }
            }

            if (!builder_.ConvertRawSaplingSpend(currentExtsk)) {
                throw JSONRPCError(RPC_WALLET_ERROR, strprintf("%s: Converting Raw Sapling Spends failed.\n", getId()));
            }
        }
    }


    if (isToTaddr_) {
        if (!builder_.AddTransparentOutput(toTaddr_, sendAmount)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid output address, not a valid taddr.");
        }
    } else {
        std::string zaddr = std::get<0>(recipient_);
        std::string strMemo = std::get<1>(recipient_);

        // Note: transaction builder expectes memo in
        //       ASCII encoding, not as a hex string.
        std::array<unsigned char, ZC_MEMO_SIZE> caMemo = {0x00};
        if (IsHex(strMemo)) {
            if (strMemo.length() > (ZC_MEMO_SIZE * 2)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, size of hex encoded memo is larger than maximum allowed %d", (ZC_MEMO_SIZE * 2)));
            }
            caMemo = get_memo_from_hex_string(strMemo);
        } else {
            if (strMemo.length() > ZC_MEMO_SIZE) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE));
            }

            int iLength = strMemo.length();
            unsigned char cByte;
            for (int iI = 0; iI < iLength; iI++) {
                cByte = (unsigned char)strMemo[iI];
                caMemo[iI] = cByte;
            }
        }

        auto saplingPaymentAddress = std::get_if<libzcash::SaplingPaymentAddress>(&toPaymentAddress_);
        if (saplingPaymentAddress == nullptr) {
            // This should never happen as we have already determined that the payment is to sapling
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Could not get Sapling payment address.");
        }
        if (saplingNoteInputs_.size() == 0 && utxoInputs_.size() > 0) {
            // Sending from t-addresses, which we don't have ovks for. Instead,
            // generate a common one from the HD seed. This ensures the data is
            // recoverable, while keeping it logically separate from the ZIP 32
            // Sapling key hierarchy, which the user might not be using.
            HDSeed seed;
            if (!pwalletMain->GetHDSeed(seed)) {
                throw JSONRPCError(
                    RPC_WALLET_ERROR,
                    "AsyncRPCOperation_sendmany: HD seed not found");
            }
            ovk = ovkForShieldingFromTaddr(seed);
        }
        if (!ovk) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Sending to a Sapling address requires an ovk.");
        }

        builder_.AddSaplingOutputRaw(*saplingPaymentAddress, sendAmount, caMemo);
        builder_.ConvertRawSaplingOutput(ovk.value());
    }


    // Build the transaction
    tx_ = builder_.Build().GetTxOrThrow();

    // Send the transaction
    // TODO: Use CWallet::CommitTransaction instead of sendrawtransaction
    auto signedtxn = EncodeHexTx(tx_);
    if (!testmode) {
        UniValue params = UniValue(UniValue::VARR);
        params.push_back(signedtxn);
        UniValue sendResultValue = sendrawtransaction(params, false, CPubKey());
        if (sendResultValue.isNull()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "sendrawtransaction did not return an error or a txid.");
        }

        auto txid = sendResultValue.get_str();

        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("txid", txid));
        set_result(o);
    } else {
        // Test mode does not send the transaction to the network.
        UniValue o(UniValue::VOBJ);
        o.push_back(Pair("test", 1));
        o.push_back(Pair("txid", tx_.GetHash().ToString()));
        o.push_back(Pair("hex", signedtxn));
        set_result(o);
    }

    return true;
}


std::array<unsigned char, ZC_MEMO_SIZE> AsyncRPCOperation_mergetoaddress::get_memo_from_hex_string(std::string s)
{
    std::array<unsigned char, ZC_MEMO_SIZE> memo = {{0x00}};

    std::vector<unsigned char> rawMemo = ParseHex(s.c_str());

    // If ParseHex comes across a non-hex char, it will stop but still return results so far.
    size_t slen = s.length();
    if (slen % 2 != 0 || (slen > 0 && rawMemo.size() != slen / 2)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");
    }

    if (rawMemo.size() > ZC_MEMO_SIZE) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE));
    }

    // copy vector into boost array
    int lenMemo = rawMemo.size();
    for (int i = 0; i < ZC_MEMO_SIZE && i < lenMemo; i++) {
        memo[i] = rawMemo[i];
    }
    return memo;
}

/**
 * Override getStatus() to append the operation's input parameters to the default status object.
 */
UniValue AsyncRPCOperation_mergetoaddress::getStatus() const
{
    UniValue v = AsyncRPCOperation::getStatus();
    if (contextinfo_.isNull()) {
        return v;
    }

    UniValue obj = v.get_obj();
    obj.push_back(Pair("method", "z_mergetoaddress"));
    obj.push_back(Pair("params", contextinfo_));
    return obj;
}

/**
 * Lock input utxos
 */
void AsyncRPCOperation_mergetoaddress::lock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : utxoInputs_) {
        pwalletMain->LockCoin(std::get<0>(utxo));
    }
}

/**
 * Unlock input utxos
 */
void AsyncRPCOperation_mergetoaddress::unlock_utxos()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto utxo : utxoInputs_) {
        pwalletMain->UnlockCoin(std::get<0>(utxo));
    }
}


/**
 * Lock input notes
 */
void AsyncRPCOperation_mergetoaddress::lock_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto note : saplingNoteInputs_) {
        pwalletMain->LockNote(std::get<0>(note));
    }
}

/**
 * Unlock input notes
 */
void AsyncRPCOperation_mergetoaddress::unlock_notes()
{
    LOCK2(cs_main, pwalletMain->cs_wallet);
    for (auto note : saplingNoteInputs_) {
        pwalletMain->UnlockNote(std::get<0>(note));
    }
}
