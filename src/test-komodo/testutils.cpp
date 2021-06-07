#include <cryptoconditions.h>
#include <gtest/gtest.h>
#include <boost/filesystem.hpp>

#include "core_io.h"
#include "key.h"
#include "main.h"
#include "miner.h"
#include "notarisationdb.h"
#include "random.h"
#include "rpc/server.h"
#include "rpc/protocol.h"
#include "txdb.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "script/cc.h"
#include "script/interpreter.h"

#include "testutils.h"


std::string notaryPubkey = "0205a8ad0c1dbc515f149af377981aab58b836af008d4d7ab21bd76faf80550b47";
std::string notarySecret = "UxFWWxsf1d7w7K5TvAWSkeX4H95XQKwdwGv49DXwWUTzPTTjHBbU";
CKey notaryKey;


/*
 * We need to have control of clock,
 * otherwise block production can fail.
 */
int64_t nMockTime;

extern uint32_t USE_EXTERNAL_PUBKEY;
extern std::string NOTARY_PUBKEY;

void setupChain()
{
    SelectParams(CBaseChainParams::REGTEST);

    // Settings to get block reward
    NOTARY_PUBKEY = notaryPubkey;
    USE_EXTERNAL_PUBKEY = 1;
    mapArgs["-mineraddress"] = "bogus";
    COINBASE_MATURITY = 1;
    // Global mock time
    nMockTime = GetTime();
    
    // Unload
    UnloadBlockIndex();

    // Init blockchain
    ClearDatadirCache();
    auto pathTemp = GetTempPath() / strprintf("test_komodo_%li_%i", GetTime(), GetRand(100000));
    if (ASSETCHAINS_SYMBOL[0])
        pathTemp = pathTemp / strprintf("_%s", ASSETCHAINS_SYMBOL);
    boost::filesystem::create_directories(pathTemp);
    mapArgs["-datadir"] = pathTemp.string();
    pblocktree = new CBlockTreeDB(1 << 20, true);
    CCoinsViewDB *pcoinsdbview = new CCoinsViewDB(1 << 23, true);
    pcoinsTip = new CCoinsViewCache(pcoinsdbview);
    pnotarisations = new NotarisationDB(1 << 20, true);
    InitBlockIndex();
}

/***
 * Generate a block
 * @param block a place to store the block (nullptr skips the disk read)
 */
void generateBlock(CBlock *block)
{
    SetMockTime(nMockTime+=100);  // CreateNewBlock can fail if not enough time passes

    UniValue params;
    params.setArray();
    params.push_back(1);

    try {
        UniValue out = generate(params, false, CPubKey());
        uint256 blockId;
        blockId.SetHex(out[0].getValStr());
        if (block) 
            ASSERT_TRUE(ReadBlockFromDisk(*block, mapBlockIndex[blockId], false));
    } catch (const UniValue& e) {
        FAIL() << "failed to create block: " << e.write().data();
    }
}

/***
 * Accept a transaction, failing the gtest if the tx is not accepted
 * @param tx the transaction to be accepted
 */
void acceptTxFail(const CTransaction tx)
{
    CValidationState state;
    if (!acceptTx(tx, state)) 
        FAIL() << state.GetRejectReason();
}


bool acceptTx(const CTransaction tx, CValidationState &state)
{
    LOCK(cs_main);
    bool missingInputs = false;
    bool accepted = AcceptToMemoryPool(mempool, state, tx, false, &missingInputs, false, -1);
    return accepted && !missingInputs;
}

/***
 * Create a transaction based on input
 * @param txIn the vin data (which becomes prevout)
 * @param nOut the index of txIn to use as prevout
 * @returns the transaction
 */
CMutableTransaction spendTx(const CTransaction &txIn, int nOut)
{
    CMutableTransaction mtx;
    mtx.vin.resize(1);
    mtx.vin[0].prevout.hash = txIn.GetHash();
    mtx.vin[0].prevout.n = nOut;
    mtx.vout.resize(1);
    mtx.vout[0].nValue = txIn.vout[nOut].nValue - 1000;
    return mtx;
}


std::vector<uint8_t> getSig(const CMutableTransaction mtx, CScript inputPubKey, int nIn)
{
    uint256 hash = SignatureHash(inputPubKey, mtx, nIn, SIGHASH_ALL, 0, 0);
    std::vector<uint8_t> vchSig;
    notaryKey.Sign(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    return vchSig;
}


/*
 * In order to do tests there needs to be inputs to spend.
 * This method creates a block and returns a transaction that spends the coinbase.
 */
CTransaction getInputTx(CScript scriptPubKey)
{
    // Get coinbase
    CBlock block;
    generateBlock(&block);
    CTransaction coinbase = block.vtx[0];

    // Create tx
    auto mtx = spendTx(coinbase);
    mtx.vout[0].scriptPubKey = scriptPubKey;
    uint256 hash = SignatureHash(coinbase.vout[0].scriptPubKey, mtx, 0, SIGHASH_ALL, 0, 0);
    std::vector<unsigned char> vchSig;
    notaryKey.Sign(hash, vchSig);
    vchSig.push_back((unsigned char)SIGHASH_ALL);
    mtx.vin[0].scriptSig << vchSig;

    // Accept
    acceptTxFail(mtx);
    return CTransaction(mtx);
}

/****
 * A class to provide a simple chain for tests
 */

TestChain::TestChain()
{
    setupChain();
    CBitcoinSecret vchSecret;
    vchSecret.SetString(notarySecret); // this returns false due to network prefix mismatch but works anyway
    notaryKey = vchSecret.GetKey();
}

CBlock TestChain::generateBlock()
{
    CBlock block;
    ::generateBlock(&block);
    for(auto wallet : toBeNotified)
    {
        wallet->BlockNotification(block);
    }
    return block;
}

CKey TestChain::getNotaryKey() { return notaryKey; }

CValidationState TestChain::acceptTx(const CTransaction& tx)
{
    CValidationState retVal;
    bool accepted = ::acceptTx(tx, retVal);
    if (!accepted && retVal.IsValid())
        retVal.DoS(100, false, 0U, "acceptTx returned false");
    return retVal;
}

std::shared_ptr<TestWallet> TestChain::AddWallet(const CKey& in)
{
    std::shared_ptr<TestWallet> retVal = std::make_shared<TestWallet>(this, in);
    toBeNotified.push_back(retVal);
    return retVal;
}

std::shared_ptr<TestWallet> TestChain::AddWallet()
{
    std::shared_ptr<TestWallet> retVal = std::make_shared<TestWallet>(this);
    toBeNotified.push_back(retVal);
    return retVal;
}


/***
 * A simplistic (dumb) wallet for helping with testing
 * - It does not keep track of spent transactions
 * - Blocks containing vOuts that apply are added to the front of a vector
 */

TestWallet::TestWallet(TestChain* chain) : chain(chain)
{
    key.MakeNewKey(true);
    destScript = GetScriptForDestination(key.GetPubKey());
}

TestWallet::TestWallet(TestChain* chain, const CKey& in) : chain(chain), key(in)
{
    destScript = GetScriptForDestination(key.GetPubKey());
}

/***
 * @returns the public key
 */
CPubKey TestWallet::GetPubKey() const { return key.GetPubKey(); }

/***
 * @returns the private key
 */
CKey TestWallet::GetPrivKey() const { return key; }

/***
 * Sign a typical transaction
 * @param hash the hash to sign
 * @param hashType SIGHASH_ALL or something similar
 * @returns the bytes to add to ScriptSig
 */
std::vector<unsigned char> TestWallet::Sign(uint256 hash, unsigned char hashType)
{
    std::vector<unsigned char> retVal;
    key.Sign(hash, retVal);
    retVal.push_back(hashType);
    return retVal;
}

/***
 * Sign a cryptocondition
 * @param cc the cryptocondition
 * @param hash the hash to sign
 * @returns the bytes to add to ScriptSig
 */
std::vector<unsigned char> TestWallet::Sign(CC* cc, uint256 hash)
{
    int out = cc_signTreeSecp256k1Msg32(cc, key.begin(), hash.begin());            
    return CCSigVec(cc);
}

/***
 * Notifies this wallet of a new block
 */
void TestWallet::BlockNotification(const CBlock& block)
{
    // TODO: remove spent txs from availableTransactions
    // see if this block has any outs for me
    for( auto tx : block.vtx )
    {
        for(uint32_t i = 0; i < tx.vout.size(); ++i)
        {
            if (tx.vout[i].scriptPubKey == destScript)
            {
                availableTransactions.insert(availableTransactions.begin(), std::pair<CTransaction, uint32_t>(tx, i));
                break; // skip to next tx
            }
        }
    }
}

/***
 * Get a transaction that has funds
 * NOTE: If no single transaction matches, throws
 * @param needed how much is needed
 * @returns a pair of CTransaction and the n value of the vout
 */
std::pair<CTransaction, uint32_t> TestWallet::GetAvailable(CAmount needed)
{
    for(auto txp : availableTransactions)
    {
        CTransaction tx = txp.first;
        uint32_t n = txp.second;
        if (tx.vout[n].nValue >= needed)
            return txp;
    }
    throw std::logic_error("No Funds");
}

/***
 * Add a transaction to the list of available vouts
 * @param tx the transaction
 * @param n the n value of the vout
 */
void TestWallet::AddOut(CTransaction tx, uint32_t n)
{
    availableTransactions.insert(availableTransactions.begin(), std::pair<CTransaction, uint32_t>(tx, n));
}

/***
 * Transfer to another user
 * @param to who to transfer to
 * @param amount the amount
 * @returns the results
 */
CValidationState TestWallet::Transfer(std::shared_ptr<TestWallet> to, CAmount amount, CAmount fee)
{
    std::pair<CTransaction, uint32_t> available = GetAvailable(amount + fee);
    CMutableTransaction tx;
    CTxIn incoming;
    incoming.prevout.hash = available.first.GetHash();
    incoming.prevout.n = available.second;
    tx.vin.push_back(incoming);
    CTxOut out1;
    out1.scriptPubKey = GetScriptForDestination(to->GetPubKey());
    out1.nValue = amount;
    tx.vout.push_back(out1);
    // give the rest back to the notary
    CTxOut out2;
    out2.scriptPubKey = GetScriptForDestination(key.GetPubKey());
    out2.nValue = available.first.vout[available.second].nValue - amount - fee;
    tx.vout.push_back(out2);

    uint256 hash = SignatureHash(available.first.vout[available.second].scriptPubKey, tx, 0, SIGHASH_ALL, 0, 0);
    tx.vin[0].scriptSig << Sign(hash, SIGHASH_ALL);

    CTransaction fundTo(tx);
    return chain->acceptTx(fundTo);
}
