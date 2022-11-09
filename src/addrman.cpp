// Copyright (c) 2012 Pieter Wuille
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

#include "addrman.h"

#include "hash.h"
#include "serialize.h"
#include "streams.h"
#include "init.h"

int CAddrInfo::GetTriedBucket(const uint256& nKey, const std::vector<bool> &asmap) const
{
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << GetKey()).GetHash().GetCheapHash();
    uint64_t hash2 = (CHashWriter(SER_GETHASH, 0) << nKey << GetGroup(asmap) << (hash1 % ADDRMAN_TRIED_BUCKETS_PER_GROUP)).GetHash().GetCheapHash();
    int tried_bucket = hash2 % ADDRMAN_TRIED_BUCKET_COUNT;
    uint32_t mapped_as = GetMappedAS(asmap);
    LogPrint("net", "IP %s mapped to AS%i belongs to tried bucket %i\n", ToStringIP(), mapped_as, tried_bucket);
    return tried_bucket;
}

int CAddrInfo::GetNewBucket(const uint256& nKey, const CNetAddr& src, const std::vector<bool> &asmap) const
{
    std::vector<unsigned char> vchSourceGroupKey = src.GetGroup(asmap);
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << GetGroup(asmap) << vchSourceGroupKey).GetHash().GetCheapHash();
    uint64_t hash2 = (CHashWriter(SER_GETHASH, 0) << nKey << vchSourceGroupKey << (hash1 % ADDRMAN_NEW_BUCKETS_PER_SOURCE_GROUP)).GetHash().GetCheapHash();
    int new_bucket = hash2 % ADDRMAN_NEW_BUCKET_COUNT;
    uint32_t mapped_as = GetMappedAS(asmap);
    LogPrint("net", "IP %s mapped to AS%i belongs to new bucket %i\n", ToStringIP(), mapped_as, new_bucket);
    return new_bucket;
}

int CAddrInfo::GetBucketPosition(const uint256 &nKey, bool fNew, int nBucket) const
{
    uint64_t hash1 = (CHashWriter(SER_GETHASH, 0) << nKey << (fNew ? 'N' : 'K') << nBucket << GetKey()).GetHash().GetCheapHash();
    return hash1 % ADDRMAN_BUCKET_SIZE;
}

bool CAddrInfo::IsTerrible(int64_t nNow) const
{
    if (fLocal) //never remove local addresses
        return false;

    if (nLastTry && nLastTry >= nNow - 60) // never remove things tried in the last minute
        return false;

    if (nTime > nNow + 10 * 60) // came in a flying DeLorean
        return true;

    if (nTime == 0 || nNow - nTime > ADDRMAN_HORIZON_DAYS * 24 * 60 * 60) // not seen in recent history
        return true;

    if (nLastSuccess == 0 && nAttempts >= ADDRMAN_RETRIES) // tried N times and never a success
        return true;

    if (nNow - nLastSuccess > ADDRMAN_MIN_FAIL_DAYS * 24 * 60 * 60 && nAttempts >= ADDRMAN_MAX_FAILURES) // N successive failures in the last week
        return true;

    return false;
}

bool CAddrInfo::IsJustTried(int64_t nNow) const
{
    if (nLastTry && nLastTry >= nNow - 60)
        return true;

    return false;
}

double CAddrInfo::GetChance(int64_t nNow) const
{
    double fChance = 1.0;

    int64_t nSinceLastSeen = nNow - nTime;
    int64_t nSinceLastTry = nNow - nLastTry;

    if (nSinceLastSeen < 0)
        nSinceLastSeen = 0;
    if (nSinceLastTry < 0)
        nSinceLastTry = 0;

    // deprioritize very recent attempts away
    if (nSinceLastTry < 60 * 10)
        fChance *= 0.01;

    // deprioritize 66% after each failed attempt, but at most 1/28th to avoid the search taking forever or overly penalizing outages.
    fChance *= pow(0.66, std::min(nAttempts, 8));

    return fChance;
}

CAddrInfo* CAddrMan::Find(const CNetAddr& addr, int* pnId)
{
    AssertLockHeld(cs);

    const auto it = mapAddr.find(addr);
    if (it == mapAddr.end())
        return nullptr;
    if (pnId)
        *pnId = (*it).second;
    const auto it2 = mapInfo.find((*it).second);
    if (it2 != mapInfo.end())
        return &(*it2).second;
    return nullptr;
}

CAddrInfo* CAddrMan::Create(const CAddress& addr, const CNetAddr& addrSource, int* pnId)
{
    AssertLockHeld(cs);

    int nId = nIdCount;
    mapInfo[nId] = CAddrInfo(addr, addrSource);
    mapAddr[addr] = nId;
    mapInfo[nId].nRandomPos = vRandom.size();
    vRandom.push_back(nId);
    nNew++;
    nIdCount++;
    if (pnId)
        *pnId = nId;

    return &mapInfo[nId];
}

void CAddrMan::SwapRandom(unsigned int nRndPos1, unsigned int nRndPos2)
{
    AssertLockHeld(cs);

    if (nRndPos1 == nRndPos2)
        return;

    assert(nRndPos1 < vRandom.size() && nRndPos2 < vRandom.size());

    int nId1 = vRandom[nRndPos1];
    int nId2 = vRandom[nRndPos2];

    const auto it_1{mapInfo.find(nId1)};
    const auto it_2{mapInfo.find(nId2)};
    assert(it_1 != mapInfo.end());
    assert(it_2 != mapInfo.end());

    it_1->second.nRandomPos = nRndPos2;
    it_2->second.nRandomPos = nRndPos1;

    vRandom[nRndPos1] = nId2;
    vRandom[nRndPos2] = nId1;
}

void CAddrMan::Delete(int nId)
{
    AssertLockHeld(cs);

    const auto it{mapInfo.find(nId)};
    if (it != mapInfo.end()) {
        CAddrInfo& info = (*it).second;
        assert(!info.fInTried);
        assert(info.nRefCount == 0);

        SwapRandom(info.nRandomPos, vRandom.size() - 1);
        vRandom.pop_back();
        mapAddr.erase(info);
        mapInfo.erase(nId);
        nNew--;
    }

}

void CAddrMan::ClearNew(int nUBucket, int nUBucketPos)
{
    AssertLockHeld(cs);

    // if there is an entry in the specified bucket, delete it.
    if (vvNew[nUBucket][nUBucketPos] != -1) {
        int nIdDelete = vvNew[nUBucket][nUBucketPos];
        const auto it{mapInfo.find(nIdDelete)};
        if (it != mapInfo.end()) {
            CAddrInfo& infoDelete = (*it).second;
            assert(infoDelete.nRefCount > 0);
            infoDelete.nRefCount--;
            vvNew[nUBucket][nUBucketPos] = -1;
            if (infoDelete.nRefCount == 0) {
                Delete(nIdDelete);
            }
        }
    }

}

void CAddrMan::MakeTried(CAddrInfo& info, int nId)
{
    AssertLockHeld(cs);

    // remove the entry from all new buckets
    const int start_bucket{info.GetNewBucket(nKey, m_asmap)};
    for (int n = 0; n < ADDRMAN_NEW_BUCKET_COUNT; ++n) {
        const int bucket{(start_bucket + n) % ADDRMAN_NEW_BUCKET_COUNT};
        const int pos{info.GetBucketPosition(nKey, true, bucket)};
        if (vvNew[bucket][pos] == nId) {
            vvNew[bucket][pos] = -1;
            info.nRefCount--;
            if (info.nRefCount == 0) break;
        }
    }
    nNew--;

    assert(info.nRefCount == 0);

    // which tried bucket to move the entry to
    int nKBucket = info.GetTriedBucket(nKey, m_asmap);
    int nKBucketPos = info.GetBucketPosition(nKey, false, nKBucket);

    // first make space to add it (the existing tried entry there is moved to new, deleting whatever is there).
    if (vvTried[nKBucket][nKBucketPos] != -1) {
        // find an item to evict
        int nIdEvict = vvTried[nKBucket][nKBucketPos];
        assert(mapInfo.count(nIdEvict) == 1);
        CAddrInfo& infoOld = mapInfo[nIdEvict];

        // Remove the to-be-evicted item from the tried set.
        infoOld.fInTried = false;
        vvTried[nKBucket][nKBucketPos] = -1;
        nTried--;

        // find which new bucket it belongs to
        int nUBucket = infoOld.GetNewBucket(nKey, m_asmap);
        int nUBucketPos = infoOld.GetBucketPosition(nKey, true, nUBucket);
        ClearNew(nUBucket, nUBucketPos);
        assert(vvNew[nUBucket][nUBucketPos] == -1);

        // Enter it into the new set again.
        infoOld.nRefCount = 1;
        vvNew[nUBucket][nUBucketPos] = nIdEvict;
        nNew++;
    }
    assert(vvTried[nKBucket][nKBucketPos] == -1);

    vvTried[nKBucket][nKBucketPos] = nId;
    nTried++;
    info.fInTried = true;
}

void CAddrMan::Good_(const CService& addr, int64_t nTime)
{
    int nId;
    CAddrInfo* pinfo = Find(addr, &nId);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    info.nLastSuccess = nTime;
    info.nLastTry = nTime;
    info.nAttempts = 0;
    // nTime is not updated here, to avoid leaking information about
    // currently-connected peers.

    // if it is already in the tried set, don't do anything else
    if (info.fInTried)
        return;

    // find a bucket it is in now
    int nRnd = RandomInt(ADDRMAN_NEW_BUCKET_COUNT);
    int nUBucket = -1;
    for (unsigned int n = 0; n < ADDRMAN_NEW_BUCKET_COUNT; n++) {
        int nB = (n + nRnd) % ADDRMAN_NEW_BUCKET_COUNT;
        int nBpos = info.GetBucketPosition(nKey, true, nB);
        if (vvNew[nB][nBpos] == nId) {
            nUBucket = nB;
            break;
        }
    }

    // if no bucket is found, something bad happened;
    // TODO: maybe re-add the node, but for now, just bail out
    if (nUBucket == -1)
        return;

    LogPrint("addrman", "Moving %s to tried\n", addr.ToString());

    // move nId to the tried tables
    MakeTried(info, nId);
}

bool CAddrMan::Add_(const CAddress& addr, const CNetAddr& source, int64_t nTimePenalty)
{
    if (!addr.IsRoutable())
        return false;

    int nId;
    CAddrInfo* pinfo = Find(addr, &nId);

    if (pinfo) {
        // periodically update nTime
        bool fCurrentlyOnline = (GetTime() - addr.nTime < 24 * 60 * 60);
        int64_t nUpdateInterval = (fCurrentlyOnline ? 60 * 60 : 24 * 60 * 60);
        if (addr.nTime && (!pinfo->nTime || pinfo->nTime < addr.nTime - nUpdateInterval - nTimePenalty))
            pinfo->nTime = std::max((int64_t)0, addr.nTime - nTimePenalty);

        // add services
        pinfo->nServices |= addr.nServices;

        // do not update if no new information is present
        if (!addr.nTime || (pinfo->nTime && addr.nTime <= pinfo->nTime))
            return false;

        // do not update if the entry was already in the "tried" table
        if (pinfo->fInTried)
            return false;

        // do not update if the max reference count is reached
        if (pinfo->nRefCount == ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
            return false;

        // stochastic test: previous nRefCount == N: 2^N times harder to increase it
        int nFactor = 1;
        for (int n = 0; n < pinfo->nRefCount; n++)
            nFactor *= 2;
        if (nFactor > 1 && (RandomInt(nFactor) != 0))
            return false;
    } else {
        pinfo = Create(addr, source, &nId);
        pinfo->nTime = std::max((int64_t)0, (int64_t)pinfo->nTime - nTimePenalty);
    }

    int nUBucket = pinfo->GetNewBucket(nKey, source, m_asmap);
    int nUBucketPos = pinfo->GetBucketPosition(nKey, true, nUBucket);
    bool fInsert = vvNew[nUBucket][nUBucketPos] == -1;
    if (vvNew[nUBucket][nUBucketPos] != nId) {
        if (!fInsert) {
            const auto it{mapInfo.find(vvNew[nUBucket][nUBucketPos])};
            if (it != mapInfo.end()) {
                CAddrInfo& infoExisting = (*it).second;
                if (infoExisting.IsTerrible() || (infoExisting.nRefCount > 1 && pinfo->nRefCount == 0)) {
                    // Overwrite the existing new table entry.
                    fInsert = true;
                }
            }
        }
        if (fInsert) {
            ClearNew(nUBucket, nUBucketPos);
            pinfo->nRefCount++;
            vvNew[nUBucket][nUBucketPos] = nId;
        } else {
            if (pinfo->nRefCount == 0) {
                Delete(nId);
            }
        }
    }
    return fInsert;
}

void CAddrMan::Attempt_(const CService& addr, int64_t nTime)
{
    CAddrInfo* pinfo = Find(addr);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    info.nLastTry = nTime;
    info.nAttempts++;
}

CAddrInfo CAddrMan::Select_(bool newOnly)
{
    if (size() == 0)
        return CAddrInfo();

    // Track number of attempts to find a table entry, before giving up to avoid infinite loop
    const int kMaxRetries = 200000;         // magic number so unit tests can pass
    const int kRetriesBetweenSleep = 1000;
    const int kRetrySleepInterval = 100;    // milliseconds

    if (newOnly && nNew == 0)
        return CAddrInfo();

    // Use a 50% chance for choosing between tried and new table entries.
    if (!newOnly &&
       (nTried > 0 && (nNew == 0 || RandomInt(2) == 0))) {
        // use a tried node
        double fChanceFactor = 1.0;
        double fReachableFactor = 1.0;
        double fJustTried = 1.0;
        while (1) {
            if (ShutdownRequested()) //break loop on shutdown request
                return CAddrInfo();

            int i = 0;
            int nKBucket = RandomInt(ADDRMAN_TRIED_BUCKET_COUNT);
            int nKBucketPos = RandomInt(ADDRMAN_BUCKET_SIZE);
            while (vvTried[nKBucket][nKBucketPos] == -1) {
                nKBucket = (nKBucket + insecure_rand()) % ADDRMAN_TRIED_BUCKET_COUNT;
                nKBucketPos = (nKBucketPos + insecure_rand()) % ADDRMAN_BUCKET_SIZE;
                if (i++ > kMaxRetries)
                    return CAddrInfo();
                if (i % kRetriesBetweenSleep == 0 && !nKey.IsNull())
                    MilliSleep(kRetrySleepInterval);
            }
            int nId = vvTried[nKBucket][nKBucketPos];
            assert(mapInfo.count(nId) == 1);
            CAddrInfo& info = mapInfo[nId];
            if (info.IsReachableNetwork()) {
                //deprioritize unreachable networks
                fReachableFactor = 0.25;
            }
            if (info.IsJustTried()) {
                //deprioritize entries just tried
                fJustTried = 0.10;
            }
            if (RandomInt(1 << 30) < fChanceFactor * fReachableFactor * fJustTried * info.GetChance() * (1 << 30))
                return info;
            fChanceFactor *= 1.2;
        }
    } else {
        // use a new node
        double fChanceFactor = 1.0;
        double fReachableFactor = 1.0;
        double fJustTried = 1.0;
        while (1) {
            if (ShutdownRequested()) //break loop on shutdown request
                return CAddrInfo();

            int i = 0;
            int nUBucket = RandomInt(ADDRMAN_NEW_BUCKET_COUNT);
            int nUBucketPos = RandomInt(ADDRMAN_BUCKET_SIZE);
            while (vvNew[nUBucket][nUBucketPos] == -1) {
                nUBucket = (nUBucket + insecure_rand()) % ADDRMAN_NEW_BUCKET_COUNT;
                nUBucketPos = (nUBucketPos + insecure_rand()) % ADDRMAN_BUCKET_SIZE;
                if (i++ > kMaxRetries)
                    return CAddrInfo();
                if (i % kRetriesBetweenSleep == 0 && !nKey.IsNull())
                    MilliSleep(kRetrySleepInterval);
            }
            int nId = vvNew[nUBucket][nUBucketPos];
            assert(mapInfo.count(nId) == 1);
            CAddrInfo& info = mapInfo[nId];
            if (info.IsReachableNetwork()) {
                //deprioritize unreachable networks
                fReachableFactor = 0.25;
            }
            if (info.IsJustTried()) {
                //deprioritize entries just tried
                fJustTried = 0.10;
            }
            if (RandomInt(1 << 30) < fChanceFactor * fReachableFactor * fJustTried * info.GetChance() * (1 << 30))
                return info;
            fChanceFactor *= 1.2;
        }
    }

    return CAddrInfo();
}

#ifdef DEBUG_ADDRMAN
int CAddrMan::Check_()
{
    std::set<int> setTried;
    std::map<int, int> mapNew;

    if (vRandom.size() != nTried + nNew)
        return -7;

    for (std::map<int, CAddrInfo>::iterator it = mapInfo.begin(); it != mapInfo.end(); it++) {
        int n = (*it).first;
        CAddrInfo& info = (*it).second;
        if (info.fInTried) {
            if (!info.nLastSuccess)
                return -1;
            if (info.nRefCount)
                return -2;
            setTried.insert(n);
        } else {
            if (info.nRefCount < 0 || info.nRefCount > ADDRMAN_NEW_BUCKETS_PER_ADDRESS)
                return -3;
            if (!info.nRefCount)
                return -4;
            mapNew[n] = info.nRefCount;
        }
        if (mapAddr[info] != n)
            return -5;
        if (info.nRandomPos < 0 || info.nRandomPos >= vRandom.size() || vRandom[info.nRandomPos] != n)
            return -14;
        if (info.nLastTry < 0)
            return -6;
        if (info.nLastSuccess < 0)
            return -8;
    }

    if (setTried.size() != nTried)
        return -9;
    if (mapNew.size() != nNew)
        return -10;

    for (int n = 0; n < ADDRMAN_TRIED_BUCKET_COUNT; n++) {
        for (int i = 0; i < ADDRMAN_BUCKET_SIZE; i++) {
             if (vvTried[n][i] != -1) {
                 if (!setTried.count(vvTried[n][i]))
                     return -11;
                 if (mapInfo[vvTried[n][i]].GetTriedBucket(nKey, m_asmap) != n)
                     return -17;
                 if (mapInfo[vvTried[n][i]].GetBucketPosition(nKey, false, n) != i)
                     return -18;
                 setTried.erase(vvTried[n][i]);
             }
        }
    }

    for (int n = 0; n < ADDRMAN_NEW_BUCKET_COUNT; n++) {
        for (int i = 0; i < ADDRMAN_BUCKET_SIZE; i++) {
            if (vvNew[n][i] != -1) {
                if (!mapNew.count(vvNew[n][i]))
                    return -12;
                if (mapInfo[vvNew[n][i]].GetBucketPosition(nKey, true, n) != i)
                    return -19;
                if (--mapNew[vvNew[n][i]] == 0)
                    mapNew.erase(vvNew[n][i]);
            }
        }
    }

    if (setTried.size())
        return -13;
    if (mapNew.size())
        return -15;
    if (nKey.IsNull())
        return -16;

    return 0;
}
#endif

void CAddrMan::GetAddr_(std::vector<CAddress>& vAddr, bool wants_addrv2)
{
    unsigned int nNodes = ADDRMAN_GETADDR_MAX_PCT * vRandom.size() / 100;
    if (nNodes > ADDRMAN_GETADDR_MAX)
        nNodes = ADDRMAN_GETADDR_MAX;

    int addrv2Nodes = nNodes/5;
    int ipv4Nodes = 0;
    int ipv6Nodes = 0;
    int torNodes = 0;
    int i2pNodes = 0;
    int cjdnsNodes = 0;

    // Randomize Nodes
    for (unsigned int n = 0; n < vRandom.size(); n++) {
        int nRndPos = RandomInt(vRandom.size() - n) + n;
        SwapRandom(n, nRndPos);
    }

    // gather a list of random nodes, skipping those of low quality
    for (unsigned int n = 0; n < vRandom.size(); n++) {
        if (vAddr.size() >= nNodes)
            break;

        assert(mapInfo.count(vRandom[n]) == 1);
        const CAddrInfo& ai = mapInfo[vRandom[n]];

        if (!ai.IsTerrible()) {
            if (!wants_addrv2) {
                vAddr.push_back(ai);
            } else {
                if (ai.IsIPv4() && ipv4Nodes <= addrv2Nodes) {
                    vAddr.push_back(ai);
                    ipv4Nodes++;
                }
                if (ai.IsIPv6() && ipv6Nodes <= addrv2Nodes) {
                    vAddr.push_back(ai);
                    ipv6Nodes++;
                }
                if (ai.IsCJDNS() && cjdnsNodes <= addrv2Nodes) {
                    vAddr.push_back(ai);
                    cjdnsNodes++;
                }
                if (ai.IsTor() && torNodes <= addrv2Nodes) {
                    vAddr.push_back(ai);
                    torNodes++;
                }
                if (ai.IsI2P() && i2pNodes <= addrv2Nodes) {
                    vAddr.push_back(ai);
                    i2pNodes++;
                }
            }
        }
    }
}

void CAddrMan::Connected_(const CService& addr, int64_t nTime)
{
    CAddrInfo* pinfo = Find(addr);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    int64_t nUpdateInterval = 20 * 60;
    if (nTime - info.nTime > nUpdateInterval)
        info.nTime = nTime;
}

void CAddrMan::SetLocal_(const CService& addr)
{
    CAddrInfo* pinfo = Find(addr);

    // if not found, bail out
    if (!pinfo)
        return;

    CAddrInfo& info = *pinfo;

    // check whether we are talking about the exact same CService (including same port)
    if (info != addr)
        return;

    // update info
    info.fLocal = true;
}

int CAddrMan::RandomInt(int nMax){
    return GetRandInt(nMax);
}

void CAddrMan::GetAllPeers(std::map<std::string, int64_t> &info) {

    for(std::map<int, CAddrInfo>::iterator it = mapInfo.begin(); it != mapInfo.end(); it++) {
        info[(*it).second.ToStringIPPort()] = (*it).second.GetLastSuccess();
    }
    return;
}

std::vector<bool> CAddrMan::DecodeAsmap(fs::path path)
{
    std::vector<bool> bits;
    FILE *filestr = fsbridge::fopen(path, "rb");
    CAutoFile file(filestr, SER_DISK, CLIENT_VERSION);
    if (file.IsNull()) {
        LogPrintf("Failed to open asmap file from disk\n");
        return bits;
    }
    fseek(filestr, 0, SEEK_END);
    int length = ftell(filestr);
    LogPrintf("Opened asmap file %s (%d bytes) from disk\n", path, length);
    fseek(filestr, 0, SEEK_SET);
    char cur_byte;
    for (int i = 0; i < length; ++i) {
        file >> cur_byte;
        for (int bit = 0; bit < 8; ++bit) {
            bits.push_back((cur_byte >> bit) & 1);
        }
    }
    if (!SanityCheckASMap(bits)) {
        LogPrintf("Sanity check of asmap file %s failed\n", path);
        return {};
    }
    return bits;
}
