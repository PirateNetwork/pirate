// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_RESTORESEED_H
#define KOMODO_QT_RESTORESEED_H

#include <QWidget>

class NetworkStyle;

namespace Ui {
    class RestoreSeedForm;
}

/** Class for creating or resorting a wallet from seed phrase.
 */
class RestoreSeed : public QWidget
{
    Q_OBJECT

public:
    explicit RestoreSeed(const NetworkStyle *networkStyle);
    ~RestoreSeed();

    Ui::RestoreSeedForm *ui;
private:

};

#endif // KOMODO_QT_RESTORESEED_H
