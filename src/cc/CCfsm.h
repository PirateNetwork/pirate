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


#ifndef CC_FSM_H
#define CC_FSM_H

#include "CCinclude.h"

#define EVAL_FSM 0xe7

bool FSMValidate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx, uint32_t nIn);

std::string FSMList();
std::string FSMInfo(uint256 fsmtxid);
// pwallet defaults to pwalletMain when not given, per the multiwallet effort's convention (see CCtx.cpp).
std::string FSMCreate(uint64_t txfee,std::string name,std::string states,CWallet *pwallet=nullptr);

#endif
