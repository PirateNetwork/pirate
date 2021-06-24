// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include "zaddressbookpage.h"
#include "ui_zaddressbookpage.h"

#include "zaddresstablemodel.h"
#include "pirateoceangui.h"
#include "csvmodelwriter.h"
#include "editzaddressdialog.h"
#include "guiutil.h"
#include "platformstyle.h"
#include "walletmodel.h"
#include "transactiondescdialog.h"

#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>

ZAddressBookPage::ZAddressBookPage(const PlatformStyle *platformStyle, Mode _mode, Tabs _tab, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ZAddressBookPage),
    model(0),
    mode(_mode),
    tab(_tab)
{
    ui->setupUi(this);

    if (!platformStyle->getImagesOnButtons()) {
        ui->newAddress->setIcon(QIcon());
        ui->copyAddress->setIcon(QIcon());
        ui->deleteAddress->setIcon(QIcon());
        ui->exportButton->setIcon(QIcon());
    } else {
        ui->newAddress->setIcon(platformStyle->SingleColorIcon(":/icons/add"));
        ui->copyAddress->setIcon(platformStyle->SingleColorIcon(":/icons/editcopy"));
        ui->deleteAddress->setIcon(platformStyle->SingleColorIcon(":/icons/remove"));
        ui->exportButton->setIcon(platformStyle->SingleColorIcon(":/icons/export"));
    }

    ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->tableView->setFocus();

    setWindowTitle(tr("Receiving z-addresses"));
    ui->labelExplanation->setText(tr("These are your Pirate z-addresses for receiving payments. It is recommended to use a new receiving z-address for each transaction."));
    ui->labelExplanation->setVisible(false);

    //Hide close button
    ui->closeButton->hide();
    ui->exportButton->hide();
    ui->deleteAddress->setVisible(false);

    // Context menu actions
    QAction *copyAddressAction = new QAction(tr("&Copy Address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy &Label"), this);
    QAction *editAction = new QAction(tr("&Edit"), this);

    // QAction *copyZSendManyToAction = new QAction(tr("Copy zsendmany (to) template"), this);
    // QAction *copyZSendManyFromAction = new QAction(tr("Copy zsendmany (from) template"), this);

    QAction *exportSpendingKeyAction = new QAction(tr("Export extended spending key"), this);
    QAction *exportViewingKeyAction = new QAction(tr("Export extended viewing key"), this);

    // Build context menu
    contextMenu = new QMenu(this);
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(editAction);

    // contextMenu->addAction(copyZSendManyToAction);
    // contextMenu->addAction(copyZSendManyFromAction);

    contextMenu->addAction(exportSpendingKeyAction);
    contextMenu->addAction(exportViewingKeyAction);

    contextMenu->addSeparator();

    // Connect signals for context menu actions
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(on_copyAddress_clicked()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(onCopyLabelAction()));
    connect(editAction, SIGNAL(triggered()), this, SLOT(onEditAction()));

    // connect(copyZSendManyToAction, SIGNAL(triggered()), this, SLOT(onCopyZSendManyToAction()));
    // connect(copyZSendManyFromAction, SIGNAL(triggered()), this, SLOT(onCopyZSendManyFromAction()));

    connect(exportSpendingKeyAction, SIGNAL(triggered()), this, SLOT(exportSK()));
    connect(exportViewingKeyAction, SIGNAL(triggered()), this, SLOT(exportVK()));

    connect(ui->tableView, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
}

ZAddressBookPage::~ZAddressBookPage()
{
    delete ui;
}

void ZAddressBookPage::setWalletModel(WalletModel *_walletModel) {
    this->walletModel = _walletModel;
}

void ZAddressBookPage::setModel(ZAddressTableModel *_model)
{
    this->model = _model;
    if(!_model)
        return;

    proxyModel = new QSortFilterProxyModel(this);
    proxyModel->setSourceModel(_model);
    proxyModel->setDynamicSortFilter(true);
    proxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel->setSortRole(Qt::EditRole);


    switch(tab)
    {
    case ReceivingTab:
        // Receive filter
        proxyModel->setFilterRole(ZAddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(ZAddressTableModel::Receive);
        break;
    case SendingTab:
        // Send filter
        proxyModel->setFilterRole(ZAddressTableModel::TypeRole);
        proxyModel->setFilterFixedString(ZAddressTableModel::Send);
        break;
    }
    ui->tableView->setModel(proxyModel);
    ui->tableView->setSortingEnabled(true);
    ui->tableView->sortByColumn(2, Qt::AscendingOrder);

    ui->tableView->setColumnWidth(ZAddressTableModel::isMine, 60);
    ui->tableView->setColumnWidth(ZAddressTableModel::Balance, 80);
    ui->tableView->setColumnWidth(ZAddressTableModel::Label, 80);

    // Set column widths
    ui->tableView->horizontalHeader()->setSectionResizeMode(ZAddressTableModel::isMine, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(ZAddressTableModel::Balance, QHeaderView::ResizeToContents);
    ui->tableView->horizontalHeader()->setSectionResizeMode(ZAddressTableModel::Label, QHeaderView::Stretch);
    ui->tableView->horizontalHeader()->setSectionResizeMode(ZAddressTableModel::Address, QHeaderView::ResizeToContents);

    // Select row for newly created address
    connect(_model, SIGNAL(rowsInserted(QModelIndex,int,int)), this, SLOT(selectNewAddress(QModelIndex,int,int)));

}

void ZAddressBookPage::exportSK()
{
    QModelIndexList selection = GUIUtil::getEntryData(ui->tableView, ZAddressTableModel::Address);
    if(!selection.isEmpty())
    {

        QString key = walletModel->getSpendingKey(selection.at(0).data(ZAddressTableModel::Address).toString());
        if (key != "") {
            TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0), SPENDING_KEY, key);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->show();
        }
    }
}

void ZAddressBookPage::exportVK()
{
    QModelIndexList selection = GUIUtil::getEntryData(ui->tableView, ZAddressTableModel::Address);
    if(!selection.isEmpty())
    {

        QString key = walletModel->getViewingKey(selection.at(0).data(ZAddressTableModel::Address).toString());

        if (key != "") {
          TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0), VIEWING_KEY, key);
          dlg->setAttribute(Qt::WA_DeleteOnClose);
          dlg->show();
        }
    }
}


void ZAddressBookPage::onCopyZSendManyFromAction()
{
    QModelIndexList selection = GUIUtil::getEntryData(ui->tableView, ZAddressTableModel::Address);
    if(!selection.isEmpty())
    {
        QString commandTemplate;
        commandTemplate = QString("z_sendmany %1 '[{\"address\":\"%2\",\"amount\":\"%3\"}]'").arg(selection.at(0).data(Qt::DisplayRole).toString(),QString("YOUR_Z_ADDRESS_TO"),QString::number(0,'f',8));
        GUIUtil::setClipboard(commandTemplate);
    }
}

void ZAddressBookPage::onCopyZSendManyToAction()
{
    QModelIndexList selection = GUIUtil::getEntryData(ui->tableView, ZAddressTableModel::Address);
    if(!selection.isEmpty())
    {
        QString commandTemplate;
        commandTemplate = QString("z_sendmany %1 '[{\"address\":\"%2\",\"amount\":\"%3\"}]'").arg(QString("YOUR_Z_ADDRESS_FROM"),selection.at(0).data(Qt::DisplayRole).toString(),QString::number(0,'f',8));
        GUIUtil::setClipboard(commandTemplate);
    }
}

void ZAddressBookPage::on_copyAddress_clicked()
{
    GUIUtil::copyEntryData(ui->tableView, ZAddressTableModel::Address);
}

void ZAddressBookPage::onCopyLabelAction()
{
    GUIUtil::copyEntryData(ui->tableView, ZAddressTableModel::Label);
}

void ZAddressBookPage::onEditAction()
{
    if(!model)
        return;

    if(!ui->tableView->selectionModel())
        return;
    QModelIndexList indexes = ui->tableView->selectionModel()->selectedRows();
    if(indexes.isEmpty())
        return;

    EditZAddressDialog dlg(EditZAddressDialog::EditReceivingAddress, this);
    dlg.setObjectName("NewZAddress");
    dlg.setModel(model);
    QModelIndex origIndex = proxyModel->mapToSource(indexes.at(0));
    dlg.loadRow(origIndex.row());
    dlg.exec();
}

void ZAddressBookPage::on_newAddress_clicked()
{
    if(!model)
        return;

    EditZAddressDialog dlg(EditZAddressDialog::NewReceivingAddress, this);
    dlg.setObjectName("NewZAddress");
    dlg.setModel(model);
    if(dlg.exec())
    {
        newAddressToSelect = dlg.getAddress();
    }
}

void ZAddressBookPage::on_exportButton_clicked()
{
    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export z-address List"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(proxyModel);
    writer.addColumn("Label", ZAddressTableModel::Label, Qt::EditRole);
    writer.addColumn("Address", ZAddressTableModel::Address, Qt::EditRole);

    if(!writer.write()) {
        QMessageBox::critical(this, tr("Exporting Failed"),
            tr("There was an error trying to save the z-address list to %1. Please try again.").arg(filename));
    }
}

void ZAddressBookPage::contextualMenu(const QPoint &point)
{
    QModelIndex index = ui->tableView->indexAt(point);
    if(index.isValid())
    {
        contextMenu->exec(QCursor::pos());
    }
}

void ZAddressBookPage::selectNewAddress(const QModelIndex &parent, int begin, int /*end*/)
{
    QModelIndex idx = proxyModel->mapFromSource(model->index(begin, ZAddressTableModel::Address, parent));
    if(idx.isValid() && (idx.data(Qt::EditRole).toString() == newAddressToSelect))
    {
        // Select row of newly created address, once
        ui->tableView->setFocus();
        ui->tableView->selectRow(idx.row());
        newAddressToSelect.clear();
    }
}
