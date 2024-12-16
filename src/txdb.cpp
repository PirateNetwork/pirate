// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2016-2022 The Zcash developers
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

#include "txdb.h"

#include "chainparams.h"
#include "hash.h"
#include "main.h"
#include "pow.h"
#include "uint256.h"
#include "core_io.h"
#include "komodo_bitcoind.h"

#include "ui_interface.h"
#include "init.h"

#include <stdint.h>

#include <boost/thread.hpp>

using namespace std;

//Anchors
static const char DB_SPROUT_ANCHOR = 'A';
static const char DB_SAPLING_ANCHOR = 'Z';
static const char DB_SAPLING_FRONTIER_ANCHOR = 'Y';
static const char DB_ORCHARD_FRONTIER_ANCHOR = 'X';

//Best Anchors
static const char DB_BEST_SPROUT_ANCHOR = 'a';
static const char DB_BEST_SAPLING_ANCHOR = 'z';
static const char DB_BEST_SAPLING_FRONTIER_ANCHOR = 'y';
static const char DB_BEST_ORCHARD_FRONTIER_ANCHOR = 'x';

//Nullifiers
static const char DB_SPROUT_NULLIFIER = 's';
static const char DB_SAPLING_NULLIFIER = 'S';
static const char DB_ORCHARD_NULLIFIER = 'o';

//Indexes
static const char DB_TXINDEX = 't';
static const char DB_ADDRESSINDEX = 'd';
static const char DB_ADDRESSUNSPENTINDEX = 'u';
static const char DB_TIMESTAMPINDEX = 'H';
static const char DB_BLOCKHASHINDEX = 'h';
static const char DB_SPENTINDEX = 'p';
static const char DB_BLOCK_INDEX = 'b';

//History Node
static const char DB_MMR_LENGTH = 'M';
static const char DB_MMR_NODE = 'm';
static const char DB_MMR_ROOT = 'r';

static const char DB_COINS = 'c';
static const char DB_BLOCK_FILES = 'f';
static const char DB_BEST_BLOCK = 'B';
static const char DB_FLAG = 'F';
static const char DB_REINDEX_FLAG = 'R';
static const char DB_LAST_BLOCK = 'l';

static const char SPEND_PROOF_HASH = 'e';
static const char OUTPUT_PROOF_HASH = 'E';

static const char DB_VERSION = 'V';

CCoinsViewDB::CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / dbName, nCacheSize, fMemory, fWipe) {
}

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe) : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe)
{
}


bool CCoinsViewDB::GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
    if (rt == SproutMerkleTree::empty_root()) {
        SproutMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SPROUT_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const {
    if (rt == SaplingMerkleTree::empty_root()) {
        SaplingMerkleTree new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SAPLING_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const {
    if (rt == SaplingMerkleFrontier::empty_root()) {
        SaplingMerkleFrontier new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_SAPLING_FRONTIER_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const {
    if (rt == OrchardMerkleFrontier::empty_root()) {
        OrchardMerkleFrontier new_tree;
        tree = new_tree;
        return true;
    }

    bool read = db.Read(make_pair(DB_ORCHARD_FRONTIER_ANCHOR, rt), tree);

    return read;
}

bool CCoinsViewDB::GetNullifier(const uint256 &nf, ShieldedType type) const {
    bool spent = false;
    char dbChar;
    switch (type) {
        case SPROUT:
            dbChar = DB_SPROUT_NULLIFIER;
            break;
        case SAPLING:
            dbChar = DB_SAPLING_NULLIFIER;
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }
    return db.Read(make_pair(dbChar, nf), spent);
}

bool CCoinsViewDB::GetCoins(const uint256 &txid, CCoins &coins) const {
    return db.Read(make_pair(DB_COINS, txid), coins);
}

bool CCoinsViewDB::HaveCoins(const uint256 &txid) const {
    return db.Exists(make_pair(DB_COINS, txid));
}

uint256 CCoinsViewDB::GetBestBlock() const {
    uint256 hashBestChain;
    if (!db.Read(DB_BEST_BLOCK, hashBestChain))
        return uint256();
    return hashBestChain;
}

uint256 CCoinsViewDB::GetBestAnchor(ShieldedType type) const {
    uint256 hashBestAnchor;

    switch (type) {
        case SPROUT:
            if (!db.Read(DB_BEST_SPROUT_ANCHOR, hashBestAnchor))
                return SproutMerkleTree::empty_root();
            break;
        case SAPLING:
            if (!db.Read(DB_BEST_SAPLING_ANCHOR, hashBestAnchor))
                return SaplingMerkleTree::empty_root();
            break;
        case SAPLINGFRONTIER:
            if (!db.Read(DB_BEST_SAPLING_FRONTIER_ANCHOR, hashBestAnchor))
                return SaplingMerkleFrontier::empty_root();
            break;
        case ORCHARDFRONTIER:
            if (!db.Read(DB_BEST_ORCHARD_FRONTIER_ANCHOR, hashBestAnchor))
                return OrchardMerkleFrontier::empty_root();
            break;
        default:
            throw runtime_error("Unknown shielded type");
    }

    return hashBestAnchor;
}

HistoryIndex CCoinsViewDB::GetHistoryLength(uint32_t epochId) const {
    HistoryIndex historyLength;
    if (!db.Read(make_pair(DB_MMR_LENGTH, epochId), historyLength)) {
        // Starting new history
        historyLength = 0;
    }

    return historyLength;
}

HistoryNode CCoinsViewDB::GetHistoryAt(uint32_t epochId, HistoryIndex index) const {
    HistoryNode mmrNode = {};

    if (index >= GetHistoryLength(epochId)) {
        throw runtime_error("History data inconsistent - reindex?");
    }

    if (libzcash::IsV1HistoryTree(epochId)) {
        // History nodes serialized by `zcashd` versions that were unaware of NU5, used
        // the previous shorter maximum serialized length. Because we stored this as an
        // array, we can't just read the current (longer) maximum serialized length, as
        // it will result in an exception for those older nodes.
        //
        // Instead, we always read an array of the older length. This works as expected
        // for V1 nodes serialized by older clients, while for V1 nodes serialized by
        // NU5-aware clients this is guaranteed to ignore only trailing zero bytes.
        std::array<unsigned char, NODE_V1_SERIALIZED_LENGTH> tmpMmrNode;
        if (!db.Read(make_pair(DB_MMR_NODE, make_pair(epochId, index)), tmpMmrNode)) {
            throw runtime_error("History data inconsistent (expected node not found) - reindex?");
        }
        std::copy(std::begin(tmpMmrNode), std::end(tmpMmrNode), mmrNode.begin());
    } else {
        if (!db.Read(make_pair(DB_MMR_NODE, make_pair(epochId, index)), mmrNode)) {
            throw runtime_error("History data inconsistent (expected node not found) - reindex?");
        }
    }

    return mmrNode;
}

uint256 CCoinsViewDB::GetHistoryRoot(uint32_t epochId) const {
    uint256 root;
    if (!db.Read(make_pair(DB_MMR_ROOT, epochId), root))
    {
        root = uint256();
    }
    return root;
}

void BatchWriteNullifiers(CDBBatch& batch, CNullifiersMap& mapToUse, const char& dbChar)
{
    for (CNullifiersMap::iterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & CNullifiersCacheEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else
                batch.Write(make_pair(dbChar, it->first), true);
            // TODO: changed++? ... See comment in CCoinsViewDB::BatchWrite. If this is needed we could return an int
        }
        CNullifiersMap::iterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

template<typename Map, typename MapIterator, typename MapEntry, typename Tree>
void BatchWriteAnchors(CDBBatch& batch, Map& mapToUse, const char& dbChar)
{
    for (MapIterator it = mapToUse.begin(); it != mapToUse.end();) {
        if (it->second.flags & MapEntry::DIRTY) {
            if (!it->second.entered)
                batch.Erase(make_pair(dbChar, it->first));
            else {
                if (it->first != Tree::empty_root()) {
                    batch.Write(make_pair(dbChar, it->first), it->second.tree);
                }
            }
            // TODO: changed++?
        }
        MapIterator itOld = it++;
        mapToUse.erase(itOld);
    }
}

void BatchWriteHistory(CDBBatch& batch, CHistoryCacheMap& historyCacheMap) {
    for (auto nextHistoryCache = historyCacheMap.begin(); nextHistoryCache != historyCacheMap.end(); nextHistoryCache++) {
        auto historyCache = nextHistoryCache->second;
        auto epochId = nextHistoryCache->first;

        // delete old entries since updateDepth
        for (int i = historyCache.updateDepth + 1; i <= historyCache.length; i++) {
            batch.Erase(make_pair(DB_MMR_NODE, make_pair(epochId, i)));
        }

        // replace/append new/updated entries
        for (auto it = historyCache.appends.begin(); it != historyCache.appends.end(); it++) {
            batch.Write(make_pair(DB_MMR_NODE, make_pair(epochId, it->first)), it->second);
        }

        // write new length
        batch.Write(make_pair(DB_MMR_LENGTH, epochId), historyCache.length);

        // write current root
        batch.Write(make_pair(DB_MMR_ROOT, epochId), historyCache.root);
    }
}

bool CCoinsViewDB::BatchWrite(CCoinsMap &mapCoins,
                              const uint256 &hashBlock,
                              const uint256 &hashSproutAnchor,
                              const uint256 &hashSaplingAnchor,
                              const uint256 &hashSaplingFrontierAnchor,
                              const uint256 &hashOrchardFrontierAnchor,
                              CAnchorsSproutMap &mapSproutAnchors,
                              CAnchorsSaplingMap &mapSaplingAnchors,
                              CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                              CAnchorsOrchardFrontierMap &mapOrchardFrontierAnchors,
                              CNullifiersMap &mapSproutNullifiers,
                              CNullifiersMap &mapSaplingNullifiers,
                              CNullifiersMap &mapOrchardNullifiers,
                              CHistoryCacheMap &historyCacheMap) {
    CDBBatch batch(db);
    size_t count = 0;
    size_t changed = 0;
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coins.IsPruned())
                batch.Erase(make_pair(DB_COINS, it->first));
            else
                batch.Write(make_pair(DB_COINS, it->first), it->second.coins);
            changed++;
        }
        count++;
        CCoinsMap::iterator itOld = it++;
        mapCoins.erase(itOld);
    }

    ::BatchWriteAnchors<CAnchorsSproutMap, CAnchorsSproutMap::iterator, CAnchorsSproutCacheEntry, SproutMerkleTree>(batch, mapSproutAnchors, DB_SPROUT_ANCHOR);
    ::BatchWriteAnchors<CAnchorsSaplingMap, CAnchorsSaplingMap::iterator, CAnchorsSaplingCacheEntry, SaplingMerkleTree>(batch, mapSaplingAnchors, DB_SAPLING_ANCHOR);
    ::BatchWriteAnchors<CAnchorsSaplingFrontierMap, CAnchorsSaplingFrontierMap::iterator, CAnchorsSaplingFrontierCacheEntry, SaplingMerkleFrontier>(batch, mapSaplingFrontierAnchors, DB_SAPLING_FRONTIER_ANCHOR);
    ::BatchWriteAnchors<CAnchorsOrchardFrontierMap, CAnchorsOrchardFrontierMap::iterator, CAnchorsOrchardFrontierCacheEntry, OrchardMerkleFrontier>(batch, mapOrchardFrontierAnchors, DB_ORCHARD_FRONTIER_ANCHOR);

    ::BatchWriteNullifiers(batch, mapSproutNullifiers, DB_SPROUT_NULLIFIER);
    ::BatchWriteNullifiers(batch, mapSaplingNullifiers, DB_SAPLING_NULLIFIER);
    ::BatchWriteNullifiers(batch, mapOrchardNullifiers, DB_ORCHARD_NULLIFIER);

    ::BatchWriteHistory(batch, historyCacheMap);

    if (!hashBlock.IsNull())
        batch.Write(DB_BEST_BLOCK, hashBlock);
    if (!hashSproutAnchor.IsNull())
        batch.Write(DB_BEST_SPROUT_ANCHOR, hashSproutAnchor);
    if (!hashSaplingAnchor.IsNull())
        batch.Write(DB_BEST_SAPLING_ANCHOR, hashSaplingAnchor);
    if (!hashSaplingFrontierAnchor.IsNull())
        batch.Write(DB_BEST_SAPLING_FRONTIER_ANCHOR, hashSaplingFrontierAnchor);
    if (!hashOrchardFrontierAnchor.IsNull())
        batch.Write(DB_BEST_ORCHARD_FRONTIER_ANCHOR, hashOrchardFrontierAnchor);

    LogPrint("coindb", "Committing %u changed transactions (out of %u) to coin database...\n", (unsigned int)changed, (unsigned int)count);
    return db.WriteBatch(batch);
}

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe, bool compression, int maxOpenFiles)
        : CDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe, compression, maxOpenFiles) {
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) const {
    return Read(make_pair(DB_BLOCK_FILES, nFile), info);
}

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
    if (fReindexing)
        return Write(DB_REINDEX_FLAG, '1');
    else
        return Erase(DB_REINDEX_FLAG);
}

bool CBlockTreeDB::ReadReindexing(bool &fReindexing) const {
    fReindexing = Exists(DB_REINDEX_FLAG);
    return true;
}

bool CBlockTreeDB::WriteVersion() {
    return Write(DB_VERSION, CLIENT_VERSION);
}

bool CBlockTreeDB::ReadVersion(bool &fReindexing) const {
    if (!Exists(DB_VERSION)) {
        fReindexing = true;
        return true;
    } else {
        int db_version;
        Read(DB_VERSION, db_version);
        if (db_version < MIN_INDEX_VERSION) {
            fReindexing = true;
        }
    }
    return true;
}

bool CBlockTreeDB::ReadLastBlockFile(int &nFile) const {
    return Read(DB_LAST_BLOCK, nFile);
}

bool CCoinsViewDB::GetStats(CCoinsStats &stats) const {
    /* It seems that there are no "const iterators" for LevelDB.  Since we
       only need read operations on it, use a const-cast to get around
       that restriction.  */
    boost::scoped_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(&db)->NewIterator());
    pcursor->Seek(DB_COINS);

    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    stats.hashBlock = GetBestBlock();
    ss << stats.hashBlock;
    CAmount nTotalAmount = 0;
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        std::pair<char, uint256> key;
        CCoins coins;
        if (pcursor->GetKey(key) && key.first == DB_COINS) {
            if (pcursor->GetValue(coins)) {
                stats.nTransactions++;
                for (unsigned int i=0; i<coins.vout.size(); i++) {
                    const CTxOut &out = coins.vout[i];
                    if (!out.IsNull()) {
                        stats.nTransactionOutputs++;
                        ss << VARINT(i+1);
                        ss << out;
                        nTotalAmount += out.nValue;
                    }
                }
                stats.nSerializedSize += 32 + pcursor->GetValueSize();
                ss << VARINT(0);
            } else {
                return error("CCoinsViewDB::GetStats() : unable to read value");
            }
        } else {
            break;
        }
        pcursor->Next();
    }
    {
        LOCK(cs_main);
        stats.nHeight = mapBlockIndex.find(stats.hashBlock)->second->nHeight;
    }
    stats.hashSerialized = ss.GetHash();
    stats.nTotalAmount = nTotalAmount;
    return true;
}

/***
 * Write a batch of records and sync
 * @param fileInfo the records to write
 * @param nLastFile the value for DB_LAST_BLOCK
 * @param blockinfo the index records to write
 * @returns true on success
 */
bool CBlockTreeDB::WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, const std::vector<CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (const auto& it : fileInfo) {
        batch.Write(make_pair(DB_BLOCK_FILES, it.first), *it.second);
    }
    batch.Write(DB_LAST_BLOCK, nLastFile);
    for (const auto& it : blockinfo) {
        std::pair<char, uint256> key = make_pair(DB_BLOCK_INDEX, it->GetBlockHash());
        try {
            CDiskBlockIndex dbindex {it, [this, &key]() {
                // It can happen that the index entry is written, then the Equihash solution is cleared from memory,
                // then the index entry is rewritten. In that case we must read the solution from the old entry.
                CDiskBlockIndex dbindex_old;
                if (!Read(key, dbindex_old)) {
                    LogPrintf("%s: Failed to read index entry", __func__);
                    throw runtime_error("Failed to read index entry");
                }
                return dbindex_old.GetSolution();
            }};
            batch.Write(key, dbindex);
        } catch (const runtime_error&) {
            return false;
        }
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::EraseBatchSync(const std::vector<const CBlockIndex*>& blockinfo) {
    CDBBatch batch(*this);
    for (std::vector<const CBlockIndex*>::const_iterator it=blockinfo.begin(); it != blockinfo.end(); it++) {
        batch.Erase(make_pair(DB_BLOCK_INDEX, (*it)->GetBlockHash()));
    }
    return WriteBatch(batch, true);
}

bool CBlockTreeDB::ReadDiskBlockIndex(const uint256 &blockhash, CDiskBlockIndex &dbindex) const {
    return Read(make_pair(DB_BLOCK_INDEX, blockhash), dbindex);
}

bool CBlockTreeDB::ReadTxIndex(const uint256 &txid, CDiskTxPos &pos) const {
    return Read(make_pair(DB_TXINDEX, txid), pos);
}

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<uint256,CDiskTxPos> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_TXINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value) const {
    return Read(make_pair(DB_SPENTINDEX, key), value);
}

bool CBlockTreeDB::UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CSpentIndexKey,CSpentIndexValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_SPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_SPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=vect.begin(); it!=vect.end(); it++) {
        if (it->second.IsNull()) {
            batch.Erase(make_pair(DB_ADDRESSUNSPENTINDEX, it->first));
        } else {
            batch.Write(make_pair(DB_ADDRESSUNSPENTINDEX, it->first), it->second);
        }
    }
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressUnspentIndex(uint160 addressHash, int type,
                                           std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &unspentOutputs) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_ADDRESSUNSPENTINDEX, CAddressIndexIteratorKey(type, addressHash)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            pair<char, CAddressUnspentKey> keyObj;
            pcursor->GetKey(keyObj);
            char chType = keyObj.first;
            CAddressUnspentKey indexKey = keyObj.second;

            if (chType == DB_ADDRESSUNSPENTINDEX && indexKey.hashBytes == addressHash) {
                try {
                    CAddressUnspentValue nValue;
                    pcursor->GetValue(nValue);
                    unspentOutputs.push_back(make_pair(indexKey, nValue));
                    pcursor->Next();
                } catch (const std::exception& e) {
                    return error("failed to get address unspent value");
                }
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }
    return true;
}

bool CBlockTreeDB::WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Write(make_pair(DB_ADDRESSINDEX, it->first), it->second);
    return WriteBatch(batch);
}

bool CBlockTreeDB::EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount > >&vect) {
    CDBBatch batch(*this);
    for (std::vector<std::pair<CAddressIndexKey, CAmount> >::const_iterator it=vect.begin(); it!=vect.end(); it++)
        batch.Erase(make_pair(DB_ADDRESSINDEX, it->first));
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadAddressIndex(uint160 addressHash, int type,
                                    std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                                    int start, int end) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    if (start > 0 && end > 0) {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorHeightKey(type, addressHash, start)));
    } else {
        pcursor->Seek(make_pair(DB_ADDRESSINDEX, CAddressIndexIteratorKey(type, addressHash)));
    }

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            pair<char, CAddressIndexKey> keyObj;
            pcursor->GetKey(keyObj);
            char chType = keyObj.first;
            CAddressIndexKey indexKey = keyObj.second;

            if (chType == DB_ADDRESSINDEX && indexKey.hashBytes == addressHash) {
                if (end > 0 && indexKey.blockHeight > end) {
                    break;
                }
                try {
                    CAmount nValue;
                    pcursor->GetValue(nValue);

                    addressIndex.push_back(make_pair(indexKey, nValue));
                    pcursor->Next();
                } catch (const std::exception& e) {
                    return error("failed to get address index value");
                }
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }

    return true;
}

bool getAddressFromIndex(const int &type, const uint160 &hash, std::string &address);

#define DECLARE_IGNORELIST std::map <std::string,int> ignoredMap = { \
    {"RReUxSs5hGE39ELU23DfydX8riUuzdrHAE", 1}, \
    {"RMUF3UDmzWFLSKV82iFbMaqzJpUnrWjcT4", 1}, \
    {"RA5imhVyJa7yHhggmBytWuDr923j2P1bxx", 1}, \
    {"RBM5LofZFodMeewUzoMWcxedm3L3hYRaWg", 1}, \
    {"RAdcko2d94TQUcJhtFHZZjMyWBKEVfgn4J", 1}, \
    {"RLzUaZ934k2EFCsAiVjrJqM8uU1vmMRFzk", 1}, \
    {"RMSZMWZXv4FhUgWhEo4R3AQXmRDJ6rsGyt", 1}, \
    {"RUDrX1v5toCsJMUgtvBmScKjwCB5NaR8py", 1}, \
    {"RMSZMWZXv4FhUgWhEo4R3AQXmRDJ6rsGyt", 1}, \
    {"RRvwmbkxR5YRzPGL5kMFHMe1AH33MeD8rN", 1}, \
    {"RQLQvSgpPAJNPgnpc8MrYsbBhep95nCS8L", 1}, \
    {"RK8JtBV78HdvEPvtV5ckeMPSTojZPzHUTe", 1}, \
    {"RHVs2KaCTGUMNv3cyWiG1jkEvZjigbCnD2", 1}, \
    {"RE3SVaDgdjkRPYA6TRobbthsfCmxQedVgF", 1}, \
    {"RW6S5Lw5ZCCvDyq4QV9vVy7jDHfnynr5mn", 1}, \
    {"RTkJwAYtdXXhVsS3JXBAJPnKaBfMDEswF8", 1}, \
    {"RD6GgnrMpPaTSMn8vai6yiGA7mN4QGPVMY", 1} \
};

bool CBlockTreeDB::Snapshot2(std::map <std::string, CAmount> &addressAmounts, UniValue *ret)
{
    int64_t total = 0; int64_t totalAddresses = 0; std::string address;
    int64_t utxos = 0; int64_t ignoredAddresses = 0, cryptoConditionsUTXOs = 0, cryptoConditionsTotals = 0;
    DECLARE_IGNORELIST
    boost::scoped_ptr<CDBIterator> iter(NewIterator());

    for (iter->SeekToLast(); iter->Valid(); iter->Prev())
    {
        boost::this_thread::interruption_point();
        try
        {
            std::vector<unsigned char> slKey = std::vector<unsigned char>();
            pair<char, CAddressIndexIteratorKey> keyObj;
            iter->GetKey(keyObj);
            char chType = keyObj.first;
            CAddressIndexIteratorKey indexKey = keyObj.second;

            if (chType == DB_ADDRESSUNSPENTINDEX)
            {
                try {
                    CAmount nValue;
                    iter->GetValue(nValue);
                    if ( nValue == 0 )
                        continue;
                    getAddressFromIndex(indexKey.type, indexKey.hashBytes, address);
                    if ( indexKey.type == 3 )
                    {
                        cryptoConditionsUTXOs++;
                        cryptoConditionsTotals += nValue;
                        total += nValue;
                        continue;
                    }
                    std::map <std::string, int>::iterator ignored = ignoredMap.find(address);
                    if (ignored != ignoredMap.end())
                    {
                        fprintf(stderr,"ignoring %s\n", address.c_str());
                        ignoredAddresses++;
                        continue;
                    }
                    std::map <std::string, CAmount>::iterator pos = addressAmounts.find(address);
                    if ( pos == addressAmounts.end() )
                    {
                        // insert new address + utxo amount
                        addressAmounts[address] = nValue;
                        totalAddresses++;
                    }
                    else
                    {
                        // update unspent tally for this address
                        addressAmounts[address] += nValue;
                    }
                    utxos++;
                    total += nValue;
                }
                catch (const std::exception& e)
                {
                    fprintf(stderr, "DONE %s: LevelDB addressindex exception! - %s\n", __func__, e.what());
                    return false; //break; this means failiure of DB? we need to exit here if so for consensus code!
                }
            }
        }
        catch (const std::exception& e)
        {
            fprintf(stderr, "DONE reading index entries\n");
            break;
        }
    }
    //fprintf(stderr, "total=%f, totalAddresses=%li, utxos=%li, ignored=%li\n", (double) total / COIN, totalAddresses, utxos, ignoredAddresses);

    // this is for the snapshot RPC, you can skip this by passing a 0 as the last argument.
    if (ret)
    {
        // Total circulating supply without CC vouts.
        ret->push_back(make_pair("total", (double) (total)/ COIN ));
        // Average amount in each address of this snapshot
        ret->push_back(make_pair("average",(double) (total/COIN) / totalAddresses ));
        // Total number of utxos processed in this snaphot
        ret->push_back(make_pair("utxos", utxos));
        // Total number of addresses in this snaphot
        ret->push_back(make_pair("total_addresses", totalAddresses ));
        // Total number of ignored addresses in this snaphot
        ret->push_back(make_pair("ignored_addresses", ignoredAddresses));
        // Total number of crypto condition utxos we skipped
        ret->push_back(make_pair("skipped_cc_utxos", cryptoConditionsUTXOs));
        // Total value of skipped crypto condition utxos
        ret->push_back(make_pair("cc_utxo_value", (double) cryptoConditionsTotals / COIN));
        // total of all the address's, does not count coins in CC vouts.
        ret->push_back(make_pair("total_includeCCvouts", (double) (total+cryptoConditionsTotals)/ COIN ));
        // The snapshot finished at this block height
        ret->push_back(make_pair("ending_height", chainActive.Height()));
    }
    return true;
}

extern std::vector <std::pair<CAmount, CTxDestination>> vAddressSnapshot; // daily snapshot

UniValue CBlockTreeDB::Snapshot(int top)
{
    std::vector <std::pair<CAmount, std::string>> vaddr;
    std::map <std::string, CAmount> addressAmounts;
    UniValue result(UniValue::VOBJ);
    UniValue addressesSorted(UniValue::VARR);
    result.push_back(Pair("start_time", (int) time(NULL)));

    if ( (vAddressSnapshot.size() > 0 && top < 0) || (Snapshot2(addressAmounts,&result) && top >= 0) )
    {
        if ( top > -1 )
        {
            for (std::pair<std::string, CAmount> element : addressAmounts)
                vaddr.push_back( make_pair(element.second, element.first) );
            std::sort(vaddr.rbegin(), vaddr.rend());
        }
        else
        {
            for ( auto address : vAddressSnapshot )
                vaddr.push_back(make_pair(address.first, CBitcoinAddress(address.second).ToString()));
            top = vAddressSnapshot.size();
        }
        int topN = 0;
        for (std::vector<std::pair<CAmount, std::string>>::iterator it = vaddr.begin(); it!=vaddr.end(); ++it)
        {
          	UniValue obj(UniValue::VOBJ);
          	obj.push_back( make_pair("addr", it->second.c_str() ) );
          	char amount[32];
          	sprintf(amount, "%.8f", (double) it->first / COIN);
          	obj.push_back( make_pair("amount", amount) );
            obj.push_back( make_pair("segid",(int32_t)komodo_segid32((char *)it->second.c_str()) & 0x3f) );
          	addressesSorted.push_back(obj);
            topN++;
            // If requested, only show top N addresses in output JSON
           	if ( top == topN )
                break;
        }
    	// Array of all addreses with balances
        result.push_back(make_pair("addresses", addressesSorted));
    } else result.push_back(make_pair("error", "problem doing snapshot"));
    return(result);
}

bool CBlockTreeDB::WriteTimestampIndex(const CTimestampIndexKey &timestampIndex) {
    CDBBatch batch(*this);
    batch.Write(make_pair(DB_TIMESTAMPINDEX, timestampIndex), 0);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, std::vector<std::pair<uint256, unsigned int> > &hashes) {

    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_TIMESTAMPINDEX, CTimestampIndexIteratorKey(low)));

    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        try {
            pair<char, CTimestampIndexKey> keyObj;
            pcursor->GetKey(keyObj);
            char chType = keyObj.first;
            CTimestampIndexKey indexKey = keyObj.second;

            if (chType == DB_TIMESTAMPINDEX && indexKey.timestamp < high) {
                if (fActiveOnly) {
                    if (blockOnchainActive(indexKey.blockHash)) {
                        hashes.push_back(std::make_pair(indexKey.blockHash, indexKey.timestamp));
                    }
                } else {
                    hashes.push_back(std::make_pair(indexKey.blockHash, indexKey.timestamp));
                }

                pcursor->Next();
            } else {
                break;
            }
        } catch (const std::exception& e) {
            break;
        }
    }

    return true;
}

bool CBlockTreeDB::WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts) {
    CDBBatch batch(*this);
    batch.Write(make_pair(DB_BLOCKHASHINDEX, blockhashIndex), logicalts);
    return WriteBatch(batch);
}

bool CBlockTreeDB::ReadTimestampBlockIndex(const uint256 &hash, unsigned int &ltimestamp) const {

    CTimestampBlockIndexValue(lts);
    if (!Read(std::make_pair(DB_BLOCKHASHINDEX, hash), lts))
	return false;

    ltimestamp = lts.ltimestamp;
    return true;
}

bool CBlockTreeDB::WriteFlag(const std::string &name, bool fValue) {
    return Write(std::make_pair(DB_FLAG, name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string &name, bool &fValue) const {
    char ch;
    if (!Read(std::make_pair(DB_FLAG, name), ch))
        return false;
    fValue = ch == '1';
    return true;
}

void komodo_index2pubkey33(uint8_t *pubkey33,CBlockIndex *pindex,int32_t height);

bool CBlockTreeDB::blockOnchainActive(const uint256 &hash) {
    BlockMap::const_iterator it = mapBlockIndex.find(hash);
    CBlockIndex* pblockindex = it != mapBlockIndex.end() ? it->second : NULL;

    if (!pblockindex || !chainActive.Contains(pblockindex)) {
	    return false;
    }

    return true;
}

bool CBlockTreeDB::LoadBlockIndexGuts()
{
    boost::scoped_ptr<CDBIterator> pcursor(NewIterator());

    pcursor->Seek(make_pair(DB_BLOCK_INDEX, uint256()));
    int64_t count = 0; int reportDone = 0;
    uiInterface.ShowProgress(_("Loading guts..."), 0, false);

    // Load mapBlockIndex
    while (pcursor->Valid()) {
        boost::this_thread::interruption_point();
        if (ShutdownRequested()) return false;

        std::pair<char, uint256> key;

        if (pcursor->GetKey(key) && key.first == DB_BLOCK_INDEX) {

            if (count++ % 256 == 0) {
                uint32_t high = 0x100 * *key.second.begin() + *(key.second.begin() + 1);
                int percentageDone = (int)(high * 100.0 / 65536.0 + 0.5);
                uiInterface.ShowProgress(_("Loading guts..."), percentageDone, false);
                if (reportDone < percentageDone/10) {
                    // report max. every 10% step
                    LogPrintf("[%d%%]...", percentageDone); /* Continued */
                    reportDone = percentageDone/10;
                }
            }

            CDiskBlockIndex diskindex;
            if (pcursor->GetValue(diskindex)) {
                // Construct block index object
                CBlockIndex* pindexNew            = InsertBlockIndex(diskindex.GetBlockHash());
                pindexNew->pprev                  = InsertBlockIndex(diskindex.hashPrev);
                pindexNew->nHeight                = diskindex.nHeight;
                pindexNew->nFile                  = diskindex.nFile;
                pindexNew->nDataPos               = diskindex.nDataPos;
                pindexNew->nUndoPos               = diskindex.nUndoPos;
                pindexNew->hashSproutAnchor       = diskindex.hashSproutAnchor;
                pindexNew->nVersion               = diskindex.nVersion;
                pindexNew->hashMerkleRoot         = diskindex.hashMerkleRoot;
                pindexNew->hashBlockCommitments   = diskindex.hashBlockCommitments;
                pindexNew->nTime                  = diskindex.nTime;
                pindexNew->nBits                  = diskindex.nBits;
                pindexNew->nNonce                 = diskindex.nNonce;
                // the Equihash solution will be loaded lazily from the dbindex entry
                pindexNew->nStatus                = diskindex.nStatus;
                pindexNew->nCachedBranchId        = diskindex.nCachedBranchId;
                pindexNew->nTx                    = diskindex.nTx;
                pindexNew->nChainSupplyDelta      = diskindex.nChainSupplyDelta;
                pindexNew->nTransparentValue      = diskindex.nTransparentValue;
                pindexNew->nBurnedAmountDelta     = diskindex.nBurnedAmountDelta;
                pindexNew->nSproutValue           = diskindex.nSproutValue;
                pindexNew->nSaplingValue          = diskindex.nSaplingValue;
                pindexNew->nOrchardValue          = diskindex.nOrchardValue;
                pindexNew->hashFinalSaplingRoot   = diskindex.hashFinalSaplingRoot;
                pindexNew->hashFinalOrchardRoot   = diskindex.hashFinalOrchardRoot;
                pindexNew->hashChainHistoryRoot   = diskindex.hashChainHistoryRoot;
                pindexNew->hashAuthDataRoot       = diskindex.hashAuthDataRoot;
                //Komodo Data
                pindexNew->segid                  = diskindex.segid;
                pindexNew->nNotaryPay             = diskindex.nNotaryPay;

                if ( 0 ) // POW will be checked before any block is connected
                {
                    // Consistency checks
                    CBlockHeader header;
                    {
                        LOCK(cs_main);
                        try {
                            header = pindexNew->GetBlockHeader();
                        } catch (const runtime_error&) {
                            return error("LoadBlockIndex(): failed to read index entry: diskindex hash = %s",
                                diskindex.GetBlockHash().ToString());
                        }
                    }
                    if (header.GetHash() != pindexNew->GetBlockHash())
                        return error("LoadBlockIndex(): block header inconsistency detected: on-disk = %s, in-memory = %s",
                                    diskindex.ToString(),  pindexNew->ToString());

                    uint8_t pubkey33[33];
                    komodo_index2pubkey33(pubkey33,pindexNew,pindexNew->nHeight);
                    if (!CheckProofOfWork(header,pubkey33,pindexNew->nHeight,Params().GetConsensus()))
                        return error("LoadBlockIndex(): CheckProofOfWork failed: %s", pindexNew->ToString());
                }
                pcursor->Next();
            } else {
                return error("LoadBlockIndex() : failed to read value");
            }
        } else {
            break;
        }
    }

    uiInterface.ShowProgress("", 100, false);
    LogPrintf("[%s].\n", ShutdownRequested() ? "CANCELLED" : "DONE");

    return true;
}
