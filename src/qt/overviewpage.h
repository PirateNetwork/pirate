// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_OVERVIEWPAGE_H
#define KOMODO_QT_OVERVIEWPAGE_H

#include "amount.h"
#include "params.h"

#include <QStringList>
#include <QWidget>
#include <memory>

class ClientModel;
class TransactionFilterProxy;
class TransactionRowDelegate;
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
class QGraphicsDropShadowEffect;
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
    /** Repopulate the header's wallet dropdown -- called for every open
     *  wallet's page whenever a wallet is added, removed, or switched to. */
    void setWalletList(const QStringList &names, const QString &current);
    void setLockMessage(QString message);
    void setUiVisible(bool visible, bool isCrypted, int64_t relockTime = 0);
    /** Re-color the balances card's drop shadow for the current theme --
     *  called by WalletView::updateIconTint() on every theme switch, the same
     *  chain phase 1 already uses to retint icons. */
    void updateShadowTheme();

public Q_SLOTS:
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                    const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                    const CAmount& privateWatchBalance, const CAmount& privateBalance, const CAmount& interestBalance);

Q_SIGNALS:
    void transactionClicked(const QModelIndex &index);
    void outOfSyncWarningClicked();
    /** Activity detected in the GUI, reset the lock timer */
    void resetUnlockTimerEvent();
    /** Quick-action buttons between the Balances card and Activity list --
     *  WalletView::setPirateOceanGUI() wires these straight to the same
     *  gotoZSendCoinsPage()/gotoReceiveCoinsPage() slots the (now-removed)
     *  left-nav Send/Receive entries used. */
    void sendCoinsClicked();
    void receiveCoinsClicked();
    /** User picked a different wallet in the header dropdown */
    void walletSwitchRequested(const QString &name);
    /** User picked "Manage wallets..." in the header dropdown */
    void manageWalletsRequested();

private:
    Ui::OverviewPage *ui;
    ClientModel *clientModel;
    WalletModel *walletModel;
    const PlatformStyle *platformStyle;

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

    // Card-style row delegate shared with the main Transactions tab (phase 2)
    // -- this mini-list's proxy is filter->setShowParentsOnly(true), so every
    // row it ever paints is a parent row (full card chrome, never the
    // flat-indented child-row style).
    TransactionRowDelegate *transactionDelegate;
    std::unique_ptr<TransactionFilterProxy> filter;

    QGraphicsDropShadowEffect *balanceCardShadow;
    // When true, setBalance() masks every amount label with a fixed string
    // instead of the real formatted value. Persisted via QSettings
    // "fPrivacyMode"; togglePrivacy() flips it and re-invokes
    // updateDisplayUnit() (already re-runs setBalance() from cached current*
    // amounts) to repaint immediately.
    bool fPrivacyMode;

    void setUnlockButtonLocked(bool locked);
    // GUI key of the wallet this page's dropdown currently shows as selected
    QString currentWalletKey;

private Q_SLOTS:
    void walletSelectorActivated(int index);
    void togglePrivacy();
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
