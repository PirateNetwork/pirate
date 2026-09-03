// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_SPLASHSCREEN_H
#define KOMODO_QT_SPLASHSCREEN_H

#include <functional>
#include <string>
#include <QSplashScreen>

class CWallet;
class NetworkStyle;

class RestoreSeed;
class NewSeed;
class NewWallet;
class OpenWallet;

QT_BEGIN_NAMESPACE
class QPushButton;
class QVBoxLayout;
class QLabel;
QT_END_NAMESPACE

/** Class for the splashscreen with information of the running client.
 *
 * @note this is intentionally not a QSplashScreen. Pirate Core initialization
 * can take a long time, and in that case a progress window that cannot be
 * moved around and minimized has turned out to be frustrating to the user.
 */
class SplashScreen : public QWidget
{
    Q_OBJECT

public:
    explicit SplashScreen(const NetworkStyle *networkStyle);
    ~SplashScreen();

    // No-default-wallet redesign: entry point for true zero-wallet startup
    // (init.cpp's fAutoLoadWalletAtStartup was false, so AppInit2() returned
    // with pwalletMain still null and never fired InitCreateWallet() -- there
    // is no wallet object yet for this dialog to merely configure). Shows the
    // same create/restore widgets as the pre-existing InitCreateWallet()
    // signal path, but every handler below creates/loads walletName via
    // CWalletManager directly instead of touching a pre-existing pwalletMain
    // -- see fZeroWalletStartup's own comment. Emits walletCreated() once a
    // wallet exists (KomodoApplication::walletCreatedDuringStartup() resumes
    // the rest of Qt startup from there).
    void startZeroWalletFlow(const std::string& walletName);

    // True once startZeroWalletFlow() has been called -- selects, in each of
    // on_btnTypeSelected_clicked()/on_btnRestore_clicked()/on_btnDone_clicked()
    // and the free function showNewPhrase() below, between the pre-existing
    // pwalletMain-based logic (an already-constructed wallet this dialog is
    // merely configuring, driven by init.cpp's own busy-wait) and the new
    // CWalletManager-based logic (no wallet exists yet, this dialog is what
    // creates one). Public alongside this class's other UI-state members
    // (seed, newSeed, ...) for the same reason: the free functions further
    // down splashscreen.cpp (showCreateWallet(), showNewPhrase(), ...) read
    // and write a SplashScreen's fields directly rather than through member
    // functions of their own. on_btnOpen_clicked() needs no such branch: it
    // is only ever shown via the pre-existing InitNeedUnlockWallet() signal,
    // which requires an already-loaded, already-encrypted wallet to fire at
    // all -- structurally unreachable during true zero-wallet startup, since
    // there is no such wallet yet.
    bool fZeroWalletStartup;
    // The name to create/load under (zero-wallet mode only) -- what
    // GetArg("-wallet", "wallet.dat") resolved to at the point
    // startZeroWalletFlow() was called.
    std::string zeroWalletName;
    // Set by CWalletManager::CreateWallet() on success (zero-wallet mode
    // only) -- this dialog's own stand-in for what the pre-existing flow
    // stores on pwalletMain->recoverySeedPhrase, since there is no wallet
    // object to hang it on until CreateWallet() itself returns.
    std::string zeroWalletSeedPhrase;

    QWidget* seed;
    QVBoxLayout* layout;
    QLabel* pirateIcon;
    NewWallet* newWallet;
    RestoreSeed* restoreSeed;
    NewSeed* newSeed;
    OpenWallet* openWallet;
    QPushButton* btnTypeSelect;
    QPushButton* btnRestore;
    QPushButton* btnDone;

protected:
    void paintEvent(QPaintEvent *event);
    void closeEvent(QCloseEvent *event);

public Q_SLOTS:
    /** Slot to call finish() method as it's not defined as slot */
    void slotFinish(QWidget *mainWin);

    /** Show message and progress */
    void showMessage(const QString &message, int alignment, const QColor &color);

Q_SIGNALS:
    // Emitted once startZeroWalletFlow() has produced a loaded wallet (a
    // brand-new random seed, or one restored from a phrase). Never emitted
    // for the pre-existing InitCreateWallet()-signal-driven flow (an
    // already-existing pwalletMain), which has no equivalent completion
    // signal of its own -- init.cpp's own busy-wait loop notices that case
    // directly by polling createType.
    void walletCreated();

protected:
    bool eventFilter(QObject * obj, QEvent * ev);

private:
    /** Connect core signals to splash screen */
    void subscribeToCoreSignals();
    /** Disconnect core signals to splash screen */
    void unsubscribeFromCoreSignals();

    QPixmap pixmap;
    QString curMessage;
    QColor curColor;
    int curAlignment;

    QList<CWallet*> connectedWallets;

    bool bProcessedInitialColdStorageSetup;

private Q_SLOTS:
    /** Select Random or Restore from seed while creating a new wallet */
    void on_btnTypeSelected_clicked();

    /** Restore wallet from seed, pressed when seed has been inputed into the form */
    void on_btnRestore_clicked();

    /** Press complete new random seed phrase */
    void on_btnDone_clicked();

    /** Press open after entering password */
    void on_btnOpen_clicked();

    /** Press quit from password screen */
    void on_btnQuit_clicked();

    /** Update the displayed seed phrase when the user picks a different language */
    void on_newSeedLanguageChanged(int index);

};

#endif // KOMODO_QT_SPLASHSCREEN_H
