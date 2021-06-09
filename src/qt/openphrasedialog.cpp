// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "openphrasedialog.h"
#include "ui_openphrasedialog.h"

// #include "guiutil.h"
// #include "walletmodel.h"

#include <QUrl>

OpenPhraseDialog::OpenPhraseDialog(QWidget *parent, QString phraseIn) :
    QDialog(parent),
    phrase(phraseIn),
    ui(new Ui::OpenPhraseDialog)
{
    ui->setupUi(this);
    ui->txtPhrase->setPlainText(phrase);
}

OpenPhraseDialog::~OpenPhraseDialog()
{
    delete ui;
}
