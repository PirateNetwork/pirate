// Copyright (c) 2018 The Zcash developers
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

/**
 * General information about each network upgrade.
 * Ordered by Consensus::UpgradeIndex.
 */
const struct NUInfo NetworkUpgradeInfo[Consensus::MAX_NETWORK_UPGRADES] = {
    {
        /*.nBranchId =*/ 0,
        /*.strName =*/ "Sprout",
        /*.strInfo =*/ "The Pirate network at launch",
    },
    {
        /*.nBranchId =*/ 0x74736554,
        /*.strName =*/ "Test dummy",
        /*.strInfo =*/ "Test dummy info",
    },
    {
        /*.nBranchId =*/ 0x5ba81b19,
        /*.strName =*/ "Overwinter",
        /*.strInfo =*/ "Activate Overwinter.",
    },
    {
        /*.nBranchId =*/ 0x76b809bb,
        /*.strName =*/ "Sapling",
        /*.strInfo =*/ "Activate Sapling.",
    },
    {
        /*.nBranchId =*/ 0xc2d6d0b4,
        /*.strName =*/ "Orchard",
        /*.strInfo =*/ "Activate Orchard.",
    }
};

const uint32_t SPROUT_BRANCH_ID = NetworkUpgradeInfo[Consensus::BASE_SPROUT].nBranchId;

UpgradeState NetworkUpgradeState(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx)
{
    if (nHeight < 0)
    {
        printf("height: %d", nHeight);
    }
    assert(nHeight >= 0);
    assert(idx >= Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);
    auto nActivationHeight = params.vUpgrades[idx].nActivationHeight;

    if (nActivationHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT) {
        return UPGRADE_DISABLED;
    } else if (nHeight >= nActivationHeight) {
        // From ZIP 200:
        //
        // ACTIVATION_HEIGHT
        //     The non-zero block height at which the network upgrade rules will come
        //     into effect, and be enforced as part of the blockchain consensus.
        //
        //     For removal of ambiguity, the block at height ACTIVATION_HEIGHT - 1 is
        //     subject to the pre-upgrade consensus rules, and would be the last common
        //     block in the event of a persistent pre-upgrade branch.
        return UPGRADE_ACTIVE;
    } else {
        return UPGRADE_PENDING;
    }
}

bool NetworkUpgradeActive(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx)
{
    return NetworkUpgradeState(nHeight, params, idx) == UPGRADE_ACTIVE;
}

int CurrentEpoch(int nHeight, const Consensus::Params& params) {
    //Always return Sprout if height is less than or equal to 0 (Genisis block)
    if (nHeight <= 0) {
        return Consensus::BASE_SPROUT;
    }

    for (auto idxInt = Consensus::MAX_NETWORK_UPGRADES - 1; idxInt >= Consensus::BASE_SPROUT; idxInt--) {
        if (NetworkUpgradeActive(nHeight, params, Consensus::UpgradeIndex(idxInt))) {
            return idxInt;
        }
    }
    // Base case
    return Consensus::BASE_SPROUT;
}

#define NSPV_BRANCHID 0x76b809bb
extern int32_t KOMODO_NSPV;
#ifndef KOMODO_NSPV_SUPERLITE
#define KOMODO_NSPV_SUPERLITE (KOMODO_NSPV > 0)
#endif

uint32_t CurrentEpochBranchId(int nHeight, const Consensus::Params& params)
{
    if ( KOMODO_NSPV_SUPERLITE )
        return(NSPV_BRANCHID);
    return NetworkUpgradeInfo[CurrentEpoch(nHeight, params)].nBranchId;
}

bool IsConsensusBranchId(int branchId) {
    for (int idx = Consensus::BASE_SPROUT; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
        if (branchId == NetworkUpgradeInfo[idx].nBranchId) {
            return true;
        }
    }
    return false;
}

bool IsActivationHeight(
    int nHeight,
    const Consensus::Params& params,
    Consensus::UpgradeIndex idx)
{
    assert(idx >= Consensus::BASE_SPROUT && idx < Consensus::MAX_NETWORK_UPGRADES);

    // Don't count Sprout as an activation height
    if (idx == Consensus::BASE_SPROUT) {
        return false;
    }

    return nHeight >= 0 && nHeight == params.vUpgrades[idx].nActivationHeight;
}

bool IsActivationHeightForAnyUpgrade(
    int nHeight,
    const Consensus::Params& params)
{
    if (nHeight < 0) {
        return false;
    }

    // Don't count Sprout as an activation height
    for (int idx = Consensus::BASE_SPROUT + 1; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
        if (nHeight == params.vUpgrades[idx].nActivationHeight)
            return true;
    }

    return false;
}

std::optional<int> NextEpoch(int nHeight, const Consensus::Params& params) {
    if (nHeight < 0) {
        return std::nullopt;
    }

    // Sprout is never pending
    for (auto idx = Consensus::BASE_SPROUT + 1; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
        if (NetworkUpgradeState(nHeight, params, Consensus::UpgradeIndex(idx)) == UPGRADE_PENDING) {
            return idx;
        }
    }

    return std::nullopt;
}

std::optional<int> NextActivationHeight(
    int nHeight,
    const Consensus::Params& params)
{
    auto idx = NextEpoch(nHeight, params);
    if (idx) {
        return params.vUpgrades[idx.value()].nActivationHeight;
    }
    return std::nullopt;
}
