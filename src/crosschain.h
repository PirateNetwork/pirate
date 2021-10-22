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
/*****
 * transfer or migrate assets from one chain to another
 */
#include "cc/eval.h"

enum CrosschainType {
    CROSSCHAIN_KOMODO = 1,
    CROSSCHAIN_TXSCL = 2,
    CROSSCHAIN_STAKED = 3
};

typedef struct CrosschainAuthority {
    uint8_t notaries[64][33];
    int8_t size;
    int8_t requiredSigs;
} CrosschainAuthority;

class CrossChain
{
public:
    /****
     * Determine the type of crosschain
     * @param symbol the asset chain to check
     * @returns the type of chain
     */
    static CrosschainType GetSymbolAuthority(const std::string& symbol);

    /***
     * @param tx the transaction to check
     * @param auth the authority object
     * @returns true on success
     */
    static bool CheckTxAuthority(const CTransaction &tx, CrosschainAuthority auth);

    /*****
     * @brief get the proof
     * @note On assetchain
     * @param hash
     * @param burnTx
     * @returns a pair containing the notarisation tx hash and the merkle branch
     */
    static TxProof GetAssetchainProof(uint256 hash,CTransaction burnTx);

    /*****
     * @brief Calculate the proof root
     * @note this happens on the KMD chain
     * @param symbol the chain symbol
     * @param targetCCid
     * @param kmdHeight
     * @param moms collection of MoMs
     * @param destNotarisationTxid
     * @returns the proof root, or 0 on error
     */
    static uint256 CalculateProofRoot(const char* symbol, uint32_t targetCCid, int kmdHeight,
            std::vector<uint256> &moms, uint256 &destNotarisationTxid);

    /*****
     * @brief Takes an importTx that has proof leading to assetchain root and extends proof to cross chain root
     * @param importTx
     * @param offset
     */
    static void CompleteImportTransaction(CTransaction &importTx,int32_t offset);

    /****
     * @brief check the MoMoM
     * @note on Assetchain
     * @param kmdNotarisationHash the hash
     * @param momom what to check
     * @returns true on success
     */
    static bool CheckMoMoM(uint256 kmdNotarisationHash, uint256 momom);

    /*****
    * @brief Check notaries approvals for the txoutproofs of burn tx
    * @note alternate check if MoMoM check has failed
    * @param burntxid - txid of burn tx on the source chain
    * @param notaryTxids txids of notaries' proofs
    * @returns true on success
    */
    static bool CheckNotariesApproval(uint256 burntxid, const std::vector<uint256> & notaryTxids);

private:
    /******
     * @brief
     * @note this happens on the KMD chain
     * @param txid
     * @param targetSymbol
     * @param targetCCid
     * @param assetChainProof
     * @param offset
     * @returns a pair of target chain notarisation txid and the merkle branch
     */
    static TxProof GetCrossChainProof(const uint256 txid, const char* targetSymbol, uint32_t targetCCid,
            const TxProof assetChainProof,int32_t offset);
};