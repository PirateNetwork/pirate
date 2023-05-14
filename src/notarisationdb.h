#ifndef NOTARISATIONDB_H
#define NOTARISATIONDB_H

#include "uint256.h"
#include "dbwrapper.h"
#include "cc/eval.h"


class NotarisationDB : public CDBWrapper
{
public:
    NotarisationDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);
};


extern NotarisationDB *pnotarisations;

typedef std::pair<uint256,NotarisationData> Notarisation;
typedef std::vector<Notarisation> NotarisationsInBlock;

/****
 * Get notarisations within a block
 * @param block the block to scan
 * @param height the height (to find appropriate notaries)
 * @returns the notarisations found
 */
NotarisationsInBlock ScanBlockNotarisations(const CBlock &block, int nHeight);
/*****
 * Get the notarisations of the block
 * @param blockHash the block to examine
 * @param nibs the notarisations
 * @returns true on success
 */
bool GetBlockNotarisations(uint256 blockHash, NotarisationsInBlock &nibs);
/***
 * Look up the value of the hash
 * @param notarisationHash the key
 * @param n the value
 * @returns true on success
 */
bool GetBackNotarisation(uint256 notarisationHash, Notarisation &n);
/***
 * Write given notarisations into db
 * @param notarisations what to write
 * @param the collection of db transactions
 */
void WriteBackNotarisations(const NotarisationsInBlock notarisations, CDBBatch &batch);
/***
 * Erase given notarisations from db
 * @param notarisations what to erase
 * @param batch the collection of db transactions
 */
void EraseBackNotarisations(const NotarisationsInBlock notarisations, CDBBatch &batch);
/*****
 * Scan notarisationsdb backwards for blocks containing a notarisation
 * for given symbol. Return height of matched notarisation or 0.
 * @param height where to start the search
 * @param symbol the symbol to look for
 * @param scanLimitBlocks max number of blocks to search
 * @param out the first Notarization found
 * @returns height (0 indicates error)
 */
int ScanNotarisationsDB(int height, std::string symbol, int scanLimitBlocks, Notarisation& out);

#endif  /* NOTARISATIONDB_H */
