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
#include "komodo_bitcoind.h"
#include "komodo_globals.h"
#include "komodo.h" // komodo_voutupdate()
#include "komodo_utils.h" // OS_milliseconds
#include "komodo_notary.h"
#include "komodo.h"
#include "rpc/net.h"
#include "init.h"


/************************************************************************
 *
 * Initialize the string handler so that it is thread safe
 *
 ************************************************************************/

void init_string(struct return_string *s)
{
    s->len = 0;
    s->ptr = (char *)calloc(1,s->len+1);
    if ( s->ptr == NULL )
    {
        fprintf(stderr,"init_string malloc() failed\n");
        StartShutdown();
    }
    s->ptr[0] = '\0';
}

int tx_height( const uint256 &hash )
{
    int nHeight = 0;
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, hashBlock, true)) {
        fprintf(stderr,"tx hash %s does not exist!\n", hash.ToString().c_str() );
    }

    BlockMap::const_iterator it = mapBlockIndex.find(hashBlock);
    if (it != mapBlockIndex.end()) {
        nHeight = it->second->nHeight;
        //fprintf(stderr,"blockHash %s height %d\n",hashBlock.ToString().c_str(), nHeight);
    } else {
        // Unconfirmed xtns
        //fprintf(stderr,"block hash %s does not exist!\n", hashBlock.ToString().c_str() );
    }
    return nHeight;
}


/************************************************************************
 *
 * Use the "writer" to accumulate text until done
 *
 ************************************************************************/

size_t accumulatebytes(void *ptr,size_t size,size_t nmemb,struct return_string *s)
{
    size_t new_len = s->len + size*nmemb;
    s->ptr = (char *)realloc(s->ptr,new_len+1);
    if ( s->ptr == NULL )
    {
        fprintf(stderr, "accumulate realloc() failed\n");
        StartShutdown();
    }
    memcpy(s->ptr+s->len,ptr,size*nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;
    return(size * nmemb);
}

/************************************************************************
 *
 * return the current system time in milliseconds
 *
 ************************************************************************/

#define EXTRACT_BITCOIND_RESULT  // if defined, ensures error is null and returns the "result" field
#ifdef EXTRACT_BITCOIND_RESULT

/************************************************************************
 *
 * perform post processing of the results
 *
 ************************************************************************/

char *post_process_bitcoind_RPC(char *debugstr,char *command,char *rpcstr,char *params)
{
    long i,j,len; char *retstr = 0; cJSON *json,*result,*error;
    //printf("<<<<<<<<<<< bitcoind_RPC: %s post_process_bitcoind_RPC.%s.[%s]\n",debugstr,command,rpcstr);
    if ( command == 0 || rpcstr == 0 || rpcstr[0] == 0 )
    {
        if ( strcmp(command,"signrawtransaction") != 0 )
            printf("<<<<<<<<<<< bitcoind_RPC: %s post_process_bitcoind_RPC.%s.[%s]\n",debugstr,command,rpcstr);
        return(rpcstr);
    }
    json = cJSON_Parse(rpcstr);
    if ( json == 0 )
    {
        printf("<<<<<<<<<<< bitcoind_RPC: %s post_process_bitcoind_RPC.%s can't parse.(%s) params.(%s)\n",debugstr,command,rpcstr,params);
        free(rpcstr);
        return(0);
    }
    result = cJSON_GetObjectItem(json,"result");
    error = cJSON_GetObjectItem(json,"error");
    if ( error != 0 && result != 0 )
    {
        if ( (error->type&0xff) == cJSON_NULL && (result->type&0xff) != cJSON_NULL )
        {
            retstr = cJSON_Print(result);
            len = strlen(retstr);
            if ( retstr[0] == '"' && retstr[len-1] == '"' )
            {
                for (i=1,j=0; i<len-1; i++,j++)
                    retstr[j] = retstr[i];
                retstr[j] = 0;
            }
        }
        else if ( (error->type&0xff) != cJSON_NULL || (result->type&0xff) != cJSON_NULL )
        {
            if ( strcmp(command,"signrawtransaction") != 0 )
                printf("<<<<<<<<<<< bitcoind_RPC: %s post_process_bitcoind_RPC (%s) error.%s\n",debugstr,command,rpcstr);
        }
        free(rpcstr);
    } else retstr = rpcstr;
    free_json(json);
    //fprintf(stderr,"<<<<<<<<<<< bitcoind_RPC: postprocess returns.(%s)\n",retstr);
    return(retstr);
}
#endif

/************************************************************************
 *
 * perform the query
 *
 ************************************************************************/

char *bitcoind_RPC(char **retstrp,char *debugstr,char *url,char *userpass,char *command,char *params)
{
    static int didinit,count,count2; static double elapsedsum,elapsedsum2;
    struct curl_slist *headers = NULL; struct return_string s; CURLcode res; CURL *curl_handle;
    char *bracket0,*bracket1,*databuf = 0; long len; int32_t specialcase,numretries; double starttime;
    if ( didinit == 0 )
    {
        didinit = 1;
        curl_global_init(CURL_GLOBAL_ALL); //init the curl session
    }
    numretries = 0;
    if ( debugstr != 0 && strcmp(debugstr,"BTCD") == 0 && command != 0 && strcmp(command,"SuperNET") ==  0 )
        specialcase = 1;
    else specialcase = 0;
    if ( url[0] == 0 )
        strcpy(url,"http://127.0.0.1:7876/nxt");
    if ( specialcase != 0 && 0 )
        printf("<<<<<<<<<<< bitcoind_RPC: debug.(%s) url.(%s) command.(%s) params.(%s)\n",debugstr,url,command,params);
try_again:
    if ( retstrp != 0 )
        *retstrp = 0;
    starttime = OS_milliseconds();
    curl_handle = curl_easy_init();
    init_string(&s);
    headers = curl_slist_append(0,"Expect:");

    curl_easy_setopt(curl_handle,CURLOPT_USERAGENT,"mozilla/4.0");//"Mozilla/4.0 (compatible; )");
    curl_easy_setopt(curl_handle,CURLOPT_HTTPHEADER,	headers);
    curl_easy_setopt(curl_handle,CURLOPT_URL,		url);
    curl_easy_setopt(curl_handle,CURLOPT_WRITEFUNCTION,	(void *)accumulatebytes); 		// send all data to this function
    curl_easy_setopt(curl_handle,CURLOPT_WRITEDATA,		&s); 			// we pass our 's' struct to the callback
    curl_easy_setopt(curl_handle,CURLOPT_NOSIGNAL,		1L);   			// supposed to fix "Alarm clock" and long jump crash
    curl_easy_setopt(curl_handle,CURLOPT_NOPROGRESS,	1L);			// no progress callback
    //curl_easy_setopt(curl_handle, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    //curl_easy_setopt(curl_handle, CURLOPT_SSLVERSION, 2);

    if ( strncmp(url,"https",5) == 0 )
    {

        /* printf("[ Decker ] SSL: %s\n", curl_version()); */
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
        //curl_easy_setopt(curl_handle, CURLOPT_VERBOSE, 1L); // this is useful for debug, but seems crash on libcurl/7.64.1 OpenSSL/1.1.1b zlib/1.2.8 librtmp/2.3
    }
    if ( userpass != 0 )
        curl_easy_setopt(curl_handle,CURLOPT_USERPWD,	userpass);
    databuf = 0;
    if ( params != 0 )
    {
        if ( command != 0 && specialcase == 0 )
        {
            len = strlen(params);
            if ( len > 0 && params[0] == '[' && params[len-1] == ']' ) {
                bracket0 = bracket1 = (char *)"";
            }
            else
            {
                bracket0 = (char *)"[";
                bracket1 = (char *)"]";
            }

            databuf = (char *)malloc(256 + strlen(command) + strlen(params));
            sprintf(databuf,"{\"id\":\"jl777\",\"method\":\"%s\",\"params\":%s%s%s}",command,bracket0,params,bracket1);
            //printf("url.(%s) userpass.(%s) databuf.(%s)\n",url,userpass,databuf);
            //
        } //else if ( specialcase != 0 ) fprintf(stderr,"databuf.(%s)\n",params);
        curl_easy_setopt(curl_handle,CURLOPT_POST,1L);
        if ( databuf != 0 )
            curl_easy_setopt(curl_handle,CURLOPT_POSTFIELDS,databuf);
        else curl_easy_setopt(curl_handle,CURLOPT_POSTFIELDS,params);
    }
    //laststart = milliseconds();
    res = curl_easy_perform(curl_handle);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);
    if ( databuf != 0 ) // clean up temporary buffer
    {
        free(databuf);
        databuf = 0;
    }
    if ( res != CURLE_OK )
    {
        numretries++;
        if ( specialcase != 0 )
        {
            fprintf(stderr,"<<<<<<<<<<< bitcoind_RPC.(%s): BTCD.%s timeout params.(%s) s.ptr.(%s) err.%d\n",url,command,params,s.ptr,res);
            free(s.ptr);
            return(0);
        }
        else if ( numretries >= 1 )
        {
            fprintf(stderr,"Maximum number of retries exceeded!\n");
            free(s.ptr);
            return(0);
        }
        if ( (rand() % 1000) == 0 )
            printf( "curl_easy_perform() failed: %s %s.(%s %s), retries: %d\n",curl_easy_strerror(res),debugstr,url,command,numretries);
        free(s.ptr);
        sleep((1<<numretries));
        goto try_again;

    }
    else
    {
        if ( command != 0 && specialcase == 0 )
        {
            count++;
            elapsedsum += (OS_milliseconds() - starttime);
            if ( (count % 1000000) == 0)
                printf("%d: ave %9.6f | elapsed %.3f millis | bitcoind_RPC.(%s) url.(%s)\n",count,elapsedsum/count,(OS_milliseconds() - starttime),command,url);
            if ( retstrp != 0 )
            {
                *retstrp = s.ptr;
                return(s.ptr);
            }
            return(post_process_bitcoind_RPC(debugstr,command,s.ptr,params));
        }
        else
        {
            if ( 0 && specialcase != 0 )
                fprintf(stderr,"<<<<<<<<<<< bitcoind_RPC: BTCD.(%s) -> (%s)\n",params,s.ptr);
            count2++;
            elapsedsum2 += (OS_milliseconds() - starttime);
            if ( (count2 % 10000) == 0)
                printf("%d: ave %9.6f | elapsed %.3f millis | NXT calls.(%s) cmd.(%s)\n",count2,elapsedsum2/count2,(double)(OS_milliseconds() - starttime),url,command);
            return(s.ptr);
        }
    }
    printf("bitcoind_RPC: impossible case\n");
    free(s.ptr);
    return(0);
}

static size_t WriteMemoryCallback(void *ptr,size_t size,size_t nmemb,void *data)
{
    size_t realsize = (size * nmemb);
    struct MemoryStruct *mem = (struct MemoryStruct *)data;
    mem->memory = (char *)((ptr != 0) ? realloc(mem->memory,mem->size + realsize + 1) : malloc(mem->size + realsize + 1));
    if ( mem->memory != 0 )
    {
        if ( ptr != 0 )
            memcpy(&(mem->memory[mem->size]),ptr,realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;
    }
    //printf("got %d bytes\n",(int32_t)(size*nmemb));
    return(realsize);
}

char *curl_post(CURL **cHandlep,char *url,char *userpass,char *postfields,char *hdr0,char *hdr1,char *hdr2,char *hdr3)
{
    struct MemoryStruct chunk; CURL *cHandle; long code; struct curl_slist *headers = 0;
    if ( (cHandle= *cHandlep) == NULL )
        *cHandlep = cHandle = curl_easy_init();
    else curl_easy_reset(cHandle);
    //#ifdef DEBUG
    //curl_easy_setopt(cHandle,CURLOPT_VERBOSE, 1);
    //#endif
    curl_easy_setopt(cHandle,CURLOPT_USERAGENT,"mozilla/4.0");//"Mozilla/4.0 (compatible; )");
    curl_easy_setopt(cHandle,CURLOPT_SSL_VERIFYPEER,0);
    //curl_easy_setopt(cHandle,CURLOPT_SSLVERSION,1);
    curl_easy_setopt(cHandle,CURLOPT_URL,url);
    curl_easy_setopt(cHandle,CURLOPT_CONNECTTIMEOUT,10);
    if ( userpass != 0 && userpass[0] != 0 )
        curl_easy_setopt(cHandle,CURLOPT_USERPWD,userpass);
    if ( postfields != 0 && postfields[0] != 0 )
    {
        curl_easy_setopt(cHandle,CURLOPT_POST,1);
        curl_easy_setopt(cHandle,CURLOPT_POSTFIELDS,postfields);
    }
    if ( hdr0 != NULL && hdr0[0] != 0 )
    {
        //printf("HDR0.(%s) HDR1.(%s) HDR2.(%s) HDR3.(%s)\n",hdr0!=0?hdr0:"",hdr1!=0?hdr1:"",hdr2!=0?hdr2:"",hdr3!=0?hdr3:"");
        headers = curl_slist_append(headers,hdr0);
        if ( hdr1 != 0 && hdr1[0] != 0 )
            headers = curl_slist_append(headers,hdr1);
        if ( hdr2 != 0 && hdr2[0] != 0 )
            headers = curl_slist_append(headers,hdr2);
        if ( hdr3 != 0 && hdr3[0] != 0 )
            headers = curl_slist_append(headers,hdr3);
    } //headers = curl_slist_append(0,"Expect:");
    if ( headers != 0 )
        curl_easy_setopt(cHandle,CURLOPT_HTTPHEADER,headers);
    //res = curl_easy_perform(cHandle);
    memset(&chunk,0,sizeof(chunk));
    curl_easy_setopt(cHandle,CURLOPT_WRITEFUNCTION,WriteMemoryCallback);
    curl_easy_setopt(cHandle,CURLOPT_WRITEDATA,(void *)&chunk);
    curl_easy_perform(cHandle);
    curl_easy_getinfo(cHandle,CURLINFO_RESPONSE_CODE,&code);
    if ( headers != 0 )
        curl_slist_free_all(headers);
    if ( code != 200 )
        printf("(%s) server responded with code %ld (%s)\n",url,code,chunk.memory);
    return(chunk.memory);
}

char *komodo_issuemethod(char *userpass,char *method,char *params,uint16_t port)
{
    //static void *cHandle;
    char url[512],*retstr=0,*retstr2=0,postdata[8192];
    if ( params == 0 || params[0] == 0 )
        params = (char *)"[]";
    if ( strlen(params) < sizeof(postdata)-128 )
    {
        sprintf(url,(char *)"http://127.0.0.1:%u",port);
        sprintf(postdata,"{\"method\":\"%s\",\"params\":%s}",method,params);
        retstr2 = bitcoind_RPC(&retstr,(char *)"debug",url,userpass,method,params);
    }
    return(retstr2);
}

int32_t notarizedtxid_height(char *dest,char *txidstr,int32_t *kmdnotarized_heightp)
{
    char *jsonstr,params[256],*userpass; uint16_t port; cJSON *json,*item; int32_t height = 0,txid_height = 0,txid_confirmations = 0;
    params[0] = 0;
    *kmdnotarized_heightp = 0;
    if ( strcmp(dest,"KMD") == 0 )
    {
        port = KMD_PORT;
        userpass = KMDUSERPASS;
    }
    else if ( strcmp(dest,"BTC") == 0 ) // this is no longer strictly BTC; depends on -notary= path
    {
        port = DEST_PORT;
        userpass = BTCUSERPASS;
    }
    else return(0);
    if ( userpass[0] != 0 )
    {
        if ( strcmp("BTC",dest) != 0 )
        {
            if ( (jsonstr= komodo_issuemethod(userpass,(char *)"getinfo",params,port)) != 0 )
            {
                //printf("(%s)\n",jsonstr);
                if ( (json= cJSON_Parse(jsonstr)) != 0 )
                {
                    if ( (item= jobj(json,(char *)"result")) != 0 )
                    {
                        height = jint(item,(char *)"blocks");
                        *kmdnotarized_heightp = height;
                    }
                    free_json(json);
                }
                free(jsonstr);
            }
        } else {
            if ( (jsonstr= komodo_issuemethod(userpass,(char *)"getblockchaininfo",params,port)) != 0 )
            {
                //printf("(%s)\n",jsonstr);
                if ( (json= cJSON_Parse(jsonstr)) != 0 )
                {
                    if ( (item= jobj(json,(char *)"result")) != 0 )
                    {
                        height = jint(item,(char *)"blocks");
                        *kmdnotarized_heightp = strcmp(dest,"KMD") == 0 ? jint(item,(char *)"notarized") : height;
                    }
                    free_json(json);
                }
                free(jsonstr);
            }
        }
        sprintf(params,"[\"%s\", 1]",txidstr);
        if ( (jsonstr= komodo_issuemethod(userpass,(char *)"getrawtransaction",params,port)) != 0 )
        {
            //printf("(%s)\n",jsonstr);
            if ( (json= cJSON_Parse(jsonstr)) != 0 )
            {
                if ( (item= jobj(json,(char *)"result")) != 0 )
                {
                    txid_confirmations = jint(item,(char *)"rawconfirmations");
                    if ( txid_confirmations > 0 && height > txid_confirmations )
                        txid_height = height - txid_confirmations;
                    else txid_height = height;
                    //printf("height.%d tconfs.%d txid_height.%d\n",height,txid_confirmations,txid_height);
                }
                free_json(json);
            }
            free(jsonstr);
        }
    }
    return(txid_height);
}

int32_t komodo_verifynotarizedscript(int32_t height,uint8_t *script,int32_t len,uint256 NOTARIZED_HASH)
{
    int32_t i; uint256 hash; char params[256];
    for (i=0; i<32; i++)
        ((uint8_t *)&hash)[i] = script[2+i];
    if ( hash == NOTARIZED_HASH )
        return(1);
    for (i=0; i<32; i++)
        printf("%02x",((uint8_t *)&NOTARIZED_HASH)[i]);
    printf(" notarized, ");
    for (i=0; i<32; i++)
        printf("%02x",((uint8_t *)&hash)[i]);
    printf(" opreturn from [%s] ht.%d MISMATCHED\n",chainName.symbol().c_str(),height);
    return(-1);
}

void komodo_reconsiderblock(uint256 blockhash)
{
    char params[256],*jsonstr,*hexstr;
    sprintf(params,"[\"%s\"]",blockhash.ToString().c_str());
    if ( (jsonstr= komodo_issuemethod(ASSETCHAINS_USERPASS,(char *)"reconsiderblock",params,ASSETCHAINS_RPCPORT)) != 0 )
    {
        //fprintf(stderr,"komodo_reconsiderblock.(%s) (%s %u) -> (%s)\n",params,ASSETCHAINS_USERPASS,ASSETCHAINS_RPCPORT,jsonstr);
        free(jsonstr);
    }
    //fprintf(stderr,"komodo_reconsiderblock.(%s) (%s %u) -> NULL\n",params,ASSETCHAINS_USERPASS,ASSETCHAINS_RPCPORT);
}

int32_t komodo_verifynotarization(const char *symbol,const char *dest,int32_t height,int32_t NOTARIZED_HEIGHT,uint256 NOTARIZED_HASH,uint256 NOTARIZED_DESTTXID)
{
    char params[256];
    char *jsonstr = nullptr;
    char *hexstr; uint8_t *script,_script[8192]; int32_t n,len,retval = -1; cJSON *json,*txjson,*vouts,*vout,*skey;
    script = _script;
    sprintf(params,"[\"%s\", 1]",NOTARIZED_DESTTXID.ToString().c_str());
    if ( strcmp(symbol, chainName.ToString().c_str()) != 0 )
        return(0);
    if ( strcmp(dest,"KMD") == 0 )
    {
        if ( KMDUSERPASS[0] != 0 )
        {
            if ( !chainName.isKMD() )
            {
                jsonstr = komodo_issuemethod(KMDUSERPASS,(char *)"getrawtransaction",params,KMD_PORT);
                //printf("userpass.(%s) got (%s)\n",KMDUSERPASS,jsonstr);
            }
        }//else jsonstr = _dex_getrawtransaction();
        else return(0); // need universal way to issue DEX* API, since notaries mine most blocks, this ok
    }
    else if ( strcmp(dest,"BTC") == 0 )     // Note: this should work for LTC too (BTC is used as an alias for LTC)
    {
        if ( BTCUSERPASS[0] != 0 )
        {
            jsonstr = komodo_issuemethod(BTCUSERPASS,(char *)"getrawtransaction",params,DEST_PORT);
        }
        //else jsonstr = _dex_getrawtransaction();
        else return(0);
    }
    else
    {
        printf("[%s] verifynotarization error unexpected dest.(%s)\n",chainName.symbol().c_str(),dest);
        return(-1);
    }
    if ( jsonstr != 0 )
    {
        if ( (json= cJSON_Parse(jsonstr)) != 0 )
        {
            if ( (txjson= jobj(json,(char *)"result")) != 0 && (vouts= jarray(&n,txjson,(char *)"vout")) != nullptr )
            {
                vout = jitem(vouts,n-1);
                if ( (skey= jobj(vout,(char *)"scriptPubKey")) != 0 )
                {
                    if ( (hexstr= jstr(skey,(char *)"hex")) != 0 )
                    {
                        //printf("HEX.(%s) vs hash.%s\n",hexstr,NOTARIZED_HASH.ToString().c_str());
                        len = strlen(hexstr) >> 1;
                        decode_hex(script,len,hexstr);
                        if ( script[1] == 0x4c )
                        {
                            script++;
                            len--;
                        }
                        else if ( script[1] == 0x4d )
                        {
                            script += 2;
                            len -= 2;
                        }
                        retval = komodo_verifynotarizedscript(height,script,len,NOTARIZED_HASH);
                    }
                }
            }
            free_json(txjson);
        }
        free(jsonstr);
    }
    return(retval);
}

CScript komodo_makeopret(CBlock *pblock, bool fNew)
{
    std::vector<uint256> vLeaves;
    vLeaves.push_back(pblock->hashPrevBlock);
    for (int32_t i = 0; i < pblock->vtx.size()-(fNew ? 0 : 1); i++)
        vLeaves.push_back(pblock->vtx[i].GetHash());
    uint256 merkleroot = GetMerkleRoot(vLeaves);
    CScript opret;
    opret << OP_RETURN << E_MARSHAL(ss << merkleroot);
    return(opret);
}

uint64_t komodo_seed(int32_t height)
{
    uint64_t seed = 0;
    seed = (height << 13) ^ (height << 2);
    seed <<= 21;
    seed |= (height & 0xffffffff);
    seed ^= (seed << 17) ^ (seed << 1);
    return(seed);
}

uint32_t komodo_txtime(CScript &opret,uint64_t *valuep,uint256 hash, int32_t n, char *destaddr)
{
    CTxDestination address; CTransaction tx; uint256 hashBlock; int32_t numvouts;
    *valuep = 0;
    if (!GetTransaction(hash, tx, hashBlock, true))
    {
        //fprintf(stderr,"ERROR: %s/v%d locktime.%u\n",hash.ToString().c_str(),n,(uint32_t)tx.nLockTime);
        return(0);
    }
    numvouts = tx.vout.size();
    //fprintf(stderr,"%s/v%d locktime.%u\n",hash.ToString().c_str(),n,(uint32_t)tx.nLockTime);
    if ( n < numvouts )
    {
        *valuep = tx.vout[n].nValue;
        opret = tx.vout[numvouts-1].scriptPubKey;
        if (ExtractDestination(tx.vout[n].scriptPubKey, address))
            strcpy(destaddr,CBitcoinAddress(address).ToString().c_str());
    }
    return(tx.nLockTime);
}

CBlockIndex *komodo_getblockindex(uint256 hash)
{
    BlockMap::const_iterator it = mapBlockIndex.find(hash);
    return((it != mapBlockIndex.end()) ? it->second : NULL);
}

uint32_t komodo_txtime2(uint64_t *valuep,uint256 hash,int32_t n,char *destaddr)
{
    CTxDestination address; CBlockIndex *pindex; CTransaction tx; uint256 hashBlock; uint32_t txtime = 0;
    *valuep = 0;
    if (!GetTransaction(hash, tx, hashBlock, true))
    {
        //fprintf(stderr,"ERROR: %s/v%d locktime.%u\n",hash.ToString().c_str(),n,(uint32_t)tx.nLockTime);
        return(0);
    }
    if ( (pindex= komodo_getblockindex(hashBlock)) != 0 )
        txtime = pindex->nTime;
    else txtime = tx.nLockTime;
    //fprintf(stderr,"%s/v%d locktime.%u\n",hash.ToString().c_str(),n,(uint32_t)tx.nLockTime);
    if ( n < tx.vout.size() )
    {
        *valuep = tx.vout[n].nValue;
        if (ExtractDestination(tx.vout[n].scriptPubKey, address))
            strcpy(destaddr,CBitcoinAddress(address).ToString().c_str());
    }
    return(txtime);
}

CScript EncodeStakingOpRet(uint256 merkleroot)
{
    CScript opret; uint8_t evalcode = 77;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << merkleroot);
    return(opret);
}

uint8_t DecodeStakingOpRet(CScript scriptPubKey, uint256 &merkleroot)
{
    std::vector<uint8_t> vopret; uint8_t evalcode;
    GetOpReturnData(scriptPubKey, vopret);
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> evalcode; ss >> merkleroot) != 0 && evalcode == 77 )
    {
        //fprintf(stderr, "evalcode.%i merkleroot.%s\n",evalcode, merkleroot.GetHex().c_str() );
        return(1);
    }
    return(0);
}

int32_t komodo_newStakerActive(int32_t height, uint32_t timestamp)
{
    if ( timestamp > nStakedDecemberHardforkTimestamp || komodo_heightstamp(height) > nStakedDecemberHardforkTimestamp ) //December 2019 hardfork
        return(1);
    else return(0);
}

int32_t komodo_hasOpRet(int32_t height, uint32_t timestamp)
{
    return komodo_newStakerActive(height, timestamp) == 1;
}

bool komodo_checkopret(CBlock *pblock, CScript &merkleroot)
{
    merkleroot = pblock->vtx.back().vout.back().scriptPubKey;
    return(merkleroot.IsOpReturn() && merkleroot == komodo_makeopret(pblock, false));
}

bool komodo_hardfork_active(uint32_t time)
{
    return ( (chainName.isKMD() && chainActive.Height() > nDecemberHardforkHeight)
            || ( !chainName.isKMD() && time > nStakedDecemberHardforkTimestamp) ); //December 2019 hardfork
}

uint256 komodo_calcmerkleroot(CBlock *pblock, uint256 prevBlockHash, int32_t nHeight, bool fNew, CScript scriptPubKey)
{
    std::vector<uint256> vLeaves;
    // rereate coinbase tx
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = fNew ? (CScript() << nHeight << CScriptNum(1)) + COINBASE_FLAGS : pblock->vtx[0].vin[0].scriptSig;
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKey;
    txNew.vout[0].nValue = 0;
    txNew.nExpiryHeight = 0;
    txNew.nLockTime = 0;
    //fprintf(stderr, "validation: coinbasetx.%s\n", EncodeHexTx(txNew).c_str());
    //fprintf(stderr, "txnew.%s\n", txNew.GetHash().ToString().c_str());
    vLeaves.push_back(txNew.GetHash());
    vLeaves.push_back(prevBlockHash);
    for (int32_t i = 1; i < pblock->vtx.size()-(fNew ? 0 : 1); i++)
        vLeaves.push_back(pblock->vtx[i].GetHash());
    return GetMerkleRoot(vLeaves);
}

int32_t komodo_isPoS(CBlock *pblock, int32_t height,CTxDestination *addressout)
{
    int32_t n,vout,numvouts,ret; uint32_t txtime; uint64_t value; char voutaddr[64],destaddr[64]; CTxDestination voutaddress; uint256 txid, merkleroot; CScript opret;
    if ( ASSETCHAINS_STAKED != 0 )
    {
        n = pblock->vtx.size();
        //fprintf(stderr,"ht.%d check for PoS numtx.%d numvins.%d numvouts.%d\n",height,n,(int32_t)pblock->vtx[n-1].vin.size(),(int32_t)pblock->vtx[n-1].vout.size());
        if ( n > 1 && pblock->vtx[n-1].vin.size() == 1 && pblock->vtx[n-1].vout.size() == 1+komodo_hasOpRet(height,pblock->nTime) )
        {
            txid = pblock->vtx[n-1].vin[0].prevout.hash;
            vout = pblock->vtx[n-1].vin[0].prevout.n;
            txtime = komodo_txtime(opret,&value,txid,vout,destaddr);
            if ( ExtractDestination(pblock->vtx[n-1].vout[0].scriptPubKey,voutaddress) )
            {
                if ( addressout != 0 ) *addressout = voutaddress;
                strcpy(voutaddr,CBitcoinAddress(voutaddress).ToString().c_str());
                //fprintf(stderr,"voutaddr.%s vs destaddr.%s\n",voutaddr,destaddr);
                if ( komodo_newStakerActive(height, pblock->nTime) != 0 )
                {
                    if ( DecodeStakingOpRet(pblock->vtx[n-1].vout[1].scriptPubKey, merkleroot) != 0 && komodo_calcmerkleroot(pblock, pblock->hashPrevBlock, height, false, pblock->vtx[0].vout[0].scriptPubKey) == merkleroot )
                    {
                        return(1);
                    }
                }
                else
                {
                    if ( pblock->vtx[n-1].vout[0].nValue == value && strcmp(destaddr,voutaddr) == 0 )
                    {
                        return(1);
                    }
                }
            }
        }
    }
    return(0);
}

int32_t komodo_is_notarytx(const CTransaction& tx)
{
    uint8_t *ptr; static uint8_t crypto777[33];
    if ( tx.vout.size() > 0 )
    {
        ptr = (uint8_t *)&tx.vout[0].scriptPubKey[0];
        if ( ptr != 0 )
        {
            if ( crypto777[0] == 0 )
                decode_hex(crypto777,33,(char *)CRYPTO777_PUBSECPSTR);
            if ( memcmp(ptr+1,crypto777,33) == 0 )
            {
                //printf("found notarytx\n");
                return(1);
            }
        }
    }
    return(0);
}

int32_t komodo_block2height(CBlock *block)
{
    static uint32_t match,mismatch;
    int32_t i,n,height2=-1,height = 0; uint8_t *ptr; CBlockIndex *pindex = NULL;
    BlockMap::const_iterator it = mapBlockIndex.find(block->GetHash());
    if ( it != mapBlockIndex.end() && (pindex = it->second) != 0 )
    {
        height2 = (int32_t)pindex->nHeight;
        if ( height2 >= 0 )
            return(height2);
    }
    if ( pindex && block != 0 && block->vtx[0].vin.size() > 0 )
    {
        ptr = (uint8_t *)&block->vtx[0].vin[0].scriptSig[0];
        if ( ptr != 0 && block->vtx[0].vin[0].scriptSig.size() > 5 )
        {
            //for (i=0; i<6; i++)
            //    printf("%02x",ptr[i]);
            n = ptr[0];
            for (i=0; i<n; i++) // looks strange but this works
            {
                //03bb81000101(bb 187) (81 48001) (00 12288256)  <- coinbase.6 ht.12288256
                height += ((uint32_t)ptr[i+1] << (i*8));
                //printf("(%02x %x %d) ",ptr[i+1],((uint32_t)ptr[i+1] << (i*8)),height);
            }
            //printf(" <- coinbase.%d ht.%d\n",(int32_t)block->vtx[0].vin[0].scriptSig.size(),height);
        }
    }
    if ( height != height2 )
    {
        //fprintf(stderr,"block2height height.%d vs height2.%d, match.%d mismatch.%d\n",height,height2,match,mismatch);
        mismatch++;
        if ( height2 >= 0 )
            height = height2;
    } else match++;
    return(height);
}

bool komodo_block2pubkey33(uint8_t *pubkey33,CBlock *block)
{
    int32_t n;
    if ( KOMODO_LOADINGBLOCKS == 0 )
        memset(pubkey33,0xff,33);
    else memset(pubkey33,0,33);
    if ( block->vtx[0].vout.size() > 0 )
    {
        txnouttype whichType;
        std::vector<std::vector<unsigned char>> vch;
        if (Solver(block->vtx[0].vout[0].scriptPubKey, whichType, vch) && whichType == TX_PUBKEY)
        {
            CPubKey pubKey(vch[0]);
            if (pubKey.IsValid())
            {
                memcpy(pubkey33,vch[0].data(),33);
                return true;
            }
            else memset(pubkey33,0,33);
        }
        else memset(pubkey33,0,33);
    }
    return false;
}

int32_t komodo_blockload(CBlock& block,CBlockIndex *pindex)
{
    block.SetNull();
    // Open history file to read
    CAutoFile filein(OpenBlockFile(pindex->GetBlockPos(),true),SER_DISK,CLIENT_VERSION);
    if (filein.IsNull())
        return(-1);
    // Read block
    try { filein >> block; }
    catch (const std::exception& e)
    {
        fprintf(stderr,"readblockfromdisk err B\n");
        return(-1);
    }
    return(0);
}

uint32_t komodo_chainactive_timestamp()
{
    AssertLockHeld(cs_main);
    CBlockIndex *index = chainActive.Tip();
    if ( index != nullptr )
        return((uint32_t)index->GetBlockTime());
    return 0;
}

CBlockIndex *komodo_chainactive(int32_t height)
{
    AssertLockHeld(cs_main);
    CBlockIndex *index = chainActive.Tip();
    if ( index != nullptr )
    {
        if ( height <= index->nHeight )
            return(chainActive[height]);
    }
    return 0;
}

uint32_t komodo_heightstamp(int32_t height)
{
    CBlockIndex *ptr;
    if ( height > 0 && (ptr= komodo_chainactive(height)) != nullptr )
        return(ptr->nTime);
    return 0;
}

void komodo_index2pubkey33(uint8_t *pubkey33,CBlockIndex *pindex,int32_t height)
{
    int32_t num,i; CBlock block;
    memset(pubkey33,0,33);
    if ( pindex != 0 )
    {
        if ( komodo_blockload(block,pindex) == 0 )
            komodo_block2pubkey33(pubkey33,&block);
    }
}

int32_t komodo_eligiblenotary(uint8_t pubkeys[66][33],int32_t *mids,uint32_t blocktimes[66],int32_t *nonzpkeysp,int32_t height)
{
    // after the season HF block ALL new notaries instantly become elegible.
    int32_t i,j,n,duplicate; CBlock block; CBlockIndex *pindex; uint8_t notarypubs33[64][33];
    memset(mids,-1,sizeof(*mids)*66);
    n = komodo_notaries(notarypubs33,height,0);
    for (i=duplicate=0; i<66; i++)
    {
        if ( (pindex= komodo_chainactive(height-i)) != 0 )
        {
            blocktimes[i] = pindex->nTime;
            if ( komodo_blockload(block,pindex) == 0 )
            {
                komodo_block2pubkey33(pubkeys[i],&block);
                for (j=0; j<n; j++)
                {
                    if ( memcmp(notarypubs33[j],pubkeys[i],33) == 0 )
                    {
                        mids[i] = j;
                        (*nonzpkeysp)++;
                        break;
                    }
                }
            } else fprintf(stderr,"couldnt load block.%d\n",height);
            if ( mids[0] >= 0 && i > 0 && mids[i] == mids[0] )
                duplicate++;
        }
    }
    if ( i == 66 && duplicate == 0 && (height > 186233 || *nonzpkeysp > 0) )
        return(1);
    else return(0);
}

int32_t komodo_minerids(uint8_t *minerids,int32_t height,int32_t width)
{
    int32_t i,j,nonz,numnotaries; CBlock block; CBlockIndex *pindex; uint8_t notarypubs33[64][33],pubkey33[33];
    numnotaries = komodo_notaries(notarypubs33,height,0);
    for (i=nonz=0; i<width; i++)
    {
        if ( height-i <= 0 )
            continue;
        if ( (pindex= komodo_chainactive(height-width+i+1)) != 0 )
        {
            if ( komodo_blockload(block,pindex) == 0 )
            {
                komodo_block2pubkey33(pubkey33,&block);
                for (j=0; j<numnotaries; j++)
                {
                    if ( memcmp(notarypubs33[j],pubkey33,33) == 0 )
                    {
                        minerids[nonz++] = j;
                        break;
                    }
                }
                if ( j == numnotaries )
                    minerids[nonz++] = j;
            } else fprintf(stderr,"couldnt load block.%d\n",height);
        }
    }
    return(nonz);
}

int32_t komodo_is_special(uint8_t pubkeys[66][33],int32_t mids[66],uint32_t blocktimes[66],int32_t height,uint8_t pubkey33[33],uint32_t blocktime)
{
    int32_t i,j,notaryid=0,minerid,limit,nid; uint8_t destpubkey33[33];
    komodo_chosennotary(&notaryid,height,pubkey33,blocktimes[0]);
    if ( height >= 82000 )
    {
        if ( notaryid >= 0 )
        {
            for (i=1; i<66; i++)
            {
                if ( mids[i] == notaryid )
                {
                    if ( height > 792000 )
                    {
                        for (j=0; j<66; j++)
                            fprintf(stderr,"%d ",mids[j]);
                        fprintf(stderr,"ht.%d repeat notaryid.%d in mids[%d]\n",height,notaryid,i);
                        return(-1);
                    } else break;
                }
            }
            if ( blocktime != 0 && blocktimes[1] != 0 && blocktime < blocktimes[1]+57 )
            {
                if ( height > 807000 )
                    return(-2);
            }
            return(1);
        } else return(0);
    }
    else
    {
        if ( height >= 34000 && notaryid >= 0 )
        {
            if ( height < 79693 )
                limit = 64;
            else if ( height < 82000 )
                limit = 8;
            else limit = 66;
            for (i=1; i<limit; i++)
            {
                komodo_chosennotary(&nid,height-i,pubkey33,blocktimes[i]);
                if ( nid == notaryid )
                {
                    //for (j=0; j<66; j++)
                    //    fprintf(stderr,"%d ",mids[j]);
                    //fprintf(stderr,"ht.%d repeat mids[%d] nid.%d notaryid.%d\n",height-i,i,nid,notaryid);
                    if ( height > 225000 )
                        return(-1);
                }
            }
            //fprintf(stderr,"special notaryid.%d ht.%d limit.%d\n",notaryid,height,limit);
            return(1);
        }
    }
    return(0);
}

int32_t komodo_MoM(int32_t *notarized_heightp,uint256 *MoMp,uint256 *kmdtxidp,int32_t nHeight,uint256 *MoMoMp,int32_t *MoMoMoffsetp,int32_t *MoMoMdepthp,int32_t *kmdstartip,int32_t *kmdendip)
{
    int32_t depth,notarized_ht; uint256 MoM,kmdtxid;
    depth = komodo_MoMdata(&notarized_ht,&MoM,&kmdtxid,nHeight,MoMoMp,MoMoMoffsetp,MoMoMdepthp,kmdstartip,kmdendip);
    memset(MoMp,0,sizeof(*MoMp));
    memset(kmdtxidp,0,sizeof(*kmdtxidp));
    *notarized_heightp = 0;
    if ( depth != 0 && notarized_ht > 0 && nHeight > notarized_ht-depth && nHeight <= notarized_ht )
    {
        *MoMp = MoM;
        *notarized_heightp = notarized_ht;
        *kmdtxidp = kmdtxid;
    }
    return(depth);
}

CBlockIndex *komodo_blockindex(uint256 hash)
{
    BlockMap::const_iterator it; CBlockIndex *pindex = 0;
    if ( (it = mapBlockIndex.find(hash)) != mapBlockIndex.end() )
        pindex = it->second;
    return(pindex);
}

int32_t komodo_blockheight(uint256 hash)
{
    BlockMap::const_iterator it; CBlockIndex *pindex = 0;
    if ( (it = mapBlockIndex.find(hash)) != mapBlockIndex.end() )
    {
        if ( (pindex= it->second) != 0 )
            return(pindex->nHeight);
    }
    return(0);
}

uint32_t komodo_blocktime(uint256 hash)
{
    BlockMap::const_iterator it; CBlockIndex *pindex = 0;
    if ( (it = mapBlockIndex.find(hash)) != mapBlockIndex.end() )
    {
        if ( (pindex= it->second) != 0 )
            return(pindex->nTime);
    }
    return(0);
}

/******
 * @brief Verify that a height and hash match the most recent (based on height) notarized_checkpoint
 * @param[out] notarized_heightp the notarized height found
 * @param[in] nHeight the height that should be greater than the notarized height
 * @param[in] hash the hash that should match the notarized hash
 * @returns true on success
 */
bool komodo_checkpoint(int32_t *notarized_heightp, int32_t nHeight, uint256 hash)
{
    CBlockIndex *pindex;
    if ( (pindex= chainActive.Tip()) == 0 )
        return false;

    // get the most recent (highest) notarized_checkpointdata
    uint256 notarized_hash;
    uint256 notarized_desttxid;
    int32_t notarized_height = komodo_notarizeddata(pindex->nHeight,&notarized_hash,&notarized_desttxid);
    *notarized_heightp = notarized_height;

    BlockMap::const_iterator it;
    CBlockIndex *notary;
    if ( notarized_height >= 0 && notarized_height <= pindex->nHeight
            && (it = mapBlockIndex.find(notarized_hash)) != mapBlockIndex.end() && (notary = it->second) != nullptr )
    {
        //verify that the block info returned from komodo_notarizeddata matches the actual block
        if ( notary->nHeight == notarized_height ) // if notarized_hash not in chain, reorg
        {
            if ( nHeight < notarized_height )
                return false;
            else if ( nHeight == notarized_height && memcmp(&hash,&notarized_hash,sizeof(hash)) != 0 )
            {
                // the height matches, but the hash they passed us does not match the notarized_hash we found
                fprintf(stderr,"[%s] nHeight.%d == NOTARIZED_HEIGHT.%d, diff hash\n",
                        chainName.symbol().c_str(),nHeight,notarized_height);
                return false;
            }
        }
    }
    return true;
}

int32_t komodo_nextheight()
{
    //AssertLockHeld(cs_main);
    LOCK(cs_main); // assume usually called without lock
    CBlockIndex *pindex; int32_t ht;
    if ( (pindex= chainActive.Tip()) != 0 && (ht= pindex->nHeight) > 0 )
        return(ht+1);
    else return(komodo_longestchain() + 1);
}

/**
 * @brief get the KMD chain height
 *
 * @param kmdheightp the chain height of KMD
 * @return 1 if this chain's height >= komodo_longestchain(), otherwise 0
 */
int32_t komodo_isrealtime(int32_t *kmdheightp)
{
    AssertLockHeld(cs_main);

    komodo_state *sp = komodo_stateptrget( (char*)"KMD");
    if ( sp != nullptr )
        *kmdheightp = sp->CURRENT_HEIGHT;
    else
        *kmdheightp = 0;
    CBlockIndex *pindex = chainActive.Tip();
    if ( pindex != nullptr && pindex->nHeight >= (int32_t)komodo_longestchain() )
        return 1;
    return 0;
}

/*******
 * @brief validate interest in processing a transaction
 * @param tx the transaction
 * @param txheight the desired chain height to evaluate
 * @param cmptime the block time (often the median block time of a chunk of recent blocks)
 * @returns true if tx seems okay, false if tx has been in mempool too long (currently an hour + some)
 */
bool komodo_validate_interest(const CTransaction &tx,int32_t txheight,uint32_t cmptime)
{
    if ( KOMODO_REWIND == 0 && chainName.isKMD() && (int64_t)tx.nLockTime >= LOCKTIME_THRESHOLD ) //1473793441 )
    {
        if ( txheight > 246748 ) // a long time ago
        {
            if ( txheight < 247205 ) // a long time ago
                cmptime -= 16000; // subtract about 4 1/2 hours
            if ( (int64_t)tx.nLockTime < cmptime-KOMODO_MAXMEMPOOLTIME )
            {
                // transaction has been in mempool for more than an hour
                if ( tx.nLockTime != 1477258935 )
                {
                    fprintf(stderr,"komodo_validate_interest.%d reject.%d [%d] locktime %u cmp2.%u\n",1,txheight,(int32_t)(tx.nLockTime - (cmptime-KOMODO_MAXMEMPOOLTIME)),(uint32_t)tx.nLockTime,cmptime);
                }
                return false;
            }
        }
    }
    return true;
}

/*
 komodo_checkPOW (fast) is called early in the process and should only refer to data immediately available. it is a filter to prevent bad blocks from going into the local DB. The more blocks we can filter out at this stage, the less junk in the local DB that will just get purged later on.

 komodo_checkPOW (slow) is called right before connecting blocks so all prior blocks can be assumed to be there and all checks must pass

 commission must be in coinbase.vout[1] and must be >= 10000 sats
 PoS stake must be without txfee and in the last tx in the block at vout[0]
 */

CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams);

uint64_t komodo_commission(const CBlock *pblock,int32_t height)
{
    // LABS fungible chains, cannot have any block reward!
    if ( is_STAKED(chainName.symbol()) == 2 )
        return(0);

    int32_t i,j,n=0,txn_count; int64_t nSubsidy;
    uint64_t commission = 0;
    uint64_t total = 0;

    if ( ASSETCHAINS_FOUNDERS != 0 )
    {
        nSubsidy = GetBlockSubsidy(height,Params().GetConsensus());
        //fprintf(stderr,"ht.%d nSubsidy %.8f prod %llu\n",height,(double)nSubsidy/COIN,(long long)(nSubsidy * ASSETCHAINS_COMMISSION));
        commission = ((nSubsidy * ASSETCHAINS_COMMISSION) / COIN);

        if ( ASSETCHAINS_FOUNDERS > 1 )
        {
            if ( (height % ASSETCHAINS_FOUNDERS) == 0 )
            {
                if ( ASSETCHAINS_FOUNDERS_REWARD == 0 )
                    commission = commission * ASSETCHAINS_FOUNDERS;
                else
                    commission = ASSETCHAINS_FOUNDERS_REWARD;
            }
            else commission = 0;
        }
    }
    else if ( pblock != 0 )
    {
        txn_count = pblock->vtx.size();
        for (i=0; i<txn_count; i++)
        {
            n = pblock->vtx[i].vout.size();
            for (j=0; j<n; j++)
            {
                if ( height > 225000 && ASSETCHAINS_STAKED != 0 && txn_count > 1 && i == txn_count-1 && j == n-1 )
                    break;
                //fprintf(stderr,"(%d %.8f).%d ",i,dstr(pblock->vtx[i].vout[j].nValue),j);
                if ( i != 0 || j != 1 )
                    total += pblock->vtx[i].vout[j].nValue;
                if ( total > 1000000 * COIN )
                {
                    total = 1000000 * COIN;
                    break;
                }
            }
        }
        commission = ((total / 10000) * ASSETCHAINS_COMMISSION) / 10000;
        //commission = ((total * ASSETCHAINS_COMMISSION) / COIN);
    }
    if ( commission < 10000 )
        commission = 0;
    //fprintf(stderr,"-> %.8f\n",(double)commission/COIN);
    return(commission);
}

uint32_t komodo_segid32(char *coinaddr)
{
    bits256 addrhash;
    vcalc_sha256(0,(uint8_t *)&addrhash,(uint8_t *)coinaddr,(int32_t)strlen(coinaddr));
    return(addrhash.uints[0]);
}

int8_t komodo_segid(int32_t nocache,int32_t height)
{
    CTxDestination voutaddress; CBlock block; CBlockIndex *pindex; uint64_t value; uint32_t txtime; char voutaddr[64],destaddr[64]; int32_t txn_count,vout,newStakerActive; uint256 txid,merkleroot; CScript opret; int8_t segid = -1;
    if ( height > 0 && (pindex= komodo_chainactive(height)) != 0 )
    {
        if ( nocache == 0 && pindex->segid >= -1 )
            return(pindex->segid);
        if ( komodo_blockload(block,pindex) == 0 )
        {
            newStakerActive = komodo_newStakerActive(height, block.nTime);
            txn_count = block.vtx.size();
            if ( txn_count > 1 && block.vtx[txn_count-1].vin.size() == 1 && block.vtx[txn_count-1].vout.size() == 1+komodo_hasOpRet(height,pindex->nTime) )
            {
                txid = block.vtx[txn_count-1].vin[0].prevout.hash;
                vout = block.vtx[txn_count-1].vin[0].prevout.n;
                txtime = komodo_txtime(opret,&value,txid,vout,destaddr);
                if ( ExtractDestination(block.vtx[txn_count-1].vout[0].scriptPubKey,voutaddress) )
                {
                    strcpy(voutaddr,CBitcoinAddress(voutaddress).ToString().c_str());
                    if ( newStakerActive == 1 && block.vtx[txn_count-1].vout.size() == 2 && DecodeStakingOpRet(block.vtx[txn_count-1].vout[1].scriptPubKey, merkleroot) != 0 )
                        newStakerActive++;
                    if ( newStakerActive == 2 || (newStakerActive == 0 && strcmp(destaddr,voutaddr) == 0 && block.vtx[txn_count-1].vout[0].nValue == value) )
                    {
                        segid = komodo_segid32(voutaddr) & 0x3f;
                        //fprintf(stderr, "komodo_segid: ht.%i --> %i\n",height,pindex->segid);
                    }
                } //else fprintf(stderr,"komodo_segid ht.%d couldnt extract voutaddress\n",height);
            }
        }
        // The new staker sets segid in komodo_checkPOW, this persists after restart by being saved in the blockindex for blocks past the HF timestamp, to keep backwards compatibility.
        // PoW blocks cannot contain a staking tx. If segid has not yet been set, we can set it here accurately.
        if ( pindex->segid == -2 )
            pindex->segid = segid;
    }
    return(segid);
}

void komodo_segids(uint8_t *hashbuf,int32_t height,int32_t n)
{
    static uint8_t prevhashbuf[100]; static int32_t prevheight;
    int32_t i;
    if ( height == prevheight && n == 100 )
        memcpy(hashbuf,prevhashbuf,100);
    else
    {
        memset(hashbuf,0xff,n);
        for (i=0; i<n; i++)
        {
            hashbuf[i] = (uint8_t)komodo_segid(0,height+i);
            //fprintf(stderr,"%02x ",hashbuf[i]);
        }
        if ( n == 100 )
        {
            memcpy(prevhashbuf,hashbuf,100);
            prevheight = height;
            //fprintf(stderr,"prevsegids.%d\n",height+n);
        }
    }
}

uint32_t komodo_stakehash(uint256 *hashp,char *address,uint8_t *hashbuf,uint256 txid,int32_t vout)
{
    bits256 addrhash;
    vcalc_sha256(0,(uint8_t *)&addrhash,(uint8_t *)address,(int32_t)strlen(address));
    memcpy(&hashbuf[100],&addrhash,sizeof(addrhash));
    memcpy(&hashbuf[100+sizeof(addrhash)],&txid,sizeof(txid));
    memcpy(&hashbuf[100+sizeof(addrhash)+sizeof(txid)],&vout,sizeof(vout));
    vcalc_sha256(0,(uint8_t *)hashp,hashbuf,100 + (int32_t)sizeof(uint256)*2 + sizeof(vout));
    return(addrhash.uints[0]);
}

arith_uint256 komodo_PoWtarget(int32_t *percPoSp,arith_uint256 target,int32_t height,int32_t goalperc,int32_t newStakerActive)
{
    int32_t oldflag = 0,dispflag = 0;
    CBlockIndex *pindex; arith_uint256 easydiff,bnTarget,hashval,sum,ave; bool fNegative,fOverflow; int32_t i,n,m,ht,percPoS,diff,val;
    *percPoSp = percPoS = 0;

    if ( newStakerActive == 0 && height <= 10 || (ASSETCHAINS_STAKED == 100 && height <= 100) )
        return(target);

    sum = arith_uint256(0);
    ave = sum;
    if ( newStakerActive != 0 )
    {
        easydiff.SetCompact(ASSETCHAINS_STAKED_MIN_POW_DIFF,&fNegative,&fOverflow);
        if ( height <= 10 )
            return(easydiff);
    }
    else
        easydiff.SetCompact(STAKING_MIN_DIFF,&fNegative,&fOverflow);
    for (i=n=m=0; i<100; i++)
    {
        ht = height - 100 + i;
        if ( ht <= 1 )
            continue;
        if ( (pindex= komodo_chainactive(ht)) != 0 )
        {
            if ( komodo_segid(0,ht) >= 0 )
            {
                n++;
                percPoS++;
                if ( dispflag != 0 && ASSETCHAINS_STAKED < 100 )
                    fprintf(stderr,"0");
            }
            else
            {
                if ( dispflag != 0 && ASSETCHAINS_STAKED < 100 )
                    fprintf(stderr,"1");
                sum += UintToArith256(pindex->GetBlockHash());
                m++;
            }
        } //else fprintf(stderr, "pindex returned null ht.%i\n",ht);
        if ( dispflag != 0 && ASSETCHAINS_STAKED < 100 && (i % 10) == 9 )
            fprintf(stderr," %d, ",percPoS);
    }
    if ( m+n < 100 )
    {
        // We do actual PoS % at the start. Requires coin distribution in first 10 blocks!
        percPoS = ((percPoS * n) + (goalperc * (100-n))) / 100;
    }
    if ( dispflag != 0 && ASSETCHAINS_STAKED < 100 )
        fprintf(stderr," -> %d%% percPoS vs goalperc.%d ht.%d\n",percPoS,goalperc,height);
    *percPoSp = percPoS;
    if ( m > 0 )
    {
        ave = (sum / arith_uint256(m));
        if ( ave > target )
            ave = target;
    } else ave = target; //easydiff; //else return(target);
    if ( percPoS == 0 )
        percPoS = 1;
    if ( percPoS < goalperc ) // increase PoW diff -> lower bnTarget
    {
        if ( oldflag != 0 )
            bnTarget = (ave / arith_uint256(goalperc * goalperc * goalperc)) * arith_uint256(percPoS * percPoS);
        else bnTarget = (ave / arith_uint256(goalperc * goalperc * goalperc * goalperc)) * arith_uint256(percPoS * percPoS);
        if ( dispflag != 0 && ASSETCHAINS_STAKED < 100 )
        {
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&ave)[i]);
            fprintf(stderr," increase diff -> ");
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[i]);
            fprintf(stderr," floor diff ");
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&target)[i]);
            fprintf(stderr," ht.%d percPoS.%d vs goal.%d -> diff %d\n",height,percPoS,goalperc,goalperc - percPoS);
        }
    }
    else if ( percPoS > goalperc ) // decrease PoW diff -> raise bnTarget
    {
        if ( oldflag != 0 )
        {
            bnTarget = ((ave * arith_uint256(goalperc)) + (easydiff * arith_uint256(percPoS))) / arith_uint256(percPoS + goalperc);
            bnTarget = (bnTarget / arith_uint256(goalperc * goalperc)) * arith_uint256(percPoS * percPoS * percPoS);
        }
        else bnTarget = (ave / arith_uint256(goalperc * goalperc)) * arith_uint256(percPoS * percPoS * percPoS);
        if ( bnTarget > easydiff )
            bnTarget = easydiff;
        else if ( bnTarget < ave ) // overflow
        {
            bnTarget = ((ave * arith_uint256(goalperc)) + (easydiff * arith_uint256(percPoS))) / arith_uint256(percPoS + goalperc);
            if ( bnTarget < ave )
                bnTarget = ave;
        }
        if ( dispflag != 0 )
        {
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&ave)[i]);
            fprintf(stderr," decrease diff -> ");
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[i]);
            fprintf(stderr," floor diff ");
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&target)[i]);
            fprintf(stderr," ht.%d percPoS.%d vs goal.%d -> diff %d\n",height,percPoS,goalperc,goalperc - percPoS);
        }
    }
    else
        bnTarget = ave; // recent ave is perfect
    if ( newStakerActive != 0 && bnTarget > easydiff )
        bnTarget = easydiff;
    return(bnTarget);
}

uint32_t komodo_stake(int32_t validateflag,arith_uint256 bnTarget,int32_t nHeight,uint256 txid,int32_t vout,uint32_t blocktime,uint32_t prevtime,char *destaddr,int32_t PoSperc)
{
    bool fNegative,fOverflow; uint8_t hashbuf[256]; char address[64]; bits256 addrhash; arith_uint256 hashval,mindiff,ratio,coinage256; uint256 hash,pasthash; int32_t segid,minage,i,iter=0; int64_t diff=0; uint32_t txtime,segid32,winner = 0 ; uint64_t value,coinage;
    txtime = komodo_txtime2(&value,txid,vout,address);
    if ( validateflag == 0 )
    {
        //fprintf(stderr,"blocktime.%u -> ",blocktime);
        if ( blocktime < prevtime+3 )
            blocktime = prevtime+3;
        if ( blocktime < GetTime()-60 )
            blocktime = GetTime()+30;
        //fprintf(stderr,"blocktime.%u txtime.%u\n",blocktime,txtime);
    }
    if ( value == 0 || txtime == 0 || blocktime == 0 || prevtime == 0 )
    {
        //fprintf(stderr,"komodo_stake null %.8f %u %u %u\n",dstr(value),txtime,blocktime,prevtime);
        return(0);
    }
    if ( value < SATOSHIDEN )
        return(0);
    value /= SATOSHIDEN;
    mindiff.SetCompact(STAKING_MIN_DIFF,&fNegative,&fOverflow);
    ratio = (mindiff / bnTarget);
    if ( (minage= nHeight*3) > 6000 ) // about 100 blocks
        minage = 6000;
    komodo_segids(hashbuf,nHeight-101,100);
    segid32 = komodo_stakehash(&hash,address,hashbuf,txid,vout);
    segid = ((nHeight + segid32) & 0x3f);
    for (iter=0; iter<600; iter++)
    {
        if ( blocktime+iter+segid*2 < txtime+minage )
            continue;
        diff = (iter + blocktime - txtime - minage);

        if ( diff < 0 )
            diff = 60;
        else if ( diff > 3600*24*30 )
        {
            //printf("diff.%d (iter.%d blocktime.%u txtime.%u minage.%d)\n",(int32_t)diff,iter,blocktime,txtime,(int32_t)minage);
            diff = 3600*24*30;
        }
        if ( iter > 0 )
            diff += segid*2;
        coinage = (value * diff);

        if ( blocktime+iter+segid*2 > prevtime+480 )
            coinage *= ((blocktime+iter+segid*2) - (prevtime+400));
        coinage256 = arith_uint256(coinage+1);
        hashval = ratio * (UintToArith256(hash) / coinage256);
        if ( hashval <= bnTarget )
        {
            winner = 1;
            if ( validateflag == 0 )
            {
                //fprintf(stderr,"winner blocktime.%u iter.%d segid.%d\n",blocktime,iter,segid);
                blocktime += iter;
                blocktime += segid * 2;
            }
            break;
        }
        if ( validateflag != 0 )
        {
            /*for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&hashval)[i]);
            fprintf(stderr," vs ");
            for (i=31; i>=24; i--)
                fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[i]);
            fprintf(stderr," segid.%d iter.%d winner.%d coinage.%llu %d ht.%d t.%u v%d diff.%d\n",segid,iter,winner,(long long)coinage,(int32_t)(blocktime - txtime),nHeight,blocktime,(int32_t)value,(int32_t)diff); */
            break;
        }
    }
    //fprintf(stderr,"iterated until i.%d winner.%d\n",i,winner);
    if ( 0 && validateflag != 0 )
    {
        for (i=31; i>=24; i--)
            fprintf(stderr,"%02x",((uint8_t *)&hashval)[i]);
        fprintf(stderr," vs ");
        for (i=31; i>=24; i--)
            fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[i]);
        fprintf(stderr," segid.%d iter.%d winner.%d coinage.%llu %d ht.%d t.%u v%d diff.%d ht.%d\n",segid,iter,winner,(long long)coinage,(int32_t)(blocktime - txtime),nHeight,blocktime,(int32_t)value,(int32_t)diff,nHeight);
    }
    if ( nHeight < 10 )
        return(blocktime);
    return(blocktime * winner);
}

int32_t komodo_is_PoSblock(int32_t slowflag,int32_t height,CBlock *pblock,arith_uint256 bnTarget,arith_uint256 bhash)
{
    CBlockIndex *previndex,*pindex; char voutaddr[64],destaddr[64]; uint256 txid, merkleroot; uint32_t txtime,prevtime=0; int32_t ret,vout,PoSperc,txn_count,eligible=0,isPoS = 0,segid; uint64_t value; arith_uint256 POWTarget;
    if ( ASSETCHAINS_STAKED == 100 && height <= 10 )
        return(1);
    BlockMap::const_iterator it = mapBlockIndex.find(pblock->GetHash());
    pindex = it != mapBlockIndex.end() ? it->second : NULL;
    int32_t newStakerActive = komodo_newStakerActive(height, pblock->nTime);
    // Get PoSperc and POW Target. slowflag only here, calling it when blocks out of order causes problems.
    if ( slowflag != 0 )
    {
        POWTarget = komodo_PoWtarget(&PoSperc,bnTarget,height,ASSETCHAINS_STAKED,newStakerActive);
    }
    else
    {
        // checks opret merkle root and existence of staking tx.
        return(komodo_isPoS(pblock,height,0));
    }
    txn_count = pblock->vtx.size();
    //fprintf(stderr,"checkblock n.%d vins.%d vouts.%d %.8f %.8f\n",txn_count,(int32_t)pblock->vtx[txn_count-1].vin.size(),(int32_t)pblock->vtx[txn_count-1].vout.size(),(double)pblock->vtx[txn_count-1].vout[0].nValue/COIN,(double)pblock->vtx[txn_count-1].vout[1].nValue/COIN);
    if ( txn_count > 1 && pblock->vtx[txn_count-1].vin.size() == 1 && pblock->vtx[txn_count-1].vout.size() == 1+komodo_hasOpRet(height,pblock->nTime) )
    {
        it = mapBlockIndex.find(pblock->hashPrevBlock);
        if ( it != mapBlockIndex.end() && (previndex = it->second) != NULL )
            prevtime = (uint32_t)previndex->nTime;

        txid = pblock->vtx[txn_count-1].vin[0].prevout.hash;
        vout = pblock->vtx[txn_count-1].vin[0].prevout.n;
        if ( slowflag != 0 && prevtime != 0 )
        {
            if ( komodo_isPoS(pblock,height,0) != 0 )
            {
                // checks utxo is eligible to stake this block
                eligible = komodo_stake(1,bnTarget,height,txid,vout,pblock->nTime,prevtime+ASSETCHAINS_STAKED_BLOCK_FUTURE_HALF,(char *)"",PoSperc);
            }
            if ( eligible == 0 || eligible > pblock->nTime )
            {
                if ( 0 && ASSETCHAINS_STAKED < 100 )
                    fprintf(stderr,"komodo_is_PoSblock PoS failure ht.%d eligible.%u vs blocktime.%u, lag.%d -> check to see if it is PoW block\n",height,eligible,(uint32_t)pblock->nTime,(int32_t)(eligible - pblock->nTime));
            }
            else
            {
                isPoS = 1;
                /*
                    If POWTarget is easydiff, then we have no possible way to detect a PoW block from a staking block!
                    The simplest fix is to make the min diff for PoW blocks higher than the staking mindiff.
                    The logic here, is that all PoS equihash solutions MUST be under the POW target diff,
                    The floor diff can be adjusted with ASSETCHAINS_STAKED_MIN_POW_DIFF, this is a hardforking change.
                */
                if ( ASSETCHAINS_STAKED < 100 && bhash < POWTarget )
                {
                    fprintf(stderr,"[%s:%i] isPoS but meets PoW diff nBits.%u < target.%u\n", chainName.symbol().c_str(), height, bhash.GetCompact(), POWTarget.GetCompact());
                    isPoS = 0;
                }
            }
        }
    }
    //fprintf(stderr,"slow.%d ht.%d isPoS.%d\n",slowflag,height,isPoS);
    return(isPoS != 0);
}

uint64_t komodo_notarypayamount(int32_t nHeight, int64_t notarycount)
{
    int8_t curEra = 0; int64_t ret = 0;
    // if we have an end block in the first era, find our current era
    if ( ASSETCHAINS_ENDSUBSIDY[0] > 1 )
    {
        for ( curEra = 0; curEra <= ASSETCHAINS_LASTERA; curEra++ )
        {
            if ( ASSETCHAINS_ENDSUBSIDY[curEra] > nHeight || ASSETCHAINS_ENDSUBSIDY[curEra] == 0 )
                break;
        }
    }
    if ( curEra > ASSETCHAINS_LASTERA )
        return(0);

    if ( notarycount == 0 )
    {
        fprintf(stderr, "komodo_notarypayamount failed num notaries is 0!\n");
        return(0);
    }
    // Because of reorgs we cannot use the notarized height value.
    // We need to basically guess here and just pay some static amount.
    // Has the unwanted effect of varying coin emission, but cannot be helped.
    //fprintf(stderr, "era.%i paying total of %lu\n",curEra, ASSETCHAINS_NOTARY_PAY[curEra]);
    ret = ASSETCHAINS_NOTARY_PAY[curEra] / notarycount;
    return(ret);
}

int32_t komodo_getnotarizedheight(uint32_t timestamp,int32_t height, uint8_t *script, int32_t len)
{
    // Check the notarisation is valid, and extract notarised height.
    uint64_t voutmask;
    uint8_t scriptbuf[10001];
    int32_t isratification,specialtx,notarizedheight;

    if ( len >= sizeof(uint32_t) && len <= sizeof(scriptbuf) )
    {
        memcpy(scriptbuf,script,len);
        if ( komodo_voutupdate(true,&isratification,0,scriptbuf,len,height,uint256(),1,1,&voutmask,&specialtx,&notarizedheight,0,1,0,timestamp) != -2 )
        {
            fprintf(stderr, "<<<<<<INVALID NOTARIZATION ht.%i\n",notarizedheight);
            return(0);
        }
    } else return(0);
    return(notarizedheight);
}

uint64_t komodo_notarypay(CMutableTransaction &txNew, std::vector<int8_t> &NotarisationNotaries, uint32_t timestamp, int32_t height, uint8_t *script, int32_t len)
{
    // fetch notary pubkey array.
    uint64_t total = 0, AmountToPay = 0;
    int8_t numSN = 0; uint8_t notarypubkeys[64][33] = {0};
    numSN = komodo_notaries(notarypubkeys, height, timestamp);

    // No point going further, no notaries can be paid.
    if ( notarypubkeys[0][0] == 0 )
        return(0);

    // Check the notarisation is valid.
    int32_t notarizedheight = komodo_getnotarizedheight(timestamp, height, script, len);
    if ( notarizedheight == 0 )
        return(0);

    // resize coinbase vouts to number of notary nodes +1 for coinbase itself.
    txNew.vout.resize(NotarisationNotaries.size()+1);

    // Calcualte the amount to pay according to the current era.
    AmountToPay = komodo_notarypayamount(height,NotarisationNotaries.size());
    if ( AmountToPay == 0 )
        return(0);

    // loop over notarisation vins and add transaction to coinbase.
    // Commented prints here can be used to verify manually the pubkeys match.
    for (int8_t n = 0; n < NotarisationNotaries.size(); n++)
    {
        uint8_t *ptr;
        txNew.vout[n+1].scriptPubKey.resize(35);
        ptr = (uint8_t *)&txNew.vout[n+1].scriptPubKey[0];
        ptr[0] = 33;
        for (int8_t i=0; i<33; i++)
        {
            ptr[i+1] = notarypubkeys[NotarisationNotaries[n]][i];
            //fprintf(stderr,"%02x",ptr[i+1]);
        }
        ptr[34] = OP_CHECKSIG;
        //fprintf(stderr," set notary %i PUBKEY33 into vout[%i] amount.%lu\n",NotarisationNotaries[n],n+1,AmountToPay);
        txNew.vout[n+1].nValue = AmountToPay;
        total += txNew.vout[n+1].nValue;
    }
    return(total);
}

bool GetNotarisationNotaries(uint8_t notarypubkeys[64][33], int8_t &numNN, const std::vector<CTxIn> &vin, std::vector<int8_t> &NotarisationNotaries)
{
    uint8_t *script; int32_t scriptlen;
    if ( notarypubkeys[0][0] == 0 )
        return false;
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        uint256 hash; CTransaction tx1;
        if ( GetTransaction(txin.prevout.hash,tx1,hash,false) )
        {
            for (int8_t i = 0; i < numNN; i++)
            {
                script = (uint8_t *)&tx1.vout[txin.prevout.n].scriptPubKey[0];
                scriptlen = (int32_t)tx1.vout[txin.prevout.n].scriptPubKey.size();
                if ( scriptlen == 35 && script[0] == 33 && script[34] == OP_CHECKSIG && memcmp(script+1,notarypubkeys[i],33) == 0 )
                    NotarisationNotaries.push_back(i);
            }
        } else return false;
    }
    return true;
}

uint64_t komodo_checknotarypay(CBlock *pblock,int32_t height)
{
    std::vector<int8_t> NotarisationNotaries; uint8_t *script; int32_t scriptlen;
    uint64_t timestamp = pblock->nTime;
    int8_t numSN = 0; uint8_t notarypubkeys[64][33] = {0};
    numSN = komodo_notaries(notarypubkeys, height, timestamp);
    if ( !GetNotarisationNotaries(notarypubkeys, numSN, pblock->vtx[1].vin, NotarisationNotaries) )
        return(0);

    // check a notary didnt sign twice (this would be an invalid notarisation later on and cause problems)
    std::set<int> checkdupes( NotarisationNotaries.begin(), NotarisationNotaries.end() );
    if ( checkdupes.size() != NotarisationNotaries.size() ) {
        fprintf(stderr, "Possible notarisation is signed multiple times by same notary. It is invalid.\n");
        return(0);
    }
    const CChainParams& chainparams = Params();
    const Consensus::Params &consensusParams = chainparams.GetConsensus();
    uint64_t totalsats = 0;
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, height);
    if ( pblock->vtx[1].vout.size() == 2 && pblock->vtx[1].vout[1].nValue == 0 )
    {
        // Get the OP_RETURN for the notarisation
        uint8_t *script = (uint8_t *)&pblock->vtx[1].vout[1].scriptPubKey[0];
        int32_t scriptlen = (int32_t)pblock->vtx[1].vout[1].scriptPubKey.size();
        if ( script[0] == OP_RETURN )
        {
            // Create the coinbase tx again, using the extracted data, this is the same function the miner uses, with the same data.
            // This allows us to know exactly that the coinbase is correct.
            totalsats = komodo_notarypay(txNew, NotarisationNotaries, pblock->nTime, height, script, scriptlen);
        }
        else
        {
            fprintf(stderr, "vout 2 of notarisation is not OP_RETURN scriptlen.%i\n", scriptlen);
            return(0);
        }
    } else return(0);

    // if notarypay fails, because the notarisation is not valid, exit now as txNew was not created.
    // This should never happen, as the notarisation is checked before this function is called.
    if ( totalsats == 0 )
    {
        fprintf(stderr, "notary pay returned 0!\n");
        return(0);
    }

    int8_t n = 0, i = 0, matches = 0;
    uint64_t total = 0, AmountToPay = 0;

    // get the pay amount from the created tx.
    AmountToPay = txNew.vout[1].nValue;

    // Check the created coinbase pays the correct notaries.
    BOOST_FOREACH(const CTxOut& txout, pblock->vtx[0].vout)
    {
        // skip the coinbase paid to the miner.
        if ( n == 0 )
        {
            n++;
            continue;
        }
        // Check the pubkeys match the pubkeys in the notarisation.
        script = (uint8_t *)&txout.scriptPubKey[0];
        scriptlen = (int32_t)txout.scriptPubKey.size();
        if ( scriptlen == 35 && script[0] == 33 && script[34] == OP_CHECKSIG && memcmp(script+1,notarypubkeys[NotarisationNotaries[n-1]],33) == 0 )
        {
            // check the value is correct
            if ( pblock->vtx[0].vout[n].nValue == AmountToPay )
            {
                matches++;
                total += txout.nValue;
                //fprintf(stderr, "MATCHED AmountPaid.%lu notaryid.%i\n",AmountToPay,NotarisationNotaries[n-1]);
            }
            else fprintf(stderr, "NOT MATCHED AmountPaid.%llu AmountToPay.%llu notaryid.%i\n", (long long)pblock->vtx[0].vout[n].nValue, (long long)AmountToPay, NotarisationNotaries[n-1]);
        }
        n++;
    }
    if ( matches != 0 && matches == NotarisationNotaries.size() && totalsats == total )
    {
        //fprintf(stderr, "Validated coinbase matches notarisation in tx position 1.\n" );
        return(totalsats);
    }
    return(0);
}

bool komodo_appendACscriptpub()
{
    static bool didinit = false;
    if ( didinit )
        return didinit;
    if ( ASSETCHAINS_SCRIPTPUB[ASSETCHAINS_SCRIPTPUB.back()] == 49 && ASSETCHAINS_SCRIPTPUB[ASSETCHAINS_SCRIPTPUB.back()-1] == 51 )
    {
        CTransaction tx; uint256 blockhash;
        // get transaction and check that it occured before height 100.
        if ( myGetTransaction(KOMODO_EARLYTXID,tx,blockhash) && mapBlockIndex[blockhash]->nHeight < KOMODO_EARLYTXID_HEIGHT )
        {
             for (int i = 0; i < tx.vout.size(); i++)
             {
                 if ( tx.vout[i].scriptPubKey[0] == OP_RETURN )
                 {
                     ASSETCHAINS_SCRIPTPUB.pop_back(); ASSETCHAINS_SCRIPTPUB.pop_back(); // remove last 2 chars.
                      // get OP_RETURN from txid and append the HexStr of it to scriptpub
                     ASSETCHAINS_SCRIPTPUB.append(HexStr(tx.vout[i].scriptPubKey.begin()+3, tx.vout[i].scriptPubKey.end()));
                     //fprintf(stderr, "ac_script.%s\n",ASSETCHAINS_SCRIPTPUB.c_str());
                     didinit = true;
                     return true;
                 }
             }
        }
        fprintf(stderr, "could not get KOMODO_EARLYTXID.%s OP_RETURN data. Restart with correct txid!\n", KOMODO_EARLYTXID.GetHex().c_str());
        StartShutdown();
    }
    return false;
}

void GetKomodoEarlytxidScriptPub()
{
    AssertLockHeld(cs_main);
    if ( KOMODO_EARLYTXID == zeroid )
    {
        fprintf(stderr, "Restart deamon with -earlytxid.\n");
        StartShutdown();
        return;
    }
    if ( chainActive.Height() < KOMODO_EARLYTXID_HEIGHT )
    {
        fprintf(stderr, "Cannot fetch -earlytxid before block %d.\n",KOMODO_EARLYTXID_HEIGHT);
        StartShutdown();
        return;
    }
    CTransaction tx; uint256 blockhash; int32_t i;
    // get transaction and check that it occured before height 100.
    if ( myGetTransaction(KOMODO_EARLYTXID,tx,blockhash) && mapBlockIndex[blockhash]->nHeight < KOMODO_EARLYTXID_HEIGHT )
    {
        for (i = 0; i < tx.vout.size(); i++)
            if ( tx.vout[i].scriptPubKey[0] == OP_RETURN )
                break;
        if ( i < tx.vout.size() )
        {
            KOMODO_EARLYTXID_SCRIPTPUB = CScript(tx.vout[i].scriptPubKey.begin()+3, tx.vout[i].scriptPubKey.end());
            fprintf(stderr, "KOMODO_EARLYTXID_SCRIPTPUB.%s\n", HexStr(KOMODO_EARLYTXID_SCRIPTPUB.begin(),KOMODO_EARLYTXID_SCRIPTPUB.end()).c_str());
            return;
        }
    }
    fprintf(stderr, "INVALID -earlytxid, restart daemon with correct txid.\n");
    StartShutdown();
}

int64_t komodo_checkcommission(CBlock *pblock,int32_t height)
{
    int64_t checktoshis=0; uint8_t *script,scripthex[8192]; int32_t scriptlen,matched = 0; static bool didinit = false;
    if ( ASSETCHAINS_COMMISSION != 0 || ASSETCHAINS_FOUNDERS_REWARD != 0 )
    {
        checktoshis = komodo_commission(pblock,height);
        if ( checktoshis >= 10000 && pblock->vtx[0].vout.size() < 2 )
        {
            //fprintf(stderr,"komodo_checkcommission vsize.%d height.%d commission %.8f\n",(int32_t)pblock->vtx[0].vout.size(),height,(double)checktoshis/COIN);
            return(-1);
        }
        else if ( checktoshis != 0 )
        {
            script = (uint8_t *)&pblock->vtx[0].vout[1].scriptPubKey[0];
            scriptlen = (int32_t)pblock->vtx[0].vout[1].scriptPubKey.size();
            if ( 0 )
            {
                int32_t i;
                for (i=0; i<scriptlen; i++)
                    fprintf(stderr,"%02x",script[i]);
                fprintf(stderr," vout[1] %.8f vs %.8f\n",(double)checktoshis/COIN,(double)pblock->vtx[0].vout[1].nValue/COIN);
            }
            if ( ASSETCHAINS_SCRIPTPUB.size() > 1 )
            {
                static bool didinit = false;
                if ( !didinit && height > KOMODO_EARLYTXID_HEIGHT && KOMODO_EARLYTXID != zeroid && komodo_appendACscriptpub() )
                {
                    fprintf(stderr, "appended CC_op_return to ASSETCHAINS_SCRIPTPUB.%s\n", ASSETCHAINS_SCRIPTPUB.c_str());
                    didinit = true;
                }
                if ( ASSETCHAINS_SCRIPTPUB.size()/2 == scriptlen && scriptlen < sizeof(scripthex) )
                {
                    decode_hex(scripthex,scriptlen,(char *)ASSETCHAINS_SCRIPTPUB.c_str());
                    if ( memcmp(scripthex,script,scriptlen) == 0 )
                        matched = scriptlen;
                }
            }
            else if ( scriptlen == 35 && script[0] == 33 && script[34] == OP_CHECKSIG && memcmp(script+1,ASSETCHAINS_OVERRIDE_PUBKEY33,33) == 0 )
                matched = 35;
            else if ( scriptlen == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG && memcmp(script+3,ASSETCHAINS_OVERRIDE_PUBKEYHASH,20) == 0 )
                matched = 25;
            if ( matched == 0 )
            {
                if ( 0 && ASSETCHAINS_SCRIPTPUB.size() > 1 )
                {
                    int32_t i;
                    for (i=0; i<ASSETCHAINS_SCRIPTPUB.size(); i++)
                        fprintf(stderr,"%02x",ASSETCHAINS_SCRIPTPUB[i]);
                }
                fprintf(stderr," -ac[%d] payment to wrong pubkey scriptlen.%d, scriptpub[%d] checktoshis.%llu\n",(int32_t)ASSETCHAINS_SCRIPTPUB.size(),scriptlen,(int32_t)ASSETCHAINS_SCRIPTPUB.size()/2,(long long)checktoshis);
                return(-1);

            }
            if ( pblock->vtx[0].vout[1].nValue != checktoshis )
            {
                fprintf(stderr,"ht.%d checktoshis %.8f vs actual vout[1] %.8f\n",height,dstr(checktoshis),dstr(pblock->vtx[0].vout[1].nValue));
                return(-1);
            }
        }
    }
    return(checktoshis);
}

bool KOMODO_TEST_ASSETCHAIN_SKIP_POW = 0;

int32_t komodo_checkPOW(int64_t stakeTxValue, int32_t slowflag,CBlock *pblock,int32_t height)
{
    uint256 hash,merkleroot; arith_uint256 bnTarget,bhash; bool fNegative,fOverflow; uint8_t *script,pubkey33[33],pubkeys[64][33]; int32_t i,scriptlen,possible,PoSperc,is_PoSblock=0,n,failed = 0,notaryid = -1; int64_t checktoshis,value; CBlockIndex *pprev;
    if ( KOMODO_TEST_ASSETCHAIN_SKIP_POW == 0 && Params().NetworkIDString() == "regtest" )
        KOMODO_TEST_ASSETCHAIN_SKIP_POW = 1;
    if ( !CheckEquihashSolution(pblock, Params()) )
    {
        fprintf(stderr,"komodo_checkPOW slowflag.%d ht.%d CheckEquihashSolution failed\n",slowflag,height);
        return(-1);
    }
    hash = pblock->GetHash();
    bnTarget.SetCompact(pblock->nBits,&fNegative,&fOverflow);
    bhash = UintToArith256(hash);
    possible = komodo_block2pubkey33(pubkey33,pblock);
    if ( height == 0 )
    {
        if ( slowflag != 0 )
        {
            fprintf(stderr,"height.%d slowflag.%d possible.%d cmp.%d\n",height,slowflag,possible,bhash > bnTarget);
            return(0);
        }
        BlockMap::const_iterator it = mapBlockIndex.find(pblock->hashPrevBlock);
        if ( it != mapBlockIndex.end() && (pprev= it->second) != 0 )
            height = pprev->nHeight + 1;
        if ( height == 0 )
            return(0);
    }
    if ( ( !chainName.isKMD() || height > 792000) && bhash > bnTarget )
    {
        failed = 1;
        if ( height > 0 && chainName.isKMD() ) // for the fast case
        {
            if ( (n= komodo_notaries(pubkeys,height,pblock->nTime)) > 0 )
            {
                for (i=0; i<n; i++)
                    if ( memcmp(pubkey33,pubkeys[i],33) == 0 )
                    {
                        notaryid = i;
                        break;
                    }
            }
        }
        else if ( possible == 0 || !chainName.isKMD() )
        {
            if ( KOMODO_TEST_ASSETCHAIN_SKIP_POW )
                return(0);
            if ( ASSETCHAINS_STAKED == 0 ) // komodo_is_PoSblock will check bnTarget for staked chains
                return(-1);
        }
    }
    //fprintf(stderr,"ASSETCHAINS_STAKED.%d ht.%d\n",(int32_t)ASSETCHAINS_STAKED,height);
    if ( ASSETCHAINS_STAKED != 0 && height >= 2 ) // must PoS or have at least 16x better PoW
    {
        CBlockIndex *pindex;
        BlockMap::const_iterator it = mapBlockIndex.find(pblock->GetHash());
        pindex = it != mapBlockIndex.end() ? it->second : NULL;
        int32_t newStakerActive = komodo_newStakerActive(height, pblock->nTime);
        if ( (is_PoSblock= komodo_is_PoSblock(slowflag,height,pblock,bnTarget,bhash)) == 0 )
        {
            if ( slowflag == 0 || height <= 100 ) // need all past 100 blocks to calculate PoW target
                return(0);
            if ( ASSETCHAINS_STAKED == 100 && height > 100 )  // only PoS allowed! POSTEST64
                return(-1);
            else
            {
                bnTarget = komodo_PoWtarget(&PoSperc,bnTarget,height,ASSETCHAINS_STAKED,newStakerActive);
                if ( bhash > bnTarget )
                {
                    for (i=31; i>=16; i--)
                        fprintf(stderr,"%02x",((uint8_t *)&bhash)[i]);
                    fprintf(stderr," > ");
                    for (i=31; i>=16; i--)
                        fprintf(stderr,"%02x",((uint8_t *)&bnTarget)[i]);
                    fprintf(stderr," ht.%d PoW diff violation PoSperc.%d vs goalperc.%d\n",height,PoSperc,(int32_t)ASSETCHAINS_STAKED);
                    return(-1);
                }
                else
                {
                    if ( newStakerActive != 0 )
                    {
                        // PoW fake blocks will be rejected here. If a staking tx is included in a block that meets PoW min diff after block 100, then this will reject it.
                        if ( pblock->vtx.size() > 1 && pblock->vtx[pblock->vtx.size()-1].vout.size() == 2 && DecodeStakingOpRet(pblock->vtx[pblock->vtx.size()-1].vout[1].scriptPubKey, merkleroot) != 0 )
                        {
                            fprintf(stderr, "[%s:%d] staking tx in PoW block, nBits.%u < target.%u\n", chainName.symbol().c_str(),height,bhash.GetCompact(),bnTarget.GetCompact());
                            return(-1);
                        }
                        // set the pindex->segid as this is now fully validated to be a PoW block.
                        if ( pindex != 0 )
                        {
                            pindex->segid = -1;
                            //fprintf(stderr,"PoW block detected set segid.%d <- %d\n",height,pindex->segid);
                        }
                    }
                    failed = 0;
                }
            }
        }
        else if ( is_PoSblock < 0 )
        {
            fprintf(stderr,"[%s:%d] unexpected negative is_PoSblock.%d\n",chainName.symbol().c_str(),height,is_PoSblock);
            return(-1);
        }
        else
        {
            if ( slowflag != 0 && newStakerActive != 0 )
            {
                int8_t segid = -2;
                // the value passed to stakeTxValue, is the blockreward + the valuein-valueout(txfee) of the last tx in the block.
                // the coinbase must pay the fees from the last transaction and the block reward at a minimum.
                if ( pblock->vtx.size() < 1 || pblock->vtx[0].vout.size() < 1 )
                {
                    fprintf(stderr, "[%s:%d] missing coinbase.\n", chainName.symbol().c_str(), height);
                    return(-1);
                }
                else if ( pblock->vtx[0].vout[0].nValue < stakeTxValue )
                {
                    fprintf(stderr, "[%s:%d] coinbase vout0.%lld < blockreward+stakingtxfee.%lld\n", chainName.symbol().c_str(), height, (long long)pblock->vtx[0].vout[0].nValue, (long long)stakeTxValue);
                    return(-1);
                }
                // set the pindex->segid as this is now fully validated to be a PoS block.
                char voutaddr[64]; CTxDestination voutaddress;
                if ( ExtractDestination(pblock->vtx.back().vout[0].scriptPubKey,voutaddress) )
                {
                    strcpy(voutaddr,CBitcoinAddress(voutaddress).ToString().c_str());
                    segid = komodo_segid32(voutaddr) & 0x3f;
                }
                if ( pindex != 0 && segid >= 0 )
                {
                    pindex->segid = segid;
                    //fprintf(stderr,"PoS block set segid.%d <- %d\n",height,pindex->segid);
                }
            }
            failed = 0;
        }
    }
    if ( failed == 0 && ASSETCHAINS_COMMISSION != 0 )
    {
        if ( height == 1 )
        {
            if ( ASSETCHAINS_SCRIPTPUB.size() > 1 && ASSETCHAINS_SCRIPTPUB[ASSETCHAINS_SCRIPTPUB.back()] != 49 && ASSETCHAINS_SCRIPTPUB[ASSETCHAINS_SCRIPTPUB.back()-1] != 51 )
            {
                int32_t scriptlen; uint8_t scripthex[10000];
                script = (uint8_t *)&pblock->vtx[0].vout[0].scriptPubKey[0];
                scriptlen = (int32_t)pblock->vtx[0].vout[0].scriptPubKey.size();
                if ( ASSETCHAINS_SCRIPTPUB.size()/2 == scriptlen && scriptlen < sizeof(scripthex) )
                {
                    decode_hex(scripthex,scriptlen,(char *)ASSETCHAINS_SCRIPTPUB.c_str());
                    if ( memcmp(scripthex,script,scriptlen) != 0 )
                        return(-1);
                } else return(-1);
            }
            else if ( ASSETCHAINS_OVERRIDE_PUBKEY33[0] != 0 )
            {
                script = (uint8_t *)&pblock->vtx[0].vout[0].scriptPubKey[0];
                scriptlen = (int32_t)pblock->vtx[0].vout[0].scriptPubKey.size();
                if ( scriptlen != 35 || script[0] != 33 || script[34] != OP_CHECKSIG || memcmp(script+1,ASSETCHAINS_OVERRIDE_PUBKEY33,33) != 0 )
                    return(-1);
            }
        }
        else
        {
            if ( komodo_checkcommission(pblock,height) < 0 )
                return(-1);
        }
    }
    // Consensus rule to force miners to mine the notary coinbase payment happens in ConnectBlock
    // the default daemon miner, checks the actual vins so the only way this will fail, is if someone changes the miner,
    // and then creates txs to the crypto address meeting min sigs and puts it in tx position 1.
    // If they go through this effort, the block will still fail at connect block, and will be auto purged by the temp file fix.
    if ( failed == 0 && ASSETCHAINS_NOTARY_PAY[0] != 0 && pblock->vtx.size() > 1 )
    {
        // We check the full validation in ConnectBlock directly to get the amount for coinbase. So just approx here.
        if ( slowflag == 0 && pblock->vtx[0].vout.size() > 1 )
        {
            // Check the notarisation tx is to the crypto address.
            if ( !komodo_is_notarytx(pblock->vtx[1]) )
            {
                fprintf(stderr, "notarisation is not to crypto address ht.%i\n",height);
                return(-1);
            }
            // Check min sigs.
            int8_t numSN = 0; uint8_t notarypubkeys[64][33] = {0};
            numSN = komodo_notaries(notarypubkeys, height, pblock->nTime);
            if ( pblock->vtx[1].vin.size() < numSN/5 )
            {
                fprintf(stderr, "ht.%i does not meet minsigs.%i sigs.%lld\n",height,numSN/5,(long long)pblock->vtx[1].vin.size());
                return(-1);
            }
        }
    }

    /*
        KIP0003 partial (for S8) consensus rule implementation. If it's an easy-mined
        block (i.e., produced by a notary), the coinbase should have at only two
        vouts. The first vout should be equal to the block reward (BR), and the second
        should reflect a burned fee. Even if the fee in the block is zero, a 0-value
        OP_RETURN second vout should exist to indicate that the block producer supports
        KIP0003. We will not calculate fees for each transaction here for simplicity.
        We will just check that the coinbase's first vout amount pays exactly the block
        reward or less. This will mean that fees are burned anyway. Before the beginning
        of S9, this rule should be removed, and a common rule (for both easy-mined and
        regular blocks) for KIP0003 with nFees burned amount check should be placed in
        ConnectBlock.
    */

    if (chainName.isKMD() && height > nKIP0003Activation && possible != 0 && failed != 0 && notaryid != -1)
    {
        // it's a P2PK block with hash > bnTarget and mined by notary with index notaryid
        const CBlock &blk = *pblock;
        const CAmount blockReward = GetBlockSubsidy(height, Params().GetConsensus());

        bool fKIP0003Checked = blk.vtx.size() > 1 &&
                               blk.vtx[0].vout.size() == 2 &&
                               blk.vtx[0].vout[0].nValue <= blockReward &&
                               blk.vtx[0].vout[1].scriptPubKey.IsOpReturn();

        LogPrintf("KIP0003 check for NN: ht.%d, hash.%s - %s\n",
                  height, blk.GetHash().ToString(),
                  fKIP0003Checked ? "PASSED!" : "FAILED!");

        // Uncomment the following line if you need to return -1 on failure
        // if (!fKIP0003Checked) return -1;
    }

    //fprintf(stderr,"komodo_checkPOW possible.%d slowflag.%d ht.%d notaryid.%d failed.%d\n",possible,slowflag,height,notaryid,failed);
    if ( failed != 0 && possible == 0 && notaryid < 0 )
        return(-1);
    else return(0);
}

int32_t komodo_acpublic(uint32_t tiptime)
{
    int32_t acpublic = ASSETCHAINS_PUBLIC; CBlockIndex *pindex;
    if ( acpublic == 0 )
    {
        if ( tiptime == 0 )
        {
            AssertLockHeld(cs_main);
            if ( (pindex= chainActive.Tip()) != 0 )
                tiptime = pindex->nTime;
        }
        if ( (chainName.isKMD() || chainName.isSymbol("ZEX")) && tiptime >= KOMODO_SAPLING_DEADLINE )
            acpublic = 1;
    }
    return(acpublic);
}

int64_t komodo_newcoins(int64_t *zfundsp,int64_t *sproutfundsp,int32_t nHeight,CBlock *pblock)
{
    CTxDestination address; int32_t i,j,m,n,vout; uint8_t *script; uint256 txid,hashBlock; int64_t zfunds=0,vinsum=0,voutsum=0,sproutfunds=0;
    n = pblock->vtx.size();
    for (i=0; i<n; i++)
    {
        CTransaction vintx,&tx = pblock->vtx[i];
        if ( (m= tx.vin.size()) > 0 )
        {
            for (j=0; j<m; j++)
            {
                if ( i == 0 )
                    continue;
                txid = tx.vin[j].prevout.hash;
                vout = tx.vin[j].prevout.n;
                if ( !GetTransaction(txid,vintx,hashBlock, false) || vout >= vintx.vout.size() )
                {
                    fprintf(stderr,"ERROR: %s/v%d cant find\n",txid.ToString().c_str(),vout);
                    return(0);
                }
                vinsum += vintx.vout[vout].nValue;
            }
        }
        if ( (m= tx.vout.size()) > 0 )
        {
            for (j=0; j<m-1; j++)
            {
                if ( ExtractDestination(tx.vout[j].scriptPubKey,address) != 0 && strcmp("RD6GgnrMpPaTSMn8vai6yiGA7mN4QGPVMY",CBitcoinAddress(address).ToString().c_str()) != 0 )
                    voutsum += tx.vout[j].nValue;
                else printf("skip %.8f -> %s\n",dstr(tx.vout[j].nValue),CBitcoinAddress(address).ToString().c_str());
            }
            script = (uint8_t *)&tx.vout[j].scriptPubKey[0];
            if ( script == 0 || script[0] != 0x6a )
            {
                if ( ExtractDestination(tx.vout[j].scriptPubKey,address) != 0 && strcmp("RD6GgnrMpPaTSMn8vai6yiGA7mN4QGPVMY",CBitcoinAddress(address).ToString().c_str()) != 0 )
                    voutsum += tx.vout[j].nValue;
            }
        }
        BOOST_FOREACH(const JSDescription& joinsplit, tx.vjoinsplit)
        {
            zfunds -= joinsplit.vpub_new;
            zfunds += joinsplit.vpub_old;
            sproutfunds -= joinsplit.vpub_new;
            sproutfunds += joinsplit.vpub_old;
        }
        zfunds -= tx.GetValueBalanceSapling();
        zfunds -= tx.GetValueBalanceOrchard();
    }
    *zfundsp = zfunds;
    *sproutfundsp = sproutfunds;
    if ( chainName.isKMD() && (voutsum-vinsum) == 100003*SATOSHIDEN ) // 15 times
        return(3 * SATOSHIDEN);
    //if ( voutsum-vinsum+zfunds > 100000*SATOSHIDEN || voutsum-vinsum+zfunds < 0 )
    //.    fprintf(stderr,"ht.%d vins %.8f, vouts %.8f -> %.8f zfunds %.8f\n",nHeight,dstr(vinsum),dstr(voutsum),dstr(voutsum)-dstr(vinsum),dstr(zfunds));
    return(voutsum - vinsum);
}

int64_t komodo_coinsupply(int64_t *zfundsp,int64_t *sproutfundsp,int32_t height)
{
    CBlockIndex *pindex; CBlock block; int64_t zfunds=0,sproutfunds=0,supply = 0;
    //fprintf(stderr,"coinsupply %d\n",height);
    *zfundsp = *sproutfundsp = 0;
    if ( (pindex= komodo_chainactive(height)) != 0 )
    {
        while ( pindex != 0 && pindex->nHeight > 0 )
        {
            if ( pindex->newcoins == 0 && pindex->zfunds == 0 )
            {
                if ( komodo_blockload(block,pindex) == 0 )
                    pindex->newcoins = komodo_newcoins(&pindex->zfunds,&pindex->sproutfunds,pindex->nHeight,&block);
                else
                {
                    fprintf(stderr,"error loading block.%d\n",pindex->nHeight);
                    return(0);
                }
            }
            supply += pindex->newcoins;
            zfunds += pindex->zfunds;
            sproutfunds += pindex->sproutfunds;
            //printf("start ht.%d new %.8f -> supply %.8f zfunds %.8f -> %.8f\n",pindex->nHeight,dstr(pindex->newcoins),dstr(supply),dstr(pindex->zfunds),dstr(zfunds));
            pindex = pindex->pprev;
        }
    }
    *zfundsp = zfunds;
    *sproutfundsp = sproutfunds;
    return(supply);
}

void komodo_addutxo(std::vector<komodo_staking> &array,uint32_t txtime,uint64_t nValue,uint256 txid,int32_t vout,char *address,uint8_t *hashbuf,CScript pk)
{
    uint256 hash; uint32_t segid32; komodo_staking kp;
    segid32 = komodo_stakehash(&hash,address,hashbuf,txid,vout);
    if ( array.size() >= array.capacity() )
    {
        array.reserve(array.capacity() + 1000);
        //fprintf(stderr,"%s realloc array.size().%d array.capacity().%d\n", __func__, array.size(), array.capacity());
    }
    //memset(&kp,0,sizeof(kp));
    strcpy(kp.address, address);
    kp.txid = txid;
    kp.vout = vout;
    kp.hashval = UintToArith256(hash);
    kp.txtime = txtime;
    kp.segid32 = segid32;
    kp.nValue = nValue;
    kp.scriptPubKey = pk;
    array.push_back(kp);
    //fprintf(stderr,"kp.%p array.size().%d\n",kp,array.size());
}

int32_t komodo_staked(CMutableTransaction &txNew,uint32_t nBits,uint32_t *blocktimep,uint32_t *txtimep,uint256 *utxotxidp,int32_t *utxovoutp,uint64_t *utxovaluep,uint8_t *utxosig, uint256 merkleroot)
{
    // use thread_local to prevent crash in case of accidental thread overlapping
    thread_local std::vector<komodo_staking> array;
    thread_local uint32_t lasttime;

    int32_t PoSperc = 0, newStakerActive;
    std::set<CBitcoinAddress> setAddress; int32_t winners,segid,minage,nHeight,counter=0,i,m,siglen=0,nMinDepth = 1,nMaxDepth = 99999999; std::vector<COutput> vecOutputs; uint32_t block_from_future_rejecttime,besttime,eligible,earliest = 0; CScript best_scriptPubKey; arith_uint256 mindiff,ratio,bnTarget,tmpTarget; CBlockIndex *pindex; CTxDestination address; bool fNegative,fOverflow; uint8_t hashbuf[256]; CTransaction tx; uint256 hashBlock;
    uint64_t cbPerc = *utxovaluep, tocoinbase = 0;
    if (!EnsureWalletIsAvailable(0))
        return 0;

    bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
    assert(pwalletMain != NULL);
    *utxovaluep = 0;
    memset(utxotxidp,0,sizeof(*utxotxidp));
    memset(utxovoutp,0,sizeof(*utxovoutp));
    memset(utxosig,0,72);
    CBlockIndex *tipindex = nullptr;
    {
        LOCK(cs_main);
        tipindex = chainActive.Tip();
    }
    if ( tipindex == nullptr )
        return(0);
    nHeight = tipindex->nHeight + 1;
    if ( (minage= nHeight*3) > 6000 ) // about 100 blocks
        minage = 6000;
    if ( *blocktimep < tipindex->nTime+60 )
        *blocktimep = tipindex->nTime+60;
    komodo_segids(hashbuf,nHeight-101,100);
    // this was for VerusHash PoS64
    //tmpTarget = komodo_PoWtarget(&PoSperc,bnTarget,nHeight,ASSETCHAINS_STAKED);
    bool resetstaker = false;
    if ( array.size() != 0 )
    {
        LOCK(cs_main);
        CBlockIndex* pblockindex = chainActive[tipindex->nHeight];
        CBlock block; CTxDestination addressout;
        if ( ReadBlockFromDisk(block, pblockindex, 1) && komodo_isPoS(&block, nHeight, &addressout) != 0 && IsMine(*pwalletMain,addressout) != 0 )
        {
              resetstaker = true;
              fprintf(stderr, "[%s:%d] Reset ram staker after mining a block!\n",chainName.symbol().c_str(),nHeight);
        }
    }

    if ( resetstaker || array.size() == 0 || time(NULL) > lasttime+600 )
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(vecOutputs, false, NULL, true);
        if ( array.size() != 0 )
        {
            array.clear();
            lasttime = 0;
        }
        BOOST_FOREACH(const COutput& out, vecOutputs)
        {
            if ( (tipindex= chainActive.Tip()) == 0 || tipindex->nHeight+1 > nHeight )
            {
                fprintf(stderr,"[%s:%d] chain tip changed during staking loop t.%u counter.%d\n",chainName.symbol().c_str(),nHeight,(uint32_t)time(NULL),counter);
                return(0);
            }
            counter++;
            if ( out.nDepth < nMinDepth || out.nDepth > nMaxDepth )
            {
                //fprintf(stderr,"komodo_staked invalid depth %d\n",(int32_t)out.nDepth);
                continue;
            }
            CAmount nValue = out.tx->vout[out.i].nValue;
            if ( nValue < COIN  || !out.fSpendable )
                continue;
            const CScript& pk = out.tx->vout[out.i].scriptPubKey;
            if ( ExtractDestination(pk,address) != 0 )
            {
                if ( IsMine(*pwalletMain,address) == 0 )
                    continue;
                if ( myGetTransaction(out.tx->GetHash(),tx,hashBlock) != 0 && (pindex= komodo_getblockindex(hashBlock)) != 0 )
                {
                    komodo_addutxo(array,(uint32_t)pindex->nTime,(uint64_t)nValue,out.tx->GetHash(),out.i,(char *)CBitcoinAddress(address).ToString().c_str(),hashbuf,(CScript)pk);
                    //fprintf(stderr,"%s array.size().%d vs array.capacity().%d\n", __func__,array.size(),array.capacity());
                }
            }
        }
        lasttime = (uint32_t)time(NULL);
        //fprintf(stderr,"%s finished kp data of utxo for staking %u ht.%d array.size().%d array.capacity().%d\n", __func__,(uint32_t)time(NULL),nHeight,array.size(),array.capacity());
    }
    block_from_future_rejecttime = (uint32_t)GetTime() + ASSETCHAINS_STAKED_BLOCK_FUTURE_MAX;
    for (i=winners=0; i<array.size(); i++)
    {
        if ( ShutdownRequested() || !GetBoolArg("-gen",false) )
            return(0);
        {
            LOCK(cs_main);
            tipindex = chainActive.Tip();
        }
        if ( tipindex == nullptr || tipindex->nHeight+1 > nHeight )
        {
            fprintf(stderr,"[%s:%d] chain tip changed during staking loop t.%u counter.%d\n",chainName.symbol().c_str(),nHeight,(uint32_t)time(NULL),i);
            return 0;
        }
        komodo_staking &kp = array[i];
        eligible = komodo_stake(0,bnTarget,nHeight,kp.txid,kp.vout,0,(uint32_t)tipindex->nTime+ASSETCHAINS_STAKED_BLOCK_FUTURE_HALF,kp.address,PoSperc);
        if ( eligible > 0 )
        {
            besttime = 0;
            if ( eligible == komodo_stake(1,bnTarget,nHeight,kp.txid,kp.vout,eligible,(uint32_t)tipindex->nTime+ASSETCHAINS_STAKED_BLOCK_FUTURE_HALF,kp.address,PoSperc) )
            {
                // have elegible utxo to stake with.
                if ( earliest == 0 || eligible < earliest || (eligible == earliest && (*utxovaluep == 0 || kp.nValue < *utxovaluep)) )
                {
                    // is better than the previous best, so use it instead.
                    earliest = eligible;
                    best_scriptPubKey = kp.scriptPubKey;
                    *utxovaluep = (uint64_t)kp.nValue;
                    decode_hex((uint8_t *)utxotxidp,32,(char *)kp.txid.GetHex().c_str());
                    *utxovoutp = kp.vout;
                    *txtimep = kp.txtime;
                }
                /*if ( eligible < block_from_future_rejecttime )
                {
                    // better to scan all and choose earliest!
                    fprintf(stderr, "block_from_future_rejecttime.%u vs eligible.%u \n", block_from_future_rejecttime, eligible);
                    break;
                } */
            }
        }
    }
    if ( array.size() < 500 && array.size() != 0 )
    {
        array.clear();
        lasttime = 0;
    }
    if ( earliest != 0 )
    {
        bool signSuccess; SignatureData sigdata; uint64_t txfee; uint8_t *ptr; uint256 revtxid,utxotxid;
        int newHeight = 0;
        {
            LOCK(cs_main);
            newHeight = chainActive.Height() + 1;
        }
        auto consensusBranchId = CurrentEpochBranchId(newHeight, Params().GetConsensus());
        const CKeyStore& keystore = *pwalletMain;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txfee = 0;
        for (i=0; i<32; i++)
            ((uint8_t *)&revtxid)[i] = ((uint8_t *)utxotxidp)[31 - i];
        txNew.vin[0].prevout.hash = revtxid;
        txNew.vin[0].prevout.n = *utxovoutp;
        txNew.vout[0].scriptPubKey = best_scriptPubKey;
        txNew.vout[0].nValue = *utxovaluep - txfee;
        txNew.nLockTime = earliest;
        txNew.nExpiryHeight = nHeight;
        if ( (newStakerActive= komodo_newStakerActive(nHeight,earliest)) != 0 )
        {
            if ( cbPerc > 0 && cbPerc <= 100 )
            {
                tocoinbase = txNew.vout[0].nValue*cbPerc/100;
                txNew.vout[0].nValue -= tocoinbase;
            }
            txNew.vout.resize(2);
            txNew.vout[1].scriptPubKey = EncodeStakingOpRet(merkleroot);
            txNew.vout[1].nValue = 0;
        }
        CTransaction txNewConst(txNew);
        CCoinsViewCache view(pcoinsTip);
        std::vector<CTxOut> allPrevOutputs;
        for (const auto& input : txNewConst.vin) {
            allPrevOutputs.push_back(view.GetOutputFor(input));
        }
        PrecomputedTransactionData txdata(txNewConst, allPrevOutputs);
        signSuccess = ProduceSignature(TransactionSignatureCreator(&keystore, &txNewConst, txdata, 0, *utxovaluep, SIGHASH_ALL), best_scriptPubKey, sigdata, consensusBranchId);
        UpdateTransaction(txNew,0,sigdata);
        ptr = (uint8_t *)&sigdata.scriptSig[0];
        siglen = sigdata.scriptSig.size();
        for (i=0; i<siglen; i++)
            utxosig[i] = ptr[i];
        *utxovaluep = newStakerActive != 0 ? tocoinbase : txNew.vout[0].nValue+txfee;
        if (!signSuccess)
            fprintf(stderr,"failed to create signature\n");
        else
            *blocktimep = earliest;
    }
    return(siglen);
}
