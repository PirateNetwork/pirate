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
#include "uint256.h"
#include <cstdint>

// each era of this many blocks reduces block reward from 3 to 2 to 1
#define KOMODO_ENDOFERA 7777777

/****
 * @brief evidently a new way to calculate interest
 * @param txheight
 * @param nValue
 * @param nLockTime
 * @param tiptime
 * @return interest calculated
 */
uint64_t komodo_interestnew(int32_t txheight,uint64_t nValue,uint32_t nLockTime,uint32_t tiptime);

/****
 * @brief calculate interest
 * @param txheight
 * @param nValue
 * @param nLockTime
 * @param tiptime
 * @returns the interest
 */
uint64_t komodo_interest(int32_t txheight,uint64_t nValue,uint32_t nLockTime,uint32_t tiptime);

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
        int32_t checkheight,uint64_t checkvalue,int32_t tipheight);
