// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_WALLET_FEES_H
#define KOMODO_WALLET_FEES_H

#include "amount.h"

class CBlockPolicyEstimator;
class CCoinControl;
class CFeeRate;
class CTxMemPool;

/* Enumeration of reason for returned fee estimate */
enum class FeeReason {
    NONE,
    HALF_ESTIMATE,
    FULL_ESTIMATE,
    DOUBLE_ESTIMATE,
    CONSERVATIVE,
    MEMPOOL_MIN,
    PAYTXFEE,
    FALLBACK,
    REQUIRED,
    MAXTXFEE,
};

std::string StringForFeeReason(FeeReason reason);

/* Used to determine type of fee estimation requested */
enum class FeeEstimateMode {
    UNSET,        //! Use default settings based on other criteria
    ECONOMICAL,   //! Force estimateSmartFee to use non-conservative estimates
    CONSERVATIVE, //! Force estimateSmartFee to use conservative estimates
};

bool FeeModeFromString(const std::string& mode_string, FeeEstimateMode& fee_estimate_mode);

/* Used to return detailed information about a feerate bucket */
struct EstimatorBucket
{
    double start = -1;
    double end = -1;
    double withinTarget = 0;
    double totalConfirmed = 0;
    double inMempool = 0;
    double leftMempool = 0;
};

/* Used to return detailed information about a fee estimate calculation */
struct EstimationResult
{
    EstimatorBucket pass;
    EstimatorBucket fail;
    double decay = 0;
    unsigned int scale = 0;
};

struct FeeCalculation
{
    EstimationResult est;
    FeeReason reason = FeeReason::NONE;
    int desiredTarget = 0;
    int returnedTarget = 0;
};

/**
 * Return the minimum required fee taking into account the
 * floating relay fee and user set minimum transaction fee
 */
CAmount GetRequiredFee(unsigned int nTxBytes);

/**
 * Estimate the minimum fee considering user set parameters
 * and the required fee
 */
CAmount GetMinimumFee(unsigned int nTxBytes, const CCoinControl& coin_control, const CTxMemPool& pool, const CBlockPolicyEstimator& estimator, FeeCalculation *feeCalc);

#endif // KOMODO_WALLET_FEES_H
