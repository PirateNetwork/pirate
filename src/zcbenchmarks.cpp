#include <cstdio>
#include <future>
#include <map>
#include <thread>
#include <unistd.h>
#include <boost/filesystem.hpp>

#include "coins.h"
#include "util.h"
#include "init.h"
#include "primitives/transaction.h"
#include "base58.h"
#include "crypto/equihash.h"
#include "chain.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "main.h"
#include "miner.h"
#include "pow.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "sodium.h"
#include "streams.h"
#include "txdb.h"
#include "utiltest.h"
#include "wallet/wallet.h"

#include "zcbenchmarks.h"

#include "zcash/Zcash.h"
#include "zcash/IncrementalMerkleTree.hpp"
#include "zcash/Note.hpp"
#include "librustzcash.h"

using namespace libzcash;
// This method is based on Shutdown from init.cpp
void pre_wallet_load()
{
    LogPrintf("%s: In progress...\n", __func__);
    if (ShutdownRequested())
        throw new std::runtime_error("The node is shutting down");

    if (pwalletMain)
        pwalletMain->Flush(false);
#ifdef ENABLE_MINING
    GenerateBitcoins(false, NULL, 0);
#endif
    UnregisterNodeSignals(GetNodeSignals());
    if (pwalletMain)
        pwalletMain->Flush(true);

    UnregisterValidationInterface(pwalletMain);
    delete pwalletMain;
    pwalletMain = NULL;
    bitdb->Reset();
    RegisterNodeSignals(GetNodeSignals());
    LogPrintf("%s: done\n", __func__);
}

void post_wallet_load(){
    RegisterValidationInterface(pwalletMain);
#ifdef ENABLE_MINING
    // Generate coins in the background
    if (pwalletMain || !GetArg("-mineraddress", "").empty())
        GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));
#endif
}


void timer_start(timeval &tv_start)
{
    gettimeofday(&tv_start, 0);
}

double timer_stop(timeval &tv_start)
{
    double elapsed;
    struct timeval tv_end;
    gettimeofday(&tv_end, 0);
    elapsed = double(tv_end.tv_sec-tv_start.tv_sec) +
        (tv_end.tv_usec-tv_start.tv_usec)/double(1000000);
    return elapsed;
}

double benchmark_sleep()
{
    struct timeval tv_start;
    timer_start(tv_start);
    sleep(1);
    return timer_stop(tv_start);
}

// double benchmark_parameter_loading()
// {
//     // FIXME: this is duplicated with the actual loading code
//     boost::filesystem::path pk_path = ZC_GetParamsDir() / "sprout-proving.key";
//     boost::filesystem::path vk_path = ZC_GetParamsDir() / "sprout-verifying.key";
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//
//     auto newParams = ZCJoinSplit::Prepared(vk_path.string(), pk_path.string());
//
//     double ret = timer_stop(tv_start);
//
//     delete newParams;
//
//     return ret;
// }

// double benchmark_create_joinsplit()
// {
//     uint256 joinSplitPubKey;
//
//     /* Get the anchor of an empty commitment tree. */
//     uint256 anchor = SproutMerkleTree().root();
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//     JSDescription jsdesc(true,
//                          *pzcashParams,
//                          joinSplitPubKey,
//                          anchor,
//                          {JSInput(), JSInput()},
//                          {JSOutput(), JSOutput()},
//                          0,
//                          0);
//     double ret = timer_stop(tv_start);
//
//     auto verifier = ProofVerifier::Strict();
//     assert(jsdesc.Verify(*pzcashParams, verifier, joinSplitPubKey));
//     return ret;
// }

// std::vector<double> benchmark_create_joinsplit_threaded(int nThreads)
// {
//     std::vector<double> ret;
//     std::vector<std::future<double>> tasks;
//     std::vector<std::thread> threads;
//     for (int i = 0; i < nThreads; i++) {
//         std::packaged_task<double(void)> task(&benchmark_create_joinsplit);
//         tasks.emplace_back(task.get_future());
//         threads.emplace_back(std::move(task));
//     }
//     std::future_status status;
//     for (auto it = tasks.begin(); it != tasks.end(); it++) {
//         it->wait();
//         ret.push_back(it->get());
//     }
//     for (auto it = threads.begin(); it != threads.end(); it++) {
//         it->join();
//     }
//     return ret;
// }

// double benchmark_verify_joinsplit(const JSDescription &joinsplit)
// {
//     struct timeval tv_start;
//     timer_start(tv_start);
//     uint256 joinSplitPubKey;
//     auto verifier = ProofVerifier::Strict();
//     joinsplit.Verify(*pzcashParams, verifier, joinSplitPubKey);
//     return timer_stop(tv_start);
// }

#ifdef ENABLE_MINING
double benchmark_solve_equihash()
{
    CBlock pblock;
    CEquihashInput I{pblock};
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << I;

    unsigned int n = Params(CBaseChainParams::MAIN).EquihashN();
    unsigned int k = Params(CBaseChainParams::MAIN).EquihashK();
    crypto_generichash_blake2b_state eh_state;
    EhInitialiseState(n, k, eh_state);
    crypto_generichash_blake2b_update(&eh_state, (unsigned char*)&ss[0], ss.size());

    uint256 nonce;
    randombytes_buf(nonce.begin(), 32);
    crypto_generichash_blake2b_update(&eh_state,
                                    nonce.begin(),
                                    nonce.size());

    struct timeval tv_start;
    timer_start(tv_start);
    std::set<std::vector<unsigned int>> solns;
    EhOptimisedSolveUncancellable(n, k, eh_state,
                                  [](std::vector<unsigned char> soln) { return false; });
    return timer_stop(tv_start);
}

std::vector<double> benchmark_solve_equihash_threaded(int nThreads)
{
    std::vector<double> ret;
    std::vector<std::future<double>> tasks;
    std::vector<std::thread> threads;
    for (int i = 0; i < nThreads; i++) {
        std::packaged_task<double(void)> task(&benchmark_solve_equihash);
        tasks.emplace_back(task.get_future());
        threads.emplace_back(std::move(task));
    }
    std::future_status status;
    for (auto it = tasks.begin(); it != tasks.end(); it++) {
        it->wait();
        ret.push_back(it->get());
    }
    for (auto it = threads.begin(); it != threads.end(); it++) {
        it->join();
    }
    return ret;
}
#endif // ENABLE_MINING

double benchmark_verify_equihash()
{
    CChainParams params = Params(CBaseChainParams::MAIN);
    CBlock genesis = Params(CBaseChainParams::MAIN).GenesisBlock();
    CBlockHeader genesis_header = genesis.GetBlockHeader();
    struct timeval tv_start;
    timer_start(tv_start);
    CheckEquihashSolution(&genesis_header, params);
    return timer_stop(tv_start);
}

// double benchmark_large_tx(size_t nInputs)
// {
//     // Create priv/pub key
//     CKey priv;
//     priv.MakeNewKey(false);
//     auto pub = priv.GetPubKey();
//     CBasicKeyStore tempKeystore;
//     tempKeystore.AddKey(priv);
//
//     // The "original" transaction that the spending transaction will spend
//     // from.
//     CMutableTransaction m_orig_tx;
//     m_orig_tx.vout.resize(1);
//     m_orig_tx.vout[0].nValue = 1000000;
//     CScript prevPubKey = GetScriptForDestination(pub.GetID());
//     m_orig_tx.vout[0].scriptPubKey = prevPubKey;
//
//     auto orig_tx = CTransaction(m_orig_tx);
//
//     CMutableTransaction spending_tx;
//     spending_tx.fOverwintered = true;
//     spending_tx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
//     spending_tx.nVersion = SAPLING_TX_VERSION;
//
//     auto input_hash = orig_tx.GetHash();
//     // Add nInputs inputs
//     for (size_t i = 0; i < nInputs; i++) {
//         spending_tx.vin.emplace_back(input_hash, 0);
//     }
//
//     // Sign for all the inputs
//     auto consensusBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;
//     for (size_t i = 0; i < nInputs; i++) {
//         SignSignature(tempKeystore, prevPubKey, spending_tx, i, 1000000, SIGHASH_ALL, consensusBranchId);
//     }
//
//     // Spending tx has all its inputs signed and does not need to be mutated anymore
//     CTransaction final_spending_tx(spending_tx);
//
//     // Benchmark signature verification costs:
//     struct timeval tv_start;
//     timer_start(tv_start);
//     PrecomputedTransactionData txdata(final_spending_tx);
//     for (size_t i = 0; i < nInputs; i++) {
//         ScriptError serror = SCRIPT_ERR_OK;
//         assert(VerifyScript(final_spending_tx.vin[i].scriptSig,
//                             prevPubKey,
//                             STANDARD_SCRIPT_VERIFY_FLAGS,
//                             TransactionSignatureChecker(&final_spending_tx, i, 1000000, txdata),
//                             consensusBranchId,
//                             &serror));
//     }
//     return timer_stop(tv_start);
// }

// double benchmark_try_decrypt_notes(size_t nAddrs)
// {
//     CWallet wallet;
//     for (int i = 0; i < nAddrs; i++) {
//         auto sk = libzcash::SproutSpendingKey::random();
//         wallet.AddSproutSpendingKey(sk);
//     }
//
//     auto sk = libzcash::SproutSpendingKey::random();
//     auto tx = GetValidReceive(*pzcashParams, sk, 10, true);
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//     auto nd = wallet.FindMySproutNotes(tx);
//     return timer_stop(tv_start);
// }

// double benchmark_increment_note_witnesses(size_t nTxs)
// {
//     CWallet wallet;
//     SproutMerkleTree sproutTree;
//     SaplingMerkleTree saplingTree;
//
//     auto sk = libzcash::SproutSpendingKey::random();
//     wallet.AddSproutSpendingKey(sk);
//
//     // First block
//     CBlock block1;
//     for (int i = 0; i < nTxs; i++) {
//         auto wtx = GetValidReceive(*pzcashParams, sk, 10, true);
//         auto note = GetNote(*pzcashParams, sk, wtx, 0, 1);
//         auto nullifier = note.nullifier(sk);
//
//         mapSproutNoteData_t noteData;
//         JSOutPoint jsoutpt {wtx.GetHash(), 0, 1};
//         SproutNoteData nd {sk.address(), nullifier};
//         noteData[jsoutpt] = nd;
//
//         wtx.SetSproutNoteData(noteData);
//         wallet.AddToWallet(wtx, true, NULL, 0);
//         block1.vtx.push_back(wtx);
//     }
//     CBlockIndex index1(block1);
//     index1.nHeight = 1;
//
//     // Increment to get transactions witnessed
//     wallet.ChainTip(&index1, &block1, sproutTree, saplingTree, true);
//
//     // Second block
//     CBlock block2;
//     block2.hashPrevBlock = block1.GetHash();
//     {
//         auto wtx = GetValidReceive(*pzcashParams, sk, 10, true);
//         auto note = GetNote(*pzcashParams, sk, wtx, 0, 1);
//         auto nullifier = note.nullifier(sk);
//
//         mapSproutNoteData_t noteData;
//         JSOutPoint jsoutpt {wtx.GetHash(), 0, 1};
//         SproutNoteData nd {sk.address(), nullifier};
//         noteData[jsoutpt] = nd;
//
//         wtx.SetSproutNoteData(noteData);
//         wallet.AddToWallet(wtx, true, NULL, 0);
//         block2.vtx.push_back(wtx);
//     }
//     CBlockIndex index2(block2);
//     index2.nHeight = 2;
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//     wallet.ChainTip(&index2, &block2, sproutTree, saplingTree, true);
//     return timer_stop(tv_start);
// }

// Fake the input of a given block
class FakeCoinsViewDB : public CCoinsViewDB {
    uint256 hash;
    SproutMerkleTree t;

public:
    FakeCoinsViewDB(std::string dbName, uint256& hash) : CCoinsViewDB(dbName, 100, false, false), hash(hash) {}

    bool GetAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
        if (rt == t.root()) {
            tree = t;
            return true;
        }
        return false;
    }

    bool GetNullifier(const uint256 &nf, ShieldedType type) const {
        return false;
    }

    uint256 GetBestBlock() const {
        return hash;
    }

    uint256 GetBestAnchor() const {
        return t.root();
    }

    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    const uint256 &hashSaplingFrontierAnchor,
                    const uint256 &hashIronwoodFrontierAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CAnchorsSaplingFrontierMap &mapSaplingFrontierAnchors,
                    CAnchorsIronwoodFrontierMap &mapIronwoodFrontierAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers,
                    CNullifiersMap &mapIronwoodNullifiers,
                    CHistoryCacheMap &historyCacheMap) {
        return false;
    }

    bool GetStats(CCoinsStats &stats) const {
        return false;
    }
};

double benchmark_connectblock_slow()
{
    // Test for issue 2017-05-01.a
    SelectParams(CBaseChainParams::MAIN);
    CBlock block;
    FILE* fp = fopen((GetDataDir() / "benchmark/block-107134.dat").string().c_str(), "rb");
    if (!fp) throw new std::runtime_error("Failed to open block data file");
    CAutoFile blkFile(fp, SER_DISK, CLIENT_VERSION);
    blkFile >> block;
    blkFile.fclose();

    // Fake its inputs
    auto hashPrev = uint256S("00000000159a41f468e22135942a567781c3f3dc7ad62257993eb3c69c3f95ef");
    FakeCoinsViewDB fakeDB("benchmark/block-107134-inputs", hashPrev);
    CCoinsViewCache view(&fakeDB);

    // Fake the chain
    CBlockIndex index(block);
    index.nHeight = 107134;
    CBlockIndex indexPrev;
    indexPrev.phashBlock = &hashPrev;
    indexPrev.nHeight = index.nHeight - 1;
    index.pprev = &indexPrev;
    mapBlockIndex.insert(std::make_pair(hashPrev, &indexPrev));

    CValidationState state;
    struct timeval tv_start;
    timer_start(tv_start);
    assert(ConnectBlock(block, state, &index, view, true));
    auto duration = timer_stop(tv_start);

    // Undo alterations to global state
    mapBlockIndex.erase(hashPrev);
    SelectParamsFromCommandLine();

    return duration;
}

extern UniValue getnewaddress(const UniValue& params, bool fHelp, const CPubKey& mypk); // in rpcwallet.cpp
extern UniValue sendtoaddress(const UniValue& params, bool fHelp, const CPubKey& mypk);

double benchmark_sendtoaddress(CAmount amount)
{
    UniValue params(UniValue::VARR);
    auto addr = getnewaddress(params, false, CPubKey());

    params.push_back(addr);
    params.push_back(ValueFromAmount(amount));

    struct timeval tv_start;
    timer_start(tv_start);
    auto txid = sendtoaddress(params, false, CPubKey());
    return timer_stop(tv_start);
}

double benchmark_loadwallet()
{
    pre_wallet_load();
    struct timeval tv_start;
    bool fFirstRunRet=true;
    timer_start(tv_start);
    pwalletMain = new CWallet("wallet.dat");
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRunRet);
    auto res = timer_stop(tv_start);
    post_wallet_load();
    return res;
}

extern UniValue listunspent(const UniValue& params, bool fHelp, const CPubKey& mypk);

double benchmark_listunspent()
{
    UniValue params(UniValue::VARR);
    struct timeval tv_start;
    timer_start(tv_start);
    auto unspent = listunspent(params, false, CPubKey());
    return timer_stop(tv_start);
}

// double benchmark_create_sapling_spend()
// {
//     auto sk = libzcash::SaplingSpendingKey::random();
//     auto expsk = sk.expanded_spending_key();
//     auto address = sk.default_address();
//     SaplingNote note(address, GetRand(MAX_MONEY), libzcash::Zip212Enabled::BeforeZip212);
//     SaplingMerkleTree tree;
//     auto maybe_cm = note.cmu();
//     tree.append(maybe_cm.value());
//     auto anchor = tree.root();
//     auto witness = tree.witness();
//     auto maybe_nf = note.nullifier(expsk.full_viewing_key(), witness.position());
//     if (!(maybe_cm && maybe_nf)) {
//         throw JSONRPCError(RPC_INTERNAL_ERROR, "Could not create note commitment and nullifier");
//     }
//
//     CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//     ss << witness.path();
//     std::vector<unsigned char> witnessChars(ss.begin(), ss.end());
//
//     uint256 alpha;
//     librustzcash_sapling_generate_r(alpha.begin());
//
//     auto ctx = librustzcash_sapling_proving_ctx_init();
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//
//     SpendDescription sdesc;
//     uint256 rcm = note.rcm();
//     bool result = librustzcash_sapling_spend_proof(
//         ctx,
//         expsk.full_viewing_key().ak.begin(),
//         expsk.nsk.begin(),
//         note.d.data(),
//         rcm.begin(),
//         alpha.begin(),
//         note.value(),
//         anchor.begin(),
//         witnessChars.data(),
//         sdesc.cv.begin(),
//         sdesc.rk.begin(),
//         sdesc.zkproof.data());
//
//     double t = timer_stop(tv_start);
//     librustzcash_sapling_proving_ctx_free(ctx);
//     if (!result) {
//         throw JSONRPCError(RPC_INTERNAL_ERROR, "librustzcash_sapling_spend_proof() should return true");
//     }
//     return t;
// }

// double benchmark_create_sapling_output()
// {
//     auto sk = libzcash::SaplingSpendingKey::random();
//     auto address = sk.default_address();
//
//     std::array<unsigned char, ZC_MEMO_SIZE> memo;
//     SaplingNote note(address, GetRand(MAX_MONEY),  libzcash::Zip212Enabled::BeforeZip212);
//
//     libzcash::SaplingNotePlaintext notePlaintext(note, memo);
//     auto res = notePlaintext.encrypt(note.pk_d);
//     if (!res) {
//         throw JSONRPCError(RPC_INTERNAL_ERROR, "SaplingNotePlaintext::encrypt() failed");
//     }
//
//     auto enc = res.value();
//     auto encryptor = enc.second;
//
//     CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
//     ss << address;
//     std::vector<unsigned char> addressBytes(ss.begin(), ss.end());
//
//     auto ctx = librustzcash_sapling_proving_ctx_init();
//
//     struct timeval tv_start;
//     timer_start(tv_start);
//
//     OutputDescription odesc;
//     uint256 rcm = note.rcm();
//     bool result = librustzcash_sapling_output_proof(
//         ctx,
//         encryptor.get_esk().begin(),
//         addressBytes.data(),
//         rcm.begin(),
//         note.value(),
//         odesc.cv.begin(),
//         odesc.zkproof.begin());
//
//     double t = timer_stop(tv_start);
//     librustzcash_sapling_proving_ctx_free(ctx);
//     if (!result) {
//         throw JSONRPCError(RPC_INTERNAL_ERROR, "librustzcash_sapling_output_proof() should return true");
//     }
//     return t;
// }
//
// // Verify Sapling output from testnet
// // txid: abbd823cbd3d4e3b52023599d81a96b74817e95ce5bb58354f979156bd22ecc8
// // position: 0
// double benchmark_verify_sapling_output()
// {
//     // Migrated to bridge.rs/sapling.rs Verifier
// }
