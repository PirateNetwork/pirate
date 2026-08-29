// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_WALLETSETTINGSPAGE_H
#define KOMODO_QT_WALLETSETTINGSPAGE_H

#include <QWidget>

class WalletModel;

QT_BEGIN_NAMESPACE
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QTimer;
QT_END_NAMESPACE

/**
 * Per-wallet settings page (Phase 6): consolidation, sweep, and fee/behavior/
 * pruning configuration for whichever wallet is currently active. One
 * instance lives inside each WalletView, so it is naturally already scoped
 * to the right wallet -- no wallet picker needed here. Every field binds
 * directly to a WalletModel getter/setter pair (no RPC round-trip), the same
 * pattern WalletModel::getDefaultConfirmTarget() already established.
 */
class WalletSettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit WalletSettingsPage(QWidget *parent = 0);

    void setWalletModel(WalletModel *model);

private Q_SLOTS:
    void applySaplingConsolidation();
    void applyIronwoodConsolidation();
    void applySweep();
    void applyFeesBehaviorPruning();
    /** Re-reads the read-only status fields (next-run height, is-running) on a timer. */
    void refreshStatus();

private:
    WalletModel *walletModel;

    // Sapling consolidation
    QCheckBox *saplingEnabledCheck;
    QSpinBox *saplingIntervalSpin;
    QSpinBox *saplingTargetQtySpin;
    QDoubleSpinBox *saplingFeeSpin;
    QLineEdit *saplingAddressesEdit;
    QLabel *saplingStatusLabel;

    // Ironwood consolidation
    QCheckBox *ironwoodEnabledCheck;
    QSpinBox *ironwoodIntervalSpin;
    QSpinBox *ironwoodTargetQtySpin;
    QDoubleSpinBox *ironwoodFeeSpin;
    QLineEdit *ironwoodAddressesEdit;
    QLabel *ironwoodStatusLabel;

    // Sweep (protocol-agnostic enable/interval/fee; one destination address per pool)
    QCheckBox *sweepEnabledCheck;
    QSpinBox *sweepIntervalSpin;
    QDoubleSpinBox *sweepFeeSpin;
    QLineEdit *saplingSweepAddressEdit;
    QLineEdit *ironwoodSweepAddressEdit;
    QLabel *sweepStatusLabel;

    // Fees / behavior / pruning
    QDoubleSpinBox *payTxFeeSpin;
    QDoubleSpinBox *minTxFeeSpin;
    QSpinBox *txConfirmTargetSpin;
    QCheckBox *spendZeroConfChangeCheck;
    QDoubleSpinBox *minTxValueSpin;
    QSpinBox *keypoolSizeSpin;
    QCheckBox *txDeleteEnabledCheck;
    QCheckBox *txConflictDeleteEnabledCheck;
    QSpinBox *deleteIntervalSpin;
    QSpinBox *keepTxForNBlocksSpin;
    QSpinBox *keepLastNTxSpin;

    QTimer *statusTimer;

    void loadFromModel();
};

#endif // KOMODO_QT_WALLETSETTINGSPAGE_H
