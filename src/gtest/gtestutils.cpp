#include <cryptoconditions.h>
#include <gtest/gtest.h>
#include <boost/filesystem.hpp>
#include <set>

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
#include "util/strencodings.h"
#include "utiltime.h"
#include "consensus/validation.h"
#include "primitives/transaction.h"
#include "script/cc.h"
#include "script/interpreter.h"
#include "komodo_extern_globals.h"
#include "komodo_globals.h"
#include "komodo_notary.h"
#include "komodo_bitcoind.h"
#include "utilmoneystr.h"
#include "gtest/gtestutils.h"
#include "coincontrol.h"
#include "cc/CCinclude.h"

// Global variables for testing
std::string notaryPubkey = "0205a8ad0c1dbc515f149af377981aab58b836af008d4d7ab21bd76faf80550b47";
std::string notarySecret = "UxFWWxsf1d7w7K5TvAWSkeX4H95XQKwdwGv49DXwWUTzPTTjHBbU";
CKey notaryKey;

// Global test wallet
TestWallet* pTestWallet = nullptr; 

/*
 * We need to have control of clock,
 * otherwise block production can fail.
 */
int64_t nMockTime;

extern int32_t USE_EXTERNAL_PUBKEY;
extern std::string NOTARY_PUBKEY;

void adjust_hwmheight(int32_t in); // in komodo.cpp
CCriticalSection& get_cs_main(); // in main.cpp
std::shared_ptr<CBlock> generateBlock(CWallet* wallet, CValidationState* state = nullptr); // in mining.cpp

static bool AlwaysBlockDownloadTrue() { return true; }
static bool AlwaysBlockDownloadFalse() { return false; }

CheckTransationResults ContextualCheckTransactionSingleThreaded(
    const CTransaction& tx, int nHeight, int dosLevel, bool isInitBlockDownload)
{
    CheckTransationResults results;
    CValidationState state;
    results.validationPassed = ContextualCheckTransaction(
        tx, state, nHeight, dosLevel,
        isInitBlockDownload ? AlwaysBlockDownloadTrue : AlwaysBlockDownloadFalse);
    results.reasonString = state.GetRejectReason();
    int nDoS = 0;
    state.IsInvalid(nDoS);
    results.dosLevel = nDoS;
    return results;
}

CheckTransationResults ContextualCheckTransactionSingleThreaded(
    const CTransaction& tx, int nHeight, bool isInitBlockDownload)
{
    return ContextualCheckTransactionSingleThreaded(tx, nHeight, 100, isInitBlockDownload);
}

CheckTransationResults ContextualCheckTransactionShieldedBundles(
    const std::vector<const CTransaction*>& vtx, CCoinsViewCache* view,
    uint32_t consensusBranchId, bool isInitBlockDownload, bool isMined)
{
    CheckTransationResults results;
    CValidationState state;
    const Consensus::Params& consensus = Params().GetConsensus();
    std::optional<rust::Box<sapling::BatchValidator>> saplingAuth = std::nullopt;
    std::optional<rust::Box<ironwood::BatchValidator>> ironwoodAuth = std::nullopt;

    for (const CTransaction* ptx : vtx) {
        std::vector<CTxOut> allPrevOutputs;
        allPrevOutputs.resize(ptx->vin.size());
        PrecomputedTransactionData txdata(*ptx, allPrevOutputs);
        if (!ContextualCheckShieldedInputs(*ptx, txdata, state, *view, saplingAuth, ironwoodAuth,
                                            consensus, consensusBranchId, isMined,
                                            isInitBlockDownload ? AlwaysBlockDownloadTrue : AlwaysBlockDownloadFalse)) {
            results.validationPassed = false;
            results.reasonString = state.GetRejectReason();
            int nDoS = 0;
            state.IsInvalid(nDoS);
            results.dosLevel = nDoS;
            return results;
        }
    }
    return results;
}

void displayTransaction(const CTransaction& tx)
{
    std::cout << "Transaction Hash: " << tx.GetHash().ToString();
    for(size_t i = 0; i < tx.vin.size(); ++i)
    {
        std::cout << "\nvIn " << i
                << " prevout hash : " << tx.vin[i].prevout.hash.ToString()
                << " n: " << tx.vin[i].prevout.n;
    }
    for(size_t i = 0; i < tx.vout.size(); ++i)
    {
        std::cout << "\nvOut " << i
                << " nValue: " << tx.vout[i].nValue
                << " scriptPubKey: " << tx.vout[i].scriptPubKey.ToString()
                << " interest: " << tx.vout[i].interest;
    }
    std::cout << "\n";
}

void displayBlock(const CBlock& blk)
{
    std::cout << "Block Hash: " << blk.GetHash().ToString()
            << "\nPrev Hash: " << blk.hashPrevBlock.ToString()
            << "\n";
    for(size_t i = 0; i < blk.vtx.size(); ++i)
    {
        std::cout << i << " ";
        displayTransaction(blk.vtx[i]);
    }
    std::cout << "\n";
}

void setConsoleDebugging(bool enable)
{
    fPrintToConsole = enable;
}

void setupChain()
{
    SelectParams(CBaseChainParams::REGTEST);

    // Settings to get block reward
    NOTARY_PUBKEY = notaryPubkey;
    USE_EXTERNAL_PUBKEY = 1;
    mapArgs["-mineraddress"] = "bogus";
    // Some tests (e.g. test_metrics.cpp) call SetMockTime() and never reset it back
    // to 0, which would freeze GetTime() at a stale value left over from an earlier
    // test in the same process. That stale time can be far enough in the past that
    // the genesis block's real timestamp looks "from the future" to CheckBlockHeader,
    // which silently fails to activate the genesis block. Clear it first so GetTime()
    // below always reflects real wall-clock time.
    SetMockTime(0);
    // Global mock time
    nMockTime = GetTime();

    // Unload
    UnloadBlockIndex();

    // Init blockchain
    ClearDatadirCache();
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


// std::vector<uint8_t> getSig(const CMutableTransaction mtx, CScript inputPubKey, int nIn)
// {
//     uint256 hash = SignatureHash(inputPubKey, mtx, nIn, SIGHASH_ALL, 0, 0);
//     std::vector<uint8_t> vchSig;
//     notaryKey.Sign(hash, vchSig);
//     vchSig.push_back((unsigned char)SIGHASH_ALL);
//     return vchSig;
// }


/*
 * In order to do tests there needs to be inputs to spend.
 * This method creates a block and returns a transaction that spends the coinbase.
 */
// CTransaction getInputTx(CScript scriptPubKey)
// {
//     // Get coinbase
//     CBlock block;
//     generateBlock(&block);
//     CTransaction coinbase = block.vtx[0];
//
//     // Create tx
//     auto mtx = spendTx(coinbase);
//     mtx.vout[0].scriptPubKey = scriptPubKey;
//     uint256 hash = SignatureHash(coinbase.vout[0].scriptPubKey, mtx, 0, SIGHASH_ALL, 0, 0);
//     std::vector<unsigned char> vchSig;
//     notaryKey.Sign(hash, vchSig);
//     vchSig.push_back((unsigned char)SIGHASH_ALL);
//     mtx.vin[0].scriptSig << vchSig;
//
//     // Accept
//     acceptTxFail(mtx);
//     return CTransaction(mtx);
// }

/****
 * A class to provide a simple chain for tests
 */

TestChain::TestChain()
{
    CleanGlobals();
    // Reset chainName to the KMD default before touching the datadir/setupChain(): some
    // earlier test in the full suite may have left chainName set to a non-default value
    // (e.g. via assetchain("TST")) without resetting it, and setupChain()'s InitBlockIndex()
    // creates/activates the genesis block before this test gets a chance to set its own
    // chainName, so a stale value here can silently break genesis activation.
    chainName = assetchain();
    previousNetwork = Params().NetworkIDString();
    dataDir = GetTempPath() / strprintf("test_komodo_%li_%i", GetTime(), GetRand(100000));
    if (!chainName.isKMD())
        dataDir = dataDir / strprintf("_%s", chainName.symbol().c_str());
    boost::filesystem::create_directories(dataDir);
    mapArgs["-datadir"] = dataDir.string();

    setupChain();
    USE_EXTERNAL_PUBKEY = 0; // we want control of who mines the block
    CBitcoinSecret vchSecret;
    vchSecret.SetString(notarySecret); // this returns false due to network prefix mismatch but works anyway
    notaryKey = vchSecret.GetKey();
}

TestChain::~TestChain()
{
    CleanGlobals();
    // cruel and crude, but cleans up any wallet dbs so subsequent tests run.
    bitdb = std::shared_ptr<CDBEnv>(new CDBEnv{});
    try {
        boost::filesystem::remove_all(dataDir);
    } catch(boost::filesystem::filesystem_error &ex) {} // throws exception on windows due to db.log being busy (apparently it is not closed)
    if (previousNetwork == "main")
        SelectParams(CBaseChainParams::MAIN);
    if (previousNetwork == "regtest")
        SelectParams(CBaseChainParams::REGTEST);
    if (previousNetwork == "test")
        SelectParams(CBaseChainParams::TESTNET);

}

boost::filesystem::path TestChain::GetDataDir() { return dataDir; }

void TestChain::CleanGlobals()
{
    // hwmheight can get skewed if komodo_connectblock not called (which some tests do)
    adjust_hwmheight(0);
    for(int i = 0; i < KOMODO_STATES_NUMBER; ++i)
    {
        komodo_state s = KOMODO_STATES[i];
        s.events.clear();
        // TODO: clean notarization points
    }
}

/***
 * Get the block index at the specified height
 * @param height the height (0 indicates current height)
 * @returns the block index
 */
CBlockIndex *TestChain::GetIndex(uint32_t height)
{
    if (height == 0)
        return chainActive.Tip();
    return chainActive[height];

}

void TestChain::IncrementChainTime()
{
    SetMockTime(nMockTime += 100);
}

CCoinsViewCache *TestChain::GetCoinsViewCache()
{
    return pcoinsTip;
}

std::shared_ptr<CBlock> TestChain::generateBlock(std::shared_ptr<CWallet> wallet, CValidationState* state)
{
    std::shared_ptr<CBlock> block;
    if (wallet == nullptr)
    {
        CBlock blk;
        ::generateBlock(&blk);
        block = std::shared_ptr<CBlock>(new CBlock(blk) );
    }
    else
        block = ::generateBlock(wallet.get(), state);
    return block;
}

bool TestChain::ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex,
        bool fJustCheck, bool fCheckPOW)
{
    LOCK( get_cs_main() );
    return ::ConnectBlock(block, state, pindex, *(this->GetCoinsViewCache()), fJustCheck, fCheckPOW);
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

/**
 * Call after CreateTransaction unless you want to abort
 */
bool TestWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, CValidationState& state)
{
    {
        LOCK2(cs_main, cs_wallet);
        LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r+") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew, false, pwalletdb, 0);

            // Notify that old coins are spent
            std::set<CWalletTx*> setCoins;
            BOOST_FOREACH(const CTxIn& txin, wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        if (fBroadcastTransactions)
        {
            // Broadcast
            if (!::AcceptToMemoryPool(mempool, state, wtxNew, false, nullptr))
            {
                // This must not fail. The transaction has already been signed and recorded.
                LogPrintf("CommitTransaction(): Error: Transaction not valid\n");
                return false;
            }
            wtxNew.RelayWalletTransaction();
        }
    }
    return true;
}
