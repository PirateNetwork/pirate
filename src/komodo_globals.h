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
 * This file provides extern access to variables mostly defined in komodo_globals.cpp
 * Please think twice before adding to this list. Can it be done with a better scope?
 * @note more global externs are in komodo_defs.h
 */
#include "komodo_structs.h"
#include <mutex>
#include <cstdint>

extern char KMDUSERPASS[8192+512+1];
extern char BTCUSERPASS[8192]; 
extern char ASSETCHAINS_USERPASS[4096];
extern char CURRENCIES[][8];
extern int COINBASE_MATURITY; // see consensus.h
extern uint8_t ASSETCHAINS_OVERRIDE_PUBKEYHASH[20]; // a hash of the key passed in -ac_pubkey
extern uint16_t KMD_PORT; // network port
extern uint16_t BITCOIND_RPCPORT; // network port
extern uint16_t DEST_PORT; // network port
extern uint16_t ASSETCHAINS_BEAMPORT; // network port
extern uint16_t ASSETCHAINS_CODAPORT; // network port
extern int32_t KOMODO_INSYNC; // current height, remains 0 until sync is complete
extern int32_t KOMODO_LASTMINED; // height
extern int32_t prevKOMODO_LASTMINED; // height
extern int32_t JUMBLR_PAUSE; // skips jumblr iteration if != 0
extern int32_t NUM_PRICES; // used for PAX
extern int32_t USE_EXTERNAL_PUBKEY; // 0/1 (T/F)
extern int32_t KOMODO_EXTERNAL_NOTARIES; // 0/1 (T/F)
extern int32_t KOMODO_PAX; // 0/1 (T/F)
extern int32_t KOMODO_REWIND; // can be set via --rewind, but normally 0/1 (T/F)
extern int32_t KOMODO_EXTRASATOSHI; // set to 1 for certain coins, helps in block reward calc
extern int32_t ASSETCHAINS_FOUNDERS; // can be set by -ac_founders, normally 0/1 but can be more
extern int32_t ASSETCHAINS_CBMATURITY; // coinbase maturity, can be set by -ac_cbmaturity
extern int32_t KOMODO_LOADINGBLOCKS; // defined in pow.cpp, boolean, 1 if currently loading the block index, 0 if not
extern uint32_t *PVALS; // for PAX
extern uint32_t ASSETCHAINS_CC; // set by -ac_cc, normally 0/1
extern uint32_t KOMODO_STOPAT; // set by -stopat, will not add more blocks after specified height
extern uint32_t KOMODO_DPOWCONFS; // set by -dpowconfs, normally 0/1
extern uint32_t STAKING_MIN_DIFF; // selected entry from ASSETCHAINS_MINDIFF
extern uint32_t ASSETCHAINS_NUMALGOS; // number of supported hash algos
extern uint32_t ASSETCHAINS_MINDIFF[3]; // hash algo dependent
extern uint64_t ASSETCHAINS_TIMELOCKGTE; // set by -ac_timelockgte or consensus
extern uint64_t ASSETCHAINS_ENDSUBSIDY[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_end, array of heights indexed by era
extern uint64_t ASSETCHAINS_HALVING[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_halving
extern uint64_t ASSETCHAINS_DECAY[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_decay
extern uint64_t ASSETCHAINS_PEGSCCPARAMS[3]; // set by -ac_pegsccparams, used in pegs.cpp
extern uint64_t ASSETCHAINS_TIMEUNLOCKFROM; // set by -ac_timeunlockfrom
extern uint64_t ASSETCHAINS_TIMEUNLOCKTO; // set by -ac_timeunlockto
extern uint64_t KOMODO_INTERESTSUM; // calculated value, returned in getinfo() RPC call
extern uint64_t KOMODO_WALLETBALANCE; // pwalletmain->GetBalance(), returned in getinfo() RPC call
extern int64_t ASSETCHAINS_GENESISTXVAL; // used in calculating money supply
extern int64_t MAX_MONEY; // consensus related sanity check. Not max supply.
extern std::mutex komodo_mutex; // seems to protect PAX values and Pubkey array
extern std::vector<uint8_t> Mineropret; // previous miner values
extern pthread_mutex_t KOMODO_CC_mutex; // mutex to help with CryptoConditions
extern komodo_kv *KOMODO_KV; // the global kv struct
extern pax_transaction *PAX; // the global pax struct see komodo_gateway.cpp
extern knotaries_entry *Pubkeys; // notary pubkeys
extern komodo_state KOMODO_STATES[34]; // array of chain states for different chains
extern CScript KOMODO_EARLYTXID_SCRIPTPUB; // used mainly in cc/prices.cpp
extern assetchain chain;
