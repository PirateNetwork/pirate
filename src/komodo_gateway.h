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

/****
 * @brief Check if the n of the vout matches one that is banned
 * @param vout the "n" of the vout
 * @param k the index in the array of banned txids
 * @param indallvouts the index at which all "n"s are banned
 * @returns true if vout is banned
 */
bool komodo_checkvout(int32_t vout,int32_t k,int32_t indallvouts);

/****
 * @brief retrieve list of banned txids
 * @param[out] indallvoutsp size of array - 2
 * @param[out] array of txids
 * @param[in] max the max size of the array
 * @returns the number of txids placed into the array
 */
int32_t komodo_bannedset(int32_t *indallvoutsp,uint256 *array,int32_t max);

/***
 * @brief update wallet balance / interest
 * @note called only on KMD chain every 10 seconds ( see ThreadUpdateKomodoInternals() )
 */
void komodo_update_interest();

/***
 * @brief  verify block is valid pax pricing
 * @param height the height of the block
 * @param block the block to check
 * @returns <0 on error, 0 on success
 */
int32_t komodo_check_deposit(int32_t height,const CBlock& block);

/***
 * @brief handle an incoming opreturn
 * @param value 
 * @param opretbuf the opreturn
 * @param opretlen the length of opreturn
 * @returns "assetchain", "kv", or "unknown"
 */
const char *komodo_opreturn(uint64_t value,uint8_t *opretbuf,int32_t opretlen);

void *OS_loadfile(char *fname,uint8_t **bufp,long *lenp,long *allocsizep);

uint8_t *OS_fileptr(long *allocsizep,const char *fname);

int32_t komodo_faststateinit(struct komodo_state *sp,const char *fname,char *symbol,char *dest);

#define issue_curl(cmdstr) bitcoind_RPC(0,(char *)"CBCOINBASE",cmdstr,0,0,0)

char *nonportable_path(char *str);

char *portable_path(char *str);

void *loadfile(char *fname,uint8_t **bufp,long *lenp,long *allocsizep);

void *filestr(long *allocsizep,char *_fname);
