// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "updatedialog.h"
#include "ui_updatedialog.h"

#include "init.h"

#include "guiutil.h"
#include "ui_interface.h"


UpdateDialog::UpdateDialog(QWidget *parent, QVersionNumber clientVersion, QVersionNumber gitVersion) :
    QDialog(parent),
    clientVersion(clientVersion),
    gitVersion(gitVersion),
    ui(new Ui::UpdateDialog)
{
    ui->setupUi(this);
    setMessage();
}

UpdateDialog::~UpdateDialog()
{
    delete ui;
}

void UpdateDialog::accept()
{
    QDialog::accept();
}

void UpdateDialog::setMessage()
{
    QString message = tr("A new release ");
    message = message + gitVersion.toString();
    message = message + tr(" is available! You have ");
    message = message + clientVersion.toString();
    message = message + tr(".\n\nWould you like to visit the releases page? ");

    ui->message->setText(message);

    if (ShutdownRequested) {
        QDialog::reject();
    }
}
