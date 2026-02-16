// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * @file transactionview.cpp
 * @brief Transaction list view with filtering and search capabilities
 * 
 * This widget provides a comprehensive transaction viewing interface with:
 * - Two-row filter bar (search + filters)
 * - Real-time search with address/label filtering
 * - Date range filtering (presets + custom range)
 * - Transaction type filtering (received/sent/mined/etc.)
 * - Amount filtering (minimum amount)
 * - Display limit control (50/100/200 transactions)
 * - Watch-only transaction visibility
 * - Expand/collapse hierarchical transaction display
 * - Context menu for copy/export operations
 * - CSV export functionality
 * - Lazy loading progress indicator
 * 
 * ARCHITECTURE:
 * - Uses TransactionFilterProxy for filtering TransactionTableModel data
 * - Filter changes trigger immediate updates via Qt signals
 * - Search uses debouncing (200ms delay) to avoid excessive filtering
 * - Integrates with lazy loading to show progress and enable filters after load
 * 
 * USER EXPERIENCE:
 * - All filters disabled during initial lazy load (prevents incomplete results)
 * - Progress indicator shows loading status and percentage
 * - Search button + Enter key both trigger search
 * - Clear button resets search field
 * - Reset button resets all filters to defaults
 * - Expand/Collapse All button toggles transaction hierarchy
 */

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

/**
 * @brief Construct transaction view with two-row filter interface
 * 
 * LAYOUT ARCHITECTURE:
 * - Row 1 (Search): Search field, Search button, Clear button, Address Only checkbox
 * - Row 2 (Filters): Watch-only, Date, Type, Limit, Min Amount, Reset, Expand/Collapse
 * - Date Range Widget: Custom date picker (hidden by default)
 * - Main Table: Transaction list with sorting and selection
 * 
 * FILTER BEHAVIOR:
 * - All filters start disabled during lazy load
 * - Enabled automatically after lazy load completes
 * - Search uses 200ms debounce to avoid excessive filtering
 * - Amount input validated for numeric format
 * 
 * SIGNAL CONNECTIONS:
 * - Filter changes: Immediate update via activated() signal
 * - Text fields: Debounced via QTimer (200ms delay)
 * - Buttons: Direct connection to slots
 * - Activity signals: All connected to sendResetUnlockSignal() for auto-lock timer
 * 
 * @param platformStyle Platform-specific styling preferences
 * @param parent Parent widget
 */
TransactionView::TransactionView(const PlatformStyle *platformStyle, QWidget *parent) :
    QWidget(parent), model(0), transactionProxyModel(0),
    transactionView(0), columnResizingFixer(0)
{
    /**
     * FIRST ROW: SEARCH INTERFACE
     * 
     * Layout: [Label] [Search Field] [Search Button] [Clear Button] [Address Only Checkbox] [Stretch]
     * 
     * Purpose: Provides address/label search with option to show only matching
     * address records (not full transaction). Search triggered by button or Enter key.
     */
    setContentsMargins(0,0,0,0);

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

    /**
     * SECOND ROW: COMPREHENSIVE FILTER CONTROLS
     * 
     * Layout: [Filter Label] [Watch-only] [Date] [Separator] [Type] [Separator] 
     *         [Limit Label] [Limit] [Separator] [Stretch] [Amount Label] [Amount] 
     *         [Separator] [Reset] [Expand/Collapse]
     * 
     * Purpose: Provides all filtering options with visual separators for grouping.
     * All filters disabled during lazy load to prevent incomplete results.
     */
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
    limitWidget->addItem(tr("50"), 50);
    limitWidget->addItem(tr("100"), 100);
    limitWidget->addItem(tr("200"), 200);
    limitWidget->setCurrentIndex(0); // Default to "50"
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

    /**
     * DEBOUNCE TIMERS
     * 
     * Prevent excessive filtering while user is typing in text fields.
     * 200ms delay provides good UX balance between responsiveness and performance.
     */
    static const int input_filter_delay = 200;

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);
    
    /**
     * LAZY LOAD STATUS INDICATOR
     * 
     * Shows loading progress and disables filters until complete.
     * Updates automatically via lazyLoadProgress() signal.
     * Changes to "All transactions loaded" when complete.
     */
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

    /**
     * CONTEXT MENU ACTIONS
     * 
     * Right-click menu provides:
     * - Copy operations: address, label, amount, txid, raw tx, full details
     * - Show operations: transaction details, memo
     * - Edit operation: address label
     * - External links: Block explorer URLs (configured in options)
     */
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

    // Connect actions (QSignalMapper requires old-style connection for mapped signal)
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(openThirdPartyTxUrl(QString)));

    // Connect filter changes - activated fires when user makes a selection
    // Modern Qt5 syntax provides compile-time type checking
    connect(dateWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::chooseDate);
    connect(typeWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::chooseType);
    connect(watchOnlyWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::chooseWatchonly);
    connect(limitWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::chooseLimit);
    connect(amountWidget, &QLineEdit::textChanged, amount_typing_delay, QOverload<>::of(&QTimer::start));
    connect(amount_typing_delay, &QTimer::timeout, this, &TransactionView::changedAmount);
    // Connect search button and Enter key to trigger search
    connect(searchButton, &QPushButton::clicked, this, &TransactionView::changedPrefix);
    connect(addressWidget, &QLineEdit::returnPressed, this, &TransactionView::changedPrefix);
    // Connect address only checkbox to toggle filter (no rebuild)
    connect(addressOnlyCheckbox, &QCheckBox::toggled, this, &TransactionView::toggleAddressOnly);
    // Connect clear button
    connect(clearButton, &QPushButton::clicked, this, &TransactionView::clearSearch);
    // Connect reset button
    connect(resetButton, &QPushButton::clicked, this, &TransactionView::resetFilters);

    connect(view, &QTableView::doubleClicked, this, &TransactionView::doubleClicked);
    connect(view, &QTableView::clicked, this, &TransactionView::handleTransactionClicked);
    connect(view, &QTableView::customContextMenuRequested, this, &TransactionView::contextualMenu);
    connect(expandAllButton, &QPushButton::clicked, this, &TransactionView::toggleExpandAll);

    connect(copyAddressAction, &QAction::triggered, this, &TransactionView::copyAddress);
    connect(copyLabelAction, &QAction::triggered, this, &TransactionView::copyLabel);
    connect(copyAmountAction, &QAction::triggered, this, &TransactionView::copyAmount);
    connect(copyTxIDAction, &QAction::triggered, this, &TransactionView::copyTxID);
    connect(copyTxHexAction, &QAction::triggered, this, &TransactionView::copyTxHex);
    connect(copyTxPlainText, &QAction::triggered, this, &TransactionView::copyTxPlainText);
    connect(editLabelAction, &QAction::triggered, this, &TransactionView::editLabel);
    connect(showMemoAction, &QAction::triggered, this, &TransactionView::showMemo);
    connect(showDetailsAction, &QAction::triggered, this, &TransactionView::showDetails);

    // Connect actions to reset lock timer
    // QSignalMapper requires old-style connection
    connect(mapperThirdPartyTxUrls, SIGNAL(mapped(QString)), this, SLOT(sendResetUnlockSignal()));
    // Modern Qt5 syntax for direct connections
    connect(dateWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::sendResetUnlockSignal);
    connect(typeWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::sendResetUnlockSignal);
    connect(watchOnlyWidget, QOverload<int>::of(&QComboBox::activated), this, &TransactionView::sendResetUnlockSignal);
    connect(amountWidget, &QLineEdit::textChanged, this, &TransactionView::sendResetUnlockSignal);
    connect(amount_typing_delay, &QTimer::timeout, this, &TransactionView::sendResetUnlockSignal);
    connect(addressWidget, &QLineEdit::textChanged, this, &TransactionView::sendResetUnlockSignal);
    connect(addressOnlyCheckbox, &QCheckBox::toggled, this, &TransactionView::sendResetUnlockSignal);
    connect(prefix_typing_delay, &QTimer::timeout, this, &TransactionView::sendResetUnlockSignal);
    connect(view, &QTableView::doubleClicked, this, &TransactionView::sendResetUnlockSignal);
    connect(view, &QTableView::customContextMenuRequested, this, &TransactionView::sendResetUnlockSignal);
    connect(copyAddressAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(copyLabelAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(copyAmountAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(copyTxIDAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(copyTxHexAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(copyTxPlainText, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(editLabelAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(showMemoAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
    connect(showDetailsAction, &QAction::triggered, this, &TransactionView::sendResetUnlockSignal);
}

/**
 * @brief Emit signal to reset wallet auto-lock timer
 * 
 * Called on any user activity to prevent wallet from locking during use.
 * Connected to all filter changes, button clicks, and context menu actions.
 */
void TransactionView::sendResetUnlockSignal() {
    Q_EMIT resetUnlockTimerEvent();
}

/**
 * @brief Initialize view with wallet model and configure proxy filtering
 * 
 * PROXY MODEL SETUP:
 * - Creates TransactionFilterProxy for filtering TransactionTableModel
 * - Configures dynamic sorting (updates as data changes)
 * - Sets case-insensitive filtering for search
 * - Sorts by date descending (newest first)
 * 
 * COLUMN CONFIGURATION:
 * - Sets fixed widths for Status, Watchonly, Date, Type, Amount
 * - ToAddress column dynamically sized using TableViewLastColumnResizingFixer
 * 
 * LAZY LOAD INTEGRATION:
 * - Connects to lazyLoadComplete and lazyLoadProgress signals
 * - Disables all filters until lazy load finishes
 * - Prevents incomplete filter results during initial load
 * 
 * BLOCK EXPLORER INTEGRATION:
 * - Parses third-party transaction URLs from options
 * - Adds "View on:" menu items to context menu
 * - Supports multiple explorer URLs separated by "|"
 * 
 * @param _model Wallet model providing transaction data
 */
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
                    // QSignalMapper requires old-style connection for map() slot
                    connect(thirdPartyTxUrlAction, SIGNAL(triggered()), mapperThirdPartyTxUrls, SLOT(map()));
                    mapperThirdPartyTxUrls->setMapping(thirdPartyTxUrlAction, listUrls[i].trimmed());
                }
            }
        }

        // show/hide column Watch-only
        updateWatchOnlyColumn(_model->haveWatchOnly());

        // Watch-only signal - modern Qt5 syntax
        connect(_model, &WalletModel::notifyWatchonlyChanged, this, &TransactionView::updateWatchOnlyColumn);
        
        // Connect lazy load signals - modern Qt5 syntax
        connect(_model->getTransactionTableModel(), &TransactionTableModel::lazyLoadComplete, 
                this, &TransactionView::onLazyLoadComplete);
        connect(_model->getTransactionTableModel(), &TransactionTableModel::lazyLoadProgress, 
                this, &TransactionView::onLazyLoadProgress);
        
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

/**
 * @brief Apply date range filter based on combo box selection
 * 
 * DATE RANGE CALCULATION:
 * - All: No date filtering (MIN_DATE to MAX_DATE)
 * - Today: Current date 00:00:00 to now
 * - This Week: Last Monday 00:00:00 to now
 * - This Month: 1st of current month 00:00:00 to now
 * - Last Month: 1st of previous month to 1st of current month
 * - This Year: January 1st 00:00:00 to now
 * - Range: Shows custom date picker, calls dateRangeChanged()
 * 
 * All ranges use startOfDay() to include full days.
 * Forces view reset to apply filter immediately (except Range).
 * 
 * @param idx Combo box item index
 */
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
}

/**
 * @brief Apply transaction type filter
 * 
 * Type filter uses bitmask to support multiple types:
 * - All: ALL_TYPES (0xFFFFFFFF)
 * - Received: RecvWithAddress | RecvWithAddressWithMemo | RecvFromOther
 * - Sent: SendToAddress | SendToAddressWithMemo | SendToOther
 * - To yourself: SendToSelf | SendToSelfWithMemo
 * - Mined: Generated
 * - Other: Other
 * 
 * @param idx Combo box item index
 */
void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
}

/**
 * @brief Apply watch-only filter
 * 
 * Filter options:
 * - All: Show all transactions
 * - Yes: Show only watch-only transactions (eye+ icon)
 * - No: Show only non-watch-only transactions (eye- icon)
 * 
 * @param idx Combo box item index
 */
void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        static_cast<TransactionFilterProxy::WatchOnlyFilter>(watchOnlyWidget->itemData(idx).toInt()));
}

/**
 * @brief Apply display limit filter
 * 
 * Limits number of parent transactions displayed to improve performance.
 * Options: 50, 100, 200 transactions.
 * 
 * Note: Limit applies to parent transactions only, not total records
 * (children are included with their parents).
 * 
 * @param idx Combo box item index
 */
void TransactionView::chooseLimit(int idx)
{
    if(!transactionProxyModel)
        return;
    int limit = limitWidget->itemData(idx).toInt();
    qDebug() << "TransactionView::chooseLimit: Setting limit to" << limit;
    transactionProxyModel->setLimit(limit);
}

/**
 * @brief Apply address/label search filter
 * 
 * SEARCH BEHAVIOR:
 * - Case-insensitive substring match on address or label
 * - Triggers filtered rebuild from cache with current filters
 * - Address Only checkbox state is also applied
 * 
 * PERFORMANCE:
 * - Rebuilds cachedWallet from decomposedTxCache (cached data)
 * - No wallet rescan needed - uses already-loaded transactions
 * 
 * DEBOUNCING:
 * - Called via 200ms timer after last keystroke (prefix_typing_delay)
 * - Also called immediately on Search button click or Enter key
 */
void TransactionView::changedPrefix()
{
    if(!transactionProxyModel)
        return;
    
    QString searchText = addressWidget->text();
    // setAddressPrefix automatically calls triggerSourceRebuild()
    transactionProxyModel->setAddressPrefix(searchText);
}

/**
 * @brief Toggle address-only filter mode
 * 
 * FILTER MODES:
 * - Unchecked: Show complete transaction (parent + all children) if any child matches search
 * - Checked: Show only parent + children that match search address/label
 * 
 * This is a display filter only - does NOT trigger data reload.
 * Works on currently displayed transactions in the table.
 * 
 * @param checked true to enable address-only mode, false to show full transactions
 */
void TransactionView::toggleAddressOnly(bool checked)
{
    if(!transactionProxyModel)
        return;
    
    transactionProxyModel->setShowAddressOnly(checked);
}

/**
 * @brief Clear search field and refresh to default view
 * 
 * Clears address search and triggers changedPrefix() to reload
 * cached transactions.
 */
void TransactionView::clearSearch()
{
    addressWidget->clear();
    changedPrefix(); // Triggers rebuild with empty search
}

/**
 * @brief Reset all filters to default values
 * 
 * DEFAULT VALUES:
 * - Date: All dates
 * - Type: All types
 * - Watch-only: All
 * - Limit: 50 transactions
 * - Amount: Empty (no minimum)
 * - Search: Empty
 * - Address Only: Unchecked
 * 
 * Triggers all filter update methods to apply changes.
 */
void TransactionView::resetFilters()
{
    // Reset all filters to default
    dateWidget->setCurrentIndex(0); // All dates
    typeWidget->setCurrentIndex(0); // All types
    watchOnlyWidget->setCurrentIndex(0); // All watch-only
    limitWidget->setCurrentIndex(0); // 50 transactions
    amountWidget->clear(); // Clear min amount
    addressWidget->clear(); // Clear search
    addressOnlyCheckbox->setChecked(false); // Uncheck address only
    
    // Trigger filter updates
    chooseDate(0);
    chooseType(0);
    chooseWatchonly(0);
    chooseLimit(0);
    changedAmount();
    changedPrefix();
}

/**
 * @brief Handle lazy load completion
 * 
 * COMPLETION ACTIONS:
 * - Updates status label to "All transactions loaded and indexed"
 * - Enables all filter controls (previously disabled)
 * - Leaves status visible so user knows all transactions are available
 * 
 * Called automatically by TransactionTableModel::lazyLoadComplete() signal.
 */
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

/**
 * @brief Update lazy load progress indicator
 * 
 * Shows percentage complete and reminds user that filters are disabled.
 * Called periodically by TransactionTableModel::lazyLoadProgress() signal.
 * 
 * @param loaded Number of transactions loaded so far
 * @param total Total number of transactions to load
 */
void TransactionView::onLazyLoadProgress(int loaded, int total)
{
    if (total > 0) {
        int percent = (loaded * 100) / total;
        lazyLoadStatusLabel->setText(tr("Loading transactions... %1% complete (filters disabled until complete)").arg(percent));
    }
}

/**
 * @brief Apply minimum amount filter
 * 
 * VALIDATION:
 * - Uses KomodoUnits::parse() to validate and convert input
 * - Accepts the current display unit (BTC, mBTC, etc.)
 * - Invalid input: Defaults to 0 (no minimum)
 * 
 * FILTER BEHAVIOR:
 * - Shows only transactions with absolute amount >= minimum
 * - Applies to both sent and received amounts
 * - Updates immediately on text change (with debouncing)
 */
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

/**
 * @brief Export filtered transactions to CSV file
 * 
 * EXPORT FORMAT:
 * - CSV format (comma-separated values)
 * - Exports currently filtered/visible transactions only
 * - Column order: Confirmed, Watch-only (if enabled), Date, Type, Label, Address, Amount, ID
 * 
 * COLUMNS EXPORTED:
 * - Confirmed: Status icon/text
 * - Watch-only: Only included if wallet has watch-only addresses
 * - Date: Transaction date/time
 * - Type: Send, Receive, etc.
 * - Label: Address book label
 * - Address: Transaction address
 * - Amount: Formatted in current display unit
 * - ID: Transaction hash
 * 
 * USER INTERACTION:
 * - Shows file save dialog
 * - Success/failure message displayed via message() signal
 * - Uses CSVModelWriter for proper escaping and formatting
 */
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

/**
 * @brief Handle row click to expand/collapse hierarchical transactions
 * 
 * HIERARCHY SUPPORT:
 * - Parent transactions can have multiple child records (inputs/outputs)
 * - Click toggles between collapsed (parent only) and expanded (show children)
 * - Source model maintains expansion state
 * 
 * INDEX MAPPING:
 * - Proxy index → Source index (required for filtering)
 * - Source model handles actual expand/collapse logic
 * - View automatically updates when model changes
 * 
 * USER EXPERIENCE:
 * - Single click to toggle (no double-click required)
 * - Expand All button can override individual states
 */
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

/**
 * @brief Toggle expand/collapse state for all transactions
 * 
 * BUTTON STATE:
 * - Checkable button changes text: "Expand All" ↔ "Collapse All"
 * - Button state syncs with action
 * 
 * MODEL OPERATION:
 * - Calls setAllTransactionsExpanded() on source model
 * - Model handles updating all transaction records
 * - Individual click states are overridden by this global state
 * 
 * PERFORMANCE:
 * - May trigger many row updates for large transaction lists
 * - Model emits signals for each changed row
 */
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

/**
 * @brief Show right-click context menu at cursor position
 * 
 * REQUIREMENTS:
 * - Must have at least one row selected
 * - Menu items configured in constructor with QSignalMapper
 * 
 * MENU CONTENTS:
 * - Copy address, label, amount, transaction ID, hex
 * - Copy full transaction details (plain text)
 * - Show transaction details dialog
 * - Show memo dialog
 * - Edit label (opens address book)
 * - Third-party block explorer links (if configured)
 * 
 * @param point Mouse position in table viewport coordinates
 */
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

/**
 * @brief Copy transaction address to clipboard
 * Uses GUIUtil::copyEntryData with AddressRole
 */
void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

/**
 * @brief Copy address label to clipboard
 * Uses GUIUtil::copyEntryData with LabelRole
 */
void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

/**
 * @brief Copy formatted amount to clipboard
 * Uses GUIUtil::copyEntryData with FormattedAmountRole (includes unit)
 */
void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

/**
 * @brief Copy transaction hash to clipboard
 * Uses GUIUtil::copyEntryData with TxIDRole (64-character hex)
 */
void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxIDRole);
}

/**
 * @brief Copy raw transaction hex to clipboard
 * Uses GUIUtil::copyEntryData with TxHexRole (full serialized transaction)
 */
void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

/**
 * @brief Copy full transaction details as formatted plain text
 * 
 * Uses GUIUtil::copyEntryData with TxPlainTextRole.
 * Includes all transaction details in human-readable format.
 */
void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
}

/**
 * @brief Edit label for transaction address
 * 
 * ADDRESS BOOK INTEGRATION:
 * - Looks up address in AddressTableModel
 * - If found: Opens EditAddressDialog for editing existing entry
 * - If not found: Opens dialog to add new sending address
 * - Handles both receiving and sending address types
 * 
 * REQUIREMENTS:
 * - Transaction must have an associated address
 * - Address book must be available from wallet model
 * 
 * USER FLOW:
 * 1. Check if transaction has address
 * 2. Look up address in address book
 * 3. Open appropriate dialog (edit existing or add new)
 * 4. Dialog changes saved to address book
 */
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

/**
 * @brief Show full transaction details dialog
 * 
 * Opens TransactionDescDialog with FULL_TRANSACTION mode.
 * Dialog displays all transaction information including:
 * - Status, date, amount
 * - Inputs and outputs
 * - Transaction ID and hex
 * - Confirmations
 * - Memo (if present)
 */
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

/**
 * @brief Show memo-only dialog
 * 
 * Opens TransactionDescDialog with MEMO_ONLY mode.
 * Specialized view for displaying encrypted memo attached to shielded transactions.
 * Only relevant for z-address transactions with memos.
 */
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

/**
 * @brief Open transaction in third-party block explorer
 * 
 * URL FORMAT:
 * - Uses template URL with %s placeholder
 * - Replaces %s with transaction hash
 * - Example: "https://explorer.pirate.black/tx/%s"
 * 
 * CONFIGURATION:
 * - URLs configured via QSignalMapper in constructor
 * - Multiple explorers can be added to context menu
 * - Opens in default system browser
 * 
 * @param url URL template with %s placeholder for transaction hash
 */
void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

/**
 * @brief Create custom date range picker widget
 * 
 * WIDGET STRUCTURE:
 * - QFrame with Panel | Raised style
 * - Two QDateTimeEdit widgets (from and to)
 * - Calendar popup for easy date selection
 * - Label "Range:" and "to" between date pickers
 * 
 * DEFAULT VALUES:
 * - From: 7 days ago
 * - To: Current date
 * - Display format: dd/MM/yy
 * 
 * BEHAVIOR:
 * - Hidden by default (shown when "Range" date option selected)
 * - dateRangeChanged() slot called when either date changes
 * - Appears below main date filter combo box
 * 
 * @return Pointer to configured date range widget
 */
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
    connect(dateFrom, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);
    connect(dateTo, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);

    return dateRangeWidget;
}

/**
 * @brief Apply custom date range filter
 * 
 * RANGE CALCULATION:
 * - From: Start of selected date (00:00:00)
 * - To: Start of day after selected date (includes full target day)
 * 
 * TRIGGER CONDITIONS:
 * - Called when either dateFrom or dateTo changes
 * - Called from chooseDate() when "Range" option selected
 * 
 * Updates proxy model date range immediately.
 */
void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            dateFrom->date().startOfDay(),
            dateTo->date().startOfDay().addDays(1));
}

/**
 * @brief Scroll to and select specific transaction
 * 
 * INDEX MAPPING:
 * - Input: Source model index
 * - Maps to proxy model index for display
 * - Handles filtering (transaction may not be visible)
 * 
 * ACTIONS:
 * - Scrolls table to make transaction visible
 * - Sets as current selection
 * - Gives focus to table view
 * 
 * USAGE:
 * - Navigate from other views (e.g., OverviewPage)
 * - Jump to specific transaction by hash or position
 * 
 * @param idx Source model index of transaction to focus
 */
void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

/**
 * @brief Handle widget resize events
 * 
 * COLUMN RESIZING:
 * - Uses columnResizingFixer to maintain proportional column widths
 * - Stretches ToAddress column to fill available space
 * - Maintains minimum widths for other columns
 * 
 * ARCHITECTURE:
 * - Overrides QWidget::resizeEvent()
 * - Ensures table columns adjust smoothly with window resize
 * - GUIUtil::TableViewLastColumnResizingFixer handles the details
 * 
 * @param event Resize event containing old and new sizes
 */
void TransactionView::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(TransactionTableModel::ToAddress);
}

/**
 * @brief Custom event filter for Ctrl+C override
 * 
 * COPY BEHAVIOR OVERRIDE:
 * - Default Qt behavior: Copies DisplayRole text only
 * - Custom behavior: Copies full transaction details (TxPlainTextRole)
 * - Provides more useful clipboard content for users
 * 
 * WHY NEEDED:
 * - Amount column uses custom display formatting
 * - Users expect Ctrl+C to copy complete transaction info
 * - Matches behavior of dedicated "Copy" context menu items
 * 
 * INSTALLATION:
 * - Installed in constructor via installEventFilter(this)
 * - Only intercepts KeyPress events
 * - Returns true to prevent default handling
 * 
 * @param obj Object that generated the event
 * @param event Event to filter (only KeyPress handled)
 * @return true if event handled, false to continue default processing
 */
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

/**
 * @brief Show or hide watch-only column and filter
 * 
 * WATCH-ONLY SUPPORT:
 * - Watch-only addresses: Addresses for which wallet has public key but not private key
 * - Can view transactions but cannot spend
 * - Column shows icon indicating watch-only status
 * 
 * VISIBILITY:
 * - Hidden if wallet has no watch-only addresses (common case)
 * - Shown when wallet contains at least one watch-only address
 * - Filter combo box visibility matches column visibility
 * 
 * CALLED BY:
 * - WalletModel when watch-only status changes
 * - setModel() during initialization
 * 
 * @param fHaveWatchOnly true if wallet has watch-only addresses, false otherwise
 */
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    watchOnlyWidget->setVisible(fHaveWatchOnly);
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, !fHaveWatchOnly);
}
