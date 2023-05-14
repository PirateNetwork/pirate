// Copyright (c) 2016 The Bitcoin Core developers
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

#ifndef BITCOIN_WALLET_RPCWALLET_H
#define BITCOIN_WALLET_RPCWALLET_H

class CRPCTable;

void RegisterWalletRPCCommands(CRPCTable &tableRPC);
uint64_t komodo_interestsum();
int32_t ensure_CCrequirements(uint8_t evalcode);
/**
 * @brief Search for 10k sat. P2PK notary utxos and make proof tx (txNew) from it for further include in block.
 * opretIn should be empty script before december hardfork, and contains prepared opret script after.
 *
 * @param txNew - out: signed notary proof tx
 * @param notarypub33 - notary node compressed pubkey to search 10k sat. P2PK utxos in the wallet (wallet should be unlocked)
 * @param opretIn - after nDecemberHardforkHeight, prepared in advance opret script, before nDecemberHardforkHeight should be empty script
 * @param nLockTimeIn - nLockTime that will be set for notary proof tx in-case of after nDecemberHardforkHeight
 * @return int32_t - signature length of vin[0] in resulted notary proof tx, actually > 0 if txNew is correct, and 0 in-case of any error
 */
int32_t komodo_notaryvin(CMutableTransaction &txNew, uint8_t *notarypub33, const CScript &opretIn, uint32_t nLockTimeIn);

#endif //BITCOIN_WALLET_RPCWALLET_H
