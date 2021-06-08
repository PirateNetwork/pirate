// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_SPLASHSCREEN_H
#define KOMODO_QT_SPLASHSCREEN_H

#include <functional>
#include <QSplashScreen>

class CWallet;
class NetworkStyle;

class RestoreSeed;
class NewSeed;
class NewWallet;

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
    explicit SplashScreen(Qt::WindowFlags f, const NetworkStyle *networkStyle);
    ~SplashScreen();

    QWidget* seed;
    QVBoxLayout* layout;
    QLabel* pirateIcon;
    NewWallet* newWallet;
    RestoreSeed* restoreSeed;
    NewSeed* newSeed;
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

protected:
    bool eventFilter(QObject * obj, QEvent * ev);

private:
    /** Connect core signals to splash screen */
    void subscribeToCoreSignals();
    /** Disconnect core signals to splash screen */
    void unsubscribeFromCoreSignals();
    /** Connect wallet signals to splash screen */
    void ConnectWallet(CWallet*);

    QPixmap pixmap;
    QString curMessage;
    QColor curColor;
    int curAlignment;

    QList<CWallet*> connectedWallets;

private Q_SLOTS:
    /** Select Random or Restore from seed while creating a new wallet */
    void on_btnTypeSelected_clicked();

    /** Restore wallet from seed, pressed when seed has been inputed into the form */
    void on_btnRestore_clicked();

    /** Press complete new random seed phrase */
    void on_btnDone_clicked();

};

#endif // KOMODO_QT_SPLASHSCREEN_H
