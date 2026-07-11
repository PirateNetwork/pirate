/* Notaries RPC Tools */

// Copyright (c) 2021-2022 DeckerSU, https://github.com/DeckerSU
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdexcept>

#include "wallet/wallet.h"
#include "init.h"
#include "main.h"
#include "rpc/server.h"
#include "key_io.h"
#include "coincontrol.h"
#include "utilmoneystr.h"
#include "transaction_builder.h"

#include "komodo_notary.h"
#include "komodo_structs.h"
#include "komodo_hardfork.h"

#include <boost/assign/list_of.hpp>

using namespace std;

static const bool fUseOnlyConfirmed = true;
static const CAmount NOTARY_VIN_AMOUNT = 10000;

static const size_t countNotaryVinToCreate_DEFAULT = 10;
static const CAmount NN_SPLIT_DEFAULT_MINERS_FEE = 10000;
static const bool fMergeAllUtxos_DEFAULT = false;
static const bool fSkipNotaryVins_DEFAULT = true;
static const bool fSendTransaction_DEFAULT = true;

std::string b2str(bool x) {
    if (x) return "true";
    return "false";
}

UniValue nn_getwalletinfo(const UniValue& params, bool fHelp, const CPubKey& mypk) {

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "nn_getwalletinfo\n"
            "Returns an object containing NN wallet info.\n"
            "\nResult:\n"
            "{\n"
            "\nExamples:\n"
            + HelpExampleCli("nn_getwalletinfo", "")
            + HelpExampleRpc("nn_getwalletinfo", "")
        );

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available.");
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is locked.");
    
    std::string pubkeyStr = GetArg("-pubkey", "");
    if (!(pubkeyStr.size() == 2 * CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && IsHex(pubkeyStr)))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Notary pubkey is not set.");
    
    CPubKey nn_pubkey(ParseHex(pubkeyStr));
    CScript nn_p2pk_script = CScript() << ToByteVector(nn_pubkey) << OP_CHECKSIG;
    CScript nn_p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(nn_pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    LOCK2(cs_main, pwalletMain->cs_wallet);
    int32_t currentSeason = chainName.isKMD() ? (
        chainActive.Tip() ? (
            chainActive.Tip()->nHeight >= KOMODO_NOTARIES_HARDCODED ? getkmdseason(chainActive.Tip()->nHeight) : 0
        ) : 0
    ) : getacseason(GetTime());

    bool fHavePrivateKey = pwalletMain->HaveKey(nn_pubkey.GetID());

    int nn_index = -1; 
    std::string nn_name = "";
    CTxDestination dest; std::string pubkey_address = "";

    if (ExtractDestination(nn_p2pkh_script, dest)) {
        pubkey_address = EncodeDestination(dest);
    }

    // pubkeyStr = HexStr(nn_pubkey, false);
    if (currentSeason > 0) {
        for (int j = 0; j < NUM_KMD_NOTARIES; j++ ) {
            const char **nn_record = notaries_elected[currentSeason - 1][j];
            if (!pubkeyStr.compare(nn_record[1])) {
                nn_index = j; nn_name = nn_record[0]; break;
            }
        }
    }
    
    CCoinControl ccNotaryVins; 
    CCoinControl ccOthers;
    size_t count_ccNotaryVins_dirty = 0, count_ccNotaryVins_infly = 0;
    size_t count_ccOthers_dirty = 0, count_ccOthers_infly = 0;

    /* here we select coins in CCoinControl just for example, but of course we can use it for 
       filtering in AvailableCoins, for example we can fill the output only with notary vins,
       or with regular utxos */

    for (const std::pair<uint256, CWalletTx>& pairWtx : pwalletMain->mapWallet) {
        const CWalletTx& wtx = pairWtx.second;
        int nDepth = wtx.GetDepthInMainChain();
        for (int i = 0; i < wtx.vout.size(); i++) {
            const CTxOut& vout = wtx.vout[i];
            if (vout.nValue == NOTARY_VIN_AMOUNT && vout.scriptPubKey == nn_p2pk_script) {
                ccNotaryVins.Select(COutPoint(wtx.GetHash(), i));
                count_ccNotaryVins_dirty++;
                if (nDepth == 0) count_ccNotaryVins_infly++;
            }
            else {
                ccOthers.Select(COutPoint(wtx.GetHash(), i));
                count_ccOthers_dirty++;
                if (nDepth == 0) count_ccOthers_infly++;
            }
        }
    }

    std::vector<COutput> vecOutputs;
    /* 
        Notes:

        1. fOnlyConfirmed -> CWalletTx::IsTrusted()
        2. Inside AvailableCoins GetDepthInMainChain() calculated twice, first time when 
        we call IsTrusted (nDepth >= 1 - trusted, nDepth < 0 - not trusted) and second
        time explicit in next conditions in AvailableCoins.
    */ 
    pwalletMain->AvailableCoins(vecOutputs, fUseOnlyConfirmed, NULL, false, true);

    size_t count_ccNotaryVins = 0;
    size_t count_ccOthers = 0;
    for (const COutput& out : vecOutputs) {
        // here out.nDepth always >= 0 , no additional depth checks needed
        if (!out.fSpendable) continue;

        const CTxOut& txOut = out.tx->vout[out.i];
        if (txOut.nValue == NOTARY_VIN_AMOUNT && txOut.scriptPubKey == nn_p2pk_script) {
            count_ccNotaryVins++;
        } else {
            count_ccOthers++;
        }
    }

    // if transactions_count != available_coins_count it means wallet contains some strange
    // transactions, simple case to emulate this, start testnet, mine some transactions, let
    // them fill into the wallet, then, stop daemon, clear entire blockchain folder, except
    // wallet.dat, and here we are. wallet will contain some transactions related to non-existing
    // blockchain state.

    UniValue result(UniValue::VOBJ);
    result.pushKV("currentSeason", currentSeason);
    result.pushKV("nn_index", nn_index);
    result.pushKV("nn_name", nn_name);
    result.pushKV("pubkey", HexStr(nn_pubkey, false));
    result.pushKV("pubkey_address", pubkey_address);
    result.pushKV("ismine", fHavePrivateKey);

    result.pushKV("transactions_count", (int64_t)pwalletMain->mapWallet.size());
    result.pushKV("available_coins_count", (int64_t)vecOutputs.size());
    
    // UniValue obj(UniValue::VOBJ);
    // obj.clear(); obj.setObject();
    // obj.pushKV("dirty", count_ccNotaryVins_dirty);
    // obj.pushKV("infly", count_ccNotaryVins_infly);
    // obj.pushKV("normal", count_ccNotaryVins);
    // result.pushKV("notaryvins_utxos_count", obj);
    // obj.clear(); obj.setObject();
    // obj.pushKV("dirty", count_ccOthers_dirty);
    // obj.pushKV("infly", count_ccOthers_infly);
    // obj.pushKV("normal", count_ccOthers);
    // result.pushKV("others_utxos_count", obj);

    result.pushKV("notaryvins_utxos_count", (int64_t)count_ccNotaryVins);
    result.pushKV("others_utxos_count", (int64_t)count_ccOthers);

    return result;
}

// transaction.h comment: spending taddr output requires CTxIn >= 148 bytes and typical taddr txout is 34 bytes
#define CTXIN_SPEND_DUST_SIZE   148
#define CTXIN_SPEND_P2SH_SIZE   400

// Examples of visitors: CScriptVisitor, CBitcoinAddressVisitor, DescribeAddressVisitor
namespace
{
    class CTransparentSpendSizeVisitor : public boost::static_visitor<size_t> {
        public:
        size_t operator()(const CNoDestination &dest) const { return 0; }
        size_t operator()(const CKeyID &keyID) const { return CTXIN_SPEND_DUST_SIZE; }
        size_t operator()(const CPubKey &key) const { return CTXIN_SPEND_DUST_SIZE; }
        size_t operator()(const CScriptID &scriptID) const { return CTXIN_SPEND_P2SH_SIZE; }
    };

    size_t GetSizeForDestination(const CTxDestination& dest) {
        return boost::apply_visitor(CTransparentSpendSizeVisitor(), dest);
    }
}

extern UniValue signrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk);

UniValue nn_split(const UniValue& params, bool fHelp, const CPubKey& mypk) {
    if (fHelp || params.size() > 5)
        throw runtime_error(
            "nn_split\n\n"
            "This RPC can create notaryvins (special P2PK utxos used by \n"
            "Komodo notary nodes for mining and notarizing), using any utxos\n"
            "in current wallet. It can act in several ways depends on params,\n"
            "for example, it can merge all UTXOs in one transaction (with\n"
            "skipping old notary vins UTXOs or not) and create a set of new\n"
            "notary vins UTXOs. Or it can create notary vins by selecting \n"
            "low amount UTXOs as a vins as well. In other words this RPC is\n"
            "a good replacement for autosplit in Iguana or using scripts.\n"
            "\n"
            "Arguments:\n"
            "\n"
            "1. countnotaryvintocreate (numeric, default="+ std::to_string(countNotaryVinToCreate_DEFAULT) +") - number of notary vin P2PK utxos to be created.\n"
            "2. fee (numeric, default=" + strprintf("%s", FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE)) + ") - the fee amount to attach to this transaction.\n"
            "3. fmergeallutxos (boolean, default=" + b2str(fMergeAllUtxos_DEFAULT) + ") - if true will merge all utxos, else merge only needed vins to match needed amount of notaryvins output.\n"
            "4. fskipnotaryvins (boolean, default=" + b2str(fSkipNotaryVins_DEFAULT) +") - if true - will skip (don't merge) existing notaryvins, else will merge existing notaryvins.\n"
            "5. fsendtransaction (boolean, default=" + b2str(fSendTransaction_DEFAULT) +") - if true - will broadcast tx immediatelly, false - just show raw hex tx.\n"
            "\nExamples:\n"
            + HelpExampleCli("nn_split", "" + std::to_string(countNotaryVinToCreate_DEFAULT) + " " + FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE) + " " + b2str(fMergeAllUtxos_DEFAULT) + " " + b2str(fSkipNotaryVins_DEFAULT) + " " + b2str(fSendTransaction_DEFAULT))
            + HelpExampleRpc("nn_split", std::to_string(countNotaryVinToCreate_DEFAULT) + " , " + FormatMoney(NN_SPLIT_DEFAULT_MINERS_FEE))
        );

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available.");
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is locked.");
    
    std::string pubkeyStr = GetArg("-pubkey", "");
    if (!(pubkeyStr.size() == 2 * CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && IsHex(pubkeyStr)))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Notary pubkey is not set.");

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM)(UniValue::VNUM)(UniValue::VBOOL)(UniValue::VBOOL)(UniValue::VBOOL));
    
    /* Argument: countNotaryVinToCreate */
    size_t countNotaryVinToCreate = countNotaryVinToCreate_DEFAULT;
    if (params.size() > 0) {
        countNotaryVinToCreate = params[0].get_int();
    }

    /* Argument: minersFee */
    // Convert fee from currency format to zatoshis
    CAmount minersFee = NN_SPLIT_DEFAULT_MINERS_FEE;
    if (params.size() > 1) {
        if (params[1].get_real() == 0.0) {
            minersFee = 0;
        } else {
            minersFee = AmountFromValue( params[1] );
        }
    }

    /* Argument: fMergeAllUtxos */
    bool fMergeAllUtxos = fMergeAllUtxos_DEFAULT;  // if true - will merge all utxos, else merge only needed vins to match needed amount of notaryvins output
    if (params.size() > 2) {
        fMergeAllUtxos = params[2].get_bool();
    }

    /* Argument: fSkipNotaryVins */
    bool fSkipNotaryVins = fSkipNotaryVins_DEFAULT; // if true - will skip (don't merge) existing notaryvins, else will merge existing notaryvins
    if (params.size() > 3) {
        fSkipNotaryVins = params[3].get_bool();
    }

    /* Argument: fSendTransaction */
    bool fSendTransaction = fSendTransaction_DEFAULT; // if true - will broadcast tx immediatelly
    if (params.size() > 4) {
        fSendTransaction = params[4].get_bool();
    }

    CPubKey nn_pubkey(ParseHex(pubkeyStr));
    CScript nn_p2pk_script = CScript() << ToByteVector(nn_pubkey) << OP_CHECKSIG;
    CScript nn_p2pkh_script = CScript() << OP_DUP << OP_HASH160 << ToByteVector(nn_pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    LOCK2(cs_main, pwalletMain->cs_wallet);

    const int nextBlockHeight = chainActive.Height() + 1;
    const bool overwinterActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);

    CTxDestination dest; std::string pubkey_address = "";

    bool fHavePrivateKey = pwalletMain->HaveKey(nn_pubkey.GetID());
    if (!fHavePrivateKey)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Haven't privkey in the wallet, won't split.");

    UniValue result(UniValue::VOBJ);

    if (ExtractDestination(nn_p2pkh_script, dest)) {
        // std::tuple<COutPoint, CAmount, CScript> MergeToAddressInputUTXO

        // Prepare to get UTXOs
        std::vector<std::tuple<COutPoint, CAmount, CScript>> utxoInputs;
        CAmount mergedUTXOValue = 0;
        size_t utxoCounter = 0;
        unsigned int max_tx_size = saplingActive ? MAX_TX_SIZE_AFTER_SAPLING : MAX_TX_SIZE_BEFORE_SAPLING;

        size_t estimatedTxSize = 200;  // tx overhead + wiggle room

        // Get available utxos
        vector<COutput> vecOutputs;
        pwalletMain->AvailableCoins(vecOutputs, fUseOnlyConfirmed, NULL, false, true);

        // TODO: implement only choose utxos to send notaryvins, without join all our utxos

        // Find unspent utxos and update estimated size
        for (const COutput& out : vecOutputs) {
            if (!out.fSpendable) continue;
            CScript scriptPubKey = out.tx->vout[out.i].scriptPubKey;
            CTxDestination address;
            if (!ExtractDestination(scriptPubKey, address)) {
                continue;
            }

            // TODO: filter by pubkey if needed, use only notary address belongs utxos
            // i.e. address == dest, where dest is from ExtractDestination(nn_p2pkh_script, dest)

            utxoCounter++;
            CAmount nValue = out.tx->vout[out.i].nValue;
            // size_t increase = GetSizeForDestination(address);
            size_t increase = (boost::get<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE; /* std::get_if */
            estimatedTxSize += increase;
            COutPoint utxo(out.tx->GetHash(), out.i);
            utxoInputs.emplace_back(utxo, nValue, scriptPubKey);
            mergedUTXOValue += nValue;
        }

        if (!fMergeAllUtxos) {
            // now we have vector of tuples std::vector<std::tuple<COutPoint, CAmount, CScript>> utxoInputs
            // filled with all utxos, let's sort it by nValue, and select only needed utxos to fund user's 
            // transaction

            std::vector<std::tuple<COutPoint, CAmount, CScript>> filteredUtxoInputs;

            std::sort(utxoInputs.begin(), utxoInputs.end(), 
            [](const std::tuple<COutPoint, CAmount, CScript>& first, const std::tuple<COutPoint, CAmount, CScript>& second) 
            {
                return std::get<1>(first) < std::get<1>(second);
            });

            const CAmount neededValue = minersFee + countNotaryVinToCreate * NOTARY_VIN_AMOUNT;
            estimatedTxSize = 200; mergedUTXOValue = 0; utxoCounter = 0;
            
            // std::cerr << "b ------------------------------" << std::endl;
            for (const std::tuple<COutPoint, CAmount, CScript>& utxo : utxoInputs) {
                
                COutPoint out; CAmount nValue; CScript script;
                std::tie(out, nValue, script) = utxo;

                if (fSkipNotaryVins && 
                    nValue == NOTARY_VIN_AMOUNT && 
                    script == nn_p2pk_script) continue;

                filteredUtxoInputs.emplace_back(out, nValue, script);

                CTxDestination address;
                if (!ExtractDestination(script, address)) {
                    continue;
                }

                size_t increase = (boost::get<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE; /* std::get_if */
                estimatedTxSize += increase;
                mergedUTXOValue += nValue;
                utxoCounter++;

                // std::cerr << out.ToString() << " " << FormatMoney(nValue) << " " << script.ToString() << std::endl;

                if (mergedUTXOValue >= neededValue)
                    break;
            }
            // std::cerr << "e ------------------------------" << std::endl;

            utxoInputs.swap(filteredUtxoInputs);
        }


        if (estimatedTxSize > max_tx_size)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Result tx exceeded allowed size.");
        if (utxoInputs.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No inputs.");
        
        CTxDestination toTaddr_;
        if (!ExtractDestination(nn_p2pkh_script, toTaddr_))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid P2PKH recipient.");

        CAmount targetAmount = mergedUTXOValue;
        if (targetAmount <= minersFee) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                            strprintf("Insufficient funds, have %s and miners fee is %s",
                                        FormatMoney(targetAmount), FormatMoney(minersFee)));
        }

        CAmount sendAmount = targetAmount - minersFee - (countNotaryVinToCreate * NOTARY_VIN_AMOUNT);

        CTransaction tx_;

        // lock all utxos
        for (auto utxo : utxoInputs) {
            pwalletMain->LockCoin(std::get<0>(utxo));
        }

        bool fUseTxBuilder = false;
        
        if (!fUseTxBuilder) 
        {   /* without builder, this method suitable for almost all Bitcoin based coins */

            // Contextual transaction we will build on
            CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
            CMutableTransaction rawTx(contextualTx);
            for (const std::tuple<COutPoint, CAmount, CScript>& t : utxoInputs) {
                CTxIn in(std::get<0>(t));
                rawTx.vin.push_back(in);
            }
            CScript scriptPubKey = GetScriptForDestination(toTaddr_);
            CTxOut out(sendAmount, scriptPubKey);
            rawTx.vout.push_back(out);

            // Create notaryvins
            for (size_t i = 0; i < countNotaryVinToCreate; ++i) {
                rawTx.vout.push_back(CTxOut(NOTARY_VIN_AMOUNT, nn_p2pk_script));
            }

            tx_ = CTransaction(rawTx);

            auto unsignedtxn = EncodeHexTx(tx_);

            UniValue params = UniValue(UniValue::VARR);
            params.push_back(unsignedtxn);
            UniValue signResultValue = signrawtransaction(params, false, CPubKey());
            UniValue signResultObject = signResultValue.get_obj();
            UniValue completeValue = find_value(signResultObject, "complete");
            bool complete = completeValue.get_bool();
            if (!complete) {
                // TODO: #1366 Maybe get "errors" and print array vErrors into a string
                throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Failed to sign transaction");
            }

            UniValue hexValue = find_value(signResultObject, "hex");
            if (hexValue.isNull()) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Missing hex data for signed transaction");
            }

            std::string signedtxn = hexValue.get_str();
            // tx_ currently store unsigned tx, so, let's store signed
            DecodeHexTx(tx_, signedtxn);

            if (!fSendTransaction) {
                // UniValue obj(UniValue::VOBJ);
                // obj.push_back(Pair("signed_rawtxn", signedtxn));
                result.push_back(Pair("tx", signedtxn));
            } else {
                UniValue send_params = UniValue(UniValue::VARR);
                // send_params.clear(); send_params.setArray();
                send_params.push_back(signedtxn);
                UniValue sendResultValue = sendrawtransaction(send_params, false, CPubKey());
                result.push_back(Pair("tx", sendResultValue));
            }
        } 
        else 
        {
            /* with builder */

            TransactionBuilder builder(Params().GetConsensus(), nextBlockHeight, pwalletMain);
            builder.SetFee(minersFee);
            for (const std::tuple<COutPoint, CAmount, CScript>& t : utxoInputs) {
                COutPoint outPoint = std::get<0>(t);
                CAmount amount = std::get<1>(t);
                CScript scriptPubKey = std::get<2>(t);
                builder.AddTransparentInput(outPoint, scriptPubKey, amount);
            }
            if (!builder.AddTransparentOutput(toTaddr_, sendAmount)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid output address, not a valid taddr.");
            }

            // Create notaryvins
            for (size_t i = 0; i < countNotaryVinToCreate; ++i) {
                // to = ExtractDestination(nn_p2pkh_script, dest)
                auto to = CTxDestination(nn_pubkey);
                builder.AddTransparentOutput(to, NOTARY_VIN_AMOUNT);
            }

            // builder.SendChangeTo(CTxDestination(nn_pubkey.GetID())); // no change?

            // Build the transaction
            auto maybe_tx = builder.Build();
            if (!maybe_tx) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to build transaction.");
            }
            tx_ = maybe_tx.get();

            auto signedtxn = EncodeHexTx(tx_);

            if (!fSendTransaction) {
                // UniValue obj(UniValue::VOBJ);
                // obj.push_back(Pair("signed_rawtxn", signedtxn));
                result.push_back(Pair("tx", signedtxn));
            } else {
                UniValue send_params = UniValue(UniValue::VARR);
                // send_params.clear(); send_params.setArray();
                send_params.push_back(signedtxn);
                UniValue sendResultValue = sendrawtransaction(send_params, false, CPubKey());
                result.push_back(Pair("tx", sendResultValue));
            }
        }

        // unlock all utxos
        for (auto utxo : utxoInputs) {
            pwalletMain->UnlockCoin(std::get<0>(utxo));
        }

        // result.pushKV("params", params);
        result.pushKV("input_utxos_value", ValueFromAmount(mergedUTXOValue)); // UniValue::VNUM
        result.pushKV("input_utxos_count", (int64_t)utxoInputs.size());
        result.pushKV("out_notaryvins_count", (int64_t)countNotaryVinToCreate);

        result.pushKV("out_utxos_value", ValueFromAmount(sendAmount));
        result.pushKV("out_utxos_count", 1);
        
        result.pushKV("estimated_tx_size", (int64_t)estimatedTxSize);
        result.pushKV("real_tx_size", (int)::GetSerializeSize(tx_, SER_NETWORK, PROTOCOL_VERSION));
        // result.pushKV("real_tx_size", EncodeHexTx(tx_).size() >> 1);
    }

    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    /* Not shown in help */
    { "hidden",             "nn_getwalletinfo",            &nn_getwalletinfo,            true  },
    { "hidden",             "nn_split",                    &nn_split,                    true  },
};

void RegisterNotariesRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}

/*
    How to setup?
    -------------

    1. Place notary.cpp and notary.h into <komodo_repo>/src/rpc/

    2. Add to client.cpp:
    static const CRPCConvertParam vRPCConvertParams[] =
    {
        ...
        { "nn_split", 0 },
        { "nn_split", 1 },
        { "nn_split", 2 },
        { "nn_split", 3 },
        { "nn_split", 4 },
        ...
    };

    3. Add to Makefile.am:
    libbitcoin_server_a_SOURCES = \
    ...
    rpc/net.cpp \
    rpc/notaries.cpp \ # <- this line should be added
    rpc/rawtransaction.cpp \
    ...
    $(BITCOIN_CORE_H) \
    $(LIBZCASH_H)

    4. Add in src/rpc/register.h:
    ...
    void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC);
    void RegisterNotariesRPCCommands(CRPCTable &tableRPC); # <- this line should be added
    ...
    static inline void RegisterAllCoreRPCCommands(CRPCTable &tableRPC)
    {
    ...
    RegisterRawTransactionRPCCommands(tableRPC);
    RegisterNotariesRPCCommands(tableRPC); # <- this line should be added
*/