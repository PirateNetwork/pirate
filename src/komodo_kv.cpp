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
#include "komodo_kv.h"
#include "komodo_globals.h"
#include "komodo_utils.h" // portable_mutex_lock
#include "komodo_curve25519.h" // for komodo_kvsigverify
#include <mutex>

std::mutex kv_mutex;

struct komodo_kv 
{ 
    UT_hash_handle hh; 
    bits256 pubkey; 
    uint8_t *key;
    uint8_t *value; 
    int32_t height; 
    uint32_t flags; 
    uint16_t keylen;
    uint16_t valuesize; 
};

komodo_kv *KOMODO_KV;

/****
 * @brief build a private key from the public key and passphrase
 * @param pubkeyp the public key
 * @param passphrase the passphrase
 * @return a private key
 */
uint256 komodo_kvprivkey(uint256 *pubkeyp,char *passphrase)
{
    uint256 privkey;
    conv_NXTpassword((uint8_t *)&privkey,(uint8_t *)pubkeyp,(uint8_t *)passphrase,(int32_t)strlen(passphrase));
    return privkey;
}

/****
 * @brief sign
 * @param buf what to sign
 * @param len the length of buf
 * @param _privkey the key to sign with
 */
uint256 komodo_kvsig(uint8_t *buf,int32_t len,uint256 _privkey)
{
    // get the private key in the format we need
    bits256 privkey; 
    memcpy(&privkey,&_privkey,sizeof(privkey));
    // hash the contents of buf
    bits256 hash;
    vcalc_sha256(0,hash.bytes,buf,len);
    bits256 otherpub = curve25519(hash,curve25519_basepoint9());
    bits256 pubkey = curve25519(privkey,curve25519_basepoint9());
    bits256 sig = curve25519_shared(privkey,otherpub);
    bits256 checksig = curve25519_shared(hash,pubkey); // is this needed?
    uint256 usig;
    memcpy(&usig,&sig,sizeof(usig));
    return usig;
}

/****
 * @brief verify the signature
 * @param buf the message
 * @param len the length of the message
 * @param _pubkey who signed
 * @param sig the signature
 * @return -1 on error, otherwise 0
 */
int32_t komodo_kvsigverify(uint8_t *buf,int32_t len,uint256 _pubkey,uint256 sig)
{
    bits256 hash;
    bits256 checksig;
    
    static uint256 zeroes;
    if (_pubkey == zeroes)
        return -1;

    // get the public key in the right format
    bits256 pubkey; 
    memcpy(&pubkey,&_pubkey,sizeof(pubkey));

    // validate the signature
    vcalc_sha256(0,hash.bytes,buf,len);
    checksig = curve25519_shared(hash,pubkey);
    if ( memcmp(&checksig,&sig,sizeof(sig)) != 0 )
        return -1;

    return 0;
}

/***
 * @brief get duration from flags
 * @param flags
 * @returns duration in days
 */
int32_t komodo_kvnumdays(uint32_t flags)
{
    int32_t numdays;
    if ( (numdays= ((flags>>2)&0x3ff)+1) > 365 )
        numdays = 365;
    return(numdays);
}

/***
 * @brief calculate the duration in minutes
 * @param flags
 * @return the duration
 */
int32_t komodo_kvduration(uint32_t flags)
{
    return(komodo_kvnumdays(flags) * KOMODO_KVDURATION);
}

/***
 * @brief calculate the required fee
 * @param flags
 * @param opretlen
 * @param keylen
 * @return the fee
 */
uint64_t komodo_kvfee(uint32_t flags,int32_t opretlen,int32_t keylen)
{
    int32_t numdays,k; uint64_t fee;
    if ( (k= keylen) > 32 )
        k = 32;
    numdays = komodo_kvnumdays(flags);
    if ( (fee= (numdays*(opretlen * opretlen / k))) < 100000 )
        fee = 100000;
    return(fee);
}

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
int32_t komodo_kvsearch(uint256 *pubkeyp, int32_t current_height, uint32_t *flagsp,
        int32_t *heightp, uint8_t value[IGUANA_MAXSCRIPTSIZE], uint8_t *key, int32_t keylen)
{
    *heightp = -1;
    *flagsp = 0;

    komodo_kv *ptr; 
    int32_t retval = -1;
    memset(pubkeyp,0,sizeof(*pubkeyp));
    std::lock_guard<std::mutex> lock(kv_mutex);
    // look in hashtable for key
    HASH_FIND(hh,KOMODO_KV,key,keylen,ptr);
    if ( ptr != nullptr )
    {
        int32_t duration = komodo_kvduration(ptr->flags);
        if ( current_height > (ptr->height + duration) )
        {
            // entry has expired, remove it
            HASH_DELETE(hh,KOMODO_KV,ptr);
            if ( ptr->value != 0 )
                free(ptr->value);
            if ( ptr->key != 0 )
                free(ptr->key);
            free(ptr);
        }
        else
        {
            // place values into parameters
            *heightp = ptr->height;
            *flagsp = ptr->flags;
            for (int32_t i=0; i<32; i++)
            {
                ((uint8_t *)pubkeyp)[i] = ((uint8_t *)&ptr->pubkey)[31-i];
            }
            memcpy(pubkeyp,&ptr->pubkey,sizeof(*pubkeyp));
            if ( (retval= ptr->valuesize) > 0 )
                memcpy(value,ptr->value,retval);
        }
    }
    if ( retval < 0 )
    {
        // search rawmempool
    }
    return retval;
}

/****
 * @brief update value
 * @param opretbuf what to write
 * @param opretlen length of opretbuf
 * @param value the value to be related to the key
 */
void komodo_kvupdate(uint8_t *opretbuf,int32_t opretlen,uint64_t value)
{
    static uint256 zeroes;

    if (chainName.isKMD()) // disable KV for KMD
        return;

    // parse opretbuf
    uint16_t keylen;
    uint16_t valuesize;
    int32_t height;
    uint32_t flags;
    iguana_rwnum(0,&opretbuf[1],sizeof(keylen),&keylen);
    iguana_rwnum(0,&opretbuf[3],sizeof(valuesize),&valuesize);
    iguana_rwnum(0,&opretbuf[5],sizeof(height),&height);
    iguana_rwnum(0,&opretbuf[9],sizeof(flags),&flags);
    uint8_t *key = &opretbuf[13];
    if ( keylen+13 > opretlen )
    {
        static uint32_t counter;
        if ( ++counter < 1 )
            fprintf(stderr,"komodo_kvupdate: keylen.%d + 13 > opretlen.%d, this can be ignored\n",keylen,opretlen);
        return;
    }
    uint8_t *valueptr = &key[keylen];
    uint64_t fee = komodo_kvfee(flags,opretlen,keylen);
    if ( value >= fee )
    {
        // we have enough for the fee
        int32_t coresize = (int32_t)(sizeof(flags)
                +sizeof(height)
                +sizeof(keylen)
                +sizeof(valuesize)
                +keylen+valuesize+1);
        uint256 pubkey;
        if ( opretlen == coresize 
                || opretlen == coresize+sizeof(uint256) 
                || opretlen == coresize+2*sizeof(uint256) )
        {
            // end could be pubkey or pubkey+signature
            if ( opretlen >= coresize+sizeof(uint256) )
            {
                for (uint8_t i=0; i<32; i++)
                    ((uint8_t *)&pubkey)[i] = opretbuf[coresize+i];
            }
            uint256 sig;
            if ( opretlen == coresize+sizeof(uint256)*2 )
            {
                for (uint8_t i=0; i<32; i++)
                    ((uint8_t *)&sig)[i] = opretbuf[coresize+sizeof(uint256)+i];
            }

            uint8_t keyvalue[IGUANA_MAXSCRIPTSIZE*8]; 
            memcpy(keyvalue,key,keylen);
            uint256 refpubkey;
            int32_t kvheight;
            int32_t refvaluesize = komodo_kvsearch(&refpubkey,height,
                    &flags,&kvheight,&keyvalue[keylen],key,keylen);
            if ( refvaluesize  >= 0 )
            {
                if ( zeroes != refpubkey )
                {
                    // validate signature
                    if ( komodo_kvsigverify(keyvalue,keylen+refvaluesize,refpubkey,sig) < 0 )
                    {
                        return;
                    }
                }
            }
            // with validation complete, update internal storage
            std::lock_guard<std::mutex> lock(kv_mutex);
            komodo_kv *ptr;
            bool newflag = false;
            HASH_FIND(hh,KOMODO_KV,key,keylen,ptr);
            if ( ptr != 0 )
            {
                // We are updating an existing entry
                // if we are doing a transfer, log it and insert the pubkey
                char *tstr = (char *)"transfer:";
                char *transferpubstr = (char *)&valueptr[strlen(tstr)];
                if ( strncmp(tstr,(char *)valueptr,strlen(tstr)) == 0 && is_hexstr(transferpubstr,0) == 64 )
                {
                    printf("transfer.(%s) to [%s]? ishex.%d\n",key,transferpubstr,is_hexstr(transferpubstr,0));
                    for (uint8_t i=0; i<32; i++)
                        ((uint8_t *)&pubkey)[31-i] = _decode_hex(&transferpubstr[i*2]);
                }
            }
            else if ( ptr == 0 )
            {
                // add a new entry to the hashtable
                ptr = (komodo_kv *)calloc(1,sizeof(*ptr));
                ptr->key = (uint8_t *)calloc(1,keylen);
                ptr->keylen = keylen;
                memcpy(ptr->key,key,keylen);
                newflag = true;
                HASH_ADD_KEYPTR(hh,KOMODO_KV,ptr->key,ptr->keylen,ptr);
            }
            if ( newflag || (ptr->flags & KOMODO_KVPROTECTED) == 0 ) // can we edit the value?
            {
                if ( ptr->value != nullptr )
                {
                    // clear out old value
                    free(ptr->value);
                    ptr->value = nullptr;
                }
                if ( (ptr->valuesize= valuesize) != 0 )
                {
                    // add value
                    ptr->value = (uint8_t *)calloc(1,valuesize);
                    memcpy(ptr->value,valueptr,valuesize);
                }
            } 
            else 
                fprintf(stderr,"newflag.%d zero or protected %d\n",(uint16_t)newflag,
                        (ptr->flags & KOMODO_KVPROTECTED));
            memcpy(&ptr->pubkey,&pubkey,sizeof(ptr->pubkey));
            ptr->height = height;
            ptr->flags = flags; // jl777 used to or in KVPROTECTED
        } 
        else 
            fprintf(stderr,"KV update size mismatch %d vs %d\n",opretlen,coresize);
    } 
    else 
        fprintf(stderr,"not enough fee\n");
}
