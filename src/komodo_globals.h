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
#pragma once
#include <mutex>
#include "komodo_defs.h"
//#include "komodo_hardfork.h"
#include "komodo_structs.h"

#define KOMODO_ELECTION_GAP 2000    //((ASSETCHAINS_SYMBOL[0] == 0) ? 2000 : 100)
#define KOMODO_ASSETCHAIN_MAXLEN 65

extern char CURRENCIES[][8];

//extern int COINBASE_MATURITY;
extern unsigned int WITNESS_CACHE_SIZE;

extern uint256 KOMODO_EARLYTXID;
extern bool IS_KOMODO_NOTARY;

extern bool IS_MODE_EXCHANGEWALLET;
extern bool IS_KOMODO_DEALERNODE;

extern int32_t KOMODO_MININGTHREADS;
extern int32_t STAKED_NOTARY_ID;
extern int32_t USE_EXTERNAL_PUBKEY;
extern int32_t KOMODO_REWIND;
extern int32_t STAKED_ERA;
extern int32_t KOMODO_CONNECTING;
extern int32_t KOMODO_EXTRASATOSHI;
extern int32_t ASSETCHAINS_FOUNDERS;
extern int32_t KOMODO_NSPV;
extern int32_t KOMODO_INSYNC;
extern int32_t KOMODO_LASTMINED;
extern int32_t prevKOMODO_LASTMINED;
extern int32_t KOMODO_CCACTIVATE;
extern int32_t JUMBLR_PAUSE;
extern std::string NOTARY_PUBKEY;
extern std::string ASSETCHAINS_OVERRIDE_PUBKEY;
extern std::string DONATION_PUBKEY;
extern std::string ASSETCHAINS_SCRIPTPUB;
extern std::string NOTARY_ADDRESS;
extern std::string ASSETCHAINS_SELFIMPORT;
extern std::string ASSETCHAINS_CCLIB;
extern uint8_t NOTARY_PUBKEY33[33];
extern uint8_t ASSETCHAINS_OVERRIDE_PUBKEY33[33];
extern uint8_t ASSETCHAINS_OVERRIDE_PUBKEYHASH[20];
extern uint8_t ASSETCHAINS_PUBLIC;
extern uint8_t ASSETCHAINS_PRIVATE;
extern uint8_t ASSETCHAINS_TXPOW;
extern int8_t ASSETCHAINS_ADAPTIVEPOW;
extern char ASSETCHAINS_USERPASS[4096];
extern uint16_t ASSETCHAINS_P2PPORT;
extern uint16_t ASSETCHAINS_RPCPORT;
extern uint16_t ASSETCHAINS_BEAMPORT;
extern uint16_t ASSETCHAINS_CODAPORT;
extern uint32_t ASSETCHAIN_INIT;
extern uint32_t ASSETCHAINS_MAGIC;
extern uint64_t ASSETCHAINS_CBOPRET;
extern uint64_t ASSETCHAINS_LASTERA;
extern uint64_t ASSETCHAINS_REWARD[ASSETCHAINS_MAX_ERAS+1];
extern uint64_t ASSETCHAINS_NOTARY_PAY[ASSETCHAINS_MAX_ERAS+1];
extern uint8_t ASSETCHAINS_CCDISABLES[256];
extern std::vector<std::string> ASSETCHAINS_PRICES;
extern std::vector<std::string> ASSETCHAINS_STOCKS;

extern uint32_t ASSETCHAINS_EQUIHASH;
extern uint32_t ASSETCHAINS_ALGO;
extern int32_t ASSETCHAINS_SAPLING;
extern int32_t ASSETCHAINS_OVERWINTER;
extern int32_t ASSETCHAINS_STAKED;
extern uint64_t ASSETCHAINS_COMMISSION;
extern uint64_t ASSETCHAINS_SUPPLY;
extern uint64_t ASSETCHAINS_FOUNDERS_REWARD;
extern uint32_t KOMODO_INITDONE;
extern char KMDUSERPASS[8192+512+1];
extern char BTCUSERPASS[8192];
extern uint16_t KMD_PORT;
extern uint16_t BITCOIND_RPCPORT;
extern uint16_t DEST_PORT;

extern uint32_t ASSETCHAINS_CC; // set by -ac_cc, normally 0/1
extern uint32_t KOMODO_STOPAT; // set by -stopat, will not add more blocks after specified height
extern uint32_t KOMODO_DPOWCONFS; // set by -dpowconfs, normally 0/1
extern uint32_t STAKING_MIN_DIFF; // selected entry from ASSETCHAINS_MINDIFF
extern uint32_t ASSETCHAINS_NUMALGOS; // number of supported hash algos
extern uint32_t ASSETCHAINS_MINDIFF[]; // hash algo dependent
extern uint64_t ASSETCHAINS_TIMELOCKGTE; // set by -ac_timelockgte or consensus
extern uint64_t ASSETCHAINS_ENDSUBSIDY[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_end, array of heights indexed by era
extern uint64_t ASSETCHAINS_HALVING[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_halving
extern uint64_t ASSETCHAINS_DECAY[ASSETCHAINS_MAX_ERAS+1]; // can be set by -ac_decay
extern uint64_t ASSETCHAINS_PEGSCCPARAMS[3]; // set by -ac_pegsccparams, used in pegs.cpp
extern uint64_t KOMODO_INTERESTSUM; // calculated value, returned in getinfo() RPC call
extern uint64_t KOMODO_WALLETBALANCE; // pwalletmain->GetBalance(), returned in getinfo() RPC call
extern int64_t ASSETCHAINS_GENESISTXVAL; // used in calculating money supply
extern int64_t MAX_MONEY; // consensus related sanity check. Not max supply.
extern std::mutex komodo_mutex; // seems to protect PAX values and Pubkey array
//extern std::vector<uint8_t> Mineropret; // previous miner values
extern pthread_mutex_t KOMODO_CC_mutex; // mutex to help with CryptoConditions
extern CScript KOMODO_EARLYTXID_SCRIPTPUB; // used mainly in cc/prices.cpp


#define KOMODO_ELECTION_GAP 2000    //((ASSETCHAINS_SYMBOL[0] == 0) ? 2000 : 100)
#define KOMODO_ASSETCHAIN_MAXLEN 65

#define _COINBASE_MATURITY 100  // defauly maturity

#define _ASSETCHAINS_TIMELOCKOFF 0xffffffffffffffff

#define _ASSETCHAINS_EQUIHASH 0
