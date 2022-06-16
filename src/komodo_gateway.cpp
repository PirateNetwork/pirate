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
#include "komodo_extern_globals.h"
#include "komodo_utils.h" // komodo_stateptrget
#include "komodo_bitcoind.h" // komodo_checkcommission

struct komodo_extremeprice
{
    uint256 blockhash;
    uint32_t pricebits,timestamp;
    int32_t height;
    int16_t dir,ind;
} ExtremePrice;

uint32_t PriceCache[KOMODO_LOCALPRICE_CACHESIZE][KOMODO_MAXPRICES];//4+sizeof(Cryptos)/sizeof(*Cryptos)+sizeof(Forex)/sizeof(*Forex)];
int64_t PriceMult[KOMODO_MAXPRICES];

struct komodo_priceinfo
{
    FILE *fp;
    char symbol[64];
} PRICES[KOMODO_MAXPRICES];

const char *Cryptos[] = { "KMD", "ETH" }; // must be on binance (for now)
// "LTC", "BCHABC", "XMR", "IOTA", "ZEC", "WAVES",  "LSK", "DCR", "RVN", "DASH", "XEM", "BTS", "ICX", "HOT", "STEEM", "ENJ", "STRAT"
const char *Forex[] =
{ "BGN","NZD","ILS","RUB","CAD","PHP","CHF","AUD","JPY","TRY","HKD","MYR","HRK","CZK","IDR","DKK","NOK","HUF","GBP","MXN","THB","ISK","ZAR","BRL","SGD","PLN","INR","KRW","RON","CNY","SEK","EUR"
}; // must be in ECB list

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

int32_t komodo_checkvout(int32_t vout,int32_t k,int32_t indallvouts)
{
    if ( k < indallvouts )
        return vout == 1;
    else if ( k == indallvouts || k == indallvouts+1 )
        return 1;
    return vout == 0;
}

int32_t komodo_bannedset(int32_t *indallvoutsp,uint256 *array,int32_t max)
{
    int32_t i;
    if ( sizeof(banned_txids)/sizeof(*banned_txids) > max )
    {
        fprintf(stderr,"komodo_bannedset: buffer too small %d vs %d\n",(int32_t)(sizeof(banned_txids)/sizeof(*banned_txids)),max);
        StartShutdown();
    }
    for (i=0; i<sizeof(banned_txids)/sizeof(*banned_txids); i++)
        array[i] = uint256S(banned_txids[i]);
    *indallvoutsp = i-2;
    return(i);
}

void komodo_passport_iteration();

int32_t komodo_check_deposit(int32_t height,const CBlock& block,uint32_t prevtime) // verify above block is valid pax pricing
{
    static uint256 array[64]; static int32_t numbanned,indallvouts;
    int32_t i,j,k,n,ht,baseid,txn_count,activation,num,opretlen,offset=1,errs=0,notmatched=0,matched=0,kmdheights[256],otherheights[256]; uint256 hash,txids[256]; char symbol[KOMODO_ASSETCHAIN_MAXLEN],base[KOMODO_ASSETCHAIN_MAXLEN]; uint16_t vouts[256]; int8_t baseids[256]; uint8_t *script,opcode,rmd160s[256*20]; uint64_t total,subsidy,available,deposited,issued,withdrawn,approved,redeemed,seed; int64_t checktoshis,values[256],srcvalues[256]; struct pax_transaction *pax; struct komodo_state *sp; CTransaction tx;
    activation = 235300;
    if ( *(int32_t *)&array[0] == 0 )
        numbanned = komodo_bannedset(&indallvouts,array,(int32_t)(sizeof(array)/sizeof(*array)));
    memset(baseids,0xff,sizeof(baseids));
    memset(values,0,sizeof(values));
    memset(srcvalues,0,sizeof(srcvalues));
    memset(rmd160s,0,sizeof(rmd160s));
    memset(kmdheights,0,sizeof(kmdheights));
    memset(otherheights,0,sizeof(otherheights));
    txn_count = block.vtx.size();
    if ( ASSETCHAINS_SYMBOL[0] == 0 )
    {
        for (i=0; i<txn_count; i++)
        {
            if ( i == 0 && txn_count > 1 && block.vtx[txn_count-1].vout.size() > 0 && block.vtx[txn_count-1].vout[0].nValue == 5000 )
            {
                /*
                if ( block.vtx[txn_count-1].vin.size() == 1 && GetTransaction(block.vtx[txn_count-1].vin[0].prevout.hash,tx,hash,false) && block.vtx[0].vout[0].scriptPubKey == tx.vout[block.vtx[txn_count-1].vin[0].prevout.n].scriptPubKey )
                    notmatched = 1;
                */
                if ( block.vtx[txn_count-1].vin.size() == 1 ) {
                    uint256 hashNotaryProofVin = block.vtx[txn_count-1].vin[0].prevout.hash;
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
                    if ( fNotaryProofVinTxFound && block.vtx[0].vout[0].scriptPubKey == tx.vout[block.vtx[txn_count-1].vin[0].prevout.n].scriptPubKey )
                        {
                            notmatched = 1;
                        }
                }  
            }
            n = block.vtx[i].vin.size();
            for (j=0; j<n; j++)
            {
                for (k=0; k<numbanned; k++)
                {
                    if ( block.vtx[i].vin[j].prevout.hash == array[k] && komodo_checkvout(block.vtx[i].vin[j].prevout.n,k,indallvouts) != 0 )
                    {
                        printf("banned tx.%d being used at ht.%d txi.%d vini.%d\n",k,height,i,j);
                        return(-1);
                    }
                }
            }
        }
    }
    // we don't want these checks in VRSC, leave it at the Sapling upgrade
    if ( ASSETCHAINS_SYMBOL[0] == 0 ||
         ((ASSETCHAINS_COMMISSION != 0 || ASSETCHAINS_FOUNDERS_REWARD) && height > 1) ||
         NetworkUpgradeActive(height, Params().GetConsensus(), Consensus::UPGRADE_SAPLING) )
    {
        n = block.vtx[0].vout.size();
        int64_t val,prevtotal = 0; int32_t strangeout=0,overflow = 0;
        total = 0;
        for (i=1; i<n; i++)
        {
            script = (uint8_t *)&block.vtx[0].vout[i].scriptPubKey[0];
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
        if ( ASSETCHAINS_SYMBOL[0] == 0 )
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
                script = (uint8_t *)&block.vtx[0].vout[0].scriptPubKey[0];
                //int32_t notary = komodo_electednotary(&num,script+1,height,0);
                //if ( (-1 * (komodo_electednotary(&num,script+1,height,0) >= 0) * (height > 1000000)) < 0 )
                //    fprintf(stderr, ">>>>>>> FAILED BLOCK.%d notary.%d insync.%d\n",height,notary,KOMODO_INSYNC);
                //else
                //    fprintf(stderr, "<<<<<<< VALID BLOCK.%d notary.%d insync.%d\n",height,notary,KOMODO_INSYNC);
                return(-1 * (komodo_electednotary(&num,script+1,height,0) >= 0) * (height > 1000000));
            }
        }
        else
        {
            checktoshis = 0;
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
                    fprintf(stderr,">>>>>>>>>>>>> %s DUST ht.%d strangeout.%d notmatched.%d <<<<<<<<<\n",ASSETCHAINS_SYMBOL,height,strangeout,notmatched);
                return(-1);
            }
        }
    }
    return(0);
}

const char *komodo_opreturn(int32_t height,uint64_t value,uint8_t *opretbuf,int32_t opretlen,
        uint256 txid,uint16_t vout,char *source)
{
    int32_t tokomodo;
    const char *typestr = "unknown";

    if ( ASSETCHAINS_SYMBOL[0] != 0 && komodo_baseid(ASSETCHAINS_SYMBOL) < 0 && opretbuf[0] != 'K' )
    {
        return("assetchain");
    }
    tokomodo = (komodo_is_issuer() == 0);
    if ( opretbuf[0] == 'K' && opretlen != 40 )
    {
        komodo_kvupdate(opretbuf,opretlen,value);
        return("kv");
    }
    return typestr;
}

int32_t komodo_parsestatefiledata(struct komodo_state *sp,uint8_t *filedata,long *fposp,long datalen,char *symbol,char *dest);

void komodo_stateind_set(struct komodo_state *sp,uint32_t *inds,int32_t n,uint8_t *filedata,long datalen,char *symbol,char *dest)
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
                        //printf("issue %c total.%d lastfpos.%ld\n",func,count,lastfpos);
                        komodo_parsestatefiledata(sp,filedata,&lastfpos,datalen,symbol,dest);
                        count++;
                    }
                }
            }
            lastfpos = fpos;
        }
    }
    printf("numR.%d numV.%d numN.%d count.%d\n",numR,numV,numN,count);
    /*else if ( func == 'K' ) // KMD height: stop after 1st
    else if ( func == 'T' ) // KMD height+timestamp: stop after 1st

    else if ( func == 'N' ) // notarization, scan backwards 1440+ blocks;
    else if ( func == 'V' ) // price feed: can stop after 1440+
    else if ( func == 'R' ) // opreturn:*/
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

long komodo_stateind_validate(struct komodo_state *sp,char *indfname,uint8_t *filedata,long datalen,uint32_t *prevpos100p,uint32_t *indcounterp,char *symbol,char *dest)
{
    FILE *fp; long fsize,lastfpos=0,fpos=0; uint8_t *inds,func; int32_t i,n; uint32_t offset,tmp,prevpos100 = 0;
    *indcounterp = *prevpos100p = 0;
    if ( (inds= OS_fileptr(&fsize,indfname)) != 0 )
    {
        lastfpos = 0;
        fprintf(stderr,"inds.%p validate %s fsize.%ld datalen.%ld n.%d lastfpos.%ld\n",inds,indfname,fsize,datalen,(int32_t)(fsize / sizeof(uint32_t)),lastfpos);
        if ( (fsize % sizeof(uint32_t)) == 0 )
        {
            n = (int32_t)(fsize / sizeof(uint32_t));
            for (i=0; i<n; i++)
            {
                memcpy(&tmp,&inds[i * sizeof(uint32_t)],sizeof(uint32_t));
                if ( 0 && i > n-10 )
                    printf("%d: tmp.%08x [%c] prevpos100.%u\n",i,tmp,tmp&0xff,prevpos100);
                if ( (i % 100) == 0 )
                    prevpos100 = tmp;
                else
                {
                    func = (tmp & 0xff);
                    offset = (tmp >> 8);
                    fpos = prevpos100 + offset;
                    if ( lastfpos >= datalen || filedata[lastfpos] != func )
                    {
                        printf("validate.%d error (%u %d) prev100 %u -> fpos.%ld datalen.%ld [%d] (%c) vs (%c) lastfpos.%ld\n",i,offset,func,prevpos100,fpos,datalen,lastfpos < datalen ? filedata[lastfpos] : -1,func,filedata[lastfpos],lastfpos);
                        return(-1);
                    }
                }
                lastfpos = fpos;
            }
            *indcounterp = n;
            *prevpos100p = prevpos100;
            if ( sp != 0 )
                komodo_stateind_set(sp,(uint32_t *)inds,n,filedata,fpos,symbol,dest);
            //printf("free inds.%p %s validated[%d] fpos.%ld datalen.%ld, offset %ld vs fsize.%ld\n",inds,indfname,i,fpos,datalen,i * sizeof(uint32_t),fsize);
            free(inds);
            return(fpos);
        } else printf("wrong filesize %s %ld\n",indfname,fsize);
    }
    free(inds);
    fprintf(stderr,"indvalidate return -1\n");
    return(-1);
}

long komodo_indfile_update(FILE *indfp,uint32_t *prevpos100p,long lastfpos,long newfpos,uint8_t func,uint32_t *indcounterp)
{
    uint32_t tmp;
    if ( indfp != 0 )
    {
        tmp = ((uint32_t)(newfpos - *prevpos100p) << 8) | (func & 0xff);
        if ( ftell(indfp)/sizeof(uint32_t) != *indcounterp )
            printf("indfp fpos %ld -> ind.%ld vs counter.%u\n",ftell(indfp),ftell(indfp)/sizeof(uint32_t),*indcounterp);
        //fprintf(stderr,"ftell.%ld indcounter.%u lastfpos.%ld newfpos.%ld func.%02x\n",ftell(indfp),*indcounterp,lastfpos,newfpos,func);
        fwrite(&tmp,1,sizeof(tmp),indfp), (*indcounterp)++;
        if ( (*indcounterp % 100) == 0 )
        {
            *prevpos100p = (uint32_t)newfpos;
            fwrite(prevpos100p,1,sizeof(*prevpos100p),indfp), (*indcounterp)++;
        }
    }
    return(newfpos);
}

int32_t komodo_faststateinit(struct komodo_state *sp,const char *fname,char *symbol,char *dest)
{
    FILE *indfp; char indfname[1024]; uint8_t *filedata; long validated=-1,datalen,fpos,lastfpos; uint32_t tmp,prevpos100,indcounter,starttime; int32_t func,finished = 0;
    starttime = (uint32_t)time(NULL);
    safecopy(indfname,fname,sizeof(indfname)-4);
    strcat(indfname,".ind");
    if ( (filedata= OS_fileptr(&datalen,fname)) != 0 )
    {
        if ( 1 )//datalen >= (1LL << 32) || GetArg("-genind",0) != 0 || (validated= komodo_stateind_validate(0,indfname,filedata,datalen,&prevpos100,&indcounter,symbol,dest)) < 0 )
        {
            lastfpos = fpos = 0;
            indcounter = prevpos100 = 0;
            if ( (indfp= fopen(indfname,"wb")) != 0 )
                fwrite(&prevpos100,1,sizeof(prevpos100),indfp), indcounter++;
            fprintf(stderr,"processing %s %ldKB, validated.%ld\n",fname,datalen/1024,validated);
            while ( (func= komodo_parsestatefiledata(sp,filedata,&fpos,datalen,symbol,dest)) >= 0 )
            {
                lastfpos = komodo_indfile_update(indfp,&prevpos100,lastfpos,fpos,func,&indcounter);
            }
            if ( indfp != 0 )
            {
                fclose(indfp);
                if ( (fpos= komodo_stateind_validate(0,indfname,filedata,datalen,&prevpos100,&indcounter,symbol,dest)) < 0 )
                    printf("unexpected komodostate.ind validate failure %s datalen.%ld\n",indfname,datalen);
                else printf("%s validated fpos.%ld\n",indfname,fpos);
            }
            finished = 1;
            fprintf(stderr,"took %d seconds to process %s %ldKB\n",(int32_t)(time(NULL)-starttime),fname,datalen/1024);
        }
        else if ( validated > 0 )
        {
            if ( (indfp= fopen(indfname,"rb+")) != 0 )
            {
                lastfpos = fpos = validated;
                fprintf(stderr,"datalen.%ld validated %ld -> indcounter %u, prevpos100 %u offset.%d\n",datalen,validated,indcounter,prevpos100,(int32_t)(indcounter * sizeof(uint32_t)));
                if ( fpos < datalen )
                {
                    fseek(indfp,indcounter * sizeof(uint32_t),SEEK_SET);
                    if ( ftell(indfp) == indcounter * sizeof(uint32_t) )
                    {
                        while ( (func= komodo_parsestatefiledata(sp,filedata,&fpos,datalen,symbol,dest)) >= 0 )
                        {
                            lastfpos = komodo_indfile_update(indfp,&prevpos100,lastfpos,fpos,func,&indcounter);
                            if ( lastfpos != fpos )
                                fprintf(stderr,"unexpected lastfpos.%ld != %ld\n",lastfpos,fpos);
                        }
                    }
                    fclose(indfp);
                }
                if ( komodo_stateind_validate(sp,indfname,filedata,datalen,&prevpos100,&indcounter,symbol,dest) < 0 )
                    printf("unexpected komodostate.ind validate failure %s datalen.%ld\n",indfname,datalen);
                else
                {
                    printf("%s validated updated from validated.%ld to %ld new.[%ld] -> indcounter %u, prevpos100 %u offset.%ld | elapsed %d seconds\n",indfname,validated,fpos,fpos-validated,indcounter,prevpos100,indcounter * sizeof(uint32_t),(int32_t)(time(NULL) - starttime));
                    finished = 1;
                }
            }
        } else printf("komodo_faststateinit unexpected case\n");
        free(filedata);
        return(finished == 1);
    }
    return(-1);
}

uint64_t komodo_interestsum();

void komodo_passport_iteration()
{
    static long lastpos[34]; static char userpass[33][1024]; static uint32_t lasttime,callcounter,lastinterest;
    int32_t maxseconds = 10;
    FILE *fp; uint8_t *filedata; long fpos,datalen,lastfpos; int32_t baseid,limit,n,ht,isrealtime,expired,refid,blocks,longest; struct komodo_state *sp,*refsp; char *retstr,fname[512],*base,symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; uint32_t buf[3],starttime; uint64_t RTmask = 0; //CBlockIndex *pindex;
    expired = 0;
    while ( 0 && KOMODO_INITDONE == 0 )
    {
        fprintf(stderr,"[%s] PASSPORT iteration waiting for KOMODO_INITDONE\n",ASSETCHAINS_SYMBOL);
        sleep(3);
    }
    if ( komodo_chainactive_timestamp() > lastinterest )
    {
        if ( ASSETCHAINS_SYMBOL[0] == 0 )
            komodo_interestsum();
        //komodo_longestchain();
        lastinterest = komodo_chainactive_timestamp();
    }
    refsp = komodo_stateptr(symbol,dest);
    if ( ASSETCHAINS_SYMBOL[0] == 0 || strcmp(ASSETCHAINS_SYMBOL,"KMDCC") == 0 )
    {
        refid = 33;
        limit = 10000000;
        jumblr_iteration();
    }
    else
    {
        limit = 10000000;
        refid = komodo_baseid(ASSETCHAINS_SYMBOL)+1; // illegal base -> baseid.-1 -> 0
        if ( refid == 0 )
        {
            KOMODO_PASSPORT_INITDONE = 1;
            return;
        }
    }
    starttime = (uint32_t)time(NULL);
    if ( callcounter++ < 1 )
        limit = 10000;
    lasttime = starttime;
    for (baseid=32; baseid>=0; baseid--)
    {
        if ( time(NULL) >= starttime+maxseconds )
            break;
        sp = 0;
        isrealtime = 0;
        base = (char *)CURRENCIES[baseid];
        //printf("PASSPORT %s baseid+1 %d refid.%d\n",ASSETCHAINS_SYMBOL,baseid+1,refid);
        if ( baseid+1 != refid ) // only need to import state from a different coin
        {
            if ( baseid == 32 ) // only care about KMD's state
            {
                refsp->RTmask &= ~(1LL << baseid);
                komodo_statefname(fname,baseid<32?base:(char *)"",(char *)"komodostate");
                komodo_nameset(symbol,dest,base);
                sp = komodo_stateptrget(symbol);
                n = 0;
                if ( lastpos[baseid] == 0 && (filedata= OS_fileptr(&datalen,fname)) != 0 )
                {
                    fpos = 0;
                    fprintf(stderr,"%s processing %s %ldKB\n",ASSETCHAINS_SYMBOL,fname,datalen/1024);
                    while ( komodo_parsestatefiledata(sp,filedata,&fpos,datalen,symbol,dest) >= 0 )
                        lastfpos = fpos;
                    fprintf(stderr,"%s took %d seconds to process %s %ldKB\n",ASSETCHAINS_SYMBOL,(int32_t)(time(NULL)-starttime),fname,datalen/1024);
                    lastpos[baseid] = lastfpos;
                    free(filedata), filedata = 0;
                    datalen = 0;
                }
                else if ( (fp= fopen(fname,"rb")) != 0 && sp != 0 )
                {
                    fseek(fp,0,SEEK_END);
                    //fprintf(stderr,"couldnt OS_fileptr(%s), freading %ldKB\n",fname,ftell(fp)/1024);
                    if ( ftell(fp) > lastpos[baseid] )
                    {
                        if ( ASSETCHAINS_SYMBOL[0] != 0 )
                            printf("%s passport refid.%d %s fname.(%s) base.%s %ld %ld\n",ASSETCHAINS_SYMBOL,refid,symbol,fname,base,ftell(fp),lastpos[baseid]);
                        fseek(fp,lastpos[baseid],SEEK_SET);
                        while ( komodo_parsestatefile(sp,fp,symbol,dest) >= 0 && n < limit )
                        {
                            if ( n == limit-1 )
                            {
                                if ( time(NULL) < starttime+maxseconds )
                                    n = 0;
                                else
                                {
                                    //printf("expire passport loop %s -> %s at %ld\n",ASSETCHAINS_SYMBOL,base,lastpos[baseid]);
                                    expired++;
                                }
                            }
                            n++;
                        }
                        lastpos[baseid] = ftell(fp);
                        if ( 0 && lastpos[baseid] == 0 && strcmp(symbol,"KMD") == 0 )
                            printf("from.(%s) lastpos[%s] %ld isrt.%d\n",ASSETCHAINS_SYMBOL,CURRENCIES[baseid],lastpos[baseid],komodo_isrealtime(&ht));
                    } //else fprintf(stderr,"%s.%ld ",CURRENCIES[baseid],ftell(fp));
                    fclose(fp);
                } else fprintf(stderr,"load error.(%s) %p\n",fname,sp);
                komodo_statefname(fname,baseid<32?base:(char *)"",(char *)"realtime");
                if ( (fp= fopen(fname,"rb")) != 0 )
                {
                    if ( fread(buf,1,sizeof(buf),fp) == sizeof(buf) )
                    {
                        sp->CURRENT_HEIGHT = buf[0];
                        if ( buf[0] != 0 && buf[0] >= buf[1] && buf[2] > time(NULL)-60 )
                        {
                            isrealtime = 1;
                            RTmask |= (1LL << baseid);
                            memcpy(refsp->RTbufs[baseid+1],buf,sizeof(refsp->RTbufs[baseid+1]));
                        }
                    }
                    fclose(fp);
                }
            }
        }
        else
        {
            refsp->RTmask &= ~(1LL << baseid);
            komodo_statefname(fname,baseid<32?base:(char *)"",(char *)"realtime");
            if ( (fp= fopen(fname,"wb")) != 0 )
            {
                buf[0] = (uint32_t)chainActive.LastTip()->nHeight;
                buf[1] = (uint32_t)komodo_longestchain();
                if ( buf[0] != 0 && buf[0] == buf[1] )
                {
                    buf[2] = (uint32_t)time(NULL);
                    RTmask |= (1LL << baseid);
                    memcpy(refsp->RTbufs[baseid+1],buf,sizeof(refsp->RTbufs[baseid+1]));
                    if ( refid != 0 )
                        memcpy(refsp->RTbufs[0],buf,sizeof(refsp->RTbufs[0]));
                }
                if ( fwrite(buf,1,sizeof(buf),fp) != sizeof(buf) )
                    fprintf(stderr,"[%s] %s error writing realtime\n",ASSETCHAINS_SYMBOL,base);
                fclose(fp);
            } else fprintf(stderr,"%s create error RT\n",base);
        }
        if ( sp != 0 && isrealtime == 0 )
            refsp->RTbufs[0][2] = 0;
    }
    refsp->RTmask |= RTmask;
    if ( expired == 0 && KOMODO_PASSPORT_INITDONE == 0 )
    {
        KOMODO_PASSPORT_INITDONE = 1;
        printf("READY for %s RPC calls at %u! done PASSPORT %s refid.%d\n",ASSETCHAINS_SYMBOL,(uint32_t)time(NULL),ASSETCHAINS_SYMBOL,refid);
    }
}

void komodo_PriceCache_shift()
{
    int32_t i;
    for (i=KOMODO_LOCALPRICE_CACHESIZE-1; i>0; i--)
        memcpy(PriceCache[i],PriceCache[i-1],sizeof(PriceCache[i]));
    memcpy(PriceCache[0],Mineropret.data(),Mineropret.size());
}

int32_t _komodo_heightpricebits(uint64_t *seedp,uint32_t *heightbits,CBlock *block)
{
    CTransaction tx; int32_t numvouts; std::vector<uint8_t> vopret;
    tx = block->vtx[0];
    numvouts = (int32_t)tx.vout.size();
    GetOpReturnData(tx.vout[numvouts-1].scriptPubKey,vopret);
    if ( vopret.size() >= PRICES_SIZEBIT0 )
    {
        if ( seedp != 0 )
            memcpy(seedp,&block->hashMerkleRoot,sizeof(*seedp));
        memcpy(heightbits,vopret.data(),vopret.size());
        return((int32_t)(vopret.size()/sizeof(uint32_t)));
    }
    return(-1);
}

// komodo_heightpricebits() extracts the price data in the coinbase for nHeight
int32_t komodo_heightpricebits(uint64_t *seedp,uint32_t *heightbits,int32_t nHeight)
{
    CBlockIndex *pindex; CBlock block;
    if ( seedp != 0 )
        *seedp = 0;
    if ( (pindex= komodo_chainactive(nHeight)) != 0 )
    {
        if ( komodo_blockload(block,pindex) == 0 )
        {
            return(_komodo_heightpricebits(seedp,heightbits,&block));
        }
    }
    fprintf(stderr,"couldnt get pricebits for %d\n",nHeight);
    return(-1);
}

/*
 komodo_pricenew() is passed in a reference price, the change tolerance and the proposed price. it needs to return a clipped price if it is too big and also set a flag if it is at or above the limit
 */
uint32_t komodo_pricenew(char *maxflagp,uint32_t price,uint32_t refprice,int64_t tolerance)
{
    uint64_t highprice,lowprice;
    if ( refprice < 2 )
        return(0);
    highprice = ((uint64_t)refprice * (COIN + tolerance)) / COIN; // calc highest acceptable price
    lowprice = ((uint64_t)refprice * (COIN - tolerance)) / COIN;  // and lowest
    if ( highprice == refprice )
        highprice++;
    if ( lowprice == refprice )
        lowprice--;
    if ( price >= highprice )
    {
        //fprintf(stderr,"high %u vs h%llu l%llu tolerance.%llu\n",price,(long long)highprice,(long long)lowprice,(long long)tolerance);
        if ( price > highprice ) // return non-zero only if we violate the tolerance
        {
            *maxflagp = 2;
            return(highprice);
        }
        *maxflagp = 1;
    }
    else if ( price <= lowprice )
    {
        //fprintf(stderr,"low %u vs h%llu l%llu tolerance.%llu\n",price,(long long)highprice,(long long)lowprice,(long long)tolerance);
        if ( price < lowprice )
        {
            *maxflagp = -2;
            return(lowprice);
        }
        *maxflagp = -1;
    }
    return(0);
}

/**
 * @brief 
 * 
 * @param nHeight 
 * @param n 
 * @param maxflags 
 * @param pricebitsA 
 * @param pricebitsB 
 * @param tolerance 
 * @return  -1 if any of the prices are beyond the tolerance
 */
int32_t komodo_pricecmp(int32_t nHeight,int32_t n,char *maxflags,uint32_t *pricebitsA,uint32_t *pricebitsB,int64_t tolerance)
{
    int32_t i; uint32_t newprice;
    for (i=1; i<n; i++)
    {
        if ( (newprice= komodo_pricenew(&maxflags[i],pricebitsA[i],pricebitsB[i],tolerance)) != 0 )
        {
            fprintf(stderr,"ht.%d i.%d/%d %u vs %u -> newprice.%u out of tolerance maxflag.%d\n",nHeight,i,n,pricebitsB[i],pricebitsA[i],newprice,maxflags[i]);
            return(-1);
        }
    }
    return(0);
}

// komodo_priceclamp() clamps any price that is beyond tolerance
int32_t komodo_priceclamp(int32_t n,uint32_t *pricebits,uint32_t *refprices,int64_t tolerance)
{
    int32_t i; uint32_t newprice; char maxflags[KOMODO_MAXPRICES];
    memset(maxflags,0,sizeof(maxflags));
    for (i=1; i<n; i++)
    {
        if ( (newprice= komodo_pricenew(&maxflags[i],pricebits[i],refprices[i],tolerance)) != 0 )
        {
            fprintf(stderr,"priceclamped[%d of %d] %u vs %u -> %u\n",i,n,refprices[i],pricebits[i],newprice);
            pricebits[i] = newprice;
        }
    }
    return(0);
}

/**
 * @brief build pricedata
 * @param nHeight the height
 * @returns a valid pricedata to add to the coinbase opreturn for nHeight
 */
CScript komodo_mineropret(int32_t nHeight)
{
    CScript opret; 

    if ( Mineropret.size() >= PRICES_SIZEBIT0 )
    {
        uint32_t pricebits[KOMODO_MAXPRICES];

        int32_t n = (int32_t)(Mineropret.size() / sizeof(uint32_t));
        int32_t numzero = 1;
        while ( numzero > 0 )
        {
            memcpy(pricebits,Mineropret.data(),Mineropret.size());
            for (int32_t i=numzero=0; i<n; i++)
                if ( pricebits[i] == 0 )
                {
                    fprintf(stderr,"%d ",i);
                    numzero++;
                }
            if ( numzero != 0 )
            {
                fprintf(stderr," komodo_mineropret numzero.%d vs n.%d\n",numzero,n);
                komodo_cbopretupdate(1);
                sleep(61);
            }
        }

        uint32_t prevbits[KOMODO_MAXPRICES]; 
        if ( komodo_heightpricebits(0,prevbits,nHeight-1) > 0 )
        {
            memcpy(pricebits,Mineropret.data(),Mineropret.size());
            char maxflags[KOMODO_MAXPRICES]; 
            memset(maxflags,0,sizeof(maxflags));
            if ( komodo_pricecmp(0,n,maxflags,pricebits,prevbits,PRICES_ERRORRATE) < 0 )
            {
                // if the new prices are outside tolerance, update Mineropret with clamped prices
                komodo_priceclamp(n,pricebits,prevbits,PRICES_ERRORRATE);
                //fprintf(stderr,"update Mineropret to clamped prices\n");
                memcpy(Mineropret.data(),pricebits,Mineropret.size());
            }
        }
        for (int32_t i=0; i<Mineropret.size(); i++)
            fprintf(stderr,"%02x",Mineropret[i]);
        fprintf(stderr," <- Mineropret\n");
        return(opret << OP_RETURN << Mineropret);
    }
    return opret;
}

void komodo_queuelocalprice(int32_t dir,int32_t height,uint32_t timestamp,uint256 blockhash,
        int32_t ind,uint32_t pricebits)
{
    fprintf(stderr,"ExtremePrice dir.%d ht.%d ind.%d cmpbits.%u\n",dir,height,ind,pricebits);
    ExtremePrice.dir = dir;
    ExtremePrice.height = height;
    ExtremePrice.blockhash = blockhash;
    ExtremePrice.ind = ind;
    ExtremePrice.timestamp = timestamp;
    ExtremePrice.pricebits = pricebits;
}

/**
 * @brief komodo_opretvalidate() is the entire price validation!
 * it prints out some useful info for debugging, like the lag from current time and prev block and the prices encoded in the opreturn.
 * 
 * The only way komodo_opretvalidate() doesnt return an error is if maxflag is set or it is within tolerance of both the prior block and the local data. The local data validation only happens if it is a recent block and not a block from the past as the local node is only getting the current price data.
*/
int32_t komodo_opretvalidate(const CBlock *block,CBlockIndex * const previndex,int32_t nHeight,CScript scriptPubKey)
{
    int32_t testchain_exemption = 0;
    std::vector<uint8_t> vopret; char maxflags[KOMODO_MAXPRICES]; uint256 bhash; double btcusd,btcgbp,btceur; uint32_t localbits[KOMODO_MAXPRICES],pricebits[KOMODO_MAXPRICES],prevbits[KOMODO_MAXPRICES],newprice; int32_t i,j,prevtime,maxflag,lag,lag2,lag3,n,errflag,iter; uint32_t now;
    now = (uint32_t)time(NULL);
    if ( ASSETCHAINS_CBOPRET != 0 && nHeight > 0 )
    {
        bhash = block->GetHash();
        GetOpReturnData(scriptPubKey,vopret);
        if ( vopret.size() >= PRICES_SIZEBIT0 )
        {
            n = (int32_t)(vopret.size() / sizeof(uint32_t));
            memcpy(pricebits,vopret.data(),Mineropret.size());
            memset(maxflags,0,sizeof(maxflags));
            if ( nHeight > 2 )
            {
                prevtime = previndex->nTime;
                lag = (int32_t)(now - pricebits[0]);
                lag2 = (int32_t)(pricebits[0] - prevtime);
                lag3 = (int32_t)(block->nTime - pricebits[0]);
                if ( lag < -60 ) // avoid data from future
                {
                    fprintf(stderr,"A ht.%d now.%u htstamp.%u %u - pricebits[0] %u -> lags.%d %d %d\n",nHeight,now,prevtime,block->nTime,pricebits[0],lag,lag2,lag3);
                    return(-1);
                }
                if ( lag2 < -60 ) //testchain_exemption ) // must be close to last block timestamp
                {
                    fprintf(stderr,"B ht.%d now.%u htstamp.%u %u - pricebits[0] %u -> lags.%d %d %d vs %d cmp.%d\n",nHeight,now,prevtime,block->nTime,pricebits[0],lag,lag2,lag3,ASSETCHAINS_BLOCKTIME,lag2<-ASSETCHAINS_BLOCKTIME);
                    if ( nHeight > testchain_exemption )
                        return(-1);
                }
                if ( lag3 < -60 || lag3 > ASSETCHAINS_BLOCKTIME )
                {
                    fprintf(stderr,"C ht.%d now.%u htstamp.%u %u - pricebits[0] %u -> lags.%d %d %d\n",nHeight,now,prevtime,block->nTime,pricebits[0],lag,lag2,lag3);
                    if ( nHeight > testchain_exemption )
                        return(-1);
                }
                btcusd = (double)pricebits[1]/10000;
                btcgbp = (double)pricebits[2]/10000;
                btceur = (double)pricebits[3]/10000;
                fprintf(stderr,"ht.%d: lag.%d %.4f USD, %.4f GBP, %.4f EUR, GBPUSD %.6f, EURUSD %.6f, EURGBP %.6f [%d]\n",nHeight,lag,btcusd,btcgbp,btceur,btcusd/btcgbp,btcusd/btceur,btcgbp/btceur,lag2);
                if ( komodo_heightpricebits(0,prevbits,nHeight-1) > 0 )
                {
                    if ( nHeight < testchain_exemption )
                    {
                        for (i=0; i<n; i++)
                            if ( pricebits[i] == 0 )
                                pricebits[i] = prevbits[i];
                    }
                    if ( komodo_pricecmp(nHeight,n,maxflags,pricebits,prevbits,PRICES_ERRORRATE) < 0 )
                    {
                        for (i=1; i<n; i++)
                            fprintf(stderr,"%.4f ",(double)prevbits[i]/10000);
                        fprintf(stderr," oldprices.%d\n",nHeight);
                        for (i=1; i<n; i++)
                            fprintf(stderr,"%.4f ",(double)pricebits[i]/10000);
                        fprintf(stderr," newprices.%d\n",nHeight);

                        fprintf(stderr,"vs prev maxflag.%d cmp error\n",maxflag);
                        return(-1);
                    } // else this is the good case we hope to happen
                } else return(-1);
                if ( lag < ASSETCHAINS_BLOCKTIME && Mineropret.size() >= PRICES_SIZEBIT0 )
                {
                    memcpy(localbits,Mineropret.data(),Mineropret.size());
                    if ( nHeight < testchain_exemption )
                    {
                        for (i=0; i<n; i++)
                            if ( localbits[i] == 0 )
                                localbits[i] = prevbits[i];
                    }
                    for (iter=0; iter<2; iter++) // first iter should just refresh prices if out of tolerance
                    {
                        for (i=1; i<n; i++)
                        {
                            if ( (maxflag= maxflags[i]) != 0 && localbits[i] != 0 )
                            {
                                // make sure local price is moving in right direction
                                fprintf(stderr,"maxflag.%d i.%d localbits.%u vs pricebits.%u prevbits.%u\n",maxflag,i,localbits[i],pricebits[i],prevbits[i]);
                                if ( maxflag > 0 && localbits[i] < prevbits[i] )
                                {
                                    if ( iter == 0 )
                                        break;
                                    // second iteration checks recent prices to see if within local volatility
                                    for (j=0; j<KOMODO_LOCALPRICE_CACHESIZE; j++)
                                        if ( PriceCache[j][i] >= prevbits[i] )
                                        {
                                            fprintf(stderr,"i.%d within recent localprices[%d] %u >= %u\n",i,j,PriceCache[j][i],prevbits[i]);
                                            break;
                                        }
                                    if ( j == KOMODO_LOCALPRICE_CACHESIZE )
                                    {
                                        komodo_queuelocalprice(1,nHeight,block->nTime,bhash,i,prevbits[i]);
                                        break;
                                    }
                                }
                                else if ( maxflag < 0 && localbits[i] > prevbits[i] )
                                {
                                    if ( iter == 0 )
                                        break;
                                    for (j=0; j<KOMODO_LOCALPRICE_CACHESIZE; j++)
                                        if ( PriceCache[j][i] <= prevbits[i] )
                                        {
                                            fprintf(stderr,"i.%d within recent localprices[%d] %u <= prev %u\n",i,j,PriceCache[j][i],prevbits[i]);
                                            break;
                                        }
                                    if ( j == KOMODO_LOCALPRICE_CACHESIZE )
                                    {
                                        komodo_queuelocalprice(-1,nHeight,block->nTime,bhash,i,prevbits[i]);
                                        break;
                                    }
                                }
                            }
                        }
                        if ( i != n )
                        {
                            if ( iter == 0 )
                            {
                                fprintf(stderr,"force update prices\n");
                                komodo_cbopretupdate(1);
                                memcpy(localbits,Mineropret.data(),Mineropret.size());
                            } else return(-1);
                        }
                    }
                }
            }
            if ( bhash == ExtremePrice.blockhash )
            {
                fprintf(stderr,"approved a previously extreme price based on new data ht.%d vs %u vs %u\n",ExtremePrice.height,ExtremePrice.timestamp,(uint32_t)block->nTime);
                memset(&ExtremePrice,0,sizeof(ExtremePrice));
            }
            return(0);
        } else fprintf(stderr,"wrong size %d vs %d, scriptPubKey size %d [%02x]\n",(int32_t)vopret.size(),(int32_t)Mineropret.size(),(int32_t)scriptPubKey.size(),scriptPubKey[0]);
        return(-1);
    }
    return(0);
}

char *nonportable_path(char *str)
{
    int32_t i;
    for (i=0; str[i]!=0; i++)
        if ( str[i] == '/' )
            str[i] = '\\';
    return(str);
}

char *portable_path(char *str)
{
#ifdef _WIN32
    return(nonportable_path(str));
#else
#ifdef __PNACL
    /*int32_t i,n;
     if ( str[0] == '/' )
     return(str);
     else
     {
     n = (int32_t)strlen(str);
     for (i=n; i>0; i--)
     str[i] = str[i-1];
     str[0] = '/';
     str[n+1] = 0;
     }*/
#endif
    return(str);
#endif
}

void *loadfile(char *fname,uint8_t **bufp,long *lenp,long *allocsizep)
{
    FILE *fp;
    long  filesize,buflen = *allocsizep;
    uint8_t *buf = *bufp;
    *lenp = 0;
    if ( (fp= fopen(portable_path(fname),"rb")) != 0 )
    {
        fseek(fp,0,SEEK_END);
        filesize = ftell(fp);
        if ( filesize == 0 )
        {
            fclose(fp);
            *lenp = 0;
            //printf("loadfile null size.(%s)\n",fname);
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

void *filestr(long *allocsizep,char *_fname)
{
    long filesize = 0; char *fname,*buf = 0; void *retptr;
    *allocsizep = 0;
    fname = (char *)malloc(strlen(_fname)+1);
    strcpy(fname,_fname);
    retptr = loadfile(fname,(uint8_t **)&buf,&filesize,allocsizep);
    free(fname);
    return(retptr);
}

cJSON *send_curl(char *url,char *fname)
{
    long fsize; char curlstr[1024],*jsonstr; cJSON *json=0;
    sprintf(curlstr,"wget -q \"%s\" -O %s",url,fname);
    if ( system(curlstr) == 0 )
    {
        if ( (jsonstr= (char *)filestr((long *)&fsize,fname)) != 0 )
        {
            json = cJSON_Parse(jsonstr);
            free(jsonstr);
        }
    }
    return(json);
}

// get_urljson just returns the JSON returned by the URL using issue_curl


/*
const char *Techstocks[] =
{ "AAPL","ADBE","ADSK","AKAM","AMD","AMZN","ATVI","BB","CDW","CRM","CSCO","CYBR","DBX","EA","FB","GDDY","GOOG","GRMN","GSAT","HPQ","IBM","INFY","INTC","INTU","JNPR","MSFT","MSI","MU","MXL","NATI","NCR","NFLX","NTAP","NVDA","ORCL","PANW","PYPL","QCOM","RHT","S","SHOP","SNAP","SPOT","SYMC","SYNA","T","TRIP","TWTR","TXN","VMW","VOD","VRSN","VZ","WDC","XRX","YELP","YNDX","ZEN"
};
const char *Metals[] = { "XAU", "XAG", "XPT", "XPD", };

const char *Markets[] = { "DJIA", "SPX", "NDX", "VIX" };
*/

cJSON *get_urljson(char *url)
{
    char *jsonstr; cJSON *json = 0;
    if ( (jsonstr= issue_curl(url)) != 0 )
    {
        //fprintf(stderr,"(%s) -> (%s)\n",url,jsonstr);
        json = cJSON_Parse(jsonstr);
        free(jsonstr);
    }
    return(json);
}

int32_t get_stockprices(uint32_t now,uint32_t *prices,std::vector<std::string> symbols)
{
    char url[32768],*symbol,*timestr; cJSON *json,*obj; int32_t i,n=0,retval=-1; uint32_t uprice,timestamp;
    sprintf(url,"https://api.iextrading.com/1.0/tops/last?symbols=%s",GetArg("-ac_stocks","").c_str());
    fprintf(stderr,"url.(%s)\n",url);
    if ( (json= get_urljson(url)) != 0 ) //if ( (json= send_curl(url,(char *)"iex")) != 0 ) //
    {
        fprintf(stderr,"stocks.(%s)\n",jprint(json,0));
        if ( (n= cJSON_GetArraySize(json)) > 0 )
        {
            retval = n;
            for (i=0; i<n; i++)
            {
                obj = jitem(json,i);
                if ( (symbol= jstr(obj,(char *)"symbol")) != 0 )
                {
                    uprice = jdouble(obj,(char *)"price")*100 + 0.0049;
                    prices[i] = uprice;
                    /*timestamp = j64bits(obj,(char *)"time");
                    if ( timestamp > now+60 || timestamp < now-ASSETCHAINS_BLOCKTIME )
                    {
                        fprintf(stderr,"time error.%d (%u vs %u)\n",timestamp-now,timestamp,now);
                        retval = -1;
                    }*/
                    if ( symbols[i] != symbol )
                    {
                        retval = -1;
                        fprintf(stderr,"MISMATCH.");
                    }
                    fprintf(stderr,"(%s %u) ",symbol,uprice);
                }
            }
            fprintf(stderr,"numstocks.%d\n",n);
        }
        //https://api.iextrading.com/1.0/tops/last?symbols=AAPL -> [{"symbol":"AAPL","price":198.63,"size":100,"time":1555092606076}]
        free_json(json);
    }
    return(retval);
}

uint32_t get_dailyfx(uint32_t *prices)
{
    //{"base":"USD","rates":{"BGN":1.74344803,"NZD":1.471652701,"ILS":3.6329113924,"RUB":65.1997682296,"CAD":1.3430201462,"USD":1.0,"PHP":52.8641469068,"CHF":0.9970582992,"AUD":1.4129078267,"JPY":110.6792654662,"TRY":5.6523444464,"HKD":7.8499732573,"MYR":4.0824567659,"HRK":6.6232840078,"CZK":22.9862720628,"IDR":14267.4986628633,"DKK":6.6551078624,"NOK":8.6806917454,"HUF":285.131039401,"GBP":0.7626582278,"MXN":19.4183455161,"THB":31.8702085933,"ISK":122.5708682475,"ZAR":14.7033339276,"BRL":3.9750401141,"SGD":1.3573720806,"PLN":3.8286682118,"INR":69.33187734,"KRW":1139.1602781244,"RON":4.2423783206,"CNY":6.7387234801,"SEK":9.3385630237,"EUR":0.8914244963},"date":"2019-03-28"}
    char url[512],*datestr; cJSON *json,*rates; int32_t i; uint32_t datenum=0,price = 0;
    sprintf(url,"https://api.openrates.io/latest?base=USD");
    if ( (json= get_urljson(url)) != 0 ) //if ( (json= send_curl(url,(char *)"dailyfx")) != 0 )
    {
        if ( (rates= jobj(json,(char *)"rates")) != 0 )
        {
            for (i=0; i<sizeof(Forex)/sizeof(*Forex); i++)
            {
                price = jdouble(rates,(char *)Forex[i]) * 10000 + 0.000049;
                fprintf(stderr,"(%s %.4f) ",Forex[i],(double)price/10000);
                prices[i] = price;
            }
        }
        if ( (datestr= jstr(json,(char *)"date")) != 0 )
            fprintf(stderr,"(%s)",datestr);
        fprintf(stderr,"\n");
        free_json(json);
    }
    return(datenum);
}

uint32_t get_binanceprice(const char *symbol)
{
    char url[512]; cJSON *json; uint32_t price = 0;
    sprintf(url,"https://api.binance.com/api/v1/ticker/price?symbol=%sBTC",symbol);
    if ( (json= get_urljson(url)) != 0 ) //if ( (json= send_curl(url,(char *)"bnbprice")) != 0 )
    {
        price = jdouble(json,(char *)"price")*SATOSHIDEN + 0.0000000049;
        free_json(json);
    }
    usleep(100000);
    return(price);
}

int32_t get_cryptoprices(uint32_t *prices,const char *list[],int32_t n,std::vector<std::string> strvec)
{
    int32_t i,errs=0; uint32_t price; char *symbol;
    for (i=0; i<n+strvec.size(); i++)
    {
        if ( i < n )
            symbol = (char *)list[i];
        else symbol = (char *)strvec[i - n].c_str();
        if ( (price= get_binanceprice(symbol)) == 0 )
            errs++;
        fprintf(stderr,"(%s %.8f) ",symbol,(double)price/SATOSHIDEN);
        prices[i] = price;
    }
    fprintf(stderr," errs.%d\n",errs);
    return(-errs);
}

// parse the coindesk specific data. yes, if this changes, it will require an update. However, regardless if the format from the data source changes, then the code that extracts it must be changed. One way to mitigate this is to have a large variety of data sources so that there is only a very remote chance that all of them are not available. Certainly the data gathering needs to be made more robust, but it doesnt really affect the proof of concept for the decentralized trustless oracle. The trustlessness is achieved by having all nodes get the oracle data.

int32_t get_btcusd(uint32_t pricebits[4])
{
    cJSON *pjson,*bpi,*obj; char str[512]; double dbtcgbp,dbtcusd,dbtceur; uint64_t btcusd = 0,btcgbp = 0,btceur = 0;
    if ( (pjson= get_urljson((char *)"http://api.coindesk.com/v1/bpi/currentprice.json")) != 0 )
    {
        if ( (bpi= jobj(pjson,(char *)"bpi")) != 0 )
        {
            pricebits[0] = (uint32_t)time(NULL);
            if ( (obj= jobj(bpi,(char *)"USD")) != 0 )
            {
                btcusd = jdouble(obj,(char *)"rate_float") * SATOSHIDEN;
                pricebits[1] = ((btcusd / 10000) & 0xffffffff);
            }
            if ( (obj= jobj(bpi,(char *)"GBP")) != 0 )
            {
                btcgbp = jdouble(obj,(char *)"rate_float") * SATOSHIDEN;
                pricebits[2] = ((btcgbp / 10000) & 0xffffffff);
            }
            if ( (obj= jobj(bpi,(char *)"EUR")) != 0 )
            {
                btceur = jdouble(obj,(char *)"rate_float") * SATOSHIDEN;
                pricebits[3] = ((btceur / 10000) & 0xffffffff);
            }
        }
        free_json(pjson);
        dbtcusd = (double)pricebits[1]/10000;
        dbtcgbp = (double)pricebits[2]/10000;
        dbtceur = (double)pricebits[3]/10000;
        fprintf(stderr,"BTC/USD %.4f, BTC/GBP %.4f, BTC/EUR %.4f GBPUSD %.6f, EURUSD %.6f EURGBP %.6f\n",dbtcusd,dbtcgbp,dbtceur,dbtcusd/dbtcgbp,dbtcusd/dbtceur,dbtcgbp/dbtceur);
        return(0);
    }
    return(-1);
}

/****
 * @brief obtains the external price data and encodes it into Mineropret, 
 * which will then be used by the miner and validation
 * save history, use new data to approve past rejection, where is the auto-reconsiderblock?
 */
int32_t komodo_cbopretsize(uint64_t flags)
{
    int32_t size = 0;
    if ( (ASSETCHAINS_CBOPRET & 1) != 0 )
    {
        size = PRICES_SIZEBIT0;
        if ( (ASSETCHAINS_CBOPRET & 2) != 0 )
            size += (sizeof(Forex)/sizeof(*Forex)) * sizeof(uint32_t);
        if ( (ASSETCHAINS_CBOPRET & 4) != 0 )
            size += (sizeof(Cryptos)/sizeof(*Cryptos) + ASSETCHAINS_PRICES.size()) * sizeof(uint32_t);
        if ( (ASSETCHAINS_CBOPRET & 8) != 0 )
            size += (ASSETCHAINS_STOCKS.size() * sizeof(uint32_t));
    }
    return(size);
}

extern uint256 Queued_reconsiderblock;

void komodo_cbopretupdate(int32_t forceflag)
{
    static uint32_t lasttime,lastbtc,pending;
    static uint32_t pricebits[4],pricebuf[KOMODO_MAXPRICES],forexprices[sizeof(Forex)/sizeof(*Forex)];
    int32_t size; uint32_t flags=0,now; CBlockIndex *pindex;
    if ( Queued_reconsiderblock != zeroid )
    {
        fprintf(stderr,"Queued_reconsiderblock %s\n",Queued_reconsiderblock.GetHex().c_str());
        komodo_reconsiderblock(Queued_reconsiderblock);
        Queued_reconsiderblock = zeroid;
    }
    if ( forceflag != 0 && pending != 0 )
    {
        while ( pending != 0 )
            fprintf(stderr,"pricewait "), sleep(1);
        return;
    }
    pending = 1;
    now = (uint32_t)time(NULL);
    if ( (ASSETCHAINS_CBOPRET & 1) != 0 )
    {
        size = komodo_cbopretsize(ASSETCHAINS_CBOPRET);
        if ( Mineropret.size() < size )
            Mineropret.resize(size);
        size = PRICES_SIZEBIT0;
        if ( (forceflag != 0 || now > lastbtc+120) && get_btcusd(pricebits) == 0 )
        {
            if ( flags == 0 )
                komodo_PriceCache_shift();
            memcpy(PriceCache[0],pricebits,PRICES_SIZEBIT0);
            flags |= 1;
        }
        if ( (ASSETCHAINS_CBOPRET & 2) != 0 )
        {
            if ( now > lasttime+3600*5 || forexprices[0] == 0 ) // cant assume timestamp is valid for forex price as it is a daily weekday changing thing anyway.
            {
                get_dailyfx(forexprices);
                if ( flags == 0 )
                    komodo_PriceCache_shift();
                flags |= 2;
                memcpy(&PriceCache[0][size/sizeof(uint32_t)],forexprices,sizeof(forexprices));
            }
            size += (sizeof(Forex)/sizeof(*Forex)) * sizeof(uint32_t);
        }
        if ( (ASSETCHAINS_CBOPRET & 4) != 0 )
        {
            if ( forceflag != 0 || flags != 0 )
            {
                get_cryptoprices(pricebuf,Cryptos,(int32_t)(sizeof(Cryptos)/sizeof(*Cryptos)),ASSETCHAINS_PRICES);
                if ( flags == 0 )
                    komodo_PriceCache_shift();
                memcpy(&PriceCache[0][size/sizeof(uint32_t)],pricebuf,(sizeof(Cryptos)/sizeof(*Cryptos)+ASSETCHAINS_PRICES.size()) * sizeof(uint32_t));
                flags |= 4; // very rarely we can see flags == 6 case
            }
            size += (sizeof(Cryptos)/sizeof(*Cryptos)+ASSETCHAINS_PRICES.size()) * sizeof(uint32_t);
        }
        now = (uint32_t)time(NULL);
        if ( (ASSETCHAINS_CBOPRET & 8) != 0 )
        {
            if ( forceflag != 0 || flags != 0 )
            {
                if ( get_stockprices(now,pricebuf,ASSETCHAINS_STOCKS) == ASSETCHAINS_STOCKS.size() )
                {
                    if ( flags == 0 )
                        komodo_PriceCache_shift();
                    memcpy(&PriceCache[0][size/sizeof(uint32_t)],pricebuf,ASSETCHAINS_STOCKS.size() * sizeof(uint32_t));
                    flags |= 8; // very rarely we can see flags == 10 case
                }
            }
            size += (ASSETCHAINS_STOCKS.size()) * sizeof(uint32_t);
        }
        if ( flags != 0 )
        {
            if ( (flags & 1) != 0 )
                lastbtc = now;
            if ( (flags & 2) != 0 )
                lasttime = now;
            memcpy(Mineropret.data(),PriceCache[0],size);
            if ( ExtremePrice.dir != 0 && ExtremePrice.ind > 0 && ExtremePrice.ind < size/sizeof(uint32_t) && now < ExtremePrice.timestamp+3600 )
            {
                fprintf(stderr,"cmp dir.%d PriceCache[0][ExtremePrice.ind] %u >= %u ExtremePrice.pricebits\n",ExtremePrice.dir,PriceCache[0][ExtremePrice.ind],ExtremePrice.pricebits);
                if ( (ExtremePrice.dir > 0 && PriceCache[0][ExtremePrice.ind] >= ExtremePrice.pricebits) || (ExtremePrice.dir < 0 && PriceCache[0][ExtremePrice.ind] <= ExtremePrice.pricebits) )
                {
                    fprintf(stderr,"future price is close enough to allow approving previously rejected block ind.%d %u vs %u\n",ExtremePrice.ind,PriceCache[0][ExtremePrice.ind],ExtremePrice.pricebits);
                    if ( (pindex= komodo_blockindex(ExtremePrice.blockhash)) != 0 )
                        pindex->nStatus &= ~BLOCK_FAILED_MASK;
                    else fprintf(stderr,"couldnt find block.%s\n",ExtremePrice.blockhash.GetHex().c_str());
                }
            }
            // high volatility still strands nodes so we need to check new prices to approve a stuck block
            // scan list of stuck blocks (one?) and auto reconsiderblock if it changed state
            
            //int32_t i; for (i=0; i<Mineropret.size(); i++)
            //    fprintf(stderr,"%02x",Mineropret[i]);
            //fprintf(stderr," <- set Mineropret[%d] size.%d %ld\n",(int32_t)Mineropret.size(),size,sizeof(PriceCache[0]));
        }
    }
    pending = 0;
}

int64_t komodo_pricemult(int32_t ind)
{
    int32_t i,j;
    if ( (ASSETCHAINS_CBOPRET & 1) != 0 && ind < KOMODO_MAXPRICES )
    {
        if ( PriceMult[0] == 0 )
        {
            for (i=0; i<4; i++)
                PriceMult[i] = 10000;
            if ( (ASSETCHAINS_CBOPRET & 2) != 0 )
            {
                for (j=0; j<sizeof(Forex)/sizeof(*Forex); j++)
                    PriceMult[i++] = 10000;
            }
            if ( (ASSETCHAINS_CBOPRET & 4) != 0 )
            {
                for (j=0; j<sizeof(Cryptos)/sizeof(*Cryptos)+ASSETCHAINS_PRICES.size(); j++)
                    PriceMult[i++] = 1;
            }
            if ( (ASSETCHAINS_CBOPRET & 8) != 0 )
            {
                for (j=0; j<ASSETCHAINS_STOCKS.size(); j++)
                    PriceMult[i++] = 1000000;
            }
        }
        return(PriceMult[ind]);
    }
    return(0);
}

char *komodo_pricename(char *name,int32_t ind)
{
    strcpy(name,"error");
    if ( (ASSETCHAINS_CBOPRET & 1) != 0 && ind < KOMODO_MAXPRICES )
    {
        if ( ind < 4 )
        {
            switch ( ind )
            {
                case 0: strcpy(name,"timestamp"); break;
                case 1: strcpy(name,"BTC_USD"); break;
                case 2: strcpy(name,"BTC_GBP"); break;
                case 3: strcpy(name,"BTC_EUR"); break;
                default: return(0); break;
            }
            return(name);
        }
        else
        {
            ind -= 4;
            if ( (ASSETCHAINS_CBOPRET & 2) != 0 )
            {
                if ( ind < 0 )
                    return(0);
                if ( ind < sizeof(Forex)/sizeof(*Forex) )
                {
                    name[0] = 'U', name[1] = 'S', name[2] = 'D', name[3] = '_';
                    strcpy(name+4,Forex[ind]);
                    return(name);
                } else ind -= sizeof(Forex)/sizeof(*Forex);
            }
            if ( (ASSETCHAINS_CBOPRET & 4) != 0 )
            {
                if ( ind < 0 )
                    return(0);
                if ( ind < sizeof(Cryptos)/sizeof(*Cryptos) + ASSETCHAINS_PRICES.size() )
                {
                    if ( ind < sizeof(Cryptos)/sizeof(*Cryptos) )
                        strcpy(name,Cryptos[ind]);
                    else
                    {
                        ind -= (sizeof(Cryptos)/sizeof(*Cryptos));
                        strcpy(name,ASSETCHAINS_PRICES[ind].c_str());
                    }
                    strcat(name,"_BTC");
                    return(name);
                } else ind -= (sizeof(Cryptos)/sizeof(*Cryptos) + ASSETCHAINS_PRICES.size());
            }
            if ( (ASSETCHAINS_CBOPRET & 8) != 0 )
            {
                if ( ind < 0 )
                    return(0);
                if ( ind < ASSETCHAINS_STOCKS.size() )
                {
                    strcpy(name,ASSETCHAINS_STOCKS[ind].c_str());
                    strcat(name,"_USD");
                    return(name);
                } else ind -= ASSETCHAINS_STOCKS.size();
            }
        }
    }
    return(0);
}
// finds index for its symbol name
int32_t komodo_priceind(const char *symbol)
{
    char name[65]; int32_t i,n = (int32_t)(komodo_cbopretsize(ASSETCHAINS_CBOPRET) / sizeof(uint32_t));
    for (i=1; i<n; i++)
    {
        komodo_pricename(name,i);
        if ( strcmp(name,symbol) == 0 )
            return(i);
    }
    return(-1);
}
// returns price value which is in a 10% interval for more than 50% points for the preceding 24 hours
int64_t komodo_pricecorrelated(uint64_t seed,int32_t ind,uint32_t *rawprices,int32_t rawskip,uint32_t *nonzprices,int32_t smoothwidth)
{
    int32_t i,j,k,n,iter,correlation,maxcorrelation=0; int64_t firstprice,price,sum,den,mult,refprice,lowprice,highprice;
    if ( PRICES_DAYWINDOW < 2 || ind >= KOMODO_MAXPRICES )
        return(-1);
    mult = komodo_pricemult(ind);
    if ( nonzprices != 0 )
        memset(nonzprices,0,sizeof(*nonzprices)*PRICES_DAYWINDOW);
    //for (i=0; i<PRICES_DAYWINDOW; i++)
    //    fprintf(stderr,"%u ",rawprices[i*rawskip]);
    //fprintf(stderr,"ind.%d\n",ind);
    for (iter=0; iter<PRICES_DAYWINDOW; iter++)
    {
        correlation = 0;
        i = (iter + seed) % PRICES_DAYWINDOW;
        refprice = rawprices[i*rawskip];
        highprice = (refprice * (COIN + PRICES_ERRORRATE*5)) / COIN;
        lowprice = (refprice * (COIN - PRICES_ERRORRATE*5)) / COIN;
        if ( highprice == refprice )
            highprice++;
        if ( lowprice == refprice )
            lowprice--;
        sum = 0;
        //fprintf(stderr,"firsti.%d: ",i);
        for (j=0; j<PRICES_DAYWINDOW; j++,i++)
        {
            if ( i >= PRICES_DAYWINDOW )
                i = 0;
            if ( (price= rawprices[i*rawskip]) == 0 )
            {
                fprintf(stderr,"null rawprice.[%d]\n",i);
                return(-1);
            }
            if ( price >= lowprice && price <= highprice )
            {
                //fprintf(stderr,"%.1f ",(double)price/10000);
                sum += price;
                correlation++;
                if ( correlation > (PRICES_DAYWINDOW>>1) )
                {
                    if ( nonzprices == 0 )
                        return(refprice * mult);
                    //fprintf(stderr,"-> %.4f\n",(double)sum*mult/correlation);
                    //return(sum*mult/correlation);
                    n = 0;
                    i = (iter + seed) % PRICES_DAYWINDOW;
                    for (k=0; k<PRICES_DAYWINDOW; k++,i++)
                    {
                        if ( i >= PRICES_DAYWINDOW )
                            i = 0;
                        if ( n > (PRICES_DAYWINDOW>>1) )
                            nonzprices[i] = 0;
                        else
                        {
                            price = rawprices[i*rawskip];
                            if ( price < lowprice || price > highprice )
                                nonzprices[i] = 0;
                            else
                            {
                                nonzprices[i] = price;
                                //fprintf(stderr,"(%d %u) ",i,rawprices[i*rawskip]);
                                n++;
                            }
                        }
                    }
                    //fprintf(stderr,"ind.%d iter.%d j.%d i.%d n.%d correlation.%d ref %llu -> %llu\n",ind,iter,j,i,n,correlation,(long long)refprice,(long long)sum/correlation);
                    if ( n != correlation )
                        return(-1);
                    sum = den = n = 0;
                    for (i=0; i<PRICES_DAYWINDOW; i++)
                        if ( nonzprices[i] != 0 )
                            break;
                    firstprice = nonzprices[i];
                    //fprintf(stderr,"firsti.%d: ",i);
                    for (i=0; i<PRICES_DAYWINDOW; i++)
                    {
                        if ( (price= nonzprices[i]) != 0 )
                        {
                            den += (PRICES_DAYWINDOW - i);
                            sum += ((PRICES_DAYWINDOW - i) * (price + firstprice*4)) / 5;
                            n++;
                        }
                    }
                    if ( n != correlation || sum == 0 || den == 0 )
                    {
                        fprintf(stderr,"seed.%llu n.%d vs correlation.%d sum %llu, den %llu\n",(long long)seed,n,correlation,(long long)sum,(long long)den);
                        return(-1);
                    }
                    //fprintf(stderr,"firstprice.%llu weighted -> %.8f\n",(long long)firstprice,((double)(sum*mult) / den) / COIN);
                    return((sum * mult) / den);
                }
            }
        }
        if ( correlation > maxcorrelation )
            maxcorrelation = correlation;
    }
    fprintf(stderr,"ind.%d iter.%d maxcorrelation.%d ref.%llu high.%llu low.%llu\n",ind,iter,maxcorrelation,(long long)refprice,(long long)highprice,(long long)lowprice);
    return(0);
}

static int revcmp_llu(const void *a, const void*b)
{
    if(*(int64_t *)a < *(int64_t *)b) return 1;
    else if(*(int64_t *)a > *(int64_t *)b) return -1;
    else if ( (uint64_t)a < (uint64_t)b ) // jl777 prevent nondeterminism
        return(-1);
    else return(1);
}

static void revsort64(int64_t *l, int32_t llen)
{
    qsort(l,llen,sizeof(uint64_t),revcmp_llu);
}

// http://www.holoborodko.com/pavel/numerical-methods/noise-robust-smoothing-filter/
//const int64_t coeffs[7] = { -2, 0, 18, 32, 18, 0, -2 };
static int cmp_llu(const void *a, const void*b)
{
    if(*(int64_t *)a < *(int64_t *)b) return -1;
    else if(*(int64_t *)a > *(int64_t *)b) return 1;
    else if ( (uint64_t)a < (uint64_t)b ) // jl777 prevent nondeterminism
        return(-1);
    else return(1);
}

static void sort64(int64_t *l, int32_t llen)
{
    qsort(l,llen,sizeof(uint64_t),cmp_llu);
}

int64_t komodo_priceave(int64_t *buf,int64_t *correlated,int32_t cskip)
{
    int32_t i,dir=0; int64_t sum=0,nonzprice,price,halfave,thirdave,fourthave,decayprice;
    if ( PRICES_DAYWINDOW < 2 )
        return(0);
    for (i=0; i<PRICES_DAYWINDOW; i++)
    {
        if ( (nonzprice= correlated[i*cskip]) != 0 )
            break;
    }
    if ( nonzprice == 0 )
        return(-1);
    for (i=0; i<PRICES_DAYWINDOW; i++)
    {
        if ( (price= correlated[i*cskip]) != 0 )
            nonzprice = price;
        buf[PRICES_DAYWINDOW+i] = nonzprice;
        sum += nonzprice;
        if ( i == PRICES_DAYWINDOW/2 )
            halfave = (sum / (PRICES_DAYWINDOW/2));
        else if ( i == PRICES_DAYWINDOW/3 )
            thirdave = (sum / (PRICES_DAYWINDOW/3));
        else if ( i == PRICES_DAYWINDOW/4 )
            fourthave = (sum / (PRICES_DAYWINDOW/4));
    }
    memcpy(buf,&buf[PRICES_DAYWINDOW],PRICES_DAYWINDOW*sizeof(*buf));
    price = sum / PRICES_DAYWINDOW;
    return(price);
    if ( halfave == price )
        return(price);
    else if ( halfave > price ) // rising prices
        sort64(buf,PRICES_DAYWINDOW);
    else revsort64(buf,PRICES_DAYWINDOW);
    decayprice = buf[0];
    for (i=0; i<PRICES_DAYWINDOW; i++)
    {
        decayprice = ((decayprice * 97) + (buf[i] * 3)) / 100;
        //fprintf(stderr,"%.4f ",(double)buf[i]/COIN);
    }
    fprintf(stderr,"%ssort half %.8f %.8f %.8f %.8f %.8f %.8f -> %.8f\n",halfave<price?"rev":"",(double)price/COIN,(double)halfave/COIN,(double)thirdave/COIN,(double)fourthave/COIN,(double)decayprice/COIN,(double)buf[PRICES_DAYWINDOW-1]/COIN,(double)(price*7 + halfave*5 + thirdave*3 + fourthave*2 + decayprice + buf[PRICES_DAYWINDOW-1])/(19*COIN));
    return((price*7 + halfave*5 + thirdave*3 + fourthave*2 + decayprice + buf[PRICES_DAYWINDOW-1]) / 19);
}

int32_t komodo_pricesinit()
{
    static int32_t didinit;
    int32_t i,num=0,createflag = 0;
    if ( didinit != 0 )
        return(-1);
    didinit = 1;
    boost::filesystem::path pricefname,pricesdir = GetDataDir() / "prices";
    fprintf(stderr,"pricesinit (%s)\n",pricesdir.string().c_str());
    if (!boost::filesystem::exists(pricesdir))
        boost::filesystem::create_directories(pricesdir), createflag = 1;
    for (i=0; i<KOMODO_MAXPRICES; i++)
    {
        if ( komodo_pricename(PRICES[i].symbol,i) == 0 )
            break;
        //fprintf(stderr,"%s.%d ",PRICES[i].symbol,i);
        if ( i == 0 )
            strcpy(PRICES[i].symbol,"rawprices");
        pricefname = pricesdir / PRICES[i].symbol;
        if ( createflag != 0 )
            PRICES[i].fp = fopen(pricefname.string().c_str(),"wb+");
        else if ( (PRICES[i].fp= fopen(pricefname.string().c_str(),"rb+")) == 0 )
            PRICES[i].fp = fopen(pricefname.string().c_str(),"wb+");
        if ( PRICES[i].fp != 0 )
        {
            num++;
            if ( createflag != 0 )
            {
                fseek(PRICES[i].fp,(2*PRICES_DAYWINDOW+PRICES_SMOOTHWIDTH) * sizeof(int64_t) * PRICES_MAXDATAPOINTS,SEEK_SET);
                fputc(0,PRICES[i].fp);
                fflush(PRICES[i].fp);
            }
        } else fprintf(stderr,"error opening %s createflag.%d\n",pricefname.string().c_str(), createflag);
    }
    if ( i > 0 && PRICES[0].fp != 0 && createflag != 0 )
    {
        fseek(PRICES[0].fp,(2*PRICES_DAYWINDOW+PRICES_SMOOTHWIDTH) * sizeof(uint32_t) * i,SEEK_SET);
        fputc(0,PRICES[0].fp);
        fflush(PRICES[0].fp);
    }
    fprintf(stderr,"pricesinit done i.%d num.%d numprices.%d\n",i,num,(int32_t)(komodo_cbopretsize(ASSETCHAINS_CBOPRET)/sizeof(uint32_t)));
    if ( i != num || i != komodo_cbopretsize(ASSETCHAINS_CBOPRET)/sizeof(uint32_t) )
    {
        fprintf(stderr,"fatal error opening prices files, start shutdown\n");
        StartShutdown();
    }
    return(0);
}

pthread_mutex_t pricemutex;

// PRICES file layouts
// [0] rawprice32 / timestamp
// [1] correlated
// [2] 24hr ave
// [3] to [7] reserved

void komodo_pricesupdate(int32_t height,CBlock *pblock)
{
    static int numprices; static uint32_t *ptr32; static int64_t *ptr64,*tmpbuf;
    int32_t ind,offset,width; int64_t correlated,smoothed; uint64_t seed,rngval; uint32_t rawprices[KOMODO_MAXPRICES],buf[PRICES_MAXDATAPOINTS*2];
    width = PRICES_DAYWINDOW;//(2*PRICES_DAYWINDOW + PRICES_SMOOTHWIDTH);
    if ( numprices == 0 )
    {
        pthread_mutex_init(&pricemutex,0);
        numprices = (int32_t)(komodo_cbopretsize(ASSETCHAINS_CBOPRET) / sizeof(uint32_t));
        ptr32 = (uint32_t *)calloc(sizeof(uint32_t),numprices * width);
        ptr64 = (int64_t *)calloc(sizeof(int64_t),PRICES_DAYWINDOW*PRICES_MAXDATAPOINTS);
        tmpbuf = (int64_t *)calloc(sizeof(int64_t),2*PRICES_DAYWINDOW);
        fprintf(stderr,"prices update: numprices.%d %p %p\n",numprices,ptr32,ptr64);
    }
    if ( _komodo_heightpricebits(&seed,rawprices,pblock) == numprices )
    {
        //for (ind=0; ind<numprices; ind++)
        //    fprintf(stderr,"%u ",rawprices[ind]);
        //fprintf(stderr,"numprices.%d\n",numprices);
        if ( PRICES[0].fp != 0 )
        {
            pthread_mutex_lock(&pricemutex);
            fseek(PRICES[0].fp,height * numprices * sizeof(uint32_t),SEEK_SET);
            if ( fwrite(rawprices,sizeof(uint32_t),numprices,PRICES[0].fp) != numprices )
                fprintf(stderr,"error writing rawprices for ht.%d\n",height);
            else fflush(PRICES[0].fp);
            if ( height > PRICES_DAYWINDOW )
            {
                fseek(PRICES[0].fp,(height-width+1) * numprices * sizeof(uint32_t),SEEK_SET);
                if ( fread(ptr32,sizeof(uint32_t),width*numprices,PRICES[0].fp) == width*numprices )
                {
                    rngval = seed;
                    for (ind=1; ind<numprices; ind++)
                    {
                        if ( PRICES[ind].fp == 0 )
                        {
                            fprintf(stderr,"PRICES[%d].fp is null\n",ind);
                            continue;
                        }
                        offset = (width-1)*numprices + ind;
                        rngval = (rngval*11109 + 13849);
                        if ( (correlated= komodo_pricecorrelated(rngval,ind,&ptr32[offset],-numprices,0,PRICES_SMOOTHWIDTH)) > 0 )
                        {
                            fseek(PRICES[ind].fp,height * sizeof(int64_t) * PRICES_MAXDATAPOINTS,SEEK_SET);
                            memset(buf,0,sizeof(buf));
                            buf[0] = rawprices[ind];
                            buf[1] = rawprices[0]; // timestamp
                            memcpy(&buf[2],&correlated,sizeof(correlated));
                            if ( fwrite(buf,1,sizeof(buf),PRICES[ind].fp) != sizeof(buf) )
                                fprintf(stderr,"error fwrite buf for ht.%d ind.%d\n",height,ind);
                            else if ( height > PRICES_DAYWINDOW*2 )
                            {
                                fseek(PRICES[ind].fp,(height-PRICES_DAYWINDOW+1) * PRICES_MAXDATAPOINTS * sizeof(int64_t),SEEK_SET);
                                if ( fread(ptr64,sizeof(int64_t),PRICES_DAYWINDOW*PRICES_MAXDATAPOINTS,PRICES[ind].fp) == PRICES_DAYWINDOW*PRICES_MAXDATAPOINTS )
                                {
                                    if ( (smoothed= komodo_priceave(tmpbuf,&ptr64[(PRICES_DAYWINDOW-1)*PRICES_MAXDATAPOINTS+1],-PRICES_MAXDATAPOINTS)) > 0 )
                                    {
                                        fseek(PRICES[ind].fp,(height * PRICES_MAXDATAPOINTS + 2) * sizeof(int64_t),SEEK_SET);
                                        if ( fwrite(&smoothed,1,sizeof(smoothed),PRICES[ind].fp) != sizeof(smoothed) )
                                            fprintf(stderr,"error fwrite smoothed for ht.%d ind.%d\n",height,ind);
                                        else fflush(PRICES[ind].fp);
                                    } else fprintf(stderr,"error price_smoothed ht.%d ind.%d\n",height,ind);
                                } else fprintf(stderr,"error fread ptr64 for ht.%d ind.%d\n",height,ind);
                            }
                        } else fprintf(stderr,"error komodo_pricecorrelated for ht.%d ind.%d\n",height,ind);
                    }
                    fprintf(stderr,"height.%d\n",height);
                } else fprintf(stderr,"error reading rawprices for ht.%d\n",height);
            } else fprintf(stderr,"height.%d <= width.%d\n",height,width);
            pthread_mutex_unlock(&pricemutex);
        } else fprintf(stderr,"null PRICES[0].fp\n");
    } else fprintf(stderr,"numprices mismatch, height.%d\n",height);
}

int32_t komodo_priceget(int64_t *buf64,int32_t ind,int32_t height,int32_t numblocks)
{
    FILE *fp; int32_t retval = PRICES_MAXDATAPOINTS;
    pthread_mutex_lock(&pricemutex);
    if ( ind < KOMODO_MAXPRICES && (fp= PRICES[ind].fp) != 0 )
    {
        fseek(fp,height * PRICES_MAXDATAPOINTS * sizeof(int64_t),SEEK_SET);
        if ( fread(buf64,sizeof(int64_t),numblocks*PRICES_MAXDATAPOINTS,fp) != numblocks*PRICES_MAXDATAPOINTS )
            retval = -1;
    }
    pthread_mutex_unlock(&pricemutex);
    return(retval);
}
