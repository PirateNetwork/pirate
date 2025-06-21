#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <sodium.h>

#include "main.h"
#include "primitives/transaction.h"
#include "consensus/validation.h"
#include "gtest/gtestutils.h"

extern ZCJoinSplit* params;

// Subclass of CTransaction which doesn't call UpdateHash when constructing
// from a CMutableTransaction.  This enables us to create a CTransaction
// with bad values which normally trigger an exception during construction.
class UNSAFE_CTransaction : public CTransaction {
    public:
        UNSAFE_CTransaction(const CMutableTransaction &tx) : CTransaction(tx, true) {}
};

TEST(ChecktransactionTests, CheckVpubNotBothNonzero) {
    CMutableTransaction tx;
    tx.nVersion = 2;

    {
        // Ensure that values within the joinsplit are well-formed.
        CMutableTransaction newTx(tx);
        CValidationState state;

        newTx.vjoinsplit.push_back(JSDescription());

        JSDescription *jsdesc = &newTx.vjoinsplit[0];
        jsdesc->vpub_old = 1;
        jsdesc->vpub_new = 1;

        EXPECT_FALSE(CheckTransactionWithoutProofVerification(0,newTx, state));
        EXPECT_EQ(state.GetRejectReason(), "bad-txns-vpubs-both-nonzero");
    }
}

class MockCValidationState : public CValidationState {
public:
    MOCK_METHOD5(DoS, bool(int level, bool ret,
             unsigned char chRejectCodeIn, std::string strRejectReasonIn,
             bool corruptionIn));
    MOCK_METHOD3(Invalid, bool(bool ret,
                 unsigned char _chRejectCode, std::string _strRejectReason));
    MOCK_METHOD1(Error, bool(std::string strRejectReasonIn));
    MOCK_CONST_METHOD0(IsValid, bool());
    MOCK_CONST_METHOD0(IsInvalid, bool());
    MOCK_CONST_METHOD0(IsError, bool());
    MOCK_CONST_METHOD1(IsInvalid, bool(int &nDoSOut));
    MOCK_CONST_METHOD0(CorruptionPossible, bool());
    MOCK_CONST_METHOD0(GetRejectCode, unsigned char());
    MOCK_CONST_METHOD0(GetRejectReason, std::string());
};

void CreateJoinSplitSignature(CMutableTransaction& mtx, uint32_t consensusBranchId);

CMutableTransaction GetValidTransaction(uint32_t consensusBranchId=SPROUT_BRANCH_ID) {

    CMutableTransaction mtx;
    if (consensusBranchId == NetworkUpgradeInfo[Consensus::UPGRADE_OVERWINTER].nBranchId) {
        mtx.fOverwintered = true;
        mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
        mtx.nVersion = OVERWINTER_TX_VERSION;
    } else if (consensusBranchId == NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId) {
        mtx.fOverwintered = true;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nVersion = SAPLING_TX_VERSION;
    } else if (consensusBranchId == NetworkUpgradeInfo[Consensus::UPGRADE_ORCHARD].nBranchId) {
        mtx.fOverwintered = true;
        mtx.nVersionGroupId = ORCHARD_VERSION_GROUP_ID;
        mtx.nVersion = ORCHARD_TX_VERSION;
    } else if (consensusBranchId != SPROUT_BRANCH_ID) {
        // Unsupported consensus branch ID
        assert(false);
    }


    mtx.vin.resize(2);
    mtx.vin[0].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
    mtx.vin[0].prevout.n = 0;
    mtx.vin[1].prevout.hash = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    mtx.vin[1].prevout.n = 0;
    mtx.vout.resize(2);
    // mtx.vout[0].scriptPubKey =
    mtx.vout[0].nValue = 0;
    mtx.vout[1].nValue = 0;
    mtx.vjoinsplit.resize(2);
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000002");
    mtx.vjoinsplit[1].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000003");

    if (mtx.nVersion >= SAPLING_TX_VERSION) {
        libzcash::GrothProof emptyProof;
        mtx.vjoinsplit[0].proof = emptyProof;
        mtx.vjoinsplit[1].proof = emptyProof;
    }

    CreateJoinSplitSignature(mtx, consensusBranchId);
    return mtx;
}

void CreateJoinSplitSignature(CMutableTransaction& mtx, uint32_t consensusBranchId) {
    // Generate an ephemeral keypair.
    uint256 joinSplitPubKey;
    unsigned char joinSplitPrivKey[crypto_sign_SECRETKEYBYTES];
    crypto_sign_keypair(joinSplitPubKey.begin(), joinSplitPrivKey);
    mtx.joinSplitPubKey = joinSplitPubKey;

    // Compute the correct hSig.
    // TODO: #966.
    static const uint256 one(uint256S("0000000000000000000000000000000000000000000000000000000000000001"));
    // Empty output script.
    CScript scriptCode;
    CTransaction signTx(mtx);
    std::vector<CTxOut> allPrevOutputs;
    PrecomputedTransactionData txdata(signTx, allPrevOutputs);
    uint256 dataToBeSigned = SignatureHash(scriptCode, signTx, NOT_AN_INPUT, SIGHASH_ALL, 0, consensusBranchId, txdata);
    if (dataToBeSigned == one) {
        throw std::runtime_error("SignatureHash failed");
    }

    // Add the signature
    assert(crypto_sign_detached(&mtx.joinSplitSig[0], NULL,
                         dataToBeSigned.begin(), 32,
                         joinSplitPrivKey
                        ) == 0);
}

TEST(ChecktransactionTests, valid_transaction) {
    CMutableTransaction mtx = GetValidTransaction();
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
}

TEST(ChecktransactionTests, BadVersionTooLow) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.nVersion = 0;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVinEmpty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.vin.resize(0);

    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-no-source-of-funds", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVoutEmpty) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.vout.resize(0);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-no-sink-of-funds", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsOversize) {
    SelectParams(CBaseChainParams::REGTEST);
    CMutableTransaction mtx = GetValidTransaction();

    mtx.vin[0].scriptSig = CScript();
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 190; ++i)
        mtx.vin[0].scriptSig << vchData << OP_DROP;
    mtx.vin[0].scriptSig << OP_1;

    {
        // Transaction is just under the limit...
        CTransaction tx(mtx);
        CValidationState state;
        ASSERT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    // Not anymore!
    mtx.vin[1].scriptSig << vchData << OP_DROP;
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        ASSERT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), 100202);

        // Passes non-contextual checks...
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));

        // ... but fails contextual ones!
        CheckTransationResults results = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
        EXPECT_FALSE(results.validationPassed);
        EXPECT_EQ(results.reasonString, "bad-txns-oversize");
        EXPECT_EQ(results.dosLevel, 100);
    }

    {
        // But should be fine again once Sapling activates!
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

        mtx.fOverwintered = true;
        mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
        mtx.nVersion = SAPLING_TX_VERSION;

        // Change the proof types (which requires re-signing the JoinSplit data)
        mtx.vjoinsplit[0].proof = libzcash::GrothProof();
        mtx.vjoinsplit[1].proof = libzcash::GrothProof();
        CreateJoinSplitSignature(mtx, NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId);

        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), 103713);

        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));

        CheckTransationResults results = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
        EXPECT_TRUE(results.validationPassed);

        // Revert to default
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_ORCHARD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }
}

TEST(ChecktransactionTests, OversizeSaplingTxns) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;

    // Change the proof types (which requires re-signing the JoinSplit data)
    mtx.vjoinsplit[0].proof = libzcash::GrothProof();
    mtx.vjoinsplit[1].proof = libzcash::GrothProof();
    CreateJoinSplitSignature(mtx, NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId);

    // Transaction just under the limit
    mtx.vin[0].scriptSig = CScript();
    std::vector<unsigned char> vchData(520);
    for (unsigned int i = 0; i < 374; ++i)
        mtx.vin[0].scriptSig << vchData << OP_DROP;
    std::vector<unsigned char> vchDataRemainder(393);
    mtx.vin[0].scriptSig << vchDataRemainder << OP_DROP;
    mtx.vin[0].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING - 1);

        CValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    // Transaction equal to the limit
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING);

        CValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    // Transaction just over the limit
    mtx.vin[1].scriptSig << OP_1;

    {
        CTransaction tx(mtx);
        EXPECT_EQ(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION), MAX_TX_SIZE_AFTER_SAPLING + 1);

        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-oversize", false)).Times(1);
        EXPECT_FALSE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_ORCHARD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(ChecktransactionTests, BadTxnsVoutNegative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = -1;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVoutToolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = MAX_MONEY + 1;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vout-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsTxouttotalToolargeOutputs) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = MAX_MONEY;
    mtx.vout[1].nValue = 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, ValueBalanceNonZero) {
    CMutableTransaction mtx = GetValidTransaction(NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId);
    mtx.saplingBundle = sapling::test_only_invalid_bundle(0, 0, 10);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-valuebalance-nonzero", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, ValueBalanceOverflowsTotal) {
    CMutableTransaction mtx = GetValidTransaction(NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId);
    mtx.vout[0].nValue = 1;
    mtx.saplingBundle = sapling::test_only_invalid_bundle(1, 0, -MAX_MONEY);

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsTxouttotalToolargeJoinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vout[0].nValue = 1;
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txouttotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsTxintotalToolargeJoinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY - 1;
    mtx.vjoinsplit[1].vpub_new = MAX_MONEY - 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txintotal-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVpubOldNegative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = -1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_old-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVpubNewNegative) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = -1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_new-negative", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVpubOldToolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_old-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVpubNewToolarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_new = MAX_MONEY + 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpub_new-toolarge", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsVpubsBothNonzero) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].vpub_old = 1;
    mtx.vjoinsplit[0].vpub_new = 1;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-vpubs-both-nonzero", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsInputsDuplicate) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.hash = mtx.vin[0].prevout.hash;
    mtx.vin[1].prevout.n = mtx.vin[0].prevout.n;

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-inputs-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadJoinsplitsNullifiersDuplicateSameJoinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadJoinsplitsNullifiersDuplicateDifferentJoinsplit) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
    mtx.vjoinsplit[1].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");

    CTransaction tx(mtx);

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-joinsplits-nullifiers-duplicate", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadCbHasJoinsplits) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(1);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-has-joinsplits", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadCbEmptyScriptsig) {
    CMutableTransaction mtx = GetValidTransaction();
    // Make it a coinbase.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    mtx.vjoinsplit.resize(0);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-cb-length", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsPrevoutNull) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vin[1].prevout.SetNull();

    CTransaction tx(mtx);
    EXPECT_FALSE(tx.IsCoinBase());

    MockCValidationState state;
    EXPECT_CALL(state, DoS(10, false, REJECT_INVALID, "bad-txns-prevout-null", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, BadTxnsInvalidJoinsplitSignature) {
    SelectParams(CBaseChainParams::REGTEST);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.joinSplitSig[0] += 1;
    mtx.vin.resize(0); // No inputs, so this is a joinsplit-only transaction.
    CTransaction tx(mtx);
    auto sproutBranchId = NetworkUpgradeInfo[Consensus::BASE_SPROUT].nBranchId;

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx;
    std::vector<std::vector<const CTransaction*>> vvtx;
    vtx.push_back(&tx);
    vvtx.push_back(vtx);

    // Recreate the fake coins being spent.
    std::vector<CTxOut> allPrevOutputs;
    allPrevOutputs.resize(tx.vin.size());
    const PrecomputedTransactionData txdata(tx, allPrevOutputs);

    MockCValidationState state;
    GTestCoinsViewDB baseView;
    CCoinsViewCache view(&baseView);

    // during initial block download, for transactions being accepted into the
    // mempool (and thus not mined), DoS ban score should be zero

    CheckTransationResults results0 = ContextualCheckTransactionShieldedBundles(vvtx[0], &view, sproutBranchId, true, 0);
    EXPECT_FALSE(results0.validationPassed);
    EXPECT_EQ(results0.reasonString, "bad-txns-invalid-joinsplit-signature");
    EXPECT_EQ(results0.dosLevel, 0);

    // for transactions that have been mined in a block, DoS ban score should
    // always be 100.
    CheckTransationResults results100 = ContextualCheckTransactionShieldedBundles(vvtx[0], &view, sproutBranchId, false, 0);
    EXPECT_FALSE(results100.validationPassed);
    EXPECT_EQ(results100.reasonString, "bad-txns-invalid-joinsplit-signature");
    EXPECT_EQ(results100.dosLevel, 100);
}

TEST(ChecktransactionTests, NonCanonicalEd25519Signature) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    GTestCoinsViewDB baseView;
    CCoinsViewCache view(&baseView);

    auto saplingBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;
    CMutableTransaction mtx = GetValidTransaction(saplingBranchId);
    mtx.vin.resize(0); // No inputs, so this is a joinsplit-only transaction.
    CreateJoinSplitSignature(mtx, saplingBranchId);

    // Check that the signature is valid before we add L
    {
        CTransaction tx(mtx);

        //Setup Transaction Vectors
        std::vector<const CTransaction*> vtx;
        std::vector<std::vector<const CTransaction*>> vvtx;
        vtx.push_back(&tx);
        vvtx.push_back(vtx);

        MockCValidationState state;
        CheckTransationResults results = ContextualCheckTransactionShieldedBundles(vvtx[0], &view, saplingBranchId, true, 0);
        EXPECT_TRUE(results.validationPassed);
    }

    // Copied from libsodium/crypto_sign/ed25519/ref10/open.c
    static const unsigned char L[32] =
      { 0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
        0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 };

    // Add L to S, which starts at mtx.joinSplitSig[32].
    unsigned int s = 0;
    for (size_t i = 0; i < 32; i++) {
        s = mtx.joinSplitSig[32 + i] + L[i] + (s >> 8);
        mtx.joinSplitSig[32 + i] = s & 0xff;
    }

    CTransaction tx(mtx);

    //Setup Transaction Vectors
    std::vector<const CTransaction*> vtx;
    std::vector<std::vector<const CTransaction*>> vvtx;
    vtx.push_back(&tx);
    vvtx.push_back(vtx);

    MockCValidationState state;

    // during initial block download, for transactions being accepted into the
    // mempool (and thus not mined), DoS ban score should be zero

    CheckTransationResults results0 = ContextualCheckTransactionShieldedBundles(vvtx[0], &view, saplingBranchId, true, 0);
    EXPECT_FALSE(results0.validationPassed);
    EXPECT_EQ(results0.reasonString, "bad-txns-invalid-joinsplit-signature");
    EXPECT_EQ(results0.dosLevel, 0);

    // for transactions that have been mined in a block, DoS ban score should
    // always be 100.
    CheckTransationResults results100 = ContextualCheckTransactionShieldedBundles(vvtx[0], &view, saplingBranchId, false, 0);
    EXPECT_FALSE(results100.validationPassed);
    EXPECT_EQ(results100.reasonString, "bad-txns-invalid-joinsplit-signature");
    EXPECT_EQ(results100.dosLevel, 100);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);

}

// TEST(ContextualCheckShieldedInputsTest, JoinsplitSignatureDetectsOldBranchId) {
//     SelectParams(CBaseChainParams::REGTEST);
//     auto consensus = Params().GetConsensus();
//     std::optional<rust::Box<sapling::BatchValidator>> saplingAuth = std::nullopt;
//     std::optional<rust::Box<orchard::BatchValidator>> orchardAuth = std::nullopt;

//     auto saplingBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_SAPLING].nBranchId;
//     auto blossomBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_BLOSSOM].nBranchId;
//     auto heartwoodBranchId = NetworkUpgradeInfo[Consensus::UPGRADE_HEARTWOOD].nBranchId;

//     // Create a valid transaction for the Sapling epoch.
//     CMutableTransaction mtx = GetValidTransaction(saplingBranchId);
//     CTransaction tx(mtx);

//     // Recreate the fake coins being spent.
//     std::vector<CTxOut> allPrevOutputs;
//     allPrevOutputs.resize(tx.vin.size());
//     const PrecomputedTransactionData txdata(tx, allPrevOutputs);

//     MockCValidationState state;
//     AssumeShieldedInputsExistAndAreSpendable baseView;
//     CCoinsViewCache view(&baseView);
//     // Ensure that the transaction validates against Sapling.
//     EXPECT_TRUE(ContextualCheckShieldedInputs(
//         tx, txdata, state, view, saplingAuth, orchardAuth, consensus, saplingBranchId, false, false,
//         [](const Consensus::Params&) { return false; }));

//     // Attempt to validate the inputs against Blossom. We should be notified
//     // that an old consensus branch ID was used for an input.
//     EXPECT_CALL(state, DoS(
//         10, false, REJECT_INVALID,
//         strprintf("old-consensus-branch-id (Expected %s, found %s)",
//             HexInt(blossomBranchId),
//             HexInt(saplingBranchId)),
//         false, "")).Times(1);
//     EXPECT_FALSE(ContextualCheckShieldedInputs(
//         tx, txdata, state, view, saplingAuth, orchardAuth, consensus, blossomBranchId, false, false,
//         [](const Consensus::Params&) { return false; }));

//     // Attempt to validate the inputs against Heartwood. All we should learn is
//     // that the signature is invalid, because we don't check more than one
//     // network upgrade back.
//     EXPECT_CALL(state, DoS(
//         10, false, REJECT_INVALID,
//         "bad-txns-invalid-joinsplit-signature", false, "")).Times(1);
//     EXPECT_FALSE(ContextualCheckShieldedInputs(
//         tx, txdata, state, view, saplingAuth, orchardAuth, consensus, heartwoodBranchId, false, false,
//         [](const Consensus::Params&) { return false; }));
// }



TEST(ChecktransactionTests, OverwinterConstructors) {
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 20;

    // Check constructor with overwinter fields
    CTransaction tx(mtx);
    EXPECT_EQ(tx.nVersion, mtx.nVersion);
    EXPECT_EQ(tx.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(tx.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(tx.nExpiryHeight, mtx.nExpiryHeight);

    // Check constructor of mutable transaction struct
    CMutableTransaction mtx2(tx);
    EXPECT_EQ(mtx2.nVersion, mtx.nVersion);
    EXPECT_EQ(mtx2.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(mtx2.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(mtx2.nExpiryHeight, mtx.nExpiryHeight);
    EXPECT_TRUE(mtx2.GetHash() == mtx.GetHash());

    // Check assignment of overwinter fields
    CTransaction tx2 = tx;
    EXPECT_EQ(tx2.nVersion, mtx.nVersion);
    EXPECT_EQ(tx2.fOverwintered, mtx.fOverwintered);
    EXPECT_EQ(tx2.nVersionGroupId, mtx.nVersionGroupId);
    EXPECT_EQ(tx2.nExpiryHeight, mtx.nExpiryHeight);
    EXPECT_TRUE(tx2 == tx);
}

TEST(ChecktransactionTests, OverwinterSerialization) {
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 99;

    // Check round-trip serialization and deserialization from mtx to tx.
    {
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << mtx;
        CTransaction tx;
        ss >> tx;
        EXPECT_EQ(mtx.nVersion, tx.nVersion);
        EXPECT_EQ(mtx.fOverwintered, tx.fOverwintered);
        EXPECT_EQ(mtx.nVersionGroupId, tx.nVersionGroupId);
        EXPECT_EQ(mtx.nExpiryHeight, tx.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), CMutableTransaction(tx).GetHash());
        EXPECT_EQ(tx.GetHash(), CTransaction(mtx).GetHash());
    }

    // Also check mtx to mtx
    {
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << mtx;
        CMutableTransaction mtx2;
        ss >> mtx2;
        EXPECT_EQ(mtx.nVersion, mtx2.nVersion);
        EXPECT_EQ(mtx.fOverwintered, mtx2.fOverwintered);
        EXPECT_EQ(mtx.nVersionGroupId, mtx2.nVersionGroupId);
        EXPECT_EQ(mtx.nExpiryHeight, mtx2.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), mtx2.GetHash());
    }

    // Also check tx to tx
    {
        CTransaction tx(mtx);
        CDataStream ss(SER_DISK, PROTOCOL_VERSION);
        ss << tx;
        CTransaction tx2;
        ss >> tx2;
        EXPECT_EQ(tx.nVersion, tx2.nVersion);
        EXPECT_EQ(tx.fOverwintered, tx2.fOverwintered);
        EXPECT_EQ(tx.nVersionGroupId, tx2.nVersionGroupId);
        EXPECT_EQ(tx.nExpiryHeight, tx2.nExpiryHeight);

        EXPECT_EQ(mtx.GetHash(), CMutableTransaction(tx).GetHash());
        EXPECT_EQ(tx.GetHash(), tx2.GetHash());
    }
}

TEST(ChecktransactionTests, OverwinterDefaultValues) {
    // Check default values (this will fail when defaults change; test should then be updated)
    CTransaction tx;
    EXPECT_EQ(tx.nVersion, 1);
    EXPECT_EQ(tx.fOverwintered, false);
    EXPECT_EQ(tx.nVersionGroupId, 0);
    EXPECT_EQ(tx.nExpiryHeight, 0);
}

// A valid v3 transaction with no joinsplits
TEST(ChecktransactionTests, OverwinterValidTx) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;
    CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
}

TEST(ChecktransactionTests, OverwinterExpiryHeight) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    CMutableTransaction mtx = GetValidTransaction(NetworkUpgradeInfo[Consensus::UPGRADE_OVERWINTER].nBranchId);
    mtx.vjoinsplit.resize(0);
    mtx.nExpiryHeight = 0;

    {
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    {
        mtx.nExpiryHeight = TX_EXPIRY_HEIGHT_THRESHOLD - 1;
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    {
        mtx.nExpiryHeight = TX_EXPIRY_HEIGHT_THRESHOLD;
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-expiry-height-too-high", false)).Times(1);
        CheckTransactionWithoutProofVerification(0, tx, state);
    }

    {
        mtx.nExpiryHeight = std::numeric_limits<uint32_t>::max();
        CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-expiry-height-too-high", false)).Times(1);
        CheckTransactionWithoutProofVerification(0, tx, state);
    }

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

TEST(ChecktransactionTests, OrchardExpiryHeight) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_ORCHARD, 100);

    CMutableTransaction preOrchardMtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 99);
    EXPECT_EQ(preOrchardMtx.nExpiryHeight, 100 - 1);
    CMutableTransaction orchardMtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), 100);
    EXPECT_EQ(orchardMtx.nExpiryHeight, 100 + expiryDelta);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_ORCHARD, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Test that a Sprout tx with a negative version number is detected
// given the new Overwinter logic
TEST(ChecktransactionTests, SproutTxVersionTooLow) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = false;
    mtx.nVersion = -1;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

TEST(ChecktransactionTests, SaplingSproutInputSumsTooLarge) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = true;
    mtx.nVersion = SAPLING_TX_VERSION;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    {
        // create JSDescription
        JSDescription jsdesc;
        libzcash::GrothProof emptyProof;
        jsdesc.proof = emptyProof;
        mtx.vjoinsplit.push_back(jsdesc);
        mtx.vjoinsplit[0].nullifiers.at(0) = uint256S("0000000000000000000000000000000000000000000000000000000000000000");
        mtx.vjoinsplit[0].nullifiers.at(1) = uint256S("0000000000000000000000000000000000000000000000000000000000000001");
    }

    mtx.saplingBundle = sapling::test_only_invalid_bundle(1, 0, 0);

    mtx.vjoinsplit[0].vpub_new = (MAX_MONEY / 2) + 10;

    {
        UNSAFE_CTransaction tx(mtx);
        CValidationState state;
        EXPECT_TRUE(CheckTransactionWithoutProofVerification(0, tx, state));
    }

    mtx.saplingBundle = sapling::test_only_invalid_bundle(1, 0, (MAX_MONEY / 2) + 10);

    {
        UNSAFE_CTransaction tx(mtx);
        MockCValidationState state;
        EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-txns-txintotal-toolarge", false)).Times(1);
        CheckTransactionWithoutProofVerification(0, tx, state);
    }
}

// Test bad Overwinter version number in CheckTransactionWithoutProofVerification
TEST(ChecktransactionTests, OverwinterVersionNumberLow) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_MIN_TX_VERSION - 1;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-overwinter-version-too-low", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

// Test bad Overwinter version number in ContextualCheckTransaction
TEST(ChecktransactionTests, OverwinterVersionNumberHigh) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_MAX_TX_VERSION + 1;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;

    CheckTransationResults results = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
    EXPECT_FALSE(results.validationPassed);
    EXPECT_EQ(results.reasonString, "bad-tx-overwinter-version-too-high");
    EXPECT_EQ(results.dosLevel, 100);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}


// Test bad Overwinter version group id
TEST(ChecktransactionTests, OverwinterBadVersionGroupId) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.vjoinsplit.resize(0);
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nExpiryHeight = 0;
    mtx.nVersionGroupId = 0x12345678;

    EXPECT_THROW((CTransaction(mtx)), std::ios_base::failure);
    UNSAFE_CTransaction tx(mtx);
    MockCValidationState state;
    EXPECT_CALL(state, DoS(100, false, REJECT_INVALID, "bad-tx-version-group-id", false)).Times(1);
    CheckTransactionWithoutProofVerification(0, tx, state);
}

// This tests an Overwinter transaction checked against Sprout
TEST(ChecktransactionTests, OverwinterNotActive) {
    SelectParams(CBaseChainParams::TESTNET);
    auto chainparams = Params();

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CTransaction tx(mtx);
    MockCValidationState state;

    // during initial block download, for transactions being accepted into the
    // mempool (and thus not mined), DoS ban score should be zero, else 10
    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx, 0, 100, true);
    EXPECT_FALSE(results0.validationPassed);
    EXPECT_EQ(results0.reasonString, "tx-overwinter-not-active");
    EXPECT_EQ(results0.dosLevel, 0);

    // for transactions that have been mined in a block, DoS ban score should
    // always be 100.
    CheckTransationResults results100 = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
    EXPECT_FALSE(results100.validationPassed);
    EXPECT_EQ(results100.reasonString, "tx-overwinter-not-active");
    EXPECT_EQ(results100.dosLevel, 100);
}

// This tests a transaction without the fOverwintered flag set, against the Overwinter consensus rule set.
TEST(ChecktransactionTests, OverwinterFlagNotSet) {
    SelectParams(CBaseChainParams::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = false;
    mtx.nVersion = OVERWINTER_TX_VERSION;
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CTransaction tx(mtx);
    MockCValidationState state;

    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
    EXPECT_FALSE(results0.validationPassed);
    EXPECT_EQ(results0.reasonString, "tx-overwinter-flag-not-set");
    EXPECT_EQ(results0.dosLevel, 100);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}


// Overwinter (NU0) does not allow soft fork to version 4 Overwintered tx.
TEST(ChecktransactionTests, OverwinterInvalidSoftForkVersion) {
    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersion = 4; // This is not allowed
    mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
    mtx.nExpiryHeight = 0;

    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    try {
        ss << mtx;
        FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
    }
    catch(std::ios_base::failure & err) {
        EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
    }
    catch(...) {
        FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
    }
}

static void ContextualCreateTxCheck(const Consensus::Params& params, int nHeight,
    int expectedVersion, bool expectedOverwintered, int expectedVersionGroupId, int expectedExpiryHeight)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(params, nHeight);
    EXPECT_EQ(mtx.nVersion, expectedVersion);
    EXPECT_EQ(mtx.fOverwintered, expectedOverwintered);
    EXPECT_EQ(mtx.nVersionGroupId, expectedVersionGroupId);
    EXPECT_EQ(mtx.nExpiryHeight, expectedExpiryHeight);
}


// Test CreateNewContextualCMutableTransaction sets default values based on height
TEST(ChecktransactionTests, OverwinteredContextualCreateTx) {
    SelectParams(CBaseChainParams::REGTEST);
    const Consensus::Params& params = Params().GetConsensus();
    int overwinterActivationHeight = 200;
    int saplingActivationHeight = 500;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, overwinterActivationHeight);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, saplingActivationHeight);

    ContextualCreateTxCheck(params, overwinterActivationHeight - 1, 1, false, 0, 0);
    // Overwinter activates
    ContextualCreateTxCheck(params, overwinterActivationHeight,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, overwinterActivationHeight + expiryDelta);
    // Close to Sapling activation
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta - 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 2);
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta - 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta + 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta + 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - expiryDelta + 3,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    // Just before Sapling activation
    ContextualCreateTxCheck(params, saplingActivationHeight - 4,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - 3,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - 2,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    ContextualCreateTxCheck(params, saplingActivationHeight - 1,
        OVERWINTER_TX_VERSION, true, OVERWINTER_VERSION_GROUP_ID, saplingActivationHeight - 1);
    // Sapling activates
    ContextualCreateTxCheck(params, saplingActivationHeight,
        SAPLING_TX_VERSION, true, SAPLING_VERSION_GROUP_ID, saplingActivationHeight + expiryDelta);

    // Revert to default
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

// Test a v1 transaction which has a malformed header, perhaps modified in-flight
TEST(ChecktransactionTests, BadTxReceivedOverNetwork)
{
    // First four bytes <01 00 00 00> have been modified to be <FC FF FF FF> (-4 as an int32)
    std::string goodPrefix = "01000000";
    std::string badPrefix = "fcffffff";
    std::string hexTx = "0176c6541939b95f8d8b7779a77a0863b2a0267e281a050148326f0ea07c3608fb000000006a47304402207c68117a6263486281af0cc5d3bee6db565b6dce19ffacc4cb361906eece82f8022007f604382dee2c1fde41c4e6e7c1ae36cfa28b5b27350c4bfaa27f555529eace01210307ff9bef60f2ac4ceb1169a9f7d2c773d6c7f4ab6699e1e5ebc2e0c6d291c733feffffff02c0d45407000000001976a9145eaaf6718517ec8a291c6e64b16183292e7011f788ac5ef44534000000001976a91485e12fb9967c96759eae1c6b1e9c07ce977b638788acbe000000";

    // Good v1 tx
    {
        std::vector<unsigned char> txData(ParseHex(goodPrefix + hexTx ));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        CTransaction tx;
        ssData >> tx;
        EXPECT_EQ(tx.nVersion, 1);
        EXPECT_EQ(tx.fOverwintered, false);
    }

    // Good v1 mutable tx
    {
        std::vector<unsigned char> txData(ParseHex(goodPrefix + hexTx ));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        CMutableTransaction mtx;
        ssData >> mtx;
        EXPECT_EQ(mtx.nVersion, 1);
    }

    // Bad tx
    {
        std::vector<unsigned char> txData(ParseHex(badPrefix + hexTx ));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        try {
            CTransaction tx;
            ssData >> tx;
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
        }
        catch(std::ios_base::failure & err) {
            EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
        }
        catch(...) {
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
        }
    }

    // Bad mutable tx
    {
        std::vector<unsigned char> txData(ParseHex(badPrefix + hexTx ));
        CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
        try {
            CMutableTransaction mtx;
            ssData >> mtx;
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format'";
        }
        catch(std::ios_base::failure & err) {
            EXPECT_THAT(err.what(), testing::HasSubstr(std::string("Unknown transaction format")));
        }
        catch(...) {
            FAIL() << "Expected std::ios_base::failure 'Unknown transaction format', got some other exception";
        }
    }
}

TEST(ChecktransactionTests, InvalidSaplingShieldedCoinbase) {
    

    CMutableTransaction mtx = GetValidTransaction();
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;

    // Make it an invalid shielded coinbase (no ciphertexts or commitments).
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();
    mtx.saplingBundle = sapling::test_only_invalid_bundle(0, 1, 0);
    mtx.vjoinsplit.resize(0);

    CTransaction tx(mtx);
    EXPECT_TRUE(tx.IsCoinBase());

    MockCValidationState state;

    CheckTransationResults results0 = ContextualCheckTransactionSingleThreaded(tx, 0, 100, false);
    EXPECT_FALSE(results0.validationPassed);
    EXPECT_EQ(results0.reasonString, "tx-coinbase-sapling-bundle");
    EXPECT_EQ(results0.dosLevel, 100);

}

