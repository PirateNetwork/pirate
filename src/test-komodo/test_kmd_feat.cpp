#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>

#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "core_io.h"
#include "key_io.h"
#include "main.h"
#include "primitives/transaction.h"
#include "txmempool.h"
#include "policy/fees.h"
#include "util.h"
#include "univalue.h"

#include "komodo_defs.h"
#include "komodo_globals.h"
#include "cc/CCinclude.h"


CCriticalSection& get_cs_main(); // in main.cpp

const int komodo_interest_height = 247205+1;
const std::string testwif("Usr24VoC3h4cSfSrFiGJkWLYwmkM1VnsBiMyWZvrF6QR5ZQ6Fbuu");
const std::string testpk("034b082c5819b5bf8798a387630ad236a8e800dbce4c4e24a46f36dfddab3cbff5");
const std::string testaddr("RXTUtWXgkepi8f2ohWLL9KhtGKRjBV48hT");

const uint256 testPrevHash = uint256S("01f1fde483c591ae81bee34f3dfc26ca4d6f061bc4ca15806ae15e07befedce9");

// Fake the input of transaction testPrevHash/0
class FakeCoinsViewDB2 : public CCoinsView { // change name to FakeCoinsViewDB2 to avoid name conflict with same class name in different files (seems a bug in macos gcc)
public:
    FakeCoinsViewDB2() {}

    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        //std::cerr << __func__ << " txid=" << txid.GetHex() << std::endl;
        if (txid == testPrevHash)  {
            CTxOut txOut;
            txOut.nValue = COIN;
            txOut.scriptPubKey = GetScriptForDestination(DecodeDestination(testaddr));
            CCoins newCoins;
            newCoins.vout.resize(1);
            newCoins.vout[0] = txOut;
            newCoins.nHeight = komodo_interest_height-1;
            coins.swap(newCoins);
            return true;
        }
        return false;
    }

    bool HaveCoins(const uint256 &txid) const {
        //std::cerr << __func__ << " txid=" << txid.GetHex() << std::endl;
        if (txid == testPrevHash)  
            return true;
        return false;
    }

    uint256 GetBestBlock() const override
    {
        return bestBlockHash;
    }

    bool BatchWrite(CCoinsMap &mapCoins,
                    const uint256 &hashBlock,
                    const uint256 &hashSproutAnchor,
                    const uint256 &hashSaplingAnchor,
                    CAnchorsSproutMap &mapSproutAnchors,
                    CAnchorsSaplingMap &mapSaplingAnchors,
                    CNullifiersMap &mapSproutNullifiers,
                    CNullifiersMap &mapSaplingNullifiers) {
        return false;
    }

    bool GetStats(CCoinsStats &stats) const {
        return false;
    }

    uint256 bestBlockHash;
};

bool TestSignTx(const CKeyStore& keystore, CMutableTransaction& mtx, int32_t vini, CAmount utxovalue, const CScript scriptPubKey)
{
    CTransaction txNewConst(mtx);
    SignatureData sigdata;
    auto consensusBranchId = CurrentEpochBranchId(chainActive.Height()+1, Params().GetConsensus());
    if (ProduceSignature(TransactionSignatureCreator(&keystore, &txNewConst, vini, utxovalue, SIGHASH_ALL), scriptPubKey, sigdata, consensusBranchId) != 0) {
        UpdateTransaction(mtx, vini, sigdata);
        return true;
    } else {
        std::cerr << __func__ << " signing error for vini=" << vini << " amount=" << utxovalue << std::endl;
        return false;
    }
}


class KomodoFeatures : public ::testing::Test {
protected:
    virtual void SetUp() {
        SelectParams(CBaseChainParams::MAIN);
        fPrintToConsole = true; // TODO save and restore 

        chainName = assetchain(); // ensure KMD

        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
        KOMODO_REWIND = 0;
        chainActive.SetTip(nullptr);
    }

    virtual void TearDown() {
        // Revert to test default. No-op on mainnet params.
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }
};

// some komodo consensus extensions
TEST_F(KomodoFeatures, Interest) {

    // Add a fake transaction to the wallet
    CMutableTransaction mtx0 = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_interest_height-1);
    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(testaddr));
    mtx0.vout.push_back(CTxOut(COIN, scriptPubKey));

    // Fake-mine the transaction
    ASSERT_EQ(-1, chainActive.Height());
    CBlock block;
    //block.hashPrevBlock = chainActive.Tip()->GetBlockHash();
    block.vtx.push_back(mtx0);
    block.hashMerkleRoot = block.BuildMerkleTree();
    auto blockHash = block.GetHash();
    CBlockIndex *pfakeIndex = new CBlockIndex(block);  // TODO: change back to auto if index is not cleaned
    pfakeIndex->pprev = nullptr;
    pfakeIndex->nHeight = komodo_interest_height-1;
    pfakeIndex->nTime = 1663755146;
    mapBlockIndex.insert(std::make_pair(blockHash, pfakeIndex));
    chainActive.SetTip(pfakeIndex);
    EXPECT_TRUE(chainActive.Contains(pfakeIndex));
    EXPECT_EQ(komodo_interest_height-1, chainActive.Height());

    //std::cerr << " mtx0.GetHash()=" << mtx0.GetHash().GetHex() << std::endl;
    EXPECT_EQ(mtx0.GetHash(), testPrevHash);

    FakeCoinsViewDB2 fakedb;
    fakedb.bestBlockHash = blockHash;
    CCoinsViewCache fakeview(&fakedb);
    pcoinsTip = &fakeview;

    CTxMemPool pool(::minRelayTxFee);
    bool missingInputs;
    CMutableTransaction mtxSpend = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_interest_height);
    mtxSpend.vin.push_back(CTxIn(mtx0.GetHash(), 0));
    CScript scriptPubKey1 = GetScriptForDestination(DecodeDestination(testaddr));
    mtxSpend.vout.push_back(CTxOut(COIN, scriptPubKey1));

    CBasicKeyStore tempKeystore;
    CKey key = DecodeSecret(testwif);
    tempKeystore.AddKey(key);

    // create coinbase
    CMutableTransaction txcb = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_interest_height);
    txcb.vin.resize(1);
    txcb.vin[0].prevout.SetNull();
    txcb.vin[0].scriptSig = (CScript() << komodo_interest_height << CScriptNum(1)) + COINBASE_FLAGS;
    txcb.vout.resize(1);
    txcb.vout[0].scriptPubKey = GetScriptForDestination(DecodeDestination(testaddr));;
    txcb.vout[0].nValue = GetBlockSubsidy(komodo_interest_height, Params().GetConsensus()) + 0;
    txcb.nExpiryHeight = 0;
    txcb.nLockTime = pfakeIndex->GetMedianTimePast()+1;

    {
        // check invalid interest in mempool

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() + 777 - KOMODO_MAXMEMPOOLTIME - 1; // too long time to add into mempool (prevent incorrect interest)
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));
        //std::cerr << __func__ << " vin0=" << mtxSpend.vin[0].scriptSig.ToString() << std::endl;

        CValidationState state1;
        CTransaction tx1(mtxSpend);

        LOCK( get_cs_main() );
        EXPECT_FALSE(AcceptToMemoryPool(pool, state1, tx1, false, &missingInputs));
        //std::cerr << __func__ << " state1.GetRejectReason()=" << state1.GetRejectReason() << " missingInputs=" << missingInputs << std::endl;
        EXPECT_EQ(state1.GetRejectReason(), "komodo-interest-invalid");
    }
    {
        // check invalid interest in block

        CBlock block;
        block.vtx.push_back(txcb);

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() - KOMODO_MAXMEMPOOLTIME - 1; // too long time staying in mempool (a bit longer than when adding )
        mtxSpend.vin[0].scriptSig.clear();
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));
        CTransaction tx1(mtxSpend);
        block.vtx.push_back(tx1);
        
        block.nTime = pfakeIndex->GetMedianTimePast();

        CValidationState state1;
        EXPECT_FALSE(ContextualCheckBlock(false, block, state1, pfakeIndex));
        EXPECT_EQ(state1.GetRejectReason(), "komodo-interest-invalid");
    }
    {
        // check valid interest in mempool

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() + 777 - KOMODO_MAXMEMPOOLTIME; // not too long time to add into mempool
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));

        CValidationState state1;
        CTransaction tx1(mtxSpend);

        LOCK( get_cs_main() );
        EXPECT_TRUE(AcceptToMemoryPool(pool, state1, tx1, false, &missingInputs));
    }

    {
        // check valid interest in block

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() - KOMODO_MAXMEMPOOLTIME; // not too long time in mempool
        mtxSpend.vin[0].scriptSig.clear();
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));

        CBlock block;
        block.vtx.push_back(txcb);
        CTransaction tx1(mtxSpend);
        block.vtx.push_back(tx1);
        block.nTime = pfakeIndex->GetMedianTimePast();

        CValidationState state1;
        EXPECT_TRUE(ContextualCheckBlock(false, block, state1, pfakeIndex));
        //std::cerr << __func__ << " state1.GetRejectReason()=" << state1.GetRejectReason() << std::endl;
    }
}

