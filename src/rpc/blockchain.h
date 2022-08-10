#pragma once
/******************************************************************************
 * Copyright © 2021 Komodo Core Developers.                                   *
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

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);
UniValue mempoolInfoToJSON();
UniValue mempoolToJSON(bool fVerbose = false);
UniValue blockheaderToJSON(const CBlockIndex* blockindex);
