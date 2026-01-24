// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zsigndialog.h"
#include "ui_zsigndialog.h"
#include "init.h"     //for *pwalletMain
#include "core_io.h"  //for EncodeHexTx()
#include "util.h"     //for HexToCharArray()
#include "rpc/client.h" //for RPCConvertValues()
#include "rpc/server.h" //CRPCTable::execute


#include "addresstablemodel.h"
#include "komodounits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "walletmodel.h"

#include "base58.h"
#include "chainparams.h"
#include "coincontrol.h"
#include "ui_interface.h"
#include "txmempool.h"
#include "main.h"
#include "policy/fees.h"
#include "wallet/wallet_fees.h"
#include "wallet/wallet.h"
#include "key_io.h"
#include "wallet/asyncrpcoperation_sendmany.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "zaddresstablemodel.h"

#include <QFontMetrics>
#include <QMessageBox>
#include <QScrollBar>
#include <QSettings>
#include <QTextDocument>
#include <QTimer>
#include <QClipboard>
#include <QApplication>

extern CRPCTable tableRPC;

ZSignDialog::ZSignDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ZSignDialog),
    clientModel(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons())
    {
        ui->clearButton->setIcon(QIcon());
        ui->signButton->setIcon(QIcon());
        ui->broadcastButton->setIcon(QIcon());
    } else {
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->signButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
        ui->broadcastButton->setIcon(_platformStyle->SingleColorIcon(":/icons/transaction_confirmed"));
    }

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));

    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->signButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->broadcastButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));

    connect(ui->teInput, SIGNAL(textChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teResult, SIGNAL(textChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teInput, SIGNAL(cursorPositionChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->teResult, SIGNAL(cursorPositionChanged()), this, SLOT(sendResetUnlockSignal()));

    //Reset the GUI
    ui->teInput->clear();
    ui->lbResult->setText("Signed transaction output:");
    ui->teResult->clear();
    
    // By default, show broadcast button and allow pasting (online mode)
    ui->teResult->setReadOnly(false);
    ui->broadcastButton->setVisible(true);
}

ZSignDialog::~ZSignDialog()
{
    delete ui;
}

void ZSignDialog::sendResetUnlockSignal() {
    Q_EMIT resetUnlockTimerEvent();
}

void ZSignDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void ZSignDialog::setModel(WalletModel *_model)
{
    this->model = _model;
    
    // Check if we're in offline signing mode (Sign role)
    updateOfflineMode();
}

void ZSignDialog::updateOfflineMode()
{
    if (model && model->getOptionsModel())
    {
        bool enableZSigning = model->getOptionsModel()->data(
            model->getOptionsModel()->index(OptionsModel::EnableZSigning, 0), Qt::EditRole).toBool();
        bool enableZSigningSign = model->getOptionsModel()->data(
            model->getOptionsModel()->index(OptionsModel::EnableZSigning_Sign, 0), Qt::EditRole).toBool();
        
        // Hide broadcast button if offline signing is enabled and in Sign role
        SetOfflineMode(enableZSigning && enableZSigningSign);
    }
    else
    {
        // Default to online mode if no model
        SetOfflineMode(false);
    }
}

void ZSignDialog::on_signButton_clicked()
{
    UniValue oResult;
    QString sMsg;

    try
    {
      ui->lbResult->setText("Signed transaction output:");

      QString sTransaction = ui->teInput->toPlainText();
      sTransaction = sTransaction.trimmed();
      
      //Remove quotation marks if present
      if (sTransaction.startsWith("\"") && sTransaction.endsWith("\""))
      {
        sTransaction = sTransaction.mid(1, sTransaction.length() - 2);
      }
      
      //Replace escaped quotations with normal quotation character
      if (sTransaction.contains("\\\""))
      {
        sTransaction = sTransaction.replace("\\\"","\"");
      }

      //No text provided yet
      if (sTransaction.isEmpty())
      {
        ui->teResult->setText("Please paste the hex output from z_createbuildinstructions into the input field above.");
        return;
      }

      //Check if input is hex
      std::string hexInput = sTransaction.toStdString();
      if (hexInput.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
      {
        QString qsResult = "z_buildrawtransaction builds and signs a shielded transaction from builder instructions.\n"
          "The transaction builder data is generated by an online wallet using the 'z_createbuildinstructions' command.\n"
          "\nUsage:\n"
          "1. On your online (view-only) wallet, run:\n"
          "   z_createbuildinstructions \"fromaddress\" '[{\"address\":\"toaddress\",\"amount\":X.XX}]'\n"
          "2. Copy the hex output from that command\n"
          "3. Paste it into the input field above\n"
          "4. Click 'Sign' to build and sign the transaction\n"
          "5. Copy the resulting signed transaction\n"
          "6. Broadcast it on your online wallet using: sendrawtransaction \"hexstring\"\n"
          "\nArguments:\n"
          "\"hexstring\"              (string, required) The hex-encoded transaction builder instructions\n"
          "\nResult:\n"
          "\"transaction\"            (string) Hex-encoded signed transaction ready for broadcast\n";
        
        ui->teResult->setText(qsResult);
        return;
      }

      //Create parameters for z_buildrawtransaction
      UniValue params(UniValue::VARR);
      params.push_back(hexInput);
      
      //Execute z_buildrawtransaction RPC call
      oResult = tableRPC.execute("z_buildrawtransaction", params);
      
      if (oResult.isNull() || oResult.getType() != UniValue::VSTR)
      {
        ui->teResult->setText("Transaction signing failed. The result is empty or invalid.");
        return;
      }

      std::string signedTx = oResult.get_str();
      if (signedTx.empty())
      {
        ui->teResult->setText("Transaction signing failed. The signed transaction is empty.");
        return;
      }
      
      ui->lbResult->setText("Signed transaction output: The transaction was successfully signed. Copy the hex below and paste it into the Broadcast field or use sendrawtransaction on your online wallet.");
      ui->teResult->setText(QString::fromStdString(signedTx));
    }
    catch (UniValue& objError)
    {
        try // Nice formatting for standard-format error
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();

            sMsg.asprintf("Transaction signing failed: %s\n",message.c_str());
            ui->teResult->setText(sMsg);
        }
        catch (const std::runtime_error&) // raised when converting to invalid type, i.e. missing code or message
        {
            ui->teResult->setText("Transaction signing failed. Could not parse the response. Please look in the log and command line output\n");
        }
    }
    catch (...)
    {
        ui->teResult->setText("Transaction signing failed. Could not parse the response. Please look in the log and command line output\n");
    }
}

void ZSignDialog::on_broadcastButton_clicked()
{
    try
    {
        ui->lbResult->setText("Broadcast result:");
        
        QString signedTx = ui->teResult->toPlainText();
        signedTx = signedTx.trimmed();
        
        // Remove quotation marks if present
        if (signedTx.startsWith("\"") && signedTx.endsWith("\""))
        {
            signedTx = signedTx.mid(1, signedTx.length() - 2);
        }
        
        // Replace escaped quotations with normal quotation character
        if (signedTx.contains("\\\""))
        {
            signedTx = signedTx.replace("\\\"","\"");
        }
        
        // No text provided
        if (signedTx.isEmpty())
        {
            ui->teResult->setText("Please paste a signed transaction hex into the output field above, or sign a transaction first using the Sign button.");
            return;
        }
        
        // Check if input is hex
        std::string hexInput = signedTx.toStdString();
        if (hexInput.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        {
            ui->teResult->setText("Error: The transaction must be in hex format. Please ensure you have a properly signed transaction.");
            return;
        }
        
        // Create parameters for sendrawtransaction
        UniValue params(UniValue::VARR);
        params.push_back(hexInput);
        
        // Execute sendrawtransaction RPC call
        UniValue result = tableRPC.execute("sendrawtransaction", params);
        
        if (result.isNull() || !result.isStr())
        {
            ui->teResult->setText("Transaction broadcast failed. Invalid response from sendrawtransaction.");
            return;
        }
        
        std::string txid = result.get_str();
        if (txid.empty())
        {
            ui->teResult->setText("Transaction broadcast failed. The transaction ID is empty.");
            return;
        }
        
        // Format success message
        std::string successMsg = "Transaction successfully broadcast to the network!\n\nTransaction ID:\n" + txid;
        
        ui->lbResult->setText("Broadcast result: SUCCESS");
        ui->teResult->setText(QString::fromStdString(successMsg));
    }
    catch (UniValue& objError)
    {
        try
        {
            int code = find_value(objError, "code").get_int();
            std::string message = find_value(objError, "message").get_str();
            
            QString errorMsg;
            errorMsg.asprintf("Transaction broadcast failed:\nError code: %d\nMessage: %s", code, message.c_str());
            ui->teResult->setText(errorMsg);
        }
        catch (const std::runtime_error&)
        {
            ui->teResult->setText("Transaction broadcast failed. Could not parse the error response. Please check the log and command line output.");
        }
    }
    catch (...)
    {
        ui->teResult->setText("Transaction broadcast failed. An unexpected error occurred. Please check the log and command line output.");
    }
}

void ZSignDialog::clear()
{
    //Hide the result frame
    ui->teInput->clear();
    ui->lbResult->setText("Signed transaction output:");
    ui->teResult->clear();    
}

void ZSignDialog::reject()
{
    clear();
}

void ZSignDialog::accept()
{
    clear();
}


void ZSignDialog::setResult(const string sHeading, const string sResult)
{
    if (sHeading.length() == 0)
    {
      ui->lbResult->setText("Result: ");
    }
    else
    {
      ui->lbResult->setText("Result:"+QString::fromStdString(sHeading));
    }

    ui->teResult->setText( QString::fromStdString(sResult) );

    ui->frameResult->show();
}

void ZSignDialog::SetOfflineMode(bool offline)
{
    // Hide broadcast button in offline mode
    ui->broadcastButton->setVisible(!offline);
    
    // Make result text editable in online mode so users can paste signed transactions
    ui->teResult->setReadOnly(offline);
}

void ZSignDialog::on_copyInputButton_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->teInput->toPlainText());
}

void ZSignDialog::on_pasteInputButton_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    ui->teInput->setPlainText(clipboard->text());
}

void ZSignDialog::on_copyResultButton_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setText(ui->teResult->toPlainText());
}

void ZSignDialog::on_pasteResultButton_clicked()
{
    QClipboard *clipboard = QApplication::clipboard();
    ui->teResult->setPlainText(clipboard->text());
}


