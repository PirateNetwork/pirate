// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZERONODE_H
#define ZERONODE_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

#define ZERONODE_MIN_CONFIRMATIONS 15
#define ZERONODE_MIN_MNP_SECONDS (10 * 60)
#define ZERONODE_MIN_MNB_SECONDS (5 * 60)
#define ZERONODE_PING_SECONDS (5 * 60)
#define ZERONODE_EXPIRATION_SECONDS (120 * 60)
#define ZERONODE_REMOVAL_SECONDS (130 * 60)
#define ZERONODE_CHECK_SECONDS 5

using namespace std;

class CZeronode;
class CZeronodeBroadcast;
class CZeronodePing;
extern map<int64_t, uint256> mapCacheBlockHashes;

bool GetBlockHash(uint256& hash, int nBlockHeight);


//
// The Zeronode Ping Class : Contains a different serialize method for sending pings from zeronodes throughout the network
//

class CZeronodePing
{
public:
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime; //znb message times
    std::vector<unsigned char> vchSig;
    //removed stop

    CZeronodePing();
    CZeronodePing(CTxIn& newVin);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);
    }

    bool CheckAndUpdate(int& nDos, bool fRequireEnabled = true);
    bool Sign(CKey& keyZeronode, CPubKey& pubKeyZeronode);
    void Relay();

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << vin;
        ss << sigTime;
        return ss.GetHash();
    }

    void swap(CZeronodePing& first, CZeronodePing& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.blockHash, second.blockHash);
        swap(first.sigTime, second.sigTime);
        swap(first.vchSig, second.vchSig);
    }

    CZeronodePing& operator=(CZeronodePing from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CZeronodePing& a, const CZeronodePing& b)
    {
        return a.vin == b.vin && a.blockHash == b.blockHash;
    }
    friend bool operator!=(const CZeronodePing& a, const CZeronodePing& b)
    {
        return !(a == b);
    }
};

//
// The Zeronode Class. For managing the Obfuscation process. It contains the input of the 1000 ZER, signature to prove
// it's the one who own that ip address and code for calculating the payment election.
//
class CZeronode
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;
    int64_t lastTimeChecked;

public:
    enum state {
        ZERONODE_PRE_ENABLED,
        ZERONODE_ENABLED,
        ZERONODE_EXPIRED,
        ZERONODE_OUTPOINT_SPENT,
        ZERONODE_REMOVE,
        ZERONODE_WATCHDOG_EXPIRED,
        ZERONODE_POSE_BAN,
        ZERONODE_VIN_SPENT,
        ZERONODE_POS_ERROR
    };

    CTxIn vin;
    CService addr;
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyZeronode;
    CPubKey pubKeyCollateralAddress1;
    CPubKey pubKeyZeronode1;
    std::vector<unsigned char> sig;
    int activeState;
    int64_t sigTime; //znb message time
    int cacheInputAge;
    int cacheInputAgeBlock;
    bool unitTest;
    bool allowFreeTx;
    int protocolVersion;
    int nActiveState;
    int64_t nLastDsq; //the dsq count from the last dsq broadcast of this node
    int nScanningErrorCount;
    int nLastScanningErrorBlockHeight;
    CZeronodePing lastPing;

    int64_t nLastDsee;  // temporary, do not save. Remove after migration to v12
    int64_t nLastDseep; // temporary, do not save. Remove after migration to v12

    CZeronode();
    CZeronode(const CZeronode& other);
    CZeronode(const CZeronodeBroadcast& znb);


    void swap(CZeronode& first, CZeronode& second) // nothrow
    {
        // enable ADL (not necessary in our case, but good practice)
        using std::swap;

        // by swapping the members of two classes,
        // the two classes are effectively swapped
        swap(first.vin, second.vin);
        swap(first.addr, second.addr);
        swap(first.pubKeyCollateralAddress, second.pubKeyCollateralAddress);
        swap(first.pubKeyZeronode, second.pubKeyZeronode);
        swap(first.sig, second.sig);
        swap(first.activeState, second.activeState);
        swap(first.sigTime, second.sigTime);
        swap(first.lastPing, second.lastPing);
        swap(first.cacheInputAge, second.cacheInputAge);
        swap(first.cacheInputAgeBlock, second.cacheInputAgeBlock);
        swap(first.unitTest, second.unitTest);
        swap(first.allowFreeTx, second.allowFreeTx);
        swap(first.protocolVersion, second.protocolVersion);
        swap(first.nLastDsq, second.nLastDsq);
        swap(first.nScanningErrorCount, second.nScanningErrorCount);
        swap(first.nLastScanningErrorBlockHeight, second.nLastScanningErrorBlockHeight);
    }

    CZeronode& operator=(CZeronode from)
    {
        swap(*this, from);
        return *this;
    }
    friend bool operator==(const CZeronode& a, const CZeronode& b)
    {
        return a.vin == b.vin;
    }
    friend bool operator!=(const CZeronode& a, const CZeronode& b)
    {
        return !(a.vin == b.vin);
    }

    // CALCULATE A RANK AGAINST OF GIVEN BLOCK
    arith_uint256 CalculateScore(const uint256& blockHash);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        LOCK(cs);

        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyZeronode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(activeState);
        READWRITE(lastPing);
        READWRITE(cacheInputAge);
        READWRITE(cacheInputAgeBlock);
        READWRITE(unitTest);
        READWRITE(allowFreeTx);
        READWRITE(nLastDsq);
        READWRITE(nScanningErrorCount);
        READWRITE(nLastScanningErrorBlockHeight);
    }

    int64_t SecondsSincePayment();

    bool UpdateFromNewBroadcast(CZeronodeBroadcast& znb);

    inline uint64_t SliceHash(uint256& hash, int slice)
    {
        uint64_t n = 0;
        memcpy(&n, &hash + slice * 64, 64);
        return n;
    }

    void Check(bool forceCheck = false);

    bool IsBroadcastedWithin(int seconds)
    {
        return (GetAdjustedTime() - sigTime) < seconds;
    }

    bool IsPingedWithin(int seconds, int64_t now = -1)
    {
        now == -1 ? now = GetAdjustedTime() : now;

        return (lastPing == CZeronodePing()) ? false : now - lastPing.sigTime < seconds;
    }

    void Disable()
    {
        sigTime = 0;
        lastPing = CZeronodePing();
    }

    bool IsEnabled()
    {
        return activeState == ZERONODE_ENABLED;
    }

    int GetZeronodeInputAge()
    {
        if (chainActive.Tip() == NULL) return 0;

        if (cacheInputAge == 0) {
            cacheInputAge = GetInputAge(vin);
            cacheInputAgeBlock = chainActive.Tip()->nHeight;
        }

        return cacheInputAge + (chainActive.Tip()->nHeight - cacheInputAgeBlock);
    }

    std::string GetStatus();

    std::string Status()
    {
        std::string strStatus = "ACTIVE";

        if (activeState == CZeronode::ZERONODE_ENABLED) strStatus = "ENABLED";
        if (activeState == CZeronode::ZERONODE_EXPIRED) strStatus = "EXPIRED";
        if (activeState == CZeronode::ZERONODE_VIN_SPENT) strStatus = "VIN_SPENT";
        if (activeState == CZeronode::ZERONODE_REMOVE) strStatus = "REMOVE";
        if (activeState == CZeronode::ZERONODE_POS_ERROR) strStatus = "POS_ERROR";

        return strStatus;
    }

    int64_t GetLastPaid();
    bool IsValidNetAddr();
};


//
// The Zeronode Broadcast Class : Contains a different serialize method for sending zeronodes through the network
//

class CZeronodeBroadcast : public CZeronode
{
public:
    CZeronodeBroadcast();
    CZeronodeBroadcast(CService newAddr, CTxIn newVin, CPubKey newPubkey, CPubKey newPubkey2, int protocolVersionIn);
    CZeronodeBroadcast(const CZeronode& zn);

    bool CheckAndUpdate(int& nDoS);
    bool CheckInputsAndAdd(int& nDos);
    bool Sign(CKey& keyCollateralAddress);
    void Relay();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(vin);
        READWRITE(addr);
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyZeronode);
        READWRITE(sig);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(lastPing);
        READWRITE(nLastDsq);
    }

    uint256 GetHash()
    {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << sigTime;
        ss << pubKeyCollateralAddress;
        return ss.GetHash();
    }

    /// Create Zeronode broadcast, needs to be relayed manually after that
    static bool Create(CTxIn vin, CService service, CKey keyCollateralAddressNew, CPubKey pubKeyCollateralAddressNew, CKey keyZeronodeNew, CPubKey pubKeyZeronodeNew, std::string& strErrorRet, CZeronodeBroadcast& znbRet);
    static bool Create(std::string strService, std::string strKey, std::string strTxHash, std::string strOutputIndex, std::string& strErrorRet, CZeronodeBroadcast& znbRet, bool fOffline = false);
};

#endif
