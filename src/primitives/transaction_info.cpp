// Copyright (c) 2024 The Pirate developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

/**
 * Returns the most recent supported transaction version and version group id,
 * as of the specified activation height and active features.
 */

#include "primitives/transaction.h"
#include "consensus/upgrades.h"

TxVersionInfo CurrentTxVersionInfo(
    const Consensus::Params& consensus,
    int nHeight)
{
    if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_ORCHARD)) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = ORCHARD_VERSION_GROUP_ID,
            .nVersion =        ORCHARD_TX_VERSION
        };
    } else if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_SAPLING)) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = SAPLING_VERSION_GROUP_ID,
            .nVersion =        SAPLING_TX_VERSION
        };
    } else if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_OVERWINTER)) {
        return {
            .fOverwintered =   true,
            .nVersionGroupId = OVERWINTER_VERSION_GROUP_ID,
            .nVersion =        OVERWINTER_TX_VERSION
        };
    }

    return {
        .fOverwintered =   false,
        .nVersionGroupId = 0,
        .nVersion =        CTransaction::SPROUT_MIN_CURRENT_VERSION};

}
