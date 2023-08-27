// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#ifndef BITCOIN_WALLET_WALLET_H
#define BITCOIN_WALLET_WALLET_H

#include "amount.h"
#include "asyncrpcoperation.h"
#include "coins.h"
#include "key.h"
#include "keystore.h"
#include "main.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "util.h"
#include "util/strencodings.h"
#include "validationinterface.h"
#include "wallet/crypter.h"
#include "wallet/wallet_ismine.h"
#include "wallet/walletdb.h"
#include "wallet/rpcwallet.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"
#include "base58.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

typedef CWallet* CWalletRef;
extern std::vector<CWalletRef> vpwallets;

/**
 * Settings
 */
extern CFeeRate payTxFee;
extern CAmount maxTxFee;
extern CAmount minTxValue;
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool fSendFreeTransactions;
extern bool fPayAtLeastCustomFee;
extern bool fWalletRbf;
extern bool fTxDeleteEnabled;
extern bool fTxConflictDeleteEnabled;
extern int fDeleteInterval;
extern unsigned int fDeleteTransactionsAfterNBlocks;
extern unsigned int fKeepLastNTransactions;
extern std::string recoverySeedPhrase;
extern bool usingGUI;
extern int recoveryHeight;
extern int maxProcessingThreads;

extern SecureString *strOpeningWalletPassphrase;

//! -paytxfee default
static const CAmount DEFAULT_TRANSACTION_FEE = 0;
//! -fallbackfee default
static const CAmount DEFAULT_FALLBACK_FEE = 20000;
//! -paytxfee will warn if called with a higher fee than this amount (in satoshis) per KB
static const CAmount nHighTransactionFeeWarning = 0.01 * COIN;
//! -maxtxfee default
static const CAmount DEFAULT_TRANSACTION_MAXFEE = 0.1 * COIN;
//! Note must be at least this value to be added to the wallet, default is 1 Arrtoshis which will add all notes.
static const CAmount DEFAULT_MIN_TX_VALUE = 1;
//! -txconfirmtarget default
static const unsigned int DEFAULT_TX_CONFIRM_TARGET = 2;
//! -maxtxfee will warn if called with a higher fee than this amount (in satoshis)
static const CAmount nHighTransactionMaxFeeWarning = 100 * nHighTransactionFeeWarning;
//! Largest (in bytes) free transaction we're willing to create
static const unsigned int MAX_FREE_TRANSACTION_CREATE_SIZE = 1000;
//! target minimum change amount
static const CAmount MIN_CHANGE = CENT;

static const bool DEFAULT_DISABLE_WALLET = false;
static const bool DEFAULT_WALLET_RBF = false;

//! Size of witness cache
//  Should be large enough that we can expect not to reorg beyond our cache
//  unless there is some exceptional network disruption.
extern unsigned int WITNESS_CACHE_SIZE;

//! Size of HD seed in bytes
static const size_t HD_WALLET_SEED_LENGTH = 32;

//Default Transaction Rentention N-BLOCKS
static const int DEFAULT_TX_DELETE_INTERVAL = 10000;

//Default Transaction Rentention N-BLOCKS
static const unsigned int DEFAULT_TX_RETENTION_BLOCKS = 10000;

//Default Retenion Last N-Transactions
static const unsigned int DEFAULT_TX_RETENTION_LASTTX = 200;

//Amount of transactions to delete per run while syncing
static const int MAX_DELETE_TX_SIZE = 50000;

class CBlockIndex;
class CCoinControl;
class COutput;
class CReserveKey;
class CScript;
class CTxMemPool;
class CWalletTx;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys

    FEATURE_LATEST = 60000
};

 enum WalletCreateType {
      UNSET,
      RANDOM,
      RECOVERY,
      COMPLETE
 };

/** A key pool entry */
class CKeyPool
{
public:
    int64_t nTime;
    CPubKey vchPubKey;

    CKeyPool();
    CKeyPool(const CPubKey& vchPubKeyIn);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);
    }
};

/** Address book data */
class CAddressBookData
{
public:
    std::string name;
    std::string purpose;

    CAddressBookData()
    {
        purpose = "unknown";
    }

    typedef std::map<std::string, std::string> StringMap;
    StringMap destdata;
};

struct CRecipient
{
    CScript scriptPubKey;
    CAmount nAmount;
    bool fSubtractFeeFromAmount;
};

typedef std::map<std::string, std::string> mapValue_t;


static void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n"))
    {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}

struct COutputEntry
{
    CTxDestination destination;
    CAmount amount;
    int vout;
};

/** A note outpoint */
class JSOutPoint
{
public:
    // Transaction hash
    uint256 hash;
    // Index into CTransaction.vjoinsplit
    uint64_t js;
    // Index into JSDescription fields of length ZC_NUM_JS_OUTPUTS
    uint8_t n;

    JSOutPoint() { SetNull(); }
    JSOutPoint(uint256 h, uint64_t js, uint8_t n) : hash {h}, js {js}, n {n} { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hash);
        READWRITE(js);
        READWRITE(n);
    }

    void SetNull() { hash.SetNull(); }
    bool IsNull() const { return hash.IsNull(); }

    friend bool operator<(const JSOutPoint& a, const JSOutPoint& b) {
        return (a.hash < b.hash ||
                (a.hash == b.hash && a.js < b.js) ||
                (a.hash == b.hash && a.js == b.js && a.n < b.n));
    }

    friend bool operator==(const JSOutPoint& a, const JSOutPoint& b) {
        return (a.hash == b.hash && a.js == b.js && a.n == b.n);
    }

    friend bool operator!=(const JSOutPoint& a, const JSOutPoint& b) {
        return !(a == b);
    }

    std::string ToString() const;
};

class SproutNoteData
{
public:
    libzcash::SproutPaymentAddress address;

    /**
     * Cached note nullifier. May not be set if the wallet was not unlocked when
     * this was SproutNoteData was created. If not set, we always assume that the
     * note has not been spent.
     *
     * It's okay to cache the nullifier in the wallet, because we are storing
     * the spending key there too, which could be used to derive this.
     * If the wallet is encrypted, this means that someone with access to the
     * locked wallet cannot spend notes, but can connect received notes to the
     * transactions they are spent in. This is the same security semantics as
     * for transparent addresses.
     */
    boost::optional<uint256> nullifier;

    /**
     * Cached incremental witnesses for spendable Notes.
     * Beginning of the list is the most recent witness.
     */
    std::list<SproutWitness> witnesses;

    /**
     * Block height corresponding to the most current witness.
     *
     * When we first create a SproutNoteData in CWallet::FindMySproutNotes, this is set to
     * -1 as a placeholder. The next time CWallet::ChainTip is called, we can
     * determine what height the witness cache for this note is valid for (even
     * if no witnesses were cached), and so can set the correct value in
     * CWallet::BuildWitnessCache and CWallet::DecrementNoteWitnesses.
     */
    int witnessHeight;

    //In Memory Only
    bool witnessRootValidated;

    SproutNoteData() : address(), nullifier(), witnessHeight {-1}, witnessRootValidated {false} { }
    SproutNoteData(libzcash::SproutPaymentAddress a) :
            address {a}, nullifier(), witnessHeight {-1}, witnessRootValidated {false} { }
    SproutNoteData(libzcash::SproutPaymentAddress a, uint256 n) :
            address {a}, nullifier {n}, witnessHeight {-1}, witnessRootValidated {false} { }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(address);
        READWRITE(nullifier);
        READWRITE(witnesses);
        READWRITE(witnessHeight);
    }

    friend bool operator<(const SproutNoteData& a, const SproutNoteData& b) {
        return (a.address < b.address ||
                (a.address == b.address && a.nullifier < b.nullifier));
    }

    friend bool operator==(const SproutNoteData& a, const SproutNoteData& b) {
        return (a.address == b.address && a.nullifier == b.nullifier);
    }

    friend bool operator!=(const SproutNoteData& a, const SproutNoteData& b) {
        return !(a == b);
    }
};

class SaplingNoteData
{
public:
    /**
     * We initialize the height to -1 for the same reason as we do in SproutNoteData.
     * See the comment in that class for a full description.
     */
    SaplingNoteData() : witnessHeight {-1}, nullifier(), witnessRootValidated {false}, value {0} { }
    SaplingNoteData(libzcash::SaplingIncomingViewingKey ivk) : ivk {ivk}, witnessHeight {-1}, nullifier(), witnessRootValidated {false}, value {0} { }
    SaplingNoteData(libzcash::SaplingIncomingViewingKey ivk, uint256 n) : ivk {ivk}, witnessHeight {-1}, nullifier(n), witnessRootValidated {false}, value {0} { }

    std::list<SaplingWitness> witnesses;
    int witnessHeight;
    libzcash::SaplingIncomingViewingKey ivk;
    boost::optional<uint256> nullifier;

    //In Memory Only
    bool witnessRootValidated;
    CAmount value;
    libzcash::SaplingPaymentAddress address;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH)) {
            READWRITE(nVersion);
        }
        READWRITE(ivk);
        READWRITE(nullifier);
        READWRITE(witnesses);
        READWRITE(witnessHeight);
    }

    friend bool operator==(const SaplingNoteData& a, const SaplingNoteData& b) {
        return (a.ivk == b.ivk && a.nullifier == b.nullifier && a.witnessHeight == b.witnessHeight);
    }

    friend bool operator!=(const SaplingNoteData& a, const SaplingNoteData& b) {
        return !(a == b);
    }
};

typedef std::map<JSOutPoint, SproutNoteData> mapSproutNoteData_t;
typedef std::map<SaplingOutPoint, SaplingNoteData> mapSaplingNoteData_t;

/** Decrypted note, its location in a transaction, and number of confirmations. */
struct CSproutNotePlaintextEntry
{
    JSOutPoint jsop;
    libzcash::SproutPaymentAddress address;
    libzcash::SproutNotePlaintext plaintext;
    int confirmations;
};

/** Sapling note, its location in a transaction, and number of confirmations. */
struct SaplingNoteEntry
{
    SaplingOutPoint op;
    libzcash::SaplingPaymentAddress address;
    libzcash::SaplingNote note;
    std::array<unsigned char, ZC_MEMO_SIZE> memo;
    int confirmations;
};

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx : public CTransaction
{
private:
    /** Constant used in hashBlock to indicate tx has been abandoned */
    static const uint256 ABANDON_HASH;

    int GetDepthInMainChainINTERNAL(const CBlockIndex* &pindexRet) const;

public:
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    int nIndex;

    // memory only
    mutable bool fMerkleVerified;


    CMerkleTx()
    {
        Init();
    }

    CMerkleTx(const CTransaction& txIn) : CTransaction(txIn)
    {
        Init();
    }

    void Init()
    {
        hashBlock = uint256();
        nIndex = -1;
        fMerkleVerified = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CTransaction*)this);
        READWRITE(hashBlock);
        READWRITE(vMerkleBranch);
        READWRITE(nIndex);
    }

    void SetMerkleBranch(const CBlock& block);


    /**
     * Return depth of transaction in blockchain:
     * -1  : not in blockchain, and not in memory pool (conflicted transaction)
     *  0  : in memory pool, waiting to be included in a block
     * >=1 : this many blocks deep in the main chain
     */
    int GetDepthInMainChain(const CBlockIndex* &pindexRet) const;
    int GetDepthInMainChain() const { const CBlockIndex *pindexRet; return GetDepthInMainChain(pindexRet); }
    bool IsInMainChain() const { const CBlockIndex *pindexRet; return GetDepthInMainChainINTERNAL(pindexRet) > 0; }
    int GetBlocksToMaturity() const;
    bool AcceptToMemoryPool(bool fLimitFree=true, bool fRejectAbsurdFee=true);

    bool isAbandoned() const { return (hashBlock == ABANDON_HASH); }
};

/**
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx
{
private:
    const CWallet* pwallet;

public:
    mapValue_t mapValue;
    mapSproutNoteData_t mapSproutNoteData;
    mapSaplingNoteData_t mapSaplingNoteData;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived; //! time received by this node
    unsigned int nTimeSmart;
    char fFromMe;
    std::string strFromAccount;
    int64_t nOrderPos; //! position in ordered transaction list

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fImmatureCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fWatchDebitCached;
    mutable bool fWatchCreditCached;
    mutable bool fImmatureWatchCreditCached;
    mutable bool fAvailableWatchCreditCached;
    mutable bool fChangeCached;
    mutable CAmount nDebitCached;
    mutable CAmount nCreditCached;
    mutable CAmount nImmatureCreditCached;
    mutable CAmount nAvailableCreditCached;
    mutable CAmount nWatchDebitCached;
    mutable CAmount nWatchCreditCached;
    mutable CAmount nImmatureWatchCreditCached;
    mutable CAmount nAvailableWatchCreditCached;
    mutable CAmount nChangeCached;

    CWalletTx()
    {
        Init(NULL);
    }

    CWalletTx(const CWallet* pwalletIn)
    {
        Init(pwalletIn);
    }

    CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn) : CMerkleTx(txIn)
    {
        Init(pwalletIn);
    }

    CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn) : CMerkleTx(txIn)
    {
        Init(pwalletIn);
    }

    void Init(const CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        mapValue.clear();
        mapSproutNoteData.clear();
        mapSaplingNoteData.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        fDebitCached = false;
        fCreditCached = false;
        fImmatureCreditCached = false;
        fAvailableCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fChangeCached = false;
        nDebitCached = 0;
        nCreditCached = 0;
        nImmatureCreditCached = 0;
        nAvailableCreditCached = 0;
        nWatchDebitCached = 0;
        nWatchCreditCached = 0;
        nAvailableWatchCreditCached = 0;
        nImmatureWatchCreditCached = 0;
        nChangeCached = 0;
        nOrderPos = -1;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        if (ser_action.ForRead())
            Init(NULL);
        char fSpent = false;

        if (!ser_action.ForRead())
        {
            mapValue["fromaccount"] = strFromAccount;

            WriteOrderPos(nOrderPos, mapValue);

            if (nTimeSmart)
                mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        READWRITE(*static_cast<CMerkleTx*>(this));
        std::vector<CMerkleTx> vUnused; //!< Used to be vtxPrev
        READWRITE(vUnused);
        READWRITE(mapValue);
        READWRITE(mapSproutNoteData);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(fFromMe);
        READWRITE(fSpent);

        if (fOverwintered && nVersion >= SAPLING_TX_VERSION) {
            READWRITE(mapSaplingNoteData);
        }

        if (ser_action.ForRead())
        {
            strFromAccount = mapValue["fromaccount"];

            ReadOrderPos(nOrderPos, mapValue);

            nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(mapValue["timesmart"]) : 0;
        }

        mapValue.erase("fromaccount");
        mapValue.erase("version");
        mapValue.erase("spent");
        mapValue.erase("n");
        mapValue.erase("timesmart");
    }

    //! make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fWatchDebitCached = false;
        fWatchCreditCached = false;
        fAvailableWatchCreditCached = false;
        fImmatureWatchCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    void SetSproutNoteData(mapSproutNoteData_t &noteData);
    void SetSaplingNoteData(mapSaplingNoteData_t &noteData);

    std::pair<libzcash::SproutNotePlaintext, libzcash::SproutPaymentAddress> DecryptSproutNote(
	JSOutPoint jsop) const;
    boost::optional<std::pair<
        libzcash::SaplingNotePlaintext,
        libzcash::SaplingPaymentAddress>> DecryptSaplingNote(const Consensus::Params& params, int height, SaplingOutPoint op) const;
    boost::optional<std::pair<
        libzcash::SaplingNotePlaintext,
        libzcash::SaplingPaymentAddress>> DecryptSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op) const;
    boost::optional<std::pair<
        libzcash::SaplingNotePlaintext,
        libzcash::SaplingPaymentAddress>> RecoverSaplingNote(const Consensus::Params& params, int height,
            SaplingOutPoint op, std::set<uint256>& ovks) const;
    boost::optional<std::pair<
        libzcash::SaplingNotePlaintext,
        libzcash::SaplingPaymentAddress>> RecoverSaplingNoteWithoutLeadByteCheck(SaplingOutPoint op, std::set<uint256>& ovks) const;

    //! filter decides which addresses will count towards the debit
    CAmount GetDebit(const isminefilter& filter) const;
    CAmount GetCredit(const isminefilter& filter) const;
    CAmount GetImmatureCredit(bool fUseCache=true) const;
    CAmount GetAvailableCredit(bool fUseCache=true) const;
    CAmount GetImmatureWatchOnlyCredit(const bool& fUseCache=true) const;
    CAmount GetAvailableWatchOnlyCredit(const bool& fUseCache=true) const;
    CAmount GetChange() const;

    void GetAmounts(std::list<COutputEntry>& listReceived,
                    std::list<COutputEntry>& listSent, CAmount& nFee, std::string& strSentAccount, const isminefilter& filter) const;

    void GetAccountAmounts(const std::string& strAccount, CAmount& nReceived,
                           CAmount& nSent, CAmount& nFee, const isminefilter& filter) const;

    bool IsFromMe(const isminefilter& filter) const
    {
        return (GetDebit(filter) > 0);
    }

    bool InMempool() const;
    bool IsTrusted() const;

    int64_t GetTxTime() const;

    bool RelayWalletTransaction();

    std::set<uint256> GetConflicts() const;
};




class COutput
{
public:
    const CWalletTx *tx;
    int i;
    int nDepth;
    bool fSpendable;

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn, bool fSpendableIn)
    {
        tx = txIn; i = iIn; nDepth = nDepthIn; fSpendable = fSpendableIn;
    }

    std::string ToString() const;
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //! todo: add something to note what created it (user, getnewaddress, change)
    //!   maybe should have a map<string, string> property map

    CWalletKey(int64_t nExpires=0);

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(LIMITED_STRING(strComment, 65536));
    }
};

/**
 * Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
public:
    std::string strAccount;
    CAmount nCreditDebit;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos;  //! position in ordered transaction list
    uint64_t nEntryNo;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
        nEntryNo = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        //! Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nTime);
        READWRITE(LIMITED_STRING(strOtherAccount, 65536));

        if (!ser_action.ForRead())
        {
            WriteOrderPos(nOrderPos, mapValue);

            if (!(mapValue.empty() && _ssExtra.empty()))
            {
                CDataStream ss(s.GetType(), s.GetVersion());
                ss.insert(ss.begin(), '\0');
                ss << mapValue;
                ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
                strComment.append(ss.str());
            }
        }

        READWRITE(LIMITED_STRING(strComment, 65536));

        size_t nSepPos = strComment.find("\0", 0, 1);
        if (ser_action.ForRead())
        {
            mapValue.clear();
            if (std::string::npos != nSepPos)
            {
                CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), s.GetType(), s.GetVersion());
                ss >> mapValue;
                _ssExtra = std::vector<char>(ss.begin(), ss.end());
            }
            ReadOrderPos(nOrderPos, mapValue);
        }
        if (std::string::npos != nSepPos)
            strComment.erase(nSepPos);

        mapValue.erase("n");
    }

private:
    std::vector<char> _ssExtra;
};


/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CValidationInterface
{
protected:
    bool fBroadcastTransactions;
private:
    bool SelectCoins(const CAmount& nTargetValue, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet, bool& fOnlyCoinbaseCoinsRet, bool& fNeedCoinbaseCoinsRet, const CCoinControl *coinControl = NULL) const;

    CWalletDB *pwalletdbEncryption;

    //In Memory Block Locator of corresponding to wallet transactions
    //Set in setBestChain, used passed to SetBestChainINTERNAL as const
    CBlockLocator currentBlock;

    //! the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    int64_t nNextResend;
    int64_t nLastResend;
    int64_t nLastSetChain;
    int nSetChainUpdates;

    template <class T>
    using TxSpendMap = std::multimap<T, uint256>;
    /**
     * Used to keep track of spent outpoints, and
     * detect and report conflicts (double-spends or
     * mutated transactions where the mutant gets mined).
     */
    typedef TxSpendMap<COutPoint> TxSpends;
    TxSpends mapTxSpends;
    /**
     * Used to keep track of spent Notes, and
     * detect and report conflicts (double-spends).
     */
    typedef TxSpendMap<uint256> TxNullifiers;
    TxNullifiers mapTxSproutNullifiers;
    TxNullifiers mapTxSaplingNullifiers;

    std::vector<CTransaction> pendingSaplingSweepTxs;
    AsyncRPCOperationId saplingSweepOperationId;

    std::vector<CTransaction> pendingSaplingConsolidationTxs;
    AsyncRPCOperationId saplingConsolidationOperationId;

    void AddToTransparentSpends(const COutPoint& outpoint, const uint256& wtxid);
    void AddToSproutSpends(const uint256& nullifier, const uint256& wtxid);
    void AddToSaplingSpends(const uint256& nullifier, const uint256& wtxid);
    void AddToSpends(const uint256& wtxid);
    void RemoveFromTransparentSpends(const uint256& wtxid);
    void RemoveFromSproutSpends(const uint256& wtxid);
    void RemoveFromSaplingSpends(const uint256& wtxid);
    void RemoveFromSpends(const uint256& wtxid);

public:
    //Height for Lockmessage in GUI
    int chainHeight = 0;
    int walletHeight = 0;

    bool needsRescan = false;
    bool fSaplingConsolidationEnabled = false;
    bool fConsolidationRunning = false;
    int initializeConsolidationInterval = (Params().GetConsensus().nPowTargetSpacing/60) * 60 * 24 * 7; //Intialize 1 per week
    int nextConsolidation = 0;
    int targetConsolidationQty = 100;

    string strCleanUpStatus = "";
    bool fCleanupRoundComplete = false;
    std::vector<uint256> vCleanUpTxids;
    int cleanUpConfirmed = 0;
    int cleanUpConflicted = 0;
    int cleanUpUnconfirmed = 0;
    int cleanupMaxExpirationHieght = 0;
    int cleanupCurrentRoundUnspent = 0;

    bool fSaplingSweepEnabled = false;
    bool fSweepRunning = false;
    int sweepInterval = (Params().GetConsensus().nPowTargetSpacing/60) * 15; //Intialize every 15 minutes
    int nextSweep = 0;
    int targetSweepQty = 0;

    //Wallet Birthday;
    int nBirthday;
    bool bip39Enabled = false;
    uint256 seedEncyptionFP;

    WalletCreateType createType = UNSET;

    void ClearNoteWitnessCache();

    int64_t NullifierCount();
    std::set<uint256> GetNullifiers();

    std::map<std::string, std::set<uint256>> mapAddressTxids;
    std::map<uint256, ArchiveTxPoint> mapArcTxs;
    void LoadArcTxs(const uint256& wtxid, const ArchiveTxPoint& arcTxPt);
    void AddToArcTxs(const uint256& wtxid, ArchiveTxPoint& arcTxPt);
    void AddToArcTxs(const CWalletTx& wtx, int txHeight, ArchiveTxPoint& arcTxPt);

    std::map<uint256, JSOutPoint> mapArcJSOutPoints;
    void AddToArcJSOutPoints(const uint256& nullifier, const JSOutPoint& op);

    std::map<uint256, SaplingOutPoint> mapArcSaplingOutPoints;
    void AddToArcSaplingOutPoints(const uint256& nullifier, const SaplingOutPoint& op);

protected:

    int SproutWitnessMinimumHeight(const uint256& nullifier, int nWitnessHeight, int nMinimumHeight);
    int SaplingWitnessMinimumHeight(const uint256& nullifier, int nWitnessHeight, int nMinimumHeight);

    /**
     * pindex is the new tip being connected.
     */
     int VerifyAndSetInitialWitness(const CBlockIndex* pindex, bool witnessOnly);
     void BuildWitnessCache(const CBlockIndex* pindex, bool witnessOnly);
    /**
     * pindex is the old tip being disconnected.
     */
    void DecrementNoteWitnesses(const CBlockIndex* pindex);

    template <typename WalletDB>
    void SetBestChainINTERNAL(WalletDB& walletdb, const CBlockLocator& loc, const int& height) {
        AssertLockHeld(cs_wallet);
        if (!walletdb.TxnBegin()) {
            // This needs to be done atomically, so don't do it at all
            LogPrintf("SetBestChain(): Couldn't start atomic write\n");
            return;
        }
        try {

            if (!IsCrypted()) {
                for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {
                    auto wtx = wtxItem.second;
                    // Write all transactions to disk
                      if (!walletdb.WriteTx(wtxItem.first, wtx, false)) {
                          LogPrintf("SetBestChain(): Failed to write CWalletTx, aborting atomic write\n");
                          walletdb.TxnAbort();
                          return;
                      }
                }

                for (std::pair<const uint256, ArchiveTxPoint>& arcTxPtItem : mapArcTxs) {
                    uint256 txid = arcTxPtItem.first;
                    ArchiveTxPoint arcTxPt = arcTxPtItem.second;
                    // Write all archived transactions to disk
                      if (!walletdb.WriteArcTx(txid, arcTxPt, false)) {
                          LogPrintf("SetBestChain(): Failed to write ArchiveTxPoint, aborting atomic write\n");
                          walletdb.TxnAbort();
                          return;
                      }
                }

                for (std::pair<const uint256, SaplingOutPoint>& opItem : mapArcSaplingOutPoints) {
                    uint256 nullifier = opItem.first;
                    SaplingOutPoint op = opItem.second;

                    // Write all archived sapling outpoint
                    if (!walletdb.WriteArcSaplingOp(nullifier, op, false)) {
                        LogPrintf("SetBestChain(): Failed to write Archive Sapling Outpoint, aborting atomic write\n");
                        walletdb.TxnAbort();
                        return;
                    }

                }

                for (std::pair<const libzcash::SaplingPaymentAddress, libzcash::SaplingIncomingViewingKey>& ivkItem : mapUnsavedSaplingIncomingViewingKeys) {
                    auto addr = ivkItem.first;
                    auto ivk = ivkItem.second;

                    // Write all archived sapling outpoint
                    if (!walletdb.WriteSaplingPaymentAddress(ivk, addr)) {
                        LogPrintf("SetBestChain(): Failed to write unsaved Sapling Payment address, aborting atomic write\n");
                        walletdb.TxnAbort();
                        return;
                    }

                }

                if (!walletdb.WriteBestBlock(loc)) {
                    LogPrintf("SetBestChain(): Failed to write best block, aborting atomic write\n");
                    walletdb.TxnAbort();
                    return;
                }
            } else {
                LogPrintf("SetBestChain(): Attempting to SetBestChain while crypted.\n");
                if (!IsLocked()) {
                    for (std::pair<const uint256, CWalletTx>& wtxItem : mapWallet) {
                        CWalletTx wtx = wtxItem.second;
                        uint256 txid = wtx.GetHash();

                        std::vector<unsigned char> vchCryptedSecret;
                        uint256 chash = HashWithFP(txid);
                        CKeyingMaterial vchSecret = SerializeForEncryptionInput(txid, wtx);

                        //Encrypt all wallet transactions in memory inorder to write to disk
                        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
                            LogPrintf("SetBestChain(): Failed to encrypt ArchiveTxPoint, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                        if (!walletdb.WriteCryptedTx(wtxItem.first, chash, vchCryptedSecret, false)) {
                            LogPrintf("SetBestChain(): Failed to write crypted CWalletTx, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }
                    }


                    for (std::pair<const uint256, ArchiveTxPoint>& arcTxPtItem : mapArcTxs) {
                        uint256 txid = arcTxPtItem.first;
                        ArchiveTxPoint arcTxPt = arcTxPtItem.second;

                        std::vector<unsigned char> vchCryptedSecret;
                        uint256 chash = HashWithFP(txid);
                        CKeyingMaterial vchSecret = SerializeForEncryptionInput(txid, arcTxPt);

                        //Encrypt all archived transactions in memory inorder to write to disk
                        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
                            LogPrintf("SetBestChain(): Failed to encrypt ArchiveTxPoint, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                        // Write all archived transactions to disk
                          if (!walletdb.WriteCryptedArcTx(txid, chash, vchCryptedSecret, false)) {
                              LogPrintf("SetBestChain(): Failed to write ArchiveTxPoint, aborting atomic write\n");
                              walletdb.TxnAbort();
                              return;
                          }
                      }

                    for (std::pair<const uint256, SaplingOutPoint>& opItem : mapArcSaplingOutPoints) {
                        uint256 nullifier = opItem.first;
                        SaplingOutPoint op = opItem.second;

                        std::vector<unsigned char> vchCryptedSecret;
                        uint256 chash = HashWithFP(nullifier);
                        CKeyingMaterial vchSecret = SerializeForEncryptionInput(nullifier, op);

                        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
                            LogPrintf("SetBestChain(): Failed to encrypt Archive Sapling Outpoint, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                        // Write all archived sapling outpoint
                        if (!walletdb.WriteCryptedArcSaplingOp(nullifier, chash, vchCryptedSecret, false)) {
                            LogPrintf("SetBestChain(): Failed to write Archive Sapling Outpoint, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                    }

                    for (std::pair<const libzcash::SaplingPaymentAddress, libzcash::SaplingIncomingViewingKey>& ivkItem : mapUnsavedSaplingIncomingViewingKeys) {
                        auto addr = ivkItem.first;
                        auto ivk = ivkItem.second;

                        std::vector<unsigned char> vchCryptedSecret;
                        uint256 chash = HashWithFP(addr);
                        CKeyingMaterial vchSecret = SerializeForEncryptionInput(addr, ivk);

                        if (!EncryptSerializedWalletObjects(vchSecret, chash, vchCryptedSecret)) {
                            LogPrintf("SetBestChain(): Failed to encrypt unsaved Sapling Payment address, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                        // Write all unsaved Sapling Payment Addresses
                        if (!walletdb.WriteCryptedSaplingPaymentAddress(addr, chash, vchCryptedSecret)) {
                            LogPrintf("SetBestChain(): Failed to write unsaved Sapling Payment address, aborting atomic write\n");
                            walletdb.TxnAbort();
                            return;
                        }

                    }

                    if (!walletdb.WriteBestBlock(loc)) {
                        LogPrintf("SetBestChain(): Failed to write best block, aborting atomic write\n");
                        walletdb.TxnAbort();
                        return;
                    }

                } else {
                    walletdb.TxnAbort();
                    LogPrintf("SetBestChain(): Wallet is locked\n");
                    return;
                }
            }
        } catch (const std::exception &exc) {
            // Unexpected failure
            LogPrintf("SetBestChain(): Unexpected error during atomic write:\n");
            LogPrintf("%s\n", exc.what());
            walletdb.TxnAbort();
            return;
        }
        if (!walletdb.TxnCommit()) {
            // Couldn't commit all to db, but in-memory state is fine
            LogPrintf("SetBestChain(): Couldn't commit atomic write\n");
            return;
        }

        //Clear Unsaved Sapling Addresses after successful TxnCommit
        mapUnsavedSaplingIncomingViewingKeys.clear();
        fRunSetBestChain = false;
        walletHeight = height; //Set Wallet height to chain height.
        LogPrintf("SetBestChain(): SetBestChain was successful\n");
    }

private:
    template <class T>
    void SyncMetaData(std::pair<typename TxSpendMap<T>::iterator, typename TxSpendMap<T>::iterator>);

protected:
    bool UpdatedNoteData(const CWalletTx& wtxIn, CWalletTx& wtx);
    void MarkAffectedTransactionsDirty(const CTransaction& tx);

    /* the hd chain data model (chain counters) */
    CHDChain hdChain;

public:
    /*
     * Main wallet lock.
     * This lock protects all the fields added by CWallet
     *   except for:
     *      fFileBacked (immutable after instantiation)
     *      strWalletFile (immutable after instantiation)
     */
    mutable CCriticalSection cs_wallet;
    mutable CCriticalSection cs_wallet_threadedfunction;

    bool fFileBacked;
    std::string strWalletFile;

    std::set<int64_t> setKeyPool;
    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;
    std::map<libzcash::SproutPaymentAddress, CKeyMetadata> mapSproutZKeyMetadata;
    std::map<libzcash::SaplingIncomingViewingKey, CKeyMetadata> mapSaplingZKeyMetadata;

    //Temporary Holfing maps for crypted data to be loaded after all keys are loaded
    std::map<uint256, std::vector<unsigned char>> mapTempHoldCryptedSaplingMetadata;

    //Key used to create diversified address
    boost::optional<libzcash::SaplingExtendedSpendingKey> primarySaplingSpendingKey;
    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    std::string GetName() const
    {
        return "dummy";
    }

    CWallet()
    {
        SetNull();
    }

    CWallet(const std::string& strWalletFileIn)
    {
        SetNull();

        strWalletFile = strWalletFileIn;
        fFileBacked = true;
    }

    ~CWallet()
    {
        delete pwalletdbEncryption;
        pwalletdbEncryption = NULL;
    }

    void SetNull()
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        fFileBacked = false;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        nOrderPosNext = 0;
        nNextResend = 0;
        nLastResend = 0;
        nLastSetChain = 0;
        nSetChainUpdates = 0;
        nTimeFirstKey = 0;
        fBroadcastTransactions = false;
    }

    /**
     * The reverse mapping of nullifiers to notes.
     *
     * The mapping cannot be updated while an encrypted wallet is locked,
     * because we need the SpendingKey to create the nullifier (#1502). This has
     * several implications for transactions added to the wallet while locked:
     *
     * - Parent transactions can't be marked dirty when a child transaction that
     *   spends their output notes is updated.
     *
     *   - We currently don't cache any note values, so this is not a problem,
     *     yet.
     *
     * - GetFilteredNotes can't filter out spent notes.
     *
     *   - Per the comment in SproutNoteData, we assume that if we don't have a
     *     cached nullifier, the note is not spent.
     *
     * Another more problematic implication is that the wallet can fail to
     * detect transactions on the blockchain that spend our notes. There are two
     * possible cases in which this could happen:
     *
     * - We receive a note when the wallet is locked, and then spend it using a
     *   different wallet client.
     *
     * - We spend from a PaymentAddress we control, then we export the
     *   SpendingKey and import it into a new wallet, and reindex/rescan to find
     *   the old transactions.
     *
     * The wallet will only miss "pure" spends - transactions that are only
     * linked to us by the fact that they contain notes we spent. If it also
     * sends notes to us, or interacts with our transparent addresses, we will
     * detect the transaction and add it to the wallet (again without caching
     * nullifiers for new notes). As by default JoinSplits send change back to
     * the origin PaymentAddress, the wallet should rarely miss transactions.
     *
     * To work around these issues, whenever the wallet is unlocked, we scan all
     * cached notes, and cache any missing nullifiers. Since the wallet must be
     * unlocked in order to spend notes, this means that GetFilteredNotes will
     * always behave correctly within that context (and any other uses will give
     * correct responses afterwards), for the transactions that the wallet was
     * able to detect. Any missing transactions can be rediscovered by:
     *
     * - Unlocking the wallet (to fill all nullifier caches).
     *
     * - Restarting the node with -reindex (which operates on a locked wallet
     *   but with the now-cached nullifiers).
     */
    std::map<uint256, JSOutPoint> mapSproutNullifiersToNotes;

    std::map<uint256, SaplingOutPoint> mapSaplingNullifiersToNotes;

    std::map<uint256, CWalletTx> mapWallet;
    bool fRunSetBestChain = false;

    int64_t nOrderPosNext;

    std::map<CTxDestination, CAddressBookData> mapAddressBook;
    std::map<libzcash::PaymentAddress, CAddressBookData> mapZAddressBook;

    CPubKey vchDefaultKey;

    std::set<COutPoint> setLockedCoins;
    std::set<JSOutPoint> setLockedSproutNotes;
    std::set<SaplingOutPoint> setLockedSaplingNotes;

    int64_t nTimeFirstKey;

    const CWalletTx* GetWalletTx(const uint256& hash) const;

    //! check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    void AvailableCoins(std::vector<COutput>& vCoins, bool fOnlyConfirmed=true, const CCoinControl *coinControl = NULL, bool fIncludeZeroValue=false, bool fIncludeCoinBase=true) const;
    /**
     * Return list of available coins and locked coins grouped by non-change output address.
     */
    std::map<CTxDestination, std::vector<COutput>> ListCoins() const;

    /**
     * Find non-change parent output.
     */
    const CTxOut& FindNonChangeParentOutput(const CWalletTx& tx, int output) const;

    bool SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, std::vector<COutput> vCoins, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, CAmount& nValueRet) const;

    bool IsSpent(const uint256& hash, unsigned int n) const;
    unsigned int GetSpendDepth(const uint256& hash, unsigned int n) const;
    bool IsSproutSpent(const uint256& nullifier) const;
    unsigned int GetSproutSpendDepth(const uint256& nullifier) const;
    bool IsSaplingSpent(const uint256& nullifier) const;
    unsigned int GetSaplingSpendDepth(const uint256& nullifier) const;

    bool IsLockedCoin(uint256 hash, unsigned int n) const;
    void LockCoin(COutPoint& output);
    void UnlockCoin(COutPoint& output);
    void UnlockAllCoins();
    void ListLockedCoins(std::vector<COutPoint>& vOutpts) const;

    bool IsLockedNote(const JSOutPoint& outpt) const;
    void LockNote(const JSOutPoint& output);
    void UnlockNote(const JSOutPoint& output);
    void UnlockAllSproutNotes();
    std::vector<JSOutPoint> ListLockedSproutNotes();

    bool IsLockedNote(const SaplingOutPoint& output) const;
    void LockNote(const SaplingOutPoint& output);
    void UnlockNote(const SaplingOutPoint& output);
    void UnlockAllSaplingNotes();
    std::vector<SaplingOutPoint> ListLockedSaplingNotes();

    /**
     * keystore implementation
     * Generate a new key
     */
    CPubKey GenerateNewKey();
    //! Adds a key to the store, and saves it to disk.
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    //! Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key, const CPubKey &pubkey);
    bool LoadCryptedKey(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret);
    //! Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &metadata);
    bool LoadCryptedKeyMetadata(const uint256 &chash, const std::vector<unsigned char> &vchCryptedSecret, CKeyMetadata &metadata);

    bool LoadMinVersion(int nVersion) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }

    //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool AddCScript(const CScript& redeemScript);
    bool LoadCScript(const CScript& redeemScript);
    bool LoadCryptedCScript(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret);

    //! Adds a destination data tuple to the store, and saves it to disk
    bool AddDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Erases a destination data tuple in the store and on disk
    bool EraseDestData(const CTxDestination &dest, const std::string &key);
    //! Adds a destination data tuple to the store, without saving it to disk
    bool LoadDestData(const CTxDestination &dest, const std::string &key, const std::string &value);
    //! Look up a destination data tuple in the store, return true if found false otherwise
    bool GetDestData(const CTxDestination &dest, const std::string &key, std::string *value) const;
    //! Get all destination values matching a prefix.
    std::vector<std::string> GetDestValues(const std::string& prefix) const;

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript &dest);
    bool RemoveWatchOnly(const CScript &dest);
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);
    bool LoadCryptedWatchOnly(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret);

    bool LoadSaplingWatchOnly(const libzcash::SaplingExtendedFullViewingKey &extfvk);

    bool OpenWallet(const SecureString& strWalletPassphrase);
    bool Unlock(const SecureString& strWalletPassphrase);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);

    bool DecryptWalletTransaction(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& hash, CWalletTx& wtx);
    bool DecryptWalletArchiveTransaction(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& txid, ArchiveTxPoint& arcTxPt);
    bool DecryptArchivedSaplingOutpoint(const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret, uint256& nullifier, SaplingOutPoint& op);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    bool EncryptSerializedWalletObjects(
        const CKeyingMaterial &vchSecret,
        const uint256 chash,
        std::vector<unsigned char> &vchCryptedSecret);
    bool EncryptSerializedWalletObjects(
        CKeyingMaterial &vMasterKeyIn,
        const CKeyingMaterial &vchSecret,
        const uint256 chash,
        std::vector<unsigned char> &vchCryptedSecret);
    bool DecryptSerializedWalletObjects(
         const std::vector<unsigned char>& vchCryptedSecret,
         const uint256 chash,
         CKeyingMaterial &vchSecret);

    //Templates for encrypting various wallet objects to be written to disk
    template<typename WalletObject>
    uint256 HashWithFP(WalletObject &wObj);

    template<typename WalletObject1>
    CKeyingMaterial SerializeForEncryptionInput(WalletObject1 &wObj1);

    template<typename WalletObject1, typename WalletObject2>
    CKeyingMaterial SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2);

    template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
    CKeyingMaterial SerializeForEncryptionInput(WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3);

    template<typename WalletObject1>
    void DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1);

    template<typename WalletObject1, typename WalletObject2>
    void DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2);

    template<typename WalletObject1, typename WalletObject2, typename WalletObject3>
    void DeserializeFromDecryptionOutput(CKeyingMaterial &vchSecret, WalletObject1 &wObj1, WalletObject2 &wObj2, WalletObject3 &wObj3);

    void GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const;

    /**
      * Sprout ZKeys
      */
    //! Generates a new Sprout zaddr
    libzcash::SproutPaymentAddress GenerateNewSproutZKey();
    //! Adds spending key to the store, and saves it to disk
    bool AddSproutZKey(const libzcash::SproutSpendingKey &key);
    //! Adds spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadZKey(const libzcash::SproutSpendingKey &key);
    //! Load spending key metadata (used by LoadWallet)
    bool LoadZKeyMetadata(const libzcash::SproutPaymentAddress &addr, const CKeyMetadata &meta);
    //! Adds an encrypted spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedZKey(const libzcash::SproutPaymentAddress &addr, const libzcash::ReceivingKey &rk, const std::vector<unsigned char> &vchCryptedSecret);
    //! Adds an encrypted spending key to the store, and saves it to disk (virtual method, declared in crypter.h)
    bool AddCryptedSproutSpendingKey(
        const libzcash::SproutPaymentAddress &address,
        const libzcash::ReceivingKey &rk,
        const std::vector<unsigned char> &vchCryptedSecret);

    //! Adds a Sprout viewing key to the store, and saves it to disk.
    bool AddSproutViewingKey(const libzcash::SproutViewingKey &vk);
    bool RemoveSproutViewingKey(const libzcash::SproutViewingKey &vk);
    //! Adds a Sprout viewing key to the store, without saving it to disk (used by LoadWallet)
    bool LoadSproutViewingKey(const libzcash::SproutViewingKey &dest);

    /**
      * Sapling ZKeys
      */
    //! Generates new Sapling key
    libzcash::SaplingPaymentAddress GenerateNewSaplingZKey();
    //! Generates new Sapling diversified payment address
    libzcash::SaplingPaymentAddress GenerateNewSaplingDiversifiedAddress();
    //Set Primary key for address diversification
    bool SetPrimarySpendingKey(
        const libzcash::SaplingExtendedSpendingKey &extsk);
    bool LoadCryptedPrimarySaplingSpendingKey(
        const uint256 &extfvkFinger,
        const std::vector<unsigned char> &vchCryptedSecret);
    //! Adds Sapling spending key to the store, and saves it to disk
    bool AddSaplingZKey(
        const libzcash::SaplingExtendedSpendingKey &key);
    bool AddSaplingExtendedFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk);
    bool AddSaplingIncomingViewingKey(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const libzcash::SaplingPaymentAddress &addr);
    bool AddSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool AddLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);

    //! Adds spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadSaplingZKey(const libzcash::SaplingExtendedSpendingKey &key);
    //! Load spending key metadata (used by LoadWallet)
    bool LoadSaplingZKeyMetadata(const libzcash::SaplingIncomingViewingKey &ivk, const CKeyMetadata &meta);
    //! Add Sapling full viewing key to the store, without saving it to disk (used by LoadWallet)
    bool LoadSaplingFullViewingKey(
        const libzcash::SaplingExtendedFullViewingKey &extfvk);
    bool LoadCryptedSaplingExtendedFullViewingKey(
        const uint256 &extfvkFinger,
        const std::vector<unsigned char> &vchCryptedSecret,
        libzcash::SaplingExtendedFullViewingKey &extfvk);
    //! Adds a Sapling payment address -> incoming viewing key map entry,
    //! without saving it to disk (used by LoadWallet)
    bool LoadSaplingPaymentAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk);
    bool LoadCryptedSaplingPaymentAddress(
        const uint256 &chash,
        const std::vector<unsigned char> &vchCryptedSecret,
        libzcash::SaplingPaymentAddress& addr);

    bool LoadSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool LoadCryptedSaplingDiversifiedAddress(
        const uint256 &chash,
        const std::vector<unsigned char> &vchCryptedSecret);

    bool LoadLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool LoadLastCryptedDiversifierUsed(
        const uint256 &chash,
        const std::vector<unsigned char> &vchCryptedSecret);
    //! Adds an encrypted spending key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedSaplingZKey(
        const uint256 &chash,
        const std::vector<unsigned char> &vchCryptedSecret,
        libzcash::SaplingExtendedFullViewingKey &extfvk);


    bool LoadTempHeldCryptedData();
    /**
     * Increment the next transaction order id
     * @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = NULL);

    typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef std::multimap<int64_t, TxPair > TxItems;

    /**
     * Get the wallet's activity log
     * @return multimap of ordered transactions and accounting entries
     * @warning Returned pointers are *only* valid within the scope of passed acentries
     */
    TxItems OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount = "");

    void MarkDirty();
    bool UpdateNullifierNoteMap();
    void UpdateNullifierNoteMapWithTx(const CWalletTx& wtx);
    void UpdateSproutNullifierNoteMapWithTx(CWalletTx& wtx);
    void UpdateSaplingNullifierNoteMapWithTx(CWalletTx& wtx);
    void UpdateNullifierNoteMapForBlock(const CBlock* pblock);
    bool AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet, CWalletDB* pwalletdb, int nHeight, bool fRescan = false);
    bool EraseFromWallet(const uint256 &hash);
    void SyncTransactions(const std::vector<CTransaction> &vtx, const CBlock* pblock, const int nHeight);
    void ForceRescanWallet();
    void RescanWallet();
    void AddToWalletIfInvolvingMe(const std::vector<CTransaction> &vtx, std::vector<CTransaction> &vAddedTxes, const CBlock* pblock, const int nHeight, bool fUpdate, std::set<libzcash::SaplingPaymentAddress>& addressesFound, bool fRescan = false);
    void WitnessNoteCommitment(
         std::vector<uint256> commitments,
         std::vector<boost::optional<SproutWitness>>& witnesses,
         uint256 &final_anchor);
    void ReorderWalletTransactions(std::map<std::pair<int,int>, CWalletTx*> &mapSorted, int64_t &maxOrderPos);
    void UpdateWalletTransactionOrder(std::map<std::pair<int,int>, CWalletTx*> &mapSorted, bool resetOrder);
    bool DeleteTransactions(std::vector<uint256> &removeTxs, std::vector<uint256> &removeArcTxs, bool fRescan = false);
    bool DeleteWalletTransactions(const CBlockIndex* pindex, bool fRescan = false);
    bool initalizeArcTx();
    int ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false, bool fIgnoreBirthday = false, bool LockOnFinish = false);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(int64_t nBestBlockTime);
    std::vector<uint256> ResendWalletTransactionsBefore(int64_t nTime);
    CAmount GetBalance() const;
    CAmount GetUnconfirmedBalance() const;
    CAmount GetImmatureBalance() const;
    CAmount GetWatchOnlyBalance() const;
    CAmount GetUnconfirmedWatchOnlyBalance() const;
    CAmount GetImmatureWatchOnlyBalance() const;
    CAmount GetAvailableBalance(const CCoinControl* coinControl = nullptr) const;

    bool FundTransaction(CMutableTransaction& tx, CAmount& nFeeRet, int& nChangePosRet, std::string& strFailReason);
    bool CreateTransaction(const std::vector<CRecipient>& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, CAmount& nFeeRet, int& nChangePosRet,
                           std::string& strFailReason, CAmount& nMinFeeOverride, const CCoinControl *coinControl = NULL, bool sign = true);
    bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey);

    static CFeeRate minTxFee;
    static CFeeRate fallbackFee;

    bool NewKeyPool();
    bool TopUpKeyPool(unsigned int kpSize = 0);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex);
    bool GetKeyFromPool(CPubKey &key);
    int64_t GetOldestKeyPoolTime();
    void GetAllReserveKeys(std::set<CKeyID>& setAddress);

    std::set< std::set<CTxDestination> > GetAddressGroupings();
    std::map<CTxDestination, CAmount> GetAddressBalances();

    std::set<CTxDestination> GetAccountAddresses(const std::string& strAccount) const;

    boost::optional<uint256> GetSproutNoteNullifier(
        const JSDescription& jsdesc,
        const libzcash::SproutPaymentAddress& address,
        const ZCNoteDecryption& dec,
        const uint256& hSig,
        uint8_t n) const;
    mapSproutNoteData_t FindMySproutNotes(const CTransaction& tx) const;
    std::pair<mapSaplingNoteData_t, SaplingIncomingViewingKeyMap> FindMySaplingNotes(const std::vector<CTransaction> &vtx, int height) const;
    bool IsSproutNullifierFromMe(const uint256& nullifier) const;
    bool IsSaplingNullifierFromMe(const uint256& nullifier) const;

    void GetSproutNoteWitnesses(
         std::vector<JSOutPoint> notes,
         std::vector<boost::optional<SproutWitness>>& witnesses,
         uint256 &final_anchor);
    void GetSaplingNoteWitnesses(
         std::vector<SaplingOutPoint> notes,
         std::vector<boost::optional<SaplingWitness>>& witnesses,
         uint256 &final_anchor);
     bool GetSaplingNoteWitnessesConsolidationCleanup(
          std::vector<SaplingOutPoint> notes,
          std::vector<boost::optional<SaplingWitness>>& witnesses,
          uint256 &final_anchor);

    isminetype IsMine(const CTxIn& txin) const;
    CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
    isminetype IsMine(const CTxOut& txout) const;
    isminetype IsMine(const CTransaction& tx, uint32_t voutNum);
    CAmount GetCredit(const CTxOut& txout, const isminefilter& filter) const;
    bool IsChange(const CTxOut& txout) const;
    CAmount GetChange(const CTxOut& txout) const;
    bool IsMine(const CTransaction& tx);
    /** should probably be renamed to IsRelevantToMe */
    bool IsFromMe(const CTransaction& tx) const;
    CAmount GetDebit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, int32_t voutNum, const isminefilter& filter) const;
    CAmount GetCredit(const CTransaction& tx, const isminefilter& filter) const;
    CAmount GetChange(const CTransaction& tx) const;
    void ChainTip(const CBlockIndex *pindex, const CBlock *pblock, SproutMerkleTree sproutTree, SaplingMerkleTree saplingTree, bool added);
    void RunSaplingSweep(int blockHeight);
    void RunSaplingConsolidation(int blockHeight);
    void CommitAutomatedTx(const CTransaction& tx);
    /** Saves witness caches and best block locator to disk. */
    void SetBestChain(const CBlockLocator& loc, const int& height);
    void SetWalletBirthday(int nHeight);
    std::set<std::pair<libzcash::PaymentAddress, uint256>> GetNullifiersForAddresses(const std::set<libzcash::PaymentAddress> & addresses);
    bool IsNoteSproutChange(const std::set<std::pair<libzcash::PaymentAddress, uint256>> & nullifierSet, const libzcash::PaymentAddress & address, const JSOutPoint & entry);
    bool IsNoteSaplingChange(const std::set<std::pair<libzcash::PaymentAddress, uint256>> & nullifierSet, const libzcash::PaymentAddress & address, const SaplingOutPoint & entry);

    DBErrors InitalizeCryptedLoad();
    DBErrors LoadCryptedSeedFromDB();
    DBErrors LoadWallet(bool& fFirstRunRet);
    DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);
    DBErrors ZapOldRecords();

    bool SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& purpose);
    bool DecryptAddressBookEntry(const uint256 chash, std::vector<unsigned char> vchCryptedSecret, string& address, string& entry);

    bool SetZAddressBook(const libzcash::PaymentAddress& address, const std::string& strName, const std::string& purpose, bool fInTransaction = false);

    bool DelAddressBook(const CTxDestination& address);
    bool DelZAddressBook(const libzcash::PaymentAddress& address);

    void UpdatedTransaction(const uint256 &hashTx);

    unsigned int GetKeyPoolSize()
    {
        AssertLockHeld(cs_wallet); // setKeyPool
        return setKeyPool.size();
    }

    bool SetDefaultKey(const CPubKey &vchPubKey);
    bool DecryptDefaultKey(const uint256 &chash, std::vector<unsigned char> &vchCryptedSecret, CPubKey &vchPubKey);

    //Write crypted status to the wallet
    bool SetWalletCrypted(CWalletDB* pwalletdb);

    //! signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

    //! change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { LOCK(cs_wallet); return nWalletVersion; }

    //! Get wallet transactions that conflict with given transaction (spend same outputs)
    std::set<uint256> GetConflicts(const uint256& txid) const;

    //! Flush wallet (bitdb flush)
    void Flush(bool shutdown=false);

    //! Verify the wallet database and perform salvage if required
    static bool Verify(const std::string& walletFile, std::string& warningString, std::string& errorString);

    /**
     * Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const CTxDestination
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyAddressBookChanged;

    boost::signals2::signal<void (CWallet *wallet, const libzcash::PaymentAddress
            &address, const std::string &label, bool isMine,
            const std::string &purpose,
            ChangeType status)> NotifyZAddressBookChanged;

    /**
     * Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx,
            ChangeType status)> NotifyTransactionChanged;

    boost::signals2::signal<void ()> NotifyBalanceChanged;
    /** Show progress e.g. for rescan */
    boost::signals2::signal<void (const std::string &title, int nProgress)> ShowProgress;
    boost::signals2::signal<void ()> NotifyRescanStarted;
    boost::signals2::signal<void ()> NotifyRescanComplete;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;

    /** Inquire whether this wallet broadcasts transactions. */
    bool GetBroadcastTransactions() const { return fBroadcastTransactions; }
    /** Set whether this wallet broadcasts transactions. */
    void SetBroadcastTransactions(bool broadcast) { fBroadcastTransactions = broadcast; }

    /* Returns true if HD is enabled for all address types, false if only for Sapling */
    bool IsHDFullyEnabled() const;

    /* Generates a new HD seed (will reset the chain child index counters)
       Sets the seed's version based on the current wallet version (so the
       caller must ensure the current wallet version is correct before calling
       this function). */
    void GenerateNewSeed();
    bool IsValidPhrase(std::string &phrase);
    bool RestoreSeedFromPhrase(std::string &phrase);

    bool SetHDSeed(const HDSeed& seed);

    /* Set the HD chain model (chain child index counters) */
    void SetHDChain(const CHDChain& chain, bool memonly);
    const CHDChain& GetHDChain() const { return hdChain; }

    /* Set the current HD seed, without saving it to disk (used by LoadWallet) */
    bool LoadHDSeed(const HDSeed& key);

    /* Set the current encrypted HD seed, without saving it to disk (used by LoadWallet) */
    bool LoadCryptedHDSeed(const uint256& seedFp, const std::vector<unsigned char>& seed);

    //Get Address balance for the GUI
    void getZAddressBalances(std::map<libzcash::PaymentAddress, CAmount> &balances, int minDepth, bool requireSpendingKey);

    /* Find notes filtered by payment address, min depth, ability to spend */
    void GetFilteredNotes(std::vector<CSproutNotePlaintextEntry>& sproutEntries,
                          std::vector<SaplingNoteEntry>& saplingEntries,
                          std::string address,
                          int minDepth=1,
                          bool ignoreSpent=true,
                          bool requireSpendingKey=true);

    /* Find notes filtered by payment addresses, min depth, max depth, if they are spent,
       if a spending key is required, and if they are locked */
    void GetFilteredNotes(std::vector<CSproutNotePlaintextEntry>& sproutEntries,
                          std::vector<SaplingNoteEntry>& saplingEntries,
                          std::set<libzcash::PaymentAddress>& filterAddresses,
                          int minDepth=1,
                          int maxDepth=INT_MAX,
                          bool ignoreSpent=true,
                          bool requireSpendingKey=true,
                          bool ignoreLocked=true);
};

/** A key allocated from the key pool. */
class CReserveKey
{
protected:
    CWallet* pwallet;
    int64_t nIndex;
    CPubKey vchPubKey;
public:
    CReserveKey(CWallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
    }

    ~CReserveKey()
    {
        ReturnKey();
    }

    void ReturnKey();
    virtual bool GetReservedKey(CPubKey &pubkey);
    void KeepKey();
};


/**
 * Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount
{
public:
    CPubKey vchPubKey;

    CAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey = CPubKey();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    }
};

/** Error status printout */
#define ERR_RESULT(x) result.push_back(Pair("result", "error")) , result.push_back(Pair("error", x));

//
// Shielded key and address generalizations
//

class PaymentAddressBelongsToWallet : public boost::static_visitor<bool>
{
private:
    CWallet *m_wallet;
public:
    PaymentAddressBelongsToWallet(CWallet *wallet) : m_wallet(wallet) {}

    bool operator()(const libzcash::SproutPaymentAddress &zaddr) const;
    bool operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    bool operator()(const libzcash::InvalidEncoding& no) const;
};

class GetViewingKeyForPaymentAddress : public boost::static_visitor<boost::optional<libzcash::ViewingKey>>
{
private:
    CWallet *m_wallet;
public:
    GetViewingKeyForPaymentAddress(CWallet *wallet) : m_wallet(wallet) {}

    boost::optional<libzcash::ViewingKey> operator()(const libzcash::SproutPaymentAddress &zaddr) const;
    boost::optional<libzcash::ViewingKey> operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    boost::optional<libzcash::ViewingKey> operator()(const libzcash::InvalidEncoding& no) const;
};

class IncomingViewingKeyBelongsToWallet : public boost::static_visitor<bool>
{
private:
    CWallet *m_wallet;
public:
    IncomingViewingKeyBelongsToWallet(CWallet *wallet) : m_wallet(wallet) {}

    bool operator()(const libzcash::SproutPaymentAddress &zaddr) const;
    bool operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    bool operator()(const libzcash::InvalidEncoding& no) const;
};

class HaveSpendingKeyForPaymentAddress : public boost::static_visitor<bool>
{
private:
    CWallet *m_wallet;
public:
    HaveSpendingKeyForPaymentAddress(CWallet *wallet) : m_wallet(wallet) {}

    bool operator()(const libzcash::SproutPaymentAddress &zaddr) const;
    bool operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    bool operator()(const libzcash::InvalidEncoding& no) const;
};

class GetSpendingKeyForPaymentAddress : public boost::static_visitor<boost::optional<libzcash::SpendingKey>>
{
private:
    CWallet *m_wallet;
public:
    GetSpendingKeyForPaymentAddress(CWallet *wallet) : m_wallet(wallet) {}

    boost::optional<libzcash::SpendingKey> operator()(const libzcash::SproutPaymentAddress &zaddr) const;
    boost::optional<libzcash::SpendingKey> operator()(const libzcash::SaplingPaymentAddress &zaddr) const;
    boost::optional<libzcash::SpendingKey> operator()(const libzcash::InvalidEncoding& no) const;
};

class GetPubKeyForPubKey : public boost::static_visitor<CPubKey> {
public:
    GetPubKeyForPubKey() {}

    CPubKey operator()(const CKeyID &id) const {
        return CPubKey();
    }

    CPubKey operator()(const CPubKey &key) const {
        return key;
    }

    CPubKey operator()(const CScriptID &sid) const {
        return CPubKey();
    }

    CPubKey operator()(const CNoDestination &no) const {
        return CPubKey();
    }
};

class AddressVisitorString : public boost::static_visitor<std::string>
{
public:
    std::string operator()(const CNoDestination &dest) const { return ""; }

    std::string operator()(const CKeyID &keyID) const {
        return "key hash: " + keyID.ToString();
    }

    std::string operator()(const CPubKey &key) const {
        return "public key: " + HexStr(key);
    }

    std::string operator()(const CScriptID &scriptID) const {
        return "script hash: " + scriptID.ToString();
    }
};

enum KeyAddResult {
    SpendingKeyExists,
    KeyAlreadyExists,
    KeyAdded,
    KeyNotAdded,
    KeyAddedAddressAdded,
    KeyAddedAddressNotAdded,
    KeyExistsAddressAdded,
    KeyExistsAddressNotAdded,
};

class AddViewingKeyToWallet : public boost::static_visitor<KeyAddResult>
{
private:
    CWallet *m_wallet;
public:
    AddViewingKeyToWallet(CWallet *wallet) : m_wallet(wallet) {}

    KeyAddResult operator()(const libzcash::SproutViewingKey &sk) const;
    KeyAddResult operator()(const libzcash::SaplingExtendedFullViewingKey &sk) const;
    KeyAddResult operator()(const libzcash::InvalidEncoding& no) const;
};

class AddDiversifiedViewingKeyToWallet : public boost::static_visitor<KeyAddResult>
{
private:
    CWallet *m_wallet;
public:
    AddDiversifiedViewingKeyToWallet(CWallet *wallet) : m_wallet(wallet) {}

    KeyAddResult operator()(const libzcash::SaplingDiversifiedExtendedFullViewingKey &sk) const;
    KeyAddResult operator()(const libzcash::InvalidEncoding& no) const;
};

class AddSpendingKeyToWallet : public boost::static_visitor<KeyAddResult>
{
private:
    CWallet *m_wallet;
    const Consensus::Params &params;
    int64_t nTime;
    boost::optional<std::string> hdKeypath; // currently sapling only
    boost::optional<std::string> seedFpStr; // currently sapling only
    bool log;
public:
    AddSpendingKeyToWallet(CWallet *wallet, const Consensus::Params &params) :
        m_wallet(wallet), params(params), nTime(1), hdKeypath(boost::none), seedFpStr(boost::none), log(false) {}
    AddSpendingKeyToWallet(
        CWallet *wallet,
        const Consensus::Params &params,
        int64_t _nTime,
        boost::optional<std::string> _hdKeypath,
        boost::optional<std::string> _seedFp,
        bool _log
    ) : m_wallet(wallet), params(params), nTime(_nTime), hdKeypath(_hdKeypath), seedFpStr(_seedFp), log(_log) {}


    KeyAddResult operator()(const libzcash::SproutSpendingKey &sk) const;
    KeyAddResult operator()(const libzcash::SaplingExtendedSpendingKey &sk) const;
    KeyAddResult operator()(const libzcash::InvalidEncoding& no) const;
};

class AddDiversifiedSpendingKeyToWallet : public boost::static_visitor<KeyAddResult>
{
private:
    CWallet *m_wallet;
public:
    AddDiversifiedSpendingKeyToWallet(CWallet *wallet) : m_wallet(wallet) {}

    KeyAddResult operator()(const libzcash::SaplingDiversifiedExtendedSpendingKey &sk) const;
    KeyAddResult operator()(const libzcash::InvalidEncoding& no) const;
};

#define RETURN_IF_ERROR(CCerror) if ( CCerror != "" ) { ERR_RESULT(CCerror); return(result); }

#endif // BITCOIN_WALLET_WALLET_H
