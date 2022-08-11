#pragma once
/******************************************************************************
 * Copyright © 2021 Komodo Core developers                                    *
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

#include "primitives/transaction.h" // CTransaction
#include "script/standard.h" // CTxDestination
#include "importcoin.h" // ImportProof
#include <cstdint>

/****
 * @brief make import tx with burntx and dual daemon
 * @param txfee fee
 * @param receipt
 * @param srcaddr source address
 * @param vouts collection of vouts
 * @returns the hex string of the import transaction
 */
std::string MakeCodaImportTx(uint64_t txfee, const std::string& receipt, 
        const std::string& srcaddr, const std::vector<CTxOut>& vouts);

/******
 * @brief make sure vin is signed by a particular key
 * @param sourcetx the source transaction
 * @param i the index of the input to check
 * @param pubkey33 the key
 * @returns true if the vin of i was signed by the given key
 */
bool CheckVinPubKey(const CTransaction &sourcetx, int32_t i, uint8_t pubkey33[33]);

/*****
 * @brief makes source tx for self import tx
 * @param dest the tx destination
 * @param amount the amount
 * @returns a transaction based on the inputs
 */
CMutableTransaction MakeSelfImportSourceTx(const CTxDestination &dest, int64_t amount);

/*****
 * @brief generate a self import proof
 * @note this prepares a tx for creating an import tx and quasi-burn tx
 * @note find burnTx with hash from "other" daemon
 * @param[in] sourceMtx the original transaction
 * @param[out] templateMtx the resultant transaction
 * @param[out] proofNull the import proof
 * @returns true on success
 */
bool GetSelfimportProof(const CMutableTransaction sourceMtx, CMutableTransaction &templateMtx, ImportProof &proofNull);
