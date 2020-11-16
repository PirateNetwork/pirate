// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sendcoinsdialog.h"
#include "zsendcoinsdialog.h"
#include "ui_zsendcoinsdialog.h"

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

    ui->payFromAddress->setMaxVisibleItems(10);
    ui->payFromAddress->setStyleSheet("QComboBox { combobox-popup: 0; }");

    if (!_platformStyle->getImagesOnButtons()) {
        ui->addButton->setIcon(QIcon());
        ui->clearButton->setIcon(QIcon());
        ui->sendButton->setIcon(QIcon());
        ui->refreshPayFrom->setIcon(QIcon());
    } else {
        ui->addButton->setIcon(_platformStyle->SingleColorIcon(":/icons/add"));
        ui->clearButton->setIcon(_platformStyle->SingleColorIcon(":/icons/remove"));
        ui->sendButton->setIcon(_platformStyle->SingleColorIcon(":/icons/send"));
        ui->refreshPayFrom->setIcon(platformStyle->SingleColorIcon(":/icons/refresh"));
    }

    addEntry();

    connect(ui->addButton, SIGNAL(clicked()), this, SLOT(addEntry()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(ui->refreshPayFrom, SIGNAL(clicked()), this, SLOT(updatePayFromList()));

    // init transaction fee section
    QSettings settings;
    ui->customFee->setValue(ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE);
    setOperationId("");
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

        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getImmatureBalance(),
                   _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchImmatureBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        // Coin Control
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(coinControlUpdateLabels()));
        ui->frameCoinControl->setVisible(true); // frame "coin control" should be always visible, bcz it contains PayFrom combobox
        coinControlUpdateLabels();

        connect(ui->payFromAddress, SIGNAL(currentIndexChanged(int)), this, SLOT(coinControlUpdateLabels()));
        connect(ui->customFee, SIGNAL(valueChanged()), this, SLOT(coinControlUpdateLabels()));
        ui->customFee->setSingleStep(GetRequiredFee(1000));
    }
}

ZSendCoinsDialog::~ZSendCoinsDialog()
{
    delete ui;
}

void ZSendCoinsDialog::on_sendButton_clicked()
{
    if(!model || !model->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    bool valid = true;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry)
        {
            if(entry->validate(true))
            {
                recipients.append(entry->getValue());
            }
            else
            {
                valid = false;
            }
        }
    }

    if(!valid || recipients.isEmpty())
    {
        return;
    }

    QString fromaddress;
    if (!ui->payFromAddress->currentText().split(' ').isEmpty()) fromaddress = ui->payFromAddress->currentText().split(' ').at(1);

    if (fromaddress.isEmpty()) return;

    fNewRecipientAllowed = false;
    WalletModel::UnlockContext ctx(model->requestUnlock());
    if(!ctx.isValid())
    {
        // Unlock wallet was cancelled
        fNewRecipientAllowed = true;
        return;
    }

    // prepare transaction for getting txFee earlier
    WalletModelZTransaction currentTransaction(fromaddress, recipients, ui->customFee->value());
    WalletModel::SendCoinsReturn prepareStatus;

    // Always use a CCoinControl instance, use the CoinControlDialog instance if CoinControl has been enabled
    CCoinControl ctrl;
    if (model->getOptionsModel()->getCoinControlFeatures())
        ctrl = *CoinControlDialog::coinControl;

    updateCoinControlState(ctrl);

    prepareStatus = model->prepareZTransaction(currentTransaction, ctrl);

    // process prepareStatus and on error generate message shown to user
    processSendCoinsReturn(prepareStatus,
        KomodoUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), currentTransaction.getTransactionFee()));

    if(prepareStatus.status != WalletModel::OK) {
        fNewRecipientAllowed = true;
        return;
    }

    CAmount txFee = currentTransaction.getTransactionFee();

    // Format confirmation message
    QStringList formatted;
    for (const SendCoinsRecipient &rcp : currentTransaction.getRecipients())
    {
        // generate bold amount string
        QString amount = "<b>" + KomodoUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), rcp.amount);
        amount.append("</b>");
        // generate monospace address string
        QString address = "<span style='font-family: monospace;'>" + rcp.address;
        address.append("</span>");

        QString recipientElement;

        if(rcp.label.length() > 0) // label with address
        {
            recipientElement = tr("%1 to %2").arg(amount, GUIUtil::HtmlEscape(rcp.label));
            recipientElement.append(QString(" (%1)").arg(address));
        }
        else // just address
        {
            recipientElement = tr("%1 to %2").arg(amount, address);
        }

        formatted.append(recipientElement);
    }

    QString questionString = tr("Are you sure you want to send?");
    questionString.append("<br /><br />%1");

    if(txFee > 0)
    {
        // append fee string if a fee is required
        questionString.append("<hr /><span style='color:#aa0000;'>");
        questionString.append(KomodoUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), txFee));
        questionString.append("</span> ");
        questionString.append(tr("added as transaction fee"));

        // append transaction size
//        questionString.append(" (" + QString::number((double)currentTransaction.getTransactionSize() / 1000) + " kB)");
    }

    // add total amount in all subdivision units
    questionString.append("<hr />");
    CAmount totalAmount = currentTransaction.getTotalTransactionAmount() + txFee;
    QStringList alternativeUnits;
    for (KomodoUnits::Unit u : KomodoUnits::availableUnits())
    {
        if(u != model->getOptionsModel()->getDisplayUnit())
            alternativeUnits.append(KomodoUnits::formatHtmlWithUnit(u, totalAmount));
    }
    questionString.append(tr("Total Amount %1")
        .arg(KomodoUnits::formatHtmlWithUnit(model->getOptionsModel()->getDisplayUnit(), totalAmount)));
    questionString.append(QString("<span style='font-size:10pt;font-weight:normal;'><br />(=%2)</span>")
        .arg(alternativeUnits.join(" " + tr("or") + "<br />")));

    SendConfirmationDialog confirmationDialog(tr("Confirm send coins"),
        questionString.arg(formatted.join("<br />")), SEND_CONFIRM_DELAY, this);
    confirmationDialog.exec();
    QMessageBox::StandardButton retval = (QMessageBox::StandardButton)confirmationDialog.result();

    if(retval != QMessageBox::Yes)
    {
        fNewRecipientAllowed = true;
        return;
    }

    // now send the prepared transaction
    WalletModel::SendCoinsReturn sendStatus = model->zsendCoins(currentTransaction);
    // process sendStatus and on error generate message shown to user
    processSendCoinsReturn(sendStatus);

    if (sendStatus.status == WalletModel::OK)
    {
        accept();
        CoinControlDialog::coinControl->UnSelectAll();
        coinControlUpdateLabels();
    }
    fNewRecipientAllowed = true;

    setOperationId(currentTransaction.getOperationId());
}

void ZSendCoinsDialog::clear()
{
    // Remove entries until only one left
    while(ui->entries->count())
    {
        ui->entries->takeAt(0)->widget()->deleteLater();
    }
    addEntry();

    updateTabsAndLabels();
}

void ZSendCoinsDialog::reject()
{
    clear();
}

void ZSendCoinsDialog::accept()
{
    clear();
}

SendCoinsEntry *ZSendCoinsDialog::addEntry()
{
    SendCoinsEntry *entry = new SendCoinsEntry(platformStyle, this, true);
    entry->setModel(model);
    ui->entries->addWidget(entry);
    connect(entry, SIGNAL(removeEntry(SendCoinsEntry*)), this, SLOT(removeEntry(SendCoinsEntry*)));
    connect(entry, SIGNAL(useAvailableBalance(SendCoinsEntry*)), this, SLOT(useAvailableBalance(SendCoinsEntry*)));
    connect(entry, SIGNAL(payAmountChanged()), this, SLOT(coinControlUpdateLabels()));
    connect(entry, SIGNAL(subtractFeeFromAmountChanged()), this, SLOT(coinControlUpdateLabels()));

    // Focus the field, so that entry can start immediately
    entry->clear();
    entry->setFocus();
    ui->scrollAreaWidgetContents->resize(ui->scrollAreaWidgetContents->sizeHint());
    qApp->processEvents();
    QScrollBar* bar = ui->scrollArea->verticalScrollBar();
    if(bar)
        bar->setSliderPosition(bar->maximum());
    entry->hideCheckboxSubtractFeeFromAmount();

    updateTabsAndLabels();
    return entry;
}

void ZSendCoinsDialog::updateTabsAndLabels()
{
    setupTabChain(0);
    coinControlUpdateLabels();
}

void ZSendCoinsDialog::removeEntry(SendCoinsEntry* entry)
{
    entry->hide();

    // If the last entry is about to be removed add an empty one
    if (ui->entries->count() == 1)
        addEntry();

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
                                 const CAmount& watchBalance, const CAmount& watchUnconfirmedBalance, const CAmount& watchImmatureBalance)
{
    Q_UNUSED(unconfirmedBalance);
    Q_UNUSED(immatureBalance);
    Q_UNUSED(watchBalance);
    Q_UNUSED(watchUnconfirmedBalance);
    Q_UNUSED(watchImmatureBalance);

    if(model && model->getOptionsModel())
    {
        ui->labelBalance->setText(KomodoUnits::formatWithUnit(model->getOptionsModel()->getDisplayUnit(), balance));
    }
}

void ZSendCoinsDialog::updateDisplayUnit()
{
    setBalance(model->getBalance(), 0, 0, 0, 0, 0);
    ui->customFee->setDisplayUnit(model->getOptionsModel()->getDisplayUnit());
}

void ZSendCoinsDialog::setOperationId(const AsyncRPCOperationId& operationId)
{
    ui->operationId->setText(QString::fromStdString(operationId));
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

void ZSendCoinsDialog::useAvailableBalance(SendCoinsEntry* entry)
{
    // Get CCoinControl instance if CoinControl is enabled or create a new one.
    CCoinControl coin_control;
    if (model->getOptionsModel()->getCoinControlFeatures()) {
        coin_control = *CoinControlDialog::coinControl;
    }

    if (ui->payFromAddress->currentText().isEmpty())
    {
        entry->setAmount(0);
        return;
    }

    QString address;
    if (!ui->payFromAddress->currentText().split(' ').isEmpty()) address = ui->payFromAddress->currentText().split(' ').at(1);

    // Calculate available amount to send.
    CAmount amount = 0;

    if (!address.isEmpty())
    {
        amount = model->getAddressBalance(address.toStdString());
        for (int i = 0; i < ui->entries->count(); ++i) {
            SendCoinsEntry* e = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
            if (e && !e->isHidden() && e != entry) {
                amount -= e->getValue().amount;
            }
        }
    }

    if (amount > 0) {
      entry->checkSubtractFeeFromAmount();
      entry->setAmount(amount);
    } else {
      entry->setAmount(0);
    }
}

void ZSendCoinsDialog::updateCoinControlState(CCoinControl& ctrl)
{
    ctrl.m_feerate = CFeeRate(ui->customFee->value());
}

// Coin Control: update labels
void ZSendCoinsDialog::coinControlUpdateLabels()
{
    if (!model || !model->getOptionsModel())
        return;

    updateCoinControlState(*CoinControlDialog::coinControl);

    // set pay amounts
    CoinControlDialog::payAmounts.clear();
    CoinControlDialog::fSubtractFeeFromAmount = false;

    for(int i = 0; i < ui->entries->count(); ++i)
    {
        SendCoinsEntry *entry = qobject_cast<SendCoinsEntry*>(ui->entries->itemAt(i)->widget());
        if(entry && !entry->isHidden())
        {
            SendCoinsRecipient rcp = entry->getValue();
            CoinControlDialog::payAmounts.append(rcp.amount);
            if (rcp.fSubtractFeeFromAmount)
                CoinControlDialog::fSubtractFeeFromAmount = true;
        }
    }

    if (CoinControlDialog::coinControl->HasSelected())
    {
        // actual coin control calculation
        CoinControlDialog::updateLabels(model, this);
    }
}

void ZSendCoinsDialog::updatePayFromList()
{
    ui->payFromAddress->clear();

    typedef std::function<bool(std::pair<CTxDestination, CAmount>, std::pair<CTxDestination, CAmount>)> ComparatorT;

    ComparatorT compFunctorT = [](std::pair<CTxDestination, CAmount> elem1 ,std::pair<CTxDestination, CAmount> elem2)
    {
        double v1 = ValueFromAmount(elem1.second).get_real();
        double v2 = ValueFromAmount(elem2.second).get_real();
        std::string a1 = EncodeDestination(elem1.first);
        std::string a2 = EncodeDestination(elem2.first);

        return (v1 > v2) || ( (v1 == v2) && ( a1 > a2 ) );
    };

    std::map<CTxDestination, CAmount> balances = model->getTAddressBalances();

    std::set<std::pair<CTxDestination, CAmount>, ComparatorT> balances_sorted(balances.begin(), balances.end(), compFunctorT);

    for (const std::pair<CTxDestination, CAmount>& item : balances_sorted)
    {
        if (ValueFromAmount(item.second).get_real() != 0.0)
            ui->payFromAddress->addItem(tr("(%1) %2").arg(QString::number(ValueFromAmount(item.second).get_real(),'f',8))
                                                     .arg(QString::fromStdString(EncodeDestination(item.first))));
    }


    typedef std::function<bool(std::pair<libzcash::PaymentAddress, CAmount>, std::pair<libzcash::PaymentAddress, CAmount>)> ComparatorZ;

    ComparatorZ compFunctorZ = [](std::pair<libzcash::PaymentAddress, CAmount> elem1 ,std::pair<libzcash::PaymentAddress, CAmount> elem2)
    {
        double v1 = ValueFromAmount(elem1.second).get_real();
        double v2 = ValueFromAmount(elem2.second).get_real();
        std::string a1 = EncodePaymentAddress(elem1.first);
        std::string a2 = EncodePaymentAddress(elem2.first);

        return (v1 > v2) || ( (v1 == v2) && ( a1 > a2 ) );
    };

    std::map<libzcash::PaymentAddress, CAmount> zbalances = model->getZAddressBalances();

    std::set<std::pair<libzcash::PaymentAddress, CAmount>, ComparatorZ> zbalances_sorted(zbalances.begin(), zbalances.end(), compFunctorZ);

    for (const std::pair<libzcash::PaymentAddress, CAmount>& item : zbalances_sorted)
    {
        if (ValueFromAmount(item.second).get_real() != 0.0)
            ui->payFromAddress->addItem(tr("(%1) %2").arg(QString::number(ValueFromAmount(item.second).get_real(),'f',8))
                                                     .arg(QString::fromStdString(EncodePaymentAddress(item.first))));
    }
}
