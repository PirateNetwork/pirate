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

#include "arith_uint256.h"
#define ASSETCHAINS_MAX_ERAS 7 // needed by chain.h
#include "chain.h"
#include "assetchain.h"
#include "komodo_nk.h"

#define NUM_KMD_NOTARIES 64

#define KOMODO_EARLYTXID_HEIGHT 100
#define ASSETCHAINS_MINHEIGHT 128
#define KOMODO_ELECTION_GAP 2000
#define KOMODO_ASSETCHAIN_MAXLEN 65
#define KOMODO_LIMITED_NETWORKSIZE 4
#define IGUANA_MAXSCRIPTSIZE 10001
#define KOMODO_MAXMEMPOOLTIME 3600 // affects consensus, 3600 secs = 1hr
#define CRYPTO777_PUBSECPSTR "020e46e79a2a8d12b9b5d12c7a91adb4e454edfae43c0a0cb805427d2ac7613fd9"
#define KOMODO_FIRSTFUNGIBLEID 100
#define KOMODO_SAPLING_ACTIVATION 1544832000 // Dec 15th, 2018
#define KOMODO_SAPLING_DEADLINE 1550188800 // Feb 15th, 2019
#define ASSETCHAINS_STAKED_BLOCK_FUTURE_MAX 57
#define ASSETCHAINS_STAKED_BLOCK_FUTURE_HALF 27
#define ASSETCHAINS_STAKED_MIN_POW_DIFF 536900000 // 537000000 537300000
#define _ASSETCHAINS_TIMELOCKOFF 0xffffffffffffffff

#define SETBIT(bits,bitoffset) (((uint8_t *)bits)[(bitoffset) >> 3] |= (1 << ((bitoffset) & 7)))
#define GETBIT(bits,bitoffset) (((uint8_t *)bits)[(bitoffset) >> 3] & (1 << ((bitoffset) & 7)))
#define CLEARBIT(bits,bitoffset) (((uint8_t *)bits)[(bitoffset) >> 3] &= ~(1 << ((bitoffset) & 7)))

#define KOMODO_MAXNVALUE (((uint64_t)1 << 63) - 1)
#define KOMODO_BIT63SET(x) ((x) & ((uint64_t)1 << 63))
#define KOMODO_VALUETOOBIG(x) ((x) > (uint64_t)10000000001*COIN)


#define SATOSHIDEN ((uint64_t)100000000L)
#define dstr(x) ((double)(x) / SATOSHIDEN)
#define SMALLVAL 0.000000000000001

//#define PRICES_DAYWINDOW ((3600*24/ASSETCHAINS_BLOCKTIME) + 1)

int32_t MAX_BLOCK_SIZE(int32_t height);

extern int32_t ASSETCHAINS_BLOCKTIME;
extern uint32_t ASSETCHAINS_ALGO;
extern int32_t KOMODO_LONGESTCHAIN,USE_EXTERNAL_PUBKEY;
extern uint64_t ASSETCHAINS_COMMISSION;
extern uint64_t ASSETCHAINS_NONCEMASK[],ASSETCHAINS_NK[2];
extern const char *ASSETCHAINS_ALGORITHMS[];
extern uint32_t ASSETCHAINS_NONCESHIFT[];

extern std::string CCerror;

extern bool IS_KOMODO_TESTNODE;
extern int32_t KOMODO_SNAPSHOT_INTERVAL;
extern int32_t ASSETCHAINS_EARLYTXIDCONTRACT;
extern int32_t ASSETCHAINS_STAKED_SPLIT_PERCENTAGE;
extern std::map <std::int8_t, int32_t> mapHeightEvalActivate;

uint256 Parseuint256(const char *hexstr); // defined in cc/CCutilbits.cpp
void komodo_netevent(std::vector<uint8_t> payload);
/* TODO: remove
extern std::vector<std::string> vWhiteListAddress;
extern std::map <std::int8_t, int32_t> mapHeightEvalActivate;
int32_t getacseason(uint32_t timestamp);
int32_t getkmdseason(int32_t height);

#define IGUANA_MAXSCRIPTSIZE 10001
*/
#define KOMODO_KVDURATION 1440
#define KOMODO_KVPROTECTED 1
/*#define PRICES_MAXDATAPOINTS 8
int32_t komodo_notaries(uint8_t pubkeys[64][33],int32_t height,uint32_t timestamp);
char *bitcoin_address(char *coinaddr,uint8_t addrtype,uint8_t *pubkey_or_rmd160,int32_t len);
int32_t komodo_minerids(uint8_t *minerids,int32_t height,int32_t width);
int32_t komodo_kvsearch(uint256 *refpubkeyp,int32_t current_height,uint32_t *flagsp,int32_t *heightp,uint8_t value[IGUANA_MAXSCRIPTSIZE],uint8_t *key,int32_t keylen);

uint32_t komodo_blocktime(uint256 hash);
int32_t komodo_dpowconfs(int32_t height,int32_t numconfs);
int8_t komodo_segid(int32_t nocache,int32_t height);
int32_t komodo_nextheight();
int32_t komodo_notarized_bracket(struct notarized_checkpoint *nps[2],int32_t height);
arith_uint256 komodo_adaptivepow_target(int32_t height,arith_uint256 bnTarget,uint32_t nTime);
bool komodo_hardfork_active(uint32_t time);
int32_t komodo_newStakerActive(int32_t height, uint32_t timestamp);

uint256 Parseuint256(const char *hexstr);
void komodo_sendmessage(int32_t minpeers, int32_t maxpeers, const char *message, std::vector<uint8_t> payload);
CBlockIndex *komodo_getblockindex(uint256 hash);
int32_t komodo_nextheight();
CBlockIndex *komodo_blockindex(uint256 hash);
CBlockIndex *komodo_chainactive(int32_t height);
int32_t komodo_blockheight(uint256 hash);
bool komodo_txnotarizedconfirmed(uint256 txid);
int32_t komodo_blockload(CBlock& block, CBlockIndex *pindex);
uint32_t komodo_chainactive_timestamp();
uint32_t GetLatestTimestamp(int32_t height);
*/
#ifndef KOMODO_NSPV_FULLNODE
#define KOMODO_NSPV_FULLNODE (KOMODO_NSPV <= 0)
#endif // !KOMODO_NSPV_FULLNODE
#ifndef KOMODO_NSPV_SUPERLITE
#define KOMODO_NSPV_SUPERLITE (KOMODO_NSPV > 0)
#endif // !KOMODO_NSPV_SUPERLITE

