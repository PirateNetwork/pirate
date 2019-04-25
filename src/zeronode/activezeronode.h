// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEZERONODE_H
#define ACTIVEZERONODE_H

#include "init.h"
#include "key.h"
#include "zeronode/zeronode.h"
#include "net.h"
#include "zeronode/obfuscation.h"
#include "sync.h"
#include "wallet/wallet.h"

#define ACTIVE_ZERONODE_INITIAL 0 // initial state
#define ACTIVE_ZERONODE_SYNC_IN_PROCESS 1
#define ACTIVE_ZERONODE_INPUT_TOO_NEW 2
#define ACTIVE_ZERONODE_NOT_CAPABLE 3
#define ACTIVE_ZERONODE_STARTED 4

// Responsible for activating the Zeronode and pinging the network
class CActiveZeronode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    /// Ping Zeronode
    bool SendZeronodePing(std::string& errorMessage);

    /// Register any Zeronode
    bool Register(CTxIn vin, CService service, CKey key, CPubKey pubKey, CKey keyZeronode, CPubKey pubKeyZeronode, std::string& errorMessage);

    /// Get 10000 ZER input that can be used for the Zeronode
    bool GetZeroNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey, std::string strTxHash, std::string strOutputIndex);
    bool GetVinFromOutput(COutput out, CTxIn& vin, CPubKey& pubkey, CKey& secretKey);

public:
    // Initialized by init.cpp
    // Keys for the main Zeronode
    CPubKey pubKeyZeronode;

    // Initialized while registering Zeronode
    CTxIn vin;
    CService service;

    int status;
    std::string notCapableReason;

    CActiveZeronode()
    {
        status = ACTIVE_ZERONODE_INITIAL;
    }

    /// Manage status of main Zeronode
    void ManageStatus();
    std::string GetStatus();

    /// Register remote Zeronode
    bool Register(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& errorMessage);

    /// Get 10000 ZER input that can be used for the Zeronode
    bool GetZeroNodeVin(CTxIn& vin, CPubKey& pubkey, CKey& secretKey);
    vector<COutput> SelectCoinsZeronode();

    /// Enable cold wallet mode (run a Zeronode with no funds)
    bool EnableHotColdZeroNode(CTxIn& vin, CService& addr);
};

#endif
