// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2017-2018 The Zero developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zeronode/zeronodeman.h"
#include "zeronode/activezeronode.h"
#include "addrman.h"
#include "zeronode/zeronode.h"
#include "zeronode/obfuscation.h"
#include "zeronode/spork.h"
#include "util.h"
#include "consensus/validation.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#define MN_WINNER_MINIMUM_AGE 8000    // Age in seconds. This should be > ZERONODE_REMOVAL_SECONDS to avoid misconfigured new nodes in the list.

/** Zeronode manager */
CZeronodeMan znodeman;

struct CompareLastPaid {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreTxIn {
    bool operator()(const pair<int64_t, CTxIn>& t1,
        const pair<int64_t, CTxIn>& t2) const
    {
        return t1.first < t2.first;
    }
};

struct CompareScoreMN {
    bool operator()(const pair<int64_t, CZeronode>& t1,
        const pair<int64_t, CZeronode>& t2) const
    {
        return t1.first < t2.first;
    }
};

//
// CZeronodeDB
//

CZeronodeDB::CZeronodeDB()
{
    pathMN = GetDataDir() / "zncache.dat";
    strMagicMessage = "ZeronodeCache";
}

bool CZeronodeDB::Write(const CZeronodeMan& znodemanToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssZeronodes(SER_DISK, CLIENT_VERSION);
    ssZeronodes << strMagicMessage;                   // zeronode cache file specific magic message
    ssZeronodes << FLATDATA(Params().MessageStart()); // network specific magic number
    ssZeronodes << znodemanToSave;
    uint256 hash = Hash(ssZeronodes.begin(), ssZeronodes.end());
    ssZeronodes << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathMN.string());

    // Write and commit header, data
    try {
        fileout << ssZeronodes;
    } catch (std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    //    FileCommit(fileout);
    fileout.fclose();

    LogPrint("zeronode","Written info to zncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("zeronode","  %s\n", znodemanToSave.ToString());

    return true;
}

CZeronodeDB::ReadResult CZeronodeDB::Read(CZeronodeMan& znodemanToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fopen(pathMN.string().c_str(), "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathMN.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = boost::filesystem::file_size(pathMN);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssZeronodes(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssZeronodes.begin(), ssZeronodes.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (zeronode cache file specific magic message) and ..

        ssZeronodes >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid zeronode cache magic message", __func__);
            return IncorrectMagicMessage;
        }

        // de-serialize file header (network specific magic number) and ..
        ssZeronodes >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }
        // de-serialize data into CZeronodeMan object
        ssZeronodes >> znodemanToLoad;
    } catch (std::exception& e) {
        znodemanToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint("zeronode","Loaded info from zncache.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint("zeronode","  %s\n", znodemanToLoad.ToString());
    if (!fDryRun) {
        LogPrint("zeronode","Zeronode manager - cleaning....\n");
        znodemanToLoad.CheckAndRemove(true);
        LogPrint("zeronode","Zeronode manager - result:\n");
        LogPrint("zeronode","  %s\n", znodemanToLoad.ToString());
    }

    return Ok;
}

void DumpZeronodes()
{
    int64_t nStart = GetTimeMillis();

    CZeronodeDB zndb;
    CZeronodeMan tempZnodeman;

    LogPrint("zeronode","Verifying zncache.dat format...\n");
    CZeronodeDB::ReadResult readResult = zndb.Read(tempZnodeman, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CZeronodeDB::FileError)
        LogPrint("zeronode","Missing zeronode cache file - zncache.dat, will try to recreate\n");
    else if (readResult != CZeronodeDB::Ok) {
        LogPrint("zeronode","Error reading zncache.dat: ");
        if (readResult == CZeronodeDB::IncorrectFormat)
            LogPrint("zeronode","magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint("zeronode","file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint("zeronode","Writting info to zncache.dat...\n");
    zndb.Write(znodeman);

    LogPrint("zeronode","Zeronode dump finished  %dms\n", GetTimeMillis() - nStart);
}

CZeronodeMan::CZeronodeMan()
{
    nDsqCount = 0;
}

bool CZeronodeMan::Add(CZeronode& zn)
{
    LOCK(cs);

    if (!zn.IsEnabled())
        return false;

    CZeronode* pzn = Find(zn.vin);
    if (pzn == NULL) {
        LogPrint("zeronode", "CZeronodeMan: Adding new Zeronode %s - %i now\n", zn.vin.prevout.hash.ToString(), size() + 1);
        vZeronodes.push_back(zn);
        return true;
    }

    return false;
}

void CZeronodeMan::AskForZN(CNode* pnode, CTxIn& vin)
{
    std::map<COutPoint, int64_t>::iterator i = mWeAskedForZeronodeListEntry.find(vin.prevout);
    if (i != mWeAskedForZeronodeListEntry.end()) {
        int64_t t = (*i).second;
        if (GetTime() < t) return; // we've asked recently
    }

    // ask for the znb info once from the node that sent znp

    LogPrint("zeronode", "CZeronodeMan::AskForZN - Asking node for missing entry, vin: %s\n", vin.prevout.hash.ToString());
    pnode->PushMessage("dseg", vin);
    int64_t askAgain = GetTime() + ZERONODE_MIN_MNP_SECONDS;
    mWeAskedForZeronodeListEntry[vin.prevout] = askAgain;
}

void CZeronodeMan::Check()
{
    LOCK(cs);

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();
    }
}

void CZeronodeMan::CheckAndRemove(bool forceExpiredRemoval)
{
    Check();

    LOCK(cs);

    //remove inactive and outdated
    vector<CZeronode>::iterator it = vZeronodes.begin();
    while (it != vZeronodes.end()) {
        if ((*it).activeState == CZeronode::ZERONODE_REMOVE ||
            (*it).activeState == CZeronode::ZERONODE_VIN_SPENT ||
            (forceExpiredRemoval && (*it).activeState == CZeronode::ZERONODE_EXPIRED) ||
            (*it).protocolVersion < zeronodePayments.GetMinZeronodePaymentsProto()) {
            LogPrint("zeronode", "CZeronodeMan: Removing inactive Zeronode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);

            //erase all of the broadcasts we've seen from this vin
            // -- if we missed a few pings and the node was removed, this will allow is to get it back without them
            //    sending a brand new znb
            map<uint256, CZeronodeBroadcast>::iterator it3 = mapSeenZeronodeBroadcast.begin();
            while (it3 != mapSeenZeronodeBroadcast.end()) {
                if ((*it3).second.vin == (*it).vin) {
                    zeronodeSync.mapSeenSyncZNB.erase((*it3).first);
                    mapSeenZeronodeBroadcast.erase(it3++);
                } else {
                    ++it3;
                }
            }

            // allow us to ask for this zeronode again if we see another ping
            map<COutPoint, int64_t>::iterator it2 = mWeAskedForZeronodeListEntry.begin();
            while (it2 != mWeAskedForZeronodeListEntry.end()) {
                if ((*it2).first == (*it).vin.prevout) {
                    mWeAskedForZeronodeListEntry.erase(it2++);
                } else {
                    ++it2;
                }
            }

            it = vZeronodes.erase(it);
        } else {
            ++it;
        }
    }

    // check who's asked for the Zeronode list
    map<CNetAddr, int64_t>::iterator it1 = mAskedUsForZeronodeList.begin();
    while (it1 != mAskedUsForZeronodeList.end()) {
        if ((*it1).second < GetTime()) {
            mAskedUsForZeronodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check who we asked for the Zeronode list
    it1 = mWeAskedForZeronodeList.begin();
    while (it1 != mWeAskedForZeronodeList.end()) {
        if ((*it1).second < GetTime()) {
            mWeAskedForZeronodeList.erase(it1++);
        } else {
            ++it1;
        }
    }

    // check which Zeronodes we've asked for
    map<COutPoint, int64_t>::iterator it2 = mWeAskedForZeronodeListEntry.begin();
    while (it2 != mWeAskedForZeronodeListEntry.end()) {
        if ((*it2).second < GetTime()) {
            mWeAskedForZeronodeListEntry.erase(it2++);
        } else {
            ++it2;
        }
    }

    // remove expired mapSeenZeronodeBroadcast
    map<uint256, CZeronodeBroadcast>::iterator it3 = mapSeenZeronodeBroadcast.begin();
    while (it3 != mapSeenZeronodeBroadcast.end()) {
        if ((*it3).second.lastPing.sigTime < GetTime() - (ZERONODE_REMOVAL_SECONDS * 2)) {
            mapSeenZeronodeBroadcast.erase(it3++);
            zeronodeSync.mapSeenSyncZNB.erase((*it3).second.GetHash());
        } else {
            ++it3;
        }
    }

    // remove expired mapSeenZeronodePing
    map<uint256, CZeronodePing>::iterator it4 = mapSeenZeronodePing.begin();
    while (it4 != mapSeenZeronodePing.end()) {
        if ((*it4).second.sigTime < GetTime() - (ZERONODE_REMOVAL_SECONDS * 2)) {
            mapSeenZeronodePing.erase(it4++);
        } else {
            ++it4;
        }
    }
}

void CZeronodeMan::Clear()
{
    LOCK(cs);
    vZeronodes.clear();
    mAskedUsForZeronodeList.clear();
    mWeAskedForZeronodeList.clear();
    mWeAskedForZeronodeListEntry.clear();
    mapSeenZeronodeBroadcast.clear();
    mapSeenZeronodePing.clear();
    nDsqCount = 0;
}

int CZeronodeMan::stable_size ()
{
    int nStable_size = 0;
    int nMinProtocol = ActiveProtocol();
    int64_t nZeronode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nZeronode_Age = 0;

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.protocolVersion < nMinProtocol) {
            continue; // Skip obsolete versions
        }
        if (IsSporkActive (SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT)) {
            nZeronode_Age = GetAdjustedTime() - zn.sigTime;
            if ((nZeronode_Age) < nZeronode_Min_Age) {
                continue; // Skip zeronodes younger than (default) 8000 sec (MUST be > ZERONODE_REMOVAL_SECONDS)
            }
        }
        zn.Check ();
        if (!zn.IsEnabled ())
            continue; // Skip not-enabled zeronodes

        nStable_size++;
    }

    return nStable_size;
}

int CZeronodeMan::CountEnabled(int protocolVersion)
{
    int i = 0;
    protocolVersion = protocolVersion == -1 ? zeronodePayments.GetMinZeronodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();
        if (zn.protocolVersion < protocolVersion || !zn.IsEnabled()) continue;
        i++;
    }

    return i;
}

void CZeronodeMan::CountNetworks(int protocolVersion, int& ipv4, int& ipv6, int& onion)
{
    protocolVersion = protocolVersion == -1 ? zeronodePayments.GetMinZeronodePaymentsProto() : protocolVersion;

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();
        std::string strHost;
        int port;
        SplitHostPort(zn.addr.ToString(), port, strHost);
        CNetAddr node = CNetAddr(strHost, false);
        int nNetwork = node.GetNetwork();
        switch (nNetwork) {
            case 1 :
                ipv4++;
                break;
            case 2 :
                ipv6++;
                break;
            case 3 :
                onion++;
                break;
        }
    }
}

void CZeronodeMan::DsegUpdate(CNode* pnode)
{
    LOCK(cs);

    if (NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
        if (!(pnode->addr.IsRFC1918() || pnode->addr.IsLocal())) {
            std::map<CNetAddr, int64_t>::iterator it = mWeAskedForZeronodeList.find(pnode->addr);
            if (it != mWeAskedForZeronodeList.end()) {
                if (GetTime() < (*it).second) {
                    LogPrint("zeronode", "dseg - we already asked peer %i for the list; skipping...\n", pnode->GetId());
                    return;
                }
            }
        }
    }

    pnode->PushMessage("dseg", CTxIn());
    int64_t askAgain = GetTime() + ZERONODES_DSEG_SECONDS;
    mWeAskedForZeronodeList[pnode->addr] = askAgain;
}

CZeronode* CZeronodeMan::Find(const CScript& payee)
{
    LOCK(cs);
    CScript payee2;

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        payee2 = GetScriptForDestination(zn.pubKeyCollateralAddress.GetID());
        if (payee2 == payee)
            return &zn;
    }
    return NULL;
}

CZeronode* CZeronodeMan::Find(const CTxIn& vin)
{
    LOCK(cs);

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.vin.prevout == vin.prevout)
            return &zn;
    }
    return NULL;
}


CZeronode* CZeronodeMan::Find(const CPubKey& pubKeyZeronode)
{
    LOCK(cs);

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.pubKeyZeronode == pubKeyZeronode)
            return &zn;
    }
    return NULL;
}

//
// Deterministically select the oldest/best zeronode to pay on the network
//
CZeronode* CZeronodeMan::GetNextZeronodeInQueueForPayment(int nBlockHeight, bool fFilterSigTime, int& nCount)
{
    LOCK(cs);

    CZeronode* pBestZeronode = NULL;
    std::vector<pair<int64_t, CTxIn> > vecZeronodeLastPaid;

    /*
        Make a vector with all of the last paid times
    */

    int nMnCount = CountEnabled();
    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();
        if (!zn.IsEnabled()) continue;

        // //check protocol version
        if (zn.protocolVersion < zeronodePayments.GetMinZeronodePaymentsProto()) continue;

        //it's in the list (up to 8 entries ahead of current block to allow propagation) -- so let's skip it
        if (zeronodePayments.IsScheduled(zn, nBlockHeight)) continue;

        //it's too new, wait for a cycle
        if (fFilterSigTime && zn.sigTime + (nMnCount * 2.6 * 60) > GetAdjustedTime()) continue;

        //make sure it has as many confirmations as there are zeronodes
        if (zn.GetZeronodeInputAge() < nMnCount) continue;

        vecZeronodeLastPaid.push_back(make_pair(zn.SecondsSincePayment(), zn.vin));
    }

    nCount = (int)vecZeronodeLastPaid.size();

    //when the network is in the process of upgrading, don't penalize nodes that recently restarted
    if (fFilterSigTime && nCount < nMnCount / 3) return GetNextZeronodeInQueueForPayment(nBlockHeight, false, nCount);

    // Sort them high to low
    sort(vecZeronodeLastPaid.rbegin(), vecZeronodeLastPaid.rend(), CompareLastPaid());

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight - 101)) {
        LogPrintf("CZeronode::GetNextZeronodeInQueueForPayment -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight - 101);
        return NULL;
    }

    // Look at 1/10 of the oldest nodes (by last payment), calculate their scores and pay the best one
    //  -- This doesn't look at who is being paid in the +8-10 blocks, allowing for double payments very rarely
    //  -- 1/100 payments should be a double payment on mainnet - (1/(3000/10))*2
    //  -- (chance per block * chances before IsScheduled will fire)
    // Look at all nodes when node count is equal to ZERONODES_MIN_PAYMENT_COUNT or less
    int nMinMnCount = ZERONODES_MIN_PAYMENT_COUNT;
    int nTenthNetwork = nMnCount / 10;
    if (nMinMnCount > nTenthNetwork) nTenthNetwork = nMinMnCount;

    int nCountTenth = 0;
    arith_uint256 nHighest = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecZeronodeLastPaid) {
        CZeronode* pzn = Find(s.second);
        if (!pzn) break;

        arith_uint256 n = pzn->CalculateScore(blockHash);
        if (n > nHighest) {
            nHighest = n;
            pBestZeronode = pzn;
        }
        nCountTenth++;
        if (nCountTenth >= nTenthNetwork) break;
    }
    return pBestZeronode;
}

CZeronode* CZeronodeMan::FindRandomNotInVec(std::vector<CTxIn>& vecToExclude, int protocolVersion)
{
    LOCK(cs);

    protocolVersion = protocolVersion == -1 ? zeronodePayments.GetMinZeronodePaymentsProto() : protocolVersion;

    int nCountEnabled = CountEnabled(protocolVersion);
    LogPrint("zeronode", "CZeronodeMan::FindRandomNotInVec - nCountEnabled - vecToExclude.size() %d\n", nCountEnabled - vecToExclude.size());
    if (nCountEnabled - vecToExclude.size() < 1) return NULL;

    int rand = GetRandInt(nCountEnabled - vecToExclude.size());
    LogPrint("zeronode", "CZeronodeMan::FindRandomNotInVec - rand %d\n", rand);
    bool found;

    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.protocolVersion < protocolVersion || !zn.IsEnabled()) continue;
        found = false;
        BOOST_FOREACH (CTxIn& usedVin, vecToExclude) {
            if (zn.vin.prevout == usedVin.prevout) {
                found = true;
                break;
            }
        }
        if (found) continue;
        if (--rand < 1) {
            return &zn;
        }
    }

    return NULL;
}

CZeronode* CZeronodeMan::GetCurrentZeroNode(int mod, int64_t nBlockHeight, int minProtocol)
{
    int64_t score = 0;
    CZeronode* winner = NULL;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if(!GetBlockHash(blockHash, nBlockHeight)) return NULL;

    // scan for winner
    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();
        if (zn.protocolVersion < minProtocol || !zn.IsEnabled()) continue;

        // calculate the score for each Zeronode
        arith_uint256 n = zn.CalculateScore(blockHash);
        int64_t n2 = n.GetCompact(false);

        // determine the winner
        if (n2 > score) {
            score = n2;
            winner = &zn;
        }
    }

    return winner;
}

int CZeronodeMan::GetZeronodeRank(const CTxIn& vin, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecZeronodeScores;
    int64_t nZeronode_Min_Age = MN_WINNER_MINIMUM_AGE;
    int64_t nZeronode_Age = 0;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, nBlockHeight)) return -1;

    // scan for winner
    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.protocolVersion < minProtocol) {
            LogPrint("zeronode","Skipping Zeronode with obsolete version %d\n", zn.protocolVersion);
            continue;                                                       // Skip obsolete versions
        }

        if (IsSporkActive(SPORK_8_ZERONODE_PAYMENT_ENFORCEMENT)) {
            nZeronode_Age = GetAdjustedTime() - zn.sigTime;
            if ((nZeronode_Age) < nZeronode_Min_Age) {
                if (fDebug) LogPrint("zeronode","Skipping just activated Zeronode. Age: %ld\n", nZeronode_Age);
                continue;                                                   // Skip zeronodes younger than (default) 1 hour
            }
        }
        if (fOnlyActive) {
            zn.Check();
            if (!zn.IsEnabled()) continue;
        }
        arith_uint256 n = zn.CalculateScore(blockHash);
        int64_t n2 = n.GetCompact(false);

        vecZeronodeScores.push_back(make_pair(n2, zn.vin));
    }

    sort(vecZeronodeScores.rbegin(), vecZeronodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecZeronodeScores) {
        rank++;
        if (s.second.prevout == vin.prevout) {
            return rank;
        }
    }

    return -1;
}

std::vector<pair<int, CZeronode> > CZeronodeMan::GetZeronodeRanks(int64_t nBlockHeight, int minProtocol)
{
    std::vector<pair<int64_t, CZeronode> > vecZeronodeScores;
    std::vector<pair<int, CZeronode> > vecZeronodeRanks;

    //make sure we know about this block
    uint256 blockHash = uint256();
    if (!GetBlockHash(blockHash, nBlockHeight)) return vecZeronodeRanks;

    // scan for winner
    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        zn.Check();

        if (zn.protocolVersion < minProtocol) continue;

        if (!zn.IsEnabled()) {
            vecZeronodeScores.push_back(make_pair(9999, zn));
            continue;
        }

        arith_uint256 n = zn.CalculateScore(blockHash);
        int64_t n2 = n.GetCompact(false);

        vecZeronodeScores.push_back(make_pair(n2, zn));
    }

    sort(vecZeronodeScores.rbegin(), vecZeronodeScores.rend(), CompareScoreMN());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CZeronode) & s, vecZeronodeScores) {
        rank++;
        vecZeronodeRanks.push_back(make_pair(rank, s.second));
    }

    return vecZeronodeRanks;
}

CZeronode* CZeronodeMan::GetZeronodeByRank(int nRank, int64_t nBlockHeight, int minProtocol, bool fOnlyActive)
{
    std::vector<pair<int64_t, CTxIn> > vecZeronodeScores;

    uint256 blockHash;
    if(!GetBlockHash(blockHash, nBlockHeight)) {
        LogPrintf("CZeronode::GetZeronodeByRank -- ERROR: GetBlockHash() failed at nBlockHeight %d\n", nBlockHeight);
        return NULL;
    }

    // scan for winner
    BOOST_FOREACH (CZeronode& zn, vZeronodes) {
        if (zn.protocolVersion < minProtocol) continue;
        if (fOnlyActive) {
            zn.Check();
            if (!zn.IsEnabled()) continue;
        }

        arith_uint256 n = zn.CalculateScore(blockHash);
        int64_t n2 = n.GetCompact(false);

        vecZeronodeScores.push_back(make_pair(n2, zn.vin));
    }

    sort(vecZeronodeScores.rbegin(), vecZeronodeScores.rend(), CompareScoreTxIn());

    int rank = 0;
    BOOST_FOREACH (PAIRTYPE(int64_t, CTxIn) & s, vecZeronodeScores) {
        rank++;
        if (rank == nRank) {
            return Find(s.second);
        }
    }

    return NULL;
}

// void CZeronodeMan::ProcessZeronodeConnections()
// {
//     //we don't care about this for regtest
//     if (NetworkIdFromCommandLine() == CBaseChainParams::REGTEST) return;
//
//     LOCK(cs_vNodes);
//     BOOST_FOREACH (CNode* pnode, vNodes) {
//         if (pnode->fObfuScationMaster) {
//             if (obfuScationPool.pSubmittedToZeronode != NULL && pnode->addr == obfuScationPool.pSubmittedToZeronode->addr) continue;
//             LogPrint("zeronode","Closing Zeronode connection peer=%i \n", pnode->GetId());
//             pnode->fObfuScationMaster = false;
//             pnode->Release();
//         }
//     }
// }

void CZeronodeMan::ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (fLiteMode) return; //disable all Obfuscation/Zeronode related functionality
    if (!zeronodeSync.IsBlockchainSynced()) return;

    LOCK(cs_process_message);

    if (strCommand == "znb") { //Zeronode Broadcast
        CZeronodeBroadcast znb;
        vRecv >> znb;

        if (mapSeenZeronodeBroadcast.count(znb.GetHash())) { //seen
            zeronodeSync.AddedZeronodeList(znb.GetHash());
            return;
        }
        mapSeenZeronodeBroadcast.insert(make_pair(znb.GetHash(), znb));

        int nDoS = 0;
        if (!znb.CheckAndUpdate(nDoS)) {
            if (nDoS > 0)
            {
                Misbehaving(pfrom->GetId(), nDoS);
            }

            //failed
            return;
        }

        // make sure the vout that was signed is related to the transaction that spawned the Zeronode
        //  - this is expensive, so it's only done once per Zeronode
        if (!obfuScationSigner.IsVinAssociatedWithPubkey(znb.vin, znb.pubKeyCollateralAddress)) {
            LogPrint("zeronode","znb - Got mismatched pubkey and vin\n");
            Misbehaving(pfrom->GetId(), 33);
            return;
        }

        // make sure it's still unspent
        //  - this is checked later by .check() in many places and by ThreadCheckObfuScationPool()
        if (znb.CheckInputsAndAdd(nDoS)) {
            // use this as a peer
            addrman.Add(CAddress(znb.addr), pfrom->addr, 2 * 60 * 60);
            zeronodeSync.AddedZeronodeList(znb.GetHash());
        } else {
            LogPrint("zeronode","znb - Rejected Zeronode entry %s\n", znb.vin.prevout.hash.ToString());

            if (nDoS > 0)
            {
                Misbehaving(pfrom->GetId(), nDoS);
            }
        }
    }

    else if (strCommand == "znp") { //Zeronode Ping
        CZeronodePing znp;
        vRecv >> znp;

        LogPrint("zeronode", "znp - Zeronode ping, vin: %s\n", znp.vin.prevout.hash.ToString());

        if (mapSeenZeronodePing.count(znp.GetHash())) return; //seen
        mapSeenZeronodePing.insert(make_pair(znp.GetHash(), znp));

        int nDoS = 0;
        if (znp.CheckAndUpdate(nDoS)) return;

        if (nDoS > 0) {
            // if anything significant failed, mark that node
            Misbehaving(pfrom->GetId(), nDoS);
        } else {
            // if nothing significant failed, search existing Zeronode list
            CZeronode* pzn = Find(znp.vin);
            // if it's known, don't ask for the znb, just return
            if (pzn != NULL) return;
        }

        // something significant is broken or zn is unknown,
        // we might have to ask for a zeronode entry once
        AskForZN(pfrom, znp.vin);

    } else if (strCommand == "dseg") { //Get Zeronode list or specific entry

        CTxIn vin;
        vRecv >> vin;

        if (vin == CTxIn()) { //only should ask for this once
            //local network
            bool isLocal = (pfrom->addr.IsRFC1918() || pfrom->addr.IsLocal());

            if (!isLocal && NetworkIdFromCommandLine() == CBaseChainParams::MAIN) {
                std::map<CNetAddr, int64_t>::iterator i = mAskedUsForZeronodeList.find(pfrom->addr);
                if (i != mAskedUsForZeronodeList.end()) {
                    int64_t t = (*i).second;
                    if (GetTime() < t) {
                        Misbehaving(pfrom->GetId(), 34);
                        LogPrint("zeronode","dseg - peer already asked me for the list\n");
                        return;
                    }
                }
                int64_t askAgain = GetTime() + ZERONODES_DSEG_SECONDS;
                mAskedUsForZeronodeList[pfrom->addr] = askAgain;
            }
        } //else, asking for a specific node which is ok


        int nInvCount = 0;

        BOOST_FOREACH (CZeronode& zn, vZeronodes) {
            if (zn.addr.IsRFC1918()) continue; //local network

            if (zn.IsEnabled()) {
                LogPrint("zeronode", "dseg - Sending Zeronode entry - %s \n", zn.vin.prevout.hash.ToString());
                if (vin == CTxIn() || vin == zn.vin) {
                    CZeronodeBroadcast znb = CZeronodeBroadcast(zn);
                    uint256 hash = znb.GetHash();
                    pfrom->PushInventory(CInv(MSG_ZERONODE_ANNOUNCE, hash));
                    nInvCount++;

                    if (!mapSeenZeronodeBroadcast.count(hash)) mapSeenZeronodeBroadcast.insert(make_pair(hash, znb));

                    if (vin == zn.vin) {
                        LogPrint("zeronode", "dseg - Sent 1 Zeronode entry to peer %i\n", pfrom->GetId());
                        return;
                    }
                }
            }
        }

        if (vin == CTxIn()) {
            pfrom->PushMessage("ssc", ZERONODE_SYNC_LIST, nInvCount);
            LogPrint("zeronode", "dseg - Sent %d Zeronode entries to peer %i\n", nInvCount, pfrom->GetId());
        }
    }
}

void CZeronodeMan::Remove(CTxIn vin)
{
    LOCK(cs);

    vector<CZeronode>::iterator it = vZeronodes.begin();
    while (it != vZeronodes.end()) {
        if ((*it).vin == vin) {
            LogPrint("zeronode", "CZeronodeMan: Removing Zeronode %s - %i now\n", (*it).vin.prevout.hash.ToString(), size() - 1);
            vZeronodes.erase(it);
            break;
        }
        ++it;
    }
}

void CZeronodeMan::UpdateZeronodeList(CZeronodeBroadcast znb)
{
    LOCK(cs);
    mapSeenZeronodePing.insert(std::make_pair(znb.lastPing.GetHash(), znb.lastPing));
    mapSeenZeronodeBroadcast.insert(std::make_pair(znb.GetHash(), znb));

    LogPrint("zeronode","CZeronodeMan::UpdateZeronodeList -- zeronode=%s\n", znb.vin.prevout.ToStringShort());

    CZeronode* pzn = Find(znb.vin);
    if (pzn == NULL) {
        CZeronode zn(znb);
        if (Add(zn)) {
            zeronodeSync.AddedZeronodeList(znb.GetHash());
        }
    } else if (pzn->UpdateFromNewBroadcast(znb)) {
        zeronodeSync.AddedZeronodeList(znb.GetHash());
    }
}

std::string CZeronodeMan::ToString() const
{
    std::ostringstream info;

    info << "Zeronodes: " << (int)vZeronodes.size() << ", peers who asked us for Zeronode list: " << (int)mAskedUsForZeronodeList.size() << ", peers we asked for Zeronode list: " << (int)mWeAskedForZeronodeList.size() << ", entries in Zeronode list we asked for: " << (int)mWeAskedForZeronodeListEntry.size() << ", nDsqCount: " << (int)nDsqCount;

    return info.str();
}
