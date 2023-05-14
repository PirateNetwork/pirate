// Copyright (c) 2009-2014 The Bitcoin Core developers
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

#ifndef BITCOIN_CHECKPOINTS_H
#define BITCOIN_CHECKPOINTS_H

#include "uint256.h"
#include "chainparams.h"

#include <map>

class CBlockIndex;
struct CCheckpointData;

/**
 * Block-chain checkpoints are compiled-in sanity checks.
 * They are updated every release or three.
 */
namespace Checkpoints
{
    /******
     * @param data the collection of checkpoints
     * @param nHeight the height
     * @param hash the expected hash at nHight
     * @returns true if the checkpoint at nHeight is not found or hash matches the found checkpoint
     */
    bool CheckBlock(const CChainParams::CCheckpointData& data, int nHeight, const uint256& hash);

    /*****
     * @brief Return conservative estimate of total number of blocks, 0 if unknown
     * @param data the collection of checkpoints
     * @returns the total number of blocks
     */
    int GetTotalBlocksEstimate(const CChainParams::CCheckpointData& data);

    /******
     * @param data the collection of checkpoints
     * @returns last CBlockIndex* in mapBlockIndex that is a checkpoint (can be nullptr)
     */
    CBlockIndex* GetLastCheckpoint(const CChainParams::CCheckpointData& data);

    /******
     * @brief Guess how far we are in the verification process at the given block index
     * @param data the checkpoint collection
     * @param pindex the block index
     * @param fsigchecks true to include signature checks in the calculation
     * @returns
     */
    double GuessVerificationProgress(const CChainParams::CCheckpointData& data, CBlockIndex* pindex, bool fSigchecks = true);

} // namespace Checkpoints

#endif // BITCOIN_CHECKPOINTS_H
