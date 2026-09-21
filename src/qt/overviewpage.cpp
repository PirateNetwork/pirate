// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "komodounits.h"
#include "clientmodel.h"
#include "clientversion.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "transactionfilterproxy.h"
#include "transactionrowdelegate.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "updatedialog.h"
#include "util.h" // for KOMODO_ASSETCHAIN_MAXLEN

#include "params.h" //curl for price check

#include <QGraphicsDropShadowEffect>
#include <QSettings>
#include <QStyle>
#include <QPainter>
#include <QPixmap>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLocale>
#include <QDesktopServices>
#include <QUrl>
#include <QVersionNumber>

#define NUM_ITEMS 5


extern int nMaxConnections; //From net.h

extern char ASSETCHAINS_SYMBOL[KOMODO_ASSETCHAIN_MAXLEN];

// Stashi-style quick-action glyph: an accent-tinted disc with an arrow drawn
// inside it (up = send, down = receive). Drawn rather than shipped as a
// resource because the accent hue is the same in both themes and the disc
// needs its own translucent fill, which a single-color icon tint can't give.
static QIcon QuickActionIcon(bool up, const QColor &accent)
{
    const int size = 40;
    const qreal dpr = 2.0;
    QPixmap pm(int(size * dpr), int(size * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor disc = accent;
    disc.setAlphaF(0.16);
    p.setPen(Qt::NoPen);
    p.setBrush(disc);
    p.drawEllipse(QRectF(0, 0, size, size));

    p.setPen(QPen(accent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    const qreal c = size / 2.0, h = 7.0, w = 5.5;
    const qreal dir = up ? -1.0 : 1.0; // arrow tip's direction on the y axis
    p.drawLine(QPointF(c, c - dir * h), QPointF(c, c + dir * h));
    p.drawLine(QPointF(c - w, c + dir * (h - w)), QPointF(c, c + dir * h));
    p.drawLine(QPointF(c + w, c + dir * (h - w)), QPointF(c, c + dir * h));
    p.end();
    return QIcon(pm);
}

OverviewPage::OverviewPage(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::OverviewPage),
    clientModel(0),
    walletModel(0),
    platformStyle(platformStyle),
    currentBalance(-1),
    currentUnconfirmedBalance(-1),
    currentImmatureBalance(-1),
    currentWatchOnlyBalance(-1),
    currentWatchUnconfBalance(-1),
    currentWatchImmatureBalance(-1),
    currentPrivateWatchBalance(-1),
    currentPrivateBalance(-1),
    currentInterestBalance(-1),
    transactionDelegate(new TransactionRowDelegate(platformStyle, this)),
    balanceCardShadow(nullptr),
    fPrivacyMode(false)
{
    ui->setupUi(this);

    // use a SingleColorIcon for the "out of sync warning" icon
    QIcon icon = platformStyle->SingleColorIcon(":/icons/warning");
    icon.addPixmap(icon.pixmap(QSize(64,64), QIcon::Normal), QIcon::Disabled); // also set the disabled icon because we are using a disabled QPushButton to work around missing HiDPI support of QLabel (https://bugreports.qt.io/browse/QTBUG-42503)
    ui->labelTransactionsStatus->setIcon(icon);
    ui->labelWalletStatus->setIcon(icon);

    // Balance hero card: a real drop shadow, since QSS has no box-shadow.
    // Color is theme-dependent -- updateShadowTheme() sets it, called once
    // here and again on every live theme switch via WalletView::
    // updateIconTint() (the same chain phase 1 already uses for icon tint).
    balanceCardShadow = new QGraphicsDropShadowEffect(this);
    balanceCardShadow->setBlurRadius(8);
    balanceCardShadow->setOffset(0, 4);
    ui->frame->setGraphicsEffect(balanceCardShadow);
    updateShadowTheme();

    // Balance privacy toggle: persisted, masks every amount label with a
    // fixed string instead of the real formatted value.
    QSettings privacySettings;
    fPrivacyMode = privacySettings.value("fPrivacyMode", false).toBool();
    ui->privacyToggle->setChecked(fPrivacyMode);
    ui->privacyToggle->setIcon(platformStyle->SingleColorIcon(fPrivacyMode ? ":/icons/eye_minus" : ":/icons/eye"));
    connect(ui->privacyToggle, SIGNAL(clicked()), this, SLOT(togglePrivacy()));

    // Send/Receive quick actions, between the Balances card and Activity --
    // replaces the left-nav Send/Receive entries (removed from PirateOceanGUI's
    // toolbar), matching the Unified Wallet's layout. Just re-emitted as our
    // own signals; WalletView::setPirateOceanGUI() connects these straight to
    // PirateOceanGUI::gotoZSendCoinsPage()/gotoReceiveCoinsPage().
    ui->sendCoinsButton->setIcon(QuickActionIcon(true, QColor(0x2B, 0x6F, 0xF7)));      // primary blue
    ui->receiveCoinsButton->setIcon(QuickActionIcon(false, QColor(0x1F, 0xA9, 0x71)));  // positive green
    // QPushButton has no icon-to-text gap setting; a leading space is the
    // usual way to keep the 40px disc from butting up against the label.
    ui->sendCoinsButton->setText("  " + ui->sendCoinsButton->text());
    ui->receiveCoinsButton->setText("  " + ui->receiveCoinsButton->text());
    connect(ui->sendCoinsButton, SIGNAL(clicked()), this, SIGNAL(sendCoinsClicked()));
    connect(ui->receiveCoinsButton, SIGNAL(clicked()), this, SIGNAL(receiveCoinsClicked()));

    // Wallet switcher in the header row -- activated() only fires for a real
    // user pick, never for setWalletList()'s programmatic repopulation, so
    // no signal-blocking dance is needed to avoid switching in a loop.
    connect(ui->walletSelector, SIGNAL(activated(int)), this, SLOT(walletSelectorActivated(int)));

    // Recent transactions -- same card-row delegate as the main Transactions
    // tab (phase 2); this mini-list's proxy only ever shows parent rows
    // (setShowParentsOnly(true) in setWalletModel() below), so every row
    // gets full card chrome.
    ui->listTransactions->setItemDelegate(transactionDelegate);
    ui->listTransactions->setSpacing(8);
    ui->listTransactions->setUniformItemSizes(true);
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * 72);
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));

    // start with displaying the "out of sync" warnings
    if (nMaxConnections>0) //On-line
    {
        showOutOfSyncWarning(true);
        connect(ui->labelWalletStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
        connect(ui->labelTransactionsStatus, SIGNAL(clicked()), this, SLOT(handleOutOfSyncWarningClicks()));
    }

    //connect Unlock wallet button
    connect(ui->btnUnlock, SIGNAL(clicked()), this, SLOT(unlockWallet()));

    //set labal name to style
    ui->lblLockedMessage->setObjectName("lockedMessage");

    updateJSONtimer = new QTimer(this);
    updateGUItimer = new QTimer(this);
    gitJSONtimer = new QTimer(this);
    gitGUItimer = new QTimer(this);

    gitReply = new JsonDownload;
    cmcReply = new JsonDownload;

    connect(updateJSONtimer, SIGNAL(timeout()), SLOT(getPrice()));
    connect(updateGUItimer, SIGNAL(timeout()), SLOT(replyPriceFinished()));
    connect(gitJSONtimer, SIGNAL(timeout()), SLOT(getGitRelease()));
    connect(gitGUItimer, SIGNAL(timeout()), SLOT(replyGitRelease()));

    updateJSONtimer->setInterval(300000); //Check every 5 minutes.
    updateJSONtimer->start();

    updateGUItimer->setInterval(5000); //Check every 15 seconds.
    updateGUItimer->start();

    gitJSONtimer->setInterval(21600000); //Check every 6 hours.
    gitJSONtimer->start();

    gitGUItimer->setInterval(5000); //Check every 15 seconds.
    gitGUItimer->start();

    getGitRelease();
    getPrice();
}

void OverviewPage::handleTransactionClicked(const QModelIndex &index)
{
    Q_EMIT resetUnlockTimerEvent();

    if(filter)
        Q_EMIT transactionClicked(filter->mapToSource(index));
}

void OverviewPage::handleOutOfSyncWarningClicks()
{
    Q_EMIT outOfSyncWarningClicked();
}

OverviewPage::~OverviewPage()
{
    delete ui;
}

void OverviewPage::getGitRelease()
{
    getHttpsJson("https://api.github.com/repos/PirateNetwork/Pirate/releases", gitReply, GITHUB_HEADERS);
}

void OverviewPage::replyGitRelease()
{
    if (gitReply->failed == false && gitReply->complete == true) {
        try {
            QJsonDocument response = QJsonDocument::fromJson(gitReply->response.c_str());
            const QJsonArray responseArray  = response.array();
            const QJsonObject firstRecord  = responseArray[0].toObject();
            QString gitStrVersion = firstRecord["tag_name"].toString();

            if (gitStrVersion.startsWith("v"))
                gitStrVersion = gitStrVersion.right(gitStrVersion.length() - 1);

            QVersionNumber gitVersion = QVersionNumber::fromString(gitStrVersion);

            QVersionNumber clientVersion = QVersionNumber::fromString(QString::fromStdString(FormatGitVersion()));

            if (gitVersion > clientVersion) {

                  // Check Setting for update ignore, only ignore update notification for 1 week.
                  QSettings settings;
                  uint64_t ignoreTime = settings.value("timeIgnoreVersion", "0").toULongLong();
                  uint64_t currentTime = GetTime();
                  QString strIgnoreVersion = "0.0.0";

                  if (currentTime - ignoreTime < 604800) {
                      //Get ignored version if ignored time is less than 7 days
                      strIgnoreVersion = settings.value("strIgnoreVersion", "0.0.0").toString();
                  }

                  QVersionNumber ignoreVersion = QVersionNumber::fromString(strIgnoreVersion);

                  if (gitVersion > ignoreVersion) {
                      //Open Update Dialog
                      UpdateDialog dlg(this, clientVersion, gitVersion);
                      dlg.exec();
                      if (dlg.result() == QDialog::Accepted) {
                          //Open Pirate Github release page
                          QDesktopServices::openUrl(QUrl("https://github.com/Piratenetwork/Pirate/releases"));
                      }
                      // Set IgnoreVersion
                      qint64 newIgnoreTime = GetTime();
                      settings.setValue("strIgnoreVersion", gitVersion.toString());
                      settings.setValue("timeIgnoreVersion", QString::number(newIgnoreTime));
                      dlg.close();

                  }

            }

        } catch (...) {
            LogPrintf("Github Releases JSON Parsing error\n");
        }
    }
}

void OverviewPage::getPrice()
{
    getHttpsJson("https://api.coingecko.com/api/v3/simple/price?ids=pirate-chain&vs_currencies=btc%2Cusd%2Ceur&include_market_cap=true&include_24hr_vol=true&include_24hr_change=true", cmcReply, CMC_HEADERS);
}

void OverviewPage::replyPriceFinished()
{
    if (cmcReply->failed == false && cmcReply->complete == true) {
        try {
            QJsonDocument response = QJsonDocument::fromJson(cmcReply->response.c_str());

            const QJsonObject item  = response.object();
            const QJsonObject usd  = item["pirate-chain"].toObject();
            auto fiatValue = usd["usd"].toDouble();

            double currentFiat = currentPrivateBalance * fiatValue;
            double watchFiat = currentPrivateWatchBalance * fiatValue;

            //TODO: Setup multiple currencies
            QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
            QLocale dollar;

            //Set Total Value
            ui->labelFiat->setText(dollar.toCurrencyString(currentFiat/1e8));
            ui->labelWatchFiat->setText(dollar.toCurrencyString(watchFiat/1e8));
            ui->labelFiatTotal->setText(dollar.toCurrencyString((currentFiat+watchFiat)/1e8));

            //Set Exchange Rate
            ui->labelExchange->setText(dollar.toCurrencyString(fiatValue));

        } catch (...) {
            LogPrintf("Coin Gecko JSON Parsing error\n");
        }
    }
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance, const CAmount& privateWatchBalance, const CAmount& privateBalance, const CAmount& interestBalance)
{
    int unit = walletModel->getOptionsModel()->getDisplayUnit();
    currentBalance = balance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;
    currentPrivateWatchBalance = privateWatchBalance;
    currentPrivateBalance = privateBalance;
    currentInterestBalance = interestBalance;

    // Privacy mode: mask every amount label with a fixed string instead of
    // its real formatted value (matching the Unified Wallet's own literal
    // mask). Fiat/exchange-rate labels (set elsewhere, from price replies)
    // are deliberately left alone -- this covers the coin-balance figures
    // set here, which are what "hide my balance" is actually asking for.
    static const QString maskedText("*******");
    auto formatOrMask = [&](CAmount amount) {
        return fPrivacyMode ? maskedText : KomodoUnits::formatWithUnit(unit, amount, false, KomodoUnits::separatorAlways);
    };
    ui->labelBalance->setText(formatOrMask(balance));
    ui->labelUnconfirmed->setText(formatOrMask(unconfirmedBalance));
    ui->labelImmature->setText(formatOrMask(immatureBalance));
    ui->labelTotal->setText(formatOrMask(balance + unconfirmedBalance + immatureBalance + privateBalance + interestBalance));
    ui->labelWatchAvailable->setText(formatOrMask(watchOnlyBalance));
    ui->labelWatchPending->setText(formatOrMask(watchUnconfBalance));
    ui->labelWatchImmature->setText(formatOrMask(watchImmatureBalance));
    ui->labelWatchTotal->setText(formatOrMask(watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + privateWatchBalance));
    ui->labelPrivateWatchBalance->setText(formatOrMask(privateWatchBalance));
    ui->labelPrivateBalance->setText(formatOrMask(privateBalance));
    ui->labelInterestBalance->setText(formatOrMask(interestBalance));
    ui->labelWalletTotal->setText(formatOrMask(balance + unconfirmedBalance + immatureBalance + privateBalance + interestBalance + watchOnlyBalance + watchUnconfBalance + watchImmatureBalance + privateWatchBalance));

    // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;
    bool showInterest = (chainName.isKMD());

    bool showTransparent = balance !=0;
    bool showWatchOnlyTransaparent = watchOnlyBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelBalance->setVisible(showTransparent || showWatchOnlyTransaparent);
    ui->labelBalanceText->setVisible(showTransparent || showWatchOnlyTransaparent);
    ui->labelWatchAvailable->setVisible(showWatchOnlyTransaparent); // show watch-only immature balance

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance
    // we should show interest only for KMD, so we need to use setVisible with condition
    ui->labelInterestBalance->setVisible(showInterest);
    ui->labelInterestTotalText->setVisible(showInterest);

}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{


    if (showWatchOnly) {
        ui->labelSpendable->setVisible(showWatchOnly);            // show spendable label (only when watch-only is active)
        ui->labelWatchonly->setVisible(showWatchOnly);            // show watch-only label
        ui->labelWatchPending->setVisible(showWatchOnly);         // show watch-only pending balance
        ui->labelPrivateWatchBalance->setVisible(showWatchOnly);  // show watch-only private balance
        ui->labelWatchTotal->setVisible(showWatchOnly);           // show watch-only total balance
        ui->labelWatchFiat->setVisible(showWatchOnly);            // Show watch-only fiat balance

        ui->labelFiatTotalText->setVisible(showWatchOnly);
        ui->labelWalletTotalText->setVisible(showWatchOnly);
        ui->labelFiatTotal->setVisible(showWatchOnly);
        ui->labelWalletTotal->setVisible(showWatchOnly);

        ui->verticalSpacerCombined->changeSize(10, 10, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->labelCombinedText->setVisible(showWatchOnly);

        bool showTransparent = (currentBalance + currentWatchOnlyBalance) != 0;
        ui->labelBalance->setVisible(showTransparent);
        ui->labelBalanceText->setVisible(showTransparent);
        ui->labelWatchAvailable->setVisible(showTransparent);

        bool showImmature = (currentImmatureBalance + currentWatchImmatureBalance) != 0;
        ui->labelImmature->setVisible(showImmature);
        ui->labelImmatureText->setVisible(showImmature);
        ui->labelWatchImmature->setVisible(showImmature); // show watch-only immature balance


    } else {
        ui->labelSpendable->setVisible(showWatchOnly);            // show spendable label (only when watch-only is active)
        ui->labelWatchonly->setVisible(showWatchOnly);            // show watch-only label
        ui->labelWatchAvailable->setVisible(showWatchOnly);       // show watch-only available balance
        ui->labelWatchPending->setVisible(showWatchOnly);         // show watch-only pending balance
        ui->labelWatchTotal->setVisible(showWatchOnly);           // show watch-only total balance
        ui->labelWatchFiat->setVisible(showWatchOnly);            // Show watch-only fiat balance
        ui->labelWatchImmature->setVisible(showWatchOnly);        // show watch-only immature balance
        ui->labelPrivateWatchBalance->setVisible(showWatchOnly);  // show watch-only private balance

        ui->labelFiatTotalText->setVisible(showWatchOnly);
        ui->labelWalletTotalText->setVisible(showWatchOnly);
        ui->labelFiatTotal->setVisible(showWatchOnly);
        ui->labelWalletTotal->setVisible(showWatchOnly);

        ui->verticalSpacerCombined->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        ui->labelCombinedText->setVisible(showWatchOnly);
    }

    // lineFiat separates the balance tiles from the Combined Wallet Totals
    // block below them -- previously this function unconditionally hid it
    // (and a since-removed sibling) right after setting them correctly
    // per-branch above, so the divider could never actually render.
    ui->lineFiat->setVisible(true);
}

void OverviewPage::setClientModel(ClientModel *model)
{
    this->clientModel = model;
    if(model)
    {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel *model)
{
    this->walletModel = model;
    if(model && model->getOptionsModel())
    {
        // Set up transaction list
        filter.reset(new TransactionFilterProxy());
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->setShowParentsOnly(true);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter.get());
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(),
                   model->getPrivateWatchBalance(), model->getPrivateBalance(),model->getInterestBalance());
        connect(model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        updateWatchOnlyLabels(model->haveWatchOnly());
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
    }

    // update the display unit, to not use the default ("KMD")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if(walletModel && walletModel->getOptionsModel())
    {
        if(currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance,
                       currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance,
                       currentPrivateWatchBalance, currentPrivateBalance, currentInterestBalance);

        // TransactionRowDelegate reads FormattedAmountRole (already
        // unit-formatted by the model, kept in sync with
        // OptionsModel::displayUnitChanged there) -- no unit to push down.

        ui->listTransactions->update();

    }
}

void OverviewPage::togglePrivacy()
{
    fPrivacyMode = !fPrivacyMode;
    QSettings settings;
    settings.setValue("fPrivacyMode", fPrivacyMode);
    ui->privacyToggle->setIcon(platformStyle->SingleColorIcon(fPrivacyMode ? ":/icons/eye_minus" : ":/icons/eye"));
    updateDisplayUnit(); // re-runs setBalance() from the cached current* amounts
}

void OverviewPage::updateShadowTheme()
{
    QSettings settings;
    bool fDarkTheme = (settings.value("strTheme", "dark").toString() == "dark");
    balanceCardShadow->setColor(fDarkTheme ? QColor(0, 0, 0, 64) : QColor(0, 0, 0, 31));

    // Recent-transactions card list uses the same theme-refresh chain.
    transactionDelegate->setThemeColors();
    ui->listTransactions->update();
}

void OverviewPage::updateAlerts(const QString &warnings)
{
    this->ui->labelAlerts->setVisible(!warnings.isEmpty());
    this->ui->labelAlerts->setText(warnings);
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
  if (nMaxConnections>0) //On-line
  {
    ui->labelWalletStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
  }
}

void OverviewPage::setWalletList(const QStringList &names, const QString &current)
{
    // Repopulated wholesale on every wallet add/remove/switch rather than
    // diffed -- it's a handful of names at most, and blocking signals keeps
    // the rebuild itself from looking like a user switching wallets.
    QSignalBlocker blocker(ui->walletSelector);
    currentWalletKey = current;
    ui->walletSelector->clear();
    for (const QString &name : names)
        ui->walletSelector->addItem(name, name);
    int idx = ui->walletSelector->findData(current);
    ui->walletSelector->setCurrentIndex(idx);
    // Last entry opens the wallets modal (load/new/close). It has no item
    // data, which is what walletSelectorActivated() keys off to tell it apart
    // from a real wallet name -- those are never empty.
    ui->walletSelector->insertSeparator(ui->walletSelector->count());
    ui->walletSelector->addItem(tr("Manage wallets..."), QString());
}

void OverviewPage::walletSelectorActivated(int index)
{
    const QString name = ui->walletSelector->itemData(index).toString();
    if (name.isEmpty()) {
        // "Manage wallets...": snap the box back to the wallet actually in
        // use so it never displays the action as if it were a selection.
        ui->walletSelector->setCurrentIndex(ui->walletSelector->findData(currentWalletKey));
        Q_EMIT manageWalletsRequested();
        return;
    }
    Q_EMIT walletSwitchRequested(name);
}

void OverviewPage::setLockMessage(QString message) {
    ui->lblLockedMessage->setText(message);
}

// Drives QPushButton#btnUnlock[locked="true"] in the theme files: a locked
// wallet's Unlock button is the page's one call to action (primary fill),
// while Lock on an already-unlocked wallet stays a quiet secondary button.
void OverviewPage::setUnlockButtonLocked(bool locked)
{
    ui->btnUnlock->setProperty("locked", locked);
    ui->btnUnlock->style()->unpolish(ui->btnUnlock);
    ui->btnUnlock->style()->polish(ui->btnUnlock);
}

void OverviewPage::setUiVisible(bool visible, bool isCrypted, int64_t relockTime) {
    if (!isCrypted) {
        //Always hide on an unencrypted wallet
        ui->lblLockedMessage->setVisible(false);
        ui->btnUnlock->setVisible(false);
        if (nMaxConnections>0) 	//Online
        {
            ui->frame->setVisible(true);
            ui->quickActionsFrame->setVisible(true);
            ui->frame_2->setVisible(true);
        }
        else			//Offline
        {
            //Hide the balances frame.
            ui->frame->setVisible(false);
            //Hide the Send/Receive quick actions -- nothing to send/receive to while offline
            ui->quickActionsFrame->setVisible(false);
            //Hide the transaction summary frame
            ui->frame_2->setVisible(false);
            //Give a message on the empty page that we're in offline mode
            OverviewPage::updateAlerts("<b>Cold storage offline mode");
        }
        return;
    }

    //Alway Show on a crypted wallet
    ui->btnUnlock->setVisible(true);

    if (visible) {
        ui->btnUnlock->setText("Unlock");
        setUnlockButtonLocked(true);
        ui->frame->setVisible(false);
        ui->quickActionsFrame->setVisible(false);
        ui->frame_2->setVisible(false);
    } else {
        ui->btnUnlock->setText("Lock");
        setUnlockButtonLocked(false);
        if (nMaxConnections>0) //Online
        {
            ui->frame->setVisible(true);
            ui->quickActionsFrame->setVisible(true);
            ui->frame_2->setVisible(true);
        }
        else //Cold storagage offline
        {
            //Hide the balances frame.
            ui->frame->setVisible(false);
            //Hide the Send/Receive quick actions -- nothing to send/receive to while offline
            ui->quickActionsFrame->setVisible(false);
            //Hide the transaction summary frame
            ui->frame_2->setVisible(false);
            //Give a message on the empty page that we're in offline mode
            OverviewPage::updateAlerts("<b>Cold storage offline mode");
        }
    }

    ui->lblLockedMessage->setVisible(visible);

}

void OverviewPage::unlockWallet() {
    if (walletModel) {
        if (walletModel->getEncryptionStatus() == WalletModel::Locked) {
            walletModel->requireUnlock();
        } else {
            walletModel->lockWallet();
        }

    }
}
