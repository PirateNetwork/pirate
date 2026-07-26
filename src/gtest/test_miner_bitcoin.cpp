// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "arith_uint256.h"
#include "consensus/validation.h"
#include "main.h"
#include "miner.h"
#include "pubkey.h"
#include "uint256.h"
#include "util.h"
#include "crypto/equihash.h"
//#include "pow/tromp/equi_miner.h"

#include "gtest/gtestutils.h"

#include <gtest/gtest.h>

// The original test replayed 110 pre-mined (nonce, Equihash solution) pairs.
// Those were mined against MAIN's full mainnet-difficulty Equihash params
// (N=200, K=9) and a coinbase byte layout that has since changed (Overwinter/
// Sapling field handling, the Rust-backed digest rewrite). Rather than perpetuate
// a blob that goes stale every time coinbase serialization changes, this fixture
// selects REGTEST (N=48, K=5) so each block can be genuinely solved in real time.
class miner_tests_bitcoin : public BitcoinTestingSetup {
protected:
    void SelectTestParams() override { SelectParams(CBaseChainParams::REGTEST); }
};

// Solves pblock's Equihash PoW in place (sets nNonce/nSolution), incrementing
// the nonce and re-solving until the resulting header hash also satisfies
// nBits - mirrors the validBlock/EhOptimisedSolve pattern in miner.cpp's real
// mining loop, minus the notary/staking-specific branches that don't apply here.
static void MineBlock(CBlock* pblock)
{
    unsigned int n = Params().EquihashN();
    unsigned int k = Params().EquihashK();
    arith_uint256 hashTarget;
    hashTarget.SetCompact(pblock->nBits);

    while (true) {
        crypto_generichash_blake2b_state eh_state;
        EhInitialiseState(n, k, eh_state);

        CEquihashInput I{*pblock};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << I;
        crypto_generichash_blake2b_update(&eh_state, (const unsigned char*)&ss[0], ss.size());

        crypto_generichash_blake2b_state curr_state = eh_state;
        crypto_generichash_blake2b_update(&curr_state, pblock->nNonce.begin(), pblock->nNonce.size());

        bool found = false;
        std::function<bool(std::vector<unsigned char>)> validBlock =
            [pblock, &hashTarget, &found](std::vector<unsigned char> soln) {
                pblock->nSolution = soln;
                if (UintToArith256(pblock->GetHash()) > hashTarget) {
                    return false;
                }
                found = true;
                return true;
            };
        std::function<bool(EhSolverCancelCheck)> cancelled = [](EhSolverCancelCheck) { return false; };
        try {
            EhOptimisedSolve(n, k, curr_state, validBlock, cancelled);
        } catch (EhSolverCancelledException&) {}

        if (found) return;

        arith_uint256 nonce = UintToArith256(pblock->nNonce) + 1;
        pblock->nNonce = ArithToUint256(nonce);
    }
}

// NOTE: These tests rely on CreateNewBlock doing its own self-validation!
TEST_F(miner_tests_bitcoin, CreateNewBlock_validity)
{
    CScript scriptPubKey = CScript() << ParseHex("04678afdb0fe5548271967f1a67130b7105cd6a828e03909a67962e0ea1f61deb649f6bc3f4cef38c4f35504e51ec112de5c384df7ba0b8d578a4c702b6bf11d5f") << OP_CHECKSIG;
    CBlockTemplate *pblocktemplate;
    CMutableTransaction tx,tx2;
    CScript script;
    uint256 hash;
    TestMemPoolEntryHelper entry;
    entry.nFee = 11;
    entry.dPriority = 111.0;
    entry.nHeight = 11;

    LOCK(cs_main);
    fCheckpointsEnabled = false;
    fCoinbaseEnforcedProtectionEnabled = false;

    // We can't make transactions until we have inputs
    // Therefore, load 100 blocks :)
    std::vector<CTransaction*>txFirst;
    const unsigned int NUM_BLOCKS_TO_MINE = 110;
    for (unsigned int i = 0; i < NUM_BLOCKS_TO_MINE; ++i)
    {
        // Simple block creation, nothing special yet:
        EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));

        CBlock *pblock = &pblocktemplate->block; // pointer for convenience
        pblock->nVersion = 4;
        // Fake the blocks taking at least nPowTargetSpacing to be mined.
        // GetMedianTimePast() returns the median of 11 blocks, so the timestamp
        // of the next block must be six spacings ahead of that to be at least
        // one spacing ahead of the tip. Within 11 blocks of genesis, the median
        // will be closer to the tip, and blocks will appear slower.
        pblock->nTime = chainActive.Tip()->GetMedianTimePast()+6*Params().GetConsensus().nPowTargetSpacing;
        // Leave txCoinbase's version fields exactly as CreateNewBlock built them:
        // this loop mines past REGTEST's Overwinter(50)/Sapling(100) activation
        // heights, so forcing a fixed legacy version (as the original vanilla-
        // Bitcoin-derived test did) would desync from what's actually valid at
        // each height once those upgrades activate mid-loop.
        CMutableTransaction txCoinbase(pblock->vtx[0]);
        txCoinbase.vin[0].scriptSig = CScript() << (chainActive.Height()+1) << OP_0;
        txCoinbase.vout[0].scriptPubKey = CScript();
        // GetBlockSubsidy() special-cases height 1 as a 100,000,000*COIN one-time
        // ICO allocation (chainName.isKMD() is true by default in this bare gtest
        // environment, with no komodo_args()/AppInit2 run to set a real chain
        // identity). That figure overflows the 21,000,000*COIN MAX_MONEY hardcoded
        // in the vendored zcash_protocol Rust crate, which CTransaction::UpdateHash()
        // uses for every tx hash - so clamp the coinbase value here rather than
        // changing chainName, which would instead route through this fork's
        // staked-chain/notary-pay mining logic that assumes real daemon-startup
        // initialization this fixture doesn't perform.
        if (txCoinbase.vout[0].nValue > COIN * 1000)
            txCoinbase.vout[0].nValue = COIN * 1000;
        pblock->vtx[0] = CTransaction(txCoinbase);
        if (txFirst.size() < 2)
            txFirst.push_back(new CTransaction(pblock->vtx[0]));
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();
        pblock->nNonce = uint256();
        MineBlock(pblock);

        CValidationState state;
        EXPECT_TRUE(ProcessNewBlock(1,0,state, NULL, pblock, true, NULL));
        EXPECT_TRUE(state.IsValid()) << state.GetRejectReason();
        pblock->hashPrevBlock = pblock->GetHash();

        // Need to recreate the template each round because of mining slow start
        delete pblocktemplate;
    }

    // Just to make sure we can still make simple blocks
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;

    // block sigops > limit: 1000 CHECKMULTISIG + 1
    tx.vin.resize(1);
    // NOTE: OP_NOP is used to force 20 SigOps for the CHECKMULTISIG
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << OP_NOP << OP_CHECKMULTISIG << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vout.resize(1);
    tx.vout[0].nValue = 50000LL;
    for (unsigned int i = 0; i < 1001; ++i)
    {
        tx.vout[0].nValue -= 10;
        hash = tx.GetHash();
        bool spendsCoinbase = (i == 0) ? true : false; // only first tx spends coinbase
        mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // block size > limit
    tx.vin[0].scriptSig = CScript();
    // 18 * (520char + DROP) + OP_1 = 9433 bytes
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 18; ++i)
        tx.vin[0].scriptSig << vchData << OP_DROP;
    tx.vin[0].scriptSig << OP_1;
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vout[0].nValue = 50000LL;
    for (unsigned int i = 0; i < 128; ++i)
    {
        tx.vout[0].nValue -= 350;
        hash = tx.GetHash();
        bool spendsCoinbase = (i == 0) ? true : false; // only first tx spends coinbase
        mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(spendsCoinbase).FromTx(tx));
        tx.vin[0].prevout.hash = hash;
    }
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // orphan in mempool
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).FromTx(tx));
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // child with higher priority than parent
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx.vout[0].nValue = 39000LL;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin.resize(2);
    tx.vin[1].scriptSig = CScript() << OP_1;
    tx.vin[1].prevout.hash = txFirst[0]->GetHash();
    tx.vin[1].prevout.n = 0;
    tx.vout[0].nValue = 49000LL;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // coinbase in mempool
    tx.vin.resize(1);
    tx.vin[0].prevout.SetNull();
    tx.vin[0].scriptSig = CScript() << OP_0 << OP_1;
    tx.vout[0].nValue = 0;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // invalid (pre-p2sh) txn in mempool
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].prevout.n = 0;
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = 49000LL;
    script = CScript() << OP_0;
    tx.vout[0].scriptPubKey = GetScriptForDestination(CScriptID(script));
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vin[0].prevout.hash = hash;
    tx.vin[0].scriptSig = CScript() << std::vector<unsigned char>(script.begin(), script.end());
    tx.vout[0].nValue -= 10000;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(false).FromTx(tx));
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // double spend txn pair in mempool
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vout[0].nValue = 49000LL;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    tx.vout[0].scriptPubKey = CScript() << OP_2;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    mempool.clear();

    // subsidy changing
    int nHeight = chainActive.Height();
    chainActive.Tip()->nHeight = 209999;
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    chainActive.Tip()->nHeight = 210000;
    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    delete pblocktemplate;
    chainActive.Tip()->nHeight = nHeight;

    // non-final txs in mempool
    SetMockTime(chainActive.Tip()->GetMedianTimePast()+1);

    // height locked
    tx.vin[0].prevout.hash = txFirst[0]->GetHash();
    tx.vin[0].scriptSig = CScript() << OP_1;
    tx.vin[0].nSequence = 0;
    tx.vout[0].nValue = 49000LL;
    tx.vout[0].scriptPubKey = CScript() << OP_1;
    tx.nLockTime = chainActive.Tip()->nHeight+1;
    hash = tx.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx));
    EXPECT_TRUE(!CheckFinalTx(tx, LOCKTIME_MEDIAN_TIME_PAST));

    // time locked
    tx2.vin.resize(1);
    tx2.vin[0].prevout.hash = txFirst[1]->GetHash();
    tx2.vin[0].prevout.n = 0;
    tx2.vin[0].scriptSig = CScript() << OP_1;
    tx2.vin[0].nSequence = 0;
    tx2.vout.resize(1);
    tx2.vout[0].nValue = 79000LL;
    tx2.vout[0].scriptPubKey = CScript() << OP_1;
    tx2.nLockTime = chainActive.Tip()->GetMedianTimePast()+1;
    hash = tx2.GetHash();
    mempool.addUnchecked(hash, entry.Time(GetTime()).SpendsCoinbase(true).FromTx(tx2));
    EXPECT_TRUE(!CheckFinalTx(tx2, LOCKTIME_MEDIAN_TIME_PAST));

    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));

    // Neither tx should have made it into the template.
    EXPECT_EQ(pblocktemplate->block.vtx.size(), 1);
    delete pblocktemplate;

    // However if we advance height and time by one, both will.
    chainActive.Tip()->nHeight = chainActive.Tip()->nHeight + 1;
    SetMockTime(chainActive.Tip()->GetMedianTimePast()+2);

    // FIXME: we should *actually* create a new block so the following test
    //        works; CheckFinalTx() isn't fooled by monkey-patching nHeight.
    //EXPECT_TRUE(CheckFinalTx(tx));
    //EXPECT_TRUE(CheckFinalTx(tx2));

    EXPECT_TRUE(pblocktemplate = CreateNewBlock(CPubKey(),scriptPubKey,-1));
    EXPECT_EQ(pblocktemplate->block.vtx.size(), 2);
    delete pblocktemplate;

    chainActive.Tip()->nHeight = chainActive.Tip()->nHeight - 1;
    SetMockTime(0);
    mempool.clear();

    for (CTransaction *tx : txFirst)
        delete tx;

    fCheckpointsEnabled = true;
    fCoinbaseEnforcedProtectionEnabled = true;
}


