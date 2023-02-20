// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_UNLOCKTIMERDIALOG_H
#define KOMODO_QT_UNLOCKTIMERDIALOG_H

#include <QDialog>
#include <QTimer>

namespace Ui {
    class UnlockTimerDialog;
}

class UnlockTimerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UnlockTimerDialog(QWidget *parent);
    ~UnlockTimerDialog();

protected Q_SLOTS:
    void accept();


private:
    Ui::UnlockTimerDialog *ui;
    QTimer *pollTimer;
    int64_t relockTime = 0;

private Q_SLOTS:
    void setLockMessage();
};


#endif
// KOMODO_QT_UNLOCKTIMERDIALOG_H
