// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletsettingspage.h"

#include "amount.h"
#include "walletmodel.h"

#include <cmath>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace {
double AmountToCoins(CAmount amount) { return (double)amount / (double)COIN; }
CAmount CoinsToAmount(double coins) { return (CAmount)llround(coins * (double)COIN); }
} // namespace

WalletSettingsPage::WalletSettingsPage(QWidget *parent) :
    QWidget(parent),
    walletModel(0)
{
    QVBoxLayout *outerLayout = new QVBoxLayout(this);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    outerLayout->addWidget(scrollArea);

    QWidget *scrollContent = new QWidget(scrollArea);
    QVBoxLayout *layout = new QVBoxLayout(scrollContent);

    // --- Sapling consolidation ---
    QGroupBox *saplingGroup = new QGroupBox(tr("Sapling Consolidation"), scrollContent);
    QFormLayout *saplingForm = new QFormLayout(saplingGroup);
    saplingEnabledCheck = new QCheckBox(tr("Enabled"), saplingGroup);
    saplingForm->addRow(saplingEnabledCheck);
    saplingIntervalSpin = new QSpinBox(saplingGroup);
    saplingIntervalSpin->setRange(1, 1000000000);
    saplingForm->addRow(tr("Interval (blocks):"), saplingIntervalSpin);
    saplingTargetQtySpin = new QSpinBox(saplingGroup);
    saplingTargetQtySpin->setRange(1, 100000);
    saplingForm->addRow(tr("Target note quantity:"), saplingTargetQtySpin);
    saplingFeeSpin = new QDoubleSpinBox(saplingGroup);
    saplingFeeSpin->setDecimals(8);
    saplingFeeSpin->setRange(0.0, 1000.0);
    saplingForm->addRow(tr("Transaction fee (ARRR):"), saplingFeeSpin);
    saplingAddressesEdit = new QLineEdit(saplingGroup);
    saplingAddressesEdit->setPlaceholderText(tr("Empty = all addresses; comma-separated to filter"));
    saplingForm->addRow(tr("Addresses:"), saplingAddressesEdit);
    saplingStatusLabel = new QLabel(saplingGroup);
    saplingForm->addRow(tr("Status:"), saplingStatusLabel);
    QPushButton *saplingApplyButton = new QPushButton(tr("Apply"), saplingGroup);
    saplingForm->addRow(saplingApplyButton);
    layout->addWidget(saplingGroup);
    connect(saplingApplyButton, SIGNAL(clicked()), this, SLOT(applySaplingConsolidation()));

    // --- Ironwood consolidation ---
    QGroupBox *ironwoodGroup = new QGroupBox(tr("Ironwood Consolidation"), scrollContent);
    QFormLayout *ironwoodForm = new QFormLayout(ironwoodGroup);
    ironwoodEnabledCheck = new QCheckBox(tr("Enabled"), ironwoodGroup);
    ironwoodForm->addRow(ironwoodEnabledCheck);
    ironwoodIntervalSpin = new QSpinBox(ironwoodGroup);
    ironwoodIntervalSpin->setRange(1, 1000000000);
    ironwoodForm->addRow(tr("Interval (blocks):"), ironwoodIntervalSpin);
    ironwoodTargetQtySpin = new QSpinBox(ironwoodGroup);
    ironwoodTargetQtySpin->setRange(1, 100000);
    ironwoodForm->addRow(tr("Target note quantity:"), ironwoodTargetQtySpin);
    ironwoodFeeSpin = new QDoubleSpinBox(ironwoodGroup);
    ironwoodFeeSpin->setDecimals(8);
    ironwoodFeeSpin->setRange(0.0, 1000.0);
    ironwoodForm->addRow(tr("Transaction fee (ARRR):"), ironwoodFeeSpin);
    ironwoodAddressesEdit = new QLineEdit(ironwoodGroup);
    ironwoodAddressesEdit->setPlaceholderText(tr("Empty = all addresses; comma-separated to filter"));
    ironwoodForm->addRow(tr("Addresses:"), ironwoodAddressesEdit);
    ironwoodStatusLabel = new QLabel(ironwoodGroup);
    ironwoodForm->addRow(tr("Status:"), ironwoodStatusLabel);
    QPushButton *ironwoodApplyButton = new QPushButton(tr("Apply"), ironwoodGroup);
    ironwoodForm->addRow(ironwoodApplyButton);
    layout->addWidget(ironwoodGroup);
    connect(ironwoodApplyButton, SIGNAL(clicked()), this, SLOT(applyIronwoodConsolidation()));

    // --- Sweep ---
    QGroupBox *sweepGroup = new QGroupBox(tr("Sweep"), scrollContent);
    QFormLayout *sweepForm = new QFormLayout(sweepGroup);
    sweepEnabledCheck = new QCheckBox(tr("Enabled"), sweepGroup);
    sweepForm->addRow(sweepEnabledCheck);
    sweepIntervalSpin = new QSpinBox(sweepGroup);
    sweepIntervalSpin->setRange(1, 1000000000);
    sweepForm->addRow(tr("Interval (blocks):"), sweepIntervalSpin);
    sweepFeeSpin = new QDoubleSpinBox(sweepGroup);
    sweepFeeSpin->setDecimals(8);
    sweepFeeSpin->setRange(0.0, 1000.0);
    sweepForm->addRow(tr("Transaction fee (ARRR):"), sweepFeeSpin);
    saplingSweepAddressEdit = new QLineEdit(sweepGroup);
    saplingSweepAddressEdit->setPlaceholderText(tr("Sapling destination address (empty = not configured)"));
    sweepForm->addRow(tr("Sapling destination:"), saplingSweepAddressEdit);
    ironwoodSweepAddressEdit = new QLineEdit(sweepGroup);
    ironwoodSweepAddressEdit->setPlaceholderText(tr("Ironwood destination address (empty = not configured)"));
    sweepForm->addRow(tr("Ironwood destination:"), ironwoodSweepAddressEdit);
    sweepStatusLabel = new QLabel(sweepGroup);
    sweepForm->addRow(tr("Status:"), sweepStatusLabel);
    QPushButton *sweepApplyButton = new QPushButton(tr("Apply"), sweepGroup);
    sweepForm->addRow(sweepApplyButton);
    layout->addWidget(sweepGroup);
    connect(sweepApplyButton, SIGNAL(clicked()), this, SLOT(applySweep()));

    // --- Fees / Behavior / Pruning ---
    QGroupBox *feesGroup = new QGroupBox(tr("Fees, Behavior && Pruning"), scrollContent);
    QFormLayout *feesForm = new QFormLayout(feesGroup);
    payTxFeeSpin = new QDoubleSpinBox(feesGroup);
    payTxFeeSpin->setDecimals(8);
    payTxFeeSpin->setRange(0.0, 1000.0);
    feesForm->addRow(tr("Pay transaction fee (ARRR/kB):"), payTxFeeSpin);
    minTxFeeSpin = new QDoubleSpinBox(feesGroup);
    minTxFeeSpin->setDecimals(8);
    minTxFeeSpin->setRange(0.0, 1000.0);
    feesForm->addRow(tr("Minimum transaction fee (ARRR/kB):"), minTxFeeSpin);
    txConfirmTargetSpin = new QSpinBox(feesGroup);
    txConfirmTargetSpin->setRange(1, 1000);
    feesForm->addRow(tr("Confirmation target (blocks):"), txConfirmTargetSpin);
    spendZeroConfChangeCheck = new QCheckBox(tr("Spend unconfirmed change"), feesGroup);
    feesForm->addRow(spendZeroConfChangeCheck);
    minTxValueSpin = new QDoubleSpinBox(feesGroup);
    minTxValueSpin->setDecimals(8);
    minTxValueSpin->setRange(0.0, 1000.0);
    feesForm->addRow(tr("Minimum note value (ARRR):"), minTxValueSpin);
    keypoolSizeSpin = new QSpinBox(feesGroup);
    keypoolSizeSpin->setRange(1, 1000000);
    feesForm->addRow(tr("Keypool size:"), keypoolSizeSpin);
    txDeleteEnabledCheck = new QCheckBox(tr("Delete old spent transactions"), feesGroup);
    feesForm->addRow(txDeleteEnabledCheck);
    txConflictDeleteEnabledCheck = new QCheckBox(tr("Delete conflicted transactions"), feesGroup);
    feesForm->addRow(txConflictDeleteEnabledCheck);
    deleteIntervalSpin = new QSpinBox(feesGroup);
    deleteIntervalSpin->setRange(1, 1000000000);
    feesForm->addRow(tr("Delete-check interval (blocks):"), deleteIntervalSpin);
    keepTxForNBlocksSpin = new QSpinBox(feesGroup);
    keepTxForNBlocksSpin->setRange(1, 1000000000);
    feesForm->addRow(tr("Keep transactions for at least (blocks):"), keepTxForNBlocksSpin);
    keepLastNTxSpin = new QSpinBox(feesGroup);
    keepLastNTxSpin->setRange(1, 1000000000);
    feesForm->addRow(tr("Always keep the last N transactions:"), keepLastNTxSpin);
    QPushButton *feesApplyButton = new QPushButton(tr("Apply"), feesGroup);
    feesForm->addRow(feesApplyButton);
    layout->addWidget(feesGroup);
    connect(feesApplyButton, SIGNAL(clicked()), this, SLOT(applyFeesBehaviorPruning()));

    layout->addStretch();
    scrollContent->setLayout(layout);
    scrollArea->setWidget(scrollContent);

    setEnabled(false); // enabled once setWalletModel() gives it a wallet to work with

    statusTimer = new QTimer(this);
    connect(statusTimer, SIGNAL(timeout()), this, SLOT(refreshStatus()));
    statusTimer->start(5000);
}

void WalletSettingsPage::setWalletModel(WalletModel *model)
{
    walletModel = model;
    setEnabled(model != 0);
    if (model) {
        loadFromModel();
        refreshStatus();
    }
}

void WalletSettingsPage::loadFromModel()
{
    if (!walletModel)
        return;

    saplingEnabledCheck->setChecked(walletModel->getSaplingConsolidationEnabled());
    saplingIntervalSpin->setValue(walletModel->getSaplingConsolidationInterval());
    saplingTargetQtySpin->setValue(walletModel->getSaplingConsolidationTargetQty());
    saplingFeeSpin->setValue(AmountToCoins(walletModel->getSaplingConsolidationTxFee()));
    saplingAddressesEdit->setText(walletModel->getSaplingConsolidationAddresses());

    ironwoodEnabledCheck->setChecked(walletModel->getIronwoodConsolidationEnabled());
    ironwoodIntervalSpin->setValue(walletModel->getIronwoodConsolidationInterval());
    ironwoodTargetQtySpin->setValue(walletModel->getIronwoodConsolidationTargetQty());
    ironwoodFeeSpin->setValue(AmountToCoins(walletModel->getIronwoodConsolidationTxFee()));
    ironwoodAddressesEdit->setText(walletModel->getIronwoodConsolidationAddresses());

    sweepEnabledCheck->setChecked(walletModel->getSweepEnabled());
    sweepIntervalSpin->setValue(walletModel->getSweepInterval());
    sweepFeeSpin->setValue(AmountToCoins(walletModel->getSweepTxFee()));
    saplingSweepAddressEdit->setText(walletModel->getSaplingSweepAddress());
    ironwoodSweepAddressEdit->setText(walletModel->getIronwoodSweepAddress());

    payTxFeeSpin->setValue(AmountToCoins(walletModel->getPayTxFee()));
    minTxFeeSpin->setValue(AmountToCoins(walletModel->getMinTxFee()));
    txConfirmTargetSpin->setValue((int)walletModel->getTxConfirmTarget());
    spendZeroConfChangeCheck->setChecked(walletModel->getSpendZeroConfChange());
    minTxValueSpin->setValue(AmountToCoins(walletModel->getMinTxValue()));
    keypoolSizeSpin->setValue((int)walletModel->getKeypoolSizeTarget());
    txDeleteEnabledCheck->setChecked(walletModel->getTxDeleteEnabled());
    txConflictDeleteEnabledCheck->setChecked(walletModel->getTxConflictDeleteEnabled());
    deleteIntervalSpin->setValue(walletModel->getDeleteInterval());
    keepTxForNBlocksSpin->setValue((int)walletModel->getKeepTransactionsAfterNBlocks());
    keepLastNTxSpin->setValue((int)walletModel->getKeepLastNTransactions());
}

void WalletSettingsPage::refreshStatus()
{
    if (!walletModel || !isEnabled())
        return;

    saplingStatusLabel->setText(walletModel->getSaplingConsolidationRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextSaplingConsolidation()));
    ironwoodStatusLabel->setText(walletModel->getIronwoodConsolidationRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextIronwoodConsolidation()));
    sweepStatusLabel->setText(walletModel->getSweepRunning()
        ? tr("Running now")
        : tr("Next run at block %1").arg(walletModel->getNextSweep()));
}

void WalletSettingsPage::applySaplingConsolidation()
{
    if (!walletModel)
        return;
    walletModel->setSaplingConsolidationEnabled(saplingEnabledCheck->isChecked());
    walletModel->setSaplingConsolidationInterval(saplingIntervalSpin->value());
    walletModel->setSaplingConsolidationTargetQty(saplingTargetQtySpin->value());
    walletModel->setSaplingConsolidationTxFee(CoinsToAmount(saplingFeeSpin->value()));
    if (!walletModel->setSaplingConsolidationAddresses(saplingAddressesEdit->text())) {
        QMessageBox::warning(this, tr("Sapling Consolidation"),
            tr("One or more addresses are invalid, not Sapling addresses, or this wallet does not "
               "have the spending key for them. The address filter was not changed."));
        saplingAddressesEdit->setText(walletModel->getSaplingConsolidationAddresses());
    }
    refreshStatus();
}

void WalletSettingsPage::applyIronwoodConsolidation()
{
    if (!walletModel)
        return;
    walletModel->setIronwoodConsolidationEnabled(ironwoodEnabledCheck->isChecked());
    walletModel->setIronwoodConsolidationInterval(ironwoodIntervalSpin->value());
    walletModel->setIronwoodConsolidationTargetQty(ironwoodTargetQtySpin->value());
    walletModel->setIronwoodConsolidationTxFee(CoinsToAmount(ironwoodFeeSpin->value()));
    if (!walletModel->setIronwoodConsolidationAddresses(ironwoodAddressesEdit->text())) {
        QMessageBox::warning(this, tr("Ironwood Consolidation"),
            tr("One or more addresses are invalid, not Ironwood addresses, or this wallet does not "
               "have the spending key for them. The address filter was not changed."));
        ironwoodAddressesEdit->setText(walletModel->getIronwoodConsolidationAddresses());
    }
    refreshStatus();
}

void WalletSettingsPage::applySweep()
{
    if (!walletModel)
        return;

    QString saplingAddr = saplingSweepAddressEdit->text().trimmed();
    QString ironwoodAddr = ironwoodSweepAddressEdit->text().trimmed();
    if (!saplingAddr.isEmpty() && !ironwoodAddr.isEmpty()) {
        QMessageBox::warning(this, tr("Sweep"),
            tr("Only one sweep destination can be active at a time -- setting one always clears the "
               "other. Clear one of the two address fields and Apply again."));
        return;
    }

    walletModel->setSweepEnabled(sweepEnabledCheck->isChecked());
    walletModel->setSweepInterval(sweepIntervalSpin->value());
    walletModel->setSweepTxFee(CoinsToAmount(sweepFeeSpin->value()));

    // Call exactly one of these, never both: each setter also clears the
    // other pool's slot, so a second call would immediately wipe out
    // whatever the first one just set.
    bool ok = !ironwoodAddr.isEmpty()
        ? walletModel->setIronwoodSweepAddress(ironwoodAddr)
        : walletModel->setSaplingSweepAddress(saplingAddr); // empty clears both when saplingAddr is also empty

    if (!ok) {
        QMessageBox::warning(this, tr("Sweep"),
            tr("Invalid sweep destination address, or this wallet does not have the spending key "
               "for it. The sweep destination was not changed."));
    }
    saplingSweepAddressEdit->setText(walletModel->getSaplingSweepAddress());
    ironwoodSweepAddressEdit->setText(walletModel->getIronwoodSweepAddress());
    refreshStatus();
}

void WalletSettingsPage::applyFeesBehaviorPruning()
{
    if (!walletModel)
        return;
    walletModel->setPayTxFee(CoinsToAmount(payTxFeeSpin->value()));
    walletModel->setMinTxFee(CoinsToAmount(minTxFeeSpin->value()));
    walletModel->setTxConfirmTarget((unsigned int)txConfirmTargetSpin->value());
    walletModel->setSpendZeroConfChange(spendZeroConfChangeCheck->isChecked());
    walletModel->setMinTxValue(CoinsToAmount(minTxValueSpin->value()));
    walletModel->setKeypoolSizeTarget(keypoolSizeSpin->value());
    walletModel->setTxDeleteEnabled(txDeleteEnabledCheck->isChecked());
    walletModel->setTxConflictDeleteEnabled(txConflictDeleteEnabledCheck->isChecked());
    walletModel->setDeleteInterval(deleteIntervalSpin->value());
    walletModel->setKeepTransactionsAfterNBlocks((unsigned int)keepTxForNBlocksSpin->value());
    walletModel->setKeepLastNTransactions((unsigned int)keepLastNTxSpin->value());
}
