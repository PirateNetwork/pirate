// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletframe.h"
#include "util.h"

#include "pirateoceangui.h"
#include "walletview.h"

#include <cassert>
#include <cstdio>
#include <chrono>

#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QResizeEvent>

WalletFrame::WalletFrame(const PlatformStyle *_platformStyle, PirateOceanGUI *_gui) :
    QFrame(_gui),
    gui(_gui),
    platformStyle(_platformStyle)
{
    // Leave HBox hook for adding a list view later
    QHBoxLayout *walletFrameLayout = new QHBoxLayout(this);
    setContentsMargins(0,0,0,0);
    walletStack = new QStackedWidget(this);
    walletFrameLayout->setContentsMargins(0,0,0,0);
    walletFrameLayout->addWidget(walletStack);

    QLabel *noWallet = new QLabel(tr("No wallet has been loaded."));
    noWallet->setAlignment(Qt::AlignCenter);
    walletStack->addWidget(noWallet);
    
    // Initialize resize timer for debounced resize events
    resizeTimer = new QTimer(this);
    resizeTimer->setSingleShot(true);
    resizeTimer->setInterval(250); // 250 ms delay
    connect(resizeTimer, &QTimer::timeout, this, &WalletFrame::applyAspectRatioResize);
    
    // Delay enabling resize until wallet is fully loaded (5 seconds after startup)
    QTimer::singleShot(5000, this, [this]() {
        // Enable resize handling after startup complete
    });
}

WalletFrame::~WalletFrame()
{
}

void WalletFrame::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    
    // Don't process resize events for the first 5 seconds after startup
    static auto startTime = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startTime).count();
    
    if (elapsed < 5) {
        return; // Skip resize handling during startup
    }
    
    // Restart the timer on each resize event
    // This prevents multiple executions during rapid resizing
    resizeTimer->stop();
    resizeTimer->start();
}

void WalletFrame::applyAspectRatioResize()
{
    // This function is called 1 second after the last resize event
    // Apply 16:9 aspect ratio to the parent window
    
    if (!gui) return;
    
    int currentWidth = gui->width();
    int currentHeight = gui->height();
    
    double aspectRatio = 16.0 / 9.0;
    
    // Calculate new dimensions maintaining aspect ratio
    int targetHeight = static_cast<int>(currentWidth / aspectRatio);
    
    // Resize the parent window to maintain aspect ratio
    gui->resize(currentWidth, targetHeight);
}

void WalletFrame::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

bool WalletFrame::addWallet(const QString& name, WalletModel *walletModel)
{
    extern int nMaxConnections;          // from net.cpp
    
    if (!gui || !clientModel || !walletModel || mapWalletViews.count(name) > 0)
        return false;

    WalletView *walletView = new WalletView(platformStyle, this);
    walletView->setPirateOceanGUI(gui);
    walletView->setClientModel(clientModel);
    walletView->setWalletModel(walletModel);
    walletView->showOutOfSyncWarning(bOutOfSync);

     /* TODO we should goto the currently selected page once dynamically adding wallets is supported */
    walletView->gotoOverviewPage();
    walletStack->addWidget(walletView);
    mapWalletViews[name] = walletView;

    // Ensure a walletView is able to show the main window
    connect(walletView, SIGNAL(showNormalIfMinimized()), gui, SLOT(showNormalIfMinimized()));

    connect(walletView, SIGNAL(outOfSyncWarningClicked()), this, SLOT(outOfSyncWarningClicked()));


    //Cold storage offline mode:
    bool newInstall = GetBoolArg("-setup_cold_storage", false);
    if (newInstall == true)
    {
        //This is the first run of the new wallet
        if (nMaxConnections==0)
        {
            // Default configuration files for the QT GUI & server (PIRATE.conf)
            // was already created prior to reaching this point.
        
            //In init.cpp a dialog asked if an online or cold storage offline
            //wallet should be created. The code in init.cpp cannot access the
            //QT GUI to update the configuration data to the selected option.
        
            //Creation of a new seed phrase or restore of the seed phrase is
            //the first GUI interaction. 
   
            // Use this spot to update the config files:
            QSettings settings;
            settings.setValue("fEnableZSigning", true);
            settings.setValue("fEnableZSigning_ModeSpend", false);
            settings.setValue("fEnableZSigning_ModeSign", true);
        }
    }

    //Some debug output:
    if (nMaxConnections==0)
    {
        printf("maxconnections=0 (Offline mode)\n");
        printf("To disable all network connections, set the following in PIRATE.conf\n");
        printf("  server=0\n");
        printf("  listen=0\n");
    }
    else
    {
        fprintf(stderr,"maxconnections=%d (Online mode)\n",nMaxConnections);
    }

    //Setup GUI to desired mode
    gui->setColdStorageLayout();

    return true;
}

bool WalletFrame::setCurrentWallet(const QString& name)
{
    if (mapWalletViews.count(name) == 0)
        return false;

    WalletView *walletView = mapWalletViews.value(name);
    walletStack->setCurrentWidget(walletView);
    assert(walletView);
    walletView->updateEncryptionStatus();
    return true;
}

bool WalletFrame::removeWallet(const QString &name)
{
    if (mapWalletViews.count(name) == 0)
        return false;

    WalletView *walletView = mapWalletViews.take(name);
    walletStack->removeWidget(walletView);
    return true;
}

void WalletFrame::removeAllWallets()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        walletStack->removeWidget(i.value());
    mapWalletViews.clear();
}

bool WalletFrame::handlePaymentRequest(const SendCoinsRecipient &recipient)
{
    WalletView *walletView = currentWalletView();
    if (!walletView)
        return false;

    if (walletView->isLocked()) {
        walletView->unlockWallet();
    }

    if (!walletView->isLocked()) {
        return walletView->handlePaymentRequest(recipient);
    }
    
    return false;
}

void WalletFrame::showOutOfSyncWarning(bool fShow)
{
    bOutOfSync = fShow;
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->showOutOfSyncWarning(fShow);
}

void WalletFrame::resetUnlockTimer()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->resetUnlockTimer();
}

void WalletFrame::gotoOverviewPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoOverviewPage();
}

void WalletFrame::gotoHistoryPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoHistoryPage();
}

void WalletFrame::gotoReceiveCoinsPage()
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoReceiveCoinsPage();
}

/*
void WalletFrame::gotoSendCoinsPage(QString addr)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoSendCoinsPage(addr);
}
*/
void WalletFrame::gotoZSendCoinsPage(QString addr)
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoZSendCoinsPage(addr);
}

void WalletFrame::gotoZSignPage( )
{
    QMap<QString, WalletView*>::const_iterator i;
    for (i = mapWalletViews.constBegin(); i != mapWalletViews.constEnd(); ++i)
        i.value()->gotoZSignPage( );
}

void WalletFrame::gotoSignMessageTab(QString addr)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->gotoSignMessageTab(addr);
}

void WalletFrame::gotoVerifyMessageTab(QString addr)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->gotoVerifyMessageTab(addr);
}

void WalletFrame::encryptWallet(bool status)
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->encryptWallet(status);
}

void WalletFrame::backupWallet()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->backupWallet();
}

void WalletFrame::changePassphrase()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->changePassphrase();
}

void WalletFrame::unlockWallet()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->unlockWallet();
}

void WalletFrame::usedSendingAddresses()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->usedSendingAddresses();
}

void WalletFrame::usedReceivingAddresses()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->usedReceivingAddresses();
}

void WalletFrame::usedReceivingZAddresses()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->usedReceivingZAddresses();
}

void WalletFrame::importSK()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->importSK();
}

void WalletFrame::importVK()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->importVK();
}

void WalletFrame::showSeedPhrase()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->showSeedPhrase();
}

void WalletFrame::rescan()
{
    WalletView *walletView = currentWalletView();
    if (walletView)
        walletView->rescan();
}

WalletView *WalletFrame::currentWalletView()
{
    return qobject_cast<WalletView*>(walletStack->currentWidget());
}

void WalletFrame::outOfSyncWarningClicked()
{
    Q_EMIT requestedSyncWarningInfo();
}
