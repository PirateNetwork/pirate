// Copyright (c) 2018-2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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


#ifndef CC_HEIR_H
#define CC_HEIR_H

#include "CCinclude.h"
#include "CCtokens.h"

//#define EVAL_HEIR 0xea

bool HeirValidate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx, uint32_t nIn);

class CoinHelper;
class TokenHelper;

// pwallet defaults to pwalletMain when not given, per the multiwallet effort's convention (see CCtx.cpp).
UniValue HeirFundCoinCaller(int64_t txfee, int64_t coins, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string memo, CWallet *pwallet=nullptr);
UniValue HeirFundTokenCaller(int64_t txfee, int64_t satoshis, std::string heirName, CPubKey heirPubkey, int64_t inactivityTimeSec, std::string memo, uint256 tokenid, CWallet *pwallet=nullptr);
UniValue HeirClaimCaller(uint256 fundingtxid, int64_t txfee, std::string amount, CWallet *pwallet=nullptr);
UniValue HeirAddCaller(uint256 fundingtxid, int64_t txfee, std::string amount, CWallet *pwallet=nullptr);
UniValue HeirInfo(uint256 fundingtxid);
UniValue HeirList();

#endif
