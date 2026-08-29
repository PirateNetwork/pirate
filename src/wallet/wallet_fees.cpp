// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/wallet_fees.h"

#include "init.h"
#include "policy/policy.h"
#include "txmempool.h"
#include "util.h"
#include "coincontrol.h"
#include "main.h"
#include "wallet/wallet.h"

namespace {
// minTxFee/payTxFee used to be process-global/static CWallet state; Phase 5
// made both real per-CWallet fields. Call sites not yet made wallet-aware
// (Qt's fee-preview code) pass wallet=nullptr and fall back to pwalletMain's
// setting, or a compiled-in default if no wallet is loaded at all -- the
// same behavior these call sites had before the settings became per-wallet.
CFeeRate GetEffectiveMinTxFee(const CWallet* wallet)
{
    if (wallet) return wallet->minTxFee;
    if (pwalletMain) return pwalletMain->minTxFee;
    return CFeeRate(1000);
}

CFeeRate GetEffectivePayTxFee(const CWallet* wallet)
{
    if (wallet) return wallet->payTxFee;
    if (pwalletMain) return pwalletMain->payTxFee;
    return CFeeRate(DEFAULT_TRANSACTION_FEE);
}
} // namespace

CAmount GetRequiredFee(unsigned int nTxBytes, const CWallet* wallet)
{
    return std::max(GetEffectiveMinTxFee(wallet).GetFee(nTxBytes), ::minRelayTxFee.GetFee(nTxBytes));
}

CAmount GetMinimumFee(unsigned int nTxBytes, const CCoinControl* coin_control, const CTxMemPool& pool, const CBlockPolicyEstimator& estimator, FeeCalculation *feeCalc, const CWallet* wallet)
{
    /* User control of how to calculate fee uses the following parameter precedence:
       1. coin_control.m_feerate
       2. coin_control.m_confirm_target
       3. payTxFee (now a per-wallet setting)
       4. nTxConfirmTarget (now a per-wallet setting)
       The first parameter that is set is used.
    */

    CAmount fee_needed;
    if (coin_control != nullptr) {
        if (coin_control->m_feerate) { // 1.
            fee_needed = coin_control->m_feerate->GetFee(nTxBytes);
            if (feeCalc) feeCalc->reason = FeeReason::PAYTXFEE;
            // Allow to override automatic min/max check over coin control instance
            if (coin_control->fOverrideFeeRate) return fee_needed;
        }
        else if (!coin_control->m_confirm_target && GetEffectivePayTxFee(wallet) != CFeeRate(0)) { // 3. TODO: remove magic value of 0 for payTxFee
            fee_needed = GetEffectivePayTxFee(wallet).GetFee(nTxBytes);
            if (feeCalc) feeCalc->reason = FeeReason::PAYTXFEE;
        }
        else { // 2. or 4.
            // We will use smart fee estimation
    //        unsigned int target = coin_control.m_confirm_target ? *coin_control.m_confirm_target : ::nTxConfirmTarget;
            // By default estimates are economical iff we are signaling opt-in-RBF
    //        bool conservative_estimate = !coin_control.signalRbf;
            // Allow to override the default fee estimate mode over the CoinControl instance
    //        if (coin_control.m_fee_mode == FeeEstimateMode::CONSERVATIVE) conservative_estimate = true;
    //        else if (coin_control.m_fee_mode == FeeEstimateMode::ECONOMICAL) conservative_estimate = false;

    //        fee_needed = estimator.estimateSmartFee(target, feeCalc, conservative_estimate).GetFee(nTxBytes);
    //        if (fee_needed == 0) {
                // if we don't have enough data for estimateSmartFee, then use fallbackFee
                fee_needed = CWallet::fallbackFee.GetFee(nTxBytes);
                if (feeCalc) feeCalc->reason = FeeReason::FALLBACK;
    //        }
            // Obey mempool min fee when using smart fee estimation
    //        CAmount min_mempool_fee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nTxBytes);
    //        if (fee_needed < min_mempool_fee) {
    //            fee_needed = min_mempool_fee;
    //            if (feeCalc) feeCalc->reason = FeeReason::MEMPOOL_MIN;
    //        }
        }
    }

    // prevent user from paying a fee below minRelayTxFee or minTxFee
    CAmount required_fee = GetRequiredFee(nTxBytes, wallet);
    if (required_fee > fee_needed) {
        fee_needed = required_fee;
        if (feeCalc) feeCalc->reason = FeeReason::REQUIRED;
    }
    // But always obey the maximum
    if (fee_needed > maxTxFee) {
        fee_needed = maxTxFee;
        if (feeCalc) feeCalc->reason = FeeReason::MAXTXFEE;
    }
    return fee_needed;
}

