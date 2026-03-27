// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openphrasedialog.h"
#include "ui_openphrasedialog.h"
#include "walletmodel.h"

#include <QString>

OpenPhraseDialog::OpenPhraseDialog(QWidget *parent, WalletModel *model) :
    QDialog(parent),
    walletModel(model),
    ui(new Ui::OpenPhraseDialog)
{
    ui->setupUi(this);
    // Initial display in English (index 0); combobox signal will update if changed.
    on_cmbLanguage_currentIndexChanged(0);
}

OpenPhraseDialog::~OpenPhraseDialog()
{
    delete ui;
}

void OpenPhraseDialog::on_cmbLanguage_currentIndexChanged(int index)
{
    std::string phrase;
    if (walletModel && walletModel->getSeedPhrase(phrase, index)) {
        ui->txtPhrase->setPlainText(QString::fromStdString(phrase));
    }
}
