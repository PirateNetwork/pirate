// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2018-2025 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file zsendcoinsdialog.cpp
 * @brief Shielded transaction send dialog implementation
 * 
 * Provides GUI for creating and sending shielded z-address transactions,
 * with support for both online and offline signing modes.
 */

#include "zsendcoinsdialog.h"
#include "ui_zsendcoinsdialog.h"
#include "ui_zsendcoinsdialog_popup.h"
#include "zsendcoinsdialog_popup.h"

#include "addresstablemodel.h"
#include "komodounits.h"
#include "clientmodel.h"
#include "coincontroldialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
#include "sendcoinsentry.h"
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

ZSendCoinsDialog::ZSendCoinsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ZSendCoinsDialog),
    clientModel(0),
    model(0),
    fNewRecipientAllowed(true),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);
    ui->payFromAddress->setMaxVisibleItems(30);

    // Configure button icons based on platform style
    if (!_platformStyle->getImagesOnButtons()) {
        ui->addButton->setIcon(QIcon());
        ui->clearButton->setIcon(QIcon());
        ui->sendButton->setIcon(QIcon());
        ui->refreshPayFrom->setIcon(QIcon());
    } else {
        ui->addButton->setIcon(_platformStyle->SingleColorIcon(":/icons/add"));
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->sendButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
        ui->refreshPayFrom->setIcon(_platformStyle->SingleColorIcon(":/icons/refresh"));
    }

    addEntry();

    // Connect UI signals
    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->refreshPayFrom, SIGNAL(clicked()), this, SLOT(updatePayFromList()));

    // Connect unlock timer reset signals
    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->refreshPayFrom, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(sendResetUnlockSignal()));
    connect(ui->payFromAddress, SIGNAL(highlighted(int)), this, SLOT(sendResetUnlockSignal()));
    connect(ui->sendButton, SIGNAL(clicked()), this, SLOT(sendResetUnlockSignal()));

    // Initialize transaction fee
    ui->customFee->setValue(ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE);
    ui->teResult->clear();
    ui->frameResult->hide();
    
    commission = 0;
}

void ZSendCoinsDialog::setClientModel(ClientModel *_clientModel)
{
    this->clientModel = _clientModel;
}

void ZSendCoinsDialog::setModel(WalletModel *_model)
{
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        for(int i = 0; i < ui->entries->count(); ++i)
        {
            SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if(entry)
            {
                entry->setModel(_model);
            }
        }

        // Keep up to date with wallet
        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(),
                   model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance(),
                   model->getPrivateWatchBalance(), model->getPrivateBalance(),model->getInterestBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        ui->frameCoinControl->setVisible(true); // frame "coin control" should be always visible, bcz it contains PayFrom combobox
        coinControlUpdateLabels();

        connect(ui->payFromAddress, SIGNAL(currentIndexChanged(int)), this, SLOT(payFromAddressIndexChanged(int)));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        ui->customFee->setSingleStep(GetRequiredFee(1000));
    }
}

ZSendCoinsDialog::~ZSendCoinsDialog()
{
    delete ui;
}

void ZSendCoinsDialog::sendResetUnlockSignal() {
    Q_EMIT resetUnlockTimerEvent();
}

/**
 * @brief Handle send button click - create and send shielded transaction
 * 
 * Supports two modes:
 * - Online: Direct transaction creation and broadcast
 * - Offline: Generate transaction builder data for offline signing
 */
void ZSendCoinsDialog::on_sendButton_clicked()
{
    if (!model || !model->getOptionsModel())
        return;

    // Extract source address and determine if it's owned (online mode)
    QString fromAddress;
    bool isOwnedAddress = true;
    
    if (!ui->payFromAddress->currentText().split(' ').isEmpty())
    {
        fromAddress = ui->payFromAddress->currentText().split(' ').at(2);
        
        // Check if address is marked for offline transaction
        if (ui->payFromAddress->currentText().contains("Off-line"))
        {
            isOwnedAddress = false;
        }
    }

    // Validate and collect recipients
    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for (int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (entry)
        {
            if (entry->validate(true))
            {
                SendCoinsRecipient recipient = entry->getValue();
                
                // Convert memo to hex format if needed
                if (recipient.memo.length() > 0 && !entry->getSubmitMemoAsHex())
                {
                    std::string memoHex = HexStr(recipient.memo.toStdString());
                    recipient.memo = QString::fromStdString(memoHex);
                }
                
                recipients.append(recipient);
            }
            else
            {
                valid = false;
            }
        }
    }

    if (!valid || recipients.isEmpty() || fromAddress.isEmpty())
    {
        return;
    }

    fNewRecipientAllowed = false;
    
    // Request wallet unlock
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
    {
        fNewRecipientAllowed = true;
        return;
    }

    // Prepare transaction
    WalletModelZTransaction currentTransaction(fromAddress, recipients, ui->customFee->value(), isOwnedAddress);
    CCoinControl ctrl;
    updateCoinControlState(ctrl);

    WalletModel::SendCoinsReturn prepareStatus = model->prepareZTransaction(currentTransaction, ctrl);

    // Handle preparation errors
    processSendCoinsReturn(prepareStatus,
        KomodoUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if (prepareStatus.status != WalletModel::OK)
    {
        fNewRecipientAllowed = true;
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();

    // Build confirmation message
    QStringList formatted;
    for (const SendCoinsRecipient &rcp : currentTransaction.getRecipients())
    {
        QString amount = "<b>" + KomodoUnits::formatHtmlWithUnit(
            model->getOptionsModel()->getDisplayUnit(), rcp.amount) + "</b>";
        QString address = "<span style='font-family: monospace;'>" + rcp.address + "</span>";

        QString recipientElement;
        if (rcp.label.length() > 0)
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
            recipientElement.append(QString(" (%1)").arg(address));
        }
        else
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        formatted.append(recipientElement);
    }

    // Determine confirmation dialog text based on mode
    QString questionString = isOwnedAddress
        ? tr("Are you sure you want to send?")
        : tr("Are you sure you want to create unsigned transaction?");
    
    questionString.append("<br /><br />%1");

    // Add transaction fee display
    if (txFee > 0)
    {
        questionString.append("<hr /><span style='color:#cc0000;'>");
        questionString.append(KomodoUnits::formatHtmlWithUnit(
            model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));
    }
    
    // Add commission if applicable (legacy hardware wallet commission removed)
    if (commission > 0)
    {
        questionString.append("<hr /><span style='color:#cc0000;'>");
        questionString.append(KomodoUnits::formatHtmlWithUnit(
            model->getOptionsModel()->getDisplayUnit(), commission));
        questionString.append("</span> ");
        questionString.append(tr("added as commission"));   
        txFee += commission;
    }

    // Add total amount display
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    
    for (KomodoUnits::Unit u : KomodoUnits::availableUnits())
    {
        if (u != model->getOptionsModel()->getDisplayUnit())
        {
            alternativeUnits.append(KomodoUnits::formatHtmlWithUnit(u, totalAmount));
        }
    }
    
    questionString.append(tr("Total amount: %1").arg(
        KomodoUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
    questionString.append(QString("<span><br />(=%2)</span>").arg(
        alternativeUnits.join(" " + tr("or") + " ")));

    // Show confirmation dialog
    QString heading = isOwnedAddress 
        ? tr("Confirm send coins")
        : tr("Confirm create unsigned transaction");
    ZSendConfirmationDialog confirmationDialog(heading,
        questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();
    if (retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // Apply commission to transaction if applicable
    if (commission > 0)
    {
        currentTransaction.setTransactionFee(txFee);
    }

    // Handle offline signing mode first - skip sending if address not owned
    if (!isOwnedAddress)
    {
        fNewRecipientAllowed = true;
        handleOfflineSigning(currentTransaction, txFee, fromAddress);
        return;
    }

    // Execute transaction (only for owned addresses)
    WalletModel::SendCoinsReturn sendStatus = model->zsendCoins(currentTransaction);
    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
        
        // Display operation ID for online mode
        std::string result = "opid = " + currentTransaction.getOperationId();
        setResult("", result);
    }
    
    fNewRecipientAllowed = true;
}

/**
 * @brief Handle offline transaction signing workflow
 * @param transaction The prepared transaction
 * @param txFee Total transaction fee including commission
 * @param fromAddress Source z-address
 */
void ZSendCoinsDialog::handleOfflineSigning(const WalletModelZTransaction &transaction, 
                                            CAmount txFee,
                                            const QString &fromAddress)
{
    try
    {
        // Build parameters for z_createbuildinstructions RPC
        UniValue params(UniValue::VARR);
        params.push_back(fromAddress.toStdString());
        
        // Build recipients array
        UniValue outputs(UniValue::VARR);
        for (const SendCoinsRecipient &rcp : transaction.getRecipients())
        {
            UniValue output(UniValue::VOBJ);
            output.push_back(Pair("address", rcp.address.toStdString()));
            output.push_back(Pair("amount", FormatMoney(rcp.amount)));
            
            if (!rcp.memo.isNull() && rcp.memo.length() > 0)
            {
                output.push_back(Pair("memo", rcp.memo.toStdString()));
            }
            
            outputs.push_back(output);
        }
        params.push_back(outputs);
        
        // Add minconf and fee parameters
        params.push_back(1);  // minconf
        params.push_back(ValueFromAmount(txFee));
        
        // Execute RPC to get transaction builder hex
        UniValue buildResult = tableRPC.execute("z_createbuildinstructions", params);
        
        if (buildResult.isNull() || !buildResult.isStr())
        {
            setResult("Transaction preparation failed", 
                     "z_createbuildinstructions did not return valid hex data");
            return;
        }
        
        std::string hexData = buildResult.get_str();
        
        // Display hex data for offline signing
        ZSendCoinsDialog_popup *popup = new ZSendCoinsDialog_popup(platformStyle);
        popup->SetUnsignedTransaction(QString::fromStdString(hexData));
        popup->SetOfflineMode(true);  // Hide broadcast button for offline mode
        popup->exec();
        
        // Check if user provided signed transaction
        QString signedTx;
        if (popup->GetSignedTransaction(&signedTx))
        {
            broadcastSignedTransaction(signedTx);
        }
    }
    catch (const UniValue& objError)
    {
        handleRPCError(objError, "Transaction preparation failed");
    }
    catch (const std::exception& e)
    {
        setResult("Transaction preparation failed", e.what());
    }
}

/**
 * @brief Broadcast a signed transaction to the network
 * @param signedTx Hex-encoded signed transaction
 */
void ZSendCoinsDialog::broadcastSignedTransaction(const QString &signedTx)
{
    try
    {
        UniValue sendParams(UniValue::VARR);
        sendParams.push_back(signedTx.toStdString());
        UniValue result = tableRPC.execute("sendrawtransaction", sendParams);
        
        if (result.isNull() || !result.isStr())
        {
            setResult("Transaction sending failed", "Invalid response from sendrawtransaction");
            return;
        }
        
        std::string txid = result.get_str();
        setResult("", "Transaction submitted. TxID=" + txid);
    }
    catch (const UniValue& objError)
    {
        handleRPCError(objError, "Transaction sending failed");
    }
    catch (...)
    {
        setResult("Transaction sending failed", 
                 "Could not parse the response. Please check the log");
    }
}

/**
 * @brief Handle RPC error responses
 * @param objError UniValue error object
 * @param context Error context description
 */
void ZSendCoinsDialog::handleRPCError(const UniValue& objError, const std::string& context)
{
    try
    {
        int code = find_value(objError, "code").get_int();
        std::string message = (code == -26) 
            ? "The transaction expired"
            : find_value(objError, "message").get_str();
        
        setResult(context, message.c_str());
    }
    catch (const std::runtime_error&)
    {
        setResult(context, "Could not parse the error response");
    }
}

/**
 * @brief Clear all form entries and reset to default state
 */
void ZSendCoinsDialog::clear()
{
    // Reset commission (legacy hardware wallet commission removed)
    commission = 0;
    ui->labelCommission->setVisible(false);
    ui->labelCommissionValue->setVisible(false);

    // Remove all entries except one
    while (ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();

    // Select the first address
    ui->payFromAddress->setCurrentIndex(0);

    // Hide result frame
    ui->teResult->clear();
    ui->frameResult->hide();
}

void ZSendCoinsDialog::reject()
{
    clear();
}

void ZSendCoinsDialog::accept()
{
    clear();
}

/**
 * @brief Add a new recipient entry to the form
 * @return Pointer to the newly created SendCoinsEntry
 */
SendCoinsEntry *ZSendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, true);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    
    // Connect entry signals
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(resetUnlockTimerEvent()), this, SLOT(sendResetUnlockSignal()));

    // Focus and scroll to new entry
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if (bar)
    {
        bar->setSliderPosition(bar->maximum());
    }
    
    entry->hideCheckboxSubtractFeeFromAmount();
    updateTabsAndLabels();
    
    return entry;
}

void ZSendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

/**
 * @brief Remove a recipient entry from the form
 * @param entry The entry to remove
 */
void ZSendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->hide();

    // Always maintain at least one entry
    if (ui->entries->count() == 1)
    {
        addEntry();
    }

    entry->deleteLater();
    updateTabsAndLabels();
}

QWidget *ZSendCoinsDialog::setupTabChain(QWidget *prev)
{
    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            prev = entry->setupTabChain(prev);
        }
    }
    QWidget::setTabOrder(prev, ui->sendButton);
    QWidget::setTabOrder(ui->sendButton, ui->clearButton);
    QWidget::setTabOrder(ui->clearButton, ui->addButton);
    QWidget::setTabOrder(ui->addButton, ui->refreshPayFrom);
    return ui->refreshPayFrom;
}

void ZSendCoinsDialog::setAddress(const QString &address)
{
    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setAddress(address);
}

void ZSendCoinsDialog::pasteEntry(const SendCoinsRecipient &rv)
{
    if(!fNewRecipientAllowed)
        return;

    SendCoinsEntry *entry = 0;
    // Replace the first entry if it is still unused
    if(ui->entries->count() == 1)
    {
        SendCoinsEntry *first = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(0)->widget());
        if(first->isClear())
        {
            entry = first;
        }
    }
    if(!entry)
    {
        entry = addEntry();
    }

    entry->setValue(rv);
    updateTabsAndLabels();
}

bool ZSendCoinsDialog::handlePaymentRequest(const SendCoinsRecipient &rv)
{
    // Just paste the entry, all pre-checks
    // are done in paymentserver.cpp.
    pasteEntry(rv);
    return true;
}

void ZSendCoinsDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance,
                                 const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance,
                                 const CAmount& privateWatchBalance, const CAmount& privateBalance, const CAmount& interestBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchOnlyBalance);
    Q_UNUSED(watchUnconfBalance);
    Q_UNUSED(watchImmatureBalance);
    Q_UNUSED(privateWatchBalance);
    Q_UNUSED(privateBalance);
    Q_UNUSED(interestBalance);

    if(model && model->getOptionsModel())
    {
        ui->labelBalance->setText(KomodoUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
        ui->labelBalance->setVisible(false);
        //ui->label->setVisible(false);
    }
}

/**
 * @brief Update display unit for currency amounts
 */
void ZSendCoinsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0, 0, 0, 0);
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());

    QString commissionStr = KomodoUnits::format(
        model->getOptionsModel()->getDisplayUnit(), commission);
    ui->labelCommissionValue->setText(commissionStr);

    updatePayFromList();
}

/**
 * @brief Display result message in the result frame
 * @param heading Result heading text (empty for default "Result:")
 * @param result Result message text
 */
void ZSendCoinsDialog::setResult(const std::string heading, const std::string result)
{
    if (heading.length() == 0)
    {
        ui->lbResult->setText(tr("Result: "));
    }
    else
    {
        ui->lbResult->setText(tr("Result: %1").arg(QString::fromStdString(heading)));
    }

    ui->teResult->setText(QString::fromStdString(result));
    ui->frameResult->show();
}

void ZSendCoinsDialog::processSendCoinsReturn(const WalletModel::SendCoinsReturn &sendCoinsReturn, const QString &msgArg)
{
    QPair<QString, CClientUIInterface::MessageBoxFlags> msgParams;
    // Default to a warning message, override if error message is needed
    msgParams.second = CClientUIInterface::MSG_WARNING;

    // This comment is specific to ZSendCoinsDialog usage of WalletModel::SendCoinsReturn.
    // WalletModel::TransactionCommitFailed is used only in WalletModel::sendCoins()
    // all others are used only in WalletModel::prepareTransaction()
    switch(sendCoinsReturn.status)
    {
    case WalletModel::InvalidFromAddress:
        msgParams.first = tr("Invalid from address, should be a taddr or zaddr.");
        break;
    case WalletModel::HaveNotSpendingKey:
        msgParams.first = tr("From address does not belong to this node, zaddr spending key not found.");
        break;
    case WalletModel::SendingBothSproutAndSapling:
        msgParams.first = tr("Cannot send to both Sprout and Sapling addresses.");
        break;
    case WalletModel::SproutUsageExpired:
        msgParams.first = tr("Sprout usage has expired.");
        break;
    case WalletModel::SproutUsageWillExpireSoon:
        msgParams.first = tr("Sprout usage will expire soon.");
        break;
    case WalletModel::SendBetweenSproutAndSapling:
        msgParams.first = tr("Cannot send between Sprout and Sapling addresses.");
        break;
    case WalletModel::InvalidAddress:
        msgParams.first = tr("The recipient address is not valid. Please recheck.");
        break;
    case WalletModel::InvalidAmount:
        msgParams.first = tr("The amount to pay must be larger than 0.");
        break;
    case WalletModel::TooManyZaddrs:
        msgParams.first = tr("Too many zaddr outputs.");
        break;
    case WalletModel::SaplingHasNotActivated:
        msgParams.first = tr("Sapling has not activated.");
        break;
    case WalletModel::LargeTransactionSize:
        msgParams.first = tr("Too many outputs, size of raw transaction would be larger than limit of %1 bytes").arg(MAX_TX_SIZE_AFTER_SAPLING);
        break;
    case WalletModel::TooLargeFeeForSmallTrans:
        msgParams.first = tr("Small transaction amount has fee that is greater than the default fee %1").arg(QString::fromStdString(FormatMoney(ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE)));
        break;
    case WalletModel::TooLargeFee:
        msgParams.first = tr("Fee %1 is greater than the sum of outputs and also greater than the default fee").arg(msgArg);
        break;
    case WalletModel::AmountExceedsBalance:
        msgParams.first = tr("The amount exceeds your source address balance.");
        break;
    case WalletModel::AmountWithFeeExceedsBalance:
        msgParams.first = tr("The total exceeds your balance when the %1 transaction fee is included.").arg(msgArg);
        break;
    case WalletModel::DuplicateAddress:
        msgParams.first = tr("Duplicate address found: addresses should only be used once each.");
        break;
    case WalletModel::TransactionCreationFailed:
        msgParams.first = tr("Transaction creation failed!");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::TransactionCommitFailed:
        msgParams.first = tr("The transaction was rejected with the following reason: %1").arg(sendCoinsReturn.reasonCommitFailed);
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    case WalletModel::AbsurdFee:
        msgParams.first = tr("A fee higher than %1 is considered an absurdly high fee.").arg(KomodoUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), maxTxFee));
        break;
    case WalletModel::PaymentRequestExpired:
        msgParams.first = tr("Payment request expired.");
        msgParams.second = CClientUIInterface::MSG_ERROR;
        break;
    // included to prevent a compiler warning.
    case WalletModel::OK:
    default:
        return;
    }

    Q_EMIT message(tr("Z-Send Coins"), msgParams.first, msgParams.second);
}

/**
 * @brief Calculate and set available balance for an entry
 * @param entry The entry to set the balance for
 */
void ZSendCoinsDialog::useAvailableBalance(SendCoinsEntry* entry)
{
    if (ui->payFromAddress->currentText().isEmpty())
    {
        entry->setAmount(0);
        return;
    }

    // Extract address from combo box text
    QString address;
    if (!ui->payFromAddress->currentText().split(' ').isEmpty())
    {
        address = ui->payFromAddress->currentText().split(' ').at(2);
    }

    // Calculate available amount
    CAmount amount = 0;
    if (!address.isEmpty())
    {
        amount = model->getAddressBalance(address.toStdString());
        
        // Reserve funds for transaction fee
        if (ui->customFee->value() > 0)
        {
            amount -= ui->customFee->value();
        }

        // Reset commission (legacy hardware wallet commission removed)
        commission = 0;
        
        // Subtract amounts from other entries
        for (int i = 0; i < ui->entries->count(); ++i)
        {
            SendCoinsEntry* e = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if (e && !e->isHidden() && e != entry)
            {
                amount -= e->getValue().amount;
            }
        }
    }

    if (amount > 0)
    {
        entry->checkSubtractFeeFromAmount();
        entry->setAmount(amount);
    }
    else
    {
        entry->setAmount(0);
    }
}

void ZSendCoinsDialog::updateCoinControlState(CCoinControl& ctrl)
{
    ctrl.m_feerate = CFeeRate(ui->customFee->value());
}

/**
 * @brief Update button text when address selection changes
 * @param index The new index in the address combobox
 */
void ZSendCoinsDialog::payFromAddressIndexChanged(int index)
{
    if (index >= 0)
    {
        QString text = ui->payFromAddress->currentText();
        bool isOffline = text.contains("Off-line");
        
        ui->sendButton->setText(isOffline 
            ? tr("Create Unsigned Transaction")
            : tr("S&end Transaction"));
        
        // Update tooltip to explain the action
        ui->sendButton->setToolTip(isOffline
            ? tr("Create transaction builder data for offline signing with private keys")
            : tr("Sign and broadcast transaction to the network"));
    }
    else
    {
        ui->sendButton->setText(tr("S&end Transaction"));
        ui->sendButton->setToolTip(tr("Sign and broadcast transaction to the network"));
    }
    
    coinControlUpdateLabels();
}

/**
 * @brief Update coin control labels and commission display
 */
void ZSendCoinsDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl);

    // Clear payment amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    // Calculate total spend and collect amounts
    for (int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if (entry && !entry->isHidden())
        {
            SendCoinsRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
            
            if (rcp.fSubtractFeeFromAmount)
            {
                CoinControlDialog::fSubtractFeeFromAmount = true;
            }
        }
    }

    // Reset commission (legacy hardware wallet commission removed)
    commission = 0;
    ui->labelCommission->setVisible(false);
    ui->labelCommissionValue->setVisible(false);

    // Update coin control dialog labels if selections exist
    if (CoinControlDialog::coinControl->HasSelected())
    {
        CoinControlDialog::updateLabels(model, this);
    }
}

/**
 * @brief Refresh the list of available z-addresses for spending
 */
void ZSendCoinsDialog::updatePayFromList()
{
    ui->payFromAddress->clear();

    // Get all z-address balances
    std::map<libzcash::PaymentAddress, CAmount> allBalances = 
        model->getZAddressBalances(1, false);
    std::map<libzcash::PaymentAddress, CAmount> ownedBalances = 
        model->getZAddressBalances(1, true);

    auto displayUnit = model->getOptionsModel()->getDisplayUnit();
    
    // Populate address list with addresses that have balances > 0
    for (const auto &pair : allBalances)
    {
        libzcash::PaymentAddress address = pair.first;
        CAmount balance = pair.second;
        
        // Skip addresses with zero balance
        if (balance <= 0)
            continue;
        
        QString addressStr = QString::fromStdString(EncodePaymentAddress(address));
        QString amountStr = KomodoUnits::formatWithUnit(displayUnit, balance);

        auto search = ownedBalances.find(address);
        if (search != ownedBalances.end())
        {
            // Address with spending key - normal transaction flow
            ui->payFromAddress->addItem(tr("(%1) %2").arg(amountStr).arg(addressStr));
        }
        else
        {
            // Viewing key only - automatically use offline transaction preparation
            ui->payFromAddress->addItem(
                tr("(%1) %2 - Off-line Transaction").arg(amountStr).arg(addressStr));
        }
    }

    payFromAddressIndexChanged(ui->payFromAddress->currentIndex());
}

ZSendConfirmationDialog::ZSendConfirmationDialog(const QString &title, const QString &text, int _secDelay,
    QWidget *parent) :
    QMessageBox(QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::Cancel, parent), secDelay(_secDelay)
{
    setDefaultButton(QMessageBox::Cancel);
    yesButton = button(QMessageBox::Yes);
    updateYesButton();
    connect(&countDownTimer, SIGNAL(timeout()), this, SLOT(countDown()));
}

int ZSendConfirmationDialog::exec()
{
    updateYesButton();
    countDownTimer.start(1000);
    return QMessageBox::exec();
}

void ZSendConfirmationDialog::countDown()
{
    secDelay--;
    updateYesButton();

    if(secDelay <= 0)
    {
        countDownTimer.stop();
    }
}

void ZSendConfirmationDialog::updateYesButton()
{
    if(secDelay > 0)
    {
        yesButton->setEnabled(false);
        yesButton->setText(tr("Yes") + " (" + QString::number(secDelay) + ")");
    }
    else
    {
        yesButton->setEnabled(true);
        yesButton->setText(tr("Yes"));
    }
}
