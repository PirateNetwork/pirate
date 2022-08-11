#pragma once
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
#include "cc/eval.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include <cryptoconditions.h>

enum ProofKind : uint8_t {
    PROOF_NONE = 0x00,
    PROOF_MERKLEBRANCH = 0x11,
    PROOF_NOTARYTXIDS = 0x12,
    PROOF_MERKLEBLOCK = 0x13
};

/*****
 * Holds the proof. This can be one of 3 types:
 *  - Merkle branch
 *  - Notary TXIDs
 *  - Merkle block
 */
class ImportProof {

private:
    uint8_t proofKind;
    TxProof proofBranch;
    std::vector<uint256> notaryTxids;
    std::vector<uint8_t> proofBlock;

public:
    /****
     * @brief Default ctor
     */
    ImportProof() { proofKind = PROOF_NONE; }
    /****
     * @brief Merkle branch proof ctor
     * @param _proofBranch the Merkle branch
     */
    ImportProof(const TxProof &_proofBranch) {
        proofKind = PROOF_MERKLEBRANCH; proofBranch = _proofBranch;
    }
    /****
     * @brief Notary TXID proof ctor
     * @param _notaryTxids the collection of txids
     */
    ImportProof(const std::vector<uint256> &_notaryTxids) {
        proofKind = PROOF_NOTARYTXIDS; notaryTxids = _notaryTxids;
    }
    /*****
     * @brief Merkle block proof ctor
     * @param _proofBlock the Merkle block
     */
    ImportProof(const std::vector<uint8_t> &_proofBlock) {
        proofKind = PROOF_MERKLEBLOCK; proofBlock = _proofBlock;
    }

    ADD_SERIALIZE_METHODS

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(proofKind);
        if (proofKind == PROOF_MERKLEBRANCH)
            READWRITE(proofBranch);
        else if (proofKind == PROOF_NOTARYTXIDS)
            READWRITE(notaryTxids);
        else if (proofKind == PROOF_MERKLEBLOCK)
            READWRITE(proofBlock);
        else
            proofKind = PROOF_NONE;  // if we have read some trash
    }

    /*****
     * @param _proofBranch the merkle branch to store
     * @returns true if this object is a Merkle branch ImportProof
     */
    bool IsMerkleBranch(TxProof &_proofBranch) const {
        if (proofKind == PROOF_MERKLEBRANCH) {
            _proofBranch = proofBranch;
            return true;
        }
        else
            return false;
    }

    /*****
     * @param _notaryTxids the txids to store
     * @returns true if this object is a Notary TXID ImportProof
     */
    bool IsNotaryTxids(std::vector<uint256> &_notaryTxids) const {
        if (proofKind == PROOF_NOTARYTXIDS) {
            _notaryTxids = notaryTxids;
            return true;
        }
        else
            return false;
    }

    /*****
     * @param _proofBlock the block to store
     * @returns true if this object is a Merkle Block ImportProof
     */
    bool IsMerkleBlock(std::vector<uint8_t> &_proofBlock) const {
        if (proofKind == PROOF_MERKLEBLOCK) {
            _proofBlock = proofBlock;
            return true;
        }
        else
            return false;
    }
};

/******
 * @brief get the value of the transaction
 * @param tx the transaction
 * @returns the burned value within tx
 */
CAmount GetCoinImportValue(const CTransaction &tx);

/******
 * @brief makes import tx for either coins or tokens
 * @param proof the proof
 * @param burnTx the inputs
 * @param payouts the outputs
 * @param nExpiryHeightOverride if an actual height (!= 0) makes a tx for validating int import tx
 * @returns the generated import transaction
 */
CTransaction MakeImportCoinTransaction(const ImportProof proof, const CTransaction burnTx, const std::vector<CTxOut> payouts, uint32_t nExpiryHeightOverride = 0);

/*****
 * @brief makes import tx for pegs cc
 * @param proof the proof
 * @param burnTx the inputs
 * @param payouts the outputs 
 * @param nExpiryHeighOverride if an actual height (!= 0) makes a tx for validating int import tx
 * @returns the transaction including spending markers for pegs CC
 */
CTransaction MakePegsImportCoinTransaction(const ImportProof proof, const CTransaction burnTx, const std::vector<CTxOut> payouts, uint32_t nExpiryHeightOverride = 0);

/******
 * @brief make a burn output
 * @param value the amount
 * @param targetCCid the ccid
 * @param targetSymbol
 * @param payouts the outputs
 * @param rawproof
 * @returns the txout
 */
CTxOut MakeBurnOutput(CAmount value, uint32_t targetCCid, const std::string& targetSymbol, 
        const std::vector<CTxOut> payouts, const std::vector<uint8_t> rawproof);

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
        std::vector<CPubKey> publishers,std::vector<uint256>txids,uint256 burntxid,int32_t height,
        int32_t burnvout,const std::string& rawburntx,CPubKey destpub, int64_t amount);

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
        const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof, const std::string& srcaddr,
        const std::string& receipt);

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
CTxOut MakeBurnOutput(CAmount value,uint32_t targetCCid,const std::string& targetSymbol,
        const std::vector<CTxOut> payouts,std::vector<uint8_t> rawproof,uint256 pegstxid,
        uint256 tokenid,CPubKey srcpub,int64_t amount,std::pair<int64_t,int64_t> account);

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
        uint256 &payoutsHash,std::vector<uint8_t> &rawproof);

/****
 * @brief break a serialized burn tx into its components
 * @param[in] burnTx the transaction
 * @param[out] srcaddr the source address
 * @param[out] receipt
 * @returns true on success
 */
bool UnmarshalBurnTx(const CTransaction burnTx, std::string &srcaddr, std::string &receipt);

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
bool UnmarshalBurnTx(const CTransaction burnTx,uint256 &bindtxid,std::vector<CPubKey> &publishers,
        std::vector<uint256> &txids,uint256& burntxid,int32_t &height,int32_t &burnvout,
        std::string &rawburntx,CPubKey &destpub, int64_t &amount);

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
        int64_t &amount,std::pair<int64_t,int64_t> &account);

/****
 * @brief break a serialized import tx into its components
 * @param[in] importTx the transaction
 * @param[out] proof the proof
 * @param[out] burnTx the burn transaction
 * @param[out] payouts the collection of tx outs
 * @returns true on success
 */
bool UnmarshalImportTx(const CTransaction importTx, ImportProof &proof, 
        CTransaction &burnTx,std::vector<CTxOut> &payouts);

/*****
 * @brief verify a coin import signature
 * @note CoinImport is different enough from normal script execution that it's not worth 
 * making all the mods neccesary in the interpreter to do the dispatch correctly.
 * @param[in] scriptSig the signature
 * @param[in] checker the checker to use
 * @param[out] state the error state
 * @returns true on success, `state` will contain the reason if false
 */
bool VerifyCoinImport(const CScript& scriptSig, TransactionSignatureChecker& checker, CValidationState &state);

/****
 * @brief add an import tombstone
 * @param importTx the transaction
 * @param inputs the inputs to be modified
 * @param nHeight the height
 */
void AddImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs, int nHeight);

/*****
 * @brief remove an import tombstone from inputs
 * @param importTx the transaction
 * @param inputs what to modify
 */
void RemoveImportTombstone(const CTransaction &importTx, CCoinsViewCache &inputs);

/*****
 * @brief See if a tombstone exists
 * @param importTx the transaction
 * @param inputs
 * @returns true if the transaction is a tombstone
 */
bool ExistsImportTombstone(const CTransaction &importTx, const CCoinsViewCache &inputs);
