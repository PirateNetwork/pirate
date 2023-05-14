#include "dbwrapper.h"
#include "notarisationdb.h"
#include "uint256.h"
#include "cc/eval.h"
#include "crosschain.h"
#include "main.h"
#include "notaries_staked.h"

#include <boost/foreach.hpp>


NotarisationDB *pnotarisations;


NotarisationDB::NotarisationDB(size_t nCacheSize, bool fMemory, bool fWipe) : CDBWrapper(GetDataDir() / "notarisations", nCacheSize, fMemory, fWipe, false, 64) { }

/****
 * Get notarisations within a block
 * @param block the block to scan
 * @param height the height (to find appropriate notaries)
 * @returns the notarisations found
 */
NotarisationsInBlock ScanBlockNotarisations(const CBlock &block, int nHeight)
{
    EvalRef eval;
    NotarisationsInBlock vNotarisations;
    CrosschainAuthority auth_STAKED;
    int timestamp = block.nTime;

    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        CTransaction tx = block.vtx[i];

        NotarisationData data;
        bool parsed = ParseNotarisationOpReturn(tx, data);
        if (!parsed) data = NotarisationData();
        if (strlen(data.symbol) == 0)
          continue;

        //printf("Checked notarisation data for %s \n",data.symbol);
        CrosschainType authority = CrossChain::GetSymbolAuthority(data.symbol);

        if (authority == CROSSCHAIN_KOMODO) {
            if (!eval->CheckNotaryInputs(tx, nHeight, block.nTime))
                continue;
        } else if (authority == CROSSCHAIN_STAKED) {
            // We need to create auth_STAKED dynamically here based on timestamp
            int32_t staked_era = STAKED_era(timestamp);
            if (staked_era == 0) {
              // this is an ERA GAP, so we will ignore this notarization
              continue;
             if ( is_STAKED(data.symbol) == 255 )
              // this chain is banned... we will discard its notarisation. 
              continue;
            } else {
              // pass era slection off to notaries_staked.cpp file
              auth_STAKED = Choose_auth_STAKED(staked_era);
            }
            if (!CrossChain::CheckTxAuthority(tx, auth_STAKED))
                continue;
        }

        if (parsed) {
            vNotarisations.push_back(std::make_pair(tx.GetHash(), data));
        } else
            LogPrintf("WARNING: Couldn't parse notarisation for tx: %s at height %i\n",
                    tx.GetHash().GetHex().data(), nHeight);
    }
    return vNotarisations;
}

bool GetBlockNotarisations(uint256 blockHash, NotarisationsInBlock &nibs)
{
    return pnotarisations->Read(blockHash, nibs);
}


bool GetBackNotarisation(uint256 notarisationHash, Notarisation &n)
{
    return pnotarisations->Read(notarisationHash, n);
}


/*
 * Write an index of KMD notarisation id -> backnotarisation
 */
void WriteBackNotarisations(const NotarisationsInBlock notarisations, CDBBatch &batch)
{
    int wrote = 0;
    BOOST_FOREACH(const Notarisation &n, notarisations)
    {
        if (!n.second.txHash.IsNull()) {
            batch.Write(n.second.txHash, n);
            wrote++;
        }
    }
}

/***
 * Erase given notarisations from db
 * @param notarisations what to erase
 * @param batch the collection of db transactions
 */
void EraseBackNotarisations(const NotarisationsInBlock notarisations, CDBBatch &batch)
{
    for(const Notarisation &n : notarisations)
    {
        if (!n.second.txHash.IsNull())
            batch.Erase(n.second.txHash);
    }
}

/*****
 * Scan notarisationsdb backwards for blocks containing a notarisation
 * for given symbol. Return height of matched notarisation or 0.
 * @param height where to start the search
 * @param symbol the symbol to look for
 * @param scanLimitBlocks max number of blocks to search
 * @param out the first Notarization found
 * @returns height (0 indicates error)
 */
int ScanNotarisationsDB(int height, std::string symbol, int scanLimitBlocks, Notarisation& out)
{
    if (height < 0 || height > chainActive.Height())
        return 0;

    for (int i=0; i<scanLimitBlocks; i++) 
    {
        if (i > height) 
            break;
        NotarisationsInBlock notarisations;
        uint256 blockHash = *chainActive[height-i]->phashBlock;
        if (GetBlockNotarisations(blockHash, notarisations))
        {
            for(Notarisation& nota : notarisations) {
                if (strcmp(nota.second.symbol, symbol.data()) == 0) 
                {
                    out = nota;
                    return height-i;
                }
            }
        }
    }
    return 0;
}
