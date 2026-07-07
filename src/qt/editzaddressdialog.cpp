// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "editzaddressdialog.h"
#include "ui_editzaddressdialog.h"

#include "zaddresstablemodel.h"
#include "guiutil.h"
#include "consensus/upgrades.h"
#include "main.h"
#include "chainparams.h"

#include <QDataWidgetMapper>
#include <QMessageBox>

EditZAddressDialog::EditZAddressDialog(Mode _mode, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::EditZAddressDialog),
    mapper(0),
    mode(_mode),
    model(0)
{
    ui->setupUi(this);

    GUIUtil::setupAddressWidget(ui->addressEdit, this);

    ui->labelEdit->setPlaceholderText("Shielded");
    ui->labelOriginal->setPlaceholderText("Shielded");

    switch(mode)
    {
    case NewReceivingAddress:
        setWindowTitle(tr("New receiving shielded address"));
        ui->addressEdit->setEnabled(false);
        ui->addressEdit->setVisible(false);
        ui->label_2->setVisible(false);
        ui->labelAddressType->setVisible(true);
        ui->addressTypeKeyCombo->setVisible(true);
        ui->labelProtocol->setVisible(true);
        ui->addressTypeCombo->setVisible(true);
        ui->labelEdit->setText("Shielded");
        
        // Populate address type combo based on network upgrades
        {
            LOCK(cs_main);
            if (chainActive.Tip() != nullptr) {
                int nHeight = chainActive.Height();
                const Consensus::Params& consensusParams = Params().GetConsensus();
                
                bool saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_SAPLING);
                bool ironwoodActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_IRONWOOD);
                
                ui->addressTypeCombo->clear();
                
                if (ironwoodActive) {
                    // Ironwood is active - show Ironwood as default (and recommended)
                    ui->addressTypeCombo->addItem("Ironwood (Recommended)", "ironwood");
                    // Also offer Sapling as an option if active
                    if (saplingActive) {
                        ui->addressTypeCombo->addItem("Sapling", "sapling");
                    }
                } else if (saplingActive) {
                    // Only Sapling is active - show only Sapling
                    ui->addressTypeCombo->addItem("Sapling", "sapling");
                } else {
                    // Neither is active, disable the dialog
                    ui->addressTypeCombo->addItem("No shielded protocols active", "");
                    ui->addressTypeCombo->setEnabled(false);
                }
            }
        }
        break;
    case NewSendingAddress:
        setWindowTitle(tr("New sending shielded address"));
        ui->addressEdit->setVisible(true);
        ui->label_2->setVisible(true);
        ui->labelAddressType->setVisible(false);
        ui->addressTypeKeyCombo->setVisible(false);
        ui->labelProtocol->setVisible(false);
        ui->addressTypeCombo->setVisible(false);
        break;
    case EditReceivingAddress:
        setWindowTitle(tr("Edit receiving shielded address"));
        ui->addressEdit->setEnabled(false);
        ui->addressEdit->setVisible(true);
        ui->label_2->setVisible(true);
        ui->labelAddressType->setVisible(false);
        ui->addressTypeKeyCombo->setVisible(false);
        ui->labelProtocol->setVisible(false);
        ui->addressTypeCombo->setVisible(false);
        break;
    case EditSendingAddress:
        setWindowTitle(tr("Edit sending shielded address"));
        ui->addressEdit->setVisible(true);
        ui->label_2->setVisible(true);
        ui->labelAddressType->setVisible(false);
        ui->addressTypeKeyCombo->setVisible(false);
        ui->labelProtocol->setVisible(false);
        ui->addressTypeCombo->setVisible(false);
        break;
    }

    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);

    //Hide mapped elements
    ui->labelOriginal->setVisible(false);
    ui->addressOriginal->setVisible(false);
}

EditZAddressDialog::~EditZAddressDialog()
{
    delete ui;
}

void EditZAddressDialog::setModel(ZAddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;

    mapper->setModel(_model);
    mapper->addMapping(ui->labelOriginal, ZAddressTableModel::Label);
    mapper->addMapping(ui->addressOriginal, ZAddressTableModel::Address);
}

void EditZAddressDialog::loadRow(int row)
{
    mapper->setCurrentIndex(row);

    //Set editable elements to mapped elements
    ui->labelEdit->setText(ui->labelOriginal->text());
    ui->addressEdit->setText(ui->addressOriginal->text());
}

bool EditZAddressDialog::saveCurrentRow()
{
    if(!model)
        return false;

    //Set mapped elements to final values
    ui->labelOriginal->setText(ui->labelEdit->text());
    ui->addressOriginal->setText(ui->addressEdit->text());

    switch(mode)
    {
    case NewReceivingAddress:
    case NewSendingAddress:
        {
            QString addressType = ui->addressTypeCombo->currentData().toString();
            bool useDiversified = (ui->addressTypeKeyCombo->currentIndex() == 0); // 0 = Diversified, 1 = New Key
            address = model->addRow(
                    mode == NewSendingAddress ? ZAddressTableModel::Send : ZAddressTableModel::Receive,
                    ui->labelEdit->text(),
                    ui->addressEdit->text(),
                    useDiversified,
                    addressType);
        }
        break;
    case EditReceivingAddress:
    case EditSendingAddress:
        if(mapper->submit())
        {
            address = ui->addressEdit->text();
        }
        break;
    }
    return !address.isEmpty();
}

void EditZAddressDialog::accept()
{
    if(!model)
        return;

    //Set mapped elements to final values
    ui->labelOriginal->setText(ui->labelEdit->text());
    ui->addressOriginal->setText(ui->addressEdit->text());

    if(!saveCurrentRow())
    {
        switch(model->getEditStatus())
        {
        case ZAddressTableModel::OK:
            // Failed with unknown reason. Just reject.
            break;
        case ZAddressTableModel::NO_CHANGES:
            // No changes were made during edit operation. Just reject.
            break;
        case ZAddressTableModel::INVALID_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is not a valid Pirate z-address.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case ZAddressTableModel::DUPLICATE_ADDRESS:
            QMessageBox::warning(this, windowTitle(),
                tr("The entered address \"%1\" is already in the address book.").arg(ui->addressEdit->text()),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case ZAddressTableModel::WALLET_UNLOCK_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("Could not unlock wallet."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;
        case ZAddressTableModel::KEY_GENERATION_FAILURE:
            QMessageBox::critical(this, windowTitle(),
                tr("New key generation failed."),
                QMessageBox::Ok, QMessageBox::Ok);
            break;

        }
        return;
    }
    QDialog::accept();
}

QString EditZAddressDialog::getAddress() const
{
    return address;
}

void EditZAddressDialog::setAddress(const QString &_address)
{
    this->address = _address;
    ui->addressEdit->setText(_address);
    ui->addressOriginal->setText(_address);
}
