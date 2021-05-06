// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_ZSENDCOINSDIALOG_H
#define KOMODO_QT_ZSENDCOINSDIALOG_H

#include "walletmodel.h"

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class ClientModel;
class PlatformStyle;
class SendCoinsEntry;
class SendCoinsRecipient;

namespace Ui {
    class ZSendCoinsDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
class QSortFilterProxyModel;
QT_END_NAMESPACE

/** Dialog for sending coins */
class ZSendCoinsDialog : public QDialog, AsyncRPCOperation
{
    Q_OBJECT

public:
    explicit ZSendCoinsDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ZSendCoinsDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *walletModel);

    /** Set up the tab chain manually, as Qt messes up the tab chain by default in some cases (issue https://bugreports.qt-project.org/browse/QTBUG-10907).
     */
    QWidget *setupTabChain(QWidget *prev);

    void setAddress(const QString &address);
    void pasteEntry(const SendCoinsRecipient &rv);
    bool handlePaymentRequest(const SendCoinsRecipient &recipient);
    void setResult(const string sHeading, const string sResult);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();
    SendCoinsEntry *addEntry();
    void updateTabsAndLabels();
    void setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                     const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                                     const CAmount& privateWatchBalance, const CAmount& privateBalance, const CAmount& interestBalance);
    void updatePayFromList();
    void payFromAddressIndexChanged(int);

private:
    Ui::ZSendCoinsDialog *ui;
    ClientModel *clientModel;

    WalletModel *model;
    bool fNewRecipientAllowed;
    const PlatformStyle *platformStyle;

    // Process WalletModel::SendCoinsReturn and generate a pair consisting
    // of a message and message flags for use in Q_EMIT message().
    // Additional parameter msgArg can be used via .arg(msgArg).
    void processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg = QString());
    // Update the passed in CCoinControl with state from the GUI
    void updateCoinControlState(CCoinControl& ctrl);

private Q_SLOTS:
    void on_sendButton_clicked();
    void removeEntry(SendCoinsEntry* entry);
    void useAvailableBalance(SendCoinsEntry* entry);
    void updateDisplayUnit();
    void coinControlUpdateLabels();

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};


#define SEND_CONFIRM_DELAY   3

class ZSendConfirmationDialog : public QMessageBox
{
    Q_OBJECT

public:
    ZSendConfirmationDialog(const QString &title, const QString &text, int secDelay = SEND_CONFIRM_DELAY, QWidget *parent = 0);
    int exec();

private Q_SLOTS:
    void countDown();
    void updateYesButton();

private:
    QAbstractButton *yesButton;
    QTimer countDownTimer;
    int secDelay;
};



#endif // KOMODO_QT_ZSENDCOINSDIALOG_H
