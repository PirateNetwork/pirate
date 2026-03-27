// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_OPENPHRASEDIALOG_H
#define KOMODO_QT_OPENPHRASEDIALOG_H

#include <QDialog>

namespace Ui {
    class OpenPhraseDialog;
}

class WalletModel;

class OpenPhraseDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenPhraseDialog(QWidget *parent, WalletModel *model);
    ~OpenPhraseDialog();

private Q_SLOTS:
    void on_cmbLanguage_currentIndexChanged(int index);

private:
    Ui::OpenPhraseDialog *ui;
    WalletModel *walletModel;
};

#endif // KOMODO_QT_OPENPHRASEDIALOG_H
