// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletoptionspage.h"
#include "ui_walletoptionspage.h"

#include "amount.h"
#include "walletmodel.h"

#include <cmath>

#include <QMessageBox>
#include <QTimer>

namespace {
double AmountToCoins(CAmount amount) { return (double)amount / (double)COIN; }
CAmount CoinsToAmount(double coins) { return (CAmount)llround(coins * (double)COIN); }
} // namespace

WalletOptionsPage::WalletOptionsPage(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::WalletOptionsPage),
    walletModel(0)
{
    ui->setupUi(this);

    connect(ui->saplingApplyButton, SIGNAL(clicked()), this, SLOT(applySaplingConsolidation()));
    connect(ui->ironwoodApplyButton, SIGNAL(clicked()), this, SLOT(applyIronwoodConsolidation()));
    connect(ui->sweepApplyButton, SIGNAL(clicked()), this, SLOT(applySweep()));
    connect(ui->feesApplyButton, SIGNAL(clicked()), this, SLOT(applyFeesBehaviorPruning()));

    setEnabled(false); // enabled once setWalletModel() gives it a wallet to work with

    statusTimer = new QTimer(this);
    connect(statusTimer, SIGNAL(timeout()), this, SLOT(refreshStatus()));
    statusTimer->start(5000);
}

WalletOptionsPage::~WalletOptionsPage()
{
    delete ui;
}

void WalletOptionsPage::setWalletModel(WalletModel *model)
{
    walletModel = model;
    setEnabled(model != 0);
    // Base title, not windowTitle(): this window is retargeted in place every
    // time the displayed wallet changes (unlike AskPassphraseDialog, which
    // this "%1 -- %2" convention is borrowed from and which is always
    // constructed fresh) -- reading windowTitle() back here would compound a
    // previous wallet's name into the next one instead of replacing it.
    setWindowTitle(model ? tr("Wallet Options -- %1").arg(model->getWalletName()) : tr("Wallet Options"));
    if (model) {
        loadFromModel();
        refreshStatus();
    }
}

void WalletOptionsPage::loadFromModel()
{
    if (!walletModel)
        return;

    // Clear any "Updated: ..." confirmation left over from whichever wallet
    // this window was previously scoped to -- it describes a different
    // wallet's settings now, not this one's.
    ui->saplingApplyResultLabel->clear();
    ui->ironwoodApplyResultLabel->clear();
    ui->sweepApplyResultLabel->clear();
    ui->feesApplyResultLabel->clear();

    ui->saplingEnabledCheck->setChecked(walletModel->getSaplingConsolidationEnabled());
    ui->saplingIntervalSpin->setValue(walletModel->getSaplingConsolidationInterval());
    ui->saplingTargetQtySpin->setValue(walletModel->getSaplingConsolidationTargetQty());
    ui->saplingFeeSpin->setValue(AmountToCoins(walletModel->getSaplingConsolidationTxFee()));
    ui->saplingAddressesEdit->setText(walletModel->getSaplingConsolidationAddresses());

    ui->ironwoodEnabledCheck->setChecked(walletModel->getIronwoodConsolidationEnabled());
    ui->ironwoodIntervalSpin->setValue(walletModel->getIronwoodConsolidationInterval());
    ui->ironwoodTargetQtySpin->setValue(walletModel->getIronwoodConsolidationTargetQty());
    ui->ironwoodFeeSpin->setValue(AmountToCoins(walletModel->getIronwoodConsolidationTxFee()));
    ui->ironwoodAddressesEdit->setText(walletModel->getIronwoodConsolidationAddresses());

    ui->sweepEnabledCheck->setChecked(walletModel->getSweepEnabled());
    ui->sweepIntervalSpin->setValue(walletModel->getSweepInterval());
    ui->sweepFeeSpin->setValue(AmountToCoins(walletModel->getSweepTxFee()));
    ui->sweepAddressEdit->setText(walletModel->getSweepAddress());

    ui->payTxFeeSpin->setValue(AmountToCoins(walletModel->getPayTxFee()));
    ui->minTxFeeSpin->setValue(AmountToCoins(walletModel->getMinTxFee()));
    ui->txConfirmTargetSpin->setValue((int)walletModel->getTxConfirmTarget());
    ui->minTxValueSpin->setValue(AmountToCoins(walletModel->getMinTxValue()));
    ui->keypoolSizeSpin->setValue((int)walletModel->getKeypoolSizeTarget());
    ui->txDeleteEnabledCheck->setChecked(walletModel->getTxDeleteEnabled());
    ui->txConflictDeleteEnabledCheck->setChecked(walletModel->getTxConflictDeleteEnabled());
    ui->deleteIntervalSpin->setValue(walletModel->getDeleteInterval());
    ui->keepTxForNBlocksSpin->setValue((int)walletModel->getKeepTransactionsAfterNBlocks());
    ui->keepLastNTxSpin->setValue((int)walletModel->getKeepLastNTransactions());
}

void WalletOptionsPage::refreshStatus()
{
    if (!walletModel || !isEnabled())
        return;

    ui->saplingStatusLabel->setText(walletModel->getSaplingConsolidationRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextSaplingConsolidation()));
    ui->ironwoodStatusLabel->setText(walletModel->getIronwoodConsolidationRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextIronwoodConsolidation()));
    ui->sweepStatusLabel->setText(walletModel->getSweepRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextSweep()));
}

void WalletOptionsPage::applySaplingConsolidation()
{
    if (!walletModel)
        return;
    walletModel->setSaplingConsolidationEnabled(ui->saplingEnabledCheck->isChecked());
    walletModel->setSaplingConsolidationInterval(ui->saplingIntervalSpin->value());
    walletModel->setSaplingConsolidationTargetQty(ui->saplingTargetQtySpin->value());
    walletModel->setSaplingConsolidationTxFee(CoinsToAmount(ui->saplingFeeSpin->value()));
    bool addressesOk = walletModel->setSaplingConsolidationAddresses(ui->saplingAddressesEdit->text());
    if (!addressesOk) {
        QMessageBox::warning(this, tr("Sapling Consolidation"),
            tr("One or more addresses are invalid, not Sapling addresses, or this wallet does not "
               "have the spending key for them. The address filter was not changed."));
    }
    ui->saplingAddressesEdit->setText(walletModel->getSaplingConsolidationAddresses());
    refreshStatus();

    // Read back from walletModel rather than the raw UI fields, so this
    // reflects what's actually now set even when the address filter above
    // was rejected and left unchanged.
    QString addresses = walletModel->getSaplingConsolidationAddresses();
    ui->saplingApplyResultLabel->setText(
        tr("Updated: %1, interval %2 blocks, target %3 notes, fee %4 ARRR, addresses: %5%6")
            .arg(walletModel->getSaplingConsolidationEnabled() ? tr("enabled") : tr("disabled"))
            .arg(walletModel->getSaplingConsolidationInterval())
            .arg(walletModel->getSaplingConsolidationTargetQty())
            .arg(AmountToCoins(walletModel->getSaplingConsolidationTxFee()), 0, 'f', 8)
            .arg(addresses.isEmpty() ? tr("all") : addresses)
            .arg(addressesOk ? QString() : tr(" (address filter unchanged -- see warning)")));
}

void WalletOptionsPage::applyIronwoodConsolidation()
{
    if (!walletModel)
        return;
    walletModel->setIronwoodConsolidationEnabled(ui->ironwoodEnabledCheck->isChecked());
    walletModel->setIronwoodConsolidationInterval(ui->ironwoodIntervalSpin->value());
    walletModel->setIronwoodConsolidationTargetQty(ui->ironwoodTargetQtySpin->value());
    walletModel->setIronwoodConsolidationTxFee(CoinsToAmount(ui->ironwoodFeeSpin->value()));
    bool addressesOk = walletModel->setIronwoodConsolidationAddresses(ui->ironwoodAddressesEdit->text());
    if (!addressesOk) {
        QMessageBox::warning(this, tr("Ironwood Consolidation"),
            tr("One or more addresses are invalid, not Ironwood addresses, or this wallet does not "
               "have the spending key for them. The address filter was not changed."));
    }
    ui->ironwoodAddressesEdit->setText(walletModel->getIronwoodConsolidationAddresses());
    refreshStatus();

    QString addresses = walletModel->getIronwoodConsolidationAddresses();
    ui->ironwoodApplyResultLabel->setText(
        tr("Updated: %1, interval %2 blocks, target %3 notes, fee %4 ARRR, addresses: %5%6")
            .arg(walletModel->getIronwoodConsolidationEnabled() ? tr("enabled") : tr("disabled"))
            .arg(walletModel->getIronwoodConsolidationInterval())
            .arg(walletModel->getIronwoodConsolidationTargetQty())
            .arg(AmountToCoins(walletModel->getIronwoodConsolidationTxFee()), 0, 'f', 8)
            .arg(addresses.isEmpty() ? tr("all") : addresses)
            .arg(addressesOk ? QString() : tr(" (address filter unchanged -- see warning)")));
}

void WalletOptionsPage::applySweep()
{
    if (!walletModel)
        return;

    walletModel->setSweepEnabled(ui->sweepEnabledCheck->isChecked());
    walletModel->setSweepInterval(ui->sweepIntervalSpin->value());
    walletModel->setSweepTxFee(CoinsToAmount(ui->sweepFeeSpin->value()));

    // Auto-detects which pool the address belongs to (or that it's neither)
    // -- see WalletModel::setSweepAddress()'s own doc comment.
    bool addressOk = walletModel->setSweepAddress(ui->sweepAddressEdit->text().trimmed());
    if (!addressOk) {
        QMessageBox::warning(this, tr("Sweep"),
            tr("That address doesn't decode as a Sapling or Ironwood address this wallet holds the "
               "spending key for. The sweep destination was not changed."));
    }
    ui->sweepAddressEdit->setText(walletModel->getSweepAddress());
    refreshStatus();

    QString addr = walletModel->getSweepAddress();
    ui->sweepApplyResultLabel->setText(
        tr("Updated: %1, interval %2 blocks, fee %3 ARRR, destination: %4%5")
            .arg(walletModel->getSweepEnabled() ? tr("enabled") : tr("disabled"))
            .arg(walletModel->getSweepInterval())
            .arg(AmountToCoins(walletModel->getSweepTxFee()), 0, 'f', 8)
            .arg(addr.isEmpty() ? tr("not configured") : addr)
            .arg(addressOk ? QString() : tr(" (destination unchanged -- see warning)")));
}

void WalletOptionsPage::applyFeesBehaviorPruning()
{
    if (!walletModel)
        return;
    walletModel->setPayTxFee(CoinsToAmount(ui->payTxFeeSpin->value()));
    walletModel->setMinTxFee(CoinsToAmount(ui->minTxFeeSpin->value()));
    walletModel->setTxConfirmTarget((unsigned int)ui->txConfirmTargetSpin->value());
    walletModel->setMinTxValue(CoinsToAmount(ui->minTxValueSpin->value()));
    walletModel->setKeypoolSizeTarget(ui->keypoolSizeSpin->value());
    walletModel->setTxDeleteEnabled(ui->txDeleteEnabledCheck->isChecked());
    walletModel->setTxConflictDeleteEnabled(ui->txConflictDeleteEnabledCheck->isChecked());
    walletModel->setDeleteInterval(ui->deleteIntervalSpin->value());
    walletModel->setKeepTransactionsAfterNBlocks((unsigned int)ui->keepTxForNBlocksSpin->value());
    walletModel->setKeepLastNTransactions((unsigned int)ui->keepLastNTxSpin->value());

    ui->feesApplyResultLabel->setText(
        tr("Updated: pay fee %1 ARRR/kB, min fee %2 ARRR/kB, confirm target %3 blocks, min note value "
           "%4 ARRR, keypool %5, delete spent: %6, delete conflicted: %7, delete-check every %8 blocks, "
           "retain %9 blocks / last %10 tx")
            .arg(AmountToCoins(walletModel->getPayTxFee()), 0, 'f', 8)
            .arg(AmountToCoins(walletModel->getMinTxFee()), 0, 'f', 8)
            .arg(walletModel->getTxConfirmTarget())
            .arg(AmountToCoins(walletModel->getMinTxValue()), 0, 'f', 8)
            .arg(walletModel->getKeypoolSizeTarget())
            .arg(walletModel->getTxDeleteEnabled() ? tr("yes") : tr("no"))
            .arg(walletModel->getTxConflictDeleteEnabled() ? tr("yes") : tr("no"))
            .arg(walletModel->getDeleteInterval())
            .arg(walletModel->getKeepTransactionsAfterNBlocks())
            .arg(walletModel->getKeepLastNTransactions()));
}
