// Copyright (c) 2014 The Bitcoin Core developers
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

#ifndef BITCOIN_CHAINPARAMSBASE_H
#define BITCOIN_CHAINPARAMSBASE_H

#include <string>
#include <vector>

/**
 * CBaseChainParams defines the base parameters (shared between bitcoin-cli and bitcoind)
 * of a given instance of the Bitcoin system.
 */
class CBaseChainParams
{
public:
    enum Network {
        MAIN,
        TESTNET,
        REGTEST,

        MAX_NETWORK_TYPES
    };

    /***
     * @brief returns the subdirectory for the network
     * @return the data subdirectory ( nothing, or "testnet3" or "regtest" )
     */
    const std::string& DataDir() const { return strDataDir; }
    /****
     * @returns the port used for RPC calls
     */
    int RPCPort() const { return nRPCPort; }

protected:
    CBaseChainParams() {}

    int nRPCPort = 0;
    std::string strDataDir;
};

/**
 * NOTE: These params should not change after startup (except for unit tests)
 * @returns the currently selected parameters
 */
const CBaseChainParams& BaseParams();

/** 
 * Sets the params returned by Params() to those for the given network. 
 * @param network the network you wish to use
 */
void SelectBaseParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and returns the appropriate Network ID.
 * @returns Network ID or MAX_NETWORK_TYPES if an invalid combination is given
 */
CBaseChainParams::Network NetworkIdFromCommandLine();

/**
 * Calls NetworkIdFromCommandLine() and then calls SelectParams as appropriate.
 * @returns false if an invalid combination is given.
 */
bool SelectBaseParamsFromCommandLine();

/**
 * @returns true if SelectBaseParamsFromCommandLine() has been called
 */
bool AreBaseParamsConfigured();

#endif // BITCOIN_CHAINPARAMSBASE_H
