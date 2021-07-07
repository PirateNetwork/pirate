// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zaddresstablemodel.h"
#include "optionsmodel.h"

#include "komodo_defs.h"
#include "komodounits.h"

#include "guiutil.h"
#include "guiconstants.h"
#include "walletmodel.h"
#include "platformstyle.h"

#include "base58.h"
#include "wallet/wallet.h"

#include "rpc/server.h"

#include <QFont>
#include <QDebug>
#include <QTimer>

const QString ZAddressTableModel::Send = "S";
const QString ZAddressTableModel::Receive = "R";

static int column_alignments[] = {
        Qt::AlignCenter|Qt::AlignVCenter, /* mine */
        Qt::AlignRight|Qt::AlignVCenter,   /* balance */
        Qt::AlignLeft|Qt::AlignVCenter,   /* label */
        Qt::AlignLeft|Qt::AlignVCenter    /* address */
    };

struct ZAddressTableEntry
{
    enum Type {
        Sending,
        Receiving,
        Hidden /* QSortFilterProxyModel will filter these out */
    };

    Type          type;
    QString       label;
    QString       address;
    CAmount       balance;
    bool          mine;

    ZAddressTableEntry() {}
};

struct ZAddressTableEntryLessThan
{
    bool operator()(const ZAddressTableEntry &a, const ZAddressTableEntry &b) const
    {
        return a.address < b.address;
    }
    bool operator()(const ZAddressTableEntry &a, const QString &b) const
    {
        return a.address < b;
    }
    bool operator()(const QString &a, const ZAddressTableEntry &b) const
    {
        return a < b.address;
    }
};

/* Determine address type from address purpose */
static ZAddressTableEntry::Type translateTransactionType(const QString &strPurpose, bool isMine)
{
    ZAddressTableEntry::Type addressType = ZAddressTableEntry::Hidden;
    // "refund" addresses aren't shown, and change addresses aren't in mapAddressBook at all.
    if (strPurpose == "send")
        addressType = ZAddressTableEntry::Sending;
    else if (strPurpose == "receive")
        addressType = ZAddressTableEntry::Receiving;
    else if (strPurpose == "unknown" || strPurpose == "") // if purpose not set, guess
        // addressType = (isMine ? ZAddressTableEntry::Receiving : ZAddressTableEntry::Sending);
        addressType = ZAddressTableEntry::Receiving;
    return addressType;
}

// Private implementation
class ZAddressTablePriv
{
public:
    CWallet *wallet;
    QList<ZAddressTableEntry> cachedAddressTable;
    ZAddressTableModel *parent;

    ZAddressTablePriv(CWallet *_wallet, ZAddressTableModel *_parent):
        wallet(_wallet),
        parent(_parent)
    {
    }

    void updateBalances(bool &fForceCheckBalanceChanged, int &cachedNumBlocks)
    {
        // Get required locks upfront. This avoids the GUI from getting stuck on
        // periodical polls if the core is holding the locks for a longer time -
        // for example, during a wallet rescan.
        TRY_LOCK(cs_main, lockMain);
        if(!lockMain)
            return;
        TRY_LOCK(wallet->cs_wallet, lockWallet);
        if(!lockWallet)
            return;

        //Don't run balance if nothing has changed
        if (!fForceCheckBalanceChanged && cachedNumBlocks == chainActive.Height()) {
            return;
        }

        fForceCheckBalanceChanged = false;
        cachedNumBlocks = chainActive.Height();

        std::map<QString, CAmount> stringBalances;
        std::map<libzcash::PaymentAddress, CAmount> balances;
        wallet->getZAddressBalances(balances, 0, false);


        std::set<libzcash::SaplingPaymentAddress> addresses;
        wallet->GetSaplingPaymentAddresses(addresses);
        for (auto addr : addresses) {
            if (balances.count(addr) == 0)
                balances[addr] = 0;
        }

        for(std::map<libzcash::PaymentAddress, CAmount>::iterator it = balances.begin(); it != balances.end(); it++) {
            stringBalances[QString::fromStdString(EncodePaymentAddress(it->first))] = it->second;
        }

          for (std::map<QString, CAmount>::iterator bi = stringBalances.begin(); bi != stringBalances.end(); bi++) {
              QString address = bi->first;
              QList<ZAddressTableEntry>::iterator lower = qLowerBound(
                  cachedAddressTable.begin(), cachedAddressTable.end(), address, ZAddressTableEntryLessThan());
              QList<ZAddressTableEntry>::iterator upper = qUpperBound(
                  cachedAddressTable.begin(), cachedAddressTable.end(), address, ZAddressTableEntryLessThan());
              int lowerIndex = (lower - cachedAddressTable.begin());
              int upperIndex = (upper - cachedAddressTable.begin());
              bool inModel = (lower != upper);

              if (inModel) {
                  if (bi != stringBalances.end()) {
                      CAmount balance = CAmount(bi->second);
                      lower->balance = balance;
                      parent->emitDataChanged(lowerIndex);
                  } else {
                      lower->balance = CAmount(0);
                      parent->emitDataChanged(lowerIndex);
                  }
              }
          }
    }

    void refreshAddressTable()
    {
        cachedAddressTable.clear();
        {
            LOCK2(cs_main, wallet->cs_wallet);

            std::map<libzcash::PaymentAddress, CAmount> balances;
            wallet->getZAddressBalances(balances, 1, false);


            for (const std::pair<libzcash::PaymentAddress, CAddressBookData>& item : wallet->mapZAddressBook)
            {
                const libzcash::PaymentAddress& zaddr = item.first;

                bool mine = false;
                auto saplingAddr = boost::get<libzcash::SaplingPaymentAddress>(&zaddr);

                if (saplingAddr != nullptr) {
                    libzcash::SaplingIncomingViewingKey ivk;
                    libzcash::SaplingExtendedFullViewingKey extfvk;
                    if (wallet->GetSaplingIncomingViewingKey(*saplingAddr, ivk) &&
                        wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
                        wallet->HaveSaplingSpendingKey(extfvk)) {
                            mine = true;
                    }

                  CAmount balance = 0;
                  std::map<libzcash::PaymentAddress, CAmount>::iterator it = balances.find(zaddr);
                  if (it != balances.end()) {
                      balance = it->second;
                  }

                  ZAddressTableEntry::Type addressType = translateTransactionType(
                          QString::fromStdString(item.second.purpose), mine);
                  const std::string& strName = item.second.name;

                  ZAddressTableEntry entry;
                  entry.type = addressType;
                  entry.label = QString::fromStdString(strName);
                  entry.address = QString::fromStdString(EncodePaymentAddress(zaddr));
                  entry.balance = balance;
                  entry.mine = mine;
                  cachedAddressTable.append(entry);

                }
            }
        }
        // qLowerBound() and qUpperBound() require our cachedAddressTable list to be sorted in asc order
        // Even though the map is already sorted this re-sorting step is needed because the originating map
        // is sorted by binary address, not by base58() address.
        qSort(cachedAddressTable.begin(), cachedAddressTable.end(), ZAddressTableEntryLessThan());
    }

    void updateEntry(const QString &address, const QString &label, bool isMine, const QString &purpose, int status)
    {
        // Find address / label in model
        QList<ZAddressTableEntry>::iterator lower = qLowerBound(
            cachedAddressTable.begin(), cachedAddressTable.end(), address, ZAddressTableEntryLessThan());
        QList<ZAddressTableEntry>::iterator upper = qUpperBound(
            cachedAddressTable.begin(), cachedAddressTable.end(), address, ZAddressTableEntryLessThan());
        int lowerIndex = (lower - cachedAddressTable.begin());
        int upperIndex = (upper - cachedAddressTable.begin());
        bool inModel = (lower != upper);
        ZAddressTableEntry::Type newEntryType = translateTransactionType(purpose, isMine);

        bool mine = false;
        ZAddressTableEntry newEntry;
        libzcash::PaymentAddress zaddr = DecodePaymentAddress(address.toStdString());
        auto saplingAddr = boost::get<libzcash::SaplingPaymentAddress>(&zaddr);
        if (saplingAddr != nullptr) {
            libzcash::SaplingIncomingViewingKey ivk;
            libzcash::SaplingExtendedFullViewingKey extfvk;
            if (wallet->GetSaplingIncomingViewingKey(*saplingAddr, ivk) &&
                wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
                wallet->HaveSaplingSpendingKey(extfvk)) {
                    mine = true;
            }

            switch(status)
            {
            case CT_NEW:
                if(inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_NEW, but entry is already in model";
                    break;
                }
                parent->beginInsertRows(QModelIndex(), lowerIndex, lowerIndex);

                newEntry.type = newEntryType;
                newEntry.label = label;
                newEntry.address = address;
                newEntry.balance = 0;
                newEntry.mine = mine;
                cachedAddressTable.insert(lowerIndex, newEntry);
                parent->endInsertRows();
                break;
            case CT_UPDATED:
                if(!inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_UPDATED, but entry is not in model";
                    break;
                }
                lower->type = newEntryType;
                lower->label = label;
                parent->emitDataChanged(lowerIndex);
                break;
            case CT_DELETED:
                if(!inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_DELETED, but entry is not in model";
                    break;
                }
                parent->beginRemoveRows(QModelIndex(), lowerIndex, upperIndex-1);
                cachedAddressTable.erase(lower, upper);
                parent->endRemoveRows();
                break;
            }
        }
    }

    int size()
    {
        return cachedAddressTable.size();
    }

    ZAddressTableEntry *index(int idx)
    {
        if(idx >= 0 && idx < cachedAddressTable.size())
        {
            return &cachedAddressTable[idx];
        }
        else
        {
            return 0;
        }
    }
};

ZAddressTableModel::ZAddressTableModel(const PlatformStyle *_platformStyle, CWallet *_wallet, WalletModel *parent) :
    QAbstractTableModel(parent),
    walletModel(parent),
    wallet(_wallet),
    priv(new ZAddressTablePriv(wallet, this)),
    platformStyle(_platformStyle)
{
    columns << tr("Mine") << tr("Balance") << tr("Address") << tr("Type");
    priv->refreshAddressTable();

    // This timer will be fired repeatedly to update the balance
    pollTimer = new QTimer(this);
    connect(pollTimer, SIGNAL(timeout()), this, SLOT(runUpdate()));
    pollTimer->start(MODEL_UPDATE_DELAY);
    fForceCheckBalanceChanged = false;
    cachedNumBlocks = 0;

    subscribeToCoreSignals();
}

ZAddressTableModel::~ZAddressTableModel()
{
    unsubscribeFromCoreSignals();
    delete priv;
}

void ZAddressTableModel::updateBalances()
{
    fForceCheckBalanceChanged = true;
    runUpdate();
}

void ZAddressTableModel::runUpdate()
{
    priv->updateBalances(fForceCheckBalanceChanged, cachedNumBlocks);
}

int ZAddressTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return priv->size();
}

int ZAddressTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return columns.length();
}

QVariant ZAddressTableModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid())
        return QVariant();

    ZAddressTableEntry *rec = static_cast<ZAddressTableEntry*>(index.internalPointer());
    if (role == Qt::DecorationRole)
    {
        switch(index.column())
        {
        case isMine:
            if (rec->mine) {
                return platformStyle->TextColorIcon(QIcon(":/icons/synced"));
            } else {
                return platformStyle->TextColorIcon(qvariant_cast<QIcon>(QVariant()));
            }
        }
    }
    else if ((role == Qt::EditRole) && (index.column() == isMine))
    {
        switch(index.column())
        {
        case isMine:
            return (rec->mine ? 1 : 0);
        }
    }
    else if(role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch(index.column())
        {
            case isMine:
                if (rec->mine) {
                    return platformStyle->TextColorIcon(QIcon(":/icons/synced"));
                } else {
                    return platformStyle->TextColorIcon(qvariant_cast<QIcon>(QVariant()));
                }
            case Label:
                if(rec->label.isEmpty() && role == Qt::DisplayRole)
                {
                    return tr("(no label)");
                }
                else
                {
                    return rec->label;
                }
            case Address:
                return rec->address;
            case Balance:
                {
                    return KomodoUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->balance, false, KomodoUnits::separatorStandard);
                }
        }
    }
    else if (role == Qt::FontRole)
    {
        QFont font;
        if(index.column() == Address)
        {
            font = GUIUtil::fixedPitchFont();
        }
        return font;
    }
    else if (role == Qt::TextAlignmentRole)
    {
        return column_alignments[index.column()];
    }
    else if (role == TypeRole)
    {
        switch(rec->type)
        {
        case ZAddressTableEntry::Sending:
            return Send;
        case ZAddressTableEntry::Receiving:
            return Receive;
        default: break;
        }
    }
    return QVariant();
}

bool ZAddressTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if(!index.isValid())
        return false;
    ZAddressTableEntry *rec = static_cast<ZAddressTableEntry*>(index.internalPointer());
    std::string strPurpose = (rec->type == ZAddressTableEntry::Sending ? "send" : "receive");
    editStatus = OK;

    if(role == Qt::EditRole)
    {
        LOCK(wallet->cs_wallet); /* For SetZAddressBook / DelZAddressBook */
        libzcash::PaymentAddress curAddress = DecodePaymentAddress(rec->address.toStdString());
        if(index.column() == Label)
        {
            // Do nothing, if old label == new label
            if(rec->label == value.toString())
            {
                editStatus = NO_CHANGES;
                return false;
            }
            wallet->SetZAddressBook(curAddress, value.toString().toStdString(), strPurpose);
        } else if(index.column() == Address) {
            libzcash::PaymentAddress newAddress = DecodePaymentAddress(value.toString().toStdString());
            // Refuse to set invalid address, set error status and return false
//!!!!! check validity
//            if(boost::get<CNoDestination>(&newAddress))
//            {
//                editStatus = INVALID_ADDRESS;
//                return false;
//            }
            // Do nothing, if old address == new address
//            else
            if(newAddress == curAddress)
            {
                editStatus = NO_CHANGES;
                return false;
            }
            // Check for duplicate addresses to prevent accidental deletion of addresses, if you try
            // to paste an existing address over another address (with a different label)
            else if(wallet->mapZAddressBook.count(newAddress))
            {
                editStatus = DUPLICATE_ADDRESS;
                return false;
            }
            // Double-check that we're not overwriting a receiving address
            else if(rec->type == ZAddressTableEntry::Sending)
            {
                // Remove old entry
                wallet->DelZAddressBook(curAddress);
                // Add new entry with new address
                wallet->SetZAddressBook(newAddress, rec->label.toStdString(), strPurpose);
            }
        }
        return true;
    }
    return false;
}

QVariant ZAddressTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole && section < columns.size())
        {
            return columns[section];
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        }
    }
    return QVariant();
}

Qt::ItemFlags ZAddressTableModel::flags(const QModelIndex &index) const
{  
    if(!index.isValid())
        return 0;
    ZAddressTableEntry *rec = static_cast<ZAddressTableEntry*>(index.internalPointer());

    Qt::ItemFlags retval = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    // Can edit address and label for sending addresses,
    // and only label for receiving addresses.
//    if(rec->type == ZAddressTableEntry::Sending ||
//      (rec->type == ZAddressTableEntry::Receiving && index.column()==Label))
//    {
//        retval |= Qt::ItemIsEditable;
//    }
    return retval;
}

QModelIndex ZAddressTableModel::index(int row, int column, const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    ZAddressTableEntry *data = priv->index(row);
    if(data)
    {
        return createIndex(row, column, priv->index(row));
    }
    else
    {
        return QModelIndex();
    }
}

void ZAddressTableModel::updateEntry(const QString &address,
        const QString &label, bool isMine, const QString &purpose, int status)
{
    // Update address book model from Pirate core
    priv->updateEntry(address, label, isMine, purpose, status);
}

QString ZAddressTableModel::addRow(const QString &type, const QString &label, const QString &address)
{

    std::string strLabel; // = label.toStdString();
    std::string strAddress = address.toStdString();

    editStatus = OK;

    if(type == Send)
    {
//!!!!! validate
//        if(!walletModel->validateAddress(address))
//        {
//            editStatus = INVALID_ADDRESS;
//            return QString();
//        }
        // Check for duplicate addresses
        {
            LOCK(wallet->cs_wallet);
            if(wallet->mapZAddressBook.count(DecodePaymentAddress(strAddress)))
            {
                editStatus = DUPLICATE_ADDRESS;
                return QString();
            }
        }
    }
    else if(type == Receive)
    {
        // Generate a new address to associate with given label
        if ( GetTime() < KOMODO_SAPLING_ACTIVATION )
        {
            strAddress = EncodePaymentAddress(wallet->GenerateNewSproutZKey());
            strLabel = "z-sprout";
        }
        else
        {
            strAddress = EncodePaymentAddress(wallet->GenerateNewSaplingZKey());
            strLabel = "z-sapling";
        }
    }
    else
    {
        return QString();
    }

    // Add entry
    {
        LOCK(wallet->cs_wallet);
        wallet->SetZAddressBook(DecodePaymentAddress(strAddress), strLabel,
                               (type == Send ? "send" : "receive"));
    }
    return QString::fromStdString(strAddress);
}

bool ZAddressTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    Q_UNUSED(parent);
    ZAddressTableEntry *rec = priv->index(row);
    if(count != 1 || !rec || rec->type == ZAddressTableEntry::Receiving)
    {
        // Can only remove one row at a time, and cannot remove rows not in model.
        // Also refuse to remove receiving addresses.
        return false;
    }
    {
        LOCK(wallet->cs_wallet);
        wallet->DelZAddressBook(DecodePaymentAddress(rec->address.toStdString()));
    }
    return true;
}

/* Look up label for address in address book, if not found return empty string.
 */
QString ZAddressTableModel::labelForAddress(const QString &address) const
{
    {
        LOCK(wallet->cs_wallet);
        libzcash::PaymentAddress destination = DecodePaymentAddress(address.toStdString());
        std::map<libzcash::PaymentAddress, CAddressBookData>::iterator mi = wallet->mapZAddressBook.find(destination);
        if (mi != wallet->mapZAddressBook.end())
        {
            return QString::fromStdString(mi->second.name);
        }
    }
    return QString();
}

int ZAddressTableModel::lookupAddress(const QString &address) const
{
    QModelIndexList lst = match(index(0, Address, QModelIndex()),
                                Qt::EditRole, address, 1, Qt::MatchExactly);
    if(lst.isEmpty())
    {
        return -1;
    }
    else
    {
        return lst.at(0).row();
    }
}

static void NotifyBalanceChanged(ZAddressTableModel *zAddressModel)
{
    QMetaObject::invokeMethod(zAddressModel, "updateBalances", Qt::QueuedConnection);
}


void ZAddressTableModel::subscribeToCoreSignals()
{
    // Connect signals to wallet
    wallet->NotifyBalanceChanged.connect(boost::bind(NotifyBalanceChanged, this));

}

void ZAddressTableModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    wallet->NotifyBalanceChanged.disconnect(boost::bind(NotifyBalanceChanged, this));
}

void ZAddressTableModel::emitDataChanged(int idx)
{
    Q_EMIT dataChanged(index(idx, 0, QModelIndex()), index(idx, columns.length()-1, QModelIndex()));
}
