// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "unlocktimerdialog.h"
#include "ui_unlocktimerdialog.h"

#include "guiutil.h"
#include "ui_interface.h"
#include "util.h"

UnlockTimerDialog::UnlockTimerDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::UnlockTimerDialog)
{

    // This timer will be fired repeatedly to update the locked message
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(setLockMessage()));
    pollTimer->start(250);

    relockTime = GetTime() + 30;

    ui->setupUi(this);
}

UnlockTimerDialog::~UnlockTimerDialog()
{
    delete ui;
}

void UnlockTimerDialog::setLockMessage()
{
    QString message = tr("Wallet will lock in ");
    message = message + QString::number(relockTime - GetTime());
    message = message + tr(" seconds!!!\n\n");
    message = message + tr(" Press OK to keep unlocked");

    ui->message->setText(message);

    if (relockTime - GetTime() <= 0) {
      QDialog::reject();
    }

}

void UnlockTimerDialog::accept()
{
    QDialog::accept();

}
