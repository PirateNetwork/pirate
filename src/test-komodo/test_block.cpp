#include "primitives/block.h"
#include "testutils.h"
#include "komodo_extern_globals.h"
#include "consensus/validation.h"
#include "miner.h"

#include <gtest/gtest.h>


TEST(block_tests, header_size_is_expected) {
    // Header with an empty Equihash solution.
    CBlockHeader header;
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << header;

    auto stream_size = CBlockHeader::HEADER_SIZE + 1;
    // ss.size is +1 due to data stream header of 1 byte
    EXPECT_EQ(ss.size(), stream_size);
}

TEST(block_tests, TestStopAt)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    lastBlock = chain.generateBlock(); // now we should be above 1
    ASSERT_GT( chain.GetIndex()->GetHeight(), 1);
    CBlock block;
    CValidationState state;
    KOMODO_STOPAT = 1;
    EXPECT_FALSE( ConnectBlock(block, state, chain.GetIndex(), *chain.GetCoinsViewCache(), false, true) );
    KOMODO_STOPAT = 0; // to not stop other tests
}

TEST(block_tests, TestConnectWithoutChecks)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Add some transaction to a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    CTransaction fundAlice = notary->CreateSpendTransaction(alice, 100000);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual tx
    block.vtx.push_back(fundAlice);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto view = chain.GetCoinsViewCache();
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_TRUE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    if (!state.IsValid() )
        FAIL() << state.GetRejectReason();
}

TEST(block_tests, TestSpendInSameBlock)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Start to build a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    CTransaction fundAlice = notary->CreateSpendTransaction(alice, 100000);
    // now have Alice move some funds to Bob in the same block
    CMutableTransaction aliceToBobMutable;
    CTxIn aliceIn;
    aliceIn.prevout.hash = fundAlice.GetHash();
    aliceIn.prevout.n = 0;
    aliceToBobMutable.vin.push_back(aliceIn);
    CTxOut bobOut;
    bobOut.scriptPubKey = GetScriptForDestination(bob->GetPubKey());
    bobOut.nValue = 10000;
    aliceToBobMutable.vout.push_back(bobOut);
    CTxOut aliceRemainder;
    aliceRemainder.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder.nValue = fundAlice.vout[0].nValue - 10000;
    aliceToBobMutable.vout.push_back(aliceRemainder);
    uint256 hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToBobMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToBobMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToBobTx(aliceToBobMutable);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual txs
    block.vtx.push_back(fundAlice);
    block.vtx.push_back(aliceToBobTx);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_TRUE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    if (!state.IsValid() )
        FAIL() << state.GetRejectReason();
}

TEST(block_tests, TestDoubleSpendInSameBlock)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    auto charlie = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    ASSERT_GT( chain.GetIndex()->GetHeight(), 0 );
    // Start to build a block
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    CTransaction fundAlice = notary->CreateSpendTransaction(alice, 100000);
    // now have Alice move some funds to Bob in the same block
    CMutableTransaction aliceToBobMutable;
    CTxIn aliceIn;
    aliceIn.prevout.hash = fundAlice.GetHash();
    aliceIn.prevout.n = 0;
    aliceToBobMutable.vin.push_back(aliceIn);
    CTxOut bobOut;
    bobOut.scriptPubKey = GetScriptForDestination(bob->GetPubKey());
    bobOut.nValue = 10000;
    aliceToBobMutable.vout.push_back(bobOut);
    CTxOut aliceRemainder;
    aliceRemainder.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder.nValue = fundAlice.vout[0].nValue - 10000;
    aliceToBobMutable.vout.push_back(aliceRemainder);
    uint256 hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToBobMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToBobMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToBobTx(aliceToBobMutable);
    // alice attempts to double spend the vout and send something to charlie
    CMutableTransaction aliceToCharlieMutable;
    CTxIn aliceIn2;
    aliceIn2.prevout.hash = fundAlice.GetHash();
    aliceIn2.prevout.n = 0;
    aliceToCharlieMutable.vin.push_back(aliceIn2);
    CTxOut charlieOut;
    charlieOut.scriptPubKey = GetScriptForDestination(charlie->GetPubKey());
    charlieOut.nValue = 10000;
    aliceToCharlieMutable.vout.push_back(charlieOut);
    CTxOut aliceRemainder2;
    aliceRemainder2.scriptPubKey = GetScriptForDestination(alice->GetPubKey());
    aliceRemainder2.nValue = fundAlice.vout[0].nValue - 10000;
    aliceToCharlieMutable.vout.push_back(aliceRemainder2);
    hash = SignatureHash(fundAlice.vout[0].scriptPubKey, aliceToCharlieMutable, 0, SIGHASH_ALL, 0, 0);
    aliceToCharlieMutable.vin[0].scriptSig << alice->Sign(hash, SIGHASH_ALL);
    CTransaction aliceToCharlieTx(aliceToCharlieMutable);
    // construct the block
    CBlock block;
    // first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // then the actual txs
    block.vtx.push_back(fundAlice);
    block.vtx.push_back(aliceToBobTx);
    block.vtx.push_back(aliceToCharlieTx);
    CValidationState state;
    // create a new CBlockIndex to forward to ConnectBlock
    auto index = chain.GetIndex();
    CBlockIndex newIndex;
    newIndex.pprev = index;
    EXPECT_FALSE( ConnectBlock(block, state, &newIndex, *chain.GetCoinsViewCache(), true, false) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txns-inputs-missingorspent");
}

bool CalcPoW(CBlock *pblock);

TEST(block_tests, TestProcessBlock)
{
    TestChain chain;
    EXPECT_EQ(chain.GetIndex()->GetHeight(), 0);
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    auto charlie = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // gives notary everything
    EXPECT_EQ(chain.GetIndex()->GetHeight(), 1);
    chain.IncrementChainTime();
    auto notaryPrevOut = notary->GetAvailable(100000);
    // add a transaction to the mempool
    CTransaction fundAlice = notary->CreateSpendTransaction(alice, 100000);
    EXPECT_TRUE( chain.acceptTx(fundAlice).IsValid() );
    // construct the block
    CBlock block;
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    CValidationState state;
    // no transactions
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-blk-length");
    EXPECT_EQ(chain.GetIndex()->GetHeight(), 1);
    // add first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // no PoW, no merkle root should fail on merkle error
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txnmrklroot");
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
    // finish constructing the block
    block.nBits = GetNextWorkRequired( chain.GetIndex(), &block, Params().GetConsensus());
    block.nTime = GetTime();
    block.hashPrevBlock = lastBlock.GetHash();
    block.hashMerkleRoot = block.BuildMerkleTree();
    // Add the PoW
    EXPECT_TRUE(CalcPoW(&block));
    state = CValidationState();
    EXPECT_TRUE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    if (!state.IsValid())
        FAIL() << state.GetRejectReason();
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
}

TEST(block_tests, TestProcessBadBlock)
{
    TestChain chain;
    auto notary = chain.AddWallet(chain.getNotaryKey());
    auto alice = chain.AddWallet();
    auto bob = chain.AddWallet();
    auto charlie = chain.AddWallet();
    CBlock lastBlock = chain.generateBlock(); // genesis block
    auto notaryPrevOut = notary->GetAvailable(100000);
    // add a transaction to the mempool
    CTransaction fundAlice = notary->CreateSpendTransaction(alice, 100000);
    EXPECT_TRUE( chain.acceptTx(fundAlice).IsValid() );
    // construct the block
    CBlock block;
    int32_t newHeight = chain.GetIndex()->GetHeight() + 1;
    CValidationState state;
    // no transactions
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-blk-length");
    // add first a coinbase tx
    auto consensusParams = Params().GetConsensus();
    CMutableTransaction txNew = CreateNewContextualCMutableTransaction(consensusParams, newHeight);
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vin[0].scriptSig = (CScript() << newHeight << CScriptNum(1)) + COINBASE_FLAGS;
    txNew.vout.resize(1);
    txNew.vout[0].nValue = GetBlockSubsidy(newHeight,consensusParams);
    txNew.nExpiryHeight = 0;
    block.vtx.push_back(CTransaction(txNew));
    // Add no PoW, should fail on merkle error
    EXPECT_FALSE( ProcessNewBlock(false, newHeight, state, nullptr, &block, false, nullptr) );
    EXPECT_EQ(state.GetRejectReason(), "bad-txnmrklroot");
    // Verify transaction is still in mempool
    EXPECT_EQ(mempool.size(), 1);
}