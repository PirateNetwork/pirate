// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactiondescdialog.h"
#include "ui_transactiondescdialog.h"

#include "transactiontablemodel.h"

#include <QModelIndex>

TransactionDescDialog::TransactionDescDialog(const QModelIndex &idx, int type, QString message, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::TransactionDescDialog),
    modelIndex(idx),
    dialogType(type)
{
    ui->setupUi(this);

    // Only show the checkbox for transaction details, not for keys
    if (type == SPENDING_KEY || type == VIEWING_KEY) {
        ui->showPaymentDisclosureCheckbox->setVisible(false);
    } else {
        // Connect checkbox signal
        connect(ui->showPaymentDisclosureCheckbox, SIGNAL(stateChanged(int)), this, SLOT(onShowPaymentDisclosureChanged(int)));
    }

    QString desc;
    if (type == FULL_TRANSACTION) {
        setWindowTitle(tr("Details for %1").arg(idx.data(TransactionTableModel::TxIDRole).toString()));
        // Use the checkbox state to determine which description to show (checkbox defaults to unchecked)
        desc = idx.data(ui->showPaymentDisclosureCheckbox->isChecked() ? 
            TransactionTableModel::LongDescriptionRole : 
            TransactionTableModel::LongDescriptionNoDisclosureRole).toString();
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

void TransactionDescDialog::onShowPaymentDisclosureChanged(int state)
{
    updateDisplay();
}

void TransactionDescDialog::updateDisplay()
{
    // Only update if we have a valid model index
    if (!modelIndex.isValid()) {
        return;
    }
    
    QString desc;
    bool showPaymentDisclosure = ui->showPaymentDisclosureCheckbox->isChecked();
    
    if (dialogType == FULL_TRANSACTION) {
        desc = modelIndex.data(showPaymentDisclosure ? 
            TransactionTableModel::LongDescriptionRole : 
            TransactionTableModel::LongDescriptionNoDisclosureRole).toString();
    } else if (dialogType == MEMO_ONLY) {
        desc = modelIndex.data(TransactionTableModel::MemoDescriptionRole).toString();
    }
    
    // Only update if we got valid content
    if (!desc.isEmpty()) {
        ui->detailText->setHtml(desc);
    }
}

TransactionDescDialog::~TransactionDescDialog()
{
    delete ui;
}
