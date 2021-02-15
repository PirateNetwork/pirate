// Copyright (c) 2011-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "importkeydialog.h"
#include "ui_importSKdialog.h"
#include "ui_importVKdialog.h"

#include "guiutil.h"
#include "ui_interface.h"


#include <QUrl>

OpenSKDialog::OpenSKDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OpenSKDialog)
{
    ui->setupUi(this);
    ui->keyEdit->setPlaceholderText("secret-extended-key-main...");
}

OpenSKDialog::~OpenSKDialog()
{
    delete ui;
}

QString OpenSKDialog::getSK()
{
    return ui->keyEdit->text();
}

void OpenSKDialog::accept()
{
    QDialog::accept();
    this->privateKey = getSK();
}



OpenVKDialog::OpenVKDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OpenVKDialog)
{
    ui->setupUi(this);
    ui->keyEdit->setPlaceholderText("zxviews...");
}

OpenVKDialog::~OpenVKDialog()
{
    delete ui;
}

QString OpenVKDialog::getSK()
{
    return ui->keyEdit->text();
}

void OpenVKDialog::accept()
{
    QDialog::accept();
    this->privateKey = getSK();
}
