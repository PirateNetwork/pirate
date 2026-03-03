// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "verifypaymentdisclosuredialog.h"
#include "ui_verifypaymentdisclosuredialog.h"

#include "guiutil.h"
#include "guiconstants.h"
#include "paymentdisclosure.h"
#include "main.h"
#include "utilmoneystr.h"

#include <QMessageBox>

VerifyPaymentDisclosureDialog::VerifyPaymentDisclosureDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VerifyPaymentDisclosureDialog),
    model(nullptr)
{
    ui->setupUi(this);

    // Connect signals
    connect(ui->verifyButton, SIGNAL(clicked()), this, SLOT(onVerifyButtonClicked()));
    connect(ui->clearButton, SIGNAL(clicked()), this, SLOT(onClearButtonClicked()));
    connect(ui->closeButton, SIGNAL(clicked()), this, SLOT(onCloseButtonClicked()));
}

VerifyPaymentDisclosureDialog::~VerifyPaymentDisclosureDialog()
{
    delete ui;
}

void VerifyPaymentDisclosureDialog::setModel(WalletModel *model)
{
    this->model = model;
}

void VerifyPaymentDisclosureDialog::onVerifyButtonClicked()
{
    QString disclosure = ui->disclosureInput->toPlainText().trimmed();
    
    if (disclosure.isEmpty()) {
        displayError("Please enter a payment disclosure key.");
        return;
    }

    LOCK(cs_main);

    // Use the unified verification function that auto-detects type
    std::string disclosureStr = disclosure.toStdString();
    UnifiedDisclosureVerificationResult result = VerifyPaymentDisclosure(disclosureStr);
    
    if (!result.success) {
        displayError(QString::fromStdString(result.error));
        return;
    }

    // Convert value to readable format
    QString valueStr = QString::fromStdString(FormatMoney(result.value));
    QString addressStr = QString::fromStdString(result.address);
    QString txidStr = QString::fromStdString(result.txid.GetHex());
    QString memoHex = QString::fromStdString(result.memoHex);
    QString type = QString::fromStdString(result.disclosureType);

    displayResults(type, txidStr, result.outputIndex, valueStr, addressStr, memoHex);
}

void VerifyPaymentDisclosureDialog::displayError(const QString& error)
{
    ui->resultsDisplay->clear();
    QString errorColor = COLOR_NEGATIVE_DARK.name();
    ui->resultsDisplay->setHtml("<b style='color:" + errorColor + ";'>Error:</b><br>" + GUIUtil::HtmlEscape(error));
}

void VerifyPaymentDisclosureDialog::displayResults(const QString& type, const QString& txid, 
                                                   int index, const QString& value, 
                                                   const QString& address, const QString& memo)
{
    QString successColor = COLOR_POSITIVE_DARK.name();
    QString html = "<b style='color:" + successColor + ";'>Verification Successful!</b><br><br>";
    html += "<b>Disclosure Type:</b> " + GUIUtil::HtmlEscape(type) + "<br>";
    html += "<b>Transaction ID:</b> " + GUIUtil::HtmlEscape(txid) + "<br>";
    html += QString("<b>") + (type == "Sapling" ? "Output Index:" : "Action Index:") + "</b> " + QString::number(index) + "<br>";
    html += "<b>Value:</b> " + GUIUtil::HtmlEscape(value) + "<br>";
    html += "<b>Address:</b> " + GUIUtil::HtmlEscape(address) + "<br>";
    html += "<b>Memo (hex):</b> " + GUIUtil::HtmlEscape(memo) + "<br>";

    ui->resultsDisplay->setHtml(html);
}

void VerifyPaymentDisclosureDialog::onClearButtonClicked()
{
    ui->disclosureInput->clear();
    ui->resultsDisplay->clear();
}

void VerifyPaymentDisclosureDialog::onCloseButtonClicked()
{
    close();
}
