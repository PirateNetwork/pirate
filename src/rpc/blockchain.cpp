// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2019-2022 The Zcash developers
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

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "crosschain.h"
#include "base58.h"
#include "consensus/validation.h"
#include "cc/eval.h"
#include "main.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "streams.h"
#include "sync.h"
#include "util.h"
#include "script/script.h"
#include "script/script_error.h"
#include "script/sign.h"
#include "script/standard.h"
#include "komodo_defs.h"
#include "komodo_structs.h"
#include "komodo_globals.h"
#include "komodo_notary.h"
#include "komodo_bitcoind.h"
#include "komodo_utils.h"
#include "komodo_kv.h"
#include "komodo_gateway.h"
#include "rpc/rawtransaction.h"

#include <stdint.h>

#include <univalue.h>

#include <regex>

#include "cc/CCinclude.h"

using namespace std;

// TODO: remove
//extern int32_t KOMODO_INSYNC;
//extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
//void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
//int32_t komodo_notarized_height(int32_t *prevMoMheightp,uint256 *hashp,uint256 *txidp);
//#include "komodo_defs.h"
#include "komodo_structs.h"
#include "komodo_interest.h"

/**
 * @brief Calculate blockchain difficulty as a multiple of minimum difficulty
 * @param blockindex Block index to calculate difficulty for (null for chain tip)
 * @param networkDifficulty True for network difficulty, false for block difficulty
 * @return Difficulty value as floating point multiple of minimum difficulty
 */
double GetDifficultyINTERNAL(const CBlockIndex* blockindex, bool networkDifficulty)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    uint32_t bits;
    if (networkDifficulty) {
        bits = GetNextWorkRequired(blockindex, nullptr, Params().GetConsensus());
    } else {
        bits = blockindex->nBits;
    }

    uint32_t powLimit =
        UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    int nShift = (bits >> 24) & 0xff;
    int nShiftAmount = (powLimit >> 24) & 0xff;

    double dDiff =
        (double)(powLimit & 0x00ffffff) /
        (double)(bits & 0x00ffffff);

    while (nShift < nShiftAmount)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > nShiftAmount)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

/**
 * @brief Get proof-of-work difficulty for a specific block
 * @param blockindex Block index to get difficulty for
 * @return Block difficulty as multiple of minimum difficulty
 */
double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, false);
}

/**
 * @brief Get current network difficulty for mining
 * @param blockindex Block index to base calculation on
 * @return Network difficulty as multiple of minimum difficulty
 */
double GetNetworkDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, true);
}

/**
 * @brief Create JSON description of a value pool for blockchain state
 * @param name Optional name of the value pool (transparent, sprout, sapling, orchard)
 * @param chainValue Total amount in the pool (optional)
 * @param valueDelta Change in pool value (optional)
 * @return JSON object describing the value pool
 */
static UniValue ValuePoolDesc(
    const std::optional<std::string> &name,
    const std::optional<CAmount> chainValue,
    const std::optional<CAmount> valueDelta)
{
    UniValue rv(UniValue::VOBJ);
    if (name != std::nullopt) {
        rv.pushKV("id", name.value());
    }
    rv.push_back(Pair("monitored", (bool)chainValue));
    if (chainValue) {
        rv.push_back(Pair("chainValue", ValueFromAmount(*chainValue)));
        rv.push_back(Pair("chainValueZat", *chainValue));
    }
    if (valueDelta) {
        rv.push_back(Pair("valueDelta", ValueFromAmount(*valueDelta)));
        rv.push_back(Pair("valueDeltaZat", *valueDelta));
    }
    return rv;
}

/**
 * @brief Convert block header to JSON representation
 * @param blockindex Block index to convert to JSON
 * @return JSON object containing block header information
 */
UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    if ( blockindex == 0 )
    {
        result.push_back(Pair("error", "null blockhash"));
        return(result);
    }
    uint256 notarized_hash,notarized_desttxid; int32_t prevMoMheight,notarized_height;
    notarized_height = komodo_notarized_height(&prevMoMheight,&notarized_hash,&notarized_desttxid);
    result.push_back(Pair("last_notarized_height", notarized_height));
    result.push_back(Pair("hash", blockindex->GetBlockHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.push_back(Pair("confirmations", komodo_dpowconfs(blockindex->nHeight,confirmations)));
    result.push_back(Pair("rawconfirmations", confirmations));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", blockindex->nVersion));
    result.push_back(Pair("merkleroot", blockindex->hashMerkleRoot.GetHex()));
    result.push_back(Pair("finalsaplingroot", blockindex->hashFinalSaplingRoot.GetHex()));
    result.push_back(Pair("time", (int64_t)blockindex->nTime));
    result.push_back(Pair("nonce", blockindex->nNonce.GetHex()));
    result.pushKV("solution", HexStr(blockindex->GetBlockHeader().nSolution));
    result.push_back(Pair("bits", strprintf("%08x", blockindex->nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));
    result.push_back(Pair("segid", (int)komodo_segid(0,blockindex->nHeight)));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

/**
 * @brief Convert block and transactions to JSON with address deltas
 * @param block Block data containing transactions
 * @param blockindex Block index for metadata
 * @return JSON object with block info and transaction address deltas
 */
UniValue blockToDeltasJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("hash", block.GetHash().GetHex()));
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex)) {
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block is an orphan");
    }
    result.push_back(Pair("confirmations", komodo_dpowconfs(blockindex->nHeight,confirmations)));
    result.push_back(Pair("rawconfirmations", confirmations));
    result.push_back(Pair("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION)));
    result.push_back(Pair("height", blockindex->nHeight));
    result.push_back(Pair("version", block.nVersion));
    result.push_back(Pair("merkleroot", block.hashMerkleRoot.GetHex()));
    result.push_back(Pair("segid", (int)komodo_segid(0,blockindex->nHeight)));

    UniValue deltas(UniValue::VARR);

    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction &tx = block.vtx[i];
        const uint256 txhash = tx.GetHash();

        UniValue entry(UniValue::VOBJ);
        entry.push_back(Pair("txid", txhash.GetHex()));
        entry.push_back(Pair("index", (int)i));

        UniValue inputs(UniValue::VARR);

        if (!tx.IsCoinBase()) {

            for (size_t j = 0; j < tx.vin.size(); j++) {
                const CTxIn input = tx.vin[j];

                UniValue delta(UniValue::VOBJ);

                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(input.prevout.hash, input.prevout.n);

                if (GetSpentIndex(spentKey, spentInfo)) {
                    if (spentInfo.addressType == 1) {
                        delta.push_back(Pair("address", CBitcoinAddress(CKeyID(spentInfo.addressHash)).ToString()));
                    }
                    else if (spentInfo.addressType == 2)  {
                        delta.push_back(Pair("address", CBitcoinAddress(CScriptID(spentInfo.addressHash)).ToString()));
                    }
                    else {
                        continue;
                    }
                    delta.push_back(Pair("satoshis", -1 * spentInfo.satoshis));
                    delta.push_back(Pair("index", (int)j));
                    delta.push_back(Pair("prevtxid", input.prevout.hash.GetHex()));
                    delta.push_back(Pair("prevout", (int)input.prevout.n));

                    inputs.push_back(delta);
                } else {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Spent information not available");
                }

            }
        }

        entry.push_back(Pair("inputs", inputs));

        UniValue outputs(UniValue::VARR);

        for (unsigned int k = 0; k < tx.vout.size(); k++) {
            const CTxOut &out = tx.vout[k];

            UniValue delta(UniValue::VOBJ);

            if (out.scriptPubKey.IsPayToScriptHash()) {
                vector<unsigned char> hashBytes(out.scriptPubKey.begin()+2, out.scriptPubKey.begin()+22);
                delta.push_back(Pair("address", CBitcoinAddress(CScriptID(uint160(hashBytes))).ToString()));

            }
            else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                vector<unsigned char> hashBytes(out.scriptPubKey.begin()+3, out.scriptPubKey.begin()+23);
                delta.push_back(Pair("address", CBitcoinAddress(CKeyID(uint160(hashBytes))).ToString()));
            }
            else if (out.scriptPubKey.IsPayToPublicKey() || out.scriptPubKey.IsPayToCryptoCondition()) {
                CTxDestination address;
                if (ExtractDestination(out.scriptPubKey, address))
                {
                    //vector<unsigned char> hashBytes(out.scriptPubKey.begin()+1, out.scriptPubKey.begin()+34);
                    //xxx delta.push_back(Pair("address", CBitcoinAddress(CKeyID(uint160(hashBytes))).ToString()));
                    delta.push_back(Pair("address", CBitcoinAddress(address).ToString()));
                }
            }
            else {
                continue;
            }

            delta.push_back(Pair("satoshis", out.nValue));
            delta.push_back(Pair("index", (int)k));

            outputs.push_back(delta);
        }

        entry.push_back(Pair("outputs", outputs));
        deltas.push_back(entry);

    }
    result.push_back(Pair("deltas", deltas));
    result.push_back(Pair("time", block.GetBlockTime()));
    result.push_back(Pair("mediantime", (int64_t)blockindex->GetMedianTimePast()));
    result.push_back(Pair("nonce", block.nNonce.GetHex()));
    result.push_back(Pair("bits", strprintf("%08x", block.nBits)));
    result.push_back(Pair("difficulty", GetDifficulty(blockindex)));
    result.push_back(Pair("chainwork", blockindex->nChainWork.GetHex()));

    if (blockindex->pprev)
        result.push_back(Pair("previousblockhash", blockindex->pprev->GetBlockHash().GetHex()));
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.push_back(Pair("nextblockhash", pnext->GetBlockHash().GetHex()));
    return result;
}

/**
 * @brief Convert block to comprehensive JSON representation
 * @param block Block data to convert
 * @param blockindex Block index for metadata
 * @param txDetails Include full transaction details if true
 * @return JSON object with complete block information including value pools and trees
 */
UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    AssertLockHeld(cs_main);
    bool orchardActive = NetworkUpgradeActive(blockindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD);

    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex());
    int confirmations = -1;
    uint256 notarized_hash,notarized_desttxid; int32_t prevMoMheight,notarized_height;
    notarized_height = komodo_notarized_height(&prevMoMheight,&notarized_hash,&notarized_desttxid);
    result.push_back(Pair("last_notarized_height", notarized_height));
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    result.pushKV("blockcommitments", blockindex->hashBlockCommitments.GetHex());
    result.pushKV("authdataroot", blockindex->hashAuthDataRoot.GetHex());
    result.pushKV("finalsaplingroot", blockindex->hashFinalSaplingRoot.GetHex());
    if (orchardActive) {
        auto finalOrchardRootBytes = blockindex->hashFinalOrchardRoot;
        result.pushKV("finalorchardroot", HexStr(finalOrchardRootBytes.begin(), finalOrchardRootBytes.end()));
    }
    result.pushKV("chainhistoryroot", blockindex->hashChainHistoryRoot.GetHex());
    UniValue txs(UniValue::VARR);
    for (const CTransaction&tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(tx, uint256(), objTx);
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx.GetHash().GetHex());
    }
    result.pushKV("tx", txs);
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("nonce", block.nNonce.GetHex());
    result.pushKV("solution", HexStr(block.nSolution));
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("anchor", blockindex->hashFinalSproutRoot.GetHex());
    result.pushKV("chainSupply", ValuePoolDesc(std::nullopt, blockindex->nChainTotalSupply, blockindex->nChainSupplyDelta));
    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("transparent", blockindex->nChainTransparentValue, blockindex->nTransparentValue));
    valuePools.push_back(ValuePoolDesc("sprout", blockindex->nChainSproutValue, blockindex->nSproutValue));
    valuePools.push_back(ValuePoolDesc("sapling", blockindex->nChainSaplingValue, blockindex->nSaplingValue));
    valuePools.push_back(ValuePoolDesc("orchard", blockindex->nChainOrchardValue, blockindex->nOrchardValue));
    result.pushKV("valuePools", valuePools);

    {
        UniValue trees(UniValue::VOBJ);

        SaplingMerkleFrontier saplingTree;
        if (pcoinsTip != nullptr && pcoinsTip->GetSaplingFrontierAnchorAt(blockindex->hashFinalSaplingRoot, saplingTree)) {
            UniValue sapling(UniValue::VOBJ);
            sapling.pushKV("size", (uint64_t)saplingTree.size());
            trees.pushKV("sapling", sapling);
        }

        OrchardMerkleFrontier orchardTree;
        if (pcoinsTip != nullptr && pcoinsTip->GetOrchardFrontierAnchorAt(blockindex->hashFinalOrchardRoot, orchardTree)) {
            UniValue orchard(UniValue::VOBJ);
            orchard.pushKV("size", (uint64_t)orchardTree.size());
            trees.pushKV("orchard", orchard);
        }

        result.pushKV("trees", trees);
    }

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

/**
 * @brief Get the current number of blocks in the best blockchain
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Current block height as numeric value
 */
UniValue getblockcount(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockcount\n"
            "\nReturns the current number of blocks in the longest valid blockchain.\n"
            "\nArguments:\n"
            "None\n"
            "\nResult:\n"
            "n                    (numeric) The current block height (number of blocks in chain)\n"
            "\nExamples:\n"
            "\nGet the current block count:\n"
            + HelpExampleCli("getblockcount", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblockcount", "")
        );

    LOCK(cs_main);
    return chainActive.Height();
}

/**
 * @brief Get the hash of the best (tip) block in the longest blockchain
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Hex string of the best block hash
 */
UniValue getbestblockhash(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getbestblockhash\n"
            "\nReturns the hash of the best (tip) block in the longest blockchain.\n"
            "\nArguments:\n"
            "None\n"
            "\nResult:\n"
            "\"hex\"              (string) The block hash, hex-encoded\n"
            "\nExamples:\n"
            "\nGet the best block hash:\n"
            + HelpExampleCli("getbestblockhash", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getbestblockhash", "")
        );

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

/**
 * @brief Get the current proof-of-work difficulty
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Current network difficulty as multiple of minimum difficulty
 */
UniValue getdifficulty(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getdifficulty\n"
            "\nReturns the current proof-of-work difficulty as a multiple of the minimum difficulty.\n"
            "\nArguments:\n"
            "None\n"
            "\nResult:\n"
            "n.nnn               (numeric) The current mining difficulty as a multiple of minimum difficulty\n"
            "\nExamples:\n"
            "\nGet the current difficulty:\n"
            + HelpExampleCli("getdifficulty", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getdifficulty", "")
        );

    LOCK(cs_main);
    return GetNetworkDifficulty();
}

bool NSPV_spentinmempool(uint256 &spenttxid,int32_t &spentvini,uint256 txid,int32_t vout);
bool NSPV_inmempool(uint256 txid);

/**
 * @brief Check if a UTXO is spent in the memory pool
 * @param spenttxid Output parameter for transaction ID that spends the UTXO
 * @param spentvini Output parameter for input index in spending transaction
 * @param txid Transaction ID of the UTXO to check
 * @param vout Output index of the UTXO to check
 * @return True if UTXO is spent in mempool, false otherwise
 */
bool myIsutxo_spentinmempool(uint256 &spenttxid,int32_t &spentvini,uint256 txid,int32_t vout)
{
    int32_t vini = 0;
    if ( KOMODO_NSPV_SUPERLITE )
        return(NSPV_spentinmempool(spenttxid,spentvini,txid,vout));
    BOOST_FOREACH(const CTxMemPoolEntry &e,mempool.mapTx)
    {
        const CTransaction &tx = e.GetTx();
        const uint256 &hash = tx.GetHash();
        BOOST_FOREACH(const CTxIn &txin,tx.vin)
        {
            //fprintf(stderr,"%s/v%d ",uint256_str(str,txin.prevout.hash),txin.prevout.n);
            if ( txin.prevout.n == vout && txin.prevout.hash == txid )
            {
                spenttxid = hash;
                spentvini = vini;
                return(true);
            }
            vini++;
        }
        //fprintf(stderr,"are vins for %s\n",uint256_str(str,hash));
    }
    return(false);
}

/**
 * @brief Check if a transaction ID exists in the memory pool
 * @param txid Transaction ID to search for
 * @return True if transaction is in mempool, false otherwise
 */
bool mytxid_inmempool(uint256 txid)
{
    if ( KOMODO_NSPV_SUPERLITE )
    {

    }
    BOOST_FOREACH(const CTxMemPoolEntry &e,mempool.mapTx)
    {
        const CTransaction &tx = e.GetTx();
        const uint256 &hash = tx.GetHash();
        if ( txid == hash )
            return(true);
    }
    return(false);
}

/**
 * @brief Convert memory pool contents to JSON format
 * @param fVerbose True for detailed transaction info, false for just transaction IDs
 * @return JSON array of transaction IDs or object with detailed transaction information
 */
UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        BOOST_FOREACH(const CTxMemPoolEntry& e, mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            info.push_back(Pair("size", (int)e.GetTxSize()));
            info.push_back(Pair("fee", ValueFromAmount(e.GetFee())));
            info.push_back(Pair("time", e.GetTime()));
            info.push_back(Pair("height", (int)e.GetHeight()));
            info.push_back(Pair("startingpriority", e.GetPriority(e.GetHeight())));
            info.push_back(Pair("currentpriority", e.GetPriority(chainActive.Height())));
            const CTransaction& tx = e.GetTx();
            set<string> setDepends;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                if (mempool.exists(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            BOOST_FOREACH(const string& dep, setDepends)
            {
                depends.push_back(dep);
            }

            info.push_back(Pair("depends", depends));
            o.push_back(Pair(hash.ToString(), info));
        }
        return o;
    }
    else
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);

        UniValue a(UniValue::VARR);
        BOOST_FOREACH(const uint256& hash, vtxid)
            a.push_back(hash.ToString());

        return a;
    }
}

/**
 * @brief Get all transaction IDs in the memory pool
 * @param params RPC parameters: [verbose]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON array of transaction IDs or object with detailed transaction info
 */
UniValue getrawmempool(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getrawmempool ( verbose )\n"
            "\nReturns all transaction IDs currently in the memory pool.\n"
            "\nArguments:\n"
            "1. verbose           (boolean, optional, default=false) True for detailed object, false for array of txids\n"
            "\nResult (verbose=false):\n"
            "[                    (json array of strings)\n"
            "  \"txid\",            (string) The transaction ID\n"
            "  ...\n"
            "]\n"
            "\nResult (verbose=true):\n"
            "{                    (json object)\n"
            "  \"txid\" : {         (json object)\n"
            "    \"size\" : n,      (numeric) Transaction size in bytes\n"
            "    \"fee\" : n,       (numeric) Transaction fee in ARRR\n"
            "    \"time\" : n,      (numeric) Local time transaction entered pool (seconds since epoch)\n"
            "    \"height\" : n,    (numeric) Block height when transaction entered pool\n"
            "    \"startingpriority\" : n, (numeric) Priority when transaction entered pool\n"
            "    \"currentpriority\" : n,  (numeric) Current transaction priority\n"
            "    \"depends\" : [    (array) Unconfirmed transactions used as inputs\n"
            "        \"txid\",      (string) Parent transaction ID\n"
            "       ... ]\n"
            "  }, ...\n"
            "}\n"
            "\nExamples:\n"
            "\nList transaction IDs in mempool:\n"
            + HelpExampleCli("getrawmempool", "")
            + "\nGet detailed mempool information:\n"
            + HelpExampleCli("getrawmempool", "true")
            + "\nAs JSON-RPC calls:\n"
            + HelpExampleRpc("getrawmempool", "false")
            + HelpExampleRpc("getrawmempool", "true")
        );

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

/**
 * @brief Get block information with address deltas for all transactions
 * @param params RPC parameters: [block_hash]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with block info and address deltas for inputs/outputs
 */
UniValue getblockdeltas(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockdeltas \"blockhash\"\n"
            "\nReturns detailed information about a block including all address balance changes.\n"
            "This is useful for tracking all inputs and outputs affected by transactions in the block.\n"
            "\nArguments:\n"
            "1. \"blockhash\"      (string, required) The block hash to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"hash\": \"hex\",               (string) Block hash\n"
            "  \"confirmations\": n,           (numeric) Number of confirmations\n"
            "  \"rawconfirmations\": n,        (numeric) Raw confirmations count\n"
            "  \"size\": n,                    (numeric) Block size in bytes\n"
            "  \"height\": n,                  (numeric) Block height\n"
            "  \"version\": n,                 (numeric) Block version\n"
            "  \"merkleroot\": \"hex\",         (string) Merkle root hash\n"
            "  \"segid\": n,                   (numeric) Segment ID for this block\n"
            "  \"deltas\": [                   (array) Address balance changes\n"
            "    {\n"
            "      \"txid\": \"hex\",            (string) Transaction hash\n"
            "      \"index\": n,               (numeric) Transaction index within block\n"
            "      \"inputs\": [               (array) Transaction inputs with address changes\n"
            "        {\n"
            "          \"address\": \"addr\",    (string) Address that spent funds\n"
            "          \"satoshis\": n,         (numeric) Amount spent in zatoshis (negative)\n"
            "          \"index\": n,            (numeric) Input index in transaction\n"
            "          \"prevtxid\": \"hex\",    (string) Previous transaction hash\n"
            "          \"prevout\": n           (numeric) Previous output index\n"
            "        },\n"
            "        ...\n"
            "      ],\n"
            "      \"outputs\": [              (array) Transaction outputs with address changes\n"
            "        {\n"
            "          \"address\": \"addr\",    (string) Address that received funds\n"
            "          \"satoshis\": n,         (numeric) Amount received in zatoshis (positive)\n"
            "          \"index\": n             (numeric) Output index in transaction\n"
            "        },\n"
            "        ...\n"
            "      ]\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"time\": n,                    (numeric) Block timestamp (Unix epoch)\n"
            "  \"mediantime\": n,              (numeric) Median block time\n"
            "  \"nonce\": \"hex\",              (string) Block nonce\n"
            "  \"bits\": \"hex\",               (string) Difficulty target bits\n"
            "  \"difficulty\": n.nn,           (numeric) Block difficulty\n"
            "  \"chainwork\": \"hex\",          (string) Total accumulated chain work\n"
            "  \"previousblockhash\": \"hex\",  (string) Previous block hash\n"
            "  \"nextblockhash\": \"hex\"       (string) Next block hash (if available)\n"
            "}\n"
            "\nExamples:\n"
            "\nGet block deltas for a specific block:\n"
            + HelpExampleCli("getblockdeltas", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblockdeltas", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex,1))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    return blockToDeltasJSON(block, pblockindex);
}

/**
 * @brief Get array of block hashes within a timestamp range
 * @param params RPC parameters: [high_timestamp, low_timestamp, options]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON array of block hashes or objects with hash and logical timestamp
 */
UniValue getblockhashes(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 2)
        throw runtime_error(
            "getblockhashes high_timestamp low_timestamp ( options )\n"
            "\nReturns an array of block hashes for blocks created within the specified timestamp range.\n"
            "Useful for finding blocks created during a specific time period.\n"
            "\nArguments:\n"
            "1. \"high_timestamp\"   (numeric, required) The newer block timestamp (Unix epoch)\n"
            "2. \"low_timestamp\"    (numeric, required) The older block timestamp (Unix epoch)\n"
            "3. \"options\"          (object, optional) Additional options:\n"
            "   {\n"
            "     \"noOrphans\": bool,        (boolean, default=false) Only include main chain blocks\n"
            "     \"logicalTimes\": bool      (boolean, default=false) Include logical timestamps in results\n"
            "   }\n"
            "\nResult (when logicalTimes=false):\n"
            "[\n"
            "  \"hash\",                      (string) Block hash\n"
            "  ...\n"
            "]\n"
            "\nResult (when logicalTimes=true):\n"
            "[\n"
            "  {\n"
            "    \"blockhash\": \"hex\",       (string) Block hash\n"
            "    \"logicalts\": n             (numeric) Logical timestamp\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            "\nGet block hashes between timestamps:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblockhashes", "1231614698, 1231024505")
            + "\nWith options to include logical times:\n"
            + HelpExampleCli("getblockhashes", "1231614698 1231024505 '{\"noOrphans\":false, \"logicalTimes\":true}'")
            );

    unsigned int high = params[0].get_int();
    unsigned int low = params[1].get_int();
    bool fActiveOnly = false;
    bool fLogicalTS = false;

    if (params.size() > 2) {
        if (params[2].isObject()) {
            UniValue noOrphans = find_value(params[2].get_obj(), "noOrphans");
            UniValue returnLogical = find_value(params[2].get_obj(), "logicalTimes");

            if (noOrphans.isBool())
                fActiveOnly = noOrphans.get_bool();

            if (returnLogical.isBool())
                fLogicalTS = returnLogical.get_bool();
        }
    }

    std::vector<std::pair<uint256, unsigned int> > blockHashes;

    if (fActiveOnly)
        LOCK(cs_main);

    if (!GetTimestampIndex(high, low, fActiveOnly, blockHashes)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for block hashes");
    }

    UniValue result(UniValue::VARR);

    for (std::vector<std::pair<uint256, unsigned int> >::const_iterator it=blockHashes.begin(); it!=blockHashes.end(); it++) {
        if (fLogicalTS) {
            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("blockhash", it->first.GetHex()));
            item.push_back(Pair("logicalts", (int)it->second));
            result.push_back(item);
        } else {
            result.push_back(it->first.GetHex());
        }
    }

    return result;
}

/**
 * @brief Sanity-check a height argument and interpret negative values
 * @param nHeight Block height to validate (negative values interpreted as relative to current height)
 * @param currentHeight Current blockchain height
 * @return Validated absolute block height
 */
int interpretHeightArg(int nHeight, int currentHeight)
{
    if (nHeight < 0) {
        nHeight += currentHeight + 1;
    }
    if (nHeight < 0 || nHeight > currentHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }
    return nHeight;
}

/**
 * @brief Parse and sanity-check a height argument, return its integer representation
 * @param strHeight String representation of block height
 * @param currentHeight Current blockchain height for validation
 * @return Validated absolute block height
 */
int parseHeightArg(const std::string& strHeight, int currentHeight)
{
    // std::stoi allows (locale-dependent) whitespace and optional '+' sign,
    // whereas we want to be strict.
    regex r("(?:(-?)[1-9][0-9]*|[0-9]+)");
    if (!regex_match(strHeight, r)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
    }
    int nHeight;
    try {
        nHeight = std::stoi(strHeight);
    }
    catch (const std::exception &e) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
    }
    return interpretHeightArg(nHeight, currentHeight);
}

/**
 * @brief Get the hash of a block at a specific height in the best chain
 * @param params RPC parameters: [block_height]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Hex string of the block hash at the specified height
 */
UniValue getblockhash(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getblockhash height\n"
            "\nReturns the block hash for the block at the specified height in the best blockchain.\n"
            "\nArguments:\n"
            "1. height            (numeric, required) The block height (0 is genesis block)\n"
            "\nResult:\n"
            "\"hex\"              (string) The block hash, hex-encoded\n"
            "\nExamples:\n"
            "\nGet the hash of block at height 1000:\n"
            + HelpExampleCli("getblockhash", "1000")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblockhash", "1000")
        );

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

/**
 * @brief Get staking statistics for each segment ID over a specified depth
 * @param params RPC parameters: [depth]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with staking counts per segment ID and PoS percentage
 */
UniValue getlastsegidstakes(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "getlastsegidstakes depth\n"
            "\nReturns staking statistics for each segment ID over the last N blocks.\n"
            "This command only works on staked asset chains (ac_staked).\n"
            "\nArguments:\n"
            "1. depth             (numeric, required) Number of recent blocks to analyze\n"
            "\nResult:\n"
            "{\n"
            "  \"NotSet\" : n,     (numeric) Number of blocks with unset segment ID\n"
            "  \"PoW\" : n,        (numeric) Number of proof-of-work blocks\n"
            "  \"PoSPerc\" : n,    (numeric) Percentage of proof-of-stake blocks\n"
            "  \"SegIds\" : {      (object) Staking counts by segment ID\n"
            "    \"0\" : n,        (numeric) Stakes from segment ID 0\n"
            "    \"1\" : n,        (numeric) Stakes from segment ID 1\n"
            "    ...\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            "\nAnalyze last 1000 blocks:\n"
            + HelpExampleCli("getlastsegidstakes", "1000")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getlastsegidstakes", "1000")
        );

    if ( ASSETCHAINS_STAKED == 0 )
        throw runtime_error("Only applies to ac_staked chains\n");

    LOCK(cs_main);

    int depth = params[0].get_int();
    if ( depth > chainActive.Height() )
        throw runtime_error("Not enough blocks to scan back that far.\n");

    int32_t segids[64] = {0};
    int32_t pow = 0;
    int32_t notset = 0;

    for (int64_t i = chainActive.Height(); i >  chainActive.Height()-depth; i--)
    {
        int8_t segid = komodo_segid(0,i);
        //CBlockIndex* pblockindex = chainActive[i];
        if ( segid >= 0 )
            segids[segid] += 1;
        else if ( segid == -1 )
            pow++;
        else
            notset++;
    }

    int8_t posperc = 100*(depth-pow)/depth;

    UniValue ret(UniValue::VOBJ);
    UniValue objsegids(UniValue::VOBJ);
    for (int8_t i = 0; i < 64; i++)
    {
        char str[4];
        sprintf(str, "%d", i);
        objsegids.push_back(Pair(str,segids[i]));
    }
    ret.push_back(Pair("NotSet",notset));
    ret.push_back(Pair("PoW",pow));
    ret.push_back(Pair("PoSPerc",posperc));
    ret.push_back(Pair("SegIds",objsegids));
    return ret;
}

/*uint256 _komodo_getblockhash(int32_t nHeight)
{
    uint256 hash;
    LOCK(cs_main);
    if ( nHeight >= 0 && nHeight <= chainActive.Height() )
    {
        CBlockIndex* pblockindex = chainActive[nHeight];
        hash = pblockindex->GetBlockHash();
        int32_t i;
        for (i=0; i<32; i++)
            printf("%02x",((uint8_t *)&hash)[i]);
        printf(" blockhash.%d\n",nHeight);
    } else memset(&hash,0,sizeof(hash));
    return(hash);
}*/

/**
 * @brief Get block header information for a specific block
 * @param params RPC parameters: [block_hash, verbose]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with block header info or hex string based on verbose flag
 */
UniValue getblockheader(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblockheader \"blockhash\" ( verbose )\n"
            "\nReturns information about the block header for the specified block hash.\n"
            "\nArguments:\n"
            "1. \"blockhash\"     (string, required) The block hash\n"
            "2. verbose          (boolean, optional, default=true) True for JSON object, false for hex data\n"
            "\nResult (verbose=true):\n"
            "{\n"
            "  \"hash\" : \"hex\",           (string) Block hash (same as provided)\n"
            "  \"confirmations\" : n,       (numeric) Number of DPoW confirmations (-1 if not on main chain)\n"
            "  \"rawconfirmations\" : n,    (numeric) Number of raw confirmations (-1 if not on main chain)\n"
            "  \"height\" : n,              (numeric) Block height\n"
            "  \"version\" : n,             (numeric) Block version\n"
            "  \"merkleroot\" : \"hex\",     (string) Merkle root hash\n"
            "  \"finalsaplingroot\" : \"hex\", (string) Final Sapling commitment tree root\n"
            "  \"time\" : n,                (numeric) Block time (seconds since epoch)\n"
            "  \"nonce\" : \"hex\",          (string) Block nonce\n"
            "  \"solution\" : \"hex\",       (string) Block solution\n"
            "  \"bits\" : \"hex\",           (string) Difficulty target bits\n"
            "  \"difficulty\" : n.nnn,      (numeric) Proof-of-work difficulty\n"
            "  \"chainwork\" : \"hex\",      (string) Total work in active chain\n"
            "  \"segid\" : n,               (numeric) Segment ID\n"
            "  \"previousblockhash\" : \"hex\", (string) Hash of previous block\n"
            "  \"nextblockhash\" : \"hex\"   (string) Hash of next block\n"
            "}\n"
            "\nResult (verbose=false):\n"
            "\"hex\"             (string) Serialized block header data, hex-encoded\n"
            "\nExamples:\n"
            "\nGet block header information:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + "\nGet raw block header data:\n"
            + HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\" false")
            + "\nAs JSON-RPC calls:\n"
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
            + HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\", false")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    try {
        if (!fVerbose) {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
        } else {
    return blockheaderToJSON(pblockindex);
        }
    } catch (const runtime_error&) {
        throw JSONRPCError(RPC_DATABASE_ERROR, "Failed to read index entry");
    }
}

/**
 * @brief Get detailed information about a specific block
 * @param params RPC parameters: [block_hash_or_height, verbosity]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Block data as hex string, JSON object, or JSON with full transaction details
 */
UniValue getblock(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "getblock \"hash|height\" ( verbosity )\n"
            "\nRetrieves detailed information about a specific block on the blockchain.\n"
            "Returns data in different formats based on the verbosity level.\n"
            "\nArguments:\n"
            "1. \"hash|height\"    (string, required) The block hash (hex string) or height (numeric)\n"
            "2. \"verbosity\"      (numeric, optional, default=1) Output format:\n"
            "                     0 = Raw hex-encoded block data\n"
            "                     1 = JSON object with block information\n"
            "                     2 = JSON object with block and transaction details\n"
            "\nResult (verbosity=0):\n"
            "\"hex_data\"          (string) Serialized, hex-encoded block data\n"
            "\nResult (verbosity=1):\n"
            "{\n"
            "  \"hash\": \"hex\",               (string) Block hash\n"
            "  \"last_notarized_height\": n,   (numeric) Height of last notarized block\n"
            "  \"confirmations\": n,           (numeric) Number of confirmations (-1 if not on main chain)\n"
            "  \"size\": n,                    (numeric) Block size in bytes\n"
            "  \"height\": n,                  (numeric) Block height/index\n"
            "  \"version\": n,                 (numeric) Block version\n"
            "  \"merkleroot\": \"hex\",         (string) Merkle root hash\n"
            "  \"blockcommitments\": \"hex\",   (string) Block commitments hash\n"
            "  \"authdataroot\": \"hex\",       (string) Auth data root hash\n"
            "  \"finalsaplingroot\": \"hex\",   (string) Final Sapling commitment tree root\n"
            "  \"finalorchardroot\": \"hex\",   (string, optional) Final Orchard commitment tree root\n"
            "  \"chainhistoryroot\": \"hex\",   (string) Chain history root hash\n"
            "  \"tx\": [                       (array) Transaction IDs\n"
            "    \"txid\",                     (string) Transaction ID\n"
            "    ...\n"
            "  ],\n"
            "  \"time\": n,                    (numeric) Block timestamp (Unix epoch)\n"
            "  \"nonce\": \"hex\",              (string) Block nonce\n"
            "  \"solution\": \"hex\",           (string) Block solution (Equihash proof)\n"
            "  \"bits\": \"hex\",               (string) Difficulty target bits\n"
            "  \"difficulty\": n.nn,           (numeric) Difficulty value\n"
            "  \"chainwork\": \"hex\",          (string) Total chain work\n"
            "  \"anchor\": \"hex\",             (string) Sprout commitment tree anchor\n"
            "  \"chainSupply\": {              (object) Chain supply information\n"
            "    \"monitored\": bool,          (boolean) Supply monitoring status\n"
            "    \"chainValue\": n.nn,         (numeric, optional) Total supply in ARRR\n"
            "    \"chainValueZat\": n,         (numeric, optional) Total supply in zatoshis\n"
            "    \"valueDelta\": n.nn,         (numeric, optional) Supply change in ARRR\n"
            "    \"valueDeltaZat\": n          (numeric, optional) Supply change in zatoshis\n"
            "  },\n"
            "  \"valuePools\": [               (array) Value pool information\n"
            "    {\n"
            "      \"id\": \"pool_name\",       (string) Pool name (transparent, sprout, sapling, orchard)\n"
            "      \"monitored\": bool,        (boolean) Pool monitoring status\n"
            "      \"chainValue\": n.nn,       (numeric, optional) Pool value in ARRR\n"
            "      \"chainValueZat\": n,       (numeric, optional) Pool value in zatoshis\n"
            "      \"valueDelta\": n.nn,       (numeric, optional) Pool change in ARRR\n"
            "      \"valueDeltaZat\": n        (numeric, optional) Pool change in zatoshis\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"trees\": {                    (object) Commitment tree information\n"
            "    \"sapling\": {                (object, optional) Sapling tree data\n"
            "      \"size\": n                 (numeric) Number of commitments\n"
            "    },\n"
            "    \"orchard\": {                (object, optional) Orchard tree data\n"
            "      \"size\": n                 (numeric) Number of commitments\n"
            "    }\n"
            "  },\n"
            "  \"previousblockhash\": \"hex\",   (string) Previous block hash\n"
            "  \"nextblockhash\": \"hex\"       (string) Next block hash\n"
            "}\n"
            "\nResult (verbosity=2):\n"
            "Same as verbosity=1, but \"tx\" array contains full transaction objects\n"
            "instead of just transaction IDs (as returned by getrawtransaction).\n"
            "\nExamples:\n"
            "\nGet block by hash (JSON format):\n"
            + HelpExampleCli("getblock", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblock", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + "\nGet block by height:\n"
            + HelpExampleCli("getblock", "12800")
            + "\nGet raw block data:\n"
            + HelpExampleCli("getblock", "12800 0")
            + "\nGet block with full transaction details:\n"
            + HelpExampleCli("getblock", "12800 2")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        // std::stoi allows characters, whereas we want to be strict
        regex r("[[:digit:]]+");
        if (!regex_match(strHash, r)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        int nHeight = -1;
        try {
            nHeight = std::stoi(strHash);
        }
        catch (const std::exception &e) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }
        strHash = chainActive[nHeight]->GetBlockHash().GetHex();
    }

    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() > 1) {
        if(params[1].isNum()) {
            verbosity = params[1].get_int();
        } else {
            verbosity = params[1].get_bool() ? 1 : 0;
        }
    }

    if (verbosity < 0 || verbosity > 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range from 0 to 2");
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex,1))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (verbosity == 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        std::string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

/**
 * @brief Get statistics about the unspent transaction output (UTXO) set
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with UTXO set statistics including count, size, and total amount
 */
UniValue gettxoutsetinfo(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "gettxoutsetinfo\n"
            "\nReturns comprehensive statistics about the unspent transaction output (UTXO) set.\n"
            "Note: This operation may take considerable time to complete.\n"
            "\nArguments:\n"
            "None\n"
            "\nResult:\n"
            "{\n"
            "  \"height\" : n,           (numeric) Current block height\n"
            "  \"bestblock\" : \"hex\",   (string) Hash of the best block\n"
            "  \"transactions\" : n,     (numeric) Number of transactions with unspent outputs\n"
            "  \"txouts\" : n,           (numeric) Number of unspent transaction outputs\n"
            "  \"bytes_serialized\" : n, (numeric) Serialized size of UTXO set in bytes\n"
            "  \"hash_serialized\" : \"hex\", (string) Serialized hash of UTXO set\n"
            "  \"total_amount\" : n.nnn  (numeric) Total amount of all UTXOs in ARRR\n"
            "}\n"
            "\nExamples:\n"
            "\nGet UTXO set statistics:\n"
            + HelpExampleCli("gettxoutsetinfo", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("gettxoutsetinfo", "")
        );

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.push_back(Pair("height", (int64_t)stats.nHeight));
        ret.push_back(Pair("bestblock", stats.hashBlock.GetHex()));
        ret.push_back(Pair("transactions", (int64_t)stats.nTransactions));
        ret.push_back(Pair("txouts", (int64_t)stats.nTransactionOutputs));
        ret.push_back(Pair("bytes_serialized", (int64_t)stats.nSerializedSize));
        ret.push_back(Pair("hash_serialized", stats.hashSerialized.GetHex()));
        ret.push_back(Pair("total_amount", ValueFromAmount(stats.nTotalAmount)));
    }
    return ret;
}

/**
 * @brief Search for a key-value pair stored on the blockchain (asset chain feature)
 * @param params RPC parameters: [key]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with key information including value, owner, height, and expiration
 */
UniValue kvsearch(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue ret(UniValue::VOBJ); uint32_t flags; uint8_t value[IGUANA_MAXSCRIPTSIZE*8],key[IGUANA_MAXSCRIPTSIZE*8]; int32_t duration,j,height,valuesize,keylen; uint256 refpubkey; static uint256 zeroes;
    if (fHelp || params.size() != 1 )
        throw runtime_error(
            "kvsearch \"key\"\n"
            "\nSearches for a key-value pair stored on the blockchain via kvupdate.\n"
            "This feature is only available on asset chains with KV storage enabled.\n"
            "\nArguments:\n"
            "1. \"key\"           (string, required) The key to search for on the blockchain\n"
            "\nResult:\n"
            "{\n"
            "  \"coin\" : \"name\",      (string) Name of the chain where key is stored\n"
            "  \"currentheight\" : n,   (numeric) Current blockchain height\n"
            "  \"key\" : \"key\",        (string) The searched key\n"
            "  \"keylen\" : n,          (numeric) Length of the key in characters\n"
            "  \"owner\" : \"hex\",      (string) Public key of the key owner\n"
            "  \"height\" : n,          (numeric) Block height where key was stored\n"
            "  \"expiration\" : n,      (numeric) Block height when key expires\n"
            "  \"flags\" : n,           (numeric) Key flags (1 if password protected, 0 otherwise)\n"
            "  \"value\" : \"data\",     (string) The stored value\n"
            "  \"valuesize\" : n        (numeric) Size of stored value in characters\n"
            "}\n"
            "\nExamples:\n"
            "\nSearch for a key:\n"
            + HelpExampleCli("kvsearch", "\"mykey\"")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("kvsearch", "\"mykey\"")
        );
    LOCK(cs_main);
    if ( (keylen= (int32_t)strlen(params[0].get_str().c_str())) > 0 )
    {
        ret.push_back(Pair("coin",chainName.ToString()));
        ret.push_back(Pair("currentheight", (int64_t)chainActive.Tip()->nHeight));
        ret.push_back(Pair("key",params[0].get_str()));
        ret.push_back(Pair("keylen",keylen));
        if ( keylen < sizeof(key) )
        {
            memcpy(key,params[0].get_str().c_str(),keylen);
            if ( (valuesize= komodo_kvsearch(&refpubkey,chainActive.Tip()->nHeight,&flags,&height,value,key,keylen)) >= 0 )
            {
                std::string val; char *valuestr;
                val.resize(valuesize);
                valuestr = (char *)val.data();
                memcpy(valuestr,value,valuesize);
                if ( memcmp(&zeroes,&refpubkey,sizeof(refpubkey)) != 0 )
                    ret.push_back(Pair("owner",refpubkey.GetHex()));
                ret.push_back(Pair("height",height));
                duration = ((flags >> 2) + 1) * KOMODO_KVDURATION;
                ret.push_back(Pair("expiration", (int64_t)(height+duration)));
                ret.push_back(Pair("flags",(int64_t)flags));
                ret.push_back(Pair("value",val));
                ret.push_back(Pair("valuesize",valuesize));
            } else ret.push_back(Pair("error",(char *)"cant find key"));
        } else ret.push_back(Pair("error",(char *)"key too big"));
    } else ret.push_back(Pair("error",(char *)"null key"));
    return ret;
}

/**
 * @brief Get information about which notaries have mined blocks at a specific height
 * @param params RPC parameters: [height]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with miner information including notary IDs, addresses, and block counts
 */
UniValue minerids(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    uint32_t timestamp = 0; UniValue ret(UniValue::VOBJ); UniValue a(UniValue::VARR); uint8_t minerids[2000],pubkeys[65][33]; int32_t i,j,n,numnotaries,tally[129];
    if ( fHelp || params.size() != 1 )
        throw runtime_error(
            "minerids height\n"
            "\nReturns statistics about which notaries have mined blocks up to a specified height.\n"
            "Useful for analyzing mining distribution and notary node activity on the network.\n"
            "\nArguments:\n"
            "1. \"height\"         (numeric, required) Block height to analyze (use 0 for current chain tip)\n"
            "\nResult:\n"
            "{\n"
            "  \"mined\": [\n"
            "    {\n"
            "      \"notaryid\": n,       (numeric) Unique notary node ID\n"
            "      \"KMDaddress\": \"addr\", (string) Komodo address of the notary node\n"
            "      \"pubkey\": \"hex\",    (string) Public key of the notary node\n"
            "      \"blocks\": n         (numeric) Number of blocks mined by this notary\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"numnotaries\": n        (numeric) Total number of active notary nodes\n"
            "}\n"
            "\nExamples:\n"
            "\nGet miner stats for current height:\n"
            + HelpExampleCli("minerids", "0")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("minerids", "0")
            + "\nGet miner stats for specific height:\n"
            + HelpExampleCli("minerids", "100000")
        );
    LOCK(cs_main);
    int32_t height = atoi(params[0].get_str().c_str());
    if ( height <= 0 )
        height = chainActive.Tip()->nHeight;
    else
    {
        CBlockIndex *pblockindex = chainActive[height];
        if ( pblockindex != 0 )
            timestamp = pblockindex->GetBlockTime();
    }
    if ( (n= komodo_minerids(minerids,height,(int32_t)(sizeof(minerids)/sizeof(*minerids)))) > 0 )
    {
        memset(tally,0,sizeof(tally));
        numnotaries = komodo_notaries(pubkeys,height,timestamp);
        if ( numnotaries > 0 )
        {
            for (i=0; i<n; i++)
            {
                if ( minerids[i] >= numnotaries )
                    tally[128]++;
                else tally[minerids[i]]++;
            }
            for (i=0; i<64; i++)
            {
                UniValue item(UniValue::VOBJ); std::string hex,kmdaddress; char *hexstr,kmdaddr[64],*ptr; int32_t m;
                hex.resize(66);
                hexstr = (char *)hex.data();
                for (j=0; j<33; j++)
                    sprintf(&hexstr[j*2],"%02x",pubkeys[i][j]);
                item.push_back(Pair("notaryid", i));

                bitcoin_address(kmdaddr,60,pubkeys[i],33);
                m = (int32_t)strlen(kmdaddr);
                kmdaddress.resize(m);
                ptr = (char *)kmdaddress.data();
                memcpy(ptr,kmdaddr,m);
                item.push_back(Pair("KMDaddress", kmdaddress));

                item.push_back(Pair("pubkey", hex));
                item.push_back(Pair("blocks", tally[i]));
                a.push_back(item);
            }
            UniValue item(UniValue::VOBJ);
            item.push_back(Pair("pubkey", (char *)"external miners"));
            item.push_back(Pair("blocks", tally[128]));
            a.push_back(item);
        }
        ret.push_back(Pair("mined", a));
        ret.push_back(Pair("numnotaries", numnotaries));
    } else ret.push_back(Pair("error", (char *)"couldnt extract minerids"));
    return ret;
}

/**
 * @brief Get information about notaries at a specific height and timestamp
 * @param params RPC parameters: [height, timestamp]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with notary information including public keys and addresses
 */
UniValue notaries(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    UniValue a(UniValue::VARR); uint32_t timestamp=0; UniValue ret(UniValue::VOBJ); int32_t i,j,n,m; char *hexstr;  uint8_t pubkeys[64][33]; char btcaddr[64],kmdaddr[64],*ptr;
    if ( fHelp || (params.size() != 1 && params.size() != 2) )
        throw runtime_error(
            "notaries height ( timestamp )\n"
            "\nReturns detailed information about active notary nodes at a specific height and timestamp.\n"
            "Notary nodes are responsible for cross-chain notarizations in the Komodo ecosystem.\n"
            "\nArguments:\n"
            "1. \"height\"      (numeric, required) Block height to query (-1 for current chain tip)\n"
            "2. \"timestamp\"   (numeric, optional) Unix timestamp for notary selection (uses current time if omitted)\n"
            "\nResult:\n"
            "{\n"
            "  \"notaries\": [\n"
            "    {\n"
            "      \"pubkey\": \"hex\",         (string) Public key of the notary node\n"
            "      \"BTCaddress\": \"addr\",    (string) Bitcoin address of the notary\n"
            "      \"KMDaddress\": \"addr\"     (string) Komodo address of the notary\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"numnotaries\": n,            (numeric) Total number of active notary nodes\n"
            "  \"height\": n,                 (numeric) Block height queried\n"
            "  \"timestamp\": n               (numeric) Unix timestamp used for query\n"
            "}\n"
            "\nExamples:\n"
            "\nGet notaries at specific height:\n"
            + HelpExampleCli("notaries", "100000")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("notaries", "100000")
            + "\nGet notaries at height with timestamp:\n"
            + HelpExampleCli("notaries", "100000 1640995200")
        );
    LOCK(cs_main);
    int32_t height = atoi(params[0].get_str().c_str());
    if ( params.size() == 2 )
        timestamp = (uint32_t)atol(params[1].get_str().c_str());
    else timestamp = (uint32_t)time(NULL);
    if ( height < 0 )
    {
        height = chainActive.Tip()->nHeight;
        timestamp = chainActive.Tip()->GetBlockTime();
    }
    else if ( params.size() < 2 )
    {
        CBlockIndex *pblockindex = chainActive[height];
        if ( pblockindex != 0 )
            timestamp = pblockindex->GetBlockTime();
    }
    if ( (n= komodo_notaries(pubkeys,height,timestamp)) > 0 )
    {
        for (i=0; i<n; i++)
        {
            UniValue item(UniValue::VOBJ);
            std::string btcaddress,kmdaddress,hex;
            hex.resize(66);
            hexstr = (char *)hex.data();
            for (j=0; j<33; j++)
                sprintf(&hexstr[j*2],"%02x",pubkeys[i][j]);
            item.push_back(Pair("pubkey", hex));

            bitcoin_address(btcaddr,0,pubkeys[i],33);
            m = (int32_t)strlen(btcaddr);
            btcaddress.resize(m);
            ptr = (char *)btcaddress.data();
            memcpy(ptr,btcaddr,m);
            item.push_back(Pair("BTCaddress", btcaddress));

            bitcoin_address(kmdaddr,60,pubkeys[i],33);
            m = (int32_t)strlen(kmdaddr);
            kmdaddress.resize(m);
            ptr = (char *)kmdaddress.data();
            memcpy(ptr,kmdaddr,m);
            item.push_back(Pair("KMDaddress", kmdaddress));
            a.push_back(item);
        }
    }
    ret.push_back(Pair("notaries", a));
    ret.push_back(Pair("numnotaries", n));
    ret.push_back(Pair("height", height));
    ret.push_back(Pair("timestamp", (uint64_t)timestamp));
    return ret;
}

/**
 * @brief Get details about an unspent transaction output (UTXO)
 * @param params RPC parameters: [txid, vout_index, include_mempool]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with UTXO details or null if spent/not found
 */
UniValue gettxout(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
            "gettxout \"txid\" n ( includemempool )\n"
            "\nReturns detailed information about an unspent transaction output (UTXO).\n"
            "Only returns information for outputs that are currently unspent.\n"
            "\nArguments:\n"
            "1. \"txid\"           (string, required) The transaction ID to query\n"
            "2. \"n\"              (numeric, required) The output index (vout number)\n"
            "3. \"includemempool\" (boolean, optional, default=true) Include mempool transactions\n"
            "\nResult:\n"
            "{\n"
            "  \"bestblock\": \"hex\",         (string) Hash of the block at chain tip\n"
            "  \"confirmations\": n,          (numeric) Number of confirmations\n"
            "  \"rawconfirmations\": n,       (numeric) Raw confirmations count\n"
            "  \"value\": n.nnnnnnnn,         (numeric) Output value in ARRR\n"
            "  \"interest\": n.nnnnnnnn,      (numeric, optional) Accrued interest in ARRR\n"
            "  \"scriptPubKey\": {            (object) Script details\n"
            "    \"asm\": \"script\",           (string) Script assembly representation\n"
            "    \"hex\": \"hex\",              (string) Script in hex format\n"
            "    \"reqSigs\": n,              (numeric) Required signatures\n"
            "    \"type\": \"script_type\",     (string) Script type (e.g., 'pubkeyhash', 'scripthash')\n"
            "    \"addresses\": [             (array) Associated addresses\n"
            "      \"address\",               (string) Pirate address\n"
            "      ...\n"
            "    ]\n"
            "  },\n"
            "  \"version\": n,                (numeric) Transaction version\n"
            "  \"coinbase\": bool             (boolean) True if this is a coinbase output\n"
            "}\n"
            "\nNote: Returns null if the output does not exist or has been spent.\n"
            "\nExamples:\n"
            "\nGet details of a transaction output:\n"
            + HelpExampleCli("gettxout", "\"a1b2c3d4e5f6...\" 0")
            + "\nCheck output excluding mempool:\n"
            + HelpExampleCli("gettxout", "\"a1b2c3d4e5f6...\" 1 false")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("gettxout", "\"a1b2c3d4e5f6...\", 0")
        );

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TODO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.push_back(Pair("bestblock", pindex->GetBlockHash().GetHex()));
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT) {
        ret.push_back(Pair("confirmations", 0));
        ret.push_back(Pair("rawconfirmations", 0));
    } else {
        ret.push_back(Pair("confirmations", komodo_dpowconfs(coins.nHeight,pindex->nHeight - coins.nHeight + 1)));
        ret.push_back(Pair("rawconfirmations", pindex->nHeight - coins.nHeight + 1));
    }
    ret.push_back(Pair("value", ValueFromAmount(coins.vout[n].nValue)));
    uint64_t interest; int32_t txheight; uint32_t locktime;
    if ( (interest= komodo_accrued_interest(&txheight,&locktime,hash,n,coins.nHeight,coins.vout[n].nValue,(int32_t)pindex->nHeight)) != 0 )
        ret.push_back(Pair("interest", ValueFromAmount(interest)));
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.push_back(Pair("scriptPubKey", o));
    ret.push_back(Pair("version", coins.nVersion));
    ret.push_back(Pair("coinbase", coins.fCoinBase));

    return ret;
}

/**
 * @brief Verify the blockchain database integrity
 * @param params RPC parameters: [check_level, num_blocks]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return True if verification successful, false otherwise
 */
UniValue verifychain(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
            "verifychain ( checklevel numblocks )\n"
            "\nPerforms comprehensive verification of the blockchain database integrity.\n"
            "This command checks for corruption, validates transactions, and ensures chain consistency.\n"
            "\nArguments:\n"
            "1. \"checklevel\"     (numeric, optional, default=3) Verification thoroughness level:\n"
            "                     0 = Basic existence check (blocks are in database)\n"
            "                     1 = Check block structure validity\n"
            "                     2 = Verify block structure + transaction format\n"
            "                     3 = Full verification with signature validation (recommended)\n"
            "                     4 = Complete verification including script execution\n"
            "2. \"numblocks\"      (numeric, optional, default=288) Number of recent blocks to verify:\n"
            "                     0 = Verify entire blockchain (may take hours)\n"
            "                     N = Verify last N blocks\n"
            "\nResult:\n"
            "true                 (boolean) Verification successful - blockchain is valid\n"
            "false                (boolean) Verification failed - corruption detected\n"
            "\nNote: Higher check levels and more blocks increase verification time but provide\n"
            "more thorough validation. Level 3 is recommended for regular verification.\n"
            "\nExamples:\n"
            "\nQuick verification of recent blocks:\n"
            + HelpExampleCli("verifychain", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("verifychain", "")
            + "\nThorough verification of last 1000 blocks:\n"
            + HelpExampleCli("verifychain", "4 1000")
            + "\nBasic check of last 100 blocks:\n"
            + HelpExampleCli("verifychain", "1 100")
        );

    LOCK(cs_main);

    int nCheckLevel = GetArg("-checklevel", 3);
    int nCheckDepth = GetArg("-checkblocks", 288);
    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(pcoinsTip, nCheckLevel, nCheckDepth);
}

/**
 * @brief Implementation of IsSuperMajority with better feedback
 * @param minVersion Minimum version number to count
 * @param pindex Block index to start checking from
 * @param nRequired Number of blocks required for majority
 * @param consensusParams Consensus parameters containing majority window
 * @return JSON object with soft fork majority status information
 */
static UniValue SoftForkMajorityDesc(int minVersion, CBlockIndex* pindex, int nRequired, const Consensus::Params& consensusParams)
{
    int nFound = 0;
    CBlockIndex* pstart = pindex;
    for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }

    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("status", nFound >= nRequired));
    rv.push_back(Pair("found", nFound));
    rv.push_back(Pair("required", nRequired));
    rv.push_back(Pair("window", consensusParams.nMajorityWindow));
    return rv;
}

/**
 * @brief Create soft fork description with deployment information
 * @param name Name identifier for the soft fork
 * @param version Version number associated with the soft fork
 * @param pindex Block index to check deployment status against
 * @param consensusParams Consensus parameters for deployment rules
 * @return JSON object describing soft fork status and deployment
 */
static UniValue SoftForkDesc(const std::string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.push_back(Pair("id", name));
    rv.push_back(Pair("version", version));
    rv.push_back(Pair("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams)));
    rv.push_back(Pair("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams)));
    return rv;
}

/**
 * @brief Create network upgrade description with activation and status information
 * @param consensusParams Consensus parameters containing upgrade definitions
 * @param idx Index of the network upgrade to describe
 * @param height Current blockchain height for status evaluation
 * @return JSON object describing network upgrade details and current status
 */
static UniValue NetworkUpgradeDesc(const Consensus::Params& consensusParams, Consensus::UpgradeIndex idx, int height)
{
    UniValue rv(UniValue::VOBJ);
    auto upgrade = NetworkUpgradeInfo[idx];
    rv.push_back(Pair("name", upgrade.strName));
    rv.push_back(Pair("activationheight", consensusParams.vUpgrades[idx].nActivationHeight));
    switch (NetworkUpgradeState(height, consensusParams, idx)) {
        case UPGRADE_DISABLED: rv.push_back(Pair("status", "disabled")); break;
        case UPGRADE_PENDING: rv.push_back(Pair("status", "pending")); break;
        case UPGRADE_ACTIVE: rv.push_back(Pair("status", "active")); break;
    }
    rv.push_back(Pair("info", upgrade.strInfo));
    return rv;
}

/**
 * @brief Add network upgrade description to JSON array if upgrade is active
 * @param networkUpgrades JSON array to add upgrade description to
 * @param consensusParams Consensus parameters containing upgrade definitions  
 * @param idx Index of the network upgrade to add
 * @param height Current blockchain height for status evaluation
 */
void NetworkUpgradeDescPushBack(
    UniValue& networkUpgrades,
    const Consensus::Params& consensusParams,
    Consensus::UpgradeIndex idx,
    int height)
{
    // Network upgrades with an activation height of NO_ACTIVATION_HEIGHT are
    // hidden. This is used when network upgrade implementations are merged
    // without specifying the activation height.
    if (consensusParams.vUpgrades[idx].nActivationHeight != Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) {
        networkUpgrades.push_back(Pair(
            HexInt(NetworkUpgradeInfo[idx].nBranchId),
            NetworkUpgradeDesc(consensusParams, idx, height)));
    }
}

/**
 * @brief Get comprehensive blockchain information and status
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with blockchain info including chain stats, consensus rules, and upgrades
 */
UniValue getblockchaininfo(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getblockchaininfo\n"
            "\nReturns comprehensive information about the current state of the blockchain.\n"
            "Provides details about chain progress, consensus rules, network upgrades, and value pools.\n"
            "\nArguments:\n"
            "None\n"
            "\nResult:\n"
            "{\n"
            "  \"chain\": \"network\",           (string) Network name (main, test, regtest)\n"
            "  \"blocks\": n,                   (numeric) Current number of blocks processed\n"
            "  \"synced\": bool,                (boolean) True if node is synchronized with network\n"
            "  \"headers\": n,                  (numeric) Current number of validated headers\n"
            "  \"bestblockhash\": \"hex\",       (string) Hash of the current best block\n"
            "  \"difficulty\": n.nn,            (numeric) Current mining difficulty\n"
            "  \"verificationprogress\": n.nn,  (numeric) Chain verification progress (0.0 to 1.0)\n"
            "  \"chainwork\": \"hex\",           (string) Total accumulated proof-of-work\n"
            "  \"pruned\": bool,                (boolean) True if blockchain is pruned\n"
            "  \"commitments\": n,              (numeric) Number of note commitments in tree\n"
            "  \"chainSupply\": {               (object) Total chain supply information\n"
            "    \"monitored\": bool,           (boolean) Supply monitoring status\n"
            "    \"chainValue\": n.nnnnnnnn,    (numeric, optional) Total supply in ARRR\n"
            "    \"chainValueZat\": n           (numeric, optional) Total supply in zatoshis\n"
            "  },\n"
            "  \"valuePools\": [                (array) Information about each value pool\n"
            "    {\n"
            "      \"id\": \"pool_name\",        (string) Pool name (transparent, sprout, sapling, orchard, burned)\n"
            "      \"monitored\": bool,         (boolean) Pool monitoring status\n"
            "      \"chainValue\": n.nnnnnnnn,  (numeric, optional) Pool value in ARRR\n"
            "      \"chainValueZat\": n         (numeric, optional) Pool value in zatoshis\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"softforks\": [                 (array) Status of soft forks in progress\n"
            "    {\n"
            "      \"id\": \"fork_name\",        (string) Soft fork identifier\n"
            "      \"version\": n,              (numeric) Block version for this fork\n"
            "      \"enforce\": {               (object) Enforcement progress\n"
            "        \"status\": bool,          (boolean) True if threshold reached\n"
            "        \"found\": n,              (numeric) Blocks with new version found\n"
            "        \"required\": n,           (numeric) Blocks required to trigger\n"
            "        \"window\": n              (numeric) Examination window size\n"
            "      },\n"
            "      \"reject\": { ... }          (object) Rejection progress (same fields as enforce)\n"
            "    },\n"
            "    ...\n"
            "  ],\n"
            "  \"upgrades\": {                  (object) Network upgrade status\n"
            "    \"branch_id\": {               (string) Branch ID of upgrade\n"
            "      \"name\": \"upgrade_name\",   (string) Name of the upgrade\n"
            "      \"activationheight\": n,     (numeric) Block height of activation\n"
            "      \"status\": \"status\",       (string) Current status of upgrade\n"
            "      \"info\": \"details\"         (string) Additional upgrade information\n"
            "    },\n"
            "    ...\n"
            "  },\n"
            "  \"consensus\": {                 (object) Current consensus rule information\n"
            "    \"chaintip\": \"branch_id\",   (string) Branch ID for current chain tip validation\n"
            "    \"nextblock\": \"branch_id\"   (string) Branch ID for next block validation\n"
            "  }\n"
            "}\n"
            "\nNote: When the chain tip is at the last block before a network upgrade,\n"
            "consensus.chaintip may differ from consensus.nextblock.\n"
            "\nExamples:\n"
            "\nGet current blockchain information:\n"
            + HelpExampleCli("getblockchaininfo", "")
            + "\nAs a JSON-RPC call:\n"
            + HelpExampleRpc("getblockchaininfo", "")
        );

    LOCK(cs_main);
    double progress;
    if ( chainName.isKMD() ) {
        progress = Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip());
    } else {
        int32_t longestchain = KOMODO_LONGESTCHAIN;
	    progress = (longestchain > 0 ) ? (double) chainActive.Height() / longestchain : 1.0;
    }
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("chain",                 Params().NetworkIDString()));
    obj.push_back(Pair("blocks",                (int)chainActive.Height()));
    obj.push_back(Pair("synced",                KOMODO_INSYNC!=0));
    obj.push_back(Pair("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1));
    obj.push_back(Pair("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex()));
    obj.push_back(Pair("difficulty",            (double)GetNetworkDifficulty()));
    obj.push_back(Pair("verificationprogress",  progress));
    obj.push_back(Pair("chainwork",             chainActive.Tip()->nChainWork.GetHex()));
    obj.push_back(Pair("pruned",                fPruneMode));

    SproutMerkleTree tree;
    pcoinsTip->GetSproutAnchorAt(pcoinsTip->GetBestAnchor(SPROUT), tree);
    obj.push_back(Pair("commitments",           static_cast<uint64_t>(tree.size())));

    CBlockIndex* tip = chainActive.Tip();
    obj.pushKV("chainSupply", ValuePoolDesc(std::nullopt, tip->nChainTotalSupply, std::nullopt));
    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc(std::string("transparent"), tip->nChainTransparentValue, std::nullopt));
    valuePools.push_back(ValuePoolDesc(std::string("sprout"), tip->nChainSproutValue, std::nullopt));
    valuePools.push_back(ValuePoolDesc(std::string("sapling"), tip->nChainSaplingValue, std::nullopt));
    valuePools.push_back(ValuePoolDesc(std::string("orchard"), tip->nChainOrchardValue, std::nullopt));
    valuePools.push_back(ValuePoolDesc(std::string("burned"), tip->nChainTotalBurned, std::nullopt));
    obj.push_back(Pair("valuePools",            valuePools));

    const Consensus::Params& consensusParams = Params().GetConsensus();
    UniValue softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    obj.push_back(Pair("softforks",             softforks));

    UniValue upgrades(UniValue::VOBJ);
    for (int i = Consensus::UPGRADE_OVERWINTER; i < Consensus::MAX_NETWORK_UPGRADES; i++) {
        NetworkUpgradeDescPushBack(upgrades, consensusParams, Consensus::UpgradeIndex(i), tip->nHeight);
    }
    obj.push_back(Pair("upgrades", upgrades));

    UniValue consensus(UniValue::VOBJ);
    consensus.push_back(Pair("chaintip", HexInt(CurrentEpochBranchId(tip->nHeight, consensusParams))));
    consensus.push_back(Pair("nextblock", HexInt(CurrentEpochBranchId(tip->nHeight + 1, consensusParams))));
    obj.push_back(Pair("consensus", consensus));

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        obj.push_back(Pair("pruneheight",        block->nHeight));
    }
    return obj;
}

/**
 * @brief Comparison function for sorting getchaintips heads by height
 * 
 * Sorts blocks by height in descending order, with pointer comparison as tiebreaker
 * to ensure unequal blocks with the same height don't compare equal.
 */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

#include <pthread.h>

/**
 * @brief Get information about all known chain tips
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON array with information about all chain tips including orphaned branches
 */
UniValue getchaintips(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getchaintips\n"
            "Return information about all known tips in the block tree,"
            " including the main chain as well as orphaned branches.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"height\": xxxx,         (numeric) height of the chain tip\n"
            "    \"hash\": \"xxxx\",         (string) block hash of the tip\n"
            "    \"branchlen\": 0          (numeric) zero for main chain\n"
            "    \"status\": \"active\"      (string) \"active\" for the main chain\n"
            "  },\n"
            "  {\n"
            "    \"height\": xxxx,\n"
            "    \"hash\": \"xxxx\",\n"
            "    \"branchlen\": 1          (numeric) length of branch connecting the tip to the main chain\n"
            "    \"status\": \"xxxx\"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)\n"
            "  }\n"
            "]\n"
            "Possible values for status:\n"
            "1.  \"invalid\"               This branch contains at least one invalid block\n"
            "2.  \"headers-only\"          Not all blocks for this branch are available, but the headers are valid\n"
            "3.  \"valid-headers\"         All blocks are available for this branch, but they were never fully validated\n"
            "4.  \"valid-fork\"            This branch is not part of the active chain, but is fully validated\n"
            "5.  \"active\"                This is the tip of the active main chain, which is certainly valid\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintips", "")
            + HelpExampleRpc("getchaintips", "")
        );

    LOCK(cs_main);

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block.  */
    /*static pthread_mutex_t mutex; static int32_t didinit;
    if ( didinit == 0 )
    {
        pthread_mutex_init(&mutex,NULL);
        didinit = 1;
    }
    pthread_mutex_lock(&mutex);*/
    std::set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    int32_t n = 0;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        n++;
        setTips.insert(item.second);
    }
    fprintf(stdout,"iterations getchaintips %d\n",n);
    n = 0;
    BOOST_FOREACH(const PAIRTYPE(const uint256, CBlockIndex*)& item, mapBlockIndex)
    {
        const CBlockIndex* pprev=0;
        n++;
        if ( item.second != 0 )
            pprev = item.second->pprev;
        if (pprev)
            setTips.erase(pprev);
    }
    fprintf(stdout,"iterations getchaintips %d\n",n);
    //pthread_mutex_unlock(&mutex);

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR); const CBlockIndex *forked;
    BOOST_FOREACH(const CBlockIndex* block, setTips)
        {
            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("height", block->nHeight));
            obj.push_back(Pair("hash", block->phashBlock->GetHex()));
            forked = chainActive.FindFork(block);
            if ( forked != 0 )
            {
                const int branchLen = block->nHeight - forked->nHeight;
                obj.push_back(Pair("branchlen", branchLen));

                string status;
                if (chainActive.Contains(block)) {
                    // This block is part of the currently active chain.
                    status = "active";
                } else if (block->nStatus & BLOCK_FAILED_MASK) {
                    // This block or one of its ancestors is invalid.
                    status = "invalid";
                } else if (block->nChainTx == 0) {
                    // This block cannot be connected because full block data for it or one of its parents is missing.
                    status = "headers-only";
                } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
                    // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
                    status = "valid-fork";
                } else if (block->IsValid(BLOCK_VALID_TREE)) {
                    // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
                    status = "valid-headers";
                } else {
                    // No clue.
                    status = "unknown";
                }
                obj.push_back(Pair("status", status));
            }
            res.push_back(obj);
        }

    return res;
}

/**
 * @brief Get tree state information for all shielded transaction types at a specific block
 * @param params RPC parameters: [block_hash_or_height]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with Sprout, Sapling, and Orchard commitment tree states
 */
UniValue z_gettreestate(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "z_gettreestate \"hash|height\"\n"
            "Return information about the given block's tree state for all shielded transaction types.\n"
            "\nArguments:\n"
            "1. \"hash|height\"          (string, required) The block hash or height. Height can be negative where -1 is the last known valid block\n"
            "\nResult:\n"
            "{\n"
            "  \"hash\": \"hash\",         (string) hex block hash\n"
            "  \"height\": n,              (numeric) block height\n"
            "  \"time\": n,                (numeric) block time: UTC seconds since the Unix 1970-01-01 epoch\n"
            "  \"sprout\": {\n"
            "    \"active\": true|false,   (boolean) whether Sprout is active at this height\n"
            "    \"skipHash\": \"hash\",   (string, optional) hash of most recent block with more information\n"
            "    \"commitments\": {\n"
            "      \"finalRoot\": \"hex\", (string) Sprout commitment tree root\n"
            "      \"finalState\": \"hex\" (string, optional) Sprout commitment tree state\n"
            "    }\n"
            "  },\n"
            "  \"sapling\": {\n"
            "    \"active\": true|false,   (boolean) whether Sapling is active at this height\n"
            "    \"skipHash\": \"hash\",   (string, optional) hash of most recent block with more information\n"
            "    \"commitments\": {\n"
            "      \"finalRoot\": \"hex\", (string) Sapling commitment tree root\n"
            "      \"finalState\": \"hex\" (string, optional) Sapling frontier state\n"
            "    }\n"
            "  },\n"
            "  \"orchard\": {\n"
            "    \"active\": true|false,   (boolean) whether Orchard is active at this height\n"
            "    \"skipHash\": \"hash\",   (string, optional) hash of most recent block with more information\n"
            "    \"commitments\": {\n"
            "      \"finalRoot\": \"hex\", (string) Orchard commitment tree root\n"
            "      \"finalState\": \"hex\" (string, optional) Orchard frontier state\n"
            "    }\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("z_gettreestate", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + HelpExampleRpc("z_gettreestate", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + HelpExampleCli("z_gettreestate", "12800")
            + HelpExampleRpc("z_gettreestate", "12800")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        strHash = chainActive[parseHeightArg(strHash, chainActive.Height())]->GetBlockHash().GetHex();
    }
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    const CBlockIndex* const pindex = mapBlockIndex[hash];
    if (!chainActive.Contains(pindex)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Requested block is not part of the main chain");
    }

    UniValue res(UniValue::VOBJ);
    res.pushKV("hash", pindex->GetBlockHash().GetHex());
    res.pushKV("height", pindex->nHeight);
    res.pushKV("time", int64_t(pindex->nTime));

    bool saplingActive = NetworkUpgradeActive(pindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING);
    bool orchardActive = NetworkUpgradeActive(pindex->nHeight, Params().GetConsensus(), Consensus::UPGRADE_ORCHARD);

    // sprout
    {
        UniValue sprout_result(UniValue::VOBJ);
        UniValue sprout_commitments(UniValue::VOBJ);
        sprout_result.pushKV("active", true);
        sprout_commitments.pushKV("finalRoot", pindex->hashFinalSproutRoot.GetHex());
        SproutMerkleTree tree;
        if (pcoinsTip->GetSproutAnchorAt(pindex->hashFinalSproutRoot, tree)) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s << tree;
            sprout_commitments.pushKV("finalState", HexStr(s.begin(), s.end()));
        } else {
            // Set skipHash to the most recent block that has a finalState.
            const CBlockIndex* pindex_skip = pindex->pprev;
            while (pindex_skip && !pcoinsTip->GetSproutAnchorAt(pindex_skip->hashFinalSproutRoot, tree)) {
                pindex_skip = pindex_skip->pprev;
            }
            if (pindex_skip) {
                sprout_result.pushKV("skipHash", pindex_skip->GetBlockHash().GetHex());
            }
        }
        sprout_result.pushKV("commitments", sprout_commitments);
        res.pushKV("sprout", sprout_result);
    }

    //Sapling Frontier
    int sapling_activation_height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight; /* ASSETCHAINS_SAPLING */
    //if (sapling_activation_height > Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
    {
        UniValue sapling_result(UniValue::VOBJ);
        UniValue sapling_commitments(UniValue::VOBJ);
        sapling_result.pushKV("active", saplingActive);
        sapling_commitments.pushKV("finalRoot", pindex->hashFinalSaplingRoot.GetHex());
        bool need_skiphash = false;
        SaplingMerkleFrontier tree;
        if (pcoinsTip->GetSaplingFrontierAnchorAt(pindex->hashFinalSaplingRoot, tree)) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s << SaplingMerkleFrontierLegacySer(tree);
            sapling_commitments.pushKV("finalState", HexStr(s.begin(), s.end()));
        } else {
            // Set skipHash to the most recent block that has a finalState.
            const CBlockIndex* pindex_skip = pindex->pprev;
            auto saplingActive = [&](const CBlockIndex* pindex_cur) -> bool {
                return pindex_cur && pindex_cur->nHeight >= sapling_activation_height;
            };
            while (saplingActive(pindex_skip) && !pcoinsTip->GetSaplingFrontierAnchorAt(pindex_skip->hashFinalSaplingRoot, tree)) {
                pindex_skip = pindex_skip->pprev;
            }
            if (saplingActive(pindex_skip)) {
                sapling_result.pushKV("skipHash", pindex_skip->GetBlockHash().GetHex());
            }
        }
        sapling_result.pushKV("commitments", sapling_commitments);
        res.pushKV("sapling", sapling_result);
    }

    //Orchard Frontier
    int orchard_activation_height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_ORCHARD].nActivationHeight;
    //if (orchard_activation_height > Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
    {
        UniValue orchard_result(UniValue::VOBJ);
        UniValue orchard_commitments(UniValue::VOBJ);
        orchard_result.pushKV("active", orchardActive);
        orchard_commitments.pushKV("finalRoot", HexStr(pindex->hashFinalOrchardRoot.begin(), pindex->hashFinalOrchardRoot.end()));
        bool need_skiphash = false;
        OrchardMerkleFrontier tree;
        if (pcoinsTip->GetOrchardFrontierAnchorAt(pindex->hashFinalOrchardRoot, tree)) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s << OrchardMerkleFrontierLegacySer(tree);
            orchard_commitments.pushKV("finalState", HexStr(s.begin(), s.end()));
        } else {
            // Set skipHash to the most recent block that has a finalState.
            const CBlockIndex* pindex_skip = pindex->pprev;
            auto orchardActive = [&](const CBlockIndex* pindex_cur) -> bool {
                return pindex_cur && pindex_cur->nHeight >= orchard_activation_height;
            };
            while (orchardActive(pindex_skip) && !pcoinsTip->GetOrchardFrontierAnchorAt(pindex_skip->hashFinalOrchardRoot, tree)) {
                pindex_skip = pindex_skip->pprev;
            }
            if (orchardActive(pindex_skip)) {
                orchard_result.pushKV("skipHash", pindex_skip->GetBlockHash().GetHex());
            }
        }
        orchard_result.pushKV("commitments", orchard_commitments);
        res.pushKV("orchard", orchard_result);
    }

    return res;
}

UniValue z_gettreestatelegacy(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "z_gettreestatelegacy \"hash|height\"\n"
            "Return information about the given block's tree state using legacy Sapling merkle tree format.\n"
            "\nArguments:\n"
            "1. \"hash|height\"          (string, required) The block hash or height. Height can be negative where -1 is the last known valid block\n"
            "\nResult:\n"
            "{\n"
            "  \"hash\": \"hash\",         (string) hex block hash\n"
            "  \"height\": n,              (numeric) block height\n"
            "  \"time\": n,                (numeric) block time: UTC seconds since the Unix 1970-01-01 epoch\n"
            "  \"sprout\": {\n"
            "    \"skipHash\": \"hash\",   (string) hash of most recent block with more information\n"
            "    \"commitments\": {\n"
            "      \"finalRoot\": \"hex\", (string)\n"
            "      \"finalState\": \"hex\" (string)\n"
            "    }\n"
            "  },\n"
            "  \"sapling\": {\n"
            "    \"skipHash\": \"hash\",   (string) hash of most recent block with more information\n"
            "    \"commitments\": {\n"
            "      \"finalRoot\": \"hex\", (string)\n"
            "      \"finalState\": \"hex\" (string)\n"
            "    }\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("z_gettreestatelegacy", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + HelpExampleRpc("z_gettreestatelegacy", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
            + HelpExampleCli("z_gettreestatelegacy", "12800")
            + HelpExampleRpc("z_gettreestatelegacy", "12800")
        );

    LOCK(cs_main);

    std::string strHash = params[0].get_str();

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        strHash = chainActive[parseHeightArg(strHash, chainActive.Height())]->GetBlockHash().GetHex();
    }
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
    const CBlockIndex* const pindex = mapBlockIndex[hash];
    if (!chainActive.Contains(pindex)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Requested block is not part of the main chain");
    }

    UniValue res(UniValue::VOBJ);
    res.pushKV("hash", pindex->GetBlockHash().GetHex());
    res.pushKV("height", pindex->nHeight);
    res.pushKV("time", int64_t(pindex->nTime));

    // sprout
    {
        UniValue sprout_result(UniValue::VOBJ);
        UniValue sprout_commitments(UniValue::VOBJ);
        sprout_commitments.pushKV("finalRoot", pindex->hashFinalSproutRoot.GetHex());
        SproutMerkleTree tree;
        if (pcoinsTip->GetSproutAnchorAt(pindex->hashFinalSproutRoot, tree)) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s << tree;
            sprout_commitments.pushKV("finalState", HexStr(s.begin(), s.end()));
        } else {
            // Set skipHash to the most recent block that has a finalState.
            const CBlockIndex* pindex_skip = pindex->pprev;
            while (pindex_skip && !pcoinsTip->GetSproutAnchorAt(pindex_skip->hashFinalSproutRoot, tree)) {
                pindex_skip = pindex_skip->pprev;
            }
            if (pindex_skip) {
                sprout_result.pushKV("skipHash", pindex_skip->GetBlockHash().GetHex());
            }
        }
        sprout_result.pushKV("commitments", sprout_commitments);
        res.pushKV("sprout", sprout_result);
    }

    // sapling (legacy incremental merkle tree)
    int sapling_activation_height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_SAPLING].nActivationHeight;
    if (sapling_activation_height > Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
    {
        UniValue sapling_result(UniValue::VOBJ);
        UniValue sapling_commitments(UniValue::VOBJ);
        sapling_commitments.pushKV("finalRoot", pindex->hashFinalSaplingRoot.GetHex());
        bool need_skiphash = false;
        SaplingMerkleTree tree;
        if (pcoinsTip->GetSaplingAnchorAt(pindex->hashFinalSaplingRoot, tree)) {
            CDataStream s(SER_NETWORK, PROTOCOL_VERSION);
            s << tree;
            sapling_commitments.pushKV("finalState", HexStr(s.begin(), s.end()));
        } else {
            // Set skipHash to the most recent block that has a finalState.
            const CBlockIndex* pindex_skip = pindex->pprev;
            auto saplingActive = [&](const CBlockIndex* pindex_cur) -> bool {
                return pindex_cur && pindex_cur->nHeight >= sapling_activation_height;
            };
            while (saplingActive(pindex_skip) && !pcoinsTip->GetSaplingAnchorAt(pindex_skip->hashFinalSaplingRoot, tree)) {
                pindex_skip = pindex_skip->pprev;
            }
            if (saplingActive(pindex_skip)) {
                sapling_result.pushKV("skipHash", pindex_skip->GetBlockHash().GetHex());
            }
        }
        sapling_result.pushKV("commitments", sapling_commitments);
        res.pushKV("sapling", sapling_result);
    }

    return res;
}

/**
 * @brief Convert memory pool information to JSON format
 * @return JSON object containing memory pool statistics
 */
UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("size", (int64_t) mempool.size()));
    ret.push_back(Pair("bytes", (int64_t) mempool.GetTotalTxSize()));
    ret.push_back(Pair("usage", (int64_t) mempool.DynamicMemoryUsage()));

    if (Params().NetworkIDString() == "regtest") {
        ret.push_back(Pair("fullyNotified", mempool.IsFullyNotified()));
    }

    return ret;
}

/**
 * @brief Get details about the current memory pool state
 * @param params RPC parameters (none required)
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with memory pool statistics including size, bytes, and usage
 */
UniValue getmempoolinfo(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmempoolinfo\n"
            "\nReturns details on the active state of the TX memory pool.\n"
            "\nResult:\n"
            "{\n"
            "  \"size\": xxxxx                (numeric) Current tx count\n"
            "  \"bytes\": xxxxx               (numeric) Sum of all tx sizes\n"
            "  \"usage\": xxxxx               (numeric) Total memory usage for the mempool\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

/**
 * @brief Look up a block index by its hash
 * @param hash Block hash to search for
 * @return Pointer to block index if found, nullptr otherwise
 */
inline CBlockIndex* LookupBlockIndex(const uint256& hash)
{
    AssertLockHeld(cs_main);
    BlockMap::const_iterator it = mapBlockIndex.find(hash);
    return it == mapBlockIndex.end() ? nullptr : it->second;
}

/**
 * @brief Compute statistics about transaction count and rate in the blockchain
 * @param params RPC parameters: [num_blocks, block_hash]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return JSON object with transaction statistics and rates over specified window
 */
UniValue getchaintxstats(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
                "getchaintxstats\n"
                "\nCompute statistics about the total number and rate of transactions in the chain.\n"
                "\nArguments:\n"
                "1. nblocks   (numeric, optional) Number of blocks in averaging window.\n"
                "2. blockhash (string, optional) The hash of the block which ends the window.\n"
                "\nResult:\n"
            "{\n"
            "  \"time\": xxxxx,                         (numeric) The timestamp for the final block in the window in UNIX format.\n"
            "  \"txcount\": xxxxx,                      (numeric) The total number of transactions in the chain up to that point.\n"
            "  \"window_final_block_hash\": \"...\",      (string) The hash of the final block in the window.\n"
            "  \"window_block_count\": xxxxx,           (numeric) Size of the window in number of blocks.\n"
            "  \"window_tx_count\": xxxxx,              (numeric) The number of transactions in the window. Only returned if \"window_block_count\" is > 0.\n"
            "  \"window_interval\": xxxxx,              (numeric) The elapsed time in the window in seconds. Only returned if \"window_block_count\" is > 0.\n"
            "  \"txrate\": x.xx,                        (numeric) The average rate of transactions per second in the window. Only returned if \"window_interval\" is > 0.\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getchaintxstats", "")
            + HelpExampleRpc("getchaintxstats", "2016")
        );

    const CBlockIndex* pindex;
    int blockcount = 30 * 24 * 60 * 60 / Params().GetConsensus().nPowTargetSpacing; // By default: 1 month

    if (params[1].isNull()) {
        LOCK(cs_main);
        pindex = chainActive.Tip();
    } else {
        uint256 hash(ParseHashV(params[1], "blockhash"));
        LOCK(cs_main);
        pindex = LookupBlockIndex(hash);
        if (!pindex) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        }
        if (!chainActive.Contains(pindex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block is not in main chain");
        }
    }

    assert(pindex != nullptr);

    if (params[0].isNull()) {
        blockcount = std::max(0, std::min(blockcount, pindex->nHeight - 1));
    } else {
        blockcount = params[0].get_int();

        if (blockcount < 0 || (blockcount > 0 && blockcount >= pindex->nHeight)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block count: should be between 0 and the block's height - 1");
        }
    }

    const CBlockIndex* pindexPast = pindex->GetAncestor(pindex->nHeight - blockcount);
    int nTimeDiff = pindex->GetMedianTimePast() - pindexPast->GetMedianTimePast();
    int nTxDiff = pindex->nChainTx - pindexPast->nChainTx;

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("time", (int64_t)pindex->nTime);
    ret.pushKV("txcount", (int64_t)pindex->nChainTx);
    ret.pushKV("window_final_block_hash", pindex->GetBlockHash().GetHex());
    ret.pushKV("window_block_count", blockcount);
    if (blockcount > 0) {
        ret.pushKV("window_tx_count", nTxDiff);
        ret.pushKV("window_interval", nTimeDiff);
        if (nTimeDiff > 0) {
            ret.pushKV("txrate", ((double)nTxDiff) / nTimeDiff);
        }
    }

    return ret;
}

/**
 * @brief Permanently mark a block as invalid for consensus rule violation
 * @param params RPC parameters: [block_hash]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Null value on success, throws exception on error
 */
UniValue invalidateblock(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "invalidateblock \"hash\"\n"
            "\nPermanently marks a block as invalid, as if it violated a consensus rule.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to mark as invalid\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("invalidateblock", "\"blockhash\"")
            + HelpExampleRpc("invalidateblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(true,state,false);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

/**
 * @brief Remove invalidity status of a block and reconsider it for activation
 * @param params RPC parameters: [block_hash]
 * @param fHelp True to display help information
 * @param mypk Public key for authentication
 * @return Null value on success, throws exception on error
 */
UniValue reconsiderblock(const UniValue& params, bool fHelp, const CPubKey& mypk)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "reconsiderblock \"hash\"\n"
            "\nRemoves invalidity status of a block and its descendants, reconsider them for activation.\n"
            "This can be used to undo the effects of invalidateblock.\n"
            "\nArguments:\n"
            "1. hash   (string, required) the hash of the block to reconsider\n"
            "\nResult:\n"
            "\nExamples:\n"
            + HelpExampleCli("reconsiderblock", "\"blockhash\"")
            + HelpExampleRpc("reconsiderblock", "\"blockhash\"")
        );

    std::string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid()) {
        ActivateBestChain(true,state,false);
    }

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      true  },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       true  },
    { "blockchain",         "getblockcount",          &getblockcount,          true  },
    { "blockchain",         "getblock",               &getblock,               true  },
    { "blockchain",         "getblockhash",           &getblockhash,           true  },
    { "blockchain",         "getblockheader",         &getblockheader,         true  },
    { "blockchain",         "getchaintips",           &getchaintips,           true  },
    { "blockchain",         "z_gettreestate",         &z_gettreestate,         true  },
    { "blockchain",         "z_gettreestatelegacy",   &z_gettreestatelegacy,   true  },
    { "blockchain",         "getchaintxstats",        &getchaintxstats,        true  },
    { "blockchain",         "getdifficulty",          &getdifficulty,          true  },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         true  },
    { "blockchain",         "getrawmempool",          &getrawmempool,          true  },
    { "blockchain",         "gettxout",               &gettxout,               true  },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        true  },
    { "blockchain",         "verifychain",            &verifychain,            true  },

    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        true  },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        true  },
};

/**
 * @brief Register all blockchain-related RPC commands with the RPC table
 * @param tableRPC RPC command table to register commands with
 */
void RegisterBlockchainRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
