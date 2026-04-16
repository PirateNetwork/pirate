// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openphrasedialog.h"
#include "ui_openphrasedialog.h"
#include "walletmodel.h"
#include "optionsmodel.h"

#include <QString>

OpenPhraseDialog::OpenPhraseDialog(QWidget *parent, WalletModel *model) :
    QDialog(parent),
    walletModel(model),
    ui(new Ui::OpenPhraseDialog)
{
    ui->setupUi(this);
    std::string phrase;
    int lang = walletModel ? walletModel->getOptionsModel()->getSeedPhraseLanguage() : 0;
    if (walletModel && walletModel->getSeedPhrase(phrase, lang)) {
        ui->txtPhrase->setPlainText(QString::fromStdString(phrase));
    }
}

OpenPhraseDialog::~OpenPhraseDialog()
{
    delete ui;
}
