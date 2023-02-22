// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_UPDATEDIALOG_H
#define KOMODO_QT_UPDATEDIALOG_H

#include <QDialog>
#include <QVersionNumber>

namespace Ui {
    class UpdateDialog;
}

class UpdateDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UpdateDialog(QWidget *parent, QVersionNumber clientVersion, QVersionNumber gitVersion);
    ~UpdateDialog();

    QVersionNumber clientVersion;
    QVersionNumber gitVersion;

protected Q_SLOTS:
    void accept();

private:
    void setMessage();
    Ui::UpdateDialog *ui;
};

#endif
// KOMODO_QT_UPDATEDIALOG_H
