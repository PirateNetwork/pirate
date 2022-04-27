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
#include "komodo_notary.h"
#include "komodo_extern_globals.h"
#include "komodo.h" // komodo_stateupdate()
#include "komodo_structs.h" // KOMODO_NOTARIES_HARDCODED
#include "komodo_utils.h" // komodo_stateptr

int32_t getkmdseason(int32_t height)
{
    if ( height <= KMD_SEASON_HEIGHTS[0] )
        return(1);
    for (int32_t i = 1; i < NUM_KMD_SEASONS; i++)
    {
        if ( height <= KMD_SEASON_HEIGHTS[i] && height > KMD_SEASON_HEIGHTS[i-1] )
            return(i+1);
    }
    return(0);
}

int32_t getacseason(uint32_t timestamp)
{
    if ( timestamp <= KMD_SEASON_TIMESTAMPS[0] )
        return(1);
    for (int32_t i = 1; i < NUM_KMD_SEASONS; i++)
    {
        if ( timestamp <= KMD_SEASON_TIMESTAMPS[i] && timestamp > KMD_SEASON_TIMESTAMPS[i-1] )
            return(i+1);
    }
    return(0);
}

int32_t komodo_notaries(uint8_t pubkeys[64][33],int32_t height,uint32_t timestamp)
{
    int32_t i,htind,n; uint64_t mask = 0; struct knotary_entry *kp,*tmp;
    static uint8_t kmd_pubkeys[NUM_KMD_SEASONS][64][33],didinit[NUM_KMD_SEASONS];
    
    if ( timestamp == 0 && ASSETCHAINS_SYMBOL[0] != 0 )
        timestamp = komodo_heightstamp(height);
    else if ( ASSETCHAINS_SYMBOL[0] == 0 )
        timestamp = 0;

    // If this chain is not a staked chain, use the normal Komodo logic to determine notaries. This allows KMD to still sync and use its proper pubkeys for dPoW.
    if ( is_STAKED(ASSETCHAINS_SYMBOL) == 0 )
    {
        int32_t kmd_season = 0;
        if ( ASSETCHAINS_SYMBOL[0] == 0 )
        {
            // This is KMD, use block heights to determine the KMD notary season.. 
            if ( height >= KOMODO_NOTARIES_HARDCODED )
                kmd_season = getkmdseason(height);
        }
        else 
        {
            // This is a non LABS assetchain, use timestamp to detemine notary pubkeys. 
            kmd_season = getacseason(timestamp);
        }
        if ( kmd_season != 0 )
        {
            if ( didinit[kmd_season-1] == 0 )
            {
                for (i=0; i<NUM_KMD_NOTARIES; i++) 
                    decode_hex(kmd_pubkeys[kmd_season-1][i],33,(char *)notaries_elected[kmd_season-1][i][1]);
                if ( ASSETCHAINS_PRIVATE != 0 )
                {
                    // this is PIRATE, we need to populate the address array for the notary exemptions. 
                    for (i = 0; i<NUM_KMD_NOTARIES; i++)
                        pubkey2addr((char *)NOTARY_ADDRESSES[kmd_season-1][i],(uint8_t *)kmd_pubkeys[kmd_season-1][i]);
                }
                didinit[kmd_season-1] = 1;
            }
            memcpy(pubkeys,kmd_pubkeys[kmd_season-1],NUM_KMD_NOTARIES * 33);
            return(NUM_KMD_NOTARIES);
        }
    }
    else if ( timestamp != 0 )
    { 
        // here we can activate our pubkeys for LABS chains everythig is in notaries_staked.cpp
        int32_t staked_era; int8_t numSN;
        uint8_t staked_pubkeys[64][33];
        staked_era = STAKED_era(timestamp);
        numSN = numStakedNotaries(staked_pubkeys,staked_era);
        memcpy(pubkeys,staked_pubkeys,numSN * 33);
        return(numSN);
    }

    htind = height / KOMODO_ELECTION_GAP;
    if ( htind >= KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP )
        htind = (KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP) - 1;
    if ( Pubkeys == 0 )
    {
        komodo_init(height);
        //printf("Pubkeys.%p htind.%d vs max.%d\n",Pubkeys,htind,KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP);
    }
    {
        std::lock_guard<std::mutex> lock(komodo_mutex);
        n = Pubkeys[htind].numnotaries;
        if ( 0 && ASSETCHAINS_SYMBOL[0] != 0 )
            fprintf(stderr,"%s height.%d t.%u genesis.%d\n",ASSETCHAINS_SYMBOL,height,timestamp,n);
        HASH_ITER(hh,Pubkeys[htind].Notaries,kp,tmp)
        {
            if ( kp->notaryid < n )
            {
                mask |= (1LL << kp->notaryid);
                memcpy(pubkeys[kp->notaryid],kp->pubkey,33);
            } else printf("illegal notaryid.%d vs n.%d\n",kp->notaryid,n);
        }
    }
    if ( (n < 64 && mask == ((1LL << n)-1)) || (n == 64 && mask == 0xffffffffffffffffLL) )
        return(n);
    printf("error retrieving notaries ht.%d got mask.%llx for n.%d\n",height,(long long)mask,n);
    return(-1);
}

int32_t komodo_electednotary(int32_t *numnotariesp,uint8_t *pubkey33,int32_t height,uint32_t timestamp)
{
    int32_t i,n; uint8_t pubkeys[64][33];
    n = komodo_notaries(pubkeys,height,timestamp);
    *numnotariesp = n;
    for (i=0; i<n; i++)
    {
        if ( memcmp(pubkey33,pubkeys[i],33) == 0 )
            return(i);
    }
    return(-1);
}

int32_t komodo_ratify_threshold(int32_t height,uint64_t signedmask)
{
    int32_t htind,numnotaries,i,wt = 0;
    htind = height / KOMODO_ELECTION_GAP;
    if ( htind >= KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP )
        htind = (KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP) - 1;
    numnotaries = Pubkeys[htind].numnotaries;
    for (i=0; i<numnotaries; i++)
        if ( ((1LL << i) & signedmask) != 0 )
            wt++;
    if ( wt > (numnotaries >> 1) || (wt > 7 && (signedmask & 1) != 0) )
        return(1);
    else return(0);
}

void komodo_notarysinit(int32_t origheight,uint8_t pubkeys[64][33],int32_t num)
{
    static int32_t hwmheight;
    int32_t k,i,htind,height; struct knotary_entry *kp; struct knotaries_entry N;
    if ( Pubkeys == 0 )
        Pubkeys = (struct knotaries_entry *)calloc(1 + (KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP),sizeof(*Pubkeys));
    memset(&N,0,sizeof(N));
    if ( origheight > 0 )
    {
        height = (origheight + KOMODO_ELECTION_GAP/2);
        height /= KOMODO_ELECTION_GAP;
        height = ((height + 1) * KOMODO_ELECTION_GAP);
        htind = (height / KOMODO_ELECTION_GAP);
        if ( htind >= KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP )
            htind = (KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP) - 1;
        //printf("htind.%d activation %d from %d vs %d | hwmheight.%d %s\n",htind,height,origheight,(((origheight+KOMODO_ELECTION_GAP/2)/KOMODO_ELECTION_GAP)+1)*KOMODO_ELECTION_GAP,hwmheight,ASSETCHAINS_SYMBOL);
    } else htind = 0;
    {
        std::lock_guard<std::mutex> lock(komodo_mutex);
        for (k=0; k<num; k++)
        {
            kp = (struct knotary_entry *)calloc(1,sizeof(*kp));
            memcpy(kp->pubkey,pubkeys[k],33);
            kp->notaryid = k;
            HASH_ADD_KEYPTR(hh,N.Notaries,kp->pubkey,33,kp);
            if ( 0 && height > 10000 )
            {
                for (i=0; i<33; i++)
                    printf("%02x",pubkeys[k][i]);
                printf(" notarypubs.[%d] ht.%d active at %d\n",k,origheight,htind*KOMODO_ELECTION_GAP);
            }
        }
        N.numnotaries = num;
        for (i=htind; i<KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP; i++)
        {
            if ( Pubkeys[i].height != 0 && origheight < hwmheight )
            {
                printf("Pubkeys[%d].height %d < %d hwmheight, origheight.%d\n",i,Pubkeys[i].height,hwmheight,origheight);
                break;
            }
            Pubkeys[i] = N;
            Pubkeys[i].height = i * KOMODO_ELECTION_GAP;
        }
    }
    if ( origheight > hwmheight )
        hwmheight = origheight;
}

int32_t komodo_chosennotary(int32_t *notaryidp,int32_t height,uint8_t *pubkey33,uint32_t timestamp)
{
    // -1 if not notary, 0 if notary, 1 if special notary
    struct knotary_entry *kp; int32_t numnotaries=0,htind,modval = -1;
    *notaryidp = -1;
    if ( height < 0 )//|| height >= KOMODO_MAXBLOCKS )
    {
        printf("komodo_chosennotary ht.%d illegal\n",height);
        return(-1);
    }
    if ( height >= KOMODO_NOTARIES_HARDCODED || ASSETCHAINS_SYMBOL[0] != 0 )
    {
        if ( (*notaryidp= komodo_electednotary(&numnotaries,pubkey33,height,timestamp)) >= 0 && numnotaries != 0 )
        {
            modval = ((height % numnotaries) == *notaryidp);
            return(modval);
        }
    }
    if ( height >= 250000 )
        return(-1);
    if ( Pubkeys == 0 )
        komodo_init(0);
    htind = height / KOMODO_ELECTION_GAP;
    if ( htind >= KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP )
        htind = (KOMODO_MAXBLOCKS / KOMODO_ELECTION_GAP) - 1;
    {
        std::lock_guard<std::mutex> lock(komodo_mutex);
        HASH_FIND(hh,Pubkeys[htind].Notaries,pubkey33,33,kp);
    }
    if ( kp != 0 )
    {
        if ( (numnotaries= Pubkeys[htind].numnotaries) > 0 )
        {
            *notaryidp = kp->notaryid;
            modval = ((height % numnotaries) == kp->notaryid);
            //printf("found notary.%d ht.%d modval.%d\n",kp->notaryid,height,modval);
        } else printf("unexpected zero notaries at height.%d\n",height);
    } //else printf("cant find kp at htind.%d ht.%d\n",htind,height);
    //int32_t i; for (i=0; i<33; i++)
    //    printf("%02x",pubkey33[i]);
    //printf(" ht.%d notary.%d special.%d htind.%d num.%d\n",height,*notaryidp,modval,htind,numnotaries);
    return(modval);
}

//struct komodo_state *komodo_stateptr(char *symbol,char *dest);

struct notarized_checkpoint *komodo_npptr_for_height(int32_t height, int *idx)
{
    char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; int32_t i; struct komodo_state *sp; struct notarized_checkpoint *np = 0;
    if ( (sp= komodo_stateptr(symbol,dest)) != 0 )
    {
        for (i=sp->NUM_NPOINTS-1; i>=0; i--)
        {
            *idx = i;
            np = &sp->NPOINTS[i];
            if ( np->MoMdepth != 0 && height > np->notarized_height-(np->MoMdepth&0xffff) && height <= np->notarized_height )
                return(np);
        }
    }
    *idx = -1;
    return(0);
}

struct notarized_checkpoint *komodo_npptr(int32_t height)
{
    int idx;
    return komodo_npptr_for_height(height, &idx);
}

struct notarized_checkpoint *komodo_npptr_at(int idx)
{
    char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; struct komodo_state *sp;
    if ( (sp= komodo_stateptr(symbol,dest)) != 0 )
        if (idx < sp->NUM_NPOINTS)
            return &sp->NPOINTS[idx];
    return(0);
}

int32_t komodo_prevMoMheight()
{
    static uint256 zero;
    char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; int32_t i; struct komodo_state *sp; struct notarized_checkpoint *np = 0;
    if ( (sp= komodo_stateptr(symbol,dest)) != 0 )
    {
        for (i=sp->NUM_NPOINTS-1; i>=0; i--)
        {
            np = &sp->NPOINTS[i];
            if ( np->MoM != zero )
                return(np->notarized_height);
        }
    }
    return(0);
}

int32_t komodo_notarized_height(int32_t *prevMoMheightp,uint256 *hashp,uint256 *txidp)
{
    char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; struct komodo_state *sp;
    *prevMoMheightp = 0;
    memset(hashp,0,sizeof(*hashp));
    memset(txidp,0,sizeof(*txidp));
    if ( (sp= komodo_stateptr(symbol,dest)) != 0 )
    {
        CBlockIndex *pindex;
        if ( (pindex= komodo_blockindex(sp->NOTARIZED_HASH)) == 0 || pindex->GetHeight() < 0 )
        {
            //fprintf(stderr,"found orphaned notarization at ht.%d pindex.%p\n",sp->NOTARIZED_HEIGHT,(void *)pindex);
            memset(&sp->NOTARIZED_HASH,0,sizeof(sp->NOTARIZED_HASH));
            memset(&sp->NOTARIZED_DESTTXID,0,sizeof(sp->NOTARIZED_DESTTXID));
            sp->NOTARIZED_HEIGHT = 0;
        }
        else
        {
            *hashp = sp->NOTARIZED_HASH;
            *txidp = sp->NOTARIZED_DESTTXID;
            *prevMoMheightp = komodo_prevMoMheight();
        }
        return(sp->NOTARIZED_HEIGHT);
    } else return(0);
}

int32_t komodo_dpowconfs(int32_t txheight,int32_t numconfs)
{
    static int32_t hadnotarization;
    char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; struct komodo_state *sp;
    if ( KOMODO_DPOWCONFS != 0 && txheight > 0 && numconfs > 0 && (sp= komodo_stateptr(symbol,dest)) != 0 )
    {
        if ( sp->NOTARIZED_HEIGHT > 0 )
        {
            hadnotarization = 1;
            if ( txheight < sp->NOTARIZED_HEIGHT )
                return(numconfs);
            else return(1);
        }
        else if ( hadnotarization != 0 )
            return(1);
    }
    return(numconfs);
}

int32_t komodo_MoMdata(int32_t *notarized_htp,uint256 *MoMp,uint256 *kmdtxidp,int32_t height,uint256 *MoMoMp,int32_t *MoMoMoffsetp,int32_t *MoMoMdepthp,int32_t *kmdstartip,int32_t *kmdendip)
{
    struct notarized_checkpoint *np = 0;
    if ( (np= komodo_npptr(height)) != 0 )
    {
        *notarized_htp = np->notarized_height;
        *MoMp = np->MoM;
        *kmdtxidp = np->notarized_desttxid;
        *MoMoMp = np->MoMoM;
        *MoMoMoffsetp = np->MoMoMoffset;
        *MoMoMdepthp = np->MoMoMdepth;
        *kmdstartip = np->kmdstarti;
        *kmdendip = np->kmdendi;
        return(np->MoMdepth & 0xffff);
    }
    *notarized_htp = *MoMoMoffsetp = *MoMoMdepthp = *kmdstartip = *kmdendip = 0;
    memset(MoMp,0,sizeof(*MoMp));
    memset(MoMoMp,0,sizeof(*MoMoMp));
    memset(kmdtxidp,0,sizeof(*kmdtxidp));
    return(0);
}

int32_t komodo_notarizeddata(int32_t nHeight,uint256 *notarized_hashp,uint256 *notarized_desttxidp)
{
    struct notarized_checkpoint *np = 0; int32_t i=0,flag = 0; char symbol[KOMODO_ASSETCHAIN_MAXLEN],dest[KOMODO_ASSETCHAIN_MAXLEN]; struct komodo_state *sp;
    if ( (sp= komodo_stateptr(symbol,dest)) != 0 )
    {
        if ( sp->NUM_NPOINTS > 0 )
        {
            flag = 0;
            if ( sp->last_NPOINTSi < sp->NUM_NPOINTS && sp->last_NPOINTSi > 0 )
            {
                np = &sp->NPOINTS[sp->last_NPOINTSi-1];
                if ( np->nHeight < nHeight )
                {
                    for (i=sp->last_NPOINTSi; i<sp->NUM_NPOINTS; i++)
                    {
                        if ( sp->NPOINTS[i].nHeight >= nHeight )
                        {
                            //printf("flag.1 i.%d np->ht %d [%d].ht %d >= nHeight.%d, last.%d num.%d\n",i,np->nHeight,i,sp->NPOINTS[i].nHeight,nHeight,sp->last_NPOINTSi,sp->NUM_NPOINTS);
                            flag = 1;
                            break;
                        }
                        np = &sp->NPOINTS[i];
                        sp->last_NPOINTSi = i;
                    }
                }
            }
            if ( flag == 0 )
            {
                np = 0;
                for (i=0; i<sp->NUM_NPOINTS; i++)
                {
                    if ( sp->NPOINTS[i].nHeight >= nHeight )
                    {
                        //printf("i.%d np->ht %d [%d].ht %d >= nHeight.%d\n",i,np->nHeight,i,sp->NPOINTS[i].nHeight,nHeight);
                        break;
                    }
                    np = &sp->NPOINTS[i];
                    sp->last_NPOINTSi = i;
                }
            }
        }
        if ( np != 0 )
        {
            //char str[65],str2[65]; printf("[%s] notarized_ht.%d\n",ASSETCHAINS_SYMBOL,np->notarized_height);
            if ( np->nHeight >= nHeight || (i < sp->NUM_NPOINTS && np[1].nHeight < nHeight) )
                printf("warning: flag.%d i.%d np->ht %d [1].ht %d >= nHeight.%d\n",flag,i,np->nHeight,np[1].nHeight,nHeight);
            *notarized_hashp = np->notarized_hash;
            *notarized_desttxidp = np->notarized_desttxid;
            return(np->notarized_height);
        }
    }
    memset(notarized_hashp,0,sizeof(*notarized_hashp));
    memset(notarized_desttxidp,0,sizeof(*notarized_desttxidp));
    return(0);
}

void komodo_notarized_update(struct komodo_state *sp,int32_t nHeight,int32_t notarized_height,uint256 notarized_hash,uint256 notarized_desttxid,uint256 MoM,int32_t MoMdepth)
{
    struct notarized_checkpoint *np;
    if ( notarized_height >= nHeight )
    {
        fprintf(stderr,"komodo_notarized_update REJECT notarized_height %d > %d nHeight\n",notarized_height,nHeight);
        return;
    }
    if ( 0 && ASSETCHAINS_SYMBOL[0] != 0 )
        fprintf(stderr,"[%s] komodo_notarized_update nHeight.%d notarized_height.%d\n",ASSETCHAINS_SYMBOL,nHeight,notarized_height);
    std::lock_guard<std::mutex> lock(komodo_mutex);
    sp->NPOINTS = (struct notarized_checkpoint *)realloc(sp->NPOINTS,(sp->NUM_NPOINTS+1) * sizeof(*sp->NPOINTS));
    np = &sp->NPOINTS[sp->NUM_NPOINTS++];
    memset(np,0,sizeof(*np));
    np->nHeight = nHeight;
    sp->NOTARIZED_HEIGHT = np->notarized_height = notarized_height;
    sp->NOTARIZED_HASH = np->notarized_hash = notarized_hash;
    sp->NOTARIZED_DESTTXID = np->notarized_desttxid = notarized_desttxid;
    sp->MoM = np->MoM = MoM;
    sp->MoMdepth = np->MoMdepth = MoMdepth;
}

void komodo_init(int32_t height)
{
    static int didinit; 
    uint256 zero; 
    uint8_t pubkeys[64][33];
    memset(&zero,0,sizeof(zero));
    if ( didinit == 0 )
    {
        decode_hex(NOTARY_PUBKEY33,33,NOTARY_PUBKEY.c_str());
        if ( height >= 0 )
        {
            int32_t count = 0;
            for( auto pair : Params().GenesisNotaries() )
            {
                if (pair.first.empty() || pair.second.empty() )
                    break;
                decode_hex(pubkeys[count],33,(char *)pair.second.c_str());
                ++count;
            }
            komodo_notarysinit(0,pubkeys,count);
        }
        didinit = 1;
        komodo_stateupdate(0,0,0,0,zero,0,0,0,0,0,0,0,0,0,0,zero,0);
    }
}
