// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_ZSignDIALOG_H
#define KOMODO_QT_ZSignDIALOG_H

#include "walletmodel.h"

#include <QDialog>
#include <QMessageBox>
#include <QString>
#include <QTimer>

class ClientModel;
class PlatformStyle;



namespace Ui {
    class ZSignDialog;
}

QT_BEGIN_NAMESPACE
class QUrl;
class QSortFilterProxyModel;
QT_END_NAMESPACE

/** Dialog for sending coins */
class ZSignDialog : public QDialog, AsyncRPCOperation
{
    Q_OBJECT

public:
    explicit ZSignDialog(const PlatformStyle *platformStyle, QWidget *parent = 0);
    ~ZSignDialog();

    void setClientModel(ClientModel *clientModel);
    void setModel(WalletModel *walletModel);

    void setResult(const string sHeading, const string sResult);

public Q_SLOTS:
    void clear();
    void reject();
    void accept();

private:
    Ui::ZSignDialog *ui;
    ClientModel *clientModel;

    WalletModel *model;
    const PlatformStyle *platformStyle;

private Q_SLOTS:
    void on_signButton_clicked();

Q_SIGNALS:
    // Fired when a message should be reported to the user
    void message(const QString &title, const QString &message, unsigned int style);
};

#endif // KOMODO_QT_ZSignDIALOG_H
