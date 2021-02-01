// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_IMPORTKEYDIALOG_H
#define KOMODO_QT_IMPORTKEYDIALOG_H

#include <QDialog>


namespace Ui {
    class OpenSKDialog;
}

class OpenSKDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenSKDialog(QWidget *parent);
    ~OpenSKDialog();

    QString getSK();
    QString privateKey;

protected Q_SLOTS:
    void accept();

private:
    Ui::OpenSKDialog *ui;

};



namespace Ui {
    class OpenVKDialog;
}

class OpenVKDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OpenVKDialog(QWidget *parent);
    ~OpenVKDialog();

    QString getSK();
    QString privateKey;

protected Q_SLOTS:
    void accept();

private:
    Ui::OpenVKDialog *ui;

};



#endif
// KOMODO_QT_IMPORTKEYDIALOG_H
