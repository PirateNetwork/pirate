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
#include "komodo_interest.h"
#include "komodo_bitcoind.h"
#include "komodo_utils.h" // dstr()
#include "komodo_hardfork.h"

#define KOMODO_INTEREST ((uint64_t)5000000) //((uint64_t)(0.05 * COIN))   // 5%

uint64_t _komodo_interestnew(int32_t txheight,uint64_t nValue,uint32_t nLockTime,uint32_t tiptime)
{
    int32_t minutes; 
    if ( nLockTime >= LOCKTIME_THRESHOLD 
            && tiptime > nLockTime 
            && (minutes= (tiptime - nLockTime) / 60) >= (KOMODO_MAXMEMPOOLTIME/60) )
    {
        if ( minutes > 365 * 24 * 60 )
            minutes = 365 * 24 * 60;
        if ( txheight >= 1000000 && minutes > 31 * 24 * 60 )
            minutes = 31 * 24 * 60;
        minutes -= ((KOMODO_MAXMEMPOOLTIME/60) - 1);
        uint64_t res = (nValue / 10512000) * minutes;
        if (txheight >= nS7HardforkHeight)
            res /= 500; // KIP-0001 implementation, reduce AUR from 5% to 0.01%
        return res;
    }
    return 0;
}

/****
 * @brief evidently a new way to calculate interest
 * @param txheight
 * @param nValue
 * @param nLockTime
 * @param tiptime
 * @return interest calculated
 */
uint64_t komodo_interestnew(int32_t txheight,uint64_t nValue,uint32_t nLockTime,uint32_t tiptime)
{
    if ( txheight < KOMODO_ENDOFERA 
            && nLockTime >= LOCKTIME_THRESHOLD 
            && tiptime != 0 
            && nLockTime < tiptime 
            && nValue >= 10*COIN )
        return _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
    return 0;
}

/****
 * @brief calculate interest
 * @param txheight
 * @param nValue
 * @param nLockTime
 * @param tiptime
 * @returns the interest
 */
uint64_t komodo_interest(int32_t txheight,uint64_t nValue,uint32_t nLockTime,uint32_t tiptime)
{
    int32_t minutes,exception; uint64_t interestnew,numerator,denominator,interest = 0; uint32_t activation;
    activation = 1491350400;  // 1491350400 5th April
    if ( !chainName.isKMD() )
        return(0);
    if ( txheight >= KOMODO_ENDOFERA )
        return 0;

    if ( nLockTime >= LOCKTIME_THRESHOLD && tiptime != 0 && nLockTime < tiptime && nValue >= 10*COIN )
    {
        int32_t minutes = (tiptime - nLockTime) / 60;
        if ( minutes >= 60 )
        {
            if ( minutes > 365 * 24 * 60 )
                minutes = 365 * 24 * 60;
            if ( txheight >= 250000 )
                minutes -= 59;
            uint64_t denominator = (((uint64_t)365 * 24 * 60) / minutes);
            if ( denominator == 0 )
                denominator = 1; // max KOMODO_INTEREST per transfer, do it at least annually!
            if ( nValue > 25000LL*COIN )
            {
                bool exception = false;
                if ( txheight <= 155949 )
                {
                    if ( (txheight == 116607 && nValue == 2502721100000LL) ||
                            (txheight == 126891 && nValue == 2879650000000LL) ||
                            (txheight == 129510 && nValue == 3000000000000LL) ||
                            (txheight == 141549 && nValue == 3500000000000LL) ||
                            (txheight == 154473 && nValue == 3983399350000LL) ||
                            (txheight == 154736 && nValue == 3983406748175LL) ||
                            (txheight == 155013 && nValue == 3983414006565LL) ||
                            (txheight == 155492 && nValue == 3983427592291LL) ||
                            (txheight == 155613 && nValue == 9997409999999797LL) ||
                            (txheight == 157927 && nValue == 9997410667451072LL) ||
                            (txheight == 155613 && nValue == 2590000000000LL) ||
                            (txheight == 155949 && nValue == 4000000000000LL) )
                    {
                        exception = true;
                    }
                    if ( exception || nValue == 4000000000000LL )
                        printf(">>>>>>>>>>>> exception.%d txheight.%d %.8f locktime %u vs tiptime %u <<<<<<<<<\n",(int32_t)exception,txheight,(double)nValue/COIN,nLockTime,tiptime);
                }
                if ( !exception )
                {
                    uint64_t numerator = (nValue / 20); // assumes 5%!
                    if ( txheight < 250000 )
                        interest = (numerator / denominator);
                    else if ( txheight < 1000000 )
                    {
                        interest = (numerator * minutes) / ((uint64_t)365 * 24 * 60);
                        uint64_t interestnew = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
                        if ( interest < interestnew )
                            printf("pathA current interest %.8f vs new %.8f for ht.%d %.8f locktime.%u tiptime.%u\n",
                                    dstr(interest),dstr(interestnew),txheight,dstr(nValue),nLockTime,tiptime);
                    }
                    else 
                        interest = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
                }
                else if ( txheight < 1000000 )
                {
                    uint64_t numerator = (nValue * KOMODO_INTEREST);
                    interest = (numerator / denominator) / COIN;
                    uint64_t interestnew = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
                    if ( interest < interestnew )
                        printf("pathB current interest %.8f vs new %.8f for ht.%d %.8f locktime.%u tiptime.%u\n",dstr(interest),dstr(interestnew),txheight,dstr(nValue),nLockTime,tiptime);
                }
                else 
                    interest = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
            }
            else
            {
                uint64_t numerator = (nValue * KOMODO_INTEREST);
                if ( txheight < 250000 || tiptime < activation )
                {
                    if ( txheight < 250000 || numerator * minutes < 365 * 24 * 60 )
                        interest = (numerator / denominator) / COIN;
                    else 
                        interest = ((numerator * minutes) / ((uint64_t)365 * 24 * 60)) / COIN;
                }
                else if ( txheight < 1000000 )
                {
                    uint64_t numerator = (nValue / 20); // assumes 5%!
                    interest = ((numerator * minutes) / ((uint64_t)365 * 24 * 60));
                    uint64_t interestnew = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
                    if ( interest < interestnew )
                        fprintf(stderr,"pathC current interest %.8f vs new %.8f for ht.%d %.8f locktime.%u tiptime.%u\n",dstr(interest),dstr(interestnew),txheight,dstr(nValue),nLockTime,tiptime);
                }
                else 
                    interest = _komodo_interestnew(txheight,nValue,nLockTime,tiptime);
            }
        }
    }
    return interest;
}

/****
 * @brief get information needed for interest calculation from a particular tx
 * @param txheighttimep time of block
 * @param txheightp height of block
 * @param tiptimep time of tip
 * @param valuep value of out at n
 * @param hash the transaction hash
 * @param n the vout to look for
 * @returns locktime
 */
uint32_t komodo_interest_args(uint32_t *txheighttimep,int32_t *txheightp,uint32_t *tiptimep,uint64_t *valuep,
        uint256 hash,int32_t n)
{
    *txheighttimep = *txheightp = *tiptimep = 0;
    *valuep = 0;

    LOCK(cs_main);
    CTransaction tx;
    uint256 hashBlock;
    if ( !GetTransaction(hash,tx,hashBlock,true) )
        return(0);
    uint32_t locktime = 0;
    if ( n < tx.vout.size() )
    {
        CBlockIndex *pindex = komodo_getblockindex(hashBlock);
        if ( pindex != nullptr )
        {
            *valuep = tx.vout[n].nValue;
            *txheightp = pindex->nHeight;
            *txheighttimep = pindex->nTime;
            CBlockIndex *tipindex;
            if ( *tiptimep == 0 && (tipindex= chainActive.Tip()) != 0 )
                *tiptimep = (uint32_t)tipindex->nTime;
            locktime = tx.nLockTime;
        }
    }
    return(locktime);
}

/****
 * @brief get accrued interest
 * @param[out] txheightp
 * @param[out] locktimep
 * @param[in] hash
 * @param[in] n
 * @param[in] checkheight
 * @param[in] checkvalue
 * @param[in] tipheight
 * @return the interest calculated
 */
uint64_t komodo_accrued_interest(int32_t *txheightp,uint32_t *locktimep,uint256 hash,int32_t n,
        int32_t checkheight,uint64_t checkvalue,int32_t tipheight)
{
    uint32_t tiptime=0; 
    CBlockIndex *pindex = chainActive[tipheight];
    if ( pindex != nullptr )
        tiptime = (uint32_t)pindex->nTime;
    else 
        fprintf(stderr,"cant find height[%d]\n",tipheight);

    uint32_t txheighttimep;
    uint64_t value;
    *locktimep = komodo_interest_args(&txheighttimep, txheightp, &tiptime, &value, hash, n);
    if ( *locktimep != 0 )
    {
        if ( (checkvalue == 0 || value == checkvalue) && (checkheight == 0 || *txheightp == checkheight) )
            return komodo_interest(*txheightp,value,*locktimep,tiptime);
        else 
            fprintf(stderr,"komodo_accrued_interest value mismatch %llu vs %llu or height mismatch %d vs %d\n",(long long)value,(long long)checkvalue,*txheightp,checkheight);
    }
    return 0;
}

