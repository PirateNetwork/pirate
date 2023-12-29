// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "walletmodel.h"

#include "addresstablemodel.h"
#include "zaddresstablemodel.h"
#include "consensus/validation.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "paymentserver.h"
#include "recentrequeststablemodel.h"
//#include "sendcoinsdialog.h"
#include "zsendcoinsdialog.h"
#include "transactiontablemodel.h"
#include "importkeydialog.h"

#include "init.h"
#include "base58.h"
#include "chain.h"
#include "keystore.h"
#include "net.h" // for g_connman
#include "policy/fees.h"
#include "sync.h"
#include "ui_interface.h"
#include "util.h" // for GetBoolArg
#include "coincontrol.h"
#include "main.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h" // for BackupWallet
#include "key_io.h"
#include "komodo_defs.h"
#include "utilmoneystr.h"
#include "asyncrpcoperation.h"
#include "asyncrpcqueue.h"
#include "rpc/server.h"

#include <stdint.h>

#include <QDebug>
#include <QMessageBox>
#include <QSet>
#include <QTimer>
#include <QSettings>

/* from rpcwallet.cpp */
extern CAmount getBalanceTaddr(std::string transparentAddress, int minDepth=1, bool ignoreUnspendable=true);
extern uint64_t komodo_interestsum();

extern char ASSETCHAINS_SYMBOL[KOMODO_ASSETCHAIN_MAXLEN];
extern int nMaxConnections;          // from net.cpp

// JSDescription size depends on the transaction version
#define V3_JS_DESCRIPTION_SIZE    (GetSerializeSize(JSDescription(), SER_NETWORK, (OVERWINTER_TX_VERSION | (1 << 31))))
// Here we define the maximum number of zaddr outputs that can be included in a transaction.
// If input notes are small, we might actually require more than one joinsplit per zaddr output.
// For now though, we assume we use one joinsplit per zaddr output (and the second output note is change).
// We reduce the result by 1 to ensure there is room for non-joinsplit CTransaction data.
#define Z_SENDMANY_MAX_ZADDR_OUTPUTS_BEFORE_SAPLING    ((MAX_TX_SIZE_BEFORE_SAPLING / V3_JS_DESCRIPTION_SIZE) - 1)

// transaction.h comment: spending taddr output requires CTxIn >= 148 bytes and typical taddr txout is 34 bytes
#define CTXIN_SPEND_DUST_SIZE   148
#define CTXOUT_REGULAR_SIZE     34

WalletModel::WalletModel(const PlatformStyle *platformStyle, CWallet *_wallet, OptionsModel *_optionsModel, QObject *parent) :
    QObject(parent), wallet(_wallet), optionsModel(_optionsModel), addressTableModel(0), zaddressTableModel(0),
    transactionTableModel(0),
    recentRequestsTableModel(0),
    cachedBalance(0), cachedUnconfirmedBalance(0), cachedImmatureBalance(0),
    cachedWatchPrivateBalance(0), cachedPrivateBalance(0), cachedInterestBalance(0),
    cachedEncryptionStatus(Unencrypted),
    cachedNumBlocks(0)
{
    fHaveWatchOnly = wallet->HaveWatchOnly();
    fForceCheckBalanceChanged = false;

    addressTableModel = new AddressTableModel(platformStyle, wallet, this);
    zaddressTableModel = new ZAddressTableModel(platformStyle, wallet, this);
    transactionTableModel = new TransactionTableModel(platformStyle, wallet, this);
    recentRequestsTableModel = new RecentRequestsTableModel(wallet, this);

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(pollBalanceChanged()));
    pollTimer->start(MODEL_UPDATE_DELAY);

    subscribeToCoreSignals();
}

WalletModel::~WalletModel()
{
    unsubscribeFromCoreSignals();
}

CAmount WalletModel::getBalance(const CCoinControl *coinControl) const
{
    if (coinControl)
    {
        return wallet->GetAvailableBalance(coinControl);
    }

    return wallet->GetBalance();
}

CAmount WalletModel::getUnconfirmedBalance() const
{
    return wallet->GetUnconfirmedBalance() + getBalanceZaddr("", 0, true) - getBalanceZaddr("", 1, true);
}

CAmount WalletModel::getImmatureBalance() const
{
    return wallet->GetImmatureBalance();
}

CAmount WalletModel::getPrivateBalance() const
{
    return getBalanceZaddr("", 1, true);
}

CAmount WalletModel::getPrivateWatchBalance() const
{
    return getBalanceZaddr("", 1, false) - getBalanceZaddr("", 1, true);
}

CAmount WalletModel::getInterestBalance() const
{
    return (chainName.isKMD()) ? komodo_interestsum() : 0;
}

bool WalletModel::haveWatchOnly() const
{
    return fHaveWatchOnly;
}

CAmount WalletModel::getWatchBalance() const
{
    return wallet->GetWatchOnlyBalance();
}

CAmount WalletModel::getWatchUnconfirmedBalance() const
{
    CAmount balance = wallet->GetUnconfirmedWatchOnlyBalance();
     balance += getBalanceZaddr("", 0, false) - getBalanceZaddr("", 1, false);
     balance -= (getBalanceZaddr("", 0, true) - getBalanceZaddr("", 1, true));
    return balance;
}

CAmount WalletModel::getWatchImmatureBalance() const
{
    return wallet->GetImmatureWatchOnlyBalance();
}

void WalletModel::updateStatus()
{
    EncryptionStatus newEncryptionStatus = getEncryptionStatus();

    if(cachedEncryptionStatus != newEncryptionStatus)
        Q_EMIT encryptionStatusChanged(newEncryptionStatus);
}


QString WalletModel::getSpendingKey(QString strAddress) {
      LOCK2(cs_main, wallet->cs_wallet);

      QMessageBox msgBox;
      msgBox.setText("Error exporting key.");
      msgBox.setStyleSheet("QLabel{min-width: 350px;}");
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Ok);

      EncryptionStatus encryptionStatus = getEncryptionStatus();

      if (encryptionStatus == Busy) {
          //Should never happen
          msgBox.setText("Encryption Status: Busy");
          msgBox.setInformativeText("Keys cannot be displayed while the wallet is Busy, try again in a few minutes.");
          int ret = msgBox.exec();
          return QString("");
      }

      if (encryptionStatus == Locked) {
          requireUnlock();
      }

      if (getEncryptionStatus() == Locked) {
        msgBox.setText("Encryption Status: Locked");
        msgBox.setInformativeText("Keys cannot be displayed while the wallet is Locked.");
        int ret = msgBox.exec();
        return QString("");
      }

      auto address = DecodePaymentAddress(strAddress.toStdString());
      if (!IsValidPaymentAddress(address)) {
          msgBox.setInformativeText("Invalid Z-Address!!!");
          int ret = msgBox.exec();
          return QString("");
      }

      // Sapling support
      auto sk = boost::apply_visitor(GetSpendingKeyForPaymentAddress(wallet), address);
      if (!sk) {
          msgBox.setInformativeText("Wallet does not hold private zkey for this zaddrs!!!");
          int ret = msgBox.exec();
          return QString("");
      }
      return QString::fromStdString(EncodeSpendingKey(sk.get()));
}

void WalletModel::importSpendingKey(QString strKey) {
    LOCK2(cs_main, wallet->cs_wallet);

    QMessageBox msgBox;
    msgBox.setText("Error importing key.");
    msgBox.setStyleSheet("QLabel{min-width: 350px;}");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);

    auto sk = DecodeSpendingKey(strKey.toStdString());
    if (!IsValidSpendingKey(sk)) {
      msgBox.setInformativeText("Invalid Key!!!");
      int ret = msgBox.exec();
      return;
    }

    auto addResult = boost::apply_visitor(AddSpendingKeyToWallet(wallet, Params().GetConsensus()), sk);
    if (addResult == KeyAlreadyExists) {
        msgBox.setInformativeText("Key already exists!!!");
        int ret = msgBox.exec();
        return;
    }

    wallet->MarkDirty();
    if (addResult == KeyNotAdded) {
      msgBox.setInformativeText("Key not added!!!");
      int ret = msgBox.exec();
      return;
    }

    startedRescan = true;

    //Add to ZAddress book
    auto zInfo = boost::apply_visitor(libzcash::AddressInfoFromSpendingKey{}, sk);
    wallet->SetZAddressBook(zInfo.second, zInfo.first, "");

    updateZAddressBook(QString::fromStdString(EncodePaymentAddress(zInfo.second)), "z-sapling", true, "", CT_NEW);

    // whenever a key is imported, we need to scan the whole chain
    wallet->nTimeFirstKey = 1;

    // We want to scan for transactions and notes
    wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, false, true);
}

QString WalletModel::getViewingKey(QString strAddress) {
      LOCK2(cs_main, wallet->cs_wallet);

      QMessageBox msgBox;
      msgBox.setText("Error exporting key.");
      msgBox.setStyleSheet("QLabel{min-width: 350px;}");
      msgBox.setStandardButtons(QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Ok);

      EncryptionStatus encryptionStatus = getEncryptionStatus();

      if (encryptionStatus == Busy) {
          //Should never happen
          msgBox.setText("Encryption Status: Busy");
          msgBox.setInformativeText("Keys cannot be displayed while the wallet is Busy, try again in a few minutes.");
          int ret = msgBox.exec();
          return QString("");
      }

      if (encryptionStatus == Locked) {
          requireUnlock();
      }

      if (getEncryptionStatus() == Locked) {
        msgBox.setText("Encryption Status: Locked");
        msgBox.setInformativeText("Keys cannot be displayed while the wallet is Locked.");
        int ret = msgBox.exec();
        return QString("");
      }

      auto address = DecodePaymentAddress(strAddress.toStdString());
      if (!IsValidPaymentAddress(address)) {
          msgBox.setInformativeText("Invalid Z-Address!!!");
          int ret = msgBox.exec();
          return QString("");
      }

      // Sapling support
      auto vk = boost::apply_visitor(GetViewingKeyForPaymentAddress(wallet), address);
      if (!vk) {
          msgBox.setInformativeText("Wallet does not hold private key or viewing key for this zaddr!!!");
          int ret = msgBox.exec();
          return QString("");
      }

      return QString::fromStdString(EncodeViewingKey(vk.get()));
}

void WalletModel::importViewingKey (QString strKey) {
    LOCK2(cs_main, wallet->cs_wallet);

    QMessageBox msgBox;
    msgBox.setText("Error importing key.");
    msgBox.setStyleSheet("QLabel{min-width: 350px;}");
    msgBox.setStandardButtons(QMessageBox::Ok);
    msgBox.setDefaultButton(QMessageBox::Ok);

    auto extfvk = DecodeViewingKey(strKey.toStdString());
    if (!IsValidViewingKey(extfvk)) {
        msgBox.setInformativeText("Invalid Key!!!");
        int ret = msgBox.exec();
        return;
    }

    auto addResult = boost::apply_visitor(AddViewingKeyToWallet(wallet), extfvk);
    if (addResult == KeyAlreadyExists || addResult == SpendingKeyExists) {
        msgBox.setInformativeText("Key already exists!!!");
        int ret = msgBox.exec();
        return;
    }

    wallet->MarkDirty();
    if (addResult == KeyNotAdded) {
        msgBox.setInformativeText("Key not added!!!");
        int ret = msgBox.exec();
        return;
    }

    startedRescan = true;

    //Add to ZAddress book
    auto zInfo = boost::apply_visitor(libzcash::AddressInfoFromViewingKey{}, extfvk);
    wallet->SetZAddressBook(zInfo.second, zInfo.first, "");
    // const QString zaddr = QString::fromStdString(EncodePaymentAddress(zInfo.second)),

    updateZAddressBook(QString::fromStdString(EncodePaymentAddress(zInfo.second)), "z-sapling", false, "", CT_NEW);

    // whenever a key is imported, we need to scan the whole chain
    wallet->nTimeFirstKey = 1;

    // We want to scan for transactions and notes
    wallet->ScanForWalletTransactions(chainActive.Genesis(), true, true, false, true);
}

void WalletModel::pollBalanceChanged()
{
    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(cs_main, lockMain);
    if(!lockMain)
        return;
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet)
        return;

    if(fForceCheckBalanceChanged || chainActive.Height() != cachedNumBlocks)
    {
        fForceCheckBalanceChanged = false;

        // Balance and number of transactions might have changed
        cachedNumBlocks = chainActive.Height();

        checkBalanceChanged();
        if(transactionTableModel)
            transactionTableModel->updateConfirmations();
    }
}

void WalletModel::checkBalanceChanged()
{
    if (nMaxConnections==0) {
        //Don't check balances in cold storage offline mode
        return;
    }

    CAmount newBalance = getBalance();
    CAmount newUnconfirmedBalance = getUnconfirmedBalance();
    CAmount newImmatureBalance = getImmatureBalance();
    CAmount newWatchOnlyBalance = 0;
    CAmount newWatchUnconfBalance = 0;
    CAmount newWatchImmatureBalance = 0;
    CAmount newWatchPrivateBalance = 0;
    CAmount newprivateBalance = getBalanceZaddr("", 1, true);
    CAmount newinterestBalance = (chainName.isKMD()) ? komodo_interestsum() : 0;
    if (haveWatchOnly())
    {
        newWatchOnlyBalance = getWatchBalance();
        newWatchUnconfBalance = getWatchUnconfirmedBalance();
        newWatchImmatureBalance = getWatchImmatureBalance();
        newWatchPrivateBalance = getBalanceZaddr("", 1, false) - getBalanceZaddr("", 1, true);
    }

    if(cachedBalance != newBalance || cachedUnconfirmedBalance != newUnconfirmedBalance || cachedImmatureBalance != newImmatureBalance ||
        cachedWatchOnlyBalance != newWatchOnlyBalance || cachedWatchUnconfBalance != newWatchUnconfBalance || cachedWatchImmatureBalance != newWatchImmatureBalance ||
        cachedWatchPrivateBalance != newWatchPrivateBalance || cachedPrivateBalance != newprivateBalance || cachedInterestBalance != newinterestBalance)
    {
        cachedBalance = newBalance;
        cachedUnconfirmedBalance = newUnconfirmedBalance;
        cachedImmatureBalance = newImmatureBalance;
        cachedWatchOnlyBalance = newWatchOnlyBalance;
        cachedWatchUnconfBalance = newWatchUnconfBalance;
        cachedWatchImmatureBalance = newWatchImmatureBalance;
        cachedWatchPrivateBalance = newWatchPrivateBalance;
        cachedPrivateBalance = newprivateBalance;
        cachedInterestBalance = newinterestBalance;
        Q_EMIT balanceChanged(newBalance, newUnconfirmedBalance, newImmatureBalance,
                            newWatchOnlyBalance, newWatchUnconfBalance, newWatchImmatureBalance,
                            newWatchPrivateBalance, newprivateBalance, newinterestBalance);
    }
}

void WalletModel::updateTransaction()
{
    // Balance and number of transactions might have changed
    fForceCheckBalanceChanged = true;
}

void WalletModel::updateAddressBook(const QString &address, const QString &label,
        bool isMine, const QString &purpose, int status)
{
    if(addressTableModel)
        addressTableModel->updateEntry(address, label, isMine, purpose, status);
}

void WalletModel::updateZAddressBook(const QString &address, const QString &label,
        bool isMine, const QString &purpose, int status)
{
    if(zaddressTableModel)
        zaddressTableModel->updateEntry(address, label, isMine, purpose, status);
}

void WalletModel::updateWatchOnlyFlag(bool fHaveWatchonly)
{
    fHaveWatchOnly = fHaveWatchonly;
    Q_EMIT notifyWatchonlyChanged(fHaveWatchonly);
}

bool WalletModel::validateAddress(const QString &address, bool allowZAddresses)
{
    bool validTAddress = IsValidDestinationString(address.toStdString());

    if (validTAddress) return true;

    if (allowZAddresses) return IsValidPaymentAddressString(address.toStdString(), CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus()));

    return false;
}

bool WalletModel::validateMemo(const QString &memo)
{
      string memoValue;
      if (!memo.isNull()) {
          memoValue = memo.toStdString();

          if (!IsHex(memoValue)) {
              memoValue = HexStr(memoValue);
          }
          if (memoValue.length() > ZC_MEMO_SIZE*2) {
            return false;
          }

          std::vector<unsigned char> rawMemo = ParseHex(memoValue.c_str());

          // If ParseHex comes across a non-hex char, it will stop but still return results so far.
          size_t slen = memoValue.length();
          if (slen % 2 !=0 || (slen>0 && rawMemo.size()!=slen/2)) {
              return false;
          }

          if (rawMemo.size() > ZC_MEMO_SIZE) {
            return false;
          }
      }
    return true;
}
/*
WalletModel::SendCoinsReturn WalletModel::prepareTransaction(WalletModelTransaction &transaction, const CCoinControl& coinControl)
{
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;

    if(recipients.empty())
    {
        return OK;
    }

    QSet<QString> setAddress; // Used to detect duplicates
    int nAddresses = 0;

    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp : recipients)
    {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;

        #ifdef ENABLE_BIP70
        if (rcp.paymentRequest.IsInitialized())
        {   // PaymentRequest...
            CAmount subtotal = 0;
            const payments::PaymentDetails& details = rcp.paymentRequest.getDetails();
            for (int i = 0; i < details.outputs_size(); i++)
            {
                const payments::Output& out = details.outputs(i);
                if (out.amount() <= 0) continue;
                subtotal += out.amount();
                const unsigned char* scriptStr = (const unsigned char*)out.script().data();
                CScript scriptPubKey(scriptStr, scriptStr+out.script().size());
                CAmount nAmount = out.amount();
                CRecipient recipient = {scriptPubKey, nAmount, rcp.fSubtractFeeFromAmount};
                vecSend.push_back(recipient);
            }
            if (subtotal <= 0)
            {
                return InvalidAmount;
            }
            total += subtotal;
        }
        else
        #endif
        {   // User-entered komodo address / amount:
            if(!validateAddress(rcp.address))
            {
                return InvalidAddress;
            }
            if(rcp.amount <= 0)
            {
                return InvalidAmount;
            }
            setAddress.insert(rcp.address);
            ++nAddresses;

            CScript scriptPubKey = GetScriptForDestination(DecodeDestination(rcp.address.toStdString()));
            CRecipient recipient = {scriptPubKey, rcp.amount, rcp.fSubtractFeeFromAmount};
            vecSend.push_back(recipient);

            total += rcp.amount;
        }
    }
    if(setAddress.size() != nAddresses)
    {
        return DuplicateAddress;
    }

    CAmount nBalance = getBalance(&coinControl);

    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    {
        LOCK2(cs_main, wallet->cs_wallet);

        transaction.newPossibleKeyChange(wallet);

        CAmount nFeeRequired = 0;
        int nChangePosRet = -1;
        std::string strFailReason;

        CWalletTx *newTx = transaction.getTransaction();
        CReserveKey *keyChange = transaction.getPossibleKeyChange();
        bool fCreated = wallet->CreateTransaction(vecSend, *newTx, *keyChange, nFeeRequired, nChangePosRet, strFailReason, &coinControl);
        transaction.setTransactionFee(nFeeRequired);
        if (fSubtractFeeFromAmount && fCreated)
            transaction.reassignAmounts(nChangePosRet);

        if(!fCreated)
        {
            if(!fSubtractFeeFromAmount && (total + nFeeRequired) > nBalance)
            {
                return SendCoinsReturn(AmountWithFeeExceedsBalance);
            }
            Q_EMIT message(tr("Send Coins"), QString::fromStdString(strFailReason),
                         CClientUIInterface::MSG_ERROR);
            return TransactionCreationFailed;
        }

        // reject absurdly high fee. (This can never happen because the
        // wallet caps the fee at maxTxFee. This merely serves as a
        // belt-and-suspenders check)
        if (nFeeRequired > maxTxFee)
            return AbsurdFee;
    }

    return SendCoinsReturn(OK);
}
*/
WalletModel::SendCoinsReturn WalletModel::prepareZTransaction(WalletModelZTransaction &transaction, const CCoinControl& coinControl)
{
    CAmount total = 0;
    bool fSubtractFeeFromAmount = false;
    QList<SendCoinsRecipient> recipients = transaction.getRecipients();
    std::vector<CRecipient> vecSend;

    if(recipients.empty())
    {
        return OK;
    }

    uint32_t branchId = CurrentEpochBranchId(chainActive.Height(), Params().GetConsensus());

    auto fromaddress = transaction.getFromAddress();
    bool bIsMine     = transaction.getIsMine();
    bool fromTaddr = false;
    bool fromSapling = false;

    CTxDestination taddr = DecodeDestination(fromaddress.toStdString());
    fromTaddr = IsValidDestination(taddr);
    if (!fromTaddr)
    {
        auto res = DecodePaymentAddress(fromaddress.toStdString());

        if (bIsMine==true)
        {
          if (!IsValidPaymentAddress(res, branchId))
          {
             return InvalidFromAddress;
          }

          // Check that we have the spending key
          if (!boost::apply_visitor(HaveSpendingKeyForPaymentAddress(wallet), res))
          {
            return HaveNotSpendingKey;
          }
        }
        // Remember whether this is a Sprout or Sapling address
        fromSapling = boost::get<libzcash::SaplingPaymentAddress>(&res) != nullptr;
    }
    else
    {
      //Only support z-sapling addresses:
      return InvalidFromAddress;
    }

    // This logic will need to be updated if we add a new shielded pool
    bool fromSprout = !(fromTaddr || fromSapling);

    // Track whether we see any Sprout addresses
    bool noSproutAddrs = !fromSprout;

    set<std::string> setAddress; // Used to detect duplicates

    // Recipients
    //std::vector<SendManyRecipient> taddrRecipients;
    std::vector<SendManyRecipient> zaddrRecipients;

    bool containsSproutOutput = false;
    bool containsSaplingOutput = false;

    // Pre-check input data for validity
    for (const SendCoinsRecipient &rcp : recipients)
    {
        if (rcp.fSubtractFeeFromAmount)
            fSubtractFeeFromAmount = true;

        bool isZaddr = false;
        CTxDestination taddr = DecodeDestination(rcp.address.toStdString());

        if (!IsValidDestination(taddr)) {
            auto res = DecodePaymentAddress(rcp.address.toStdString());
            if (IsValidPaymentAddress(res, branchId)) {
                isZaddr = true;

                bool toSapling = boost::get<libzcash::SaplingPaymentAddress>(&res) != nullptr;
                bool toSprout = !toSapling;
                noSproutAddrs = noSproutAddrs && toSapling;

                containsSproutOutput |= toSprout;
                containsSaplingOutput |= toSapling;

                // Sending to both Sprout and Sapling is currently unsupported using z_sendmany
                if (containsSproutOutput && containsSaplingOutput)
                {
                    return SendingBothSproutAndSapling;
                }
                if ( GetTime() > KOMODO_SAPLING_DEADLINE )
                {
                    if ( fromSprout || toSprout )
                        return SproutUsageExpired;
                }
                if ( toSapling && chainName.isKMD() )
                    return SproutUsageWillExpireSoon;

                // If we are sending from a shielded address, all recipient
                // shielded addresses must be of the same type.
                if ((fromSprout && toSapling) || (fromSapling && toSprout))
                {
                    return SendBetweenSproutAndSapling;
                }
            } else {
                return InvalidAddress;
            }
        }

        if(rcp.amount <= 0)
        {
            return InvalidAmount;
        }

        if (setAddress.count(rcp.address.toStdString()))
            return DuplicateAddress;

        setAddress.insert(rcp.address.toStdString());

        string memo;
        if (!rcp.memo.isNull()) {
            memo = rcp.memo.toStdString();
        }

        if (isZaddr)
        {
            zaddrRecipients.push_back( SendManyRecipient(rcp.address.toStdString(), rcp.amount, memo) );
        }
        else
        {
            //taddr not allowed on PirateNetwork
            return InvalidAddress;
        }

        total += rcp.amount;
    }
    CAmount nBalance = getAddressBalance(fromaddress.toStdString());
    if(total > nBalance)
    {
        return AmountExceedsBalance;
    }

    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    unsigned int max_tx_size = MAX_TX_SIZE_AFTER_SAPLING;

    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        if (NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_OVERWINTER)) {
            mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
            mtx.nVersion = OVERWINTER_TX_VERSION;
        } else {
            mtx.fOverwintered = false;
            mtx.nVersion = 2;
        }

        max_tx_size = MAX_TX_SIZE_BEFORE_SAPLING;

        // Check the number of zaddr outputs does not exceed the limit.
        if (zaddrRecipients.size() > Z_SENDMANY_MAX_ZADDR_OUTPUTS_BEFORE_SAPLING)
        {
            return TooManyZaddrs;
        }
    }

    // If Sapling is not active, do not allow sending from or sending to Sapling addresses.
    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UPGRADE_SAPLING)) {
        if (fromSapling || containsSaplingOutput)
        {
            return SaplingHasNotActivated;
        }
    }

    // As a sanity check, estimate and verify that the size of the transaction will be valid.
    // Depending on the input notes, the actual tx size may turn out to be larger and perhaps invalid.
    size_t txsize = 0;
    for (int i = 0; i < zaddrRecipients.size(); i++) {
        auto address = std::get<0>(zaddrRecipients[i]);
        auto res = DecodePaymentAddress(address);
        bool toSapling = boost::get<libzcash::SaplingPaymentAddress>(&res) != nullptr;
        if (toSapling) {
            mtx.vShieldedOutput.push_back(OutputDescription());
        } else {
            JSDescription jsdesc;
            if (mtx.fOverwintered && (mtx.nVersion >= SAPLING_TX_VERSION)) {
                jsdesc.proof = GrothProof();
            }
            mtx.vjoinsplit.push_back(jsdesc);
        }
    }

    CTransaction tx(mtx);
    txsize += GetSerializeSize(tx, SER_NETWORK, tx.nVersion);
    if (txsize > max_tx_size)
    {
        return LargeTransactionSize;
    }

    // Minimum confirmations
    int nMinDepth = 1;

    // Fee in Zatoshis, not currency format)
    CAmount nFee        = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE;
    CAmount nDefaultFee = nFee;


    nFee = transaction.getTransactionFee();
    // Check that the user specified fee is not absurd.
    // This allows amount=0 (and all amount < nDefaultFee) transactions to use the default network fee
    // or anything less than nDefaultFee instead of being forced to use a custom fee and leak metadata
    if (total < nDefaultFee) {
        if (nFee > nDefaultFee)
            return TooLargeFeeForSmallTrans;
    } else {
        // Check that the user specified fee is not absurd.
        if (nFee > total)
            return SendCoinsReturn(TooLargeFee);
    }

    if((total + nFee) > nBalance)
    {
        return SendCoinsReturn(AmountWithFeeExceedsBalance);
    }

    // Use input parameters as the optional context info to be returned by z_getoperationstatus and z_getoperationresult.
    UniValue o(UniValue::VOBJ);
    o.push_back(Pair("fromaddress", fromaddress.toStdString()));
    UniValue o_addrs(UniValue::VOBJ);
    for (const SendCoinsRecipient &rcp : recipients)
    {
        std::string strAddress = rcp.address.toStdString();
        o_addrs.push_back(Pair("address", strAddress));
        o_addrs.push_back(Pair("amount", QString::number(ValueFromAmount(rcp.amount).get_real(),'f',8).toStdString()));
    }

    o.push_back(Pair("amounts", o_addrs));
    o.push_back(Pair("total", std::stod(FormatMoney(total))));
    o.push_back(Pair("minconf", nMinDepth));
    o.push_back(Pair("fee", std::stod(FormatMoney(nFee))));
    UniValue contextInfo = o;

    // Builder (used if Sapling addresses are involved)
    boost::optional<TransactionBuilder> builder;
    if (noSproutAddrs)
    {
        builder = TransactionBuilder(Params().GetConsensus(), nextBlockHeight, wallet);
    }

    // Contextual transaction we will build on
    // (used if no Sapling addresses are involved)
    CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
    bool isShielded = !fromTaddr || zaddrRecipients.size() > 0;
    if (contextualTx.nVersion == 1 && isShielded)
    {
        contextualTx.nVersion = 2; // Tx format should support vjoinsplits
    }
    transaction.setBuilder(builder);
    transaction.setContextualTx(contextualTx);
    transaction.setZaddrRecipients(zaddrRecipients);
    transaction.setContextInfo(contextInfo);

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::sendCoins(WalletModelTransaction &transaction)
{
    QByteArray transaction_array; /* store serialized transaction */

    {
        LOCK2(cs_main, wallet->cs_wallet);
        CWalletTx *newTx = transaction.getTransaction();

        for (const SendCoinsRecipient &rcp : transaction.getRecipients())
        {
            #ifdef ENABLE_BIP70
            if (rcp.paymentRequest.IsInitialized())
            {
                // Make sure any payment requests involved are still valid.
                if (PaymentServer::verifyExpired(rcp.paymentRequest.getDetails())) {
                    return PaymentRequestExpired;
                }

                // Store PaymentRequests in wtx.vOrderForm in wallet.
                std::string key("PaymentRequest");
                std::string value;
                rcp.paymentRequest.SerializeToString(&value);
                newTx->vOrderForm.push_back(make_pair(key, value));
            }
            else
            #endif
                if (!rcp.message.isEmpty()) // Message from normal pirate:URI (pirate:123...?message=example)
                    newTx->vOrderForm.push_back(make_pair("Message", rcp.message.toStdString()));
        }

        CReserveKey *keyChange = transaction.getPossibleKeyChange();
        CValidationState state;
        if(!wallet->CommitTransaction(*newTx, *keyChange))
            return SendCoinsReturn(TransactionCommitFailed, QString::fromStdString(state.GetRejectReason()));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
//        ssTx << *newTx->tx;
        ssTx << *newTx;
        transaction_array.append(&(ssTx[0]), ssTx.size());
    }

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp : transaction.getRecipients())
    {
        // Don't touch the address book when we have a payment request
        #ifdef ENABLE_BIP70
        if (!rcp.paymentRequest.IsInitialized())
        #endif
        {
            std::string strAddress = rcp.address.toStdString();
            CTxDestination dest = DecodeDestination(strAddress);
            std::string strLabel = rcp.label.toStdString();
            {
                LOCK(wallet->cs_wallet);

                std::map<CTxDestination, CAddressBookData>::iterator mi = wallet->mapAddressBook.find(dest);

                // Check if we have a new address or an updated label
                if (mi == wallet->mapAddressBook.end())
                {
                    wallet->SetAddressBook(dest, strLabel, "send");
                }
                else if (mi->second.name != strLabel)
                {
                    wallet->SetAddressBook(dest, strLabel, ""); // "" means don't change purpose
                }
            }
        }
        Q_EMIT coinsSent(wallet, rcp, transaction_array);
    }
    checkBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    return SendCoinsReturn(OK);
}

WalletModel::SendCoinsReturn WalletModel::zsendCoins(WalletModelZTransaction &transaction)
{
    // Create operation and add to global queue
    std::shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    std::shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_sendmany(transaction.getBuilder(),
                                                                                 transaction.getContextualTx(),
                                                                                 transaction.getFromAddress().toStdString(),
                                                                                 transaction.getTaddrRecipients(),
                                                                                 transaction.getZaddrRecipients(),
                                                                                 1,
                                                                                 transaction.getTransactionFee(),
                                                                                 transaction.getContextInfo()) );
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();

    // Add addresses / update labels that we've sent to the address book,
    // and emit coinsSent signal for each recipient
    for (const SendCoinsRecipient &rcp : transaction.getRecipients())
    {
        std::string strAddress = rcp.address.toStdString();
        CTxDestination dest = DecodeDestination(strAddress);
        std::string strLabel = rcp.label.toStdString();
        if (!IsValidDestination(dest)) continue;

        {
            LOCK(wallet->cs_wallet);

            std::map<CTxDestination, CAddressBookData>::iterator mi = wallet->mapAddressBook.find(dest);

            // Check if we have a new address or an updated label
            if (mi == wallet->mapAddressBook.end())
            {
                wallet->SetAddressBook(dest, strLabel, "send");
            }
            else if (mi->second.name != strLabel)
            {
                wallet->SetAddressBook(dest, strLabel, ""); // "" means don't change purpose
            }
        }
    }

    checkBalanceChanged(); // update balance immediately, otherwise there could be a short noticeable delay until pollBalanceChanged hits

    transaction.setOperationId(operationId);

    //Poll async operation for a result:
    //int iCount = q->getNumberOfWorkers();
    //std::shared_ptr<AsyncRPCOperation> operation = q->getOperationForId(  currentTransaction.getOperationId()  );
    int iCounter=0;
    UniValue oResult;
    string sResult;
    int64_t lResult;
    QString qsResult;
    while(1)
    {
      OperationStatus status;
      status=operation->getState();
      switch(status)
      {
        case OperationStatus::CANCELLED:
          transaction.setZSignOfflineTransaction("Background thread was cancelled");
          qsResult=QString::asprintf("Background thread was cancelled");
          Q_EMIT coinsZSent(operationId);
          return SendCoinsReturn(TransactionCreationFailed,qsResult);
          break;
        case OperationStatus::EXECUTING:
          break;
        case OperationStatus::READY:
          break;
        case OperationStatus::FAILED:
          oResult = operation->getError();          
          lResult = find_value(oResult, "code").get_int64();
          sResult = find_value(oResult, "message").get_str();

          if (lResult==-26) //Transaction expired 
          {
              qsResult=QString::asprintf("The transaction expired");
              Q_EMIT coinsZSent(operationId);
              return SendCoinsReturn(PaymentRequestExpired,qsResult);
          }
          else
          {
              qsResult=QString::asprintf("Background thread indicates an error occurred: %s",sResult.c_str() );
              Q_EMIT coinsZSent(operationId);
              return SendCoinsReturn(TransactionCreationFailed,qsResult);
          }
          break;
        case OperationStatus::SUCCESS:
          oResult = operation->getResult();
          sResult = oResult[0].get_str();
          if (sResult.find("z_sign_offline") != std::string::npos)
          {
            transaction.setZSignOfflineTransaction(sResult);
          }
          else
          {
            //Specific for the off-line transactions. Normal 'online' transactions won't see this
            transaction.setZSignOfflineTransaction("Could not find z_sign_offline in the result: "+sResult);
          }
          iCounter=11; //exit polling loop
          break;
        default:
          transaction.setZSignOfflineTransaction("Unknown result from the background thread");
          qsResult=QString::asprintf("Unknown result from the background thread");
          Q_EMIT coinsZSent(operationId);
          return SendCoinsReturn(TransactionCreationFailed,qsResult);
          break;
      }
      sleep(1);

      if (iCounter>10)
      {
        break;
      }
      iCounter++;
    }

    Q_EMIT coinsZSent(operationId);

    return SendCoinsReturn(OK);
}

OptionsModel *WalletModel::getOptionsModel()
{
    return optionsModel;
}

AddressTableModel *WalletModel::getAddressTableModel()
{
    return addressTableModel;
}

ZAddressTableModel *WalletModel::getZAddressTableModel()
{
    return zaddressTableModel;
}

TransactionTableModel *WalletModel::getTransactionTableModel()
{
    return transactionTableModel;
}

RecentRequestsTableModel *WalletModel::getRecentRequestsTableModel()
{
    return recentRequestsTableModel;
}

WalletModel::EncryptionStatus WalletModel::getEncryptionStatus() const
{

  if (ShutdownRequested())
    return Busy;

    try {
        if (wallet) {

            TRY_LOCK(wallet->cs_wallet, lockWallet);
            if(!lockWallet){
                return Busy;
                }

            TRY_LOCK(wallet->cs_KeyStore, lockKeystore);
            if(!lockKeystore){
                return Busy;
            }

            if(!wallet->IsCrypted())
            {
                return Unencrypted;
            }
            else if(wallet->IsLocked())
            {
                return Locked;
            }
        }
    }
    catch(const std::runtime_error& e)
    {
        LogPrintf("Exception: WalletModel::getEncryptionStatus: %s\n", e.what());
    }

    return Unlocked;
}

bool WalletModel::setWalletEncrypted(bool encrypted, const SecureString &passphrase)
{
    if(encrypted)
    {
        // Encrypt
        return wallet->EncryptWallet(passphrase);
    }
    else
    {
        // Decrypt -- TODO; not supported yet
        return false;
    }
}

void WalletModel::getWalletChainHeights(int &walletHeight, int &chainHeight) {
    walletHeight = wallet->walletHeight;
    chainHeight = wallet->chainHeight;
}

void WalletModel::lockWallet() {
    wallet->Lock();
    relockTime = 0;
}

void WalletModel::setLockedLater() {
    //Reset if wallet is locked

    // Get required locks upfront. This avoids the GUI from getting stuck on
    // periodical polls if the core is holding the locks for a longer time -
    // for example, during a wallet rescan.
    TRY_LOCK(wallet->cs_wallet, lockWallet);
    if(!lockWallet){
        QTimer::singleShot(250, this, SLOT(setLockedLater()));
        return;
    }

    TRY_LOCK(wallet->cs_KeyStore, lockKeystore);
    if(!lockKeystore){
        QTimer::singleShot(250, this, SLOT(setLockedLater()));
        return;
    }

    if (wallet->IsLocked()) {
        relockTime = 0;
        startedRescan = false;
        return;
    }

    //Set Relock Time to 0 if rescanning, will relock when finished
    if (startedRescan) {
        relockTime = 0;
    }

    //Relock if time is expired
    if (relockTime <= GetTime() && !startedRescan) {
        wallet->Lock();
        relockTime = 0;
        return;
    }

    QTimer::singleShot(250, this, SLOT(setLockedLater()));
}

bool WalletModel::setWalletLocked(bool locked, const SecureString &passPhrase)
{
    if(locked)
    {
        // Lock
        return wallet->Lock();
    }
    else
    {
        // Unlock
        relockTime = GetTime() + 300; //relock after 5 minutes
        QTimer::singleShot(250, this, SLOT(setLockedLater()));
        return wallet->Unlock(passPhrase);
    }
}

bool WalletModel::changePassphrase(const SecureString &oldPass, const SecureString &newPass)
{
    bool retval;
    {
        LOCK(wallet->cs_wallet);
        wallet->Lock(); // Make sure wallet is locked before attempting pass change
        retval = wallet->ChangeWalletPassphrase(oldPass, newPass);
    }
    return retval;
}

bool WalletModel::backupWallet(const QString &filename)
{
    return BackupWallet(*wallet, filename.toLocal8Bit().data());
}

// Handlers for core signals
static void NotifyKeyStoreStatusChanged(WalletModel *walletmodel, CCryptoKeyStore *wallet)
{
    qDebug() << "NotifyKeyStoreStatusChanged";
    QMetaObject::invokeMethod(walletmodel, "updateStatus", Qt::QueuedConnection);
}

static void NotifyAddressBookChanged(WalletModel *walletmodel, CWallet *wallet,
        const CTxDestination &address, const std::string &label, bool isMine,
        const std::string &purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(EncodeDestination(address));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + strPurpose + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(QString, strPurpose),
                              Q_ARG(int, status));
}

static void NotifyZAddressBookChanged(WalletModel *walletmodel, CWallet *wallet,
        const libzcash::PaymentAddress &address, const std::string &label, bool isMine,
        const std::string &purpose, ChangeType status)
{
    QString strAddress = QString::fromStdString(EncodePaymentAddress(address));
    QString strLabel = QString::fromStdString(label);
    QString strPurpose = QString::fromStdString(purpose);

    qDebug() << "NotifyZAddressBookChanged: " + strAddress + " " + strLabel + " isMine=" + QString::number(isMine) + " purpose=" + strPurpose + " status=" + QString::number(status);
    QMetaObject::invokeMethod(walletmodel, "updateZAddressBook", Qt::QueuedConnection,
                              Q_ARG(QString, strAddress),
                              Q_ARG(QString, strLabel),
                              Q_ARG(bool, isMine),
                              Q_ARG(QString, strPurpose),
                              Q_ARG(int, status));
}

static void NotifyTransactionChanged(WalletModel *walletmodel, CWallet *wallet, const uint256 &hash, ChangeType status)
{
    Q_UNUSED(wallet);
    Q_UNUSED(hash);
    Q_UNUSED(status);
    QMetaObject::invokeMethod(walletmodel, "updateTransaction", Qt::QueuedConnection);
}

static void NotifyWatchonlyChanged(WalletModel *walletmodel, bool fHaveWatchonly)
{
    QMetaObject::invokeMethod(walletmodel, "updateWatchOnlyFlag", Qt::QueuedConnection,
                              Q_ARG(bool, fHaveWatchonly));
}

static void NotifyRescanStarted(WalletModel *walletmodel)
{
    walletmodel->startedRescan = true;
}

static void NotifyRescanComplete(WalletModel *walletmodel)
{
    walletmodel->startedRescan = false;
}

void WalletModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyRescanStarted.connect(boost::bind(NotifyRescanStarted, this));
    wallet->NotifyRescanComplete.connect(boost::bind(NotifyRescanStarted, this));
    wallet->NotifyStatusChanged.connect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.connect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyZAddressBookChanged.connect(boost::bind(NotifyZAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.connect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->NotifyWatchonlyChanged.connect(boost::bind(NotifyWatchonlyChanged, this, _1));
}

void WalletModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyRescanStarted.disconnect(boost::bind(NotifyRescanStarted, this));
    wallet->NotifyRescanComplete.disconnect(boost::bind(NotifyRescanStarted, this));
    wallet->NotifyStatusChanged.disconnect(boost::bind(&NotifyKeyStoreStatusChanged, this, _1));
    wallet->NotifyAddressBookChanged.disconnect(boost::bind(NotifyAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyZAddressBookChanged.disconnect(boost::bind(NotifyZAddressBookChanged, this, _1, _2, _3, _4, _5, _6));
    wallet->NotifyTransactionChanged.disconnect(boost::bind(NotifyTransactionChanged, this, _1, _2, _3));
    wallet->NotifyWatchonlyChanged.disconnect(boost::bind(NotifyWatchonlyChanged, this, _1));
}

// WalletModel::UnlockContext implementation
WalletModel::UnlockContext WalletModel::requestUnlock()
{
    bool was_locked = getEncryptionStatus() == Locked;
    if(was_locked)
    {
        // Request UI to unlock wallet
        Q_EMIT requireUnlock();
    }
    // If wallet is still locked, unlock was failed or cancelled, mark context as invalid
    bool valid = getEncryptionStatus() != Locked;

    return UnlockContext(this, valid, was_locked);
}

WalletModel::UnlockContext::UnlockContext(WalletModel *_wallet, bool _valid, bool _relock):
        wallet(_wallet),
        valid(_valid),
        relock(_relock)
{

}

WalletModel::UnlockContext::~UnlockContext()
{
    if(valid && relock)
    {
        wallet->setWalletLocked(true);
    }
}

void WalletModel::UnlockContext::CopyFrom(const UnlockContext& rhs)
{
    // Transfer context; old object no longer relocks wallet
    *this = rhs;
    rhs.relock = false;
}

bool WalletModel::getPubKey(const CKeyID &address, CPubKey& vchPubKeyOut) const
{
    return wallet->GetPubKey(address, vchPubKeyOut);
}

bool WalletModel::IsSpendable(const CTxDestination& dest) const
{
    return IsMine(*wallet, dest) & ISMINE_SPENDABLE;
}

bool WalletModel::getPrivKey(const CKeyID &address, CKey& vchPrivKeyOut) const
{
    return wallet->GetKey(address, vchPrivKeyOut);
}

bool WalletModel::getSeedPhrase(std::string &phrase) const
{
    if (wallet->bip39Enabled) {
      return wallet->GetSeedPhrase(phrase);
    }

    phrase = "Bip39 is not enabled for this wallet. Bip39 can only be enabled by creating a new wallet.";
    return true;
}

void WalletModel::rescan()
{
    startedRescan = true;
    relockTime = 0;
    return wallet->ForceRescanWallet();
}
// returns a list of COutputs from COutPoints
void WalletModel::getOutputs(const std::vector<COutPoint>& vOutpoints, std::vector<COutput>& vOutputs)
{
    LOCK2(cs_main, wallet->cs_wallet);
    for (const COutPoint& outpoint : vOutpoints)
    {
        auto it = wallet->mapWallet.find(outpoint.hash);
        if (it == wallet->mapWallet.end()) continue;
        int nDepth = it->second.GetDepthInMainChain();
        if (nDepth < 0) continue;
        COutput out(&it->second, outpoint.n, nDepth, true /* spendable */);
        vOutputs.push_back(out);
    }
}

bool WalletModel::isSpent(const COutPoint& outpoint) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsSpent(outpoint.hash, outpoint.n);
}

// AvailableCoins + LockedCoins grouped by wallet address (put change in one group with wallet address)
void WalletModel::listCoins(std::map<QString, std::vector<COutput> >& mapCoins) const
{
    for (auto& group : wallet->ListCoins()) {
        auto& resultGroup = mapCoins[QString::fromStdString(EncodeDestination(group.first))];
        for (auto& coin : group.second) {
            resultGroup.emplace_back(std::move(coin));
        }
    }
}

bool WalletModel::isLockedCoin(uint256 hash, unsigned int n) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    return wallet->IsLockedCoin(hash, n);
}

void WalletModel::lockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->LockCoin(output);
}

void WalletModel::unlockCoin(COutPoint& output)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->UnlockCoin(output);
}

void WalletModel::listLockedCoins(std::vector<COutPoint>& vOutpts)
{
    LOCK2(cs_main, wallet->cs_wallet);
    wallet->ListLockedCoins(vOutpts);
}

void WalletModel::loadReceiveRequests(std::vector<std::string>& vReceiveRequests)
{
    vReceiveRequests = wallet->GetDestValues("rr"); // receive request
}

bool WalletModel::saveReceiveRequest(const std::string &sAddress, const int64_t nId, const std::string &sRequest)
{
    CTxDestination dest = DecodeDestination(sAddress);

    std::stringstream ss;
    ss << nId;
    std::string key = "rr" + ss.str(); // "rr" prefix = "receive request" in destdata

    LOCK(wallet->cs_wallet);
    if (sRequest.empty())
        return wallet->EraseDestData(dest, key);
    else
        return wallet->AddDestData(dest, key, sRequest);
}

bool WalletModel::isWalletEnabled()
{
   return !GetBoolArg("-disablewallet", DEFAULT_DISABLE_WALLET);
}

bool WalletModel::hdEnabled() const
{
    return wallet->IsHDFullyEnabled();
}

int WalletModel::getDefaultConfirmTarget() const
{
    return nTxConfirmTarget;
}

bool WalletModel::getDefaultWalletRbf() const
{
    return fWalletRbf;
}

std::map<CTxDestination, CAmount> WalletModel::getTAddressBalances()
{
    std::map<CTxDestination, CAmount> balances;

    {
        LOCK2(cs_main, wallet->cs_wallet);

        std::vector<COutput> vecOutputs;
        wallet->AvailableCoins(vecOutputs, false, NULL, true);

        BOOST_FOREACH(const COutput& out, vecOutputs)
        {
            if (out.nDepth < 1 || out.nDepth > 9999999)
                continue;
            if (!out.fSpendable) continue;

            CTxDestination address;
            const CScript& scriptPubKey = out.tx->vout[out.i].scriptPubKey;
            bool fValidAddress = ExtractDestination(scriptPubKey, address);

            if (fValidAddress) {
                CAmount nValue = out.tx->vout[out.i].nValue;

                if (!balances.count(address))
                    balances[address] = 0;
                balances[address] += nValue;
            }
        }
    }

    return balances;
}

std::map<libzcash::PaymentAddress, CAmount> WalletModel::getZAddressBalances(int minDepth, bool requireSpendingKey)
{
    LOCK2(cs_main, wallet->cs_wallet);
    std::map<libzcash::PaymentAddress, CAmount> balances;
    wallet->getZAddressBalances(balances, minDepth, requireSpendingKey);
    return balances;
}

CAmount WalletModel::getBalanceZaddr(std::string sAddress, int minDepth, bool requireSpendingKey) const
{
    LOCK2(cs_main, wallet->cs_wallet);
    std::map<libzcash::PaymentAddress, CAmount> balances;
    wallet->getZAddressBalances(balances, minDepth, requireSpendingKey);

    CAmount balance = 0;
    for (std::map<libzcash::PaymentAddress, CAmount>::iterator it = balances.begin(); it != balances.end(); it++) {
        if (sAddress == "") {
           balance += it->second;
        } else {
            if (EncodePaymentAddress(it->first) == sAddress) {
              balance += it->second;
            }
        }
    }
    return balance;
}

CAmount WalletModel::getAddressBalance(const std::string &sAddress)
{
    bool isTaddr = false;
    CTxDestination taddr = DecodeDestination(sAddress);
    isTaddr = IsValidDestination(taddr);

    CAmount nBalance = 0;
    if (isTaddr) {
        nBalance = getBalanceTaddr(sAddress, 1, false);
    } else {
        nBalance = getBalanceZaddr(sAddress, 1, false);
    }

    return nBalance;
}
