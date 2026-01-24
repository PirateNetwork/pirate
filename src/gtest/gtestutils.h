#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "main.h"
#include "wallet/wallet.h"
#include "consensus/validation.h"

class TestWallet;

#define VCH(a,b) std::vector<unsigned char>(a, a + b)

static char ccjsonerr[1000] = "\0";
#define CCFromJson(o,s) \
    o = cc_conditionFromJSONString(s, ccjsonerr); \
    if (!o) FAIL() << "bad json: " << ccjsonerr;


extern std::string notaryPubkey;
extern std::string notarySecret;
extern CKey notaryKey;

extern TestWallet* pTestWallet;

/***
 * @brief Look inside a transaction
 * @param tx the transaction to look at
 */
void displayTransaction(const CTransaction& tx);
/****
 * @brief Look inside a block
 * @param blk the block to look at
 */
void displayBlock(const CBlock& blk);

void setConsoleDebugging(bool enable);

void setupChain();
/***
 * Generate a block
 * @param block a place to store the block (read from disk)
 */
void generateBlock(CBlock *block=NULL);
bool acceptTx(const CTransaction tx, CValidationState &state);
void acceptTxFail(const CTransaction tx);
/****
 * In order to do tests there needs to be inputs to spend.
 * This method creates a block and returns a transaction that spends the coinbase.
 * @param scriptPubKey
 * @returns the transaction
 */
// CTransaction getInputTx(CScript scriptPubKey);
CMutableTransaction spendTx(const CTransaction &txIn, int nOut=0);
// std::vector<uint8_t> getSig(const CMutableTransaction mtx, CScript inputPubKey, int nIn=0);

// int GenZero(int n)
// {
//     return 0;
// }

// int GenMax(int n)
// {
//     return n-1;
// }

class TransactionInProcess
{
public:
    TransactionInProcess(CWallet* wallet) : reserveKey(wallet) {}
    CWalletTx transaction;
    CReserveKey reserveKey;
};

class TestWallet;

class TestChain
{
public:
    /***
     * ctor to create a chain
     */
    TestChain();
    /***
     * dtor to release resources
     */
    ~TestChain();
    /***
     * Get the block index at the specified height
     * @param height the height (0 indicates current height
     * @returns the block index
     */
    CBlockIndex *GetIndex(uint32_t height = 0);
    /***
     * Get this chains view of the state of the chain
     * @returns the view
     */
    CCoinsViewCache *GetCoinsViewCache();
    /**
     * Generate a block
     * @returns the block generated
     */
    std::shared_ptr<CBlock> generateBlock(std::shared_ptr<CWallet> wallet,
            CValidationState* validationState = nullptr);
    /****
     * @brief set the chain time to something reasonable
     * @note must be called after generateBlock if you
     * want to produce another block
     */
    void IncrementChainTime();
    /***
     * @returns the notary's key
     */
    CKey getNotaryKey();
    /***
     * Add a transactoion to the mempool
     * @param tx the transaction
     * @returns the results
     */
    CValidationState acceptTx(const CTransaction &tx);
    /****
     * @brief attempt to connect a block to the chain
     * @param block the block to connect
     * @param state where to hold the results
     * @param pindex the new chain index
     * @param justCheck whether or not to do all checks
     * @param checkPOW true to check PoW
     * @returns true on success
     */
    bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex,
            bool fJustCheck = false,bool fCheckPOW = false);

    boost::filesystem::path GetDataDir();
private:
    boost::filesystem::path dataDir;
    std::string previousNetwork;
    void CleanGlobals();
};



class MockWalletDB {
public:
    MOCK_METHOD0(TxnBegin, bool());
    MOCK_METHOD0(TxnCommit, bool());
    MOCK_METHOD0(TxnAbort, bool());

    MOCK_METHOD1(WriteBestBlock, bool(const CBlockLocator& loc));

    MOCK_METHOD3(WriteTx, bool(uint256 hash, const CWalletTx& wtx, bool txnProtected));
    MOCK_METHOD3(WriteArcTx, bool(uint256 hash, ArchiveTxPoint arcTxPoint, bool txnProtected));
    MOCK_METHOD3(WriteArcSaplingOp, bool(uint256 nullifier, SaplingOutPoint op, bool txnProtected));
    MOCK_METHOD3(WriteArcOrchardOp, bool(uint256 nullifier, OrchardOutPoint op, bool txnProtected));
    MOCK_METHOD2(WriteSaplingPaymentAddress, bool(const libzcash::SaplingIncomingViewingKey &ivk, const libzcash::SaplingPaymentAddress &addr));
    MOCK_METHOD2(WriteOrchardPaymentAddress, bool(const libzcash::OrchardIncomingViewingKeyPirate &ivk,const libzcash::OrchardPaymentAddressPirate &addr));

    MOCK_METHOD4(WriteCryptedTx, bool(uint256 txid,uint256 hash,const std::vector<unsigned char>& vchCryptedSecret,bool txnProtected));
    MOCK_METHOD4(WriteCryptedArcTx, bool(uint256 txid, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected));
    MOCK_METHOD4(WriteCryptedArcSaplingOp, bool(uint256 nullifier, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected));
    MOCK_METHOD4(WriteCryptedArcOrchardOp, bool(uint256 nullifier, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected));
    MOCK_METHOD3(WriteCryptedSaplingPaymentAddress, bool(libzcash::SaplingPaymentAddress &addr,const uint256 chash,const std::vector<unsigned char> &vchCryptedSecret));
    MOCK_METHOD3(WriteCryptedOrchardPaymentAddress, bool(libzcash::OrchardPaymentAddressPirate &addr,const uint256 chash,const std::vector<unsigned char> &vchCryptedSecre));
    
    MOCK_METHOD1(WriteSaplingWitnesses, bool(const SaplingWallet& wallet));
    MOCK_METHOD1(WriteOrchardWitnesses, bool(const OrchardWallet& wallet));

};

class TestWallet : public CWallet {
public:
    TestWallet() : CWallet() { }

    TestWallet(const std::string& name)
        : CWallet( name + ".dat")
    {
        LOCK(cs_wallet);
        bool firstRunRet;
        DBErrors err = LoadWallet(firstRunRet);
        RegisterValidationInterface(this);
    }

    ~TestWallet() {}

    bool EncryptSerializedWalletObjects(
        const CKeyingMaterial &vchSecret,
        const uint256 chash,
        std::vector<unsigned char> &vchCryptedSecret){

        return CCryptoKeyStore::EncryptSerializedSecret(vchSecret, chash, vchCryptedSecret);
    }

    bool EncryptSerializedWalletObjects(
        CKeyingMaterial &vMasterKeyIn,
        const CKeyingMaterial &vchSecret,
        const uint256 chash,
        std::vector<unsigned char> &vchCryptedSecret) {

        return CCryptoKeyStore::EncryptSerializedSecret(vMasterKeyIn, vchSecret, chash, vchCryptedSecret);
    }

    bool DecryptSerializedWalletObjects(
        const std::vector<unsigned char>& vchCryptedSecret,
        const uint256 chash,
        CKeyingMaterial &vchSecret) {

        return CCryptoKeyStore::DecryptSerializedSecret(vchCryptedSecret, chash, vchSecret);
    }

    bool Unlock(const CKeyingMaterial& vMasterKeyIn) {
        return CCryptoKeyStore::Unlock(vMasterKeyIn);
    }

    void SetBestChain(MockWalletDB& walletdb, const CBlockLocator& loc, const int& height) {
        CWallet::SetBestChainINTERNAL(walletdb, loc, height);
    }

    void MarkAffectedTransactionsDirty(const CTransaction& tx) {
        CWallet::MarkAffectedTransactionsDirty(tx);
    }
};

// /***
//  * An easy-to-use wallet for testing Komodo
//  */
// class TestWallet : public CWallet
// {
// public:
//     TestWallet(const std::string& name);
//     TestWallet(const CKey& in, const std::string& name);
//     ~TestWallet();
//     /***
//      * @returns the public key
//      */
//     CPubKey GetPubKey() const;
//     /***
//      * @returns the private key
//      */
//     CKey GetPrivKey() const;
//     /***
//      * Sign a typical transaction
//      * @param hash the hash to sign
//      * @param hashType SIGHASH_ALL or something similar
//      * @returns the bytes to add to ScriptSig
//      */
//     std::vector<unsigned char> Sign(uint256 hash, unsigned char hashType);
//     /***
//      * Sign a cryptocondition
//      * @param cc the cryptocondition
//      * @param hash the hash to sign
//      * @returns the bytes to add to ScriptSig
//      */
//     std::vector<unsigned char> Sign(CC* cc, uint256 hash);
//     /*****
//      * @brief create a transaction with 1 recipient (signed)
//      * @param to who to send funds to
//      * @param amount
//      * @param fee
//      * @returns the transaction
//      */
//     TransactionInProcess CreateSpendTransaction(std::shared_ptr<TestWallet> to, CAmount amount,
//             CAmount fee = 0, bool commit = true);
//     /*************
//      * @brief Create a transaction, do not place in mempool
//      * @note throws std::logic_error if there was a problem
//      * @param to who to send to
//      * @param amount the amount to send
//      * @param fee the fee
//      * @param txToSpend the specific transaction to spend (ok if not transmitted yet)
//      * @returns the transaction
//     */
//     TransactionInProcess CreateSpendTransaction(std::shared_ptr<TestWallet> to,
//             CAmount amount, CAmount fee, CCoinControl& coinControl);
//     /****
//      * @brief create a transaction spending a vout that is not yet in the wallet
//      * @param vecSend the recipients
//      * @param wtxNew the resultant tx
//      * @param reserveKey the key used
//      * @param strFailReason the reason for any failure
//      * @param outputControl the tx to spend
//      * @returns true on success
//      */
//     bool CreateTransaction(const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew,
//             CReserveKey& reservekey, std::string& strFailReason, CAmount nMinFeeOverride, CCoinControl* coinControl);
//     using CWallet::CommitTransaction;
//     bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, CValidationState& state);
//     /***
//      * Transfer to another user (sends to mempool)
//      * @param to who to transfer to
//      * @param amount the amount
//      * @returns the results
//      */
//     CTransaction Transfer(std::shared_ptr<TestWallet> to, CAmount amount, CAmount fee = 0);
// private:
//     CKey key;
// };





// Fake an empty view
class GTestCoinsViewDB : public CCoinsView {
public:
    GTestCoinsViewDB() {}

    bool GetSproutAnchorAt(const uint256 &rt, SproutMerkleTree &tree) const {
        return false;
    }

    bool GetSaplingFrontierAnchorAt(const uint256 &rt, SaplingMerkleFrontier &tree) const {
        return false;
    }
    
    bool GetOrchardFrontierAnchorAt(const uint256 &rt, OrchardMerkleFrontier &tree) const {
        return false;
    }

    bool GetNullifier(const uint256 &nf, ShieldedType type) const {
        return false;
    }

    bool GetCoins(const uint256 &txid, CCoins &coins) const {
        return false;
    }

    bool HaveCoins(const uint256 &txid) const {
        return false;
    }

    uint256 GetBestBlock() const {
        uint256 a;
        return a;
    }

    uint256 GetBestAnchor(ShieldedType type) const {
        uint256 a;
        return a;
    }

    bool GetStats(CCoinsStats &stats) const {
        return false;
    }

};

// Fake an empty view
class GTestCCoinsViewCache : public CCoinsViewCache {
public:
    GTestCCoinsViewCache(GTestCoinsViewDB *baseIn) : CCoinsViewCache(baseIn) {
        fprintf(stderr, "GTestCCoinsViewCache created\n");
     }

};