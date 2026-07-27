// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cryptoconditions.h>
#include <gtest/gtest.h>

#include "cc/eval.h"
#include "importcoin.h"
#include "base58.h"
#include "core_io.h"
#include "key.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/cc.h"
#include "script/interpreter.h"
#include "script/serverchecker.h"
#include "txmempool.h"

#include "gtest/gtestutils.h"

// Tests for importcoin / cross-chain coin import, exercised through the
// CryptoConditions eval path. testProcessImportThroughPipeline and
// testImportTombstone remain DISABLED_ - see their comments - because they
// require a genuinely valid crosschain proof (real CheckMoMoM/
// CheckNotariesApproval data), which this synthetic-burn-tx fixture can't
// produce.

extern Eval* EVAL_TEST;

std::shared_ptr<TestChain> testChain;

namespace TestCoinImport {


static uint8_t testNum = 0;

class TestCoinImport : public ::testing::Test, public Eval {
public:
    CMutableTransaction burnTx; std::vector<uint8_t> rawproof;
    std::vector<CTxOut> payouts;
    TxProof proof;
    uint256 MoMoM;
    CMutableTransaction importTx;
    // cc/import.cpp rejects any targetCcid < KOMODO_FIRSTFUNGIBLEID (100) with
    // "chain-not-fungible" before reaching any of the checks these tests
    // actually mean to exercise - must be >= 100 for the burn tx embedding
    // this value to be considered a valid import candidate at all.
    uint32_t testCcid = 100;
    std::string testSymbol = "PIZZA";
    CAmount amount = 100;

    void SetImportTx() {
        burnTx.vout.resize(0);
        burnTx.vout.push_back(MakeBurnOutput(amount, testCcid, testSymbol, payouts,rawproof));
        importTx = CMutableTransaction(MakeImportCoinTransaction(proof, CTransaction(burnTx), payouts));
        MoMoM = burnTx.GetHash();  // TODO: an actual branch
    }

    uint32_t GetAssetchainsCC() const { return testCcid; }
    std::string GetAssetchainsSymbol() const { return testSymbol; }

    bool GetProofRoot(uint256 hash, uint256 &momom) const
    {
        if (MoMoM.IsNull()) return false;
        momom = MoMoM;
        return true;
    }


protected:
    static void SetUpTestCase() { testChain = std::make_shared<TestChain>(); }
    static void TearDownTestCase() { testChain = nullptr;   };

    virtual void SetUp() {
        ASSETCHAINS_CC = 1;
        EVAL_TEST = this;

        std::vector<uint8_t> fakepk;
        fakepk.resize(33);
        fakepk.begin()[0] = testNum++;
        payouts.push_back(CTxOut(amount, CScript() << fakepk << OP_CHECKSIG));
        SetImportTx();
    }


    void TestRunCCEval(CMutableTransaction mtx)
    {
        CTransaction importTx(mtx);
        std::vector<CTxOut> allPrevOutputs;
        PrecomputedTransactionData txdata(importTx, allPrevOutputs);
        ServerTransactionSignatureChecker checker(&importTx, 0, 0, false, txdata);
        CValidationState verifystate;
        if (!VerifyCoinImport(importTx.vin[0].scriptSig, checker, verifystate))
            printf("TestRunCCEval: %s\n", verifystate.GetRejectReason().data());
    }
};


// Requires a genuinely valid crosschain proof to reach the mempool at all:
// cc/import.cpp's CheckMigration takes the default (MerkleBranch-kind)
// ImportProof this fixture builds and checks it via CrossChain::CheckMoMoM
// against real notarized chain data, which a synthetic unit-test burn tx
// can't satisfy - it now fails "momom-check-fail" before any of this test's
// assertions become reachable, and segfaults if force-run (chain-state
// corruption from the InvalidateBlock/acceptTx sequence on top of a rejected
// tx). The fixture's GetProofRoot()/MoMoM member this test relies on for its
// premise is unreferenced dead code - see the file-level comment above.
TEST_F(TestCoinImport, DISABLED_testProcessImportThroughPipeline)
{
    CValidationState mainstate;
    CTransaction tx(importTx);

    // first should work
    acceptTxFail(tx);

    // should fail in mempool
    ASSERT_FALSE(acceptTx(tx, mainstate));
    EXPECT_EQ("already in mempool", mainstate.GetRejectReason());

    // should be in persisted UTXO set
    generateBlock();
    ASSERT_FALSE(acceptTx(tx, mainstate));
    EXPECT_EQ("already have coins", mainstate.GetRejectReason());
    ASSERT_TRUE(pcoinsTip->HaveCoins(tx.GetHash()));

    // Now disconnect the block
    CValidationState invalstate;
    if (!InvalidateBlock(invalstate, chainActive.Tip())) {
        FAIL() << invalstate.GetRejectReason();
    }
    ASSERT_FALSE(pcoinsTip->HaveCoins(tx.GetHash()));

    // should be back in mempool
    ASSERT_FALSE(acceptTx(tx, mainstate));
    EXPECT_EQ("already in mempool", mainstate.GetRejectReason());
}


// Same root cause as DISABLED_testProcessImportThroughPipeline above: needs
// a real crosschain proof to ever reach the mempool/tombstone logic.
TEST_F(TestCoinImport, DISABLED_testImportTombstone)
{
    CValidationState mainstate;
    // By setting an unspendable output, there will be no addition to UTXO
    // Nonetheless, we dont want to be able to import twice
    payouts[0].scriptPubKey = CScript() << OP_RETURN;
    SetImportTx();
    MoMoM = burnTx.GetHash();  // TODO: an actual branch
    CTransaction tx(importTx);

    // first should work
    acceptTxFail(tx);

    // should be in persisted UTXO set
    generateBlock();
    ASSERT_FALSE(acceptTx(tx, mainstate));
    EXPECT_EQ("import tombstone exists", mainstate.GetRejectReason());
    ASSERT_TRUE(pcoinsTip->HaveCoins(burnTx.GetHash()));

    // Now disconnect the block
    CValidationState invalstate;
    if (!InvalidateBlock(invalstate, chainActive.Tip())) {
        FAIL() << invalstate.GetRejectReason();
    }
    // Tombstone should be gone from utxo set
    ASSERT_FALSE(pcoinsTip->HaveCoins(burnTx.GetHash()));

    // should be back in mempool
    ASSERT_FALSE(acceptTx(tx, mainstate));
    EXPECT_EQ("already in mempool", mainstate.GetRejectReason());
}


TEST_F(TestCoinImport, testNoVouts)
{
    importTx.vout.resize(0);
    TestRunCCEval(importTx);
    EXPECT_EQ("too-few-vouts", state.GetRejectReason());
}


TEST_F(TestCoinImport, testInvalidParams)
{
    std::vector<uint8_t> payload = E_MARSHAL(ss << EVAL_IMPORTCOIN; ss << 'a');
    importTx.vin[0].scriptSig = CScript() << payload;
    TestRunCCEval(importTx);
    EXPECT_EQ("invalid-params", state.GetRejectReason());
}


TEST_F(TestCoinImport, testNonCanonical)
{
    importTx.nLockTime = 10;
    TestRunCCEval(importTx);
    EXPECT_EQ("non-canonical", state.GetRejectReason());
}


TEST_F(TestCoinImport, testInvalidBurnOutputs)
{
    burnTx.vout.resize(0);
    MoMoM = burnTx.GetHash();  // TODO: an actual branch
    CTransaction tx = MakeImportCoinTransaction(proof, CTransaction(burnTx), payouts);
    TestRunCCEval(tx);
    EXPECT_EQ("invalid-burn-tx", state.GetRejectReason());
}


TEST_F(TestCoinImport, testInvalidBurnParams)
{
    burnTx.vout.back().scriptPubKey = CScript() << OP_RETURN << E_MARSHAL(ss << VARINT(testCcid));
    MoMoM = burnTx.GetHash();  // TODO: an actual branch
    CTransaction tx = MakeImportCoinTransaction(proof, CTransaction(burnTx), payouts);
    TestRunCCEval(tx);
    EXPECT_EQ("invalid-burn-tx", state.GetRejectReason());
}


TEST_F(TestCoinImport, testWrongChainId)
{
    testCcid = 0;
    TestRunCCEval(importTx);
    EXPECT_EQ("importcoin-wrong-chain", state.GetRejectReason());
}


TEST_F(TestCoinImport, testInvalidBurnAmount)
{
    burnTx.vout.back().nValue = 0;
    MoMoM = burnTx.GetHash();  // TODO: an actual branch
    CTransaction tx = MakeImportCoinTransaction(proof, CTransaction(burnTx), payouts);
    TestRunCCEval(tx);
    EXPECT_EQ("invalid-burn-amount", state.GetRejectReason());
}


TEST_F(TestCoinImport, testPayoutTooHigh)
{
    // vout[0] is the payout (vout[1] is the opret, always tail); raising it
    // above burnAmount (100) trips cc/import.cpp's totalOut>burnAmount check.
    // (This and testAmountInOpret had their vout indices swapped - mutating
    // vout[1], the opret, always makes MakeImportCoinTransaction's
    // reconstruction (which hardcodes the opret's own nValue to 0) disagree
    // with the given tx, so it was really testing "non-canonical" instead.)
    importTx.vout[0].nValue = 101;
    TestRunCCEval(importTx);
    EXPECT_EQ("payout-too-high-or-too-low", state.GetRejectReason());
}


TEST_F(TestCoinImport, testAmountInOpret)
{
    // vout[1] is the opret CTxOut(0, OP_RETURN ...) appended by
    // MakeImportCoinTransaction; giving it a nonzero value makes the
    // reconstructed tx (which always uses nValue=0 for this vout) disagree
    // with the given one, i.e. exactly "an amount in the opret" -> non-canonical.
    importTx.vout[1].nValue = 1;
    TestRunCCEval(importTx);
    EXPECT_EQ("non-canonical", state.GetRejectReason());
}



TEST_F(TestCoinImport, testInvalidPayouts)
{
    // Insert an extra, zero-value payout entry *before* the opret tail (not
    // append after it - that made vout.back() no longer a valid opret, so
    // UnmarshalImportTx failed structurally with "invalid-params" instead of
    // ever reaching the payouts-hash check). Zero value keeps totalOut
    // unchanged (avoids tripping payout-too-high-or-too-low first), while the
    // extra entry makes the reconstructed payouts hash disagree with what's
    // embedded in the burn tx.
    importTx.vout.insert(importTx.vout.end() - 1, CTxOut(0, importTx.vout[0].scriptPubKey));
    TestRunCCEval(importTx);
    EXPECT_EQ("wrong-payouts", state.GetRejectReason());
}


// testCouldntLoadMomom (expected reject reason "coudnt-load-momom") deleted:
// that string doesn't exist anywhere in production code. It - and this
// fixture's GetProofRoot()/MoMoM member - are leftovers from an older
// GetProofRoot-callback-based crosschain-proof design; the current
// cc/import.cpp validates crosschain proofs via ImportProof's
// MerkleBranch/NotaryTxids variants and CrossChain::CheckMoMoM /
// CheckNotariesApproval instead, neither of which GetProofRoot participates
// in (grep confirms GetProofRoot is unreferenced outside this test file).

TEST_F(TestCoinImport, testMomomCheckFail)
{
    // The fixture's `proof` member is a default TxProof, which ImportProof's
    // implicit-conversion constructor always turns into a MerkleBranch-kind
    // proof (see importcoin.h) - so this always takes the CheckMoMoM path in
    // cc/import.cpp regardless of the (vestigial) MoMoM member above, and
    // fails it since there's no real crosschain notarization backing a
    // synthetic unit-test burn tx.
    TestRunCCEval(importTx);
    EXPECT_EQ("momom-check-fail", state.GetRejectReason());
}


TEST_F(TestCoinImport, testGetCoinImportValue)
{
    ASSERT_EQ(100, GetCoinImportValue(importTx));
}

} /* namespace TestCoinImport */
