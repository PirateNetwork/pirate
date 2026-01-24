// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2024-2025 The Pirate Network developers
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

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "init.h"
#include "deprecation.h"
#include "key_io.h"
#include "keystore.h"
#include "main.h"
#include "merkleblock.h"
#include "net.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "transaction_builder.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "uint256.h"
#include "importcoin.h"
#include "komodo_notary.h"
#include "komodo_bitcoind.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/rpcpiratewallet.h"
#endif

#include "komodo_defs.h"
#include "assetchain.h"
#include "komodo_interest.h"

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>
#include <utf8cpp/utf8.h>

#include <rust/bridge.h>

using namespace std;

/**
 * @brief Converts a script public key to JSON representation with address extraction
 * 
 * Analyzes the provided script to extract transaction output type, required signatures,
 * and associated addresses. Supports standard script types including P2PKH, P2PK, P2SH,
 * and multisig patterns.
 * 
 * @param scriptPubKey The script to analyze and convert
 * @param out Reference to UniValue object that will contain the JSON output
 * @param fIncludeHex Whether to include the raw hex-encoded script data
 */
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.push_back(Pair("asm", ScriptToAsmStr(scriptPubKey)));
    if (fIncludeHex)
        out.push_back(Pair("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end())));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired))
    {
        out.push_back(Pair("type", GetTxnOutputType(type)));
        return;
    }

    out.push_back(Pair("reqSigs", nRequired));
    out.push_back(Pair("type", GetTxnOutputType(type)));

    UniValue a(UniValue::VARR);
    for (const CTxDestination& addr : addresses) {
        a.push_back(EncodeDestination(addr));
    }
    out.push_back(Pair("addresses", a));
}

/**
 * @brief Converts Sprout JoinSplit transaction data to JSON format
 * 
 * Processes zero-knowledge proof data from Sprout shielded transactions, including
 * nullifiers, commitments, ciphertexts, and proof serialization. Handles both
 * PGHR13 and Groth16 proof systems based on transaction version.
 * 
 * @param tx Transaction containing JoinSplit descriptions to serialize
 * @return UniValue JSON array containing all JoinSplit objects with complete proof data
 */
UniValue TxJoinSplitToJSON(const CTransaction& tx) {
    bool useGroth = tx.fOverwintered && tx.nVersion >= SAPLING_TX_VERSION;
    UniValue vjoinsplit(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vjoinsplit.size(); i++) {
        const JSDescription& jsdescription = tx.vjoinsplit[i];
        UniValue joinsplit(UniValue::VOBJ);

        joinsplit.push_back(Pair("vpub_old", ValueFromAmount(jsdescription.vpub_old)));
        joinsplit.push_back(Pair("vpub_oldZat", jsdescription.vpub_old));
        joinsplit.push_back(Pair("vpub_new", ValueFromAmount(jsdescription.vpub_new)));
        joinsplit.push_back(Pair("vpub_newZat", jsdescription.vpub_new));

        joinsplit.push_back(Pair("anchor", jsdescription.anchor.GetHex()));

        {
            UniValue nullifiers(UniValue::VARR);
            BOOST_FOREACH(const uint256 nf, jsdescription.nullifiers) {
                nullifiers.push_back(nf.GetHex());
            }
            joinsplit.push_back(Pair("nullifiers", nullifiers));
        }

        {
            UniValue commitments(UniValue::VARR);
            BOOST_FOREACH(const uint256 commitment, jsdescription.commitments) {
                commitments.push_back(commitment.GetHex());
            }
            joinsplit.push_back(Pair("commitments", commitments));
        }

        joinsplit.push_back(Pair("onetimePubKey", jsdescription.ephemeralKey.GetHex()));
        joinsplit.push_back(Pair("randomSeed", jsdescription.randomSeed.GetHex()));

        {
            UniValue macs(UniValue::VARR);
            BOOST_FOREACH(const uint256 mac, jsdescription.macs) {
                macs.push_back(mac.GetHex());
            }
            joinsplit.push_back(Pair("macs", macs));
        }

        CDataStream ssProof(SER_NETWORK, PROTOCOL_VERSION);
        auto ps = SproutProofSerializer<CDataStream>(ssProof, useGroth);
        std::visit(ps, jsdescription.proof);
        joinsplit.push_back(Pair("proof", HexStr(ssProof.begin(), ssProof.end())));

        {
            UniValue ciphertexts(UniValue::VARR);
            for (const ZCNoteEncryption::Ciphertext ct : jsdescription.ciphertexts) {
                ciphertexts.push_back(HexStr(ct.begin(), ct.end()));
            }
            joinsplit.push_back(Pair("ciphertexts", ciphertexts));
        }

        vjoinsplit.push_back(joinsplit);
    }
    return vjoinsplit;
}

/**
 * @brief Converts Sapling shielded spend descriptions to JSON format
 * 
 * Serializes Sapling spend proofs including value commitments, nullifiers,
 * randomized verification keys, and zero-knowledge proofs for spend authorization.
 * 
 * @param saplingSpends Vector of Sapling spend descriptions from transaction
 * @return UniValue JSON array of spend objects with cryptographic commitments
 */
UniValue TxShieldedSpendsToJSON(const rust::Vec<sapling::Spend>& saplingSpends) {
    UniValue vdesc(UniValue::VARR);
    for (const auto& spendDesc : saplingSpends) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("cv", uint256::FromRawBytes(spendDesc.cv()).GetHex());
        obj.pushKV("anchor", uint256::FromRawBytes(spendDesc.anchor()).GetHex());
        obj.pushKV("nullifier", uint256::FromRawBytes(spendDesc.nullifier()).GetHex());
        obj.pushKV("rk", uint256::FromRawBytes(spendDesc.rk()).GetHex());
        obj.pushKV("proof", HexStr(spendDesc.zkproof()));
        obj.pushKV("spendAuthSig", HexStr(spendDesc.spend_auth_sig()));
        vdesc.push_back(obj);
    }
    return vdesc;
}

/**
 * @brief Converts Sapling shielded output descriptions to JSON format
 * 
 * Serializes Sapling output data including value commitments, note commitments,
 * ephemeral keys, encrypted ciphertexts, and zero-knowledge proofs for output validity.
 * 
 * @param saplingOutputs Vector of Sapling output descriptions from transaction
 * @return UniValue JSON array of output objects with encrypted note data
 */
UniValue TxShieldedOutputsToJSON(const rust::Vec<sapling::Output>& saplingOutputs) {
    UniValue vdesc(UniValue::VARR);
    for (const auto& outputDesc : saplingOutputs) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("cv", uint256::FromRawBytes(outputDesc.cv()).GetHex());
        obj.pushKV("cmu", uint256::FromRawBytes(outputDesc.cmu()).GetHex());
        obj.pushKV("ephemeralKey", uint256::FromRawBytes(outputDesc.ephemeral_key()).GetHex());
        obj.pushKV("encCiphertext", HexStr(outputDesc.enc_ciphertext()));
        obj.pushKV("outCiphertext", HexStr(outputDesc.out_ciphertext()));
        obj.pushKV("proof", HexStr(outputDesc.zkproof()));
        vdesc.push_back(obj);
    }
    return vdesc;
}

/**
 * @brief Converts Orchard action descriptions to JSON format
 * 
 * Serializes Orchard actions which combine both spend and output functionality
 * in a single cryptographic action, including nullifiers, commitments, ciphertexts,
 * and authorization signatures.
 * 
 * @param actions Vector of Orchard actions from the transaction bundle
 * @return UniValue JSON array of action objects with unified spend/output data
 */
UniValue TxActionsToJSON(const rust::Vec<orchard_bundle::Action>& actions)
{
    UniValue arr(UniValue::VARR);
    for (const auto& action : actions) {
        UniValue obj(UniValue::VOBJ);
        auto cv = action.cv();
        obj.pushKV("cv", HexStr(cv.begin(), cv.end()));
        auto nullifier = action.nullifier();
        obj.pushKV("nullifier", HexStr(nullifier.begin(), nullifier.end()));
        auto rk = action.rk();
        obj.pushKV("rk", HexStr(rk.begin(), rk.end()));
        auto cmx = action.cmx();
        obj.pushKV("cmx", HexStr(cmx.begin(), cmx.end()));
        auto ephemeralKey = action.ephemeral_key();
        obj.pushKV("ephemeralKey", HexStr(ephemeralKey.begin(), ephemeralKey.end()));
        auto encCiphertext = action.enc_ciphertext();
        obj.pushKV("encCiphertext", HexStr(encCiphertext.begin(), encCiphertext.end()));
        auto outCiphertext = action.out_ciphertext();
        obj.pushKV("outCiphertext", HexStr(outCiphertext.begin(), outCiphertext.end()));
        auto spendAuthSig = action.spend_auth_sig();
        obj.pushKV("spendAuthSig", HexStr(spendAuthSig.begin(), spendAuthSig.end()));
        arr.push_back(obj);
    }
    return arr;
}

/**
 * @brief Converts Orchard transaction bundle to JSON format with error handling
 * 
 * Extracts and serializes Orchard bundle data including actions, value balance,
 * flags, anchors, proofs, and binding signatures. Implements defensive programming
 * to handle missing bundles and processing errors gracefully.
 * 
 * @param tx Transaction containing the Orchard bundle
 * @param entry Reference to transaction JSON object (unused but kept for consistency)
 * @return UniValue JSON object containing Orchard bundle data or empty object if unavailable
 */
UniValue TxOrchardBundleToJSON(const CTransaction& tx, UniValue& entry)
{
    UniValue obj(UniValue::VOBJ);
    
    // Check if the transaction has an Orchard bundle
    if (!tx.GetOrchardBundle().IsPresent()) {
        return obj; // Return empty object if no Orchard bundle
    }
    
    try {
        const auto& bundle = tx.GetOrchardBundle().GetDetails();

        auto actions = bundle.actions();
        obj.pushKV("actions", TxActionsToJSON(actions));
        auto valueBalanceZat = bundle.value_balance_zat();
        obj.pushKV("valueBalance", ValueFromAmount(valueBalanceZat));
        obj.pushKV("valueBalanceZat", valueBalanceZat);
        
        // If this tx has no actions, then flags, anchor, etc. are not present.
        if (!actions.empty()) {
            {
                UniValue obj_flags{UniValue::VOBJ};
                auto enableSpends = bundle.enable_spends();
                obj_flags.pushKV("enableSpends", enableSpends);
                auto enableOutputs = bundle.enable_outputs();
                obj_flags.pushKV("enableOutputs", enableOutputs);
                obj.pushKV("flags", obj_flags);
            }
            auto anchor = bundle.anchor();
            obj.pushKV("anchor", HexStr(anchor.begin(), anchor.end()));
            auto proof = bundle.proof();
            obj.pushKV("proof", HexStr(proof.begin(), proof.end()));
            auto bindingSig = bundle.binding_sig();
            obj.pushKV("bindingSig", HexStr(bindingSig.begin(), bindingSig.end()));
        }
    } catch (const std::exception& e) {
        // If there's any error processing the Orchard bundle, return empty object
        return UniValue(UniValue::VOBJ);
    }
    
    return obj;
}

/**
 * @brief Utility function to check if a UTXO has been spent
 * 
 * Queries the spent index to determine if a specific transaction output
 * has been consumed by a subsequent transaction.
 * 
 * @param spenttxid Reference to store the spending transaction ID
 * @param txid Transaction ID of the output to check
 * @param vout Output index within the transaction
 * @return Input index in spending transaction, or -1 if unspent
 */
int32_t myIsutxo_spent(uint256 &spenttxid,uint256 txid,int32_t vout)
{
    CSpentIndexValue spentInfo; CSpentIndexKey spentKey(txid,vout);
    if ( GetSpentIndex(spentKey,spentInfo) )
    {
        spenttxid = spentInfo.txid;
        return((int32_t)spentInfo.inputIndex);
        // out.push_back(Pair("spentHeight", spentInfo.blockHeight));
    }
    spenttxid.SetNull();
    return(-1);
}

/**
 * @brief Converts transaction to comprehensive JSON representation
 * 
 * Primary transaction serialization function supporting all transaction types including
 * transparent, Sprout JoinSplit, Sapling, and Orchard. Handles version-specific features,
 * block context, confirmation data, and optional components like notarization info.
 * 
 * Key features:
 * - Multi-version transaction support (v1-v5)
 * - Shielded transaction data (JoinSplit, Sapling, Orchard)
 * - Input/output analysis with spent index information
 * - Block context and confirmation tracking
 * - Interest calculation for time-locked outputs
 * - Import/export transaction handling
 * 
 * @param tx Transaction to serialize
 * @param hashBlock Block hash containing transaction (null hash if mempool)
 * @param entry Output JSON object to populate with transaction data
 * @param isTxBuilder Whether output is for transaction builder (affects txid inclusion)
 * @param nHeight Block height (0 if not in block)
 * @param nConfirmations Number of confirmations (0 if not confirmed)
 * @param nBlockTime Block timestamp (0 if not in block)
 * @param includeHex Whether to include raw hex-encoded transaction data
 */
void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, bool isTxBuilder = false, int nHeight = 0, int nConfirmations = 0, int nBlockTime = 0, bool includeHex = true)
{
    uint256 notarized_hash,notarized_desttxid; int32_t prevMoMheight,notarized_height;
    notarized_height = komodo_notarized_height(&prevMoMheight,&notarized_hash,&notarized_desttxid);
    const uint256 txid = tx.GetHash();
    if (!isTxBuilder)
        entry.pushKV("txid", txid.GetHex());
    entry.pushKV("authdigest", tx.GetAuthDigest().GetHex());
    entry.pushKV("size", (int)::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION));
    entry.pushKV("overwintered", tx.fOverwintered);
    entry.pushKV("version", tx.nVersion);
    entry.pushKV("last_notarized_height", notarized_height);
    if (tx.fOverwintered) {
        entry.pushKV("versiongroupid", HexInt(tx.nVersionGroupId));
    }
    entry.pushKV("locktime", (int64_t)tx.nLockTime);
    if (tx.fOverwintered) {
        entry.pushKV("expiryheight", (int64_t)tx.nExpiryHeight);
    }

    if (includeHex) {
        entry.pushKV("hex", EncodeHexTx(tx));
    }

    UniValue vin(UniValue::VARR);
    BOOST_FOREACH(const CTxIn& txin, tx.vin) {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.push_back(Pair("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
        else if (tx.IsCoinImport() && txin.prevout.n==10e8) {
            in.push_back(Pair("is_import", "1"));
            ImportProof proof; CTransaction burnTx; std::vector<CTxOut> payouts; CTxDestination importaddress;
            if (UnmarshalImportTx(tx, proof, burnTx, payouts))
            {
                if (burnTx.vout.size() == 0)
                    continue;
                in.push_back(Pair("txid", burnTx.GetHash().ToString()));
                in.push_back(Pair("value", ValueFromAmount(burnTx.vout.back().nValue)));
                in.push_back(Pair("valueSat", burnTx.vout.back().nValue));
                // extract op_return to get burn source chain.
                std::vector<uint8_t> burnOpret; std::string targetSymbol; uint32_t targetCCid; uint256 payoutsHash; std::vector<uint8_t>rawproof;
                if (UnmarshalBurnTx(burnTx, targetSymbol, &targetCCid, payoutsHash, rawproof))
                {
                    if (rawproof.size() > 0)
                    {
                        std::string sourceSymbol;
                        E_UNMARSHAL(rawproof, ss >> sourceSymbol);
                        in.push_back(Pair("address", "IMP-" + sourceSymbol + "-" + burnTx.GetHash().ToString()));
                    }
                }
            }
        }
        else {
            in.push_back(Pair("txid", txin.prevout.hash.GetHex()));
            in.push_back(Pair("vout", (int64_t)txin.prevout.n));
            {
                uint256 hash; CTransaction tx; CTxDestination address;
                if (GetTransaction(txin.prevout.hash,tx,hash,false))
                {
                    if (ExtractDestination(tx.vout[txin.prevout.n].scriptPubKey, address))
                        in.push_back(Pair("address", CBitcoinAddress(address).ToString()));
                }
            }
            UniValue o(UniValue::VOBJ);
            o.push_back(Pair("asm", ScriptToAsmStr(txin.scriptSig, true)));
            o.push_back(Pair("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
            in.push_back(Pair("scriptSig", o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (GetSpentIndex(spentKey, spentInfo)) {
                in.push_back(Pair("value", ValueFromAmount(spentInfo.satoshis)));
                in.push_back(Pair("valueSat", spentInfo.satoshis));
                if (spentInfo.addressType == 1) {
                    in.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                }
                else if (spentInfo.addressType == 2)  {
                    in.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                }
            }
        }
        in.push_back(Pair("sequence", (int64_t)txin.nSequence));
        vin.push_back(in);
    }
    entry.push_back(Pair("vin", vin));
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *tipindex,*pindex = it->second;
    uint64_t interest;
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.push_back(Pair("value", ValueFromAmount(txout.nValue)));
        if ( chainName.isKMD() && pindex != 0 && tx.nLockTime >= 500000000 && (tipindex= chainActive.Tip()) != 0 )
        {
            int64_t interest; int32_t txheight; uint32_t locktime;
            interest = komodo_accrued_interest(&txheight,&locktime,tx.GetHash(),i,0,txout.nValue,(int32_t)tipindex->nHeight);
            out.push_back(Pair("interest", ValueFromAmount(interest)));
        }
        out.push_back(Pair("valueZat", txout.nValue));
        out.push_back(Pair("n", (int64_t)i));
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        if (txout.scriptPubKey.IsOpReturn() && txout.nValue != 0)
        {
            std::vector<uint8_t> burnOpret; std::string targetSymbol; uint32_t targetCCid; uint256 payoutsHash; std::vector<uint8_t>rawproof;
            if (UnmarshalBurnTx(tx, targetSymbol, &targetCCid, payoutsHash, rawproof))
            {
                out.push_back(Pair("target", "EXPORT->" +  targetSymbol));
            }
        }
        out.push_back(Pair("scriptPubKey", o));

        // Add spent information if spentindex is enabled
        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(txid, i);
        if (GetSpentIndex(spentKey, spentInfo)) {
            out.push_back(Pair("spentTxId", spentInfo.txid.GetHex()));
            out.push_back(Pair("spentIndex", (int)spentInfo.inputIndex));
            out.push_back(Pair("spentHeight", spentInfo.blockHeight));
        }

        vout.push_back(out);
    }
    entry.push_back(Pair("vout", vout));

    UniValue vjoinsplit = TxJoinSplitToJSON(tx);
    entry.push_back(Pair("vjoinsplit", vjoinsplit));

    if (tx.fOverwintered) {
        if (tx.nVersion >= SAPLING_TX_VERSION) {
            const auto& bundle = tx.GetSaplingBundle().GetDetails();
            entry.pushKV("valueBalance", ValueFromAmount(tx.GetValueBalanceSapling()));
            entry.pushKV("valueBalanceZat", tx.GetValueBalanceSapling());
            UniValue vspenddesc = TxShieldedSpendsToJSON(bundle.spends());
            entry.pushKV("vShieldedSpend", vspenddesc);
            UniValue voutputdesc = TxShieldedOutputsToJSON(bundle.outputs());
            entry.pushKV("vShieldedOutput", voutputdesc);
            if (!(vspenddesc.empty() && voutputdesc.empty())) {
                auto bindingSig = bundle.binding_sig();
                entry.pushKV("bindingSig", HexStr(bindingSig.begin(), bindingSig.end()));
            }
        }
        if (tx.nVersion >= ORCHARD_TX_VERSION && tx.GetOrchardBundle().IsPresent()) {
            UniValue orchard = TxOrchardBundleToJSON(tx, entry);
            if (!orchard.empty()) {
                entry.pushKV("orchard", orchard);
            }
        }
    }

    if (!hashBlock.IsNull()) {
        entry.push_back(Pair("blockhash", hashBlock.GetHex()));

        if (nConfirmations > 0) {
            entry.push_back(Pair("height", nHeight));
            entry.push_back(Pair("confirmations", komodo_dpowconfs(nHeight,nConfirmations)));
            entry.push_back(Pair("rawconfirmations", nConfirmations));
            entry.push_back(Pair("time", nBlockTime));
            entry.push_back(Pair("blocktime", nBlockTime));
        } else {
            BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                CBlockIndex* pindex = (*mi).second;
                if (chainActive.Contains(pindex)) {
                    entry.push_back(Pair("height", pindex->nHeight));
                    entry.push_back(Pair("rawconfirmations", 1 + chainActive.Height() - pindex->nHeight));
                    entry.push_back(Pair("confirmations", komodo_dpowconfs(pindex->nHeight,1 + chainActive.Height() - pindex->nHeight)));
                    entry.push_back(Pair("time", pindex->GetBlockTime()));
                    entry.push_back(Pair("blocktime", pindex->GetBlockTime()));
                } else {
                    entry.push_back(Pair("height", -1));
                    entry.push_back(Pair("confirmations", 0));
                    entry.push_back(Pair("rawconfirmations", 0));
                }
            }
        }
    }
}

/**
 * @brief RPC command to retrieve raw transaction data by transaction ID
 * 
 * Fetches transaction data from mempool or blockchain and returns either raw hex
 * or detailed JSON representation. Supports all transaction types including
 * transparent, Sapling, and Orchard shielded transactions.
 * 
 * @param params RPC parameters: [txid, verbose]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Raw hex string (verbose=0) or detailed JSON object (verbose=1)
 */
UniValue getrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getrawtransaction \"txid\" ( verbose )\n"
            "\nReturn the raw transaction data for a given transaction id.\n"
            "Retrieves complete transaction information including transparent, Sapling, and Orchard data.\n"
            "\nNOTE: By default this function only works sometimes. This is when the tx is in the mempool\n"
            "or there is an unspent output in the utxo for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option.\n"
            "\nIf verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.\n"
            "If verbose is non-zero, returns an Object with information about 'txid'.\n"

            "\nArguments:\n"
            "1. \"txid\"      (string, required) The transaction id\n"
            "2. verbose       (numeric, optional, default=0) If 0, return a string, other return a json object\n"

            "\nResult (if verbose is not set or set to 0):\n"
            "\"data\"      (string) The serialized, hex-encoded data for 'txid'\n"

            "\nResult (if verbose > 0):\n"
            "{\n"
            "  \"hex\" : \"data\",         (string) The serialized, hex-encoded data for 'txid'\n"
            "  \"txid\" : \"id\",          (string) The transaction id (same as provided)\n"
            "  \"authdigest\" : \"hex\",   (string) The transaction authorization digest\n"
            "  \"size\" : n,               (numeric) The transaction size in bytes\n"
            "  \"overwintered\" : bool,    (boolean) The Overwintered flag\n"
            "  \"version\" : n,            (numeric) The transaction version\n"
            "  \"versiongroupid\" : \"hex\", (string, optional) The version group id (Overwintered txs)\n"
            "  \"locktime\" : ttt,         (numeric) The lock time\n"
            "  \"expiryheight\" : ttt,     (numeric, optional) The block height after which the transaction expires\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n      (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [              (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"valueZat\" : nnn,           (numeric) The value in zatoshis\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"pirateaddress\"          (string) Pirate address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [        (array of json objects, only for version >= 2)\n"
            "     {\n"
            "       \"vpub_old\" : x.xxx,         (numeric) public input value in ARRR\n"
            "       \"vpub_new\" : x.xxx,         (numeric) public output value in ARRR\n"
            "       \"anchor\" : \"hex\",         (string) the anchor\n"
            "       \"nullifiers\" : [            (json array of string)\n"
            "         \"hex\"                     (string) input note nullifier\n"
            "         ,...\n"
            "       ],\n"
            "       \"commitments\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note commitment\n"
            "         ,...\n"
            "       ],\n"
            "       \"onetimePubKey\" : \"hex\",  (string) the onetime public key used to encrypt the ciphertexts\n"
            "       \"randomSeed\" : \"hex\",     (string) the random seed\n"
            "       \"macs\" : [                  (json array of string)\n"
            "         \"hex\"                     (string) input note MAC\n"
            "         ,...\n"
            "       ],\n"
            "       \"proof\" : \"hex\",          (string) the zero-knowledge proof\n"
            "       \"ciphertexts\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note ciphertext\n"
            "         ,...\n"
            "       ]\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"valueBalance\" : x.xxx,   (numeric, optional) Sapling value balance (if tx version >= 4)\n"
            "  \"valueBalanceZat\" : nnn,  (numeric, optional) Sapling value balance in zatoshis\n"
            "  \"vShieldedSpend\" : [      (array of json objects, only for version >= 4)\n"
            "     {\n"
            "       \"cv\" : \"hex\",            (string) Value commitment\n"
            "       \"anchor\" : \"hex\",        (string) Merkle tree anchor\n"
            "       \"nullifier\" : \"hex\",     (string) Nullifier\n"
            "       \"rk\" : \"hex\",            (string) Randomized public key\n"
            "       \"proof\" : \"hex\",         (string) Zero-knowledge proof\n"
            "       \"spendAuthSig\" : \"hex\"   (string) Spending authorization signature\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vShieldedOutput\" : [     (array of json objects, only for version >= 4)\n"
            "     {\n"
            "       \"cv\" : \"hex\",            (string) Value commitment\n"
            "       \"cmu\" : \"hex\",           (string) Note commitment\n"
            "       \"ephemeralKey\" : \"hex\",  (string) Ephemeral public key\n"
            "       \"encCiphertext\" : \"hex\", (string) Encrypted note ciphertext\n"
            "       \"outCiphertext\" : \"hex\", (string) Output ciphertext\n"
            "       \"proof\" : \"hex\"          (string) Zero-knowledge proof\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"bindingSig\" : \"hex\",    (string, optional) Sapling binding signature (if Sapling data present)\n"
            "  \"orchard\" : {             (object, optional) Orchard bundle data (if tx version >= 5)\n"
            "    \"actions\" : [           (array of json objects)\n"
            "       {\n"
            "         \"cv\" : \"hex\",          (string) Value commitment\n"
            "         \"nullifier\" : \"hex\",   (string) Nullifier\n"
            "         \"rk\" : \"hex\",          (string) Randomized public key\n"
            "         \"cmx\" : \"hex\",         (string) Note commitment\n"
            "         \"ephemeralKey\" : \"hex\", (string) Ephemeral public key\n"
            "         \"encCiphertext\" : \"hex\", (string) Encrypted note ciphertext\n"
            "         \"outCiphertext\" : \"hex\", (string) Output ciphertext\n"
            "         \"spendAuthSig\" : \"hex\"  (string) Spending authorization signature\n"
            "       }\n"
            "       ,...\n"
            "    ],\n"
            "    \"valueBalance\" : x.xxx,   (numeric) Orchard value balance\n"
            "    \"valueBalanceZat\" : nnn,  (numeric) Orchard value balance in zatoshis\n"
            "    \"flags\" : {              (object, optional)\n"
            "      \"enableSpends\" : bool, (boolean) Whether spends are enabled\n"
            "      \"enableOutputs\" : bool (boolean) Whether outputs are enabled\n"
            "    },\n"
            "    \"anchor\" : \"hex\",       (string, optional) Merkle tree anchor\n"
            "    \"proof\" : \"hex\",        (string, optional) Zero-knowledge proof\n"
            "    \"bindingSig\" : \"hex\"    (string, optional) Binding signature\n"
            "  },\n"
            "  \"blockhash\" : \"hash\",   (string) the block hash\n"
            "  \"height\" : n,            (numeric) The block height containing the transaction\n"
            "  \"confirmations\" : n,      (numeric) The number of notarized DPoW confirmations\n"
            "  \"rawconfirmations\" : n,   (numeric) The number of raw confirmations\n"
            "  \"time\" : ttt,             (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"blocktime\" : ttt         (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("getrawtransaction", "\"mytxid\"")
            + HelpExampleCli("getrawtransaction", "\"mytxid\" 1")
            + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1")
        );


    uint256 hash = ParseHashV(params[0], "parameter 1");

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    CTransaction tx;
    uint256 hashBlock;
    int nHeight = 0;
    int nConfirmations = 0;
    int nBlockTime = 0;

    {
        LOCK(cs_main);
        if (!GetTransaction(hash, tx, hashBlock, true))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available about transaction");

        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) {
            CBlockIndex* pindex = (*mi).second;
            if (chainActive.Contains(pindex)) {
                nHeight = pindex->nHeight;
                nConfirmations = 1 + chainActive.Height() - pindex->nHeight;
                nBlockTime = pindex->GetBlockTime();
            } else {
                nHeight = -1;
                nConfirmations = 0;
                nBlockTime = pindex->GetBlockTime();
            }
        }
    }

    string strHex = EncodeHexTx(tx);

    if (!fVerbose)
        return strHex;

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, hashBlock, result, false, nHeight, nConfirmations, nBlockTime);
    return result;
}

/**
 * @brief RPC command to generate Merkle proofs for transaction inclusion
 * 
 * Constructs cryptographic proof that specified transactions were included in
 * a blockchain block using Merkle tree verification. Enables efficient transaction
 * validation without downloading complete block data.
 * 
 * @param params RPC parameters: [txids, blockhash]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Hex-encoded Merkle proof data
 */
UniValue gettxoutproof(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
            "gettxoutproof [\"txid\",...] ( blockhash )\n"
            "\nReturns a hex-encoded Merkle proof that one or more specified transactions were included in a block.\n"
            "\nNOTE: By default this function only works sometimes. This is when there is an\n"
            "unspent output in the UTXO set for this transaction. To make it always work,\n"
            "you need to maintain a transaction index, using the -txindex command line option or\n"
            "specify the block in which the transaction is included manually (by blockhash).\n"
            "\nProvides cryptographic proof of transaction inclusion in the Pirate blockchain.\n"
            "\nArguments:\n"
            "1. \"txids\"       (array, required) A JSON array of transaction IDs to filter\n"
            "    [\n"
            "      \"txid\"     (string) A transaction hash\n"
            "      ,...\n"
            "    ]\n"
            "2. \"blockhash\"   (string, optional) If specified, looks for txids in the block with this hash\n"
            "\nResult:\n"
            "\"data\"           (string) A hex-encoded string containing the serialized Merkle proof\n"
            "\nExamples:\n"
            + HelpExampleCli("gettxoutproof", "\\\"[\\\"mytxid\\\"]\\\"")
            + HelpExampleCli("gettxoutproof", "\\\"[\\\"mytxid\\\"]\\\" \\\"myblockhash\\\"")
            + HelpExampleRpc("gettxoutproof", "\\\"[\\\"mytxid\\\"]\\\", \\\"myblockhash\\\"")
        );

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (size_t idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = NULL;

    uint256 hashBlock;
    if (params.size() > 1)
    {
        hashBlock = uint256S(params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        CCoins coins;
        if (pcoinsTip->GetCoins(oneTxid, coins) && coins.nHeight > 0 && coins.nHeight <= chainActive.Height())
            pblockindex = chainActive[coins.nHeight];
    }

    if (pblockindex == NULL)
    {
        CTransaction tx;
        if (!GetTransaction(oneTxid, tx, hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if(!ReadBlockFromDisk(block, pblockindex,1))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    BOOST_FOREACH(const CTransaction&tx, block.vtx)
        if (setTxids.count(tx.GetHash()))
            ntxFound++;
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    std::string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

/**
 * @brief RPC command to verify Merkle proofs for transaction inclusion
 * 
 * Validates cryptographic proofs generated by gettxoutproof to confirm that
 * transactions were included in blockchain blocks. Returns transaction IDs
 * that the proof commits to or empty array if verification fails.
 * 
 * @param params RPC parameters: [proof]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Array of transaction IDs verified by the proof
 */
UniValue verifytxoutproof(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "verifytxoutproof \"proof\"\n"
            "\nVerifies that a Merkle proof points to one or more transactions in a block, returning the\n"
            "transaction IDs it commits to and throwing an RPC error if the block is not in our best chain.\n"
            "\nThis function validates cryptographic inclusion proofs generated by gettxoutproof.\n"
            "\nArguments:\n"
            "1. \"proof\"    (string, required) The hex-encoded Merkle proof generated by gettxoutproof\n"
            "\nResult:\n"
            "[\"txid\"]      (array, strings) The transaction ID(s) which the proof commits to, or empty array if the proof is invalid\n"
            "\nExamples:\n"
            + HelpExampleCli("verifytxoutproof", "\\\"proof_hex_string\\\"")
            + HelpExampleRpc("verifytxoutproof", "\\\"proof_hex_string\\\"")
        );

    CDataStream ssMB(ParseHexV(params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    vector<uint256> vMatch;
    if (merkleBlock.txn.ExtractMatches(vMatch) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);
    uint256 idx = merkleBlock.header.GetHash();
    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) || (mapBlockIndex.count(idx) && !chainActive.Contains(mapBlockIndex[idx])))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    BOOST_FOREACH(const uint256& hash, vMatch)
        res.push_back(hash.GetHex());
    return res;
}

/**
 * @brief RPC command to create unsigned raw transaction from inputs and outputs
 * 
 * Constructs a raw transaction with specified inputs and outputs without signing.
 * Supports contextual transaction creation with Overwinter/Sapling features.
 * 
 * @param params RPC parameters: [inputs, outputs, locktime, expiryheight]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Hex-encoded raw transaction string
 */
UniValue createrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    string examplescriptPubKey = "21021ce1eac70455c3e6c52d67c133549b8aed4a588fba594372e8048e65c4f0fcb6ac";

    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            "createrawtransaction [{\"txid\":\"id\",\"vout\":n},...] {\"address\":amount,...} ( locktime ) ( expiryheight )\n"
            "\nCreate a transaction spending the given inputs and creating new outputs.\n"
            "Outputs can be Pirate addresses, standard scripts (in hex), or data.\n"
            "Returns hex-encoded raw transaction.\n"
            "Note that the transaction's inputs are not signed, and\n"
            "it is not stored in the wallet or transmitted to the network.\n"

            "\nArguments:\n"
            "1. \"inputs\"                (array, required) A JSON array of JSON objects\n"
            "     [\n"
            "       {\n"
            "         \"txid\":\"id\",    (string, required) The transaction id\n"
            "         \"vout\":n,         (numeric, required) The output number\n"
            "         \"sequence\":n      (numeric, optional) The sequence number\n"
            "       } \n"
            "       ,...\n"
            "     ]\n"
            "2. \"outputs\"               (object, required) A JSON object with outputs\n"
            "    {\n"
            "      \"address\": x.xxx,    (numeric or string, required) The key is the Pirate address or script (in hex), the numeric value (can be string) is the " + CURRENCY_UNIT + " amount\n"
            "      \"data\": \"hex\"      (string, required) The key is \"data\", the value is hex-encoded data\n"
            "      ,...\n"
            "    }\n"
            "3. locktime              (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs\n"
            "4. expiryheight          (numeric, optional, default=" + strprintf("%d", DEFAULT_TX_EXPIRY_DELTA) + ") Expiry height of transaction (if Overwinter is active)\n"
            "\nResult:\n"
            "\"transaction\"            (string) Hex string of the transaction\n"

            "\nExamples:\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\""+examplescriptPubKey+"\\\":0.01}\"")
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"data\\\":\\\"00010203\\\"}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\""+examplescriptPubKey+"\\\":0.01}\"")
            + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"data\\\":\\\"00010203\\\"}\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VOBJ)(UniValue::VNUM)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction rawTx = CreateNewContextualCMutableTransaction(
        Params().GetConsensus(), nextBlockHeight);

    if (params.size() > 2 && !params[2].isNull()) {
        int64_t nLockTime = params[2].get_int64();
        if (nLockTime < 0 || nLockTime > std::numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = static_cast<uint32_t>(nLockTime);
    }

    if (params.size() > 3 && !params[3].isNull()) {
        if (NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
            int64_t nExpiryHeight = params[3].get_int64();
            if (nExpiryHeight < 0 || nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, expiryheight must be nonnegative and less than %d.", TX_EXPIRY_HEIGHT_THRESHOLD));
            }
            rawTx.nExpiryHeight = nExpiryHeight;
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expiryheight can only be used if Overwinter is active when the transaction is mined");
        }
    }

    for (size_t idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? std::numeric_limits<uint32_t>::max() - 1 : std::numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum())
            nSequence = sequenceObj.get_int();

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    std::set<CTxDestination> destinations;
    //std::set<std::string> destinations;

    vector<string> addrList = sendTo.getKeys();

    if (addrList.size() != sendTo.size()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid outputs")); // for checking edge case, should never happened ...
    }

    //for (const std::string& name_ : addrList) {
    for (size_t idx = 0; idx < sendTo.size(); idx++) {

        const std::string& name_ = addrList[idx];

        CScript scriptPubKey;
        CTxDestination destination;

        if (name_ == "data") {
            std::vector<unsigned char> data = ParseHexV(sendTo[name_].getValStr(),"Data");
            CTxOut out(0, CScript() << OP_RETURN << data);
            rawTx.vout.push_back(out);
        } else {
            destination = DecodeDestination(name_);
            if (IsValidDestination(destination)) {
                scriptPubKey = GetScriptForDestination(destination);
            } else if (IsHex(name_)) {
                std::vector<unsigned char> data(ParseHex(name_));
                scriptPubKey = CScript(data.begin(), data.end());
                // destination is not valid, but we should convert it to valid anyway, to be able to check duplicates,
                // so we need to get destination from existing scriptPubKey via ExtractDestination
                if (!(ExtractDestination(scriptPubKey, destination) &&
                    (scriptPubKey.IsPayToPublicKeyHash() || scriptPubKey.IsPayToPublicKey() || scriptPubKey.IsPayToScriptHash())
                    )) {
                        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid script: ") + name_ + std::string(" (only P2PKH, P2PK and P2SH scripts are allowed)"));
                }
            }
            else {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, std::string("Invalid Komodo address or script: ") + name_);
            }

            if (!(fExperimentalMode && IS_KOMODO_NOTARY)) {
                // support of sending duplicates in createrawtransaction requires experimental features enabled and
                // notary flag, to prevent common users to get messed up with duplicates

                //if (!destinations.insert(EncodeDestination(destination)).second) {
                if (!destinations.insert(destination).second) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, std::string("Invalid parameter, duplicated destination: ") + name_);
                }
            }

            // CAmount nAmount = AmountFromValue(sendTo[name_]);    // operator[](const std::string& key) const;
            CAmount nAmount = AmountFromValue(sendTo[idx]);         // operator[](size_t index) const;

            CTxOut out(nAmount, scriptPubKey);
            rawTx.vout.push_back(out);
        }
    }

    return EncodeHexTx(rawTx);
}

/**
 * @brief RPC command to decode hex-encoded transaction or transaction builder data
 * 
 * Parses hex-encoded transaction data and returns detailed JSON representation.
 * Supports both completed transactions and transaction builder instructions with
 * decryption capabilities for shielded transaction components.
 * 
 * @param params RPC parameters: [hexstring]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Detailed JSON object containing decoded transaction data
 */
UniValue decoderawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decoderawtransaction \"hexstring\"\n"
            "\nReturn a JSON object representing the serialized, hex-encoded transaction.\n"
            "Supports all transaction types including transparent, Sapling, and Orchard shielded transactions.\n"

            "\nArguments:\n"
            "1. \"hexstring\" (string, required) The transaction hex string\n"

            "\nResult:\n"
            "{\n"
            "  \"txid\" : \"id\",        (string) The transaction id\n"
            "  \"authdigest\" : \"hex\",   (string) The transaction authorization digest\n"
            "  \"size\" : n,               (numeric) The transaction size in bytes\n"
            "  \"overwintered\" : bool,    (boolean) The Overwintered flag\n"
            "  \"version\" : n,            (numeric) The version\n"
            "  \"versiongroupid\": \"hex\"   (string, optional) The version group id (Overwintered txs)\n"
            "  \"locktime\" : ttt,       (numeric) The lock time\n"
            "  \"expiryheight\" : n,     (numeric, optional) Last valid block height for mining transaction (Overwintered txs)\n"
            "  \"vin\" : [               (array of json objects)\n"
            "     {\n"
            "       \"txid\": \"id\",    (string) The transaction id\n"
            "       \"vout\": n,         (numeric) The output number\n"
            "       \"scriptSig\": {     (json object) The script\n"
            "         \"asm\": \"asm\",  (string) asm\n"
            "         \"hex\": \"hex\"   (string) hex\n"
            "       },\n"
            "       \"sequence\": n     (numeric) The script sequence number\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vout\" : [             (array of json objects)\n"
            "     {\n"
            "       \"value\" : x.xxx,            (numeric) The value in " + CURRENCY_UNIT + "\n"
            "       \"valueZat\" : nnn,           (numeric) The value in zatoshis\n"
            "       \"n\" : n,                    (numeric) index\n"
            "       \"scriptPubKey\" : {          (json object)\n"
            "         \"asm\" : \"asm\",          (string) the asm\n"
            "         \"hex\" : \"hex\",          (string) the hex\n"
            "         \"reqSigs\" : n,            (numeric) The required sigs\n"
            "         \"type\" : \"pubkeyhash\",  (string) The type, eg 'pubkeyhash'\n"
            "         \"addresses\" : [           (json array of string)\n"
            "           \"zs1...\"                (string) Pirate address\n"
            "           ,...\n"
            "         ]\n"
            "       }\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vjoinsplit\" : [        (array of json objects, only for version >= 2)\n"
            "     {\n"
            "       \"vpub_old\" : x.xxx,         (numeric) public input value in ARRR\n"
            "       \"vpub_new\" : x.xxx,         (numeric) public output value in ARRR\n"
            "       \"anchor\" : \"hex\",         (string) the anchor\n"
            "       \"nullifiers\" : [            (json array of string)\n"
            "         \"hex\"                     (string) input note nullifier\n"
            "         ,...\n"
            "       ],\n"
            "       \"commitments\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note commitment\n"
            "         ,...\n"
            "       ],\n"
            "       \"onetimePubKey\" : \"hex\",  (string) the onetime public key used to encrypt the ciphertexts\n"
            "       \"randomSeed\" : \"hex\",     (string) the random seed\n"
            "       \"macs\" : [                  (json array of string)\n"
            "         \"hex\"                     (string) input note MAC\n"
            "         ,...\n"
            "       ],\n"
            "       \"proof\" : \"hex\",          (string) the zero-knowledge proof\n"
            "       \"ciphertexts\" : [           (json array of string)\n"
            "         \"hex\"                     (string) output note ciphertext\n"
            "         ,...\n"
            "       ]\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"valueBalance\" : x.xxx,   (numeric, optional) Sapling value balance (if tx version >= 4)\n"
            "  \"valueBalanceZat\" : nnn,  (numeric, optional) Sapling value balance in zatoshis\n"
            "  \"vShieldedSpend\" : [      (array of json objects, only for version >= 4)\n"
            "     {\n"
            "       \"cv\" : \"hex\",            (string) Value commitment\n"
            "       \"anchor\" : \"hex\",        (string) Merkle tree anchor\n"
            "       \"nullifier\" : \"hex\",     (string) Nullifier\n"
            "       \"rk\" : \"hex\",            (string) Randomized public key\n"
            "       \"proof\" : \"hex\",         (string) Zero-knowledge proof\n"
            "       \"spendAuthSig\" : \"hex\"   (string) Spending authorization signature\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"vShieldedOutput\" : [     (array of json objects, only for version >= 4)\n"
            "     {\n"
            "       \"cv\" : \"hex\",            (string) Value commitment\n"
            "       \"cmu\" : \"hex\",           (string) Note commitment\n"
            "       \"ephemeralKey\" : \"hex\",  (string) Ephemeral public key\n"
            "       \"encCiphertext\" : \"hex\", (string) Encrypted note ciphertext\n"
            "       \"outCiphertext\" : \"hex\", (string) Output ciphertext\n"
            "       \"proof\" : \"hex\"          (string) Zero-knowledge proof\n"
            "     }\n"
            "     ,...\n"
            "  ],\n"
            "  \"bindingSig\" : \"hex\",    (string, optional) Sapling binding signature (if Sapling data present)\n"
            "  \"orchard\" : {             (object, optional) Orchard bundle data (if tx version >= 5)\n"
            "    \"actions\" : [           (array of json objects)\n"
            "       {\n"
            "         \"cv\" : \"hex\",          (string) Value commitment\n"
            "         \"nullifier\" : \"hex\",   (string) Nullifier\n"
            "         \"rk\" : \"hex\",          (string) Randomized public key\n"
            "         \"cmx\" : \"hex\",         (string) Note commitment\n"
            "         \"ephemeralKey\" : \"hex\", (string) Ephemeral public key\n"
            "         \"encCiphertext\" : \"hex\", (string) Encrypted note ciphertext\n"
            "         \"outCiphertext\" : \"hex\", (string) Output ciphertext\n"
            "         \"spendAuthSig\" : \"hex\"  (string) Spending authorization signature\n"
            "       }\n"
            "       ,...\n"
            "    ],\n"
            "    \"valueBalance\" : x.xxx,   (numeric) Orchard value balance\n"
            "    \"valueBalanceZat\" : nnn,  (numeric) Orchard value balance in zatoshis\n"
            "    \"flags\" : {              (object, optional)\n"
            "      \"enableSpends\" : bool, (boolean) Whether spends are enabled\n"
            "      \"enableOutputs\" : bool (boolean) Whether outputs are enabled\n"
            "    },\n"
            "    \"anchor\" : \"hex\",       (string, optional) Merkle tree anchor\n"
            "    \"proof\" : \"hex\",        (string, optional) Zero-knowledge proof\n"
            "    \"bindingSig\" : \"hex\"    (string, optional) Binding signature\n"
            "  },\n"
            "  \"rawtype\" : \"type\",       (string) Either \"raw\" for decoded transaction or \"instructions\" for transaction builder\n"
            "  \"decryptedinputs\" : [...], (array, optional) Decrypted shielded input information (if wallet enabled)\n"
            "  \"decryptedoutputs\" : [...], (array, optional) Decrypted shielded output information (if wallet enabled)\n"
            "  \"rawinputs\" : [...],       (array, optional) Raw transaction builder input information (if transaction builder)\n"
            "  \"rawoutputs\" : [...],      (array, optional) Raw transaction builder output information (if transaction builder)\n"
            "  \"checksum\" : \"hex\"       (string) Transaction builder checksum (if transaction builder)\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("decoderawtransaction", "\"hexstring\"")
            + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    string strHexTx = params[0].get_str();
    string strHexTb = params[0].get_str();
    if (!IsHex(strHexTx))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid hex string");

    CTransaction tx;
    TransactionBuilder tb;

    bool isTx = DecodeHexTx(tx, strHexTx);

    if (!isTx) {
      vector<unsigned char> txBuilder(ParseHex(strHexTb));
      CDataStream ssData(txBuilder, SER_NETWORK, PROTOCOL_VERSION);
      try {
          ssData >> tb;
          tx = tb.getTransaction();
      }
      catch (const std::exception&) {
          throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
      }
    }




    UniValue result(UniValue::VOBJ);

    if (!isTx) {
        result.push_back(Pair("rawtype","instuctions"));
        TxToJSON(tx, uint256(), result, true, 0, 0, 0, false);

        UniValue inputs(UniValue::VARR);
        for (int i = 0; i < tb.vSaplingSpends.size(); i++) {
            //Return Hex encoded serialized merkle path
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << tb.vSaplingSpends[i].saplingMerklePath;

            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type","sapling"));
            input.push_back(Pair("fromaddr",EncodePaymentAddress(tb.vSaplingSpends[i].addr)));
            input.push_back(Pair("value", ValueFromAmount(CAmount(tb.vSaplingSpends[i].value))));
            input.push_back(Pair("valueZat", tb.vSaplingSpends[i].value));
            input.push_back(Pair("txid",tb.vSaplingSpends[i].op.hash.ToString()));
            input.push_back(Pair("shieldedoutputindex",(uint64_t)tb.vSaplingSpends[i].op.n));
            input.push_back(Pair("rcm", tb.vSaplingSpends[i].rcm.ToString()));
            input.push_back(Pair("merklepath",HexStr(ss.begin(), ss.end())));
            input.push_back(Pair("anchor", tb.vSaplingSpends[i].anchor.ToString()));

            inputs.push_back(input);
        }

        for (int i = 0; i < tb.vOrchardSpends.size(); i++) {
            //Return Hex encoded serialized merkle path
            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << tb.vOrchardSpends[i].orchardMerklePath;

            UniValue input(UniValue::VOBJ);
            input.push_back(Pair("type","orchard"));
            input.push_back(Pair("fromaddr",EncodePaymentAddress(tb.vOrchardSpends[i].addr)));
            input.push_back(Pair("value", ValueFromAmount(CAmount(tb.vOrchardSpends[i].value))));
            input.push_back(Pair("valueZat", tb.vOrchardSpends[i].value));
            input.push_back(Pair("txid",tb.vOrchardSpends[i].op.hash.ToString()));
            input.push_back(Pair("shieldedoutputindex",(uint64_t)tb.vOrchardSpends[i].op.n));
            input.push_back(Pair("rho", tb.vOrchardSpends[i].rho.ToString()));
            input.push_back(Pair("rseed", tb.vOrchardSpends[i].rseed.ToString()));
            input.push_back(Pair("merklepath",HexStr(ss.begin(), ss.end())));
            input.push_back(Pair("anchor", tb.vOrchardSpends[i].anchor.ToString()));

            inputs.push_back(input);
        }

        UniValue outputs(UniValue::VARR);
        for (int i = 0; i < tb.vSaplingOutputs.size(); i++) {
            UniValue output(UniValue::VOBJ);
            output.push_back(Pair("type","sapling"));
            output.push_back(Pair("toaddr",EncodePaymentAddress(tb.vSaplingOutputs[i].addr)));
            output.push_back(Pair("value", ValueFromAmount(CAmount(tb.vSaplingOutputs[i].value))));
            output.push_back(Pair("valueZat", tb.vSaplingOutputs[i].value));;

            auto memoBytes = tb.vSaplingOutputs[i].memo.has_value() ? tb.vSaplingOutputs[i].memo.value().ToBytes() : libzcash::Memo::ToBytes(std::nullopt);
            output.push_back(Pair("memo", HexStr(memoBytes)));
            if (memoBytes[0] <= 0xf4) {
                // Trim off trailing zeroes
                auto end = std::find_if(
                    memoBytes.rbegin(),
                    memoBytes.rend(),
                    [](unsigned char v) { return v != 0; });
                std::string memoStr(memoBytes.begin(), end.base());
                if (utf8::is_valid(memoStr)) {
                    output.push_back(Pair("memoStr",memoStr));
                } else {
                    output.push_back(Pair("memoStr",""));
                }
            }

            outputs.push_back(output);
        }

        for (int i = 0; i < tb.vOrchardOutputs.size(); i++) {
            UniValue output(UniValue::VOBJ);
            output.push_back(Pair("type","orchard"));
            output.push_back(Pair("toaddr",EncodePaymentAddress(tb.vOrchardOutputs[i].addr)));
            output.push_back(Pair("value", ValueFromAmount(CAmount(tb.vOrchardOutputs[i].value))));
            output.push_back(Pair("valueZat", tb.vOrchardOutputs[i].value));;

            auto memoBytes = tb.vOrchardOutputs[i].memo.has_value() ? tb.vOrchardOutputs[i].memo.value().ToBytes() : libzcash::Memo::ToBytes(std::nullopt);
            output.push_back(Pair("memo", HexStr(memoBytes)));
            if (memoBytes[0] <= 0xf4) {
                // Trim off trailing zeroes
                auto end = std::find_if(
                    memoBytes.rbegin(),
                    memoBytes.rend(),
                    [](unsigned char v) { return v != 0; });
                std::string memoStr(memoBytes.begin(), end.base());
                if (utf8::is_valid(memoStr)) {
                    output.push_back(Pair("memoStr",memoStr));
                } else {
                    output.push_back(Pair("memoStr",""));
                }
            }

            outputs.push_back(output);
        }

        result.push_back(Pair("rawinputs",inputs));
        result.push_back(Pair("rawoutputs",outputs));
    } else {
        result.push_back(Pair("rawtype","raw"));
        TxToJSON(tx, uint256(), result, false, 0, 0, 0, false);

#ifdef ENABLE_WALLET
        RpcArcTransaction dtx;
        int nHeight = chainActive.Tip()->nHeight;
        decrypttransaction(tx, dtx, nHeight);

        UniValue spends(UniValue::VARR);
        getRpcArcTxJSONSpends(dtx, spends);
        result.push_back(Pair("decryptedinputs", spends));

        UniValue sends(UniValue::VARR);
        getRpcArcTxJSONSends(dtx, sends);
        result.push_back(Pair("decryptedoutputs", sends));
#endif
    }

    //Return Hex encoded serialized merkle path
    CDataStream cs(SER_NETWORK, PROTOCOL_VERSION);
    cs << tb.GetChecksum();
    result.push_back(Pair("checksum", HexStr(cs.begin(), cs.end())));

    return result;
}

/**
 * @brief RPC command to decode hex-encoded script data into human-readable format
 * 
 * Analyzes a hex-encoded script and extracts detailed information including
 * assembly representation, script type classification, required signatures,
 * and associated addresses for standard script patterns.
 * 
 * @param params RPC parameters: [hex]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue JSON object containing decoded script information
 */
UniValue decodescript(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "decodescript \"hex\"\n"
            "\nDecode a hex-encoded script and return detailed script information.\n"
            "Supports standard script types including P2PKH, P2PK, P2SH, and multisig.\n"
            "\nArguments:\n"
            "1. \"hex\"     (string, required) The hex-encoded script to decode\n"
            "\nResult:\n"
            "{\n"
            "  \"asm\":\"asm\",   (string) Script assembly representation\n"
            "  \"hex\":\"hex\",   (string) Hex-encoded script\n"
            "  \"type\":\"type\", (string) Script type (e.g., pubkeyhash, scripthash, multisig)\n"
            "  \"reqSigs\": n,    (numeric) Required signatures for multisig scripts\n"
            "  \"addresses\": [   (json array of string)\n"
            "     \"address\"     (string) Pirate address associated with script\n"
            "     ,...\n"
            "  ],\n"
            "  \"p2sh\":\"address\" (string) Pay-to-script-hash address for this script\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("decodescript", "\"76a914...88ac\"")
            + HelpExampleRpc("decodescript", "\"76a914...88ac\"")
        );

    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    r.push_back(Pair("p2sh", EncodeDestination(CScriptID(script))));
    return r;
}

/**
 * @brief Creates JSON error object for transaction input verification failures
 * 
 * Constructs a detailed error report for failed script verification or signing
 * operations, including transaction input details and error descriptions for
 * debugging purposes.
 * 
 * @param txin Transaction input that failed verification
 * @param vErrorsRet Reference to JSON array for appending error objects
 * @param strMessage Descriptive error message explaining the failure
 */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const std::string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.push_back(Pair("txid", txin.prevout.hash.ToString()));
    entry.push_back(Pair("vout", (uint64_t)txin.prevout.n));
    entry.push_back(Pair("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end())));
    entry.push_back(Pair("sequence", (uint64_t)txin.nSequence));
    entry.push_back(Pair("error", strMessage));
    vErrorsRet.push_back(entry);
}

/**
 * @brief RPC command to sign inputs of a raw transaction using available keys
 * 
 * Creates digital signatures for transaction inputs to authorize UTXO spending.
 * Supports multiple signature types, custom private keys, and partial signing
 * workflows for complex transaction construction scenarios.
 * 
 * @param params RPC parameters: [hexstring, prevtxs, privatekeys, sighashtype, branchid]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue JSON object with signed transaction hex and completion status
 */
UniValue signrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
            "signrawtransaction \"hexstring\" ( [{\"txid\":\"id\",\"vout\":n,\"scriptPubKey\":\"hex\",\"redeemScript\":\"hex\"},...] [\"privatekey1\",...] sighashtype )\n"
            "\nSign inputs for raw transaction (serialized, hex-encoded) using available keys.\n"
            "Creates digital signatures for transaction inputs to authorize spending of UTXO.\n"
            "The second optional argument is an array of previous transaction outputs that\n"
            "this transaction depends on but may not yet be in the blockchain.\n"
            "The third optional argument is an array of base58-encoded private\n"
            "keys that, if given, will be the only keys used to sign the transaction.\n"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif

            "\nArguments:\n"
            "1. \"hexstring\"     (string, required) The transaction hex string to sign\n"
            "2. \"prevtxs\"       (array, optional) Previous dependent transaction outputs\n"
            "     [               (json array of json objects, or 'null' if none provided)\n"
            "       {\n"
            "         \"txid\":\"id\",             (string, required) The transaction id\n"
            "         \"vout\":n,                  (numeric, required) The output number\n"
            "         \"scriptPubKey\": \"hex\",   (string, required) Script public key\n"
            "         \"redeemScript\": \"hex\",   (string, required for P2SH) Redeem script\n"
            "         \"amount\": value            (numeric, required) The amount spent in " + CURRENCY_UNIT + "\n"
            "       }\n"
            "       ,...\n"
            "    ]\n"
            "3. \"privatekeys\"     (array, optional) Private keys for signing\n"
            "    [                  (json array of strings, or 'null' if none provided)\n"
            "      \"privatekey\"   (string) Private key in base58-encoding\n"
            "      ,...\n"
            "    ]\n"
            "4. \"sighashtype\"     (string, optional, default=ALL) The signature hash type. Must be one of\n"
            "       \"ALL\"\n"
            "       \"NONE\"\n"
            "       \"SINGLE\"\n"
            "       \"ALL|ANYONECANPAY\"\n"
            "       \"NONE|ANYONECANPAY\"\n"
            "       \"SINGLE|ANYONECANPAY\"\n"
            "5.  \"branchid\"       (string, optional) The hex representation of the consensus branch id to sign with."
            " This can be used to force signing with consensus rules that are ahead of the node's current height.\n"

            "\nResult:\n"
            "{\n"
            "  \"hex\" : \"value\",           (string) The hex-encoded raw transaction with signature(s)\n"
            "  \"complete\" : true|false,   (boolean) If the transaction has a complete set of signatures\n"
            "  \"errors\" : [                 (json array of objects) Script verification errors (if there are any)\n"
            "    {\n"
            "      \"txid\" : \"hash\",           (string) The hash of the referenced, previous transaction\n"
            "      \"vout\" : n,                (numeric) The index of the output to spent and used as input\n"
            "      \"scriptSig\" : \"hex\",       (string) The hex-encoded signature script\n"
            "      \"sequence\" : n,            (numeric) Script sequence number\n"
            "      \"error\" : \"text\"           (string) Verification or signing error related to the input\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"")
            + HelpExampleRpc("signrawtransaction", "\"myhex\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VARR)(UniValue::VARR)(UniValue::VSTR)(UniValue::VSTR), true);

    vector<unsigned char> txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (const std::exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        BOOST_FOREACH(const CTxIn& txin, mergedTx.vin) {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].isNull()) {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        for (size_t idx = 0; idx < keys.size(); idx++) {
            UniValue k = keys[idx];
            CKey key = DecodeSecret(k.get_str());
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key");
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain)
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull()) {
        UniValue prevTxs = params[1].get_array();
        for (size_t idx = 0; idx < prevTxs.size(); idx++) {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR));

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0;
                if (prevOut.exists("amount")) {
                    coins->vout[nOut].nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
                RPCTypeCheckObj(prevOut, boost::assign::map_list_of("txid", UniValue::VSTR)("vout", UniValue::VNUM)("scriptPubKey", UniValue::VSTR)("redeemScript",UniValue::VSTR));
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    int nHashType = SIGHASH_ALL;
    if (params.size() > 3 && !params[3].isNull()) {
        static map<string, int> mapSigHashValues =
            boost::assign::map_list_of
            (string("ALL"), int(SIGHASH_ALL))
            (string("ALL|ANYONECANPAY"), int(SIGHASH_ALL|SIGHASH_ANYONECANPAY))
            (string("NONE"), int(SIGHASH_NONE))
            (string("NONE|ANYONECANPAY"), int(SIGHASH_NONE|SIGHASH_ANYONECANPAY))
            (string("SINGLE"), int(SIGHASH_SINGLE))
            (string("SINGLE|ANYONECANPAY"), int(SIGHASH_SINGLE|SIGHASH_ANYONECANPAY))
            ;
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    bool fHashSingle = ((nHashType & ~SIGHASH_ANYONECANPAY) == SIGHASH_SINGLE);
    // Use the approximate release height if it is greater so offline nodes
    // have a better estimation of the current height and will be more likely to
    // determine the correct consensus branch ID.  Regtest mode ignores release height.
    int chainHeight = chainActive.Height() + 1;

    // Grab the current consensus branch ID
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, Params().GetConsensus());

    if (params.size() > 4 && !params[4].isNull()) {
        consensusBranchId = ParseHexToUInt32(params[4].get_str());
        if (!IsConsensusBranchId(consensusBranchId)) {
            throw runtime_error(params[4].get_str() + " is not a valid consensus branch id");
        }
    }

    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    CMutableTransaction mergedTxsave = mergedTx;
    int32_t txpow,numiters = 0;
    const CTransaction txConst(mergedTx);

    std::vector<CTxOut> allPrevOutputs;
    for (const auto& input : txConst.vin) {
        allPrevOutputs.push_back(view.GetOutputFor(input));
    }
    PrecomputedTransactionData txdata(txConst, allPrevOutputs);

    if ( (txpow = ASSETCHAINS_TXPOW) != 0 )
    {
        if ( txConst.IsCoinBase() != 0 )
        {
            if ( (txpow & 2) == 0 )
                txpow = 0;
        }
        else
        {
            if ( (txpow & 1) == 0 )
                txpow = 0;
        }
    }
    while ( 1 )
    {
        if ( txpow != 0 )
            mergedTx = mergedTxsave;
        // Sign what we can:

        std::vector<CTxOut> allMergedPrevOutputs;
        for (const auto& input : mergedTx.vin) {
            allMergedPrevOutputs.push_back(view.GetOutputFor(input));
        }
        PrecomputedTransactionData mergedTxdata(mergedTx, allPrevOutputs);

        for (unsigned int i = 0; i < mergedTx.vin.size(); i++) {
            CTxIn& txin = mergedTx.vin[i];
            const CCoins* coins = view.AccessCoins(txin.prevout.hash);
            if (coins == NULL || !coins->IsAvailable(txin.prevout.n)) {
                TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
                continue;
            }
            const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;
            const CAmount& amount = coins->vout[txin.prevout.n].nValue;

            SignatureData sigdata;
            // Only sign SIGHASH_SINGLE if there's a corresponding output:
            if (!fHashSingle || (i < mergedTx.vout.size()))
                ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mergedTx, mergedTxdata, i, amount, nHashType), prevPubKey, sigdata, consensusBranchId);

            // ... and merge in other signatures:
            BOOST_FOREACH(const CMutableTransaction& txv, txVariants) {
                sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount, txdata), sigdata, DataFromTransaction(txv, i), consensusBranchId);
            }

            UpdateTransaction(mergedTx, i, sigdata);

            ScriptError serror = SCRIPT_ERR_OK;
            if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount, txdata), consensusBranchId, &serror)) {
                TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
            }
        }
        if ( txpow != 0 )
        {
            uint256 txid = mergedTx.GetHash();
            if ( ((uint8_t *)&txid)[0] == 0 && ((uint8_t *)&txid)[31] == 0 )
                break;
            //fprintf(stderr,"%d: tmp txid.%s\n",numiters,txid.GetHex().c_str());
        } else break;
        numiters++;
    }
    if ( numiters > 0 )
        fprintf(stderr,"ASSETCHAINS_TXPOW.%d txpow.%d numiters.%d for signature\n",ASSETCHAINS_TXPOW,txpow,numiters);
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hex", EncodeHexTx(mergedTx)));
    result.push_back(Pair("complete", fComplete));
    if (!vErrors.empty()) {
        result.push_back(Pair("errors", vErrors));
    }

    return result;
}

extern UniValue NSPV_broadcast(char *hex);

/**
 * @brief RPC command to broadcast a signed raw transaction to the network
 * 
 * Submits a hex-encoded signed transaction to the local node and propagates it
 * across the Pirate network for inclusion in the blockchain. Validates transaction
 * format and performs initial checks before broadcasting.
 * 
 * @param params RPC parameters: [hexstring, allowhighfees]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Transaction hash as hex string upon successful broadcast
 */
UniValue sendrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "sendrawtransaction \"hexstring\" ( allowhighfees )\n"
            "\nSubmits raw transaction (serialized, hex-encoded) to local node and network.\n"
            "Broadcasts the signed transaction to the Pirate network for inclusion in blockchain.\n"
            "\nAlso see createrawtransaction and signrawtransaction calls.\n"
            "\nArguments:\n"
            "1. \"hexstring\"    (string, required) The hex string of the raw transaction\n"
            "2. allowhighfees    (boolean, optional, default=false) Allow high fees\n"
            "\nResult:\n"
            "\"hex\"             (string) The transaction hash in hex\n"
            "\nExamples:\n"
            "\nCreate a transaction\n"
            + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") +
            "Sign the transaction, and get back the hex\n"
            + HelpExampleCli("signrawtransaction", "\"myhex\"") +
            "\nSend the transaction (signed hex)\n"
            + HelpExampleCli("sendrawtransaction", "\"signedhex\"") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
        );
    }
    LOCK(cs_main);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR)(UniValue::VBOOL));

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    uint256 hashTx = tx.GetHash();

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();
    if ( KOMODO_NSPV_FULLNODE )
    {
        CCoinsViewCache &view = *pcoinsTip;
        const CCoins* existingCoins = view.AccessCoins(hashTx);
        bool fHaveMempool = mempool.exists(hashTx);
        bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
        if (!fHaveMempool && !fHaveChain) {
            // push to local node and sync with wallets
            CValidationState state;
            bool fMissingInputs;
            if (!AcceptToMemoryPool(mempool, state, tx, false, &fMissingInputs, !fOverrideFees)) {
                if (state.IsInvalid()) {
                    throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
                } else {
                    if (fMissingInputs) {
                        throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                    }
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
                }
            }
        } else if (fHaveChain) {
            throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
        }
        RelayTransaction(tx);
    }
    else
    {
        NSPV_broadcast((char *)params[0].get_str().c_str());
    }

    return hashTx.GetHex();
}

/**
 * @brief RPC command to build and finalize shielded transactions from builder instructions
 * 
 * Processes transaction builder hex data to construct complete shielded transactions
 * with Sapling and Orchard components. Handles witness generation, proof creation,
 * and transaction signing for offline transaction construction workflows.
 * 
 * @param params RPC parameters: [hexstring]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Hex-encoded finalized transaction ready for broadcast
 */
UniValue z_buildrawtransaction(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
  if (fHelp || params.size() != 1)
      throw runtime_error(
          "z_buildrawtransaction \"hexstring\"\n"
          "\nBuild and finalize a shielded transaction from transaction builder data.\n"
          "Processes hex-encoded transaction builder instructions to create a complete\n"
          "shielded transaction for Sapling or Orchard pools.\n"

          "\nArguments:\n"
          "1. \"hex\"      (string, required) The transaction builder hex string\n"

          "\nResult:\n"
          "\"transaction\"  (string) Hex-encoded raw transaction ready for broadcast\n"

          "\nExamples:\n"
          + HelpExampleCli("z_buildrawtransaction", "\"hexstring\"")
          + HelpExampleRpc("z_buildrawtransaction", "\"hexstring\"")
      );

  LOCK2(cs_main, pwalletMain->cs_wallet);
  RPCTypeCheck(params, boost::assign::list_of(UniValue::VSTR));

  string strHexTb = params[0].get_str();
  if (!IsHex(strHexTb)) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Invalid hex string");
  }

  CTransaction tx;
  TransactionBuilder tb;

  if (DecodeHexTx(tx, strHexTb)) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX does not require building. If transaction is unsigned use signrawtransaction.");
  }

  //Deserialize hex encoded transaction builder
  vector<unsigned char> txBuilder(ParseHex(strHexTb));
  CDataStream ssData(txBuilder, SER_NETWORK, PROTOCOL_VERSION);
  try {
      ssData >> tb;
  }
  catch (const std::exception&) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction builder datastrem decode failed.");
  }

  //Validity Checks
  if (!tb.ValidateChecksum()) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction builder checksum is invalid.");
  }

  if (tb.vSaplingSpends.size() > 0 && tb.vOrchardSpends.size() > 0 ) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction builder contains both Sapling Spends and Orchard Spends, coins must be spent from Sapling Orchard.");
  }

  if (tb.vSaplingSpends.size() == 0 && tb.vOrchardSpends.size() == 0 ) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction builder does not contain Sapling Spends or Orchard Spends, coins must be spent from Sapling or Orchard.");
  }

  //Outgoing viewing key to be set by either the Sapling spending key or the Orchard spending key
  uint256 ovk;

  //Sapling
  if (tb.vSaplingSpends.size() > 0) {
      LogPrintf("Adding Sapling Spends\n");
      libzcash::SaplingExtendedSpendingKey primarySaplingKey;
      uint256 primaryAnchor;

      //Get the spending key for each address referenced
      for (int i = 0; i < tb.vSaplingSpends.size(); i++) {
          libzcash::SaplingPaymentAddress addr = tb.vSaplingSpends[i].addr;

          libzcash::SaplingIncomingViewingKey ivk;
          if (!pwalletMain->GetSaplingIncomingViewingKey(addr, ivk))
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Incoming Viewing key for Sapling Spend not found.");

          libzcash::SaplingExtendedSpendingKey extsk;
          if (!pwalletMain->GetSaplingExtendedSpendingKey(addr, extsk))
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Spending key for Sapling Spend not found.");

          //Validate the all of the spending keys are the same. Not a consensus rule, better for wallet useability.
          if (i == 0) {
            primarySaplingKey = extsk;
          } else if (!(primarySaplingKey == extsk)) {
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction with that use multiple spending keys are not supported.");
          }

          if (i == 0) {
            primaryAnchor = tb.vSaplingSpends[i].anchor;
          } else if (!(primaryAnchor == tb.vSaplingSpends[i].anchor)) {
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "All Sapling inputs must have the same anchor.");
          }

      }

      //Validate the anchor passed is not null
      if (primaryAnchor.IsNull()) {
          throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Sapling Anchor cannot be null.");
      }

      //Create a new sapling bulider before converting the store sapling notes
      tb.InitializeSapling();

      //Add the stored sapling notes to the sapling builder
      if (!tb.ConvertRawSaplingSpend(primarySaplingKey))
          throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX converting raw Sapling Spends failed.");

      //Get the OVK for the spending key being used
      ovk = primarySaplingKey.ToXFVK().fvk.ovk;
  }

  //Orchard
  bool orchardInitialized = false;
  if (tb.vOrchardSpends.size() > 0) {
      LogPrintf("Adding Orchard Spends\n");
      libzcash::OrchardExtendedSpendingKeyPirate primaryOrchardKey;
      uint256 primaryAnchor;

      //Get the spending key for each address referenced
      for (int i = 0; i < tb.vOrchardSpends.size(); i++) {
          libzcash::OrchardPaymentAddressPirate addr = tb.vOrchardSpends[i].addr;

          libzcash::OrchardIncomingViewingKeyPirate ivk;
          if (!pwalletMain->GetOrchardIncomingViewingKey(addr, ivk))
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Incoming Viewing key for Sapling Spend not found.");

          libzcash::OrchardExtendedSpendingKeyPirate extsk;
          if (!pwalletMain->GetOrchardExtendedSpendingKey(addr, extsk))
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Spending key for Sapling Spend not found.");

          //Validate the all of the spending keys are the same. Not a consensus rule, better for wallet useability.
          if (i == 0) {
            primaryOrchardKey= extsk;
          } else if (!(primaryOrchardKey == extsk)) {
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Transaction with that use multiple spending keys are not supported.");
          }

          if (i == 0) {
            primaryAnchor = tb.vOrchardSpends[i].anchor;
          } else if (!(primaryAnchor == tb.vOrchardSpends[i].anchor)) {
              throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "All Orchard inputs must have the same anchor.");
          }

      }

      //Validate the anchor passed is not null
      if (primaryAnchor.IsNull()) {
          throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Orchard Anchor cannot be null.");
      }

      //Create a new orchard bulider before converting the store orchard actions, Enable both Spend and Outputs
      tb.InitializeOrchard(true, true, primaryAnchor);
      orchardInitialized = true;

      //Add the stored orchard action to the orchard builder
      if (!tb.ConvertRawOrchardSpend(primaryOrchardKey)) {
          throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX converting raw Orchard Spends failed.");
      }

      //Get the OVK for the spending key being used
      auto fvkOpt = primaryOrchardKey.GetXFVK();
      if (fvkOpt == std::nullopt) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR,"FVK not found for Orchard spending key. Stopping.");
      }

      auto fvk = fvkOpt.value().fvk;
      auto ovkOpt = fvk.GetOVK();
      if (fvkOpt == std::nullopt) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR,"OVK not found for Orchard spending key. Stopping.");
      }

      ovk = ovkOpt.value().ovk;
  }

  //Validate the outgoing viewing key passed is not null
  if (ovk.IsNull()) {
      throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Spending key failed to produce a non-null outgoing viewing key.");
  }

  //Add the stored outputs to the sapling builder
  tb.ConvertRawSaplingOutput(ovk);

  //Initialize the orchard builder if there are orchard outputs and the orchard builder has not already been initialized
  //This can happen when there are no orchard spends but there are orchard outputs
  if(orchardInitialized == false && tb.vOrchardOutputs.size() > 0) {
      tb.InitializeOrchard(false, true, uint256());
  }

  //Add the stored outputs to the orchard builder
  tb.ConvertRawOrchardOutput(ovk);

  //Build and sign transaction
  auto rtx = tb.Build().GetTxOrThrow();

  //Return Hex encoded serialized completed transaction
  CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
  ssTx << rtx;
  return HexStr(ssTx.begin(), ssTx.end());

}

/**
 * @brief RPC command to create transaction builder instructions for offline shielded transactions
 * 
 * Generates hex-encoded transaction builder data for constructing shielded transactions
 * offline. Supports transparent and shielded inputs with automatic change handling
 * and coinbase UTXO spending restrictions for enhanced privacy workflows.
 * 
 * @param params RPC parameters: [fromaddress, amounts, minconf, fee]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Hex-encoded transaction builder instructions
 */
UniValue z_createbuildinstructions(const UniValue& params, bool fHelp, const CPubKey& mypk) {
  if (fHelp || params.size() < 2 || params.size() > 4)
      throw runtime_error(
          "z_createbuildinstructions \"fromaddress\" [{\"address\":... ,\"amount\":...},...] ( minconf ) ( fee )\n"
          "\nCreate transaction builder instructions for offline shielded transaction construction.\n"
          "Generates hex-encoded builder data that can be used to create shielded transactions\n"
          "from transparent or shielded addresses to shielded (Sapling/Orchard) addresses.\n"
          "Change generated from a transparent address flows to a new transparent address,\n"
          "while change generated from a shielded address returns to itself.\n"
          "\nWhen sending coinbase UTXOs to a shielded address, change is not allowed.\n"
          "The entire value of the UTXO(s) must be consumed.\n"

          + HelpRequiringPassphrase() + "\n"
          "\nArguments:\n"
          "1. \"fromaddress\"         (string, required) The transparent or shielded address to send funds from\n"
          "2. \"amounts\"             (array, required) Array of output destinations and amounts\n"
          "    [{\n"
          "      \"address\":address  (string, required) The Sapling or Orchard shielded address\n"
          "      \"amount\":amount    (numeric, required) The amount in " + CURRENCY_UNIT + "\n"
          "      \"memo\":memo        (string, optional) Hex-encoded memo data for shielded addresses\n"
          "    }, ... ]\n"
          "3. minconf               (numeric, optional, default=1) Only use funds confirmed at least this many times\n"
          "4. fee                   (numeric, optional, default=10000) The fee in zatoshis\n"
          "\nResult:\n"
          "\"transaction\"            (string) Hex string of the transaction builder instructions\n"
          "\nExamples:\n"
          + HelpExampleCli("z_createbuildinstructions", "\"RD6GgnrMpPaTSMn8vai6yiGA7mN4QGPV\" '[{\"address\": \"zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37\" ,\"amount\": 5.0}]'")
          + HelpExampleRpc("z_createbuildinstructions", "\"RD6GgnrMpPaTSMn8vai6yiGA7mN4QGPV\", [{\"address\": \"zs14d8tc0hl9q0vg5l28uec5vk6sk34fkj2n8s7jalvw5fxpy6v39yn4s2ga082lymrkjk0x2nqg37\" ,\"amount\": 5.0}]")
      );

      LOCK2(cs_main, pwalletMain->cs_wallet);

      EnsureWalletIsUnlocked();

      //THROW_IF_SYNCING(KOMODO_INSYNC);

      //Initalize Transaction Builder
      CAmount totalOut = 0;
      int nHeight = chainActive.Tip()->nHeight;
      TransactionBuilder tb = TransactionBuilder(Params().GetConsensus(), nHeight + DEFAULT_TX_EXPIRY_DELTA, pwalletMain);

      CAmount nFee = 10000;
      if (!params[3].isNull()) {
        double fee = params[3].get_real();
        nFee = AmountFromValue(fee);
        if (nFee < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, fee must be positive");
      }
      tb.SetFee(nFee);
      totalOut += nFee;



      // Outputs
      UniValue outputs = params[1].get_array();

      if (outputs.size()==0)
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amounts array is empty.");

      for (const UniValue& o : outputs.getValues()) {
          if (!o.isObject())
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

          // sanity check, report error if unknown key-value pairs
          for (const string& name_ : o.getKeys()) {
              std::string s = name_;
              if (s != "address" && s != "amount" && s!="memo")
                  throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ")+s);
          }


          bool toSapling = false;
          bool toOrchard = false;
          string address = find_value(o, "address").get_str();
          CTxDestination taddr = DecodeDestination(address);

          if (IsValidDestination(taddr)) {
              throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a sapling or orchard.");
          }

          auto res = DecodePaymentAddress(address);
          if (IsValidPaymentAddress(res)) {

              // Remember whether this is Sapling address
              toSapling = std::get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;

              // Remember whether this is Orchard address
              toOrchard = std::get_if<libzcash::OrchardPaymentAddressPirate>(&res) != nullptr;

              // Remember whether this is Sprout address
              bool toSprout = std::get_if<libzcash::SproutPaymentAddress>(&res) != nullptr;

              if (toSprout)
                  throw JSONRPCError(RPC_INVALID_PARAMETER,"Sprout usage has expired");

              if (!(toSapling || toOrchard))
                  throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ")+address );

          } else {
              throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ")+address );
          }

          //get memo
          std::optional<libzcash::Memo> hexMemo = std::nullopt;
          UniValue memoValue = find_value(o, "memo");
          string memo;
          if (!memoValue.isNull()) {
              memo = memoValue.get_str();

              if (!IsHex(memo)) {
                  memo = HexStr(memo);
              }
              if (memo.length() > ZC_MEMO_SIZE*2) {
                  throw JSONRPCError(RPC_INVALID_PARAMETER,  strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE ));
              }

              std::vector<unsigned char> rawMemo = ParseHex(memo.c_str());

              // If ParseHex comes across a non-hex char, it will stop but still return results so far.
              size_t slen = memo.length();
              if (slen % 2 !=0 || (slen>0 && rawMemo.size()!=slen/2)) {
                  throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");
              }

              if (rawMemo.size() > ZC_MEMO_SIZE) {
                  throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE));
              }

              // Convert to Memo class
              auto memoResult = libzcash::Memo::FromBytes(rawMemo);
              if (memoResult.has_value()) {
                  hexMemo = memoResult.value();
              }
          }

          UniValue av = find_value(o, "amount");
          CAmount nAmount = AmountFromValue( av );
          if (nAmount < 0)
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

          if (toSapling) {
              tb.AddSaplingOutputRaw(*std::get_if<libzcash::SaplingPaymentAddress>(&res), nAmount, hexMemo);
          } else if (toOrchard) {
              tb.AddOrchardOutputRaw(*std::get_if<libzcash::OrchardPaymentAddressPirate>(&res), nAmount, hexMemo);
          } else {
              throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ")+address );
          }

          totalOut += nAmount;
      }

      // Inputs

      // Check that the from address is valid.
      auto fromaddress = params[0].get_str();

      CTxDestination taddr = DecodeDestination(fromaddress);
      bool fromTaddr = IsValidDestination(taddr);
      if (fromTaddr) {
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a sapling or orchard.");
      }

      auto res = DecodePaymentAddress(fromaddress);
      if (!IsValidPaymentAddress(res)) {
          // invalid
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr, sapling or orchard.");
      }

      // Remember whether this is Sapling address
      bool fromSapling = std::get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;

      // Remember whether this is Orchard address
      bool fromOrchard = std::get_if<libzcash::OrchardPaymentAddressPirate>(&res) != nullptr;

      // Remember whether this is Sprout address
      bool fromSprout = std::get_if<libzcash::SproutPaymentAddress>(&res) != nullptr;
      if (fromSprout) {
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a sapling or orchard.");
      }

      if (!(fromSapling || fromOrchard)) {
          throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a sapling or orchard.");
      }

      // Minimum confirmations
      int nMinDepth = 1;
      if (params.size() > 2) {
          nMinDepth = params[2].get_int();
      }
      if (nMinDepth < 0) {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
      }

      //Get Notes
      std::vector<SaplingNoteEntry> saplingEntries;
      std::vector<OrchardNoteEntry> orchardEntries;
      CAmount totalIn = 0;

      {
          LOCK2(cs_main, pwalletMain->cs_wallet);
          // Offline transaction, Does not require the spending key in this wallet
          pwalletMain->GetFilteredNotes(saplingEntries, orchardEntries, fromaddress, nMinDepth, true, false);


          // Select Sapling notes
          std::vector<SaplingOutPoint> ops;
          std::vector<libzcash::SaplingNote> notes;

          for (auto entry : saplingEntries) {

              libzcash::MerklePath saplingMerklePath;
              if (!pwalletMain->SaplingWalletGetMerklePathOfNote(entry.op.hash, entry.op.n, saplingMerklePath)) {
                  throw JSONRPCError(RPC_INVALID_PARAMETER, "Getting Sapling Merkle Path failed");
              }

              uint256 anchor;
              if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingMerklePath, entry.note.cmu().value(), anchor)) {
                  throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Getting Anchor failed.");
              }

              if (!tb.AddSaplingSpendRaw(entry.op, entry.address, entry.note.value(), entry.note.rcm(), saplingMerklePath, anchor)) {
                  throw JSONRPCError(RPC_INVALID_PARAMETER, "All inputs must be from the same address");
              }

              totalIn += entry.note.value();
              if (totalIn >= totalOut) {
                  break;
              }
          }

          for (auto entry : orchardEntries) {

              libzcash::MerklePath orchardMerklePath;
              if (!pwalletMain->OrchardWalletGetMerklePathOfNote(entry.op.hash, entry.op.n, orchardMerklePath)) {
                  throw JSONRPCError(RPC_WALLET_ERROR, "Merkle Path not found for Orchard note. Stopping.");
              }

              uint256 anchor;
              if (!pwalletMain->OrchardWalletGetPathRootWithCMU(orchardMerklePath, entry.note.cmx(), anchor)) {
                  throw JSONRPCError(RPC_WALLET_ERROR,"Getting Orchard Anchor failed. Stopping.");
              }

              if (!tb.AddOrchardSpendRaw(entry.op, entry.address, entry.note.value(), entry.note.rho(), entry.note.rseed(), orchardMerklePath, anchor)) {
                  throw JSONRPCError(RPC_WALLET_ERROR,"Adding Raw Orchard Spend failed. Stopping.");
              }

              totalIn += entry.note.value();
              if (totalIn >= totalOut) {
                  break;
              }
          }
      }

      tb.SetChecksum();

      CDataStream ssTb(SER_NETWORK, PROTOCOL_VERSION);
      ssTb << tb;
      return HexStr(ssTb.begin(), ssTb.end());

}

/**
 * @brief RPC command to create transaction builder instructions with manual coin control
 * 
 * Generates transaction builder data with precise control over input selection and
 * output destinations for advanced shielded transaction construction. Enables
 * deterministic note spending and custom fee management for privacy-focused workflows.
 * 
 * @param params RPC parameters: [inputs, outputs, fee, expiryBlocks]
 * @param fHelp Whether to display help information
 * @param mypk Public key for authentication (unused)
 * @return UniValue Hex-encoded transaction builder instructions with coin control
 */
UniValue z_createbuildinstructionscoincontrol(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
  if (fHelp)
      throw runtime_error(
          "z_createbuildinstructionscoincontrol inputs outputs ( fee ) ( expiryBlocks )\n"
          "\nCreate transaction builder instructions with manual coin control for shielded transactions.\n"
          "Allows precise control over which shielded notes to spend and where to send outputs.\n"
          "Provides fine-grained control over transaction construction for advanced users.\n"

          "\nArguments:\n"
          "1. \"inputs\"                (array, required) Array of shielded inputs to spend\n"
          "     [\n"
          "       {\n"
          "         \"txid\":\"id\",          (string, required) The transaction id\n"
          "         \"index\":n,          (numeric, required) Shielded output index of input transaction\n"
          "       } \n"
          "       ,...\n"
          "     ]\n"
          "2. \"outputs\"               (array, required) Array of shielded outputs to create\n"
          "     [\n"
          "       {\n"
          "         \"address\":address     (string, required) Pirate shielded address\n"
          "         \"amount\":amount       (numeric, required) The amount in " + CURRENCY_UNIT + "\n"
          "         \"memo\": \"string\"    (string, optional) Memo in UTF-8 or hexadecimal format\n"
          "         ,...\n"
          "       }\n"
          "     ]\n"
          "3. fee                  (numeric, optional, default=0.0001) Transaction fee in " + CURRENCY_UNIT + "\n"
          "4. expiryBlocks         (numeric, optional, default=" + strprintf("%d", DEFAULT_TX_EXPIRY_DELTA) + ") Number of blocks before transaction expires\n"
          "\nResult:\n"
          "\"transaction\"            (string) Hex string of the transaction builder instructions\n"

          "\nExamples:\n"
          + HelpExampleCli("z_createbuildinstructionscoincontrol", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"index\\\":0},...]\" \"[{\\\"address\\\":\\\"sendtoaddress\\\",\\\"amount\\\":1.0000,\\\"memo\\\":\\\"memostring\\\"},...]\" 0.0001 200")
      );

    LOCK2(cs_main, pwalletMain->cs_wallet);
    RPCTypeCheck(params, boost::assign::list_of(UniValue::VARR)(UniValue::VARR)(UniValue::VNUM)(UniValue::VNUM), true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue outputs = params[1].get_array();

    CAmount total = 0;
    int nHeight = chainActive.Tip()->nHeight;
    TransactionBuilder tx = TransactionBuilder(Params().GetConsensus(), nHeight + DEFAULT_TX_EXPIRY_DELTA, pwalletMain);

    CAmount nFee = 10000;
    if (!params[2].isNull()) {
      double fee = params[2].get_real();
      nFee = AmountFromValue(fee);
      if (nFee < 0)
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, fee must be positive");
    }
    tx.SetFee(nFee);
    total -= nFee;

    if (!params[3].isNull()) {
      int expHeight = params[3].get_int();
      if (expHeight < 0)
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expiry height must be positive");

      tx.SetExpiryHeight(nHeight + expHeight);
    }

    for (size_t idx = 0; idx < inputs.size(); idx++) {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& nIndex = find_value(o, "index");
        if (!nIndex.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing index key");
        int nOutput = nIndex.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, index must be positive");

        const CWalletTx* wtx = pwalletMain->GetWalletTx(txid);
        if (wtx != NULL) {
            SaplingOutPoint op = SaplingOutPoint(txid, nOutput);

            int txHeight = chainActive.Tip()->nHeight - wtx->GetDepthInMainChain();
            auto maybe_decrypted = wtx->DecryptSaplingNote(Params().GetConsensus(), txHeight, op);
            if (maybe_decrypted == std::nullopt)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Note decryption failed.");

            auto decrypted = maybe_decrypted.value();
            libzcash::SaplingNotePlaintext pt = decrypted.first;
            libzcash::SaplingPaymentAddress pa = decrypted.second;
            auto note = pt.note(wtx->mapSaplingNoteData.at(op).ivk).value();
            CAmount value = note.value();
            total += value;

            libzcash::MerklePath saplingMerklePath;
            if (!pwalletMain->SaplingWalletGetMerklePathOfNote(op.hash, op.n, saplingMerklePath))
               throw JSONRPCError(RPC_INVALID_PARAMETER, "Getting Sapling Merkle Path failed");

           uint256 anchor;
           if (!pwalletMain->SaplingWalletGetPathRootWithCMU(saplingMerklePath, note.cmu().value(), anchor))
               throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Getting Anchor failed.");

           if (!tx.AddSaplingSpendRaw(op, pa, note.value(), note.rcm(), saplingMerklePath, anchor))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "All inputs must be from the same address");

        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Wallet transaction does not exist");
        }
    }

    for (size_t idx = 0; idx < outputs.size(); idx++) {
      const UniValue& output = outputs[idx];
      const UniValue& o = output.get_obj();

      // sanity check, report error if unknown key-value pairs
      for (const string& name_ : o.getKeys()) {
          std::string s = name_;
          if (s != "address" && s != "amount" && s != "memo")
              throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ")+s);
      }

      //Check address
      bool isTAddr = false;
      CTxDestination tAddress;
      bool isZsAddr = false;
      libzcash::SaplingPaymentAddress zsAddress;

      UniValue addrValue = find_value(o, "address");
      if (!addrValue.isNull()) {
          string encodedAddress = addrValue.get_str();
          tAddress = DecodeDestination(encodedAddress);
          if (IsValidDestination(tAddress)) {
              isTAddr = true;
          } else {
              libzcash::PaymentAddress zAddress = DecodePaymentAddress(encodedAddress);
              if (std::get_if<libzcash::SaplingPaymentAddress>(&zAddress) != nullptr) {
                  zsAddress = *(std::get_if<libzcash::SaplingPaymentAddress>(&zAddress));
                  isZsAddr = true;
              } else {
                  throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address.");
              }
          }
      } else {
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Address not found.");
      }

      //get amount
      UniValue av = find_value(o, "amount");
      CAmount nAmount = AmountFromValue( av );
      if (nAmount < 0)
          throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

      //get memo
      std::optional<libzcash::Memo> hexMemo = std::nullopt;
      UniValue memoValue = find_value(o, "memo");
      string memo;
      if (!memoValue.isNull()) {
          memo = memoValue.get_str();

          if (!isZsAddr) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo cannot be used with a taddr.  It can only be used with a zaddr.");
          }
          if (!IsHex(memo)) {
              memo = HexStr(memo);
          }
          if (memo.length() > ZC_MEMO_SIZE*2) {
              throw JSONRPCError(RPC_INVALID_PARAMETER,  strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE ));
          }

          std::vector<unsigned char> rawMemo = ParseHex(memo.c_str());

          // If ParseHex comes across a non-hex char, it will stop but still return results so far.
          size_t slen = memo.length();
          if (slen % 2 !=0 || (slen>0 && rawMemo.size()!=slen/2)) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo must be in hexadecimal format");
          }

          if (rawMemo.size() > ZC_MEMO_SIZE) {
              throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Memo size of %d is too big, maximum allowed is %d", rawMemo.size(), ZC_MEMO_SIZE));
          }

          // Convert to Memo class
          auto memoResult = libzcash::Memo::FromBytes(rawMemo);
          if (memoResult.has_value()) {
              hexMemo = memoResult.value();
          }
      }


      if (isZsAddr) {
          tx.AddSaplingOutputRaw(zsAddress, nAmount, hexMemo);
          total -= nAmount;
      } else {
          tx.AddTransparentOutput(tAddress,nAmount);
      }
    }

    if (total < 0)
      throw JSONRPCError(RPC_INVALID_PARAMETER, "Output values plus tx fee are greater than input values");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;
    return HexStr(ssTx.begin(), ssTx.end());


}


static const CRPCCommand commands[] =
{ //  category              name                                      actor (function)                          okSafeMode
  //  --------------------- ------------------------                  -----------------------                   ----------
    { "rawtransactions",    "getrawtransaction",                      &getrawtransaction,                       true  },
    { "rawtransactions",    "createrawtransaction",                   &createrawtransaction,                    true  },
    { "rawtransactions",    "decoderawtransaction",                   &decoderawtransaction,                    true  },
    { "rawtransactions",    "decodescript",                           &decodescript,                            true  },
    { "rawtransactions",    "sendrawtransaction",                     &sendrawtransaction,                      false },
    { "rawtransactions",    "signrawtransaction",                     &signrawtransaction,                      false }, /* uses wallet if enabled */

    { "rawtransactions",    "z_createbuildinstructions",              &z_createbuildinstructions,               true  }, /* uses wallet if enabled */
    { "rawtransactions",    "z_createbuildinstructionscoincontrol",   &z_createbuildinstructionscoincontrol,    true  }, /* uses wallet if enabled */
    { "rawtransactions",    "z_buildrawtransaction",                  &z_buildrawtransaction,                   false }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",                          &gettxoutproof,                           true  },
    { "blockchain",         "verifytxoutproof",                       &verifytxoutproof,                        true  },
};

/**
 * @brief Registers all raw transaction RPC commands with the RPC server
 * 
 * Initializes the RPC command table with all transaction-related commands including
 * creation, signing, decoding, and broadcasting operations. Configures command
 * categories and safety mode settings for each RPC endpoint.
 * 
 * @param tableRPC Reference to the RPC command table for registration
 */
void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
