#pragma once
/******************************************************************************
 * Copyright © 2021 Komodo Core Developers.                                   *
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

/**
 * @brief Converts Sprout JoinSplit transaction data to JSON format
 * @param tx Transaction containing JoinSplit data
 * @return UniValue JSON array of JoinSplit objects with nullifiers, commitments, and proofs
 */
UniValue TxJoinSplitToJSON(const CTransaction& tx);

/**
 * @brief Converts transaction data to comprehensive JSON representation
 * @param tx Transaction to serialize
 * @param hashBlock Block hash containing the transaction (can be null)
 * @param entry Output JSON object to populate
 * @param isTxBuilder Whether this is for transaction builder output
 * @param nHeight Block height (0 if not in block)
 * @param nConfirmations Number of confirmations (0 if not in block)
 * @param nBlockTime Block timestamp (0 if not in block)
 * @param includeHex Whether to include raw hex transaction data
 */
void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry, bool isTxBuilder = false, int nHeight = 0, int nConfirmations = 0, int nBlockTime = 0, bool includeHex = true);

/**
 * @brief Converts script public key to JSON representation
 * @param scriptPubKey Script to analyze and convert
 * @param out Output JSON object to populate with script details
 * @param fIncludeHex Whether to include hex-encoded script data
 */
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
