// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 Pirate Chain developers
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
#include "keystore.h"

#include "rpc/server.h"
#include "chainparams.h"
#include "consensus/upgrades.h"
#include "main.h"

#include <QFont>
#include <QDebug>
#include <QHash>
#include <QTimer>

const QString ZAddressTableModel::Send = "S";
const QString ZAddressTableModel::Receive = "R";

static int column_alignments[] = {
        Qt::AlignCenter|Qt::AlignVCenter, /* mine */
        Qt::AlignRight|Qt::AlignVCenter,   /* balance */
        Qt::AlignCenter|Qt::AlignVCenter, /* scope */
        Qt::AlignLeft|Qt::AlignVCenter,   /* address */
        Qt::AlignLeft|Qt::AlignVCenter    /* label */
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
    QString       scope;
    
    // Grouping support
    bool          isGroup;        // True if this is a group header row
    QString       groupKey;       // Spending key fingerprint for grouping
    int           childCount;     // Number of addresses under this group
    bool          isExpanded;     // Whether group is expanded
    int           level;          // 0 for groups, 1 for children

    ZAddressTableEntry() : isGroup(false), childCount(0), isExpanded(true), level(0) {}
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

/* Compare strings with embedded runs of ASCII digits ordered by numeric value,
   so "Key Group 2" sorts before "Key Group 10". Strings that compare equal this
   way (differing only in leading zeros) fall back to plain comparison so the
   result is still a strict weak ordering. */
static bool naturalLess(const QString &a, const QString &b)
{
    auto isDigit = [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); };
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            int si = i, sj = j;
            while (i < a.size() && isDigit(a[i])) ++i;
            while (j < b.size() && isDigit(b[j])) ++j;
            while (si < i - 1 && a[si] == QLatin1Char('0')) ++si;
            while (sj < j - 1 && b[sj] == QLatin1Char('0')) ++sj;
            int lenA = i - si, lenB = j - sj;
            if (lenA != lenB)
                return lenA < lenB;
            int c = QStringView(a).mid(si, lenA).compare(QStringView(b).mid(sj, lenB));
            if (c != 0)
                return c < 0;
        } else {
            if (a[i] != b[j])
                return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    if ((i < a.size()) != (j < b.size()))
        return i >= a.size();
    return a < b;
}

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
    int sortColumn;
    Qt::SortOrder sortOrder;

    ZAddressTablePriv(CWallet *_wallet, ZAddressTableModel *_parent):
        wallet(_wallet),
        parent(_parent),
        sortColumn(-1),
        sortOrder(Qt::AscendingOrder)
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


        std::set<libzcash::SaplingPaymentAddress> sapAddresses;
        wallet->GetSaplingPaymentAddresses(sapAddresses);
        for (auto addr : sapAddresses) {
            if (balances.count(addr) == 0)
                balances[addr] = 0;
        }

        std::set<libzcash::IronwoodPaymentAddress> ironAddresses;
        wallet->GetIronwoodPaymentAddresses(ironAddresses);
        for (auto addr : ironAddresses) {
            if (balances.count(addr) == 0)
                balances[addr] = 0;
        }

        for(std::map<libzcash::PaymentAddress, CAmount>::iterator it = balances.begin(); it != balances.end(); it++) {
            stringBalances[QString::fromStdString(EncodePaymentAddress(it->first))] = it->second;
        }

        for (std::map<QString, CAmount>::iterator bi = stringBalances.begin(); bi != stringBalances.end(); bi++) {
            QString address = bi->first;
            CAmount newBalance = bi->second;
            
            // Linear search through table (can't use binary search with grouping)
            for (int i = 0; i < cachedAddressTable.size(); i++) {
                ZAddressTableEntry& entry = cachedAddressTable[i];
                
                if (!entry.isGroup && entry.address == address) {
                    // Update child address balance
                    entry.balance = newBalance;
                    parent->emitDataChanged(i);
                    
                    // Find and update parent group balance
                    if (!entry.groupKey.isEmpty()) {
                        // Search backwards for the group header
                        for (int j = i - 1; j >= 0; j--) {
                            if (cachedAddressTable[j].isGroup && 
                                cachedAddressTable[j].groupKey == entry.groupKey) {
                                // Recalculate group total balance
                                CAmount totalBalance = 0;
                                for (int k = j + 1; k < cachedAddressTable.size() &&
                                     !cachedAddressTable[k].isGroup &&
                                     cachedAddressTable[k].groupKey == entry.groupKey; k++) {
                                    totalBalance += cachedAddressTable[k].balance;
                                }
                                cachedAddressTable[j].balance = totalBalance;
                                parent->emitDataChanged(j);
                                break;
                            }
                        }
                    }
                    break;
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

            // Group addresses by spending key fingerprint
            std::map<QString, QList<ZAddressTableEntry>> groupedAddresses;
            QList<ZAddressTableEntry> ungroupedAddresses;  // Collect ungrouped addresses separately
            std::map<libzcash::SaplingExtendedFullViewingKey, QString> saplingFvkToGroup;
            std::map<libzcash::IronwoodExtendedFullViewingKeyPirate, QString> ironwoodFvkToGroup;
            int saplingGroupCounter = 1;  // 1-based counter for Sapling groups
            int ironwoodGroupCounter = 1;  // 1-based counter for Ironwood groups

            for (const std::pair<libzcash::PaymentAddress, CAddressBookData>& item : wallet->mapZAddressBook)
            {
                const libzcash::PaymentAddress& zaddr = item.first;
                bool mine = false;
                QString groupKey;
                QString scope;
                
                // Check if this is a Sapling address
                auto saplingAddr = std::get_if<libzcash::SaplingPaymentAddress>(&zaddr);
                if (saplingAddr != nullptr) {
                    libzcash::SaplingIncomingViewingKey ivk;
                    libzcash::SaplingExtendedFullViewingKey extfvk;
                    if (wallet->GetSaplingIncomingViewingKey(*saplingAddr, ivk) &&
                        wallet->GetSaplingFullViewingKey(ivk, extfvk)) {
                            // Group by FVK regardless of whether we have the spending key
                            if (saplingFvkToGroup.find(extfvk) == saplingFvkToGroup.end()) {
                                saplingFvkToGroup[extfvk] = QString("sapl_%1").arg(saplingGroupCounter++);
                            }
                            groupKey = saplingFvkToGroup[extfvk];
                            if (wallet->HaveSaplingSpendingKey(extfvk)) {
                                mine = true;
                            }
                    }
                    // Get scope for Sapling addresses
                    KeyScope sapScope;
                    if (wallet->GetSaplingKeyScope(*saplingAddr, sapScope)) {
                        scope = (sapScope == KeyScope::External) ? QObject::tr("Normal") : QObject::tr("Change");
                    } else {
                        scope = QObject::tr("Normal");
                    }
                }

                // Check if this is an Ironwood address
                auto ironwoodAddr = std::get_if<libzcash::IronwoodPaymentAddress>(&zaddr);
                if (ironwoodAddr != nullptr) {
                    libzcash::IronwoodIncomingViewingKey ivk;
                    libzcash::IronwoodExtendedFullViewingKeyPirate extfvk;
                    if (wallet->GetIronwoodIncomingViewingKey(*ironwoodAddr, ivk) &&
                        wallet->GetIronwoodFullViewingKey(ivk, extfvk)) {
                            // Group by FVK regardless of whether we have the spending key
                            if (ironwoodFvkToGroup.find(extfvk) == ironwoodFvkToGroup.end()) {
                                ironwoodFvkToGroup[extfvk] = QString("iron_%1").arg(ironwoodGroupCounter++);
                            }
                            groupKey = ironwoodFvkToGroup[extfvk];
                            if (wallet->HaveIronwoodSpendingKey(extfvk)) {
                                mine = true;
                            }
                    }

                    // Get scope for Ironwood addresses
                    KeyScope ironScope;
                    if (wallet->GetIronwoodKeyScope(*ironwoodAddr, ironScope)) {
                        scope = (ironScope == KeyScope::External) ? QObject::tr("Normal") : QObject::tr("Change");
                    } else {
                        scope = QString("");
                    }
                }

                // Get balance for this address
                CAmount balance = 0;
                std::map<libzcash::PaymentAddress, CAmount>::iterator it = balances.find(zaddr);
                if (it != balances.end()) {
                    balance = it->second;
                }

                // Create entry
                ZAddressTableEntry::Type addressType = translateTransactionType(
                        QString::fromStdString(item.second.purpose), mine);
                const std::string& strName = item.second.name;

                ZAddressTableEntry entry;
                entry.type = addressType;
                entry.label = QString::fromStdString(strName);
                entry.address = QString::fromStdString(EncodePaymentAddress(zaddr));
                entry.balance = balance;
                entry.mine = mine;
                entry.scope = scope;
                entry.groupKey = groupKey;
                entry.level = 1;  // Child level
                
                if (!groupKey.isEmpty()) {
                    groupedAddresses[groupKey].append(entry);
                } else {
                    ungroupedAddresses.append(entry);  // Collect ungrouped addresses
                }
            }
            
            // Build table with groups and children first
            for (auto it = groupedAddresses.begin(); it != groupedAddresses.end(); ++it) {
                const QString& key = it->first;
                QList<ZAddressTableEntry>& children = it->second;
                
                if (children.isEmpty()) continue;
                
                // Create group header
                ZAddressTableEntry groupHeader;
                groupHeader.isGroup = true;
                groupHeader.groupKey = key;
                groupHeader.childCount = children.size();
                groupHeader.isExpanded = true;
                groupHeader.level = 0;
                groupHeader.type = children[0].type;
                
                // Calculate total balance and determine mine status from children
                CAmount totalBalance = 0;
                bool anyMine = false;
                for (const ZAddressTableEntry& child : children) {
                    totalBalance += child.balance;
                    if (child.mine) anyMine = true;
                }
                groupHeader.balance = totalBalance;
                groupHeader.mine = anyMine;
                
                // Build display name from key prefix and number
                QString displayName;
                if (key.startsWith("sapl_"))
                    displayName = QObject::tr("Sapling Key Group ") + key.mid(5);
                else if (key.startsWith("iron_"))
                    displayName = QObject::tr("Ironwood Key Group ") + key.mid(5);
                else
                    displayName = key;
                groupHeader.label = QString("");
                groupHeader.address = displayName;
                
                cachedAddressTable.append(groupHeader);
                
                // Add children if expanded, with Change addresses always last
                if (groupHeader.isExpanded) {
                    std::stable_sort(children.begin(), children.end(),
                        [](const ZAddressTableEntry& a, const ZAddressTableEntry& b) {
                            bool aChange = (a.scope == QObject::tr("Change"));
                            bool bChange = (b.scope == QObject::tr("Change"));
                            return aChange < bChange;  // false < true => Normal before Change
                        });
                    cachedAddressTable.append(children);
                }
            }
            
            // Add ungrouped addresses at the end
            cachedAddressTable.append(ungroupedAddresses);
        }
        // Re-apply the view's last requested sort so a rebuilt table (an address
        // added or removed) matches the order the header indicator shows.
        if (sortColumn >= 0)
            sort(sortColumn, sortOrder);
    }

    // Custom sort that maintains group structure
    void sort(int column, Qt::SortOrder order)
    {
        sortColumn = column;
        sortOrder = order;

        // Separate groups from ungrouped entries
        QList<QPair<ZAddressTableEntry, QList<ZAddressTableEntry>>> groups;
        QList<ZAddressTableEntry> ungrouped;
        
        int i = 0;
        while (i < cachedAddressTable.size()) {
            ZAddressTableEntry& entry = cachedAddressTable[i];
            
            if (entry.isGroup) {
                // This is a group header
                QList<ZAddressTableEntry> children;
                i++; // Move past group header
                
                // Collect all children (until next group or end)
                while (i < cachedAddressTable.size() && !cachedAddressTable[i].isGroup) {
                    children.append(cachedAddressTable[i]);
                    i++;
                }
                
                groups.append(qMakePair(entry, children));
            } else {
                // Ungrouped entry
                ungrouped.append(entry);
                i++;
            }
        }
        
        // Descending order must swap the operands rather than negate the
        // result: !(a < b) is true for equal elements, which violates the
        // strict weak ordering std::sort requires and lets its unguarded
        // partition run off the end of the list.
        auto columnLess = [column](const ZAddressTableEntry& a, const ZAddressTableEntry& b) {
            switch(column) {
                case ZAddressTableModel::isMine:
                    return a.mine < b.mine;
                case ZAddressTableModel::Balance:
                    return a.balance < b.balance;
                case ZAddressTableModel::Scope:
                    return a.scope < b.scope;
                case ZAddressTableModel::Address:
                    return naturalLess(a.address, b.address);
                case ZAddressTableModel::Label:
                    return naturalLess(a.label, b.label);
                default:
                    return false;
            }
        };
        auto orderedLess = [columnLess, order](const ZAddressTableEntry& a, const ZAddressTableEntry& b) {
            return (order == Qt::AscendingOrder) ? columnLess(a, b) : columnLess(b, a);
        };

        // Sort groups based on the column (using group header values). Headers
        // tie on columns they don't carry (e.g. Scope is always empty), so break
        // ties by group name to keep group order fixed across re-sorts.
        std::sort(groups.begin(), groups.end(),
            [orderedLess](const QPair<ZAddressTableEntry, QList<ZAddressTableEntry>>& a,
                          const QPair<ZAddressTableEntry, QList<ZAddressTableEntry>>& b) {
                if (orderedLess(a.first, b.first))
                    return true;
                if (orderedLess(b.first, a.first))
                    return false;
                return naturalLess(a.first.address, b.first.address);
            });

        // Sort children within each group: Change addresses always last, then by column
        const QString changeScope = QObject::tr("Change");
        for (auto& group : groups) {
            QList<ZAddressTableEntry>& children = group.second;
            std::stable_sort(children.begin(), children.end(),
                [orderedLess, &changeScope](const ZAddressTableEntry& a, const ZAddressTableEntry& b) {
                    bool aChange = (a.scope == changeScope);
                    bool bChange = (b.scope == changeScope);
                    // Change always last regardless of sort order
                    if (aChange != bChange)
                        return aChange < bChange;  // Normal before Change
                    return orderedLess(a, b);
                });
        }

        // Sort ungrouped entries
        std::sort(ungrouped.begin(), ungrouped.end(),
            [orderedLess](const ZAddressTableEntry& a, const ZAddressTableEntry& b) {
                if (orderedLess(a, b))
                    return true;
                if (orderedLess(b, a))
                    return false;
                return naturalLess(a.address, b.address);
            });
        
        // Rebuild table maintaining group structure
        cachedAddressTable.clear();
        
        // Add sorted groups with their children
        for (const auto& group : groups) {
            cachedAddressTable.append(group.first);  // Add group header
            if (group.first.isExpanded) {
                cachedAddressTable.append(group.second);  // Add children
            }
        }
        
        // Add sorted ungrouped entries
        cachedAddressTable.append(ungrouped);
    }

    void updateEntry(const QString &address, const QString &label, bool isMine, const QString &purpose, int status)
    {
        // Find address in model using linear search (grouping breaks binary search)
        int foundIndex = -1;
        for (int i = 0; i < cachedAddressTable.size(); i++) {
            if (!cachedAddressTable[i].isGroup && cachedAddressTable[i].address == address) {
                foundIndex = i;
                break;
            }
        }
        
        bool inModel = (foundIndex >= 0);
        ZAddressTableEntry::Type newEntryType = translateTransactionType(purpose, isMine);

        bool mine = false;
        ZAddressTableEntry newEntry;
        libzcash::PaymentAddress zaddr = DecodePaymentAddress(address.toStdString());
        auto saplingAddr = std::get_if<libzcash::SaplingPaymentAddress>(&zaddr);
        auto ironwoodAddr = std::get_if<libzcash::IronwoodPaymentAddress>(&zaddr);

        if (saplingAddr != nullptr || ironwoodAddr != nullptr) {

            if (saplingAddr != nullptr) {
                libzcash::SaplingIncomingViewingKey ivk;
                libzcash::SaplingExtendedFullViewingKey extfvk;
                if (wallet->GetSaplingIncomingViewingKey(*saplingAddr, ivk) &&
                    wallet->GetSaplingFullViewingKey(ivk, extfvk) &&
                    wallet->HaveSaplingSpendingKey(extfvk)) {
                        mine = true;
                }
            }

            if (ironwoodAddr != nullptr) {
                libzcash::IronwoodIncomingViewingKey ivk;
                libzcash::IronwoodExtendedFullViewingKeyPirate extfvk;
                if (wallet->GetIronwoodIncomingViewingKey(*ironwoodAddr, ivk) &&
                    wallet->GetIronwoodFullViewingKey(ivk, extfvk) &&
                    wallet->HaveIronwoodSpendingKey(extfvk)) {
                        mine = true;
                }
            }

            switch(status)
            {
            case CT_NEW:
                if(inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_NEW, but entry is already in model";
                    break;
                }
                
                // For grouped entries, add after the group header or at end
                // For now, just refresh the entire table to maintain grouping
                parent->beginResetModel();
                refreshAddressTable();
                parent->endResetModel();
                break;
                
            case CT_UPDATED:
                if(!inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_UPDATED, but entry is not in model";
                    break;
                }
                
                cachedAddressTable[foundIndex].type = newEntryType;
                cachedAddressTable[foundIndex].label = label;
                
                // Update scope/type for addresses
                if (ironwoodAddr != nullptr) {
                    KeyScope scope;
                    if (wallet->GetIronwoodKeyScope(*ironwoodAddr, scope)) {
                        cachedAddressTable[foundIndex].scope = (scope == KeyScope::External) ? QObject::tr("Normal") : QObject::tr("Change");
                    } else {
                        cachedAddressTable[foundIndex].scope = QString("");
                    }
                } else if (saplingAddr != nullptr) {
                    KeyScope sapScope;
                    if (wallet->GetSaplingKeyScope(*saplingAddr, sapScope)) {
                        cachedAddressTable[foundIndex].scope = (sapScope == KeyScope::External) ? QObject::tr("Normal") : QObject::tr("Change");
                    } else {
                        cachedAddressTable[foundIndex].scope = QObject::tr("Normal");
                    }
                } else {
                    cachedAddressTable[foundIndex].scope = QString("");  // Other address types
                }
                
                parent->emitDataChanged(foundIndex);
                break;
                
            case CT_DELETED:
                if(!inModel)
                {
                    qWarning() << "ZAddressTablePriv::updateEntry: Warning: Got CT_DELETED, but entry is not in model";
                    break;
                }
                // For grouped entries, refresh the entire table to maintain grouping
                parent->beginResetModel();
                refreshAddressTable();
                parent->endResetModel();
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
    columns << tr("Mine") << tr("Balance") << tr("Type") << tr("Address") << tr("Label");
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

void ZAddressTableModel::sort(int column, Qt::SortOrder order)
{
    Q_EMIT layoutAboutToBeChanged();

    // Record which entry each persistent index (selection, current item) points
    // at before the sort frees the entries, then move it to that entry's new
    // row. A group header is identified by its groupKey, an address row by its
    // address, which is unique in the table.
    const QModelIndexList oldIndexes = persistentIndexList();
    QList<QPair<bool, QString>> oldKeys;
    oldKeys.reserve(oldIndexes.size());
    for (const QModelIndex &idx : oldIndexes) {
        const ZAddressTableEntry *rec = priv->index(idx.row());
        if (rec)
            oldKeys.append(qMakePair(rec->isGroup, rec->isGroup ? rec->groupKey : rec->address));
        else
            oldKeys.append(qMakePair(false, QString()));
    }

    priv->sort(column, order);

    QHash<QString, int> groupRows, addressRows;
    for (int row = 0; row < priv->size(); row++) {
        const ZAddressTableEntry *rec = priv->index(row);
        if (rec->isGroup)
            groupRows.insert(rec->groupKey, row);
        else
            addressRows.insert(rec->address, row);
    }

    QModelIndexList newIndexes;
    newIndexes.reserve(oldIndexes.size());
    for (int i = 0; i < oldIndexes.size(); i++) {
        const QHash<QString, int> &rows = oldKeys[i].first ? groupRows : addressRows;
        auto it = rows.constFind(oldKeys[i].second);
        if (oldKeys[i].second.isEmpty() || it == rows.constEnd())
            newIndexes.append(QModelIndex());
        else
            newIndexes.append(index(it.value(), oldIndexes[i].column(), QModelIndex()));
    }
    changePersistentIndexList(oldIndexes, newIndexes);

    Q_EMIT layoutChanged();
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
            case Balance:
                {
                    return KomodoUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->balance, false, KomodoUnits::separatorStandard);
                }
            case Scope:
                // Don't show scope for group headers
                if (rec->isGroup) {
                    return QVariant();
                }
                return rec->scope;
            case Address:
                // For child addresses, add indentation
                if (rec->level > 0 && !rec->isGroup) {
                    return QString("    ") + rec->address;  // Indent child addresses
                }
                return rec->address;
            case Label:
                if (rec->isGroup) {
                    // Group headers show group label
                    return rec->label;
                } else if(rec->label.isEmpty() && role == Qt::DisplayRole) {
                    return tr("(no label)");
                } else {
                    return rec->label;
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
        // Make group headers bold
        if (rec->isGroup) {
            font.setBold(true);
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
    else if (role == AddressRole)
    {
        // Return raw address string (group headers return empty)
        if (rec->isGroup)
            return QVariant();
        return rec->address;
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
//            if(std::get_if<CNoDestination>(&newAddress))
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
        return Qt::NoItemFlags;

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

QString ZAddressTableModel::addRow(const QString &type, const QString &label, const QString &address, bool useDiversified, const QString &addressType)
{

    std::string strLabel = label.toStdString();
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
        // Check which shielded address type to generate based on network upgrade activation
        bool saplingActive = false;
        bool ironwoodActive = false;
        
        {
            LOCK(cs_main);
            if (chainActive.Tip() != nullptr) {
                int nHeight = chainActive.Height();
                const Consensus::Params& consensusParams = Params().GetConsensus();
                
                saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_SAPLING);
                ironwoodActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UPGRADE_IRONWOOD);
            }
        }
        
        // Use user-specified address type if provided, otherwise use default behavior
        bool useIronwood = false;
        bool useSapling = false;
        
        if (!addressType.isEmpty()) {
            // User explicitly chose a type
            if (addressType == "ironwood" && ironwoodActive) {
                useIronwood = true;
            } else if (addressType == "sapling" && saplingActive) {
                useSapling = true;
            } else {
                // Invalid or inactive selection
                editStatus = KEY_GENERATION_FAILURE;
                return QString();
            }
        } else {
            // Auto-select: prefer Ironwood if active, otherwise Sapling
            if (ironwoodActive) {
                useIronwood = true;
            } else if (saplingActive) {
                useSapling = true;
            } else {
                // Pre-Sapling - Sprout addresses are no longer supported
                editStatus = KEY_GENERATION_FAILURE;
                return QString();
            }
        }
        
        if (useIronwood) {
            // Generate Ironwood address
            if (useDiversified) {
                strAddress = EncodePaymentAddress(wallet->GenerateNewIronwoodDiversifiedAddress());
            } else {
                strAddress = EncodePaymentAddress(wallet->GenerateNewIronwoodZKey());
            }
        }
        else if (useSapling) {
            // Generate Sapling address
            if (useDiversified) {
                strAddress = EncodePaymentAddress(wallet->GenerateNewSaplingDiversifiedAddress());
            } else {
                strAddress = EncodePaymentAddress(wallet->GenerateNewSaplingZKey());
            }
        }
        else {
            // Should not reach here
            editStatus = KEY_GENERATION_FAILURE;
            return QString();
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
