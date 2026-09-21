// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_WALLETSDIALOG_H
#define KOMODO_QT_WALLETSDIALOG_H

#include <QDialog>
#include <QList>
#include <QString>

class PlatformStyle;

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
QT_END_NAMESPACE

/**
 * Modal wallet manager, opened from the Overview page's wallet dropdown
 * ("Manage wallets...") and from File > Manage Wallets. Replaces the old
 * File > Wallets submenu: lists every wallet open in this window as a card
 * (the current one badged ACTIVE, with a "..." menu to close it) and offers
 * New/Load wallet. It owns no wallet state -- it only reports what the user
 * asked for via signals, and PirateOceanGUI does the actual work.
 */
class WalletsDialog : public QDialog
{
    Q_OBJECT

public:
    struct WalletEntry {
        QString name;    // GUI key -- also the wallet's real CWalletManager name
        bool encrypted;
    };

    explicit WalletsDialog(const PlatformStyle *platformStyle, QWidget *parent = nullptr);

    /** Rebuild the card list; `current` is the wallet the window is showing. */
    void setWallets(const QList<WalletEntry> &wallets, const QString &current);

Q_SIGNALS:
    void switchRequested(const QString &name);
    void closeWalletRequested(const QString &name);
    void newWalletRequested();
    void loadWalletRequested();

private:
    const PlatformStyle *platformStyle;
    QVBoxLayout *cardsLayout;
    QLabel *emptyLabel;
};

#endif // KOMODO_QT_WALLETSDIALOG_H
