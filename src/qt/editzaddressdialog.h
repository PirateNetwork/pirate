// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_EDITZADDRESSDIALOG_H
#define KOMODO_QT_EDITZADDRESSDIALOG_H

#include <QDialog>

class ZAddressTableModel;

namespace Ui {
    class EditZAddressDialog;
}

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

/** Dialog for editing an address and associated information.
 */
class EditZAddressDialog : public QDialog
{
    Q_OBJECT

public:
    enum Mode {
        NewReceivingAddress,
        NewSendingAddress,
        EditReceivingAddress,
        EditSendingAddress
    };

    explicit EditZAddressDialog(Mode mode, QWidget *parent);
    ~EditZAddressDialog();

    void setModel(ZAddressTableModel *model);
    void loadRow(int row);

    QString getAddress() const;
    void setAddress(const QString &address);

public Q_SLOTS:
    void accept();

private:
    bool saveCurrentRow();

    Ui::EditZAddressDialog *ui;
    QDataWidgetMapper *mapper;
    Mode mode;
    ZAddressTableModel *model;

    QString address;
};

#endif // KOMODO_QT_EDITZADDRESSDIALOG_H
