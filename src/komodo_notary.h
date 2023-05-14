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
#pragma once

#include "komodo_defs.h"
#include "komodo_cJSON.h"

#include "notaries_staked.h"
#include "komodo_hardfork.h"

#define KOMODO_MAINNET_START 178999
#define KOMODO_NOTARIES_HEIGHT1 814000
#define KOMODO_NOTARIES_HEIGHT2 2588672

#define CRYPTO777_PUBSECPSTR "020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9"

/****
 * @brief get the kmd season based on height (used on the KMD chain)
 * @param height the chain height
 * @returns the KMD season (returns 0 if the height is above the range)
 */
int32_t getkmdseason(int32_t height);

/****
 * @brief get the season based on timestamp (used for alternate chains)
 * @param timestamp the time
 * @returns the KMD season (returns 0 if timestamp is above the range)
 */
int32_t getacseason(uint32_t timestamp);

/****
 * Calculate the height index (how notaries are stored) based on the height
 * @param height the height
 * @returns the height index
 */
int32_t ht_index_from_height(int32_t height);

/***
 * @brief Given a height or timestamp, get the appropriate notary keys
 * @param[out] pubkeys the results
 * @param[in] height the height
 * @param[in] timestamp the timestamp
 * @returns the number of notaries
 */
int32_t komodo_notaries(uint8_t pubkeys[64][33],int32_t height,uint32_t timestamp);

int32_t komodo_electednotary(int32_t *numnotariesp,uint8_t *pubkey33,int32_t height,uint32_t timestamp);

int32_t komodo_ratify_threshold(int32_t height,uint64_t signedmask);

/*****
 * Push keys into the notary collection
 * @param origheight the height where these notaries begin
 * @param pubkeys the notaries' public keys
 * @param num the number of keys in pubkeys
 */
void komodo_notarysinit(int32_t origheight,uint8_t pubkeys[64][33],int32_t num);

int32_t komodo_chosennotary(int32_t *notaryidp,int32_t height,uint8_t *pubkey33,uint32_t timestamp);

/****
 * Search for the last (chronological) MoM notarized height
 * @returns the last notarized height that has a MoM
 */
int32_t komodo_prevMoMheight();

/******
 * @brief Get the last notarization information
 * @param[out] prevMoMheightp the MoM height
 * @param[out] hashp the notarized hash
 * @param[out] txidp the DESTTXID
 * @returns the notarized height
 */
int32_t komodo_notarized_height(int32_t *prevMoMheightp,uint256 *hashp,uint256 *txidp);

int32_t komodo_dpowconfs(int32_t txheight,int32_t numconfs);

int32_t komodo_MoMdata(int32_t *notarized_htp,uint256 *MoMp,uint256 *kmdtxidp,int32_t height,uint256 *MoMoMp,int32_t *MoMoMoffsetp,int32_t *MoMoMdepthp,int32_t *kmdstartip,int32_t *kmdendip);

/****
 * Get the notarization data below a particular height
 * @param[in] nHeight the height desired
 * @param[out] notarized_hashp the hash of the notarized block
 * @param[out] notarized_desttxidp the desttxid
 * @returns the notarized height
 */
int32_t komodo_notarizeddata(int32_t nHeight,uint256 *notarized_hashp,uint256 *notarized_desttxidp);

/***
 * Add a notarized checkpoint to the komodo_state
 * @param[in] sp the komodo_state to add to
 * @param[in] nHeight the height
 * @param[in] notarized_height the height of the notarization
 * @param[in] notarized_hash the hash of the notarization
 * @param[in] notarized_desttxid the txid of the notarization on the destination chain
 * @param[in] MoM the MoM
 * @param[in] MoMdepth the depth
 */
void komodo_notarized_update(struct komodo_state *sp,int32_t nHeight,int32_t notarized_height,
        uint256 notarized_hash,uint256 notarized_desttxid,uint256 MoM,int32_t MoMdepth);

/****
 * @brief Initialize genesis notaries into memory
 * @note After a successful run, subsequent calls do nothing
 * @param height the current height (not used other than to stop initialization if less than zero)
 */
void komodo_init(int32_t height);


int32_t komodo_notaries(uint8_t pubkeys[64][33],int32_t height,uint32_t timestamp);
void komodo_notarysinit(int32_t origheight,uint8_t pubkeys[64][33],int32_t num);
void komodo_notaries_uninit(); // gets rid of values stored in statics
void komodo_statefile_uninit(); // closes statefile

extern struct knotaries_entry *Pubkeys;
#define KOMODO_STATES_NUMBER 2
extern struct komodo_state KOMODO_STATES[];