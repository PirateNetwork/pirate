// Copyright (c) 2011-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_ZADDRESSTABLEMODEL_H
#define KOMODO_QT_ZADDRESSTABLEMODEL_H

#include <QAbstractTableModel>
#include <QStringList>

class ZAddressTablePriv;
class WalletModel;
class PlatformStyle;
class ZSendConfirmationDialog;

class CWallet;

/**
   Qt model of the address book in the core. This allows views to access and modify the address book.
 */
class ZAddressTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ZAddressTableModel(const PlatformStyle *platformStyle, CWallet *wallet, WalletModel *parent = 0);
    ~ZAddressTableModel();

    enum ColumnIndex {
        isMine = 0,
        Balance = 1,
        Label = 2,   /**< User specified label */
        Address = 3  /**< Komodo z-address */
    };

    enum RoleIndex {
        TypeRole = Qt::UserRole /**< Type of address (#Send or #Receive) */
    };

    /** Return status of edit/insert operation */
    enum EditStatus {
        OK,                     /**< Everything ok */
        NO_CHANGES,             /**< No changes were made during edit operation */
        INVALID_ADDRESS,        /**< Unparseable address */
        DUPLICATE_ADDRESS,      /**< Address already in address book */
        WALLET_UNLOCK_FAILURE,  /**< Wallet could not be unlocked to create new receiving address */
        KEY_GENERATION_FAILURE  /**< Generating a new public key for a receiving address failed */
    };

    static const QString Send;      /**< Specifies send address */
    static const QString Receive;   /**< Specifies receive address */

    /** @name Methods overridden from QAbstractTableModel
        @{*/
    int rowCount(const QModelIndex &parent) const;
    int columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    bool setData(const QModelIndex &index, const QVariant &value, int role);
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent) const;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex());
    Qt::ItemFlags flags(const QModelIndex &index) const;
    /*@}*/

    /* Add an address to the model.
       Returns the added address on success, and an empty string otherwise.
     */
    QString addRow(const QString &type, const QString &label, const QString &address);

    /* Look up label for address in address book, if not found return empty string.
     */
    QString labelForAddress(const QString &address) const;

    /* Look up row index of an address in the model.
       Return -1 if not found.
     */
    int lookupAddress(const QString &address) const;

    EditStatus getEditStatus() const { return editStatus; }

private:
    WalletModel *walletModel;
    CWallet *wallet;
    ZAddressTablePriv *priv;
    QStringList columns;
    EditStatus editStatus;
    const PlatformStyle *platformStyle;

    /** Notify listeners that data changed. */
    void emitDataChanged(int index);

public Q_SLOTS:
    /* Update address list from core.
     */
    void updateEntry(const QString &address, const QString &label, bool isMine, const QString &purpose, int status);

    friend class ZAddressTablePriv;
};

#endif // KOMODO_QT_ZADDRESSTABLEMODEL_H
