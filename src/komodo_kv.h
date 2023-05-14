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
#include "uint256.h"
#include "komodo_defs.h"
#include <cstdint>

/***
 * @brief calculate the duration in minutes
 * @param flags
 * @return the duration
 */
int32_t komodo_kvduration(uint32_t flags);

/***
 * @brief calculate the required fee
 * @param flags
 * @param opretlen
 * @param keylen
 * @return the fee
 */
uint64_t komodo_kvfee(uint32_t flags,int32_t opretlen,int32_t keylen);

/***
 * @brief find a value
 * @param[out] pubkeyp the found pubkey
 * @param current_height current chain height
 * @param[out] flagsp flags found within the value
 * @param[out] heightp height 
 * @param[out] value the value
 * @param key the key
 * @param keylen the length of the key
 * @return -1 on error, otherwise size of value
 */
int32_t komodo_kvsearch(uint256 *pubkeyp,int32_t current_height,uint32_t *flagsp,
        int32_t *heightp,uint8_t value[IGUANA_MAXSCRIPTSIZE],uint8_t *key,int32_t keylen);

/****
 * @brief update value
 * @param opretbuf what to write
 * @param opretlen length of opretbuf
 * @param value the value to be related to the key
 */
void komodo_kvupdate(uint8_t *opretbuf,int32_t opretlen,uint64_t value);

/****
 * @brief build a private key from the public key and passphrase
 * @param pubkeyp the public key
 * @param passphrase the passphrase
 * @return a private key
 */
uint256 komodo_kvprivkey(uint256 *pubkeyp,char *passphrase);

/****
 * @brief sign
 * @param buf what to sign
 * @param len the length of buf
 * @param _privkey the key to sign with
 */
uint256 komodo_kvsig(uint8_t *buf,int32_t len,uint256 _privkey);

/****
 * @brief verify the signature
 * @param buf the message
 * @param len the length of the message
 * @param _pubkey who signed
 * @param sig the signature
 * @return -1 on error, otherwise 0
 */
int32_t komodo_kvsigverify(uint8_t *buf,int32_t len,uint256 _pubkey,uint256 sig);
