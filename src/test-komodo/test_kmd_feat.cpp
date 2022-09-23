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
#include "komodo_interest.h"
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
        fPrintToConsoleOld = fPrintToConsole;
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
        fPrintToConsole = fPrintToConsoleOld;
    }
    bool fPrintToConsoleOld;
};

// some komodo consensus extensions
TEST_F(KomodoFeatures, komodo_interest_validate) {

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

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() + 777 - 3600 - 1; // too long time to add into mempool (prevent incorrect interest)
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));

        CValidationState state1;
        CTransaction tx1(mtxSpend);

        LOCK( get_cs_main() );
        EXPECT_FALSE(AcceptToMemoryPool(pool, state1, tx1, false, &missingInputs));
        EXPECT_EQ(state1.GetRejectReason(), "komodo-interest-invalid");
    }
    {
        // check invalid interest in block

        CBlock block;
        block.vtx.push_back(txcb);

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() - 3600 - 1; // too long time staying in mempool (a bit longer than when adding )
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

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() + 777 - 3600; // not too long time to add into mempool
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));

        CValidationState state1;
        CTransaction tx1(mtxSpend);

        LOCK( get_cs_main() );
        EXPECT_TRUE(AcceptToMemoryPool(pool, state1, tx1, false, &missingInputs));
    }

    {
        // check valid interest in block

        mtxSpend.nLockTime = chainActive.Tip()->GetMedianTimePast() - 3600; // not too long time in mempool
        mtxSpend.vin[0].scriptSig.clear();
        EXPECT_TRUE(TestSignTx(tempKeystore, mtxSpend, 0, mtx0.vout[0].nValue, mtx0.vout[0].scriptPubKey));

        CBlock block;
        block.vtx.push_back(txcb);
        CTransaction tx1(mtxSpend);
        block.vtx.push_back(tx1);
        block.nTime = pfakeIndex->GetMedianTimePast();

        CValidationState state1;
        EXPECT_TRUE(ContextualCheckBlock(false, block, state1, pfakeIndex));
    }
}

// check komodo_interestnew calculations
TEST_F(KomodoFeatures, komodo_interestnew) {

    // some not working values
    EXPECT_EQ(komodo_interestnew(1, 1000LL, 1, 1), 0LL); 
    // time lower than cut off month time limit
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600 /*KOMODO_MAXMEMPOOLTIME*/), 10LL*COIN/10512000 * (31*24*60 - 59)); 

    // end of interest era
    EXPECT_EQ(komodo_interestnew(7777777-1, 10LL*COIN, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), 10LL*COIN/10512000 * (31*24*60 - 59)); 
    EXPECT_EQ(komodo_interestnew(7777777, 10LL*COIN, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), 0LL); 

    // value less than limit
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN-1, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), 0); 
    // tip less than nLockTime
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN-1, 1663839248, 1663839248 - 1), 0); 
    // not timestamp value
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN-1, 400000000U, 400000000U + 30 * 24 * 60 * 60 + 3600), 0); 

    // too small period
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN, 1663839248, 1663839248 + 3600 - 1), 0); 
    // time over cut off month time limit
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN, 1663839248, 1663839248 + 31 * 24 * 60 * 60 + 3600+1), 10LL*COIN/10512000 * (31*24*60 - 59)); 
    EXPECT_EQ(komodo_interestnew(1000000, 10LL*COIN, 1663839248, 1663839248 + 32 * 24 * 60 * 60 + 3600), 10LL*COIN/10512000 * (31*24*60 - 59)); 

    // time close to cut off year time limit 
    EXPECT_EQ(komodo_interestnew(1000000-1, 10LL*COIN, 1663839248, 1663839248 + (365 * 24 * 60 - 1) * 60 + 3600), 10LL*COIN/10512000 * (365*24*60 - 59)); 
    // time over cut off year time limit 
    EXPECT_EQ(komodo_interestnew(1000000-1, 10LL*COIN, 1663839248, 1663839248 + (365 * 24 * 60 - 1) * 60 + 3600 + 60), 10LL*COIN/10512000 * (365*24*60 - 59)); 
    EXPECT_EQ(komodo_interestnew(1000000-1, 10LL*COIN, 1663839248, 1663839248 + (365 * 24 * 60 - 1) * 60 + 3600 + 30 * 24 * 60), 10LL*COIN/10512000 * (365*24*60 - 59)); 
}

// check komodo_interest calculations
TEST_F(KomodoFeatures, komodo_interest) {

    const uint32_t activation = 1491350400;  // 1491350400 5th April

    {
        // some not working values should produce 0LL
        EXPECT_EQ(komodo_interest(1, 1000LL, 1, 1), 0LL); 
    }
    {
        // nValue <= 25000LL*COIN and nValue >= 25000LL*COIN
        // txheight >= 1000000 
        // should be routed to komodo_interestnew

        for (CAmount nValue : { 10LL*COIN, 25001LL*COIN })
        {
            // time lower than cut off month time limit
            EXPECT_EQ(komodo_interest(1000000, nValue, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), nValue/10512000 * (31*24*60 - 59)); 

            // end of interest era
            EXPECT_EQ(komodo_interest(7777777-1, nValue, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), nValue/10512000 * (31*24*60 - 59)); 
            EXPECT_EQ(komodo_interest(7777777 /*KOMODO_ENDOFERA*/, nValue, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), 0LL); 

            // tip less than nLockTime
            EXPECT_EQ(komodo_interest(1000000, nValue-1, 1663839248, 1663839248 - 1), 0); 
            // not timestamp value
            EXPECT_EQ(komodo_interest(1000000, nValue-1, 400000000U, 400000000U + 30 * 24 * 60 * 60 + 3600), 0); 

            // too small period
            EXPECT_EQ(komodo_interest(1000000, nValue, 1663839248, 1663839248 + 3600 - 1), 0); 
            // time over cut off month time limit
            EXPECT_EQ(komodo_interest(1000000, nValue, 1663839248, 1663839248 + 31 * 24 * 60 * 60 + 3600+1), nValue/10512000 * (31*24*60 - 59)); 
            EXPECT_EQ(komodo_interest(1000000, nValue, 1663839248, 1663839248 + 32 * 24 * 60 * 60 + 3600), nValue/10512000 * (31*24*60 - 59)); 
        }
        // value less than limit
        EXPECT_EQ(komodo_interest(1000000, 10LL*COIN-1, 1663839248, 1663839248 + (31 * 24 * 60 - 1) * 60 + 3600), 0); 
    }

    for (auto days : { 1, 10, 365, 365*2, 365*3 })
    {
        std::cerr << "running komodo_interest test for days=" << days << "..." << std::endl;
        int32_t minutes = days * 24 * 60;
        if (minutes > 365 * 24 * 60)
            minutes = 365 * 24 * 60;
        {
            // nValue <= 25000LL*COIN
            // txheight < 1000000 

            uint64_t numerator = (10LL*COIN / 20); // assumes 5%!
            EXPECT_EQ(komodo_interest(1000000-1, 10LL*COIN, 1663839248, 1663839248 + minutes * 60), numerator * (minutes - 59) / (365ULL * 24 * 60)); 
        }
        {
            // nValue <= 25000LL*COIN
            // txheight < 250000 

            uint64_t numerator = (10LL*COIN * 5000000 /*KOMODO_INTEREST*/);
            uint32_t locktime = activation - 2 * days * 24 * 60 * 60;
            uint32_t tiptime = locktime + minutes * 60;
            ASSERT_TRUE(tiptime < activation);
            uint64_t denominator = (365LL * 24 * 60) / minutes;
            denominator = (denominator == 0LL) ? 1LL : denominator;
            EXPECT_EQ(komodo_interest(250000-1, 10LL*COIN, locktime, tiptime), numerator / denominator / COIN); 
        }
        {
            // !exception
            // nValue > 25000LL*COIN
            // txheight < 250000 

            uint64_t numerator = (25000LL*COIN+1) / 20; // assumes 5%!
            uint64_t denominator = (365LL * 24 * 60) / minutes; // no minutes-59 adjustment
            denominator = (denominator == 0LL) ? 1LL : denominator;
            EXPECT_EQ(komodo_interest(250000-1, 25000LL*COIN+1, 1663839248, 1663839248 + minutes * 60), numerator / denominator); 
        }
        {
            // !exception
            // nValue > 25000LL*COIN
            // txheight < 1000000 

            uint64_t numerator = (25000LL*COIN+1) / 20; // assumes 5%!
            int32_t minutes_adj = minutes - 59; // adjusted since ht=250000
            EXPECT_EQ(komodo_interest(1000000-1, 25000LL*COIN+1, 1663839248, 1663839248 + minutes * 60), numerator * minutes_adj / (365LL * 24 * 60)); 
        }
        {
            // exception
            // nValue > 25000LL*COIN
            // txheight < 1000000 

            for (const auto htval : std::vector<std::pair<int32_t, CAmount>>{ {116607, 2502721100000LL}, {126891, 2879650000000LL}, {129510, 3000000000000LL}, {141549, 3500000000000LL}, {154473, 3983399350000LL}, {154736, 3983406748175LL}, {155013, 3983414006565LL}, {155492, 3983427592291LL}, {155613, 9997409999999797LL}, {157927, 9997410667451072LL}, {155613, 2590000000000LL}, {155949, 4000000000000LL} })
            {
                int32_t txheight = htval.first;
                CAmount nValue = htval.second;
                uint64_t numerator = (static_cast<uint64_t>(nValue) * 5000000 /*KOMODO_INTEREST*/);  // NOTE: uint64_t (for CAmount it is an overflow here for some exceptions)
                uint32_t locktime = 1484490069; // close to real tx locktime
                // uint32_t locktime = 1663839248;
                uint32_t tiptime = locktime + minutes * 60;
                // uint32_t tiptime = 1663920715; // test writing time
                // int32_t minutes = (tiptime - locktime) / 60;
                uint64_t denominator = (365LL * 24 * 60) / minutes;
                denominator = (denominator == 0LL) ? 1LL : denominator;
                if (txheight < 155949)
                    EXPECT_EQ(komodo_interest(txheight, nValue, locktime, tiptime), numerator / denominator / COIN); 
            }
        }
    }
}