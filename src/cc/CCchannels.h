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


#ifndef CC_CHANNELS_H
#define CC_CHANNELS_H

#include "CCinclude.h"
#define CHANNELS_MAXPAYMENTS 1000

bool ChannelsValidate(struct CCcontract_info *cp,Eval* eval,const CTransaction &tx, uint32_t nIn);
UniValue ChannelOpen(const CPubKey& pk,uint64_t txfee,CPubKey destpub,int32_t numpayments,int64_t payment,uint256 tokenid,CWallet *pwallet=nullptr);
UniValue ChannelPayment(const CPubKey& pk,uint64_t txfee,uint256 opentxid,int64_t amount, uint256 secret,CWallet *pwallet=nullptr);
UniValue ChannelClose(const CPubKey& pk,uint64_t txfee,uint256 opentxid,CWallet *pwallet=nullptr);
UniValue ChannelRefund(const CPubKey& pk,uint64_t txfee,uint256 opentxid,uint256 closetxid,CWallet *pwallet=nullptr);
UniValue ChannelsList(const CPubKey& pk);
// CCcustom
UniValue ChannelsInfo(const CPubKey& pk,uint256 opentxid);

#endif
