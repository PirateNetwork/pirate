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

#include "wallet/walletdb.h"

#include "consensus/validation.h"
#include "keystore.h"
#include "key_io.h"
#include "main.h"
#include "protocol.h"
#include "serialize.h"
#include "sync.h"
#include "util.h"
#include "utiltime.h"
#include "wallet/wallet.h"
#include "zcash/Proof.hpp"
#include "komodo_defs.h"
#include "komodo_bitcoind.h"

#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/thread.hpp>

using namespace std;

static uint64_t nAccountingEntryNumber = 0;
static list<uint256> deadTxns;

//
// CWalletDB
//

bool CWalletDB::WriteName(const string& strAddress, const string& strName)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("name"), strAddress), strName);
}

bool CWalletDB::WriteCryptedName(const string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(make_pair(string("cname"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(make_pair(string("name"), strAddress));
    return true;
}

bool CWalletDB::EraseName(const string& strAddress)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    nWalletDBUpdated++;
    return Erase(make_pair(string("name"), strAddress));
}

bool CWalletDB::EraseCryptedName(const uint256& chash)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    nWalletDBUpdated++;
    return Erase(make_pair(string("cname"), chash));
}

bool CWalletDB::WritePurpose(const string& strAddress, const string& strPurpose)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("purpose"), strAddress), strPurpose);
}

bool CWalletDB::WriteCryptedPurpose(const string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(make_pair(string("cpurpose"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(make_pair(string("purpose"), strAddress));
    return true;
}

bool CWalletDB::ErasePurpose(const string& strPurpose)
{
    nWalletDBUpdated++;
    return Erase(make_pair(string("purpose"), strPurpose));
}

bool CWalletDB::EraseCryptedPurpose(const uint256& chash)
{
    nWalletDBUpdated++;
    return Erase(make_pair(string("cpurpose"), chash));
}

//Begin Sapling Address book
bool CWalletDB::WriteSaplingName(const string& strAddress, const string& strName)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("zname"), strAddress), strName);
}

bool CWalletDB::WriteCryptedSaplingName(const string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(make_pair(string("czname"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(make_pair(string("zname"), strAddress));
    return true;
}

bool CWalletDB::EraseSaplingName(const string& strAddress)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    nWalletDBUpdated++;
    return Erase(make_pair(string("zname"), strAddress));
}

bool CWalletDB::EraseCryptedSaplingName(const uint256& chash)
{
    // This should only be used for sending addresses, never for receiving addresses,
    // receiving addresses must always have an address book entry if they're not change return.
    nWalletDBUpdated++;
    return Erase(make_pair(string("czname"), chash));
}

bool CWalletDB::WriteSaplingPurpose(const string& strAddress, const string& strPurpose)
{
    nWalletDBUpdated++;
    return Write(make_pair(string("zpurpose"), strAddress), strPurpose);
}

bool CWalletDB::WriteCryptedSaplingPurpose(const string& strAddress, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(make_pair(string("czpurpose"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(make_pair(string("zpurpose"), strAddress));
    return true;
}

bool CWalletDB::EraseSaplingPurpose(const string& strPurpose)
{
    nWalletDBUpdated++;
    return Erase(make_pair(string("zpurpose"), strPurpose));
}

bool CWalletDB::EraseCryptedSaplingPurpose(const uint256& chash)
{
    nWalletDBUpdated++;
    return Erase(make_pair(string("czpurpose"), chash));
}

//Begin Historical Wallet Tx
bool CWalletDB::WriteArcTx(uint256 hash, ArchiveTxPoint arcTxPoint, bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        return WriteTxn(std::make_pair(std::string("arctx"), hash), arcTxPoint, __FUNCTION__);
    } else {
        return Write(std::make_pair(std::string("arctx"), hash), arcTxPoint);
    }
}

bool CWalletDB::WriteCryptedArcTx(uint256 txid, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        if (!WriteTxn(std::make_pair(std::string("carctx"), chash), vchCryptedSecret, __FUNCTION__)) {
            return false;
        }
    } else {
        if (!Write(std::make_pair(std::string("carctx"), chash), vchCryptedSecret)) {
            return false;
        }
    }

    Erase(std::make_pair(std::string("arctx"), txid));

    return true;
}

bool CWalletDB::EraseArcTx(uint256 hash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("arctx"), hash));
}

bool CWalletDB::EraseCryptedArcTx(uint256 hash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("carctx"), hash));
}

bool CWalletDB::WriteArcSaplingOp(uint256 nullifier, SaplingOutPoint op, bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        if (!WriteTxn(std::make_pair(std::string("arczsop"), nullifier), op, __FUNCTION__)) {
            return false;
        }
    } else {
        if (!Write(std::make_pair(std::string("arczsop"), nullifier), op)) {
            return false;
        }
    }
    return true;
}

bool CWalletDB::WriteCryptedArcSaplingOp(uint256 nullifier, uint256 chash, const std::vector<unsigned char>& vchCryptedSecret, bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        if (!WriteTxn(std::make_pair(std::string("carczsop"), chash), vchCryptedSecret, __FUNCTION__)) {
            return false;
        }
    } else {
        if (!Write(std::make_pair(std::string("carczsop"), chash), vchCryptedSecret)) {
            return false;
        }
    }

    Erase(std::make_pair(std::string("arczsop"), nullifier));
    return true;
}

bool CWalletDB::EraseArcSaplingOp(uint256 nullifier)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("arczsop"), nullifier));
}

bool CWalletDB::EraseCryptedArcSaplingOp(uint256 chash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("carczsop"), chash));
}
//End Historical Wallet Tx

bool CWalletDB::WriteTx(uint256 hash, const CWalletTx& wtx, bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        return WriteTxn(std::make_pair(std::string("tx"), hash), wtx, __FUNCTION__);
    } else {
        return Write(std::make_pair(std::string("tx"), hash), wtx);
    }
}

bool CWalletDB::WriteCryptedTx(
  uint256 txid,
  uint256 hash,
  const std::vector<unsigned char>& vchCryptedSecret,
  bool txnProtected)
{
    nWalletDBUpdated++;
    if (txnProtected) {
        if (!WriteTxn(std::make_pair(std::string("ctx"), hash), vchCryptedSecret, __FUNCTION__)) {
            return false;
        }
    } else {
        if (!Write(std::make_pair(std::string("ctx"), hash), vchCryptedSecret)) {
            return false;
        }
    }

    Erase(std::make_pair(std::string("tx"), txid));

    return true;

}

bool CWalletDB::EraseTx(uint256 hash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("tx"), hash));
}

bool CWalletDB::EraseCryptedTx(uint256 hash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("ctx"), hash));
}

bool CWalletDB::WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("keymeta"), vchPubKey), keyMeta, false))
        return false;

    // hash pubkey/privkey to accelerate wallet load
    std::vector<unsigned char> vchKey;
    vchKey.reserve(vchPubKey.size() + vchPrivKey.size());
    vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
    vchKey.insert(vchKey.end(), vchPrivKey.begin(), vchPrivKey.end());

    return Write(std::make_pair(std::string("key"), vchPubKey), std::make_pair(vchPrivKey, Hash(vchKey.begin(), vchKey.end())), false);
}

bool CWalletDB::WriteCryptedKey(const CPubKey& vchPubKey,
                                const std::vector<unsigned char>& vchCryptedSecret,
                                const uint256 chash,
                                const std::vector<unsigned char> &vchCryptedMetaDataSecret)
{
    const bool fEraseUnencryptedKey = true;
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("ckeymeta"), chash), vchCryptedMetaDataSecret))
        return false;

    if (!Write(std::make_pair(std::string("ckey"), chash), vchCryptedSecret, false))
        return false;
    if (fEraseUnencryptedKey)
    {
        Erase(std::make_pair(std::string("key"), vchPubKey));
        Erase(std::make_pair(std::string("wkey"), vchPubKey));
        Erase(std::make_pair(std::string("keymeta"), vchPubKey));
    }
    return true;
}

bool CWalletDB::WriteCryptedZKey(const libzcash::SproutPaymentAddress & addr,
                                 const libzcash::ReceivingKey &rk,
                                 const std::vector<unsigned char>& vchCryptedSecret,
                                 const CKeyMetadata &keyMeta)
{
    const bool fEraseUnencryptedKey = true;
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("zkeymeta"), addr), keyMeta))
        return false;

    if (!Write(std::make_pair(std::string("czkey"), addr), std::make_pair(rk, vchCryptedSecret), false))
        return false;
    if (fEraseUnencryptedKey)
    {
        Erase(std::make_pair(std::string("zkey"), addr));
    }
    return true;
}

bool CWalletDB::WriteCryptedSaplingZKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    const std::vector<unsigned char>& vchCryptedSecret,
    const std::vector<unsigned char>& vchCryptedMetaDataSecret)
{
    const bool fEraseUnencryptedKey = true;
    nWalletDBUpdated++;
    auto ivk = extfvk.fvk.in_viewing_key();
    uint256 extfvkFinger = extfvk.fvk.GetFingerprint();

    if (!WriteTxn(std::make_pair(std::string("csapzkeymeta"), extfvkFinger), vchCryptedMetaDataSecret, __FUNCTION__))
        return false;

    if (!WriteTxn(std::make_pair(std::string("csapzkey"), extfvkFinger), vchCryptedSecret, __FUNCTION__, false))
        return false;

    if (fEraseUnencryptedKey)
    {
      //Update the key to something invalid before deleting it, so any if the record ends up in the slack space it contains an invalid key
        if (!WriteTxn(std::make_pair(std::string("sapzkey"), ivk), 0, __FUNCTION__))
            return false;

        Erase(std::make_pair(std::string("sapzkey"), ivk));
        Erase(std::make_pair(std::string("sapzkeymeta"), ivk));

        //Erase the extended fullviewing key record when it's corresponding spending key is added
        Erase(std::make_pair(std::string("sapextfvk"), extfvk));
        Erase(std::make_pair(std::string("csapextfvk"), extfvkFinger));
    }
    return true;
}

bool CWalletDB::WriteCryptedSaplingExtendedFullViewingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk,
    const std::vector<unsigned char>& vchCryptedSecret)
{
    const bool fEraseUnencryptedKey = true;
    nWalletDBUpdated++;
    uint256 extfvkFinger = extfvk.fvk.GetFingerprint();

    if (!WriteTxn(std::make_pair(std::string("csapextfvk"), extfvkFinger), vchCryptedSecret, __FUNCTION__))
      return false;

    if (fEraseUnencryptedKey)
    {
        Erase(std::make_pair(std::string("sapextfvk"), extfvk));
    }
    return true;
}

bool CWalletDB::WriteCryptedPrimarySaplingSpendingKey(
    const libzcash::SaplingExtendedSpendingKey &extsk,
    const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    uint256 extfvkFinger = extsk.ToXFVK().fvk.GetFingerprint();

    if (!WriteTxn(std::string("cpspendkey"), std::make_pair(extfvkFinger, vchCryptedSecret), __FUNCTION__, true)) {
        return false;
    }

    Erase(std::string("pspendkey"));

    return true;
}

bool CWalletDB::WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey)
{
    nWalletDBUpdated++;
    return WriteTxn(std::make_pair(std::string("mkey"), nID), kMasterKey, __FUNCTION__, true);
}

bool CWalletDB::WriteZKey(const libzcash::SproutPaymentAddress& addr, const libzcash::SproutSpendingKey& key, const CKeyMetadata &keyMeta)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("zkeymeta"), addr), keyMeta))
        return false;

    // pair is: tuple_key("zkey", paymentaddress) --> secretkey
    return Write(std::make_pair(std::string("zkey"), addr), key, false);
}
bool CWalletDB::WriteSaplingZKey(const libzcash::SaplingIncomingViewingKey &ivk,
                const libzcash::SaplingExtendedSpendingKey &key,
                const CKeyMetadata &keyMeta)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("sapzkeymeta"), ivk), keyMeta))
        return false;

    return Write(std::make_pair(std::string("sapzkey"), ivk), key, false);

    //Erase the extended full viewing key record when it's corresponding spending key is added
    Erase(std::make_pair(std::string("sapextfvk"), key.ToXFVK()));
}

bool CWalletDB::WriteSaplingPaymentAddress(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const libzcash::SaplingPaymentAddress &addr)
{
    nWalletDBUpdated++;

    return Write(std::make_pair(std::string("sapzaddr"), addr), ivk);
}

bool CWalletDB::WriteCryptedSaplingPaymentAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const uint256 chash,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("csapzaddr"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(std::make_pair(std::string("sapzaddr"), addr));
    return true;
}

bool CWalletDB::WriteSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    nWalletDBUpdated++;

    return Write(std::make_pair(std::string("sapzdivaddr"), addr), std::make_pair(ivk, path));
}

bool CWalletDB::WriteCryptedSaplingDiversifiedAddress(
    const libzcash::SaplingPaymentAddress &addr,
    const uint256 chash,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("csapzdivaddr"), chash), vchCryptedSecret, false)) {
        return false;
    }

    Erase(std::make_pair(std::string("sapzdivaddr"), addr));
    return true;
}

bool CWalletDB::WriteLastDiversifierUsed(
    const libzcash::SaplingIncomingViewingKey &ivk,
    const blob88 &path)
{
    nWalletDBUpdated++;

    return Write(std::make_pair(std::string("sapzlastdiv"), ivk), path);
}

bool CWalletDB::WriteLastCryptedDiversifierUsed(
    const uint256 chash,
    const libzcash::SaplingIncomingViewingKey &ivk,
    const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("csapzlastdiv"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(std::make_pair(std::string("sapzlastdiv"), ivk));
    return true;
}

bool CWalletDB::WritePrimarySaplingSpendingKey(
    const libzcash::SaplingExtendedSpendingKey &key)
{
    nWalletDBUpdated++;
    return Write(std::string("pspendkey"), key);
}

bool CWalletDB::WriteSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("vkey"), vk), '1');
}

bool CWalletDB::EraseSproutViewingKey(const libzcash::SproutViewingKey &vk)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("vkey"), vk));
}

bool CWalletDB::WriteSaplingExtendedFullViewingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("sapextfvk"), extfvk), '1');
}

bool CWalletDB::EraseSaplingExtendedFullViewingKey(
    const libzcash::SaplingExtendedFullViewingKey &extfvk)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("sapextfvk"), extfvk));
}

bool CWalletDB::WriteSaplingWitnesses(const SaplingWallet& wallet) {
    nWalletDBUpdated++;
    return Write(
            std::string("sapling_note_commitment_tree"),
            SaplingWalletNoteCommitmentTreeWriter(wallet));
}


bool CWalletDB::WriteCScript(const uint160& hash, const CScript& redeemScript)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("cscript"), hash), *(const CScriptBase*)(&redeemScript), false);
}

bool CWalletDB::WriteCryptedCScript(const uint256 chash, const uint160& hash, const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;

    if (!Write(std::make_pair(std::string("ccscript"), chash), vchCryptedSecret, false)) {
        return false;
    }

    Erase(std::make_pair(std::string("cscript"), hash));
    return true;
}

bool CWalletDB::WriteWatchOnly(const CScript &dest)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("watchs"), *(const CScriptBase*)(&dest)), '1');
}

bool CWalletDB::WriteCryptedWatchOnly(const uint256 chash, const CScript &dest, const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(std::make_pair(std::string("cwatchs"), chash), vchCryptedSecret)) {
        return false;
    }

    Erase(std::make_pair(std::string("watchs"), *(const CScriptBase*)(&dest)));
    return true;
}

bool CWalletDB::EraseWatchOnly(const CScript &dest)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("watchs"), *(const CScriptBase*)(&dest)));
}

bool CWalletDB::EraseCryptedWatchOnly(const uint256 chash)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("cwatchs"), chash));
}

bool CWalletDB::WriteIsCrypted(const bool &crypted)
{
    nWalletDBUpdated++;
    return Write(std::string("iscrypted"), crypted);
}

bool CWalletDB::WriteWalletBirthday(const int& nHeight)
{
    nWalletDBUpdated++;
    return Write(std::string("walletbirthday"), nHeight);
}

bool CWalletDB::ReadWalletBirthday(int& nHeight)
{
    return Read(std::string("walletbirthday"), nHeight);
}

bool CWalletDB::WriteWalletBip39Enabled(const bool& enabled)
{
    nWalletDBUpdated++;
    return Write(std::string("walletbip39enabled"), enabled);
}

bool CWalletDB::ReadWalletBip39Enabled(bool& enabled)
{
    return Read(std::string("walletbip39enabled"), enabled);
}

bool CWalletDB::WriteBestBlock(const CBlockLocator& locator)
{
    nWalletDBUpdated++;
    return Write(std::string("bestblock"), locator);
}

bool CWalletDB::ReadBestBlock(CBlockLocator& locator)
{
    return Read(std::string("bestblock"), locator);
}

bool CWalletDB::WriteOrderPosNext(int64_t nOrderPosNext)
{
    nWalletDBUpdated++;
    return Write(std::string("orderposnext"), nOrderPosNext);
}

bool CWalletDB::WriteDefaultKey(const CPubKey& vchPubKey)
{
    nWalletDBUpdated++;
    return Write(std::string("defaultkey"), vchPubKey);
}

bool CWalletDB::WriteCryptedDefaultKey(const uint256 chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(std::string("cdefaultkey"), make_pair(chash, vchCryptedSecret))) {
        return false;
    }
    Erase(std::string("defaultkey"));
    return true;
}

bool CWalletDB::ReadPool(int64_t nPool, CKeyPool& keypool)
{
    return Read(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::ReadCryptedPool(int64_t nPool, std::pair<uint256, std::vector<unsigned char>> &vchCryptedSecretPair)
{
    return Read(std::make_pair(std::string("cpool"), nPool), vchCryptedSecretPair);
}

bool CWalletDB::WritePool(int64_t nPool, const CKeyPool& keypool)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::WriteCryptedPool(int64_t nPool, const uint256 chash, const std::vector<unsigned char> &vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!Write(std::make_pair(std::string("cpool"), nPool), std::make_pair(chash, vchCryptedSecret))) {
        return false;
    }

    Erase(std::make_pair(std::string("pool"), nPool));
    return true;
}

bool CWalletDB::ErasePool(int64_t nPool)
{
    nWalletDBUpdated++;
    if (!Erase(std::make_pair(std::string("pool"), nPool))) {
        return false;
    }
    if (!Erase(std::make_pair(std::string("cpool"), nPool))) {
        return false;
    }
    return true;
}

bool CWalletDB::WriteMinVersion(int nVersion)
{
    return Write(std::string("minversion"), nVersion);
}

bool CWalletDB::ReadAccount(const string& strAccount, CAccount& account)
{
    account.SetNull();
    return Read(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccount(const string& strAccount, const CAccount& account)
{
    return Write(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry)
{
    return Write(std::make_pair(std::string("acentry"), std::make_pair(acentry.strAccount, nAccEntryNum)), acentry);
}

bool CWalletDB::WriteAccountingEntry(const CAccountingEntry& acentry)
{
    return WriteAccountingEntry(++nAccountingEntryNumber, acentry);
}

CAmount CWalletDB::GetAccountCreditDebit(const string& strAccount)
{
    list<CAccountingEntry> entries;
    ListAccountCreditDebit(strAccount, entries);

    CAmount nCreditDebit = 0;
    BOOST_FOREACH (const CAccountingEntry& entry, entries)
        nCreditDebit += entry.nCreditDebit;

    return nCreditDebit;
}

void CWalletDB::ListAccountCreditDebit(const string& strAccount, list<CAccountingEntry>& entries)
{
    return;
}


class CWalletScanState {
public:
    unsigned int nKeys;
    unsigned int nCKeys;
    unsigned int nKeyMeta;
    unsigned int nZKeys;
    unsigned int nCZKeys;
    unsigned int nZKeyMeta;
    unsigned int nCZKeyMeta;
    unsigned int nSapZAddrs;
    unsigned int nCSapZAddrs;
    unsigned int nArcTx;
    unsigned int nWalletTx;
    bool fIsEncrypted;
    bool fAnyUnordered;
    int nFileVersion;
    vector<uint256> vWalletUpgrade;

    CWalletScanState() {
        nKeys = nCKeys = nKeyMeta = nZKeys = nCZKeys = nZKeyMeta = nCZKeyMeta = nSapZAddrs = nCSapZAddrs = 0;
        nArcTx = nWalletTx = 0;
        fIsEncrypted = false;
        fAnyUnordered = false;
        nFileVersion = 0;
    }
};

bool
ReadKeyValue(CWallet* pwallet, CDataStream& ssKey, CDataStream& ssValue,
             CWalletScanState &wss, string& strType, string& strErr)
{
    try {
        // Unserialize
        // Taking advantage of the fact that pair serialization
        // is just the two items serialized one after the other
        ssKey >> strType;

        //General Wallet Info
        if (strType == "hdseed") // encypted type is chdseed
        {
            uint256 seedFp;
            RawHDSeed rawSeed;
            ssKey >> seedFp;
            ssValue >> rawSeed;
            HDSeed seed(rawSeed);

            if (seed.Fingerprint() != seedFp)
            {
                strErr = "Error reading wallet database: HDSeed corrupt";
                return false;
            }

            if (!pwallet->LoadHDSeed(seed))
            {
                strErr = "Error reading wallet database: LoadHDSeed failed";
                return false;
            }
        }
        else if (strType == "hdchain") //no need to encrypt
        {
            CHDChain chain;
            ssValue >> chain;
            pwallet->SetHDChain(chain, true);
        }
        else if (strType == "version") //no need to encrypt
        {
            ssValue >> wss.nFileVersion;
            if (wss.nFileVersion == 10300)
                wss.nFileVersion = 300;
        }
        //End general wallet data records

        //Begin Transaction Data records
        else if (strType == "orderposnext") //no need to encrypt
        {
            ssValue >> pwallet->nOrderPosNext;
        }

        else if (strType == "tx" || strType == "ctx") //ctx is encrypted tx
        {

            if (nMaxConnections > 0) {
                uint256 hash;
                CWalletTx wtx;

                if (strType == "tx") {
                    ssKey >> hash;
                    ssValue >> wtx;
                } else {
                    uint256 chash;
                    ssKey >> chash;
                    vector<unsigned char> vchCryptedSecret;
                    ssValue >> vchCryptedSecret;

                    if (!pwallet->DecryptWalletTransaction(chash, vchCryptedSecret, hash, wtx))
                    {
                        strErr = "Error reading wallet database: DecryptWalletTransaction failed";
                        return false;
                    }
                }

                CValidationState state;
                auto verifier = libzcash::ProofVerifier::Strict();
                // ac_public chains set at height like KMD and ZEX, will force a rescan if we dont ignore this error: bad-txns-acpublic-chain
                // there cannot be any ztx in the wallet on ac_public chains that started from block 1, so this wont affect those.
                // PIRATE fails this check for notary nodes, need exception. Triggers full rescan without it.
                if ( !(CheckTransaction(0,wtx, state, verifier, 0, 0) && (wtx.GetHash() == hash) && state.IsValid()) && (state.GetRejectReason() != "bad-txns-acpublic-chain" && state.GetRejectReason() != "bad-txns-acprivacy-chain" && state.GetRejectReason() != "bad-txns-stakingtx") )
                {
                    //fprintf(stderr, "tx failed: %s rejectreason.%s\n", wtx.GetHash().GetHex().c_str(), state.GetRejectReason().c_str());
                    // vin-empty on staking chains is error relating to a failed staking tx, that for some unknown reason did not fully erase. save them here to erase and re-add later on.
                    if ( ASSETCHAINS_STAKED != 0 && state.GetRejectReason() == "bad-txns-vin-empty" )
                        deadTxns.push_back(hash);
                    return false;
                }
                // Undo serialize changes in 31600
                if (31404 <= wtx.fTimeReceivedIsTxTime && wtx.fTimeReceivedIsTxTime <= 31703)
                {
                    if (!ssValue.empty())
                    {
                        char fTmp;
                        char fUnused;
                        ssValue >> fTmp >> fUnused >> wtx.strFromAccount;
                        strErr = strprintf("LoadWallet() upgrading tx ver=%d %d '%s' %s",
                                           wtx.fTimeReceivedIsTxTime, fTmp, wtx.strFromAccount, hash.ToString());
                        wtx.fTimeReceivedIsTxTime = fTmp;
                    }
                    else
                    {
                        strErr = strprintf("LoadWallet() repairing tx ver=%d %s", wtx.fTimeReceivedIsTxTime, hash.ToString());
                        wtx.fTimeReceivedIsTxTime = 0;
                    }
                    wss.vWalletUpgrade.push_back(hash);
                }

                if (wtx.nOrderPos == -1)
                    wss.fAnyUnordered = true;

                wss.nWalletTx++;
                pwallet->AddToWallet(wtx, true, NULL, 0);
            }
        }
        else if (strType == "arctx" || strType == "carctx") //carctx is encrypted arctx
        {
            //The ArchiveTxPoint structure was changed. An older version will fail
            //to deserialize and not be added to the mapArcTx, triggering a full
            //ZapWalletTxes and Rescan.
            if (nMaxConnections > 0) {
                try
                {
                    uint256 txid;
                    ArchiveTxPoint arcTxPt;
                    if (strType == "arctx") {
                        ssKey >> txid;
                        ssValue >> arcTxPt;
                    } else {
                        uint256 chash;
                        ssKey >> chash;
                        vector<unsigned char> vchCryptedSecret;
                        ssValue >> vchCryptedSecret;

                        if (!pwallet->DecryptWalletArchiveTransaction(chash, vchCryptedSecret, txid, arcTxPt))
                        {
                            strErr = "Error reading wallet database: DecryptWalletArchiveTransaction failed";
                            return false;
                        }
                    }

                    wss.nArcTx++;
                    pwallet->LoadArcTxs(txid, arcTxPt);
                }
                catch (...) {}
            }

        }
        else if (strType == "arczsop" || strType == "carczsop") //carczsop is encrypted arczsop
        {
            if (nMaxConnections > 0) {
                uint256 nullifier;
                SaplingOutPoint op;
                if (strType == "arczsop") {
                    ssKey >> nullifier;
                    ssValue >> op;
                } else {
                    uint256 chash;
                    ssKey >> chash;
                    vector<unsigned char> vchCryptedSecret;
                    ssValue >> vchCryptedSecret;

                    if (!pwallet->DecryptArchivedSaplingOutpoint(chash, vchCryptedSecret, nullifier, op))
                    {
                        strErr = "Error reading wallet database: DecryptArchivedSaplingOutpoint failed";
                        return false;
                    }
                }

                pwallet->AddToArcSaplingOutPoints(nullifier, op);
            }
        }
        //End transaction data records

        //Begin t-address data records
        else if (strType == "name")
        {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].name;
        }
        else if (strType =="cname")
        {
            string strAddress;
            string strName;
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            pwallet->DecryptAddressBookEntry(chash, vchCryptedSecret, strAddress, strName);
            if (strName == "None")
                strName = "";

            pwallet->mapAddressBook[DecodeDestination(strAddress)].name = strName;
        }
        else if (strType == "purpose")
        {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].purpose;
        }
        else if (strType =="cpurpose")
        {
          string strAddress;
          string strPurpose;
          uint256 chash;
          ssKey >> chash;
          vector<unsigned char> vchCryptedSecret;
          ssValue >> vchCryptedSecret;

          pwallet->DecryptAddressBookEntry(chash, vchCryptedSecret, strAddress, strPurpose);
          if (strPurpose == "None")
              strPurpose = "";

          pwallet->mapAddressBook[DecodeDestination(strAddress)].purpose = strPurpose;
        }
        else if (strType == "watchs")
        {
            CScript script;
            ssKey >> *(CScriptBase*)(&script);
            char fYes;
            ssValue >> fYes;
            if (fYes == '1')
                pwallet->LoadWatchOnly(script);

            // Watch-only addresses have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        }
        else if (strType == "cwatchs")
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->LoadCryptedWatchOnly(chash, vchCryptedSecret)) {
                strErr = "Error reading wallet database: LoadCryptedWatchOnly failed";
                return false;
            }

            // Watch-only addresses have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        }
        else if (strType == "cscript")
        {
            uint160 hash;
            ssKey >> hash;
            CScript script;
            ssValue >> *(CScriptBase*)(&script);

            if (!pwallet->LoadCScript(script))
            {
                strErr = "Error reading wallet database: LoadCScript failed";
                return false;
            }
        }
        else if (strType == "ccscript")
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->LoadCryptedCScript(chash, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedCScript failed";
                return false;
            }
        }
        else if (strType == "key" || strType == "wkey") //encrypt type of key is ckey
        {
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            if (!vchPubKey.IsValid())
            {
                strErr = "Error reading wallet database: CPubKey corrupt";
                return false;
            }
            CKey key;
            CPrivKey pkey;
            uint256 hash;

            if (strType == "key")
            {
                wss.nKeys++;
                ssValue >> pkey;
            } else {
                CWalletKey wkey;
                ssValue >> wkey;
                pkey = wkey.vchPrivKey;
            }

            // Old wallets store keys as "key" [pubkey] => [privkey]
            // ... which was slow for wallets with lots of keys, because the public key is re-derived from the private key
            // using EC operations as a checksum.
            // Newer wallets store keys as "key"[pubkey] => [privkey][hash(pubkey,privkey)], which is much faster while
            // remaining backwards-compatible.
            try
            {
                ssValue >> hash;
            }
            catch (...) {}

            bool fSkipCheck = false;

            if (!hash.IsNull())
            {
                // hash pubkey/privkey to accelerate wallet load
                std::vector<unsigned char> vchKey;
                vchKey.reserve(vchPubKey.size() + pkey.size());
                vchKey.insert(vchKey.end(), vchPubKey.begin(), vchPubKey.end());
                vchKey.insert(vchKey.end(), pkey.begin(), pkey.end());

                if (Hash(vchKey.begin(), vchKey.end()) != hash)
                {
                    strErr = "Error reading wallet database: CPubKey/CPrivKey corrupt";
                    return false;
                }

                fSkipCheck = true;
            }

            if (!key.Load(pkey, vchPubKey, fSkipCheck))
            {
                strErr = "Error reading wallet database: CPrivKey corrupt";
                return false;
            }
            if (!pwallet->LoadKey(key, vchPubKey))
            {
                strErr = "Error reading wallet database: LoadKey failed";
                return false;
            }
        }
        else if (strType == "ckey") //ckey is encrypt key (transparent)
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;
            wss.nCKeys++;

            if (!pwallet->LoadCryptedKey(chash, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedKey failed";
                return false;
            }
            wss.fIsEncrypted = true;
        }
        else if (strType == "keymeta")
        {
            CPubKey vchPubKey;
            ssKey >> vchPubKey;
            CKeyMetadata keyMeta;
            ssValue >> keyMeta;
            wss.nKeyMeta++;

            pwallet->LoadKeyMetadata(vchPubKey, keyMeta);

            // find earliest key creation time, as wallet birthday
            if (!pwallet->nTimeFirstKey ||
                (keyMeta.nCreateTime < pwallet->nTimeFirstKey))
                pwallet->nTimeFirstKey = keyMeta.nCreateTime;
        }

        else if (strType == "ckeymeta")
        {
            CKeyMetadata keyMeta;
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            wss.nKeyMeta++;

            pwallet->LoadCryptedKeyMetadata(chash, vchCryptedSecret, keyMeta);

            // find earliest key creation time, as wallet birthday
            if (!pwallet->nTimeFirstKey ||
                (keyMeta.nCreateTime < pwallet->nTimeFirstKey))
                pwallet->nTimeFirstKey = keyMeta.nCreateTime;
        }

        else if (strType == "defaultkey")
        {
            ssValue >> pwallet->vchDefaultKey;
        }
        else if (strType == "cdefaultkey")
        {
            CPubKey vchPubKey;
            uint256 chash;
            ssValue >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->DecryptDefaultKey(chash, vchCryptedSecret, vchPubKey)) {
              strErr = "Error reading wallet database: DecryptDefaultKey failed";
              return false;
            }

            pwallet->vchDefaultKey = vchPubKey;
        }
        else if (strType == "pool")
        {
            int64_t nIndex;
            ssKey >> nIndex;
            CKeyPool keypool;
            ssValue >> keypool;
            pwallet->setKeyPool.insert(nIndex);

            // If no metadata exists yet, create a default with the pool key's
            // creation time. Note that this may be overwritten by actually
            // stored metadata for that key later, which is fine.
            CKeyID keyid = keypool.vchPubKey.GetID();
            if (pwallet->mapKeyMetadata.count(keyid) == 0)
                pwallet->mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
        }
        //End transparent data records


        //Sapling Address data
        else if (strType == "zname")
        {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapZAddressBook[DecodePaymentAddress(strAddress)].name;
        }
        else if (strType =="czname")
        {
            string strAddress;
            string strName;
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            pwallet->DecryptAddressBookEntry(chash, vchCryptedSecret, strAddress, strName);
            if (strName == "None")
                strName = "";

            pwallet->mapZAddressBook[DecodePaymentAddress(strAddress)].name = strName;
        }
        else if (strType == "zpurpose")
        {
            string strAddress;
            ssKey >> strAddress;
            ssValue >> pwallet->mapZAddressBook[DecodePaymentAddress(strAddress)].purpose;
        }
        else if (strType =="czpurpose")
        {
          string strAddress;
          string strPurpose;
          uint256 chash;
          ssKey >> chash;
          vector<unsigned char> vchCryptedSecret;
          ssValue >> vchCryptedSecret;

          pwallet->DecryptAddressBookEntry(chash, vchCryptedSecret, strAddress, strPurpose);
          if (strPurpose == "None")
              strPurpose = "";

          pwallet->mapZAddressBook[DecodePaymentAddress(strAddress)].purpose = strPurpose;
        }
        else if (strType == "sapzkey")
        {
            libzcash::SaplingIncomingViewingKey ivk;
            ssKey >> ivk;
            libzcash::SaplingExtendedSpendingKey key;
            ssValue >> key;

            if (!pwallet->LoadSaplingZKey(key))
            {
                strErr = "Error reading wallet database: LoadSaplingZKey failed";
                return false;
            }

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(key.DefaultAddress());
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[key.DefaultAddress()].name = "z-sapling";
                pwallet->mapZAddressBook[key.DefaultAddress()].purpose = "unknown";
            }

            //add checks for integrity
            wss.nZKeys++;
        }
        else if (strType == "csapzkey")
        {
            uint256 extfvkFinger;
            ssKey >> extfvkFinger;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;
            libzcash::SaplingExtendedFullViewingKey extfvk;
            wss.nCZKeys++;

            if (!pwallet->LoadCryptedSaplingZKey(extfvkFinger, vchCryptedSecret, extfvk))
            {
                strErr = "Error reading wallet database: LoadCryptedSaplingZKey failed";
                return false;
            }

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(extfvk.DefaultAddress());
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].name = "z-sapling";
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].purpose = "unknown";
            }

            wss.fIsEncrypted = true;
        }
        else if (strType == "sapextfvk")
        {
            libzcash::SaplingExtendedFullViewingKey extfvk;
            ssKey >> extfvk;
            char fYes;
            ssValue >> fYes;
            if (fYes == '1') {
                if(!pwallet->LoadSaplingFullViewingKey(extfvk))
                {
                    strErr = "Error reading wallet database: LoadSaplingFullViewingKey failed";
                    return false;
                }
                if(!pwallet->LoadSaplingWatchOnly(extfvk))
                {
                    strErr = "Error reading wallet database: LoadSaplingWatchOnly failed";
                    return false;
                }
            }

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(extfvk.DefaultAddress());
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].name = "z-sapling";
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].purpose = "unknown";
            }

            // Viewing keys have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        }
        else if (strType == "csapextfvk")
        {
            uint256 extfvkFinger;
            ssKey >> extfvkFinger;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;
            libzcash::SaplingExtendedFullViewingKey extfvk;
            if (!pwallet->LoadCryptedSaplingExtendedFullViewingKey(extfvkFinger, vchCryptedSecret, extfvk))
            {
                strErr = "Error reading wallet database: LoadCryptedSaplingExtendedFullViewingKey failed";
                return false;
            }

            pwallet->LoadSaplingWatchOnly(extfvk);

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(extfvk.DefaultAddress());
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].name = "z-sapling";
                pwallet->mapZAddressBook[extfvk.DefaultAddress()].purpose = "unknown";
            }

            // Viewing keys have no birthday information for now,
            // so set the wallet birthday to the beginning of time.
            pwallet->nTimeFirstKey = 1;
        }
        else if (strType == "sapzkeymeta")
        {
            libzcash::SaplingIncomingViewingKey ivk;
            ssKey >> ivk;
            CKeyMetadata keyMeta;
            ssValue >> keyMeta;

            wss.nZKeyMeta++;

            pwallet->LoadSaplingZKeyMetadata(ivk, keyMeta);
        }
        else if (strType == "csapzkeymeta")
        {
            uint256 extfvkFinger;
            ssKey >> extfvkFinger;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            wss.nCZKeyMeta++;

            pwallet->mapTempHoldCryptedSaplingMetadata[extfvkFinger] = vchCryptedSecret;
        }
        else if (strType == "sapzaddr")
        {
            libzcash::SaplingPaymentAddress addr;
            ssKey >> addr;
            libzcash::SaplingIncomingViewingKey ivk;
            ssValue >> ivk;

            wss.nSapZAddrs++;

            if (!pwallet->LoadSaplingPaymentAddress(addr, ivk))
            {
                strErr = "Error reading wallet database: LoadSaplingPaymentAddress failed";
                return false;
            }

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(addr);
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[addr].name = "z-sapling";
                pwallet->mapZAddressBook[addr].purpose = "unknown";
            }
        }
        else if (strType == "csapzaddr")
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;
            libzcash::SaplingPaymentAddress addr;

            wss.nCSapZAddrs++;

            if (!pwallet->LoadCryptedSaplingPaymentAddress(chash, vchCryptedSecret, addr))
            {
                strErr = "Error reading wallet database: LoadCryptedSaplingPaymentAddress failed";
                return false;
            }

            //Insert if not found, don't overwrite if found
            auto r = pwallet->mapZAddressBook.find(addr);
            if (r == pwallet->mapZAddressBook.end()) {
                pwallet->mapZAddressBook[addr].name = "z-sapling";
                pwallet->mapZAddressBook[addr].purpose = "unknown";
            }
        }
        else if (strType == "sapzdivaddr")
        {
            libzcash::SaplingPaymentAddress addr;
            ssKey >> addr;
            DiversifierPath dPath;
            ssValue >> dPath;

            if (!pwallet->LoadSaplingDiversifiedAddress(addr, dPath.first, dPath.second))
            {
                strErr = "Error reading wallet database: LoadSaplingDiversifiedAddress failed";
                return false;
            }
        }
        else if (strType == "csapzdivaddr")
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->LoadCryptedSaplingDiversifiedAddress(chash, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedSaplingDiversifiedAddress failed";
                return false;
            }
        }
        else if (strType == "pspendkey")
        {
            libzcash::SaplingExtendedSpendingKey key;
            ssValue >> key;

            pwallet->primarySaplingSpendingKey = key;
        }
        else if (strType == "cpspendkey")
        {
            uint256 extfvkFinger;
            ssValue >> extfvkFinger;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->LoadCryptedPrimarySaplingSpendingKey(extfvkFinger, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedPrimarySaplingSpendingKey failed";
                return false;
            }
        }

        else if (strType == "sapzlastdiv")
        {
            libzcash::SaplingIncomingViewingKey ivk;
            ssKey >> ivk;
            blob88 path;
            ssValue >> path;

            if (!pwallet->LoadLastDiversifierUsed(ivk, path))
            {
                strErr = "Error reading wallet database: LoadLastDiversifierUsed failed";
                return false;
            }
        }
        else if (strType == "csapzlastdiv")
        {
            uint256 chash;
            ssKey >> chash;
            vector<unsigned char> vchCryptedSecret;
            ssValue >> vchCryptedSecret;

            if (!pwallet->LoadLastCryptedDiversifierUsed(chash, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadLastDiversifierUsed failed";
                return false;
            }
        }
        else if (strType == "sapling_note_commitment_tree")
        {
            auto loader = pwallet->GetSaplingNoteCommitmentTreeLoader();
            ssValue >> loader;
        }

    } catch (...)
    {
        return false;
    }
    return true;
}

static bool IsKeyType(string strType)
{
    return (strType == "key" || strType == "wkey" ||
            strType == "hdseed" || strType == "chdseed" ||
            strType == "zkey" || strType == "czkey" ||
            strType == "sapzkey" || strType == "csapzkey" ||
            strType == "vkey" ||
            strType == "mkey" || strType == "ckey");
}

bool ReadCryptedSeedValue(CWallet* pwallet, CDataStream& ssKey, CDataStream& ssValue,
             CWalletScanState &wss, string& strType, string& strErr)
{
    try {
        // Unserialize
        // Taking advantage of the fact that pair serialization
        // is just the two items serialized one after the other
        ssKey >> strType;
        if (strType == "chdseed")
        {
            uint256 chash;
            vector<unsigned char> vchCryptedSecret;
            ssKey >> chash;
            ssValue >> vchCryptedSecret;
            if (!pwallet->LoadCryptedHDSeed(chash, vchCryptedSecret))
            {
                strErr = "Error reading wallet database: LoadCryptedHDSeed failed";
                return false;
            }
            wss.fIsEncrypted = true;
        }
        else if (strType == "mkey")
        {
            unsigned int nID;
            ssKey >> nID;
            CMasterKey kMasterKey;
            ssValue >> kMasterKey;
            if(pwallet->mapMasterKeys.count(nID) != 0)
            {
                strErr = strprintf("Error reading wallet database: duplicate CMasterKey id %u", nID);
                return false;
            }
            pwallet->mapMasterKeys[nID] = kMasterKey;
            if (pwallet->nMasterKeyMaxID < nID)
                pwallet->nMasterKeyMaxID = nID;
        }
    } catch (...)
    {
        return false;
    }
    return true;
}

DBErrors CWalletDB::InitalizeCryptedLoad(CWallet* pwallet) {
    LOCK(pwallet->cs_wallet);
    bool isCrypted = false;

    if (Read((string)"iscrypted", isCrypted)) {
        return DB_LOAD_CRYPTED;
    }
    return DB_LOAD_OK;
}

DBErrors CWalletDB::LoadCryptedSeedFromDB(CWallet* pwallet) {
    LOCK(pwallet->cs_wallet);
    CWalletScanState wss;

    // Get cursor
    Dbc* pcursor = GetCursor();
    if (!pcursor)
    {
        LogPrintf("Error getting wallet database cursor\n");
        return DB_CORRUPT;
    }


    while (true)
    {
        // Read next record
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        int ret = ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND)
            break;
        else if (ret != 0)
        {
            LogPrintf("Error reading next record from wallet database\n");
            return DB_CORRUPT;
        }


        string strType, strErr;
        if (!ReadCryptedSeedValue(pwallet, ssKey, ssValue, wss, strType, strErr))
        {
            LogPrintf("DB_Corrupt reading crypted seed\n");
            return DB_CORRUPT;
        }

    }
    pcursor->close();
    return DB_LOAD_OK;
}


DBErrors CWalletDB::LoadWallet(CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    pwallet->vchDefaultKey = CPubKey();
    CWalletScanState wss;
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;

    try {
        int nMinVersion = 0;
        if (Read((string)"minversion", nMinVersion))
        {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
            pwallet->LoadMinVersion(nMinVersion);
        }

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor)
        {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true)
        {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
            {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            // Try to be tolerant of single corrupt records:
            string strType, strErr;
            if (!ReadKeyValue(pwallet, ssKey, ssValue, wss, strType, strErr))
            {
                // losing keys is considered a catastrophic error, anything else
                // we assume the user can live with:
                if (IsKeyType(strType))
                    result = DB_CORRUPT;
                else
                {
                    // Leave other errors alone, if we try to fix them we might make things worse.
                    fNoncriticalErrors = true; // ... but do warn the user there is something wrong.
                    // set rescan for any error that is not vin-empty on staking chains.
                    if ( deadTxns.empty() && strType == "tx")
                        SoftSetBoolArg("-rescan", true);
                }
            }
            if (!strErr.empty())
                LogPrintf("%s\n", strErr);
        }
        pcursor->close();
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (...) {
        result = DB_CORRUPT;
    }

    if(!pwallet->LoadTempHeldCryptedData()) {
        LogPrintf("Loading Temp Held crypted data failed!!!\n");
    }

    if ( !deadTxns.empty() )
    {
        // staking chains with vin-empty error is a failed staking tx.
        // we remove then re add the tx here to stop needing a full rescan, which does not actually fix the problem.
        int32_t reAdded = 0;
        BOOST_FOREACH (uint256& hash, deadTxns)
        {
            fprintf(stderr, "Removing possible orphaned staking transaction from wallet.%s\n", hash.ToString().c_str());
            if (!pwallet->EraseFromWallet(hash))
                fprintf(stderr, "could not delete tx.%s\n",hash.ToString().c_str());
            uint256 blockhash; CTransaction tx; CBlockIndex* pindex;
            if ( GetTransaction(hash,tx,blockhash,false) && (pindex= komodo_blockindex(blockhash)) != 0 && chainActive.Contains(pindex) )
            {
                CWalletTx wtx(pwallet,tx);
                pwallet->AddToWallet(wtx, true, NULL, 0);
                reAdded++;
            }
        }
        fprintf(stderr, "Cleared %li orphaned staking transactions from wallet. Readded %i real transactions.\n",deadTxns.size(),reAdded);
        fNoncriticalErrors = false;
        deadTxns.clear();
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    // Any wallet corruption at all: skip any rewriting or
    // upgrading, we don't want to make it worse.
    if (result != DB_LOAD_OK)
        return result;

    LogPrintf("nFileVersion = %d\n", wss.nFileVersion);

    LogPrintf("Keys: %u plaintext, %u encrypted, %u w/ metadata, %u total\n",
           wss.nKeys, wss.nCKeys, wss.nKeyMeta, wss.nKeys + wss.nCKeys);

    LogPrintf("ZKeys: %u plaintext, %u w/metadata\n",
           wss.nZKeys, wss.nZKeyMeta);

     LogPrintf("ZKeys: %u encrypted, %u w/encrypted metadata\n",
           wss.nCZKeys, wss.nCZKeyMeta);

    LogPrintf("Sapling Addresses: %u \n",wss.nSapZAddrs);
    LogPrintf("Encrypted Sapling Addresses: %u \n",wss.nCSapZAddrs);
    LogPrintf("Transactions: %u wallet transactions, %u archived transactions\n", wss.nWalletTx, wss.nArcTx);

    // nTimeFirstKey is only reliable if all keys have metadata
    if ((wss.nKeys + wss.nCKeys) != wss.nKeyMeta)
        pwallet->nTimeFirstKey = 1; // 0 would be considered 'no value'

    BOOST_FOREACH(uint256 hash, wss.vWalletUpgrade)
        WriteTx(hash, pwallet->mapWallet[hash], true);

    // Rewrite encrypted wallets of versions 0.4.0 and 0.5.0rc:
    if (wss.fIsEncrypted && (wss.nFileVersion == 40000 || wss.nFileVersion == 50000))
        return DB_NEED_REWRITE;

    if (wss.nFileVersion < CLIENT_VERSION) // Update
        WriteVersion(CLIENT_VERSION);

    return result;
}

DBErrors CWalletDB::FindWalletTxToZap(
  CWallet* pwallet, vector<uint256>& vTxHash,
  vector<CWalletTx>& vWtx, vector<uint256>& vCTxHash,
  vector<uint256>& vArcHash, vector<uint256>& vCArcHash,
  vector<uint256>& vArcSaplingNullifier, vector<uint256>& vCArcSaplingNullifier)
{
    pwallet->vchDefaultKey = CPubKey();
    bool fNoncriticalErrors = false;
    DBErrors result = DB_LOAD_OK;

    try {
        LOCK(pwallet->cs_wallet);
        int nMinVersion = 0;
        if (Read((string)"minversion", nMinVersion))
        {
            if (nMinVersion > CLIENT_VERSION)
                return DB_TOO_NEW;
            pwallet->LoadMinVersion(nMinVersion);
        }

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor)
        {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true)
        {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
            {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            string strType;
            ssKey >> strType;
            if (strType == "tx") {
                uint256 hash;
                ssKey >> hash;
                vTxHash.push_back(hash);
            } if (strType == "ctx") {
                uint256 hash;
                ssKey >> hash;
                vCTxHash.push_back(hash);
            } if (strType == "arctx") {
                uint256 hash;
                ssKey >> hash;
                vArcHash.push_back(hash);
            } if (strType == "carctx") {
                uint256 hash;
                ssKey >> hash;
                vCArcHash.push_back(hash);
            } if (strType == "arczsop") {
                uint256 nullifier;
                ssKey >> nullifier;
                vArcSaplingNullifier.push_back(nullifier);
            } if (strType == "carczsop") {
                uint256 hash;
                ssKey >> hash;
                vCArcSaplingNullifier.push_back(hash);
            }
        }
        pcursor->close();
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (...) {
        result = DB_CORRUPT;
    }

    if (fNoncriticalErrors && result == DB_LOAD_OK)
        result = DB_NONCRITICAL_ERROR;

    return result;
}

DBErrors CWalletDB::ZapWalletTx(CWallet* pwallet, vector<CWalletTx>& vWtx)
{
    // build list of wallet TXs
    vector<uint256> vTxHash;
    vector<uint256> vCTxHash;
    vector<uint256> vArcTxHash;
    vector<uint256> vCArcTxHash;
    vector<uint256> vArcSaplingNullifier;
    vector<uint256> vCArcSaplingNullifier;
    DBErrors err = FindWalletTxToZap(pwallet, vTxHash, vWtx, vCTxHash, vArcTxHash, vCArcTxHash, vArcSaplingNullifier, vCArcSaplingNullifier);
    if (err != DB_LOAD_OK)
        return err;

    // erase each wallet TX
    BOOST_FOREACH (uint256& hash, vTxHash) {
        if (!EraseTx(hash))
            return DB_CORRUPT;
    }

    // erase each crypted wallet TX
    BOOST_FOREACH (uint256& hash, vCTxHash) {
        if (!EraseCryptedTx(hash))
            return DB_CORRUPT;
    }

    // erase each archive TX
    BOOST_FOREACH (uint256& arcHash, vArcTxHash) {
        if (!EraseArcTx(arcHash))
            return DB_CORRUPT;
    }

    // erase each crypted archive TX
    BOOST_FOREACH (uint256& arcHash, vCArcTxHash) {
        if (!EraseCryptedArcTx(arcHash))
            return DB_CORRUPT;
    }
    // erase each archive Nullier SaplingOutput set
    BOOST_FOREACH (uint256& arcNullifier, vArcSaplingNullifier) {
        if (!EraseArcSaplingOp(arcNullifier))
            return DB_CORRUPT;
    }

    // erase each crypted archive Nullier SaplingOutput set
    BOOST_FOREACH (uint256& arcNullifier, vCArcSaplingNullifier) {
        if (!EraseCryptedArcSaplingOp(arcNullifier))
            return DB_CORRUPT;
    }
    return DB_LOAD_OK;
}

DBErrors CWalletDB::FindOldRecordsToZap(
  CWallet* pwallet,
  vector<uint256>& vArcSproutNullifier,
  vector<libzcash::SproutViewingKey>& vSproutViewingKeys,
  vector<libzcash::SproutPaymentAddress>& vSproutPaymentAddresses,
  vector<libzcash::SproutPaymentAddress>& vCSproutPaymentAddresses,
  vector<libzcash::SproutPaymentAddress>& vSproutMetaData)
{

    DBErrors result = DB_LOAD_OK;

    try {
        LOCK(pwallet->cs_wallet);

        // Get cursor
        Dbc* pcursor = GetCursor();
        if (!pcursor)
        {
            LogPrintf("Error getting wallet database cursor\n");
            return DB_CORRUPT;
        }

        while (true)
        {
            // Read next record
            CDataStream ssKey(SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(SER_DISK, CLIENT_VERSION);
            int ret = ReadAtCursor(pcursor, ssKey, ssValue);
            if (ret == DB_NOTFOUND)
                break;
            else if (ret != 0)
            {
                LogPrintf("Error reading next record from wallet database\n");
                return DB_CORRUPT;
            }

            string strType;
            ssKey >> strType;
            if (strType == "arczcop") {
                uint256 nullifier;
                ssKey >> nullifier;
                vArcSproutNullifier.push_back(nullifier);
            } else if (strType == "vkey") {
                libzcash::SproutViewingKey vk;
                ssKey >> vk;
                vSproutViewingKeys.push_back(vk);
            } else if (strType == "zkey") {
                libzcash::SproutPaymentAddress addr;
                ssKey >> addr;
                vSproutPaymentAddresses.push_back(addr);
            } else if (strType == "czkey") {
                libzcash::SproutPaymentAddress addr;
                ssKey >> addr;
                vCSproutPaymentAddresses.push_back(addr);
            } else if (strType == "zkeymeta") {
                libzcash::SproutPaymentAddress addr;
                ssKey >> addr;
                vCSproutPaymentAddresses.push_back(addr);
            }


        }
        pcursor->close();
    }
    catch (const boost::thread_interrupted&) {
        throw;
    }
    catch (...) {
        result = DB_CORRUPT;
    }

    return result;
}

DBErrors CWalletDB::ZapOldRecords(CWallet* pwallet)
{
    // build list of obsolete records
    vector<uint256> vArcSproutNullifier;
    vector<libzcash::SproutViewingKey> vSproutViewingKeys;
    vector<libzcash::SproutPaymentAddress> vSproutPaymentAddresses;
    vector<libzcash::SproutPaymentAddress> vCSproutPaymentAddresses;
    vector<libzcash::SproutPaymentAddress> vSproutMetaData;


    LOCK(pwallet->cs_wallet);

    DBErrors err = FindOldRecordsToZap(pwallet
                                    , vArcSproutNullifier
                                    , vSproutViewingKeys
                                    , vSproutPaymentAddresses
                                    , vCSproutPaymentAddresses
                                    , vSproutMetaData);
    if (err != DB_LOAD_OK)
        return err;


    // erase each archive Nullier Sprout Output set
    BOOST_FOREACH (uint256& arcNullifier, vArcSproutNullifier) {
        if (!Erase(std::make_pair(std::string("arczcop"), arcNullifier))) {
            return DB_CORRUPT;
        }
    }

    // erase each sprout viewing key
    BOOST_FOREACH (libzcash::SproutViewingKey& sproutViewingKey, vSproutViewingKeys) {
        if (!Erase(std::make_pair(std::string("vkey"), sproutViewingKey))) {
            return DB_CORRUPT;
        }
    }

    // erase each sprout spending key
    BOOST_FOREACH (libzcash::SproutPaymentAddress& sproutPaymentAddress, vSproutPaymentAddresses) {
        if (!Erase(std::make_pair(std::string("zkey"), sproutPaymentAddress))) {
            return DB_CORRUPT;
        }
    }

    // erase each crypted sprout spending key
    BOOST_FOREACH (libzcash::SproutPaymentAddress& sproutPaymentAddress, vCSproutPaymentAddresses) {
        if (!Erase(std::make_pair(std::string("czkey"), sproutPaymentAddress))) {
            return DB_CORRUPT;
        }
    }

    // erase each sprout spending key metadata
    BOOST_FOREACH (libzcash::SproutPaymentAddress& sproutPaymentAddress, vSproutMetaData) {
        if (!Erase(std::make_pair(std::string("zkeymeta"), sproutPaymentAddress))) {
            return DB_CORRUPT;
        }
    }

    if(!Erase(std::string("witnesscachesize"))) {
        return DB_CORRUPT;
    }

    return DB_LOAD_OK;
}

void ThreadFlushWalletDB(const string& strFile)
{
    // Make this thread recognisable as the wallet flushing thread
    RenameThread("zcash-wallet");

    static bool fOneThread;
    if (fOneThread)
        return;
    fOneThread = true;
    if (!GetBoolArg("-flushwallet", true))
        return;

    unsigned int nLastSeen = nWalletDBUpdated;
    unsigned int nLastFlushed = nWalletDBUpdated;
    int64_t nLastWalletUpdate = GetTime();
    while (true)
    {
        MilliSleep(500);

        int* aborted = 0;
        int ret = bitdb->dbenv->lock_detect(0, DB_LOCK_EXPIRE, aborted);
        if (ret != 0) {
            LogPrintf("DB Lock detection, %d\n", ret);
        }

        if (nLastSeen != nWalletDBUpdated)
        {
            nLastSeen = nWalletDBUpdated;
            nLastWalletUpdate = GetTime();
        }

        if (nLastFlushed != nWalletDBUpdated && GetTime() - nLastWalletUpdate >= 2)
        {
            TRY_LOCK(bitdb->cs_db,lockDb);
            if (lockDb)
            {
                // Don't do this if any databases are in use
                int nRefCount = 0;
                map<string, int>::iterator mi = bitdb->mapFileUseCount.begin();
                while (mi != bitdb->mapFileUseCount.end())
                {
                    nRefCount += (*mi).second;
                    mi++;
                }

                if (nRefCount == 0)
                {
                    boost::this_thread::interruption_point();
                    map<string, int>::iterator mi = bitdb->mapFileUseCount.find(strFile);
                    if (mi != bitdb->mapFileUseCount.end())
                    {
                        LogPrint("db", "Flushing wallet.dat\n");
                        nLastFlushed = nWalletDBUpdated;
                        int64_t nStart = GetTimeMillis();

                        // Flush wallet.dat so it's self contained
                        bitdb->CloseDb(strFile);
                        bitdb->CheckpointLSN(strFile);

                        bitdb->mapFileUseCount.erase(mi++);
                        LogPrint("db", "Flushed wallet.dat %dms\n", GetTimeMillis() - nStart);
                    }
                }
            }
        }
    }
}

bool BackupWallet(const CWallet& wallet, const string& strDest)
{
    if (!wallet.fFileBacked)
        return false;
    while (true)
    {
        {
            LOCK(bitdb->cs_db);
            if (!bitdb->mapFileUseCount.count(wallet.strWalletFile) || bitdb->mapFileUseCount[wallet.strWalletFile] == 0)
            {
                // Flush log data to the dat file
                bitdb->CloseDb(wallet.strWalletFile);
                bitdb->CheckpointLSN(wallet.strWalletFile);
                bitdb->mapFileUseCount.erase(wallet.strWalletFile);

                // Copy wallet.dat
                boost::filesystem::path pathSrc = GetDataDir() / wallet.strWalletFile;
                boost::filesystem::path pathDest(strDest);
                if (boost::filesystem::is_directory(pathDest))
                    pathDest /= wallet.strWalletFile;

                try {
                    boost::filesystem::copy_file(pathSrc, pathDest, boost::filesystem::copy_option::overwrite_if_exists);
                    LogPrintf("copied wallet.dat to %s\n", pathDest.string());
                    return true;
                } catch (const boost::filesystem::filesystem_error& e) {
                    LogPrintf("error copying wallet.dat to %s - %s\n", pathDest.string(), e.what());
                    return false;
                }
            }
        }
        MilliSleep(100);
    }
    return false;
}

bool CWalletDB::Compact(CDBEnv& dbenv, const std::string& strFile)
{
  bool fSuccess = dbenv.Compact(strFile);
  return fSuccess;
}
//
// Try to (very carefully!) recover wallet.dat if there is a problem.
//
bool CWalletDB::Recover(CDBEnv& dbenv, const std::string& filename, bool fOnlyKeys)
{
    // Recovery procedure:
    // move wallet.dat to wallet.timestamp.bak
    // Call Salvage with fAggressive=true to
    // get as much data as possible.
    // Rewrite salvaged data to wallet.dat
    // Set -rescan so any missing transactions will be
    // found.
    int64_t now = GetTime();
    std::string newFilename = strprintf("wallet.%d.bak", now);

    int result = dbenv.dbenv->dbrename(NULL, filename.c_str(), NULL,
                                       newFilename.c_str(), DB_AUTO_COMMIT);
    if (result == 0)
        LogPrintf("Renamed %s to %s\n", filename, newFilename);
    else
    {
        LogPrintf("Failed to rename %s to %s\n", filename, newFilename);
        return false;
    }

    std::vector<CDBEnv::KeyValPair> salvagedData;
    bool fSuccess = dbenv.Salvage(newFilename, true, salvagedData);
    if (salvagedData.empty())
    {
        LogPrintf("Salvage(aggressive) found no records in %s.\n", newFilename);
        return false;
    }
    LogPrintf("Salvage(aggressive) found %u records\n", salvagedData.size());

    boost::scoped_ptr<Db> pdbCopy(new Db(dbenv.dbenv, 0));
    int ret = pdbCopy->open(NULL,               // Txn pointer
                            filename.c_str(),   // Filename
                            "main",             // Logical db name
                            DB_BTREE,           // Database type
                            DB_CREATE,          // Flags
                            0);
    if (ret > 0)
    {
        LogPrintf("Cannot create database file %s\n", filename);
        return false;
    }
    CWallet dummyWallet;
    CWalletScanState wss;

    DbTxn* ptxn = dbenv.TxnBegin();
    BOOST_FOREACH(CDBEnv::KeyValPair& row, salvagedData)
    {
        if (fOnlyKeys)
        {
            CDataStream ssKey(row.first, SER_DISK, CLIENT_VERSION);
            CDataStream ssValue(row.second, SER_DISK, CLIENT_VERSION);
            string strType, strErr;
            bool fReadOK = ReadKeyValue(&dummyWallet, ssKey, ssValue,
                                        wss, strType, strErr);
            if (!IsKeyType(strType))
                continue;
            if (!fReadOK)
            {
                LogPrintf("WARNING: CWalletDB::Recover skipping %s: %s\n", strType, strErr);
                continue;
            }
        }
        Dbt datKey(&row.first[0], row.first.size());
        Dbt datValue(&row.second[0], row.second.size());
        int ret2 = pdbCopy->put(ptxn, &datKey, &datValue, DB_NOOVERWRITE);
        if (ret2 > 0)
            fSuccess = false;
    }
    ptxn->commit(0);
    pdbCopy->close(0);

    return fSuccess;
}

bool CWalletDB::Recover(CDBEnv& dbenv, const std::string& filename)
{
    return CWalletDB::Recover(dbenv, filename, false);
}

bool CWalletDB::WriteDestData(const std::string &address, const std::string &key, const std::string &value)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("destdata"), std::make_pair(address, key)), value);
}

bool CWalletDB::EraseDestData(const std::string &address, const std::string &key)
{
    nWalletDBUpdated++;
    return Erase(std::make_pair(std::string("destdata"), std::make_pair(address, key)));
}


bool CWalletDB::WriteHDSeed(const HDSeed& seed)
{
    nWalletDBUpdated++;
    return Write(std::make_pair(std::string("hdseed"), seed.Fingerprint()), seed.RawSeed());
}

bool CWalletDB::WriteCryptedHDSeed(const uint256& seedFp, const uint256& chash, const std::vector<unsigned char>& vchCryptedSecret)
{
    nWalletDBUpdated++;
    if (!WriteTxn(std::make_pair(std::string("chdseed"), chash), vchCryptedSecret, __FUNCTION__)) {
        return false;
    }

    Erase(std::make_pair(std::string("hdseed"), seedFp));

    return true;
}

bool CWalletDB::WriteHDChain(const CHDChain& chain)
{
    nWalletDBUpdated++;
    return Write(std::string("hdchain"), chain);
}
