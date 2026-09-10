// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_WALLETOPTIONSPAGE_H
#define KOMODO_QT_WALLETOPTIONSPAGE_H

#include <QWidget>

class WalletModel;

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace Ui {
class WalletOptionsPage;
}

/**
 * Wallet-scoped settings window: consolidation, sweep, and fee/behavior/pruning
 * configuration for whichever wallet is currently displayed in the main window.
 * A single, non-modal, top-level instance lives on PirateOceanGUI (shown/raised
 * like RPCConsole's debug window, never exec()'d) and is retargeted via
 * setWalletModel() every time the current wallet changes -- it is not
 * per-wallet-tab the way the page it replaced was. Every field binds directly
 * to a WalletModel getter/setter pair (no RPC round-trip), the same pattern
 * WalletModel::getDefaultConfirmTarget() already established.
 */
class WalletOptionsPage : public QWidget
{
    Q_OBJECT

public:
    explicit WalletOptionsPage(QWidget *parent = 0);
    ~WalletOptionsPage();

    void setWalletModel(WalletModel *model);

private Q_SLOTS:
    void applySaplingConsolidation();
    void applyIronwoodConsolidation();
    void applySweep();
    void applyFeesBehaviorPruning();
    /** Re-reads the read-only status fields (next-run height, is-running) on a timer. */
    void refreshStatus();

private:
    Ui::WalletOptionsPage *ui;
    WalletModel *walletModel;

    QTimer *statusTimer;

    void loadFromModel();
};

#endif // KOMODO_QT_WALLETOPTIONSPAGE_H
