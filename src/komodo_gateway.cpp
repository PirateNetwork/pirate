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
#include "komodo.h"
#include "komodo_globals.h"
#include "komodo_utils.h" // komodo_stateptrget
#include "komodo_bitcoind.h" // komodo_checkcommission
#include "komodo_notary.h"

const char *banned_txids[] =
{
    "78cb4e21245c26b015b888b14c4f5096e18137d2741a6de9734d62b07014dfca", // vout1 only 233559
    "00697be658e05561febdee1aafe368b821ca33fbb89b7027365e3d77b5dfede5", //234172
    "e909465788b32047c472d73e882d79a92b0d550f90be008f76e1edaee6d742ea", //234187
    "f56c6873748a327d0b92b8108f8ec8505a2843a541b1926022883678fb24f9dc", //234188
    "abf08be07d8f5b3a433ddcca7ef539e79a3571632efd6d0294ec0492442a0204", //234213
    "3b854b996cc982fba8c06e76cf507ae7eed52ab92663f4c0d7d10b3ed879c3b0", //234367
    "fa9e474c2cda3cb4127881a40eb3f682feaba3f3328307d518589024a6032cc4", //234635
    "ca746fa13e0113c4c0969937ea2c66de036d20274efad4ce114f6b699f1bc0f3", //234662
    "43ce88438de4973f21b1388ffe66e68fda592da38c6ef939be10bb1b86387041", //234697
    "0aeb748de82f209cd5ff7d3a06f65543904c4c17387c9d87c65fd44b14ad8f8c", //234899
    "bbd3a3d9b14730991e1066bd7c626ca270acac4127131afe25f877a5a886eb25", //235252
    "fa9943525f2e6c32cbc243294b08187e314d83a2870830180380c3c12a9fd33c", //235253
    "a01671c8775328a41304e31a6693bbd35e9acbab28ab117f729eaba9cb769461", //235265
    "2ef49d2d27946ad7c5d5e4ab5c089696762ff04e855f8ab48e83bdf0cc68726d", //235295
    "c85dcffb16d5a45bd239021ad33443414d60224760f11d535ae2063e5709efee", //235296
    // all vouts banned
    "c4ea1462c207547cd6fb6a4155ca6d042b22170d29801a465db5c09fec55b19d", //246748
    "305dc96d8bc23a69d3db955e03a6a87c1832673470c32fe25473a46cc473c7d1", //247204
    //"43416a0c4da6b1a5c1d375bdbe8f7dc8d44d8f60df593d3376aa8221ec66357e", // vout0 only
    //"1eb295ed54c47f35cbccd7e7e40d03041f1853581da6d41102a9d8813782b6cb",
    //"db121e4012222adfc841824984a2a90b7e5b018dd71307822537d58160195e43",
    //"28f95b8148ac4ae6e09c7380e34422fab41d568a411e53dc94823e36a3d6f386",
    //"01d8c839463bda2f2f6400ede4611357913684927a767422a8560ead1b22557c",
    //"6e4980a9e1bd669f4df04732dc6f11b7773b6de88d1abcf89a6b9007d72ef9ac",
    //"6cc1d0495170bc0e11fd3925297623562e529ea1336b66ea61f8a1159041aed2",
    //"250875424cece9bcd98cb226b09da7671625633d6958589e3a462bad89ad87cc", // missed
    //"ea8659011de52f4dac42cda12326064b7b5013b8492f88e33159884ca299aa05", // missed
    //"ce567928b5490a17244167af161b1d8dd6ff753fef222fe6855d95b2278a35b3", // missed
};

/****
 * @brief Check if the n of the vout matches one that is banned
 * @param vout the "n" of the vout
 * @param k the index in the array of banned txids
 * @param indallvouts the index at which all "n"s are banned
 * @returns true if vout is banned
 */
bool komodo_checkvout(int32_t vout,int32_t k,int32_t indallvouts)
{
    if ( k < indallvouts ) // most banned txids are vout 1
        return vout == 1;
    else if ( k == indallvouts || k == indallvouts+1 ) // all vouts are banned for the last 2 txids
        return true;
    return vout == 0; // unsure when this might get executed - JMJ
}

/****
 * @brief retrieve list of banned txids
 * @param[out] indallvoutsp lowest index where all txid "n"s are banned, not just vout 1
 * @param[out] array of txids
 * @param[in] max the max size of the array
 * @returns the number of txids placed into the array
 */
int32_t komodo_bannedset(int32_t *indallvoutsp,uint256 *array,int32_t max)
{
    if ( sizeof(banned_txids)/sizeof(*banned_txids) > max )
    {
        fprintf(stderr,"komodo_bannedset: buffer too small %d vs %d\n",(int32_t)(sizeof(banned_txids)/sizeof(*banned_txids)),max);
        StartShutdown();
    }
    int32_t i;
    for (i=0; i<sizeof(banned_txids)/sizeof(*banned_txids); i++)
        array[i] = uint256S(banned_txids[i]);
    *indallvoutsp = i-2;
    return(i);
}

/***
 * @brief  verify block is valid pax pricing
 * @param height the height of the block
 * @param block the block to check
 * @returns <0 on error, 0 on success
 */
int32_t komodo_check_deposit(int32_t height,const CBlock& block)
{
    int32_t notmatched=0; 
    int32_t activation = 235300;
    if (chainName.isKMD())
    {
        // initialize the array of banned txids
        static uint256 array[64];
        static int32_t numbanned,indallvouts;
        if ( *(int32_t *)&array[0] == 0 )
            numbanned = komodo_bannedset(&indallvouts,array,(int32_t)(sizeof(array)/sizeof(*array)));

        int32_t txn_count = block.vtx.size();
        for (int32_t i=0; i<txn_count; i++)
        {
            if ( i == 0 && txn_count > 1 && block.vtx[txn_count-1].vout.size() > 0 
                    && block.vtx[txn_count-1].vout[0].nValue == 5000 )
            {
                if ( block.vtx[txn_count-1].vin.size() == 1 ) {
                    uint256 hashNotaryProofVin = block.vtx[txn_count-1].vin[0].prevout.hash;
                    CTransaction tx;
                    uint256 hash;
                    int fNotaryProofVinTxFound = GetTransaction(hashNotaryProofVin,tx,hash,false);
                    if (!fNotaryProofVinTxFound) {
                        // try to search in the same block
                        BOOST_FOREACH(const CTransaction &txInThisBlock, block.vtx) {
                            if (txInThisBlock.GetHash() == hashNotaryProofVin) {
                                fNotaryProofVinTxFound = 1;
                                tx = txInThisBlock;
                                hash = block.GetHash();
                                break;
                            }
                        }
                    }
                    if ( fNotaryProofVinTxFound 
                            && block.vtx[0].vout[0].scriptPubKey == tx.vout[block.vtx[txn_count-1].vin[0].prevout.n].scriptPubKey )
                    {
                        notmatched = 1;
                    }
                }  
            }
            int32_t n = block.vtx[i].vin.size();
            for (int32_t j=0; j<n; j++) // for each vin
            {
                for (int32_t k=0; k<numbanned; k++) // for each banned txid
                {
                    if ( block.vtx[i].vin[j].prevout.hash == array[k] 
                            && komodo_checkvout(block.vtx[i].vin[j].prevout.n,k,indallvouts) )
                    {
                        printf("banned tx.%d being used at ht.%d txi.%d vini.%d\n",k,height,i,j);
                        return(-1);
                    }
                }
            }
        }
    }
    // we don't want these checks in VRSC, leave it at the Sapling upgrade
    if ( chainName.isKMD() ||
         ((ASSETCHAINS_COMMISSION != 0 || ASSETCHAINS_FOUNDERS_REWARD) && height > 1) ||
         NetworkUpgradeActive(height, Params().GetConsensus(), Consensus::UPGRADE_SAPLING) )
    {
        int32_t n = block.vtx[0].vout.size();
        int64_t val,prevtotal = 0; int32_t strangeout=0,overflow = 0;
        uint64_t total = 0;
        for (int32_t i=1; i<n; i++)
        {
            uint8_t *script = (uint8_t *)&block.vtx[0].vout[i].scriptPubKey[0];
            if ( (val= block.vtx[0].vout[i].nValue) < 0 || val >= MAX_MONEY )
            {
                overflow = 1;
                break;
            }
            if ( i > 1 && script[0] != 0x6a && val < 5000 )
                strangeout++;
            total += val;
            if ( total < prevtotal || (val != 0 && total == prevtotal) )
            {
                overflow = 1;
                break;
            }
            prevtotal = total;
        }
        if ( chainName.isKMD() )
        {
            if ( overflow != 0 || total > COIN/10 )
            {
                if ( height >= activation )
                {
                    if ( height > 800000 )
                        fprintf(stderr,">>>>>>>> <<<<<<<<<< ht.%d illegal nonz output %.8f n.%d\n",height,dstr(block.vtx[0].vout[1].nValue),n);
                    return(-1);
                }
            }
            else if ( block.nBits == KOMODO_MINDIFF_NBITS && total > 0 ) // to deal with fee stealing
            {
                fprintf(stderr,"notary mined ht.%d with extra %.8f\n",height,dstr(total));
                if ( height > KOMODO_NOTARIES_HEIGHT1 )
                    return(-1);
            }
            if ( strangeout != 0 || notmatched != 0 )
            {
                if ( 0 && strcmp(NOTARY_PUBKEY.c_str(),"03b7621b44118017a16043f19b30cc8a4cfe068ac4e42417bae16ba460c80f3828") == 0 )
                    fprintf(stderr,">>>>>>>>>>>>> DUST ht.%d strangout.%d notmatched.%d <<<<<<<<<\n",height,strangeout,notmatched);
                if ( height > 1000000 && strangeout != 0 )
                    return(-1);
            }
            else if ( height > 814000 )
            {
                uint8_t *script = (uint8_t *)&block.vtx[0].vout[0].scriptPubKey[0];
                int32_t num;
                return(-1 * (komodo_electednotary(&num,script+1,height,0) >= 0) * (height > 1000000));
            }
        }
        else
        {
            int64_t checktoshis = 0;
            if ( (ASSETCHAINS_COMMISSION != 0 || ASSETCHAINS_FOUNDERS_REWARD) && height > 1 )
            {
                if ( (checktoshis= komodo_checkcommission((CBlock *)&block,height)) < 0 )
                {
                    fprintf(stderr,"ht.%d checktoshis %.8f overflow.%d total %.8f strangeout.%d\n",height,dstr(checktoshis),overflow,dstr(total),strangeout);
                    return(-1);
                }
            }
            if ( height > 1 && checktoshis == 0 )
            {
                checktoshis = ((uint64_t)GetBlockSubsidy(height, Params().GetConsensus()) - block.vtx[0].vout[0].nValue);
                // some pools will need to change their pool fee to be (poolfee % - txfees)
                //checktoshis += txn_count * 0.001; // rely on higher level validations to prevent emitting more coins than actual txfees
            }
            if ( height >= 2 && (overflow != 0 || total > checktoshis || strangeout != 0) )
            {
                fprintf(stderr,"checkdeposit: ht.%d checktoshis %.8f overflow.%d total %.8f strangeout.%d\n",height,dstr(checktoshis),overflow,dstr(total),strangeout);
                if ( strangeout != 0 )
                    fprintf(stderr,">>>>>>>>>>>>> %s DUST ht.%d strangeout.%d notmatched.%d <<<<<<<<<\n",chainName.symbol().c_str(),height,strangeout,notmatched);
                return(-1);
            }
        }
    }
    return(0);
}

void komodo_stateind_set(struct komodo_state *sp,uint32_t *inds,int32_t n,uint8_t *filedata,long datalen,const char *symbol,const char *dest)
{
    uint8_t func; long lastK,lastT,lastN,lastV,fpos,lastfpos; int32_t i,count,doissue,iter,numn,numv,numN,numV,numR; uint32_t tmp,prevpos100,offset;
    count = numR = numN = numV = numn = numv = 0;
    lastK = lastT = lastN = lastV = -1;
    for (iter=0; iter<2; iter++)
    {
        for (lastfpos=fpos=prevpos100=i=0; i<n; i++)
        {
            tmp = inds[i];
            if ( (i % 100) == 0 )
                prevpos100 = tmp;
            else
            {
                func = (tmp & 0xff);
                offset = (tmp >> 8);
                fpos = prevpos100 + offset;
                if ( lastfpos >= datalen || (filedata[lastfpos] != func && func != 0) )
                    printf("i.%d/n.%d lastfpos.%ld >= datalen.%ld or [%d] != func.%d\n",i,n,lastfpos,datalen,filedata[lastfpos],func);
                else if ( iter == 0 )
                {
                    switch ( func )
                    {
                        default: case 'P': case 'U': case 'D':
                            inds[i] &= 0xffffff00;
                            break;
                        case 'K':
                            lastK = lastfpos;
                            inds[i] &= 0xffffff00;
                            break;
                        case 'T':
                            lastT = lastfpos;
                            inds[i] &= 0xffffff00;
                            break;
                        case 'N':
                            lastN = lastfpos;
                            numN++;
                            break;
                        case 'V':
                            lastV = lastfpos;
                            numV++;
                            break;
                        case 'R':
                            numR++;
                            break;
                    }
                }
                else
                {
                    doissue = 0;
                    if ( func == 'K' )
                    {
                        if ( lastK == lastfpos )
                            doissue = 1;
                    }
                    else if ( func == 'T' )
                    {
                        if ( lastT == lastfpos )
                            doissue = 1;
                    }
                    else if ( func == 'N' )
                    {
                        if ( numn > numN-128 )
                            doissue = 1;
                        numn++;
                    }
                    else if ( func == 'V' )
                    {
                        numv++;
                    }
                    else if ( func == 'R' )
                        doissue = 1;
                    if ( doissue != 0 )
                    {
                        komodo_parsestatefiledata(sp,filedata,&lastfpos,datalen,symbol,dest);
                        count++;
                    }
                }
            }
            lastfpos = fpos;
        }
    }
    printf("numR.%d numV.%d numN.%d count.%d\n",numR,numV,numN,count);
}

void *OS_loadfile(const char *fname,uint8_t **bufp,long *lenp,long *allocsizep)
{
    FILE *fp;
    long  filesize,buflen = *allocsizep;
    uint8_t *buf = *bufp;
    *lenp = 0;
    if ( (fp= fopen(fname,"rb")) != 0 )
    {
        fseek(fp,0,SEEK_END);
        filesize = ftell(fp);
        if ( filesize == 0 )
        {
            fclose(fp);
            *lenp = 0;
            printf("OS_loadfile null size.(%s)\n",fname);
            return(0);
        }
        if ( filesize > buflen )
        {
            *allocsizep = filesize;
            *bufp = buf = (uint8_t *)realloc(buf,(long)*allocsizep+64);
        }
        rewind(fp);
        if ( buf == 0 )
            printf("Null buf ???\n");
        else
        {
            if ( fread(buf,1,(long)filesize,fp) != (unsigned long)filesize )
                printf("error reading filesize.%ld\n",(long)filesize);
            buf[filesize] = 0;
        }
        fclose(fp);
        *lenp = filesize;
        //printf("loaded.(%s)\n",buf);
    } //else printf("OS_loadfile couldnt load.(%s)\n",fname);
    return(buf);
}

uint8_t *OS_fileptr(long *allocsizep,const char *fname)
{
    long filesize = 0; uint8_t *buf = 0; void *retptr;
    *allocsizep = 0;
    retptr = OS_loadfile(fname,&buf,&filesize,allocsizep);
    return((uint8_t *)retptr);
}

/**
 * @brief Validate the index of the komodostate file
 * 
 * @param[in] sp the komodo_state struct
 * @param[in] indfname the index filename
 * @param filedata bytes of data
 * @param datalen length of filedata
 * @param[out] prevpos100p 
 * @param[out] indcounterp 
 * @param symbol 
 * @param dest 
 * @return -1 on error
 */
long komodo_stateind_validate(struct komodo_state *sp,const std::string& indfname,uint8_t *filedata,long datalen,
        uint32_t *prevpos100p,uint32_t *indcounterp,const char *symbol,const char *dest)
{
    *indcounterp = *prevpos100p = 0;
    long fsize;
    uint8_t *inds;
    if ( (inds= OS_fileptr(&fsize,indfname.c_str())) != 0 )
    {
        long lastfpos = 0;
        fprintf(stderr,"inds.%p validate %s fsize.%ld datalen.%ld n.%d lastfpos.%ld\n",inds,indfname.c_str(),fsize,datalen,(int32_t)(fsize / sizeof(uint32_t)),lastfpos);
        if ( (fsize % sizeof(uint32_t)) == 0 )
        {
            int32_t n = (int32_t)(fsize / sizeof(uint32_t));
            uint32_t prevpos100 = 0;
            long fpos = 0;
            for (int32_t i=0; i<n; i++)
            {
                uint32_t tmp;
                memcpy(&tmp,&inds[i * sizeof(uint32_t)],sizeof(uint32_t));
                if ( (i % 100) == 0 )
                    prevpos100 = tmp;
                else
                {
                    uint8_t func = (tmp & 0xff);
                    uint32_t offset = (tmp >> 8);
                    fpos = prevpos100 + offset;
                    if ( lastfpos >= datalen || filedata[lastfpos] != func )
                    {
                        printf("validate.%d error (%u %d) prev100 %u -> fpos.%ld datalen.%ld [%d] (%c) vs (%c) lastfpos.%ld\n",i,offset,func,prevpos100,fpos,datalen,lastfpos < datalen ? filedata[lastfpos] : -1,func,filedata[lastfpos],lastfpos);
                        return -1;
                    }
                }
                lastfpos = fpos;
            }
            *indcounterp = n;
            *prevpos100p = prevpos100;
            if ( sp != 0 )
                komodo_stateind_set(sp,(uint32_t *)inds,n,filedata,fpos,symbol,dest);
            free(inds);
            return fpos;
        } 
        else 
            printf("wrong filesize %s %ld\n",indfname.c_str(),fsize);
    }
    free(inds);
    fprintf(stderr,"indvalidate return -1\n");
    return -1;
}

long komodo_indfile_update(FILE *indfp,uint32_t *prevpos100p,long lastfpos,long newfpos,uint8_t func,uint32_t *indcounterp)
{
    if ( indfp != 0 )
    {
        uint32_t tmp = ((uint32_t)(newfpos - *prevpos100p) << 8) | (func & 0xff);
        if ( ftell(indfp)/sizeof(uint32_t) != *indcounterp )
            printf("indfp fpos %ld -> ind.%ld vs counter.%u\n",ftell(indfp),ftell(indfp)/sizeof(uint32_t),*indcounterp);
        fwrite(&tmp,1,sizeof(tmp),indfp), (*indcounterp)++;
        if ( (*indcounterp % 100) == 0 )
        {
            *prevpos100p = (uint32_t)newfpos;
            fwrite(prevpos100p,1,sizeof(*prevpos100p),indfp), (*indcounterp)++;
        }
    }
    return newfpos;
}

/***
 * @brief read the komodostate file
 * @param sp the komodo_state struct
 * @param fname the filename
 * @param symbol the chain symbol
 * @param dest the "parent" chain
 * @return true on success
 */
bool komodo_faststateinit(komodo_state *sp,const char *fname,char *symbol, const char *dest)
{
    uint32_t starttime = (uint32_t)time(NULL);

    uint8_t *filedata = nullptr;
    long datalen;
    if ( (filedata= OS_fileptr(&datalen,fname)) != 0 )
    {
        long fpos = 0;
        long lastfpos = 0;
        uint32_t indcounter = 0;
        uint32_t prevpos100 = 0;

        std::string indfname(fname);
        indfname += ".ind";
        FILE *indfp = fopen(indfname.c_str(), "wb");
        if ( indfp != nullptr )
            fwrite(&prevpos100,1,sizeof(prevpos100),indfp), indcounter++;

        fprintf(stderr,"processing %s %ldKB, validated.%d\n",fname,datalen/1024,-1);
        int32_t func;
        while (!ShutdownRequested() && (func= komodo_parsestatefiledata(sp,filedata,&fpos,datalen,symbol,dest)) >= 0)
        {
            lastfpos = komodo_indfile_update(indfp,&prevpos100,lastfpos,fpos,func,&indcounter);
        }
        if (ShutdownRequested()) { fclose(indfp); return false; }
        if ( indfp != nullptr )
        {
            fclose(indfp);
            if ( (fpos= komodo_stateind_validate(0,indfname,filedata,datalen,&prevpos100,&indcounter,symbol,dest)) < 0 )
                printf("unexpected komodostate.ind validate failure %s datalen.%ld\n",indfname.c_str(),datalen);
            else 
                printf("%s validated fpos.%ld\n",indfname.c_str(),fpos);
        }
        fprintf(stderr,"took %d seconds to process %s %ldKB\n",(int32_t)(time(NULL)-starttime),fname,datalen/1024);
        free(filedata);
        return true;
    }
    return false;
}

//uint64_t komodo_interestsum(); // in wallet/rpcwallet.cpp

/***
 * @brief update wallet balance / interest
 * @note called only on KMD chain every 10 seconds ( see ThreadUpdateKomodoInternals() )
 */
void komodo_update_interest()
{
    static uint32_t lastinterest; // prevent needless komodo_interestsum calls
    if (komodo_chainactive_timestamp() > lastinterest)
    {
        komodo_interestsum();
        lastinterest = komodo_chainactive_timestamp();
    }

    static bool first_call = true;
    if ( first_call )
    {
        first_call = false;
        printf("READY for %s RPC calls at %u\n",
                chainName.ToString().c_str(), (uint32_t)time(NULL));
    }
}
