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

int32_t komodo_checkvout(int32_t vout,int32_t k,int32_t indallvouts);

int32_t komodo_bannedset(int32_t *indallvoutsp,uint256 *array,int32_t max);

void komodo_passport_iteration();

int32_t komodo_check_deposit(int32_t height,const CBlock& block,uint32_t prevtime); // verify above block is valid pax pricing

const char *komodo_opreturn(int32_t height,uint64_t value,uint8_t *opretbuf,int32_t opretlen,uint256 txid,uint16_t vout,char *source);

void *OS_loadfile(char *fname,uint8_t **bufp,long *lenp,long *allocsizep);

uint8_t *OS_fileptr(long *allocsizep,const char *fname);

int32_t komodo_faststateinit(struct komodo_state *sp,const char *fname,char *symbol,char *dest);

extern std::vector<uint8_t> Mineropret; // opreturn data set by the data gathering code
#define PRICES_ERRORRATE (COIN / 100)	  // maximum acceptable change, set at 1%
#define PRICES_SIZEBIT0 (sizeof(uint32_t) * 4) // 4 uint32_t unixtimestamp, BTCUSD, BTCGBP and BTCEUR
#define KOMODO_LOCALPRICE_CACHESIZE 13
#define KOMODO_MAXPRICES 2048
#define PRICES_SMOOTHWIDTH 1

#define issue_curl(cmdstr) bitcoind_RPC(0,(char *)"CBCOINBASE",cmdstr,0,0,0)

char *nonportable_path(char *str);

char *portable_path(char *str);

void *loadfile(char *fname,uint8_t **bufp,long *lenp,long *allocsizep);

void *filestr(long *allocsizep,char *_fname);
