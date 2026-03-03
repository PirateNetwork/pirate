// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_VERIFYPAYMENTDISCLOSUREDIALOG_H
#define KOMODO_QT_VERIFYPAYMENTDISCLOSUREDIALOG_H

#include "paymentdisclosure.h"

#include <QDialog>

namespace Ui {
    class VerifyPaymentDisclosureDialog;
}

class WalletModel;

/** Dialog for verifying payment disclosure keys. */
class VerifyPaymentDisclosureDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VerifyPaymentDisclosureDialog(QWidget *parent = 0);
    ~VerifyPaymentDisclosureDialog();

    void setModel(WalletModel *model);

private Q_SLOTS:
    void onVerifyButtonClicked();
    void onClearButtonClicked();
    void onCloseButtonClicked();

private:
    Ui::VerifyPaymentDisclosureDialog *ui;
    WalletModel *model;

    void displayError(const QString& error);
    void displayResults(const QString& type, const QString& txid, int index, 
                       const QString& value, const QString& address, const QString& memo);
};

#endif // KOMODO_QT_VERIFYPAYMENTDISCLOSUREDIALOG_H
