// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
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

#ifndef BITCOIN_WALLET_WALLETDB_H
#define BITCOIN_WALLET_WALLETDB_H

#include "amount.h"
#include "wallet/db.h"
#include "wallet/sapling.h"
#include "key.h"
#include "keystore.h"
#include "zcash/Address.hpp"
#include "zcash/address/zip32.h"
#include "util.h"
#include "utiltime.h"

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

class CAccount;
class CAccountingEntry;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;
class JSOutPoint;

/** Error statuses for the wallet database */
enum DBErrors
{
    DB_LOAD_OK,
    DB_LOAD_CRYPTED,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

/* simple hd chain data model */
class CHDChain
{
public:
    static const int VERSION_HD_BASE = 1;
    static const int CURRENT_VERSION = VERSION_HD_BASE;
    int nVersion;
    uint256 seedFp;
    int64_t nCreateTime; // 0 means unknown
    uint32_t saplingAccountCounter;

    CHDChain() { SetNull(); }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(seedFp);
        READWRITE(nCreateTime);
        READWRITE(saplingAccountCounter);
    }

    void SetNull()
    {
        nVersion = CHDChain::CURRENT_VERSION;
        seedFp.SetNull();
        nCreateTime = 0;
        saplingAccountCounter = 0;
    }
};

class CKeyMetadata
{
public:
    static const int VERSION_BASIC=1;
    static const int VERSION_WITH_HDDATA=10;
    static const int CURRENT_VERSION=VERSION_WITH_HDDATA;
    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    std::string hdKeypath; //optional HD/zip32 keypath
    uint256 seedFp;

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(this->nVersion);
        READWRITE(nCreateTime);
        if (this->nVersion >= VERSION_WITH_HDDATA)
        {
            READWRITE(hdKeypath);
            READWRITE(seedFp);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hdKeypath.clear();
        seedFp.SetNull();
    }
};

/** Access to the wallet database (wallet.dat) */
class CWalletDB : public CDB
{
public:
    CWalletDB(const std::string& strFilename, const char* pszMode = "r+", bool fFlushOnClose = true) : CDB(strFilename, pszMode, fFlushOnClose)
    {
    }


    template <typename K, typename T>
    bool WriteTxn(const K& key, const T& value, std::string calling, bool fOverwrite = true)
    {

        LOCK(bitdb->cs_db);
        bool txnWrite = false;
        int retries = 0;

        while(!txnWrite) {
            //Writing transaction to the database
            if(!TxnBegin()) {
                LogPrintf("%s: Failed to begin txn, will retry.\n", calling);
                TxnAbort();
            } else {
                TxnSetTimeout();
                txnWrite = Write(key, value, fOverwrite);
                if (!txnWrite) {
                    LogPrintf("%s: Failed to write txn, will retry.\n", calling);
                    TxnAbort();
                } else {
                    if(!TxnCommit()) {
                      LogPrintf("%s: Failed to commit txn, warning.\n", calling);
                    }
                }
            }

            if (!txnWrite) {
              TxnAbort();
              MilliSleep(500);
              retries++;
            }

            if (retries > 3) {
              LogPrintf("%s Failed!!! Retry, attempts #%d.\n", calling, retries - 1);
              return false;
            }
        }
        return true;
    }

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool WriteCryptedName(const std::string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret);
    bool EraseName(const std::string& strAddress);
    bool EraseCryptedName(const uint256& chash);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool WriteCryptedPurpose(const std::string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret);
    bool ErasePurpose(const std::string& strAddress);
    bool EraseCryptedPurpose(const uint256& chash);

    bool WriteSaplingName(const std::string& strAddress, const std::string& strName);
    bool WriteCryptedSaplingName(const std::string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret);
    bool EraseSaplingName(const std::string& strAddress);
    bool EraseCryptedSaplingName(const uint256& chash);

    bool WriteSaplingPurpose(const std::string& strAddress, const std::string& purpose);
    bool WriteCryptedSaplingPurpose(const std::string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret);
    bool EraseSaplingPurpose(const std::string& strAddress);
    bool EraseCryptedSaplingPurpose(const uint256& chash);

    //Begin Historical Wallet Tx
    bool WriteArcTx(uint256 hash, ArchiveTxPoint arcTxPoint, bool txnProtected);
    bool EraseArcTx(uint256 hash);
    bool WriteCryptedArcTx(uint256 txid, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected);
    bool EraseCryptedArcTx(uint256 hash);
    bool WriteArcSaplingOp(uint256 nullifier, SaplingOutPoint op, bool txnProtected);
    bool WriteCryptedArcSaplingOp(uint256 nullifier, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected);
    bool EraseArcSaplingOp(uint256 nullifier);
    bool EraseCryptedArcSaplingOp(uint256 chash);
    //End Historical Wallet Tx

    bool WriteTx(uint256 hash, const CWalletTx& wtx, bool txnProtected);
    bool EraseTx(uint256 hash);

    bool WriteCryptedTx(uint256 txid, uint256 hash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected);
    bool EraseCryptedTx(uint256 hash);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata &keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey,const std::vector<unsigned char>& vchCryptedSecret,const uint256 chash,const std::vector<unsigned char> &vchCryptedMetaDataSecret);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);
    bool WriteCryptedCScript(const uint256 chash, const uint160& hash, const std::vector<unsigned char> &vchCryptedSecret);

    bool WriteWatchOnly(const CScript &script);
    bool WriteCryptedWatchOnly(const uint256 chash, const CScript &dest, const std::vector<unsigned char> &vchCryptedSecret);
    bool EraseWatchOnly(const CScript &script);
    bool EraseCryptedWatchOnly(const uint256 chash);

    //Write crypted status of the wallet
    bool WriteIsCrypted(const bool &crypted);

    //Height of first known transaction
    bool WriteWalletBirthday(const int& nHeight);
    bool ReadWalletBirthday(int& nHeight);

    bool WriteWalletBip39Enabled(const bool& enabled);
    bool ReadWalletBip39Enabled(bool& enabled);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteDefaultKey(const CPubKey& vchPubKey);
    bool WriteCryptedDefaultKey(const uint256 chash, const std::vector<unsigned char> &vchCryptedSecret);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool ReadCryptedPool(int64_t nPool, std::pair<uint256, std::vector<unsigned char>> &vchCryptedSecretPair);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool WriteCryptedPool(int64_t nPool, const uint256 chash, const std::vector<unsigned char> &vchCryptedSecret);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string &address, const std::string &key, const std::string &value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string &address, const std::string &key);

    bool WriteAccountingEntry(const CAccountingEntry& acentry);
    CAmount GetAccountCreditDebit(const std::string& strAccount);
    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& acentries);

    DBErrors InitalizeCryptedLoad(CWallet* pwallet);
    DBErrors LoadCryptedSeedFromDB(CWallet* pwallet);
    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTxToZap(
      CWallet* pwallet, std::vector<uint256>& vTxHash,
      std::vector<CWalletTx>& vWtx, std::vector<uint256>& vCTxHash,
      std::vector<uint256>& vArcHash, std::vector<uint256>& vCArcHash,
      std::vector<uint256>& vArcSaplingNullifier, std::vector<uint256>& vCArcSaplingNullifier);
    DBErrors ZapWalletTx(CWallet* pwallet, std::vector<CWalletTx>& vWtx);

    //Find and Erase records that are no longer used in the wallet.
    DBErrors FindOldRecordsToZap(
      CWallet* pwallet,
      std::vector<uint256>& vArcSproutNullifier,
      std::vector<libzcash::SproutViewingKey>& vSproutViewingKeys,
      std::vector<libzcash::SproutPaymentAddress>& vSproutPaymentAddresses,
      std::vector<libzcash::SproutPaymentAddress>& vCSproutPaymentAddresses,
      std::vector<libzcash::SproutPaymentAddress>& vSproutMetaData);
    DBErrors ZapOldRecords(CWallet* pwallet);

    static bool Compact(CDBEnv& dbenv, const std::string& strFile);
    static bool Recover(CDBEnv& dbenv, const std::string& filename, bool fOnlyKeys);
    static bool Recover(CDBEnv& dbenv, const std::string& filename);

    bool WriteHDSeed(const HDSeed& seed);
    bool WriteCryptedHDSeed(const uint256& seedFp, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret);
    //! write the hdchain model (external chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    /// Write spending key to wallet database, where key is payment address and value is spending key.
    bool WriteZKey(const libzcash::SproutPaymentAddress& addr, const libzcash::SproutSpendingKey& key, const CKeyMetadata &keyMeta);
    bool WriteSaplingZKey(const libzcash::SaplingIncomingViewingKey &ivk,
                          const libzcash::SaplingExtendedSpendingKey &key,
                          const CKeyMetadata  &keyMeta);

    bool WriteSaplingPaymentAddress(
      const libzcash::SaplingIncomingViewingKey &ivk,
      const libzcash::SaplingPaymentAddress &addr);
    bool WriteCryptedSaplingPaymentAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const uint256 chash,
        const std::vector<unsigned char> &vchCryptedSecret);

    //Wrtie the address, ivk and path of diversified address to the wallet
    bool WriteSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool WriteCryptedSaplingDiversifiedAddress(
        const libzcash::SaplingPaymentAddress &addr,
        const uint256 chash,
        const std::vector<unsigned char> &vchCryptedSecret);

    //Write the last used diversifier and ivk used
    bool WriteLastDiversifierUsed(
        const libzcash::SaplingIncomingViewingKey &ivk,
        const blob88 &path);
    bool WriteLastCryptedDiversifierUsed(
        const uint256 chash,
        const libzcash::SaplingIncomingViewingKey &ivk,
        const std::vector<unsigned char> &vchCryptedSecret);

    //Write the current spending key used to create diversified addresses to the wallet
    bool WritePrimarySaplingSpendingKey(
        const libzcash::SaplingExtendedSpendingKey &key);

    bool WriteCryptedZKey(const libzcash::SproutPaymentAddress & addr,
                          const libzcash::ReceivingKey & rk,
                          const std::vector<unsigned char>& vchCryptedSecret,
                          const CKeyMetadata &keyMeta);
    bool WriteCryptedSaplingZKey(const libzcash::SaplingExtendedFullViewingKey &extfvk,
                          const std::vector<unsigned char>& vchCryptedSecret,
                          const std::vector<unsigned char>& vchCryptedMetaDataSecret);
    bool WriteCryptedSaplingExtendedFullViewingKey(
                          const libzcash::SaplingExtendedFullViewingKey &extfvk,
                          const std::vector<unsigned char>& vchCryptedSecret);
    bool WriteCryptedPrimarySaplingSpendingKey(
                          const libzcash::SaplingExtendedSpendingKey &extsk,
                          const std::vector<unsigned char>& vchCryptedSecret);

    bool WriteSproutViewingKey(const libzcash::SproutViewingKey &vk);
    bool EraseSproutViewingKey(const libzcash::SproutViewingKey &vk);
    bool WriteSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);
    bool EraseSaplingExtendedFullViewingKey(const libzcash::SaplingExtendedFullViewingKey &extfvk);

    bool WriteSaplingWitnesses(const SaplingWallet& wallet);

private:
    CWalletDB(const CWalletDB&);
    void operator=(const CWalletDB&);

    bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry);
};

bool BackupWallet(const CWallet& wallet, const std::string& strDest);
void ThreadFlushWalletDB(const std::string& strFile);

#endif // BITCOIN_WALLET_WALLETDB_H
