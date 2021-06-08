// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_NEWSEED_H
#define KOMODO_QT_NEWSEED_H

#include <QWidget>

class NetworkStyle;

namespace Ui {
    class NewSeedForm;
}

/** Class for creating or resorting a wallet from seed phrase.
 */
class NewSeed : public QWidget
{
    Q_OBJECT

public:
    explicit NewSeed(const NetworkStyle *networkStyle);
    ~NewSeed();

    Ui::NewSeedForm *ui;
private:

};

#endif // KOMODO_QT_NEWSEED_H
