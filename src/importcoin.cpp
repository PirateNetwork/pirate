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

#include "crosschain.h"
#include "importcoin.h"
#include "cc/utils.h"
#include "coins.h"
#include "hash.h"
#include "script/cc.h"
#include "primitives/transaction.h"
#include "core_io.h"
#include "script/sign.h"
#include "wallet/wallet.h"
#include "cc/CCinclude.h"
#include "komodo_bitcoind.h"


/******
 * @brief makes import tx for either coins or tokens
 * @param proof the proof
 * @param burnTx the inputs
 * @param payouts the outputs
 * @param nExpiryHeightOverride if an actual height (!= 0) makes a tx for validating int import tx
 * @returns the generated import transaction
 */
CTransaction MakeImportCoinTransaction(const ImportProof proof, const CTransaction burnTx, 
        const std::vector<CTxOut> payouts, uint32_t nExpiryHeightOverride)
{
    CScript scriptSig;

    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    if (mtx.fOverwintered) 
        mtx.nExpiryHeight = 0;
    mtx.vout = payouts;
    if (mtx.vout.size() == 0)
        return CTransaction(mtx);

    // add special import tx vin:
    scriptSig << E_MARSHAL(ss << EVAL_IMPORTCOIN);      // simple payload for coins
    mtx.vin.push_back(CTxIn(COutPoint(burnTx.GetHash(), 10e8), scriptSig));

    if (nExpiryHeightOverride != 0)
        mtx.nExpiryHeight = nExpiryHeightOverride;  //this is for validation code, to make a tx used for validating the import tx

    auto importData = E_MARSHAL(ss << EVAL_IMPORTCOIN; ss << proof; ss << burnTx);  // added evalcode to differentiate importdata from token opret
    // if it is tokens:
    vscript_t vopret;
    GetOpReturnData(mtx.vout.back().scriptPubKey, vopret);

    if (!vopret.empty()) {
        std::vector<uint8_t> vorigpubkey;
        uint8_t funcId;
        std::vector <std::pair<uint8_t, vscript_t>> oprets;
        std::string name, desc;

        if (DecodeTokenCreateOpRet(mtx.vout.back().scriptPubKey, vorigpubkey, name, desc, oprets) == 'c') {    // parse token 'c' opret
            mtx.vout.pop_back(); //remove old token opret
            oprets.push_back(std::make_pair(OPRETID_IMPORTDATA, importData));
            mtx.vout.push_back(CTxOut(0, EncodeTokenCreateOpRet('c', vorigpubkey, name, desc, oprets)));   // make new token 'c' opret with importData                                                                                    
        }
        else {
            LOGSTREAM("importcoin", CCLOG_INFO, stream << "MakeImportCoinTransaction() incorrect token import opret" << std::endl);
        }
    }
    else { //no opret in coin payouts
        mtx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << importData));     // import tx's opret now is in the vout's tail
    }

    return CTransaction(mtx);
}

/*****
 * @brief makes import tx that includes spending markers to track account state
 * @param proof
 * @param burnTx
 * @param payouts
 * @param nExpiryHeighOverride
 * @returns the transaction
 */
CTransaction MakePegsImportCoinTransaction(const ImportProof proof, const CTransaction burnTx, 
        const std::vector<CTxOut> payouts, uint32_t nExpiryHeightOverride)
{
    CMutableTransaction mtx=MakeImportCoinTransaction(proof,burnTx,payouts);
    // for spending markers in import tx - to track account state
    uint256 accounttxid = burnTx.vin[0].prevout.hash;    
    mtx.vin.push_back(CTxIn(accounttxid,0,CScript()));
    mtx.vin.push_back(CTxIn(accounttxid,1,CScript()));
    return (mtx);
}

/******
 * @brief make a burn output
 * @param value the amount
 * @param targetCCid the ccid
 * @param targetSymbol
 * @param payouts the outputs
 * @param rawproof the proof in binary form
 * @returns the txout
 */
CTxOut MakeBurnOutput(CAmount value, uint32_t targetCCid, const std::string& targetSymbol, 
        const std::vector<CTxOut> payouts, const std::vector<uint8_t> rawproof)
{
    std::vector<uint8_t> opret;
    opret = E_MARSHAL(ss << (uint8_t)EVAL_IMPORTCOIN;  // should mark burn opret to differentiate it from token opret
                      ss << VARINT(targetCCid);
                      ss << targetSymbol;
                      ss << SerializeHash(payouts);
                      ss << rawproof);
    return CTxOut(value, CScript() << OP_RETURN << opret);
}

/******
 * @brief make a burn output
 * @param value 
 * @param targetCCid the target ccid
 * @param targetSymbol the target symbol
 * @param payouts the outputs
 * @param rawproof the proof in binary form
 * @param bindtxid
 * @param publishers
 * @param txids
 * @param burntxid
 * @param height
 * @param burnvout
 * @param rawburntx
 * @param destpub
 * @param amount
 * @returns the txout
 */
CTxOut MakeBurnOutput(CAmount value, uint32_t targetCCid, const std::string& targetSymbol, 
        const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof, uint256 bindtxid,
        std::vector<CPubKey> publishers,std::vector<uint256> txids,uint256 burntxid,
        int32_t height,int32_t burnvout, const std::string& rawburntx,CPubKey destpub, int64_t amount)
{
    std::vector<uint8_t> opret;
    opret = E_MARSHAL(ss << (uint8_t)EVAL_IMPORTCOIN;
                      ss << VARINT(targetCCid);
                      ss << targetSymbol;
                      ss << SerializeHash(payouts);
                      ss << rawproof;
                      ss << bindtxid;
                      ss << publishers;
                      ss << txids;
                      ss << burntxid;
                      ss << height;
                      ss << burnvout;
                      ss << rawburntx;                      
                      ss << destpub;
                      ss << amount);
                      
    return CTxOut(value, CScript() << OP_RETURN << opret);
}

/******
 * @brief make a burn output
 * @param value the amount
 * @param targetCCid the ccid
 * @param targetSymbol
 * @param payouts the outputs
 * @param rawproof the proof in binary form
 * @param srcaddr the source address
 * @param receipt
 * @returns the txout
 */
CTxOut MakeBurnOutput(CAmount value, uint32_t targetCCid, const std::string& targetSymbol, 
        const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof,const std::string& srcaddr,
        const std::string& receipt)
{
    std::vector<uint8_t> opret;
    opret = E_MARSHAL(ss << (uint8_t)EVAL_IMPORTCOIN;
                      ss << VARINT(targetCCid);
                      ss << targetSymbol;
                      ss << SerializeHash(payouts);
                      ss << rawproof;
                      ss << srcaddr;
                      ss << receipt);
    return CTxOut(value, CScript() << OP_RETURN << opret);
}

/******
 * @brief make a burn output
 * @param value the amount
 * @param targetCCid the ccid
 * @param targetSymbol
 * @param payouts the outputs
 * @param rawproof the proof in binary form
 * @param pegstxid
 * @param tokenid
 * @param srcpub
 * @param amount
 * @param account
 * @returns the txout
 */
CTxOut MakeBurnOutput(CAmount value,uint32_t targetCCid, const std::string& targetSymbol,
        const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof,uint256 pegstxid,
        uint256 tokenid,CPubKey srcpub,int64_t amount, std::pair<int64_t,int64_t> account)
{
    std::vector<uint8_t> opret;
    opret = E_MARSHAL(ss << (uint8_t)EVAL_IMPORTCOIN;
                      ss << VARINT(targetCCid);
                      ss << targetSymbol;
                      ss << SerializeHash(payouts);
                      ss << rawproof;
                      ss << pegstxid;
                      ss << tokenid;
                      ss << srcpub;
                      ss << amount;
                      ss << account);
    return CTxOut(value, CScript() << OP_RETURN << opret);
}

/****
 * @brief break a serialized import tx into its components
 * @param[in] importTx the transaction
 * @param[out] proof the proof
 * @param[out] burnTx the burn transaction
 * @param[out] payouts the collection of tx outs
 * @returns true on success
 */
bool UnmarshalImportTx(const CTransaction importTx, ImportProof &proof, CTransaction &burnTx, 
        std::vector<CTxOut> &payouts)
{
    if (importTx.vout.size() < 1) 
        return false;
    
    if ((!importTx.IsPegsImport() && importTx.vin.size() != 1) || importTx.vin[0].scriptSig != (CScript() << E_MARSHAL(ss << EVAL_IMPORTCOIN))) {
        LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalImportTx() incorrect import tx vin" << std::endl);
        return false;
    }

    std::vector<uint8_t> vImportData;
    GetOpReturnData(importTx.vout.back().scriptPubKey, vImportData);
    if (vImportData.empty()) {
        LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalImportTx() no opret" << std::endl);
        return false;
    }

    if (vImportData.begin()[0] == EVAL_TOKENS) {          // if it is tokens
        // get import data after token opret:
        std::vector<std::pair<uint8_t, vscript_t>>  oprets;
        std::vector<uint8_t> vorigpubkey;
        std::string name, desc;

        if (DecodeTokenCreateOpRet(importTx.vout.back().scriptPubKey, vorigpubkey, name, desc, oprets) == 0) {
            LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalImportTx() could not decode token opret" << std::endl);
            return false;
        }

        GetOpretBlob(oprets, OPRETID_IMPORTDATA, vImportData);  // fetch import data after token opret
        for (std::vector<std::pair<uint8_t, vscript_t>>::const_iterator i = oprets.begin(); i != oprets.end(); i++)
            if ((*i).first == OPRETID_IMPORTDATA) {
                oprets.erase(i);            // remove import data from token opret to restore original payouts:
                break;
            }

        payouts = std::vector<CTxOut>(importTx.vout.begin(), importTx.vout.end()-1);       //exclude opret with import data 
        payouts.push_back(CTxOut(0, EncodeTokenCreateOpRet('c', vorigpubkey, name, desc, oprets)));   // make original payouts token opret (without import data)
    }
    else {
        //payouts = std::vector<CTxOut>(importTx.vout.begin()+1, importTx.vout.end());   // see next
        payouts = std::vector<CTxOut>(importTx.vout.begin(), importTx.vout.end() - 1);   // skip opret; and it is now in the back
    }

    uint8_t evalCode;
    bool retcode = E_UNMARSHAL(vImportData, ss >> evalCode; ss >> proof; ss >> burnTx);
    if (!retcode)
        LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalImportTx() could not unmarshal import data" << std::endl);
    return retcode;
}

/****
 * @brief break a serialized burn tx into its components
 * @param[in] burnTx the transaction
 * @param[out] targetSymbol the symbol
 * @param[out] targetCCid the target ccid
 * @param[out] payoutsHash the hash of the payouts
 * @param[out] rawproof the bytes of the proof
 * @returns true on success
 */
bool UnmarshalBurnTx(const CTransaction burnTx, std::string &targetSymbol, uint32_t *targetCCid, 
        uint256 &payoutsHash,std::vector<uint8_t>&rawproof)
{
    if (burnTx.vout.size() == 0) 
        return false;

    std::vector<uint8_t> vburnOpret; 
    GetOpReturnData(burnTx.vout.back().scriptPubKey, vburnOpret);
    if (vburnOpret.empty()) {
        LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalBurnTx() cannot unmarshal burn tx: empty burn opret" << std::endl);
        return false;
    }

    if (vburnOpret.begin()[0] == EVAL_TOKENS) {      //if it is tokens
        std::vector<std::pair<uint8_t, vscript_t>>  oprets;
        uint256 tokenid;
        uint8_t evalCodeInOpret;
        std::vector<CPubKey> voutTokenPubkeys;

        if (DecodeTokenOpRet(burnTx.vout.back().scriptPubKey, evalCodeInOpret, tokenid, voutTokenPubkeys, oprets) != 't')
            return false;

        //skip token opret:
        GetOpretBlob(oprets, OPRETID_BURNDATA, vburnOpret);  // fetch burnOpret after token opret
        if (vburnOpret.empty()) {
            LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalBurnTx() cannot unmarshal token burn tx: empty burn opret for tokenid=" << tokenid.GetHex() << std::endl);
            return false;
        }
    }

    if (vburnOpret.begin()[0] == EVAL_IMPORTCOIN) {
        uint8_t evalCode;
        bool isEof = true;
        return E_UNMARSHAL(vburnOpret,  ss >> evalCode;
                                        ss >> VARINT(*targetCCid);
                                        ss >> targetSymbol;
                                        ss >> payoutsHash;
                                        ss >> rawproof; isEof = ss.eof();) || !isEof; // if isEof == false it means we have successfully read the vars upto 'rawproof'
                                                                                      // and it might be additional data further that we do not need here so we allow !isEof
    }

    LOGSTREAM("importcoin", CCLOG_INFO, stream << "UnmarshalBurnTx() invalid eval code in opret" << std::endl);
    return false;
}

/****
 * @brief break a serialized burn tx into its components
 * @param[in] burnTx the transaction
 * @param[out] srcaddr the source address
 * @param[out] receipt
 * @returns true on success
 */
bool UnmarshalBurnTx(const CTransaction burnTx, std::string &srcaddr, std::string &receipt)
{
    if (burnTx.vout.size() == 0) 
        return false;

    // parts of tx that are deserialized but not returned
    std::vector<uint8_t> rawproof;
    std::string targetSymbol; 
    uint32_t targetCCid; 
    uint256 payoutsHash;
    uint8_t evalCode;

    std::vector<uint8_t> burnOpret;
    GetOpReturnData(burnTx.vout.back().scriptPubKey, burnOpret);

    return (E_UNMARSHAL(burnOpret, ss >> evalCode;
                    ss >> VARINT(targetCCid);
                    ss >> targetSymbol;
                    ss >> payoutsHash;
                    ss >> rawproof;
                    ss >> srcaddr;
                    ss >> receipt));
}

/****
 * @brief break a serialized burn tx into its components
 * @param[in] burnTx the transaction
 * @param[out] bindtxid
 * @param[out] publishers
 * @param[out] txids
 * @param[out] burntxid
 * @param[out] height
 * @param[out] burnvout
 * @param[out] rawburntx
 * @param[out] destpub
 * @param[out] amount
 * @returns true on success
 */
bool UnmarshalBurnTx(const CTransaction burnTx,uint256 &bindtxid,
        std::vector<CPubKey> &publishers,std::vector<uint256> &txids,
        uint256& burntxid, int32_t &height,int32_t &burnvout,
        std::string &rawburntx,CPubKey &destpub, int64_t &amount)
{
    if (burnTx.vout.size() == 0) 
        return false;

    // parts of tx that are deserialized but not returned
    std::vector<uint8_t> rawproof; 
    uint32_t targetCCid; 
    uint256 payoutsHash; 
    std::string targetSymbol;
    uint8_t evalCode;

    std::vector<uint8_t> burnOpret;
    GetOpReturnData(burnTx.vout.back().scriptPubKey, burnOpret);
    return (E_UNMARSHAL(burnOpret, ss >> evalCode;
                    ss >> VARINT(targetCCid);
                    ss >> targetSymbol;
                    ss >> payoutsHash;
                    ss >> rawproof;
                    ss >> bindtxid;
                    ss >> publishers;
                    ss >> txids;
                    ss >> burntxid;
                    ss >> height;
                    ss >> burnvout;
                    ss >> rawburntx;                      
                    ss >> destpub;
                    ss >> amount));
}

/****
 * @brief break a serialized burn tx into its components
 * @param[in] burnTx the transaction
 * @param[out] pegstxid
 * @param[out] tokenid
 * @param[out] srcpub
 * @param[out] amount
 * @param[out] account
 * @returns true on success
 */
bool UnmarshalBurnTx(const CTransaction burnTx,uint256 &pegstxid,uint256 &tokenid,CPubKey &srcpub, 
        int64_t &amount,std::pair<int64_t,int64_t> &account)
{
    std::vector<uint8_t> burnOpret,rawproof; bool isEof=true;
    uint32_t targetCCid; uint256 payoutsHash; std::string targetSymbol;
    uint8_t evalCode;


    if (burnTx.vout.size() == 0) return false;
    GetOpReturnData(burnTx.vout.back().scriptPubKey, burnOpret);
    return (E_UNMARSHAL(burnOpret, ss >> evalCode;
                    ss >> VARINT(targetCCid);
                    ss >> targetSymbol;
                    ss >> payoutsHash;
                    ss >> rawproof;
                    ss >> pegstxid;
                    ss >> tokenid;
                    ss >> srcpub;
                    ss >> amount;
                    ss >> account));
}

/******
 * @brief get the value of the transaction
 * @param tx the transaction
 * @returns the burned value within tx
 */
CAmount GetCoinImportValue(const CTransaction &tx)
{
    ImportProof proof; 
    CTransaction burnTx; 
    std::vector<CTxOut> payouts;
    
    if ( UnmarshalImportTx(tx, proof, burnTx, payouts) ) {
        if (burnTx.vout.size() > 0) {
            vscript_t vburnOpret;

            GetOpReturnData(burnTx.vout.back().scriptPubKey, vburnOpret);
            if (vburnOpret.empty()) {
                LOGSTREAM("importcoin", CCLOG_INFO, stream << "GetCoinImportValue() empty burn opret" << std::endl);
                return 0;
            }

            if ( vburnOpret.begin()[0] == EVAL_TOKENS) {      //if it is tokens

                uint8_t evalCodeInOpret;
                uint256 tokenid;
                std::vector<CPubKey> voutTokenPubkeys;
                std::vector<std::pair<uint8_t, vscript_t>>  oprets;

                if (DecodeTokenOpRet(tx.vout.back().scriptPubKey, evalCodeInOpret, tokenid, voutTokenPubkeys, oprets) == 0)
                    return 0;

                uint8_t nonfungibleEvalCode = EVAL_TOKENS; // init as if no non-fungibles
                vscript_t vnonfungibleOpret;
                GetOpretBlob(oprets, OPRETID_NONFUNGIBLEDATA, vnonfungibleOpret);
                if (!vnonfungibleOpret.empty())
                    nonfungibleEvalCode = vnonfungibleOpret.begin()[0];

                // calc outputs for burn tx
                int64_t ccBurnOutputs = 0;
                for (auto v : burnTx.vout)
                    if (v.scriptPubKey.IsPayToCryptoCondition() &&
                        CTxOut(v.nValue, v.scriptPubKey) == MakeTokensCC1vout(nonfungibleEvalCode, v.nValue, pubkey2pk(ParseHex(CC_BURNPUBKEY))))  // burned to dead pubkey
                        ccBurnOutputs += v.nValue;

                return ccBurnOutputs + burnTx.vout.back().nValue;   // total token burned value
            }
            else
                return burnTx.vout.back().nValue; // coin burned value
        }
    }
    return 0;
}



/*****
 * @brief verify a coin import signature
 * @note CoinImport is different enough from normal script execution that it's not worth 
 * making all the mods neccesary in the interpreter to do the dispatch correctly.
 * @param[in] scriptSig the signature
 * @param[in] checker the checker to use
 * @param[out] state the error state
 * @returns true on success, `state` will contain the reason if false
 */
bool VerifyCoinImport(const CScript& scriptSig, TransactionSignatureChecker& checker, CValidationState &state)
{
    auto pc = scriptSig.begin();

    auto f = [&] () {
        opcodetype opcode;
        std::vector<uint8_t> evalScript;
        if (!scriptSig.GetOp(pc, opcode, evalScript))
            return false;
        if (pc != scriptSig.end())
            return false;
        if (evalScript.size() == 0)
            return false;
        if (evalScript.begin()[0] != EVAL_IMPORTCOIN)
            return false;
        // Ok, all looks good so far...
        CC *cond = CCNewEval(evalScript);
        bool out = checker.CheckEvalCondition(cond);
        cc_free(cond);
        return out;
    };

    return f() ? true : state.Invalid(false, 0, "invalid-coin-import");
}

/****
 * @brief add an import tombstone
 * @param importTx the transaction
 * @param inputs the inputs to be modified
 * @param nHeight the height
 */
void AddImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs, int nHeight)
{
    CCoinsModifier modifier = inputs.ModifyCoins(importTx.vin[0].prevout.hash);
    modifier->nHeight = nHeight;
    modifier->nVersion = 4;
    modifier->vout.push_back(CTxOut(0, CScript() << OP_0));
}

/*****
 * @brief remove an import tombstone from inputs
 * @param importTx the transaction
 * @param inputs what to modify
 */
void RemoveImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs)
{
    inputs.ModifyCoins(importTx.vin[0].prevout.hash)->Clear();
}

/*****
 * @brief See if a tombstone exists
 * @param importTx the transaction
 * @param inputs
 * @returns true if the transaction is a tombstone
 */
bool ExistsImportTombstone(const CTransaction &importTx, const CCoinsViewCache &inputs)
{
    return inputs.HaveCoins(importTx.vin[0].prevout.hash);
}
