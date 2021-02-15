// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiondescdialog.h"
#include "ui_transactiondescdialog.h"

#include "transactiontablemodel.h"

#include <QModelIndex>

TransactionDescDialog::TransactionDescDialog(const QModelIndex &idx, int type, QString message, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TransactionDescDialog)
{
    ui->setupUi(this);

    QString desc;
    if (type == FULL_TRANSACTION) {
        setWindowTitle(tr("Details for %1").arg(idx.data(TransactionTableModel::TxIDRole).toString()));
        desc = idx.data(TransactionTableModel::LongDescriptionRole).toString();
    } else if (type == MEMO_ONLY) {
        setWindowTitle(tr("Memo for %1").arg(idx.data(TransactionTableModel::TxIDRole).toString()));
        desc = idx.data(TransactionTableModel::MemoDescriptionRole).toString();
    } else if (type == SPENDING_KEY) {
        setWindowTitle(tr("Extended Spending Key"));
        desc = message;
    } else if (type == VIEWING_KEY) {
        setWindowTitle(tr("Extended Viewing Key"));
        desc = message;
    }

    ui->detailText->setHtml(desc);
}

TransactionDescDialog::~TransactionDescDialog()
{
    delete ui;
}
