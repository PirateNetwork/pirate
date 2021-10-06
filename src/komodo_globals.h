#pragma once
/******************************************************************************
 * Copyright © 2021 Komodo Core Developers                                    *
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
/****
 * This file provides extern access to variables in komodo_globals.h
 * Please think twice before adding to this list. Can it be done with a better scope?
 */
#include "komodo_structs.h"
#include <mutex>
#include <cstdint>

extern char KMDUSERPASS[8192+512+1];
extern char BTCUSERPASS[8192]; 
extern char ASSETCHAINS_USERPASS[4096];
extern char CURRENCIES[][8];
extern int COINBASE_MATURITY; // see consensus.h
extern uint8_t NOTARY_PUBKEY33[33];
extern uint8_t ASSETCHAINS_OVERRIDE_PUBKEYHASH[20];
extern uint16_t KMD_PORT;
extern uint16_t BITCOIND_RPCPORT;
extern uint16_t DEST_PORT;
extern uint16_t ASSETCHAINS_BEAMPORT;
extern uint16_t ASSETCHAINS_CODAPORT;
extern int32_t KOMODO_INSYNC;
extern int32_t KOMODO_LASTMINED;
extern int32_t prevKOMODO_LASTMINED;
extern int32_t JUMBLR_PAUSE;
extern int32_t NUM_PRICES; 
extern int32_t USE_EXTERNAL_PUBKEY;
extern int32_t KOMODO_EXTERNAL_NOTARIES;
extern int32_t KOMODO_PASSPORT_INITDONE;
extern int32_t KOMODO_EXTERNAL_NOTARIES;
extern int32_t KOMODO_PAX;
extern int32_t KOMODO_REWIND;
extern int32_t KOMODO_EXTRASATOSHI;
extern int32_t ASSETCHAINS_FOUNDERS;
extern int32_t ASSETCHAINS_CBMATURITY;
extern int32_t KOMODO_LOADINGBLOCKS; // defined in pow.cpp, boolean, 1 if currently loading the block index, 0 if not
extern uint32_t *PVALS;
extern uint32_t ASSETCHAINS_CC;
extern uint32_t KOMODO_STOPAT;
extern uint32_t KOMODO_DPOWCONFS;
extern uint32_t STAKING_MIN_DIFF;
extern uint32_t ASSETCHAINS_NUMALGOS;
extern uint32_t ASSETCHAINS_MINDIFF[3];
extern uint64_t PENDING_KOMODO_TX;
extern uint64_t ASSETCHAINS_TIMELOCKGTE;
extern uint64_t ASSETCHAINS_ENDSUBSIDY[ASSETCHAINS_MAX_ERAS+1];
extern uint64_t ASSETCHAINS_HALVING[ASSETCHAINS_MAX_ERAS+1];
extern uint64_t ASSETCHAINS_DECAY[ASSETCHAINS_MAX_ERAS+1];
extern uint64_t ASSETCHAINS_PEGSCCPARAMS[3];
extern uint64_t ASSETCHAINS_TIMEUNLOCKFROM;
extern uint64_t ASSETCHAINS_TIMEUNLOCKTO;
extern uint64_t KOMODO_INTERESTSUM;
extern uint64_t KOMODO_WALLETBALANCE;
extern int64_t ASSETCHAINS_GENESISTXVAL;
extern int64_t MAX_MONEY;
extern std::mutex komodo_mutex;
extern std::vector<uint8_t> Mineropret;
extern pthread_mutex_t KOMODO_KV_mutex;
extern pthread_mutex_t KOMODO_CC_mutex;
extern komodo_kv *KOMODO_KV;
extern pax_transaction *PAX;
extern knotaries_entry *Pubkeys;
extern komodo_state KOMODO_STATES[34];
extern CScript KOMODO_EARLYTXID_SCRIPTPUB;
