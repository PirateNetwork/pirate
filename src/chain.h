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

#ifndef BITCOIN_CHAIN_H
#define BITCOIN_CHAIN_H

#include "arith_uint256.h"
#include "primitives/block.h"
#include "pow.h"
#include "tinyformat.h"
#include "uint256.h"
#include "sync.h"

#include "assetchain.h"
#include <vector>

#include <boost/foreach.hpp>


extern CCriticalSection cs_main;

static const int SPROUT_VALUE_VERSION = 80102;
static const int SAPLING_VALUE_VERSION = 80102;

// These 5 are declared here to avoid circular dependencies
// code used this moved into .cpp
/*extern assetchain chainName;
extern uint64_t ASSETCHAINS_NOTARY_PAY[ASSETCHAINS_MAX_ERAS+1];
extern int32_t ASSETCHAINS_STAKED;
extern const uint32_t nStakedDecemberHardforkTimestamp; //December 2019 hardfork
extern const int32_t nDecemberHardforkHeight;   //December 2019 hardfork
uint8_t is_STAKED(const std::string& symbol);*/

struct CDiskBlockPos
{
    int nFile;
    unsigned int nPos;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(VARINT(nFile));
        READWRITE(VARINT(nPos));
    }

    CDiskBlockPos() {
        SetNull();
    }

    CDiskBlockPos(int nFileIn, unsigned int nPosIn) {
        nFile = nFileIn;
        nPos = nPosIn;
    }

    friend bool operator==(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return (a.nFile == b.nFile && a.nPos == b.nPos);
    }

    friend bool operator!=(const CDiskBlockPos &a, const CDiskBlockPos &b) {
        return !(a == b);
    }

    void SetNull() { nFile = -1; nPos = 0; }
    bool IsNull() const { return (nFile == -1); }

    std::string ToString() const
    {
        return strprintf("CBlockDiskPos(nFile=%i, nPos=%i)", nFile, nPos);
    }

};

enum BlockStatus: uint32_t {
    //! Unused.
    BLOCK_VALID_UNKNOWN      =    0,

    //! Parsed, version ok, hash satisfies claimed PoW, 1 <= vtx count <= max, timestamp not in future
    BLOCK_VALID_HEADER       =    1,

    //! All parent headers found, difficulty matches, timestamp >= median previous, checkpoint. Implies all parents
    //! are also at least TREE.
    BLOCK_VALID_TREE         =    2,

    /**
     * Only first tx is coinbase, 2 <= coinbase input script length <= 100, transactions valid, no duplicate txids,
     * sigops, size, merkle root. Implies all parents are at least TREE but not necessarily TRANSACTIONS. When all
     * parent blocks also have TRANSACTIONS, CBlockIndex::nChainTx will be set.
     */
    BLOCK_VALID_TRANSACTIONS =    3,

    //! Outputs do not overspend inputs, no double spends, coinbase output ok, no immature coinbase spends, BIP30.
    //! Implies all parents are also at least CHAIN.
    BLOCK_VALID_CHAIN        =    4,

    //! Scripts & signatures ok. Implies all parents are also at least SCRIPTS.
    BLOCK_VALID_SCRIPTS      =    5,

    // flag to check if contextual check block has passed in Accept block, if it has not check at connect block. 
    BLOCK_VALID_CONTEXT      =    6,
    
    //! All validity bits.
    BLOCK_VALID_MASK         =   BLOCK_VALID_HEADER | BLOCK_VALID_TREE | BLOCK_VALID_TRANSACTIONS |
                                 BLOCK_VALID_CHAIN | BLOCK_VALID_SCRIPTS,

    BLOCK_HAVE_DATA          =    8, //! full block available in blk*.dat
    BLOCK_HAVE_UNDO          =   16, //! undo data available in rev*.dat
    BLOCK_HAVE_MASK          =   BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO,

    BLOCK_FAILED_VALID       =   32, //! stage after last reached validness failed
    BLOCK_FAILED_CHILD       =   64, //! descends from failed block
    BLOCK_FAILED_MASK        =   BLOCK_FAILED_VALID | BLOCK_FAILED_CHILD,

    BLOCK_ACTIVATES_UPGRADE  =   128, //! block activates a network upgrade
    BLOCK_IN_TMPFILE         =   256 
};

//! Short-hand for the highest consensus validity we implement.
//! Blocks with this validity are assumed to satisfy all consensus rules.
static const BlockStatus BLOCK_VALID_CONSENSUS = BLOCK_VALID_SCRIPTS;

/** The block chain is a tree shaped structure starting with the
 * genesis block at the root, with each block potentially having multiple
 * candidates to be the next block. A blockindex may have multiple pprev pointing
 * to it, but at most one of them can be part of the currently active branch.
 */
class CBlockIndex
{
public:
    //! pointer to the hash of the block, if any. Memory is owned by this CBlockIndex
    const uint256* phashBlock;

    //! pointer to the index of the predecessor of this block
    CBlockIndex* pprev;

    //! pointer to the index of some further predecessor of this block
    CBlockIndex* pskip;

    //! height of the entry in the chain. The genesis block has height 0
    int nHeight;

    int64_t newcoins,zfunds,sproutfunds,nNotaryPay; int8_t segid; // jl777 fields
    //! Which # file this block is stored in (blk?????.dat)
    int nFile;

    //! Byte offset within blk?????.dat where this block's data is stored
    unsigned int nDataPos;

    //! Byte offset within rev?????.dat where this block's undo data is stored
    unsigned int nUndoPos;

    //! (memory only) Total amount of work (expected number of hashes) in the chain up to and including this block
    arith_uint256 nChainWork;

    //! Number of transactions in this block.
    //! Note: in a potential headers-first mode, this number cannot be relied upon
    unsigned int nTx;

    //! (memory only) Number of transactions in the chain up to and including this block.
    //! This value will be non-zero only if and only if transactions for this block and all its parents are available.
    //! Change to 64-bit type when necessary; won't happen before 2030
    unsigned int nChainTx;

    //! Verification status of this block. See enum BlockStatus
    unsigned int nStatus;

    //! Branch ID corresponding to the consensus rules used to validate this block.
    //! Only cached if block validity is BLOCK_VALID_CONSENSUS.
    //! Persisted at each activation height, memory-only for intervening blocks.
    boost::optional<uint32_t> nCachedBranchId;

    //! The anchor for the tree state up to the start of this block
    uint256 hashSproutAnchor;

    //! (memory only) The anchor for the tree state up to the end of this block
    uint256 hashFinalSproutRoot;

    //! Change in value held by the Sprout circuit over this block.
    //! Will be boost::none for older blocks on old nodes until a reindex has taken place.
    boost::optional<CAmount> nSproutValue;

    //! (memory only) Total value held by the Sprout circuit up to and including this block.
    //! Will be boost::none for on old nodes until a reindex has taken place.
    //! Will be boost::none if nChainTx is zero.
    boost::optional<CAmount> nChainSproutValue;

    //! Change in value held by the Sapling circuit over this block.
    //! Not a boost::optional because this was added before Sapling activated, so we can
    //! rely on the invariant that every block before this was added had nSaplingValue = 0.
    CAmount nSaplingValue;

    //! (memory only) Total value held by the Sapling circuit up to and including this block.
    //! Will be boost::none if nChainTx is zero.
    boost::optional<CAmount> nChainSaplingValue;

    //! block header
    int nVersion;
    uint256 hashMerkleRoot;
    uint256 hashFinalSaplingRoot;
    unsigned int nTime;
    unsigned int nBits;
    uint256 nNonce;

protected:
    // The Equihash solution, if it is stored. Once we know that the block index
    // entry is present in leveldb, this field can be cleared via the TrimSolution
    // method to save memory.
    std::vector<unsigned char> nSolution;

public:
    //! (memory only) Sequential id assigned to distinguish order in which blocks are received.
    uint32_t nSequenceId;
    
    void SetNull()
    {
        phashBlock = NULL;
        newcoins = zfunds = 0;
        segid = -2;
        nNotaryPay = 0;
        pprev = NULL;
        pskip = NULL;
        nHeight = 0;
        nFile = 0;
        nDataPos = 0;
        nUndoPos = 0;
        nChainWork = arith_uint256();
        nTx = 0;
        nChainTx = 0;
        nStatus = 0;
        nCachedBranchId = boost::none;
        hashSproutAnchor = uint256();
        hashFinalSproutRoot = uint256();
        nSequenceId = 0;
        nSproutValue = boost::none;
        nChainSproutValue = boost::none;
        nSaplingValue = 0;
        nChainSaplingValue = boost::none;

        nVersion       = 0;
        hashMerkleRoot = uint256();
        hashFinalSaplingRoot   = uint256();
        nTime          = 0;
        nBits          = 0;
        nNonce         = uint256();
        nSolution.clear();
    }

    CBlockIndex()
    {
        SetNull();
    }

    CBlockIndex(const CBlockHeader& block)
    {
        SetNull();

        nVersion       = block.nVersion;
        hashMerkleRoot = block.hashMerkleRoot;
        hashFinalSaplingRoot   = block.hashFinalSaplingRoot;
        nTime          = block.nTime;
        nBits          = block.nBits;
        nNonce         = block.nNonce;
        nSolution      = block.nSolution;
    }

    CDiskBlockPos GetBlockPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_DATA) {
            ret.nFile = nFile;
            ret.nPos  = nDataPos;
        }
        return ret;
    }

    CDiskBlockPos GetUndoPos() const {
        CDiskBlockPos ret;
        if (nStatus & BLOCK_HAVE_UNDO) {
            ret.nFile = nFile;
            ret.nPos  = nUndoPos;
        }
        return ret;
    }

    //! Get the block header for this block index. Requires cs_main.
    CBlockHeader GetBlockHeader() const;

    //! Clear the Equihash solution to save memory. Requires cs_main.
    void TrimSolution();

    uint256 GetBlockHash() const
    {
        assert(phashBlock);
        return *phashBlock;
    }

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }

    enum { nMedianTimeSpan=11 };

    /***
     * @note times are stored as a 4 byte int. Although this returns int64_t, it will be at
     * 32 bit resolution.
     * @note storing this as 32 bits can cause a "Year 2038" problem.
     * @returns the median time (uinx epoch) of the last nMedianTimeSpan (currently 11) blocks
     */
    int64_t GetMedianTimePast() const
    {
        int64_t pmedian[nMedianTimeSpan];
        int64_t* pbegin = &pmedian[nMedianTimeSpan];
        int64_t* pend = &pmedian[nMedianTimeSpan];

        // grab the times of the last 11 blocks
        const CBlockIndex* pindex = this;
        for (int i = 0; i < nMedianTimeSpan && pindex; i++, pindex = pindex->pprev)
            *(--pbegin) = pindex->GetBlockTime();

        std::sort(pbegin, pend);
        return pbegin[(pend - pbegin)/2];
    }

    std::string ToString() const
    {
        return strprintf("CBlockIndex(pprev=%p, nHeight=%d, merkle=%s, hashBlock=%s, HasSolution=%s)",
            pprev, nHeight,
            hashMerkleRoot.ToString(),
            phashBlock ? GetBlockHash().ToString() : "(nil)",
            HasSolution());
    }

    //! Check whether this block index entry is valid up to the passed validity level.
    bool IsValid(enum BlockStatus nUpTo = BLOCK_VALID_TRANSACTIONS) const
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        return ((nStatus & BLOCK_VALID_MASK) >= nUpTo);
    }

    //! Is the Equihash solution stored?
    bool HasSolution() const
    {
        return !nSolution.empty();
    }

    //! Raise the validity level of this block index entry.
    //! Returns true if the validity was changed.
    bool RaiseValidity(enum BlockStatus nUpTo)
    {
        assert(!(nUpTo & ~BLOCK_VALID_MASK)); // Only validity flags allowed.
        if (nStatus & BLOCK_FAILED_MASK)
            return false;
        if ((nStatus & BLOCK_VALID_MASK) < nUpTo) {
            nStatus = (nStatus & ~BLOCK_VALID_MASK) | nUpTo;
            return true;
        }
        return false;
    }

    //! Build the skiplist pointer for this entry.
    void BuildSkip();

    //! Efficiently find an ancestor of this block.
    CBlockIndex* GetAncestor(int height);
    const CBlockIndex* GetAncestor(int height) const;
};

/** Used to marshal pointers into hashes for db storage. */
class CDiskBlockIndex : public CBlockIndex
{
public:
    uint256 hashPrev;

    CDiskBlockIndex() : CBlockIndex() {
        hashPrev = uint256();
    }

    explicit CDiskBlockIndex(const CBlockIndex* pindex, std::function<std::vector<unsigned char>()> getSolution) : CBlockIndex(*pindex) {
        hashPrev = (pprev ? pprev->GetBlockHash() : uint256());
        if (!HasSolution()) {
            nSolution = getSolution();
        }
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(VARINT(nVersion));

        READWRITE(VARINT(nHeight));
        READWRITE(VARINT(nStatus));
        READWRITE(VARINT(nTx));
        if (nStatus & (BLOCK_HAVE_DATA | BLOCK_HAVE_UNDO))
            READWRITE(VARINT(nFile));
        if (nStatus & BLOCK_HAVE_DATA)
            READWRITE(VARINT(nDataPos));
        if (nStatus & BLOCK_HAVE_UNDO)
            READWRITE(VARINT(nUndoPos));
        if (nStatus & BLOCK_ACTIVATES_UPGRADE) {
            if (ser_action.ForRead()) {
                uint32_t branchId;
                READWRITE(branchId);
                nCachedBranchId = branchId;
            } else {
                // nCachedBranchId must always be set if BLOCK_ACTIVATES_UPGRADE is set.
                assert(nCachedBranchId);
                uint32_t branchId = *nCachedBranchId;
                READWRITE(branchId);
            }
        }
        READWRITE(hashSproutAnchor);

        // block header
        READWRITE(this->nVersion);
        READWRITE(hashPrev);
        READWRITE(hashMerkleRoot);
        READWRITE(hashFinalSaplingRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);
        READWRITE(nSolution);

        // Only read/write nSproutValue if the client version used to create
        // this index was storing them.
        if ((s.GetType() & SER_DISK) && (nVersion >= SPROUT_VALUE_VERSION)) {
            READWRITE(nSproutValue);
        }

        // Only read/write nSaplingValue if the client version used to create
        // this index was storing them.
        if ((s.GetType() & SER_DISK) && (nVersion >= SAPLING_VALUE_VERSION)) {
            READWRITE(nSaplingValue);
        }
        
        // leave the existing LABS exemption here for segid and notary pay, but also add a timestamp activated segid for non LABS PoS64 chains.
        if ( (s.GetType() & SER_DISK) && isStakedAndNotaryPay() /*is_STAKED(chainName.symbol()) != 0 && ASSETCHAINS_NOTARY_PAY[0] != 0*/ )
        {
            READWRITE(nNotaryPay);
        }
        if ( (s.GetType() & SER_DISK) && isStakedAndAfterDec2019(nTime) /*ASSETCHAINS_STAKED != 0 && (nTime > nStakedDecemberHardforkTimestamp || is_STAKED(chainName.symbol()) != 0)*/ ) //December 2019 hardfork
        {
            READWRITE(segid);
        }
    }
private:
    bool isStakedAndNotaryPay() const;
    bool isStakedAndAfterDec2019(unsigned int nTime) const;

    //! This method should not be called on a CDiskBlockIndex.
    void TrimSolution()
    {
        assert(!"called CDiskBlockIndex::TrimSolution");
    }

public:
    uint256 GetBlockHash() const
    {
        return GetBlockHeader().GetHash();
    }

    //! Get the block header for this block index.
    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader header;
        header.nVersion             = nVersion;
        header.hashPrevBlock        = hashPrev;
        header.hashMerkleRoot       = hashMerkleRoot;
        header.hashFinalSaplingRoot = hashFinalSaplingRoot;
        header.nTime                = nTime;
        header.nBits                = nBits;
        header.nNonce               = nNonce;
        header.nSolution            = nSolution;
        return header;
    }

    std::vector<unsigned char> GetSolution() const
    {
        assert(HasSolution());
        return nSolution;
    }

    std::string ToString() const
    {
        std::string str = "CDiskBlockIndex(";
        str += CBlockIndex::ToString();
        str += strprintf("\n                hashBlock=%s, hashPrev=%s)",
            GetBlockHash().ToString(),
            hashPrev.ToString());
        return str;
    }
};

/** An in-memory indexed chain of blocks. */
class CChain {
protected:
    std::vector<CBlockIndex*> vChain;
    CBlockIndex *at(int nHeight) const REQUIRES(cs_main)
    {
        if (nHeight < 0 || nHeight >= (int)vChain.size())
            return NULL;
        return vChain[nHeight];
    }
public:
    /** Returns the index entry for the genesis block of this chain, or NULL if none. */
    CBlockIndex *Genesis() const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return vChain.size() > 0 ? vChain[0] : NULL;
    }

    /** Returns the index entry for the tip of this chain, or NULL if none. */
    CBlockIndex *Tip() const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return vChain.size() > 0 ? vChain[vChain.size() - 1] : nullptr;
    }
    
    /** Returns the index entry at a particular height in this chain, or NULL if no such height exists. */
    CBlockIndex *operator[](int nHeight) const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return at(nHeight);
    }

    /** Compare two chains efficiently. */
    friend bool operator==(const CChain &a, const CChain &b) REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return a.Height() == b.Height() &&
               a.Tip() == b.Tip();
    }

    /** Efficiently check whether a block is present in this chain. */
    bool Contains(const CBlockIndex *pindex) const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return (*this)[pindex->nHeight] == pindex;
    }

    /** Find the successor of a block in this chain, or NULL if the given index is not found or is the tip. */
    CBlockIndex *Next(const CBlockIndex *pindex) const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        if (Contains(pindex))
            return (*this)[pindex->nHeight + 1];
        else
            return NULL;
    }

    /** Return the maximal height in the chain. Is equal to chain.Tip() ? chain.Tip()->nHeight : -1. */
    int Height() const REQUIRES(cs_main) {
        AssertLockHeld(cs_main);
        return vChain.size() - 1;
    }

    /** Set/initialize a chain with a given tip. */
    void SetTip(CBlockIndex *pindex) REQUIRES(cs_main);

    /** Return a CBlockLocator that refers to a block in this chain (by default the tip). */
    CBlockLocator GetLocator(const CBlockIndex *pindex = NULL) const REQUIRES(cs_main);

    /** Find the last common block between this chain and a block index entry. */
    const CBlockIndex *FindFork(const CBlockIndex *pindex) const REQUIRES(cs_main);
};

#endif // BITCOIN_CHAIN_H
