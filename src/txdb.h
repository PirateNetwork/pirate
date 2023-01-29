// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
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

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include "coins.h"
#include "dbwrapper.h"

#include <map>
#include <string>
#include <utility>
#include <vector>
#include <univalue.h>

class CBlockFileInfo;
class CBlockIndex;
struct CDiskTxPos;
struct CAddressUnspentKey;
struct CAddressUnspentValue;
struct CAddressIndexKey;
struct CAddressIndexIteratorKey;
struct CAddressIndexIteratorHeightKey;
struct CTimestampIndexKey;
struct CTimestampIndexIteratorKey;
struct CTimestampBlockIndexKey;
struct CTimestampBlockIndexValue;
struct CSpentIndexKey;
struct CSpentIndexValue;
class uint256;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 450;
//! max. -dbcache (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 16384 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

/** 
 * CCoinsView backed by the coin database (chainstate/) 
*/
class CCoinsViewDB : public CCoinsView
{
protected:
    CDBWrapper db;
    CCoinsViewDB(std::string dbName, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
public:
    CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const;
    bool GetSaplingAnchorAt(const uint256 &rt, SaplingMerkleTree &tree) const;
    bool GetNullifier(const uint256 &nf, ShieldedType type) const;
    /***
     * @param txid the transaction id
     * @param coins the coins within the txid
     * @returns true on success
     */
    bool GetCoins(const uint256 &txid, CCoins &coins) const;
    /****
     * Determine if a txid exists
     * @param txid
     * @returns true if the txid exists in the database
     */
    bool HaveCoins(const uint256 &txid) const;
    uint256 GetBestBlock() const;
    uint256 GetBestAnchor(ShieldedType type) const;
    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers);
    bool GetStats(CCoinsStats &stats) const;
};

/** 
 * Access to the block database (blocks/index/)
 * This database consists of:
 * - CBlockFileInfo records that contain info about the individual files that store blocks
 * - CBlockIndex info about the blocks themselves
 * - txid / CDiskTxPos index
 * - spent index
 * - unspent index
 * - address / amount
 * - timestamp index
 * - block hash / timestamp index
 */
class CBlockTreeDB : public CDBWrapper
{
public:
    /****
     * ctor
     * 
     * @param nCacheSize leveldb cache size
     * @param fMemory use leveldb memory environment
     * @param fWipe wipe data
     * @param compression enable leveldb compression
     * @param maxOpenFiles leveldb max open files
     */
    CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false, bool compression = true, int maxOpenFiles = 1000);
private:
    CBlockTreeDB(const CBlockTreeDB&);
    void operator=(const CBlockTreeDB&);
public:
    /***
     * Write a batch of records and sync
     * @param fileInfo the block file info records to write
     * @param nLastFile the value for DB_LAST_BLOCK
     * @param blockinfo the block index records to write
     * @returns true on success
     */
    bool WriteBatchSync(const std::vector<std::pair<int, const CBlockFileInfo*> >& fileInfo, int nLastFile, 
            const std::vector<const CBlockIndex*>& blockinfo);
    /***
     * Erase a batch of block index records and sync
     * @param blockinfo the records
     * @returns true on success
     */
    bool EraseBatchSync(const std::vector<const CBlockIndex*>& blockinfo);
    /***
     * Read the file information
     * @param nFile the file to read
     * @param fileinfo where to store the results
     * @returns true on success
     */
    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &fileinfo);
    /****
     * Read the value of DB_LAST_BLOCK
     * @param nFile where to store the results
     * @returns true on success
     */
    bool ReadLastBlockFile(int &nFile);
    /***
     * Write to the DB_REINDEX_FLAG
     * @param fReindex true to set DB_REINDEX_FLAG, false to erase the key
     * @returns true on success
     */
    bool WriteReindexing(bool fReindex);
    /****
     * Retrieve the value of DB_REINDEX_FLAG
     * @param fReindex true if DB_REINDEX_FLAG exists
     * @returns true on success
     */
    bool ReadReindexing(bool &fReindex);
    /***
     * Retrieve the location of a particular transaction index value
     * @param txid what to look for
     * @param pos the results
     * @returns true on success
     */
    bool ReadTxIndex(const uint256 &txid, CDiskTxPos &pos);
    /****
     * Write transaction index records
     * @param list the records to write
     * @returns true on success
     */
    bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> > &list);
    /****
     * Read a value from the spent index
     * @param key the key
     * @param value the value
     * @returns true on success
     */
    bool ReadSpentIndex(CSpentIndexKey &key, CSpentIndexValue &value);
    /****
     * Update a batch of spent index entries
     * @param vect the entries to add/update
     * @returns true on success
     */
    bool UpdateSpentIndex(const std::vector<std::pair<CSpentIndexKey, CSpentIndexValue> >&vect);
    /****
     * Update the unspent indexes for an address
     * @param vect the name/value pairs
     * @returns true on success
     */
    bool UpdateAddressUnspentIndex(const std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue > >&vect);
    /****
     * Read the unspent key/value pairs for a particular address
     * @param addressHash the address
     * @param type the address type
     * @param vect the results
     * @returns true on success
     */
    bool ReadAddressUnspentIndex(uint160 addressHash, int type,
                                 std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > &vect);
    /*****
     * Write a batch of address index / amount records
     * @param vect a collection of address index/amount records
     * @returns true on success
     */
    bool WriteAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    /****
     * Remove a batch of address index / amount records
     * @param vect the records to erase
     * @returns true on success
     */
    bool EraseAddressIndex(const std::vector<std::pair<CAddressIndexKey, CAmount> > &vect);
    /****
     * Read a range of address index / amount records for a particular address
     * @param addressHash the address to look for
     * @param type the address type
     * @param addressIndex the address index / amount records found
     * @param start the starting index
     * @param end the end
     * @returns true on success
     */
    bool ReadAddressIndex(uint160 addressHash, int type,
                          std::vector<std::pair<CAddressIndexKey, CAmount> > &addressIndex,
                          int start = 0, int end = 0);
    /****
     * Write a timestamp entry to the db
     * @param timestampIndex the record to write
     * @returns true on success
     */
    bool WriteTimestampIndex(const CTimestampIndexKey &timestampIndex);
    /****
     * Read the timestamp entry from the db
     * @param high ending timestamp (most recent)
     * @param low starting timestamp (oldest)
     * @param fActiveOnly only include on-chain active entries in the results
     * @param vect the results
     * @returns true on success
     */
    bool ReadTimestampIndex(const unsigned int &high, const unsigned int &low, const bool fActiveOnly, 
            std::vector<std::pair<uint256, unsigned int> > &vect);
    /****
     * Write a block hash / timestamp record
     * @param blockhashIndex the key (the hash)
     * @param logicalts the value (the timestamp)
     * @returns true on success
     */
    bool WriteTimestampBlockIndex(const CTimestampBlockIndexKey &blockhashIndex, const CTimestampBlockIndexValue &logicalts);
    /*****
     * Given a hash, find its timestamp
     * @param hash the hash (the key)
     * @param logicalTS the timestamp (the value)
     * @returns true on success
     */
    bool ReadTimestampBlockIndex(const uint256 &hash, unsigned int &logicalTS);
    /***
     * Store a flag value in the DB
     * @param name the key
     * @param fValue the value
     * @returns true on success
     */
    bool WriteFlag(const std::string &name, bool fValue);
    /***
     * Read a flag value from the DB
     * @param name the key
     * @param fValue the value
     * @returns true on success
     */
    bool ReadFlag(const std::string &name, bool &fValue);
    /****
     * Load the block headers from disk
     * NOTE: this does no consistency check beyond verifying records exist
     * @returns true on success
     */
    bool LoadBlockIndexGuts();
    /****
     * Check if a block is on the active chain
     * @param hash the block hash
     * @returns true if the block exists on the active chain
     */
    bool blockOnchainActive(const uint256 &hash);
    /****
     * Get a snapshot
     * @param top max number of results, sorted by amount descending (aka richlist)
     * @returns the data ( a collection of (addr, amount, segid) )
     */
    UniValue Snapshot(int top);
    /****
     * Get a snapshot
     * @param addressAmounts the results
     * @param ret results summary (passing nullptr skips compiling this summary)
     * @returns true on success
     */
    bool Snapshot2(std::map <std::string, CAmount> &addressAmounts, UniValue *ret);
};

#endif // BITCOIN_TXDB_H
