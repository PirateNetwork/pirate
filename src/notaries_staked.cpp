
#include "notaries_staked.h"
#include "crosschain.h"
#include "cc/CCinclude.h"
#include "komodo_defs.h"
#include "komodo_globals.h"
#include "komodo_hardfork.h"
#include "hex.h"
#include <cstring>

pthread_mutex_t staked_mutex; // todo remove

//static bool doneinit_STAKED = false;

namespace
{
    static const int32_t DPOW_UNREACHABLE_SIGS = 65; // one more than the maximum notary set size

    bool DPoWLitecoinQuorumActiveAtHeight(int32_t height,int32_t numnotaries,int32_t activationHeight)
    {
        return(DPoWLitecoinUpgradeActiveAtHeight(height,activationHeight) &&
               numnotaries == DPOW_LITECOIN_NOTARY_COUNT);
    }

    int32_t DPoWLegacyMinRatify(int32_t height)
    {
        return((height < 90000) ? 7 : 11);
    }

    bool DPoWLitecoinPubkeyConfigured(const char *pubkeystr)
    {
        return(pubkeystr != 0 && strlen(pubkeystr) == 66 &&
               pubkeystr[0] == '0' && (pubkeystr[1] == '2' || pubkeystr[1] == '3'));
    }
}

bool DPoWLitecoinUpgradeActive(int32_t height)
{
    return(DPoWLitecoinUpgradeActiveAtHeight(height,DPOW_LITECOIN_ACTIVATION_HEIGHT));
}

bool DPoWLitecoinUpgradeActiveAtHeight(int32_t height,int32_t activationHeight)
{
    return(!chainName.isKMD() &&
           chainName.isSymbol(DPOW_LITECOIN_CHAIN_SYMBOL) &&
           activationHeight > 0 &&
           height >= activationHeight);
}

int32_t DPoWLitecoinNotaries(uint8_t pubkeys[64][33],int32_t height)
{
    if ( !DPoWLitecoinUpgradeActive(height) )
        return(0);

    memset(pubkeys,0,64 * 33);
    for (int32_t i=0; i<DPOW_LITECOIN_NOTARY_COUNT; i++)
    {
        const char *pubkeystr = notaries_LITECOIN[i][1];
        if ( !DPoWLitecoinPubkeyConfigured(pubkeystr) )
        {
            fprintf(stderr,"%s dPoW Litecoin notary key %d is not configured; refusing activated authority set\n",
                    chainName.symbol().c_str(),i);
            return(-1);
        }
        decode_hex(pubkeys[i],33,(char *)pubkeystr);
    }
    return(DPOW_LITECOIN_NOTARY_COUNT);
}

int32_t DPoWRequiredSigs(int32_t height,int32_t numnotaries)
{
    return(DPoWRequiredSigsAtHeight(height,numnotaries,DPOW_LITECOIN_ACTIVATION_HEIGHT));
}

int32_t DPoWRequiredSigsAtHeight(int32_t height,int32_t numnotaries,int32_t activationHeight)
{
    if ( DPoWLitecoinUpgradeActiveAtHeight(height,activationHeight) )
        return(DPoWLitecoinQuorumActiveAtHeight(height,numnotaries,activationHeight) ? DPOW_LITECOIN_REQUIRED_SIGS : DPOW_UNREACHABLE_SIGS);
    return(11);
}

bool DPoWNotarizationSigsMet(int32_t height,int32_t numvalid,uint64_t signedmask,int32_t numnotaries,bool isKMD)
{
    return(DPoWNotarizationSigsMetAtHeight(height,numvalid,signedmask,numnotaries,isKMD,DPOW_LITECOIN_ACTIVATION_HEIGHT));
}

bool DPoWNotarizationSigsMetAtHeight(int32_t height,int32_t numvalid,uint64_t signedmask,int32_t numnotaries,bool isKMD,int32_t activationHeight)
{
    if ( DPoWLitecoinUpgradeActiveAtHeight(height,activationHeight) )
        return(DPoWLitecoinQuorumActiveAtHeight(height,numnotaries,activationHeight) && numvalid >= DPOW_LITECOIN_REQUIRED_SIGS);

    int32_t minsigs = DPoWLegacyMinRatify(height);
    return(((height < 90000 || (signedmask & 1) != 0) && numvalid >= minsigs) ||
           (numvalid >= minsigs && !isKMD) ||
           numvalid > (numnotaries / 5));
}

bool DPoWVoutSigsMet(int32_t height,int32_t numvalid,int32_t numnotaries)
{
    return(DPoWVoutSigsMetAtHeight(height,numvalid,numnotaries,DPOW_LITECOIN_ACTIVATION_HEIGHT));
}

bool DPoWVoutSigsMetAtHeight(int32_t height,int32_t numvalid,int32_t numnotaries,int32_t activationHeight)
{
    if ( DPoWLitecoinUpgradeActiveAtHeight(height,activationHeight) )
        return(DPoWLitecoinQuorumActiveAtHeight(height,numnotaries,activationHeight) && numvalid >= DPOW_LITECOIN_REQUIRED_SIGS);
    return(numvalid >= DPoWLegacyMinRatify(height));
}

const char *DPoWAssetChainDest(int32_t height)
{
    return(DPoWAssetChainDestAtHeight(height,DPOW_LITECOIN_ACTIVATION_HEIGHT));
}

const char *DPoWAssetChainDestAtHeight(int32_t height,int32_t activationHeight)
{
    return(DPoWLitecoinUpgradeActiveAtHeight(height,activationHeight) ? DPOW_LITECOIN_DEST : DPOW_ASSETCHAIN_LEGACY_DEST);
}

/*****
 * Reset the doneinit static (for unit testing)
 */
/*void undo_init_STAKED()
{
    doneinit_STAKED = false;
}*/

/****
 * @brief given the chan name, determine the type of chain
 * @param chain_name the chain name
 * @returns 0=kmd, 1=LABS, 2=LABSxxx, 3=CFEK, 4=TEST, 255=banned
 */
uint8_t is_STAKED(const std::string& symbol) 
{
    // let's not use static vars:
    /* static uint8_t STAKED;
    if (symbol.empty())
        return(0);
    if (doneinit_STAKED && !chainName.isKMD())
        return(STAKED);
    else STAKED = 0;*/
    uint8_t STAKED = 0;

    if ( symbol == "LABS" ) 
        STAKED = 1; // These chains are allowed coin emissions.
    else if ( symbol.find("LABS") == 0 ) 
        STAKED = 2; // These chains have no coin emission, block subsidy is always 0, and comission is 0. Notary pay is allowed.
    else if ( symbol == "CFEK" || symbol.find("CFEK") == 0 )
        STAKED = 3; // These chains have no speical rules at all.
    else if ( symbol == "TEST" || symbol.find("TEST") == 0 )
        STAKED = 4; // These chains are for testing consensus to create a chain etc. Not meant to be actually used for anything important.
    else if ( symbol == "THIS_CHAIN_IS_BANNED" )
        STAKED = 255; // Any chain added to this group is banned, no notarisations are valid, as a consensus rule. Can be used to remove a chain from cluster if needed.
    //doneinit_STAKED = true;
    return(STAKED);
};

int32_t STAKED_era(int timestamp)
{
    int8_t era = 0;
    if (timestamp <= STAKED_NOTARIES_TIMESTAMP[0])
        return(1);
    for (int32_t i = 1; i < NUM_STAKED_ERAS; i++)
    {
        if (timestamp <= STAKED_NOTARIES_TIMESTAMP[i] && timestamp >= (STAKED_NOTARIES_TIMESTAMP[i-1] + STAKED_ERA_GAP))
            return(i+1);
    }
  // if we are in a gap, return era 0, this allows to invalidate notarizations when in GAP.
  return(0);
};

//char NOTARYADDRS[64][64];  // todo remove

int8_t StakedNotaryID(std::string &notaryname, char *Raddress) {
    if ( STAKED_ERA != 0 )
    {
        for (int8_t i = 0; i < num_notaries_STAKED[STAKED_ERA-1]; i++) {
            if ( strcmp(Raddress,NOTARYADDRS[i]) == 0 ) {
                notaryname.assign(notaries_STAKED[STAKED_ERA-1][i][0]);
                return(i);
            }
        }
    }
    return(-1);
}

int8_t numStakedNotaries(uint8_t pubkeys[64][33],int8_t era) {
    int i; int8_t retval = 0;
    static uint8_t staked_pubkeys[NUM_STAKED_ERAS][64][33],didinit[NUM_STAKED_ERAS];
    static char ChainName[65];

    if ( ChainName[0] == 0 )
    {
        strcpy(ChainName, chainName.ToString().c_str());
    }

    if ( era == 0 )
    {
        // era is zero so we need to null out the pubkeys.
        memset(pubkeys,0,64 * 33);
        printf("%s is a STAKED chain and is in an ERA GAP.\n",ChainName);
        return(64);
    }
    else
    {
        if ( didinit[era-1] == 0 )
        {
            for (i=0; i<num_notaries_STAKED[era-1]; i++) {
                decode_hex(staked_pubkeys[era-1][i],33,notaries_STAKED[era-1][i][1]);
            }
            didinit[era-1] = 1;
            printf("%s is a STAKED chain in era %i \n",ChainName,era);
        }
        memcpy(pubkeys,staked_pubkeys[era-1],num_notaries_STAKED[era-1] * 33);
        retval = num_notaries_STAKED[era-1];
    }
    return(retval);
}

void UpdateNotaryAddrs(uint8_t pubkeys[64][33],int8_t numNotaries) {
    static int didinit;
    if ( didinit == 0 ) {
        pthread_mutex_init(&staked_mutex,NULL);
        didinit = 1;
    }
    if ( pubkeys[0][0] == 0 )
    {
        // null pubkeys, era 0.
        pthread_mutex_lock(&staked_mutex);
        memset(NOTARYADDRS,0,sizeof(NOTARYADDRS));
        pthread_mutex_unlock(&staked_mutex);
    }
    else
    {
        // staked era is set.
        pthread_mutex_lock(&staked_mutex);
        for (int i = 0; i<numNotaries; i++)
        {
            pubkey2addr((char *)NOTARYADDRS[i],(uint8_t *)pubkeys[i]);
            if ( memcmp(NOTARY_PUBKEY33,pubkeys[i],33) == 0 )
            {
                NOTARY_ADDRESS.assign(NOTARYADDRS[i]);
                STAKED_NOTARY_ID = i;
            }
        }
        pthread_mutex_unlock(&staked_mutex);
    }
}

CrosschainAuthority Choose_auth_STAKED(int32_t chosen_era) {
  CrosschainAuthority auth;
  auth.requiredSigs = (num_notaries_STAKED[chosen_era-1] / 5);
  auth.size = num_notaries_STAKED[chosen_era-1];
  for (int n=0; n<auth.size; n++)
      for (size_t i=0; i<33; i++)
          sscanf(notaries_STAKED[chosen_era-1][n][1]+(i*2), "%2hhx", auth.notaries[n]+i);
  return auth;
};
