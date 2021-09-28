// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_OPENWALLET_H
#define KOMODO_QT_OPENWALLET_H

#include <QWidget>

class NetworkStyle;

namespace Ui {
    class OpenWalletForm;
}

/** Class for creating or resorting a wallet from seed phrase.
 */
class OpenWallet : public QWidget
{
    Q_OBJECT

public:
    explicit OpenWallet(const NetworkStyle *networkStyle);
    ~OpenWallet();

    Ui::OpenWalletForm *ui;

};

#endif // KOMODO_QT_NEWW_H
