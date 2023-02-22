// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_OVERVIEWPAGE_H
#define KOMODO_QT_OVERVIEWPAGE_H

#include "amount.h"
#include "params.h"

#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TxViewDelegate;
class PlatformStyle;
class WalletModel;

namespace Ui {
    class OverviewPage;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
class QTimer;
class QNetworkAccessManager;
class QNetworkReply;
QT_END_NAMESPACE

/** Overview ("home") page widget */
class OverviewPage : public QWidget
{
    Q_OBJECT

public:
    explicit OverviewPage(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~OverviewPage();

    void setClientModel(ClientModel *clientModel);
    void setWalletModel(WalletModel *walletModel);
    void showOutOfSyncWarning(bool fShow);
    void setLockMessage(QString message);
    void setUiVisible(bool visible, bool isCrypted, int64_t relockTime = 0);

public Q_SLOTS:
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                    const CAmount& privateWatchBalance, const CAmount& privateBalance, const CAmount& interestBalance);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();
    /** Activity detected in the GUI, reset the lock timer */
    void resetUnlockTimerEvent();

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;

    QTimer *updateJSONtimer;
    QTimer *updateGUItimer;
    QTimer *gitJSONtimer;
    QTimer *gitGUItimer;
    QNetworkAccessManager *manager;
    QNetworkReply *reply;

    CAmount currentBalance;
    CAmount currentUnconfirmedBalance;
    CAmount currentImmatureBalance;
    CAmount currentWatchOnlyBalance;
    CAmount currentWatchUnconfBalance;
    CAmount currentWatchImmatureBalance;
    CAmount currentPrivateWatchBalance;
    CAmount currentPrivateBalance;
    CAmount currentInterestBalance;

    JsonDownload *gitReply;
    JsonDownload *cmcReply;

    TxViewDelegate *txdelegate;
    std::unique_ptr<TransactionFilterProxy> filter;

private Q_SLOTS:
    void getGitRelease();
    void replyGitRelease();
    void getPrice();
    void replyPriceFinished();
    void updateDisplayUnit();
    void handleTransactionClicked(const QModelIndex &index);
    void updateAlerts(const QString &warnings);
    void updateWatchOnlyLabels(bool showWatchOnly);
    void handleOutOfSyncWarningClicks();
    void unlockWallet();
};

#endif // KOMODO_QT_OVERVIEWPAGE_H
