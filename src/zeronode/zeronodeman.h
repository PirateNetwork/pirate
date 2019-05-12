// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ZERONODEMAN_H
#define ZERONODEMAN_H

#include "base58.h"
#include "key.h"
#include "main.h"
#include "zeronode/zeronode.h"
#include "net.h"
#include "sync.h"
#include "util.h"

#define ZERONODES_DUMP_SECONDS (15 * 60)
#define ZERONODES_DSEG_SECONDS (3 * 60 * 60)
#define ZERONODES_MIN_PAYMENT_COUNT 10

using namespace std;

class CZeronodeMan;

extern CZeronodeMan znodeman;
void DumpZeronodes();

/** Access to the MN database (zncache.dat)
 */
class CZeronodeDB
{
private:
    boost::filesystem::path pathMN;
    std::string strMagicMessage;

public:
    enum ReadResult {
        Ok,
        FileError,
        HashReadError,
        IncorrectHash,
        IncorrectMagicMessage,
        IncorrectMagicNumber,
        IncorrectFormat
    };

    CZeronodeDB();
    bool Write(const CZeronodeMan& znodemanToSave);
    ReadResult Read(CZeronodeMan& znodemanToLoad, bool fDryRun = false);
};

class CZeronodeMan
{
private:
    // critical section to protect the inner data structures
    mutable CCriticalSection cs;

    // critical section to protect the inner data structures specifically on messaging
    mutable CCriticalSection cs_process_message;

    // map to hold all MNs
    std::vector<CZeronode> vZeronodes;
    // who's asked for the Zeronode list and the last time
    std::map<CNetAddr, int64_t> mAskedUsForZeronodeList;
    // who we asked for the Zeronode list and the last time
    std::map<CNetAddr, int64_t> mWeAskedForZeronodeList;
    // which Zeronodes we've asked for
    std::map<COutPoint, int64_t> mWeAskedForZeronodeListEntry;

public:
    // Keep track of all broadcasts I've seen
    map<uint256, CZeronodeBroadcast> mapSeenZeronodeBroadcast;
    // Keep track of all pings I've seen
    map<uint256, CZeronodePing> mapSeenZeronodePing;

    // keep track of dsq count to prevent zeronodes from gaming obfuscation queue
    int64_t nDsqCount;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        LOCK(cs);
        READWRITE(vZeronodes);
        READWRITE(mAskedUsForZeronodeList);
        READWRITE(mWeAskedForZeronodeList);
        READWRITE(mWeAskedForZeronodeListEntry);
        READWRITE(nDsqCount);

        READWRITE(mapSeenZeronodeBroadcast);
        READWRITE(mapSeenZeronodePing);
    }

    CZeronodeMan();
    CZeronodeMan(CZeronodeMan& other);

    /// Add an entry
    bool Add(CZeronode& zn);

    /// Ask (source) node for znb
    void AskForZN(CNode* pnode, CTxIn& vin);

    /// Check all Zeronodes
    void Check();

    /// Check all Zeronodes and remove inactive
    void CheckAndRemove(bool forceExpiredRemoval = false);

    /// Clear Zeronode vector
    void Clear();

    int CountEnabled(int protocolVersion = -1);

    void CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion);

    void DsegUpdate(CNode* pnode);

    /// Find an entry
    CZeronode* Find(const CScript& payee);
    CZeronode* Find(const CTxIn& vin);
    CZeronode* Find(const CPubKey& pubKeyZeronode);

    /// Find an entry in the zeronode list that is next to be paid
    CZeronode* GetNextZeronodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount);

    /// Find a random entry
    CZeronode* FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion = -1);

    /// Get the current winner for this block
    CZeronode* GetCurrentZeroNode(int mod = 1, int64_t nBlockHeight = 0, int minProtocol = 0);

    std::vector<CZeronode> GetFullZeronodeVector()
    {
        Check();
        return vZeronodes;
    }

    std::vector<pair<int, CZeronode> > GetZeronodeRanks(int64_t nBlockHeight, int minProtocol = 0);
    int GetZeronodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);
    CZeronode* GetZeronodeByRank(int nRank, int64_t nBlockHeight, int minProtocol = 0, bool fOnlyActive = true);

    void ProcessZeronodeConnections();

    void ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);

    /// Return the number of (unique) Zeronodes
    int size() { return vZeronodes.size(); }

    /// Return the number of Zeronodes older than (default) 8000 seconds
    int stable_size ();

    std::string ToString() const;

    void Remove(CTxIn vin);

    /// Update zeronode list and maps using provided CZeronodeBroadcast
    void UpdateZeronodeList(CZeronodeBroadcast znb);
};

#endif
