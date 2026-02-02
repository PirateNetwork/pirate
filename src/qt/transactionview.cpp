// Copyright (c) 2011-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionview.h"

#include "addresstablemodel.h"
#include "komodounits.h"
#include "csvmodelwriter.h"
#include "editaddressdialog.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "platformstyle.h"
//#include "sendcoinsdialog.h"
#include "transactiondescdialog.h"
#include "transactionfilterproxy.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"

#include "ui_interface.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDebug>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPoint>
#include <QProgressDialog>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalMapper>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

TransactionView::TransactionView(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0), columnResizingFixer(0)
{
    // Build filter row
    setContentsMargins(0,0,0,0);

    // First line: search field, search button, clear button, address only checkbox
    QHBoxLayout *searchLayout = new QHBoxLayout();
    searchLayout->setContentsMargins(0,3,0,3);
    if (platformStyle->getUseExtraSpacing()) {
        searchLayout->setSpacing(8);
        searchLayout->addSpacing(26);
    } else {
        searchLayout->setSpacing(6);
        searchLayout->addSpacing(23);
    }

    QLabel *searchLabel = new QLabel(tr("Search:"), this);
    QFont searchFont = searchLabel->font();
    searchFont.setBold(true);
    searchLabel->setFont(searchFont);
    searchLabel->setStyleSheet("QLabel { color: white; }");
    searchLayout->addWidget(searchLabel);
    
    addressWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    addressWidget->setPlaceholderText(tr("Enter address or label to search"));
#endif
    addressWidget->setMinimumWidth(200);
    searchLayout->addWidget(addressWidget);

    // Add search button
    QPushButton *searchButton = new QPushButton(tr("Search"), this);
    searchButton->setObjectName("searchButton");
    if (platformStyle->getUseExtraSpacing()) {
        searchButton->setFixedWidth(70);
    } else {
        searchButton->setFixedWidth(60);
    }
    searchLayout->addWidget(searchButton);

    // Add clear button
    QPushButton *clearButton = new QPushButton(tr("Clear"), this);
    clearButton->setObjectName("clearButton");
    if (platformStyle->getUseExtraSpacing()) {
        clearButton->setFixedWidth(70);
    } else {
        clearButton->setFixedWidth(60);
    }
    searchLayout->addWidget(clearButton);

    // Add small spacing to visually separate checkbox
    searchLayout->addSpacing(10);

    addressOnlyCheckbox = new QCheckBox(tr("Address only"), this);
    addressOnlyCheckbox->setToolTip(tr("Show only matching address records (not full transaction)"));
    searchLayout->addWidget(addressOnlyCheckbox);

    searchLayout->addStretch();

    // Second line: all other filter controls
    QHBoxLayout *hlayout2 = new QHBoxLayout();
    hlayout2->setContentsMargins(0,3,0,3);
    if (platformStyle->getUseExtraSpacing()) {
        hlayout2->setSpacing(8);
        hlayout2->addSpacing(26);
    } else {
        hlayout2->setSpacing(6);
        hlayout2->addSpacing(23);
    }

    QLabel *filterLabel = new QLabel(tr("Filter:"), this);
    QFont filterFont = filterLabel->font();
    filterFont.setBold(true);
    filterLabel->setFont(filterFont);
    filterLabel->setStyleSheet("QLabel { color: white; }");
    hlayout2->addWidget(filterLabel);
    
    watchOnlyWidget = new QComboBox(this);
    watchOnlyWidget->setFixedWidth(24);
    watchOnlyWidget->addItem("", TransactionFilterProxy::WatchOnlyFilter_All);
    watchOnlyWidget->addItem(platformStyle->SingleColorIcon(":/icons/eye_plus"), "", TransactionFilterProxy::WatchOnlyFilter_Yes);
    watchOnlyWidget->addItem(platformStyle->SingleColorIcon(":/icons/eye_minus"), "", TransactionFilterProxy::WatchOnlyFilter_No);
    hlayout2->addWidget(watchOnlyWidget);

    dateWidget = new QComboBox(this);
    dateWidget->setToolTip(tr("Filter transactions by date range"));
    if (platformStyle->getUseExtraSpacing()) {
        dateWidget->setFixedWidth(121);
    } else {
        dateWidget->setFixedWidth(120);
    }
    dateWidget->addItem(tr("All"), All);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range..."), Range);
    dateWidget->setObjectName("dateComboBox");
    hlayout2->addWidget(dateWidget);

    // Add visual separator
    QFrame *separator1 = new QFrame(this);
    separator1->setFrameShape(QFrame::VLine);
    separator1->setFrameShadow(QFrame::Sunken);
    separator1->setLineWidth(1);
    hlayout2->addWidget(separator1);

    typeWidget = new QComboBox(this);
    typeWidget->setToolTip(tr("Filter transactions by type"));
    if (platformStyle->getUseExtraSpacing()) {
        typeWidget->setFixedWidth(121);
    } else {
        typeWidget->setFixedWidth(120);
    }

    typeWidget->addItem(tr("All"),          TransactionFilterProxy::ALL_TYPES);
    typeWidget->addItem(tr("Received"),     TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddressWithMemo) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    typeWidget->addItem(tr("Sent to"),      TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::SendToAddressWithMemo) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::SendToOther) );
    typeWidget->addItem(tr("To yourself"),  TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf) |
                                            TransactionFilterProxy::TYPE(TransactionRecord::SendToSelfWithMemo) );
    typeWidget->addItem(tr("Mined"),        TransactionFilterProxy::TYPE(TransactionRecord::Generated));
    typeWidget->addItem(tr("Other"),        TransactionFilterProxy::TYPE(TransactionRecord::Other));
    typeWidget->setObjectName("typeComboBox");
    hlayout2->addWidget(typeWidget);

    // Add visual separator
    QFrame *separator2 = new QFrame(this);
    separator2->setFrameShape(QFrame::VLine);
    separator2->setFrameShadow(QFrame::Sunken);
    separator2->setLineWidth(1);
    hlayout2->addWidget(separator2);

    // Add limit combo box
    QLabel *limitLabel = new QLabel(tr("Limit:"), this);
    limitLabel->setStyleSheet("QLabel { color: white; }");
    hlayout2->addWidget(limitLabel);
    
    limitWidget = new QComboBox(this);
    limitWidget->setToolTip(tr("Limit number of transactions displayed"));
    limitWidget->addItem(tr("All"), -1);
    limitWidget->addItem(tr("50"), 50);
    limitWidget->addItem(tr("100"), 100);
    limitWidget->addItem(tr("200"), 200);
    limitWidget->addItem(tr("500"), 500);
    limitWidget->addItem(tr("1000"), 1000);
    limitWidget->setCurrentIndex(1); // Default to "50"
    if (platformStyle->getUseExtraSpacing()) {
        limitWidget->setFixedWidth(85);
    } else {
        limitWidget->setFixedWidth(80);
    }
    hlayout2->addWidget(limitWidget);

    // Add visual separator
    QFrame *separator3 = new QFrame(this);
    separator3->setFrameShape(QFrame::VLine);
    separator3->setFrameShadow(QFrame::Sunken);
    separator3->setLineWidth(1);
    hlayout2->addWidget(separator3);

    // Add stretch to push remaining items to the right
    hlayout2->addStretch();

    // Add min amount label
    QLabel *amountLabel = new QLabel(tr("Min amount:"), this);
    amountLabel->setStyleSheet("QLabel { color: white; }");
    hlayout2->addWidget(amountLabel);

    amountWidget = new QLineEdit(this);
#if QT_VERSION >= 0x040700
    amountWidget->setPlaceholderText(tr("0.00"));
#endif
    if (platformStyle->getUseExtraSpacing()) {
        amountWidget->setFixedWidth(97);
    } else {
        amountWidget->setFixedWidth(100);
    }
    amountWidget->setValidator(new QDoubleValidator(0, 1e20, 8, this));
    hlayout2->addWidget(amountWidget);

    // Add visual separator
    QFrame *separator4 = new QFrame(this);
    separator4->setFrameShape(QFrame::VLine);
    separator4->setFrameShadow(QFrame::Sunken);
    separator4->setLineWidth(1);
    hlayout2->addWidget(separator4);

    // Add reset button
    QPushButton *resetButton = new QPushButton(tr("Reset"), this);
    resetButton->setObjectName("resetButton");
    resetButton->setToolTip(tr("Reset all filters to default"));
    if (platformStyle->getUseExtraSpacing()) {
        resetButton->setFixedWidth(70);
    } else {
        resetButton->setFixedWidth(60);
    }
    hlayout2->addWidget(resetButton);

    // Add expand/collapse all button
    QPushButton *expandAllButton = new QPushButton(tr("Collapse All"), this);
    expandAllButton->setObjectName("expandAllButton");
    expandAllButton->setCheckable(true);
    expandAllButton->setChecked(true); // Start checked since transactions are expanded by default
    if (platformStyle->getUseExtraSpacing()) {
        expandAllButton->setFixedWidth(90);
    } else {
        expandAllButton->setFixedWidth(85);
    }
    hlayout2->addWidget(expandAllButton);

    // Delay before filtering transactions in ms
    static const int input_filter_delay = 200;

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);
    
    // Create lazy load status label
    lazyLoadStatusLabel = new QLabel(tr("Loading transactions... (filters disabled until complete)"), this);
    lazyLoadStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; }");
    lazyLoadStatusLabel->setAlignment(Qt::AlignCenter);
    lazyLoadStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred); // Allow label to expand horizontally
    lazyLoadStatusLabel->setMinimumHeight(30); // Give it a minimum height for proper spacing
    lazyLoadStatusLabel->setVisible(true);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->setContentsMargins(0,0,0,0);
    vlayout->setSpacing(0);

    QTableView *view = new QTableView(this);
    vlayout->addLayout(searchLayout);
    vlayout->addLayout(hlayout2);
    vlayout->addWidget(createDateRangeWidget());
    vlayout->addWidget(view, 1); // Give the table view a stretch factor of 1
    
    vlayout->setSpacing(0);
    int width = view->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
    if (platformStyle->getUseExtraSpacing()) {
        searchLayout->addSpacing(width+2);
    } else {
        searchLayout->addSpacing(width);
    }
    // Always show scroll bar
    view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    view->setTabKeyNavigation(false);
    view->setContextMenuPolicy(Qt::CustomContextMenu);

    view->installEventFilter(this);

    transactionView = view;
    transactionView->setObjectName("transactionView");

    // Actions
    QAction *copyAddressAction = new QAction(tr("Copy address"), this);
    QAction *copyLabelAction = new QAction(tr("Copy label"), this);
    QAction *copyAmountAction = new QAction(tr("Copy amount"), this);
    QAction *copyTxIDAction = new QAction(tr("Copy transaction ID"), this);
    QAction *copyTxHexAction = new QAction(tr("Copy raw transaction"), this);
    QAction *copyTxPlainText = new QAction(tr("Copy full transaction details"), this);
    QAction *editLabelAction = new QAction(tr("Edit label"), this);
    QAction *showMemoAction = new QAction(tr("Show Memo"), this);
    QAction *showDetailsAction = new QAction(tr("Show transaction details"), this);

    contextMenu = new QMenu(this);
    contextMenu->setObjectName("contextMenu");
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTxIDAction);
    contextMenu->addAction(copyTxHexAction);
    contextMenu->addAction(copyTxPlainText);
    contextMenu->addAction(showMemoAction);
    contextMenu->addAction(showDetailsAction);
    contextMenu->addSeparator();
    contextMenu->addAction(editLabelAction);

    mapperThirdPartyTxUrls = new QSignalMapper(this);

    // Connect actions
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(openThirdPartyTxUrl(QString)));

    // Connect filter changes - activated fires when user makes a selection
    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(chooseDate(int)));
    connect(typeWidget, SIGNAL(activated(int)), this, SLOT(chooseType(int)));
    connect(watchOnlyWidget, SIGNAL(activated(int)), this, SLOT(chooseWatchonly(int)));
    connect(limitWidget, SIGNAL(activated(int)), this, SLOT(chooseLimit(int)));
    connect(amountWidget, SIGNAL(textChanged(QString)), amount_typing_delay, SLOT(start()));
    connect(amount_typing_delay, SIGNAL(timeout()), this, SLOT(changedAmount()));
    // Connect search button and Enter key to trigger search
    connect(searchButton, SIGNAL(clicked()), this, SLOT(changedPrefix()));
    connect(addressWidget, SIGNAL(returnPressed()), this, SLOT(changedPrefix()));
    // Connect clear button
    connect(clearButton, SIGNAL(clicked()), this, SLOT(clearSearch()));
    // Connect reset button
    connect(resetButton, SIGNAL(clicked()), this, SLOT(resetFilters()));

    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SIGNAL(doubleClicked(QModelIndex)));
    connect(view, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(contextualMenu(QPoint)));
    connect(expandAllButton, SIGNAL(clicked()), this, SLOT(toggleExpandAll()));

    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(copyAddress()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(copyLabel()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(copyAmount()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(copyTxID()));
    connect(copyTxHexAction, SIGNAL(triggered()), this, SLOT(copyTxHex()));
    connect(copyTxPlainText, SIGNAL(triggered()), this, SLOT(copyTxPlainText()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(editLabel()));
    connect(showMemoAction, SIGNAL(triggered()), this, SLOT(showMemo()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(showDetails()));

    // Connect actions to reset lock timer
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(sendResetUnlockSignal()));
    connect(dateWidget, SIGNAL(activated(int)), this, SLOT(sendResetUnlockSignal()));
    connect(typeWidget, SIGNAL(activated(int)), this, SLOT(sendResetUnlockSignal()));
    connect(watchOnlyWidget, SIGNAL(activated(int)), this, SLOT(sendResetUnlockSignal()));
    connect(amountWidget, SIGNAL(textChanged(QString)), this, SLOT(sendResetUnlockSignal()));
    connect(amount_typing_delay, SIGNAL(timeout()), this, SLOT(sendResetUnlockSignal()));
    connect(addressWidget, SIGNAL(textChanged(QString)), this, SLOT(sendResetUnlockSignal()));
    connect(prefix_typing_delay, SIGNAL(timeout()), this, SLOT(sendResetUnlockSignal()));
    connect(view, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(sendResetUnlockSignal()));
    connect(view, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(sendResetUnlockSignal()));
    connect(copyAddressAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(copyLabelAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(copyAmountAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(copyTxIDAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(copyTxHexAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(copyTxPlainText, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(editLabelAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(showMemoAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
    connect(showDetailsAction, SIGNAL(triggered()), this, SLOT(sendResetUnlockSignal()));
}

void TransactionView::sendResetUnlockSignal() {
    Q_EMIT resetUnlockTimerEvent();
}

void TransactionView::setModel(WalletModel *_model)
{
    this->model = _model;
    if(_model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel(_model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);

        transactionProxyModel->setSortRole(Qt::EditRole);

        transactionView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        transactionView->setModel(transactionProxyModel);
        transactionView->setAlternatingRowColors(true);
        transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
        transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        transactionView->setSortingEnabled(true);
        transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        transactionView->verticalHeader()->hide();

        transactionView->setColumnWidth(TransactionTableModel::Status, STATUS_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Watchonly, WATCHONLY_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Date, DATE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Type, TYPE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);

        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(transactionView, AMOUNT_MINIMUM_COLUMN_WIDTH, MINIMUM_COLUMN_WIDTH, this);

        if (_model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = _model->getOptionsModel()->getThirdPartyTxUrls().split("|", Qt::SkipEmptyParts);
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString host = QUrl(listUrls[i].trimmed(), QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    QAction *thirdPartyTxUrlAction = new QAction("View on: " + host, this); // use host as menu item label
                    if (i == 0)
                        contextMenu->addSeparator();
                    contextMenu->addAction(thirdPartyTxUrlAction);
                    connect(thirdPartyTxUrlAction, SIGNAL(triggered()), mapperThirdPartyTxUrls, SLOT(map()));
                    mapperThirdPartyTxUrls->setMapping(thirdPartyTxUrlAction, listUrls[i].trimmed());
                }
            }
        }

        // show/hide column Watch-only
        updateWatchOnlyColumn(_model->haveWatchOnly());

        // Watch-only signal
        connect(_model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyColumn(bool)));
        
        // Connect lazy load signals
        connect(_model->getTransactionTableModel(), SIGNAL(lazyLoadComplete()), this, SLOT(onLazyLoadComplete()));
        connect(_model->getTransactionTableModel(), SIGNAL(lazyLoadProgress(int,int)), this, SLOT(onLazyLoadProgress(int,int)));
        
        // Disable all filters until lazy load completes
        dateWidget->setEnabled(false);
        typeWidget->setEnabled(false);
        watchOnlyWidget->setEnabled(false);
        limitWidget->setEnabled(false);
        amountWidget->setEnabled(false);
        addressWidget->setEnabled(false);
        addressOnlyCheckbox->setEnabled(false);
        
        // Initialize limit to default value (50)
        transactionProxyModel->setLimit(50);
    }
}

void TransactionView::chooseDate(int idx)
{
    if(!transactionProxyModel)
        return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                TransactionFilterProxy::MIN_DATE,
                TransactionFilterProxy::MAX_DATE);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                current.startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                startOfWeek.startOfDay(),
                TransactionFilterProxy::MAX_DATE);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                QDate(current.year(), current.month(), 1).startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                QDate(current.year(), current.month(), 1).addMonths(-1).startOfDay(),
                QDate(current.year(), current.month(), 1).startOfDay());
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                QDate(current.year(), 1, 1).startOfDay(),
                TransactionFilterProxy::MAX_DATE);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
    
    // Force view to update (except for Range which calls dateRangeChanged)
    if(transactionView && dateWidget->itemData(idx).toInt() != Range) {
        transactionView->reset();
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
    // Force view to update
    if(transactionView) {
        transactionView->reset();
    }
}

void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        static_cast<TransactionFilterProxy::WatchOnlyFilter>(watchOnlyWidget->itemData(idx).toInt()));
    // Force view to update
    if(transactionView) {
        transactionView->reset();
    }
}

void TransactionView::chooseLimit(int idx)
{
    if(!transactionProxyModel)
        return;
    int limit = limitWidget->itemData(idx).toInt();
    qDebug() << "TransactionView::chooseLimit: Setting limit to" << limit;
    transactionProxyModel->setLimit(limit);
    // Force view to update
    if(transactionView) {
        transactionView->reset();
    }
}

void TransactionView::changedPrefix()
{
    if(!transactionProxyModel)
        return;
    
    QString searchText = addressWidget->text();
    transactionProxyModel->setAddressPrefix(searchText);
    transactionProxyModel->setShowAddressOnly(addressOnlyCheckbox->isChecked());
    
    // If searching, reload wallet to search entire history
    // Otherwise, use the cached 200 transactions
    if (!searchText.isEmpty() && model) {
        QProgressDialog progress(tr("Searching transactions..."), tr("Cancel"), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(500); // Only show if operation takes > 500ms
        progress.setValue(0);
        
        model->getTransactionTableModel()->refreshWallet();
        
        progress.setValue(1);
    }
}

void TransactionView::clearSearch()
{
    addressWidget->clear();
    
    QProgressDialog progress(tr("Refreshing transactions..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setValue(0);
    
    changedPrefix(); // This will trigger refresh to reload default 200
    
    progress.setValue(1);
}

void TransactionView::resetFilters()
{
    QProgressDialog progress(tr("Resetting filters..."), tr("Cancel"), 0, 0, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);
    progress.setValue(0);
    
    // Reset all filters to default
    dateWidget->setCurrentIndex(0); // All dates
    typeWidget->setCurrentIndex(0); // All types
    watchOnlyWidget->setCurrentIndex(0); // All watch-only
    limitWidget->setCurrentIndex(1); // 50 transactions
    amountWidget->clear(); // Clear min amount
    addressWidget->clear(); // Clear search
    addressOnlyCheckbox->setChecked(false); // Uncheck address only
    
    // Trigger filter updates
    chooseDate(0);
    chooseType(0);
    chooseWatchonly(0);
    chooseLimit(1);
    changedAmount();
    changedPrefix();
    
    progress.setValue(1);
}

void TransactionView::onLazyLoadComplete()
{
    // Update status label
    lazyLoadStatusLabel->setText(tr("All transactions loaded and indexed"));
    lazyLoadStatusLabel->setStyleSheet("QLabel { color: white; font-weight: bold; }"); // White
    
    // Enable all filter controls
    dateWidget->setEnabled(true);
    typeWidget->setEnabled(true);
    watchOnlyWidget->setEnabled(true);
    limitWidget->setEnabled(true);
    amountWidget->setEnabled(true);
    addressWidget->setEnabled(true);
    addressOnlyCheckbox->setEnabled(true);
    
    // Keep status visible so user knows all transactions are loaded
}

void TransactionView::onLazyLoadProgress(int loaded, int total)
{
    if (total > 0) {
        int percent = (loaded * 100) / total;
        lazyLoadStatusLabel->setText(tr("Loading transactions... %1% complete (filters disabled until complete)").arg(percent));
    }
}

void TransactionView::changedAmount()
{
    if(!transactionProxyModel)
        return;
    CAmount amount_parsed = 0;
    if (KomodoUnits::parse(model->getOptionsModel()->getDisplayUnit(), amountWidget->text(), &amount_parsed)) {
        transactionProxyModel->setMinAmount(amount_parsed);
    }
    else
    {
        transactionProxyModel->setMinAmount(0);
    }
}

void TransactionView::exportClicked()
{
    if (!model || !model->getOptionsModel()) {
        return;
    }

    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Transaction History"), QString(),
        tr("Comma separated file (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    if (model && model->haveWatchOnly())
        writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(KomodoUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxIDRole);

    if(!writer.write()) {
        Q_EMIT message(tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
    }
    else {
        Q_EMIT message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::handleTransactionClicked(const QModelIndex &index)
{
    if (!index.isValid() || !transactionProxyModel)
        return;
    
    // Map proxy index to source model index
    QModelIndex sourceIndex = transactionProxyModel->mapToSource(index);
    if (!sourceIndex.isValid())
        return;
    
    // Get the source model and toggle expand/collapse
    TransactionTableModel* sourceModel = qobject_cast<TransactionTableModel*>(transactionProxyModel->sourceModel());
    if (sourceModel) {
        sourceModel->toggleTransactionExpanded(sourceIndex);
    }
}

void TransactionView::toggleExpandAll()
{
    if (!transactionProxyModel)
        return;
    
    // Get the source model
    TransactionTableModel* sourceModel = qobject_cast<TransactionTableModel*>(transactionProxyModel->sourceModel());
    if (sourceModel) {
        // Find the button and toggle its state
        QPushButton* button = findChild<QPushButton*>("expandAllButton");
        if (button) {
            bool expanding = button->isChecked();
            button->setText(expanding ? tr("Collapse All") : tr("Expand All"));
            sourceModel->setAllTransactionsExpanded(expanding);
        }
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    // check if transaction can be abandoned, disable context menu action in case it doesn't
    uint256 hash;
    hash.SetHex(selection.at(0).data(TransactionTableModel::TxHashRole).toString().toStdString());

    if(index.isValid())
    {
        contextMenu->popup(transactionView->viewport()->mapToGlobal(point));
    }
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxIDRole);
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            EditAddressDialog dlg(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg.setModel(addressBook);
            dlg.loadRow(idx);
            dlg.exec();
        }
        else
        {
            // Add sending address
            EditAddressDialog dlg(EditAddressDialog::NewSendingAddress,
                this);
            dlg.setModel(addressBook);
            dlg.setAddress(address);
            dlg.exec();
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0), FULL_TRANSACTION);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

void TransactionView::showMemo()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0), MEMO_ONLY);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setFrameStyle(QFrame::Panel | QFrame::Raised);
    dateRangeWidget->setContentsMargins(1,1,1,1);
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing(23);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("dd/MM/yy");
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(100);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("dd/MM/yy");
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(100);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));
    connect(dateTo, SIGNAL(dateChanged(QDate)), this, SLOT(dateRangeChanged()));

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            dateFrom->date().startOfDay(),
            dateTo->date().startOfDay().addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

// We override the virtual resizeEvent of the QWidget to adjust tables column
// sizes as the tables width is proportional to the dialogs width.
void TransactionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddress);
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
             return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide column Watch-only
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    watchOnlyWidget->setVisible(fHaveWatchOnly);
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, !fHaveWatchOnly);
}
