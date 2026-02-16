// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2026 The Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KOMODO_QT_TRANSACTIONVIEW_H
#define KOMODO_QT_TRANSACTIONVIEW_H

#include "guiutil.h"

#include <QWidget>
#include <QKeyEvent>

class PlatformStyle;
class TransactionFilterProxy;
class WalletModel;

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QLabel;
class QLineEdit;
class QMenu;
class QModelIndex;
class QSignalMapper;
class QTableView;
QT_END_NAMESPACE

/**
 * @brief Transaction list view widget with advanced filtering and search
 * 
 * Provides a comprehensive transaction viewing interface with two-row filter bar:
 * 
 * ROW 1 (Search):
 * - Search field: Filter by address or label (case-insensitive)
 * - Search button: Trigger search (also Enter key)
 * - Clear button: Clear search field
 * - Address Only checkbox: Show only matching address records (not full transaction)
 * 
 * ROW 2 (Filters):
 * - Watch-only filter: All/Yes/No watch-only transactions
 * - Date filter: Presets (Today/Week/Month/Year) or custom range
 * - Type filter: All/Received/Sent/To yourself/Mined/Other
 * - Limit: Number of transactions to display (50/100/200)
 * - Min amount: Minimum transaction amount filter
 * - Reset: Reset all filters to defaults
 * - Expand/Collapse All: Toggle transaction hierarchy
 * 
 * LAZY LOADING INTEGRATION:
 * - All filters disabled during initial load (prevents incomplete results)
 * - Progress label shows loading percentage
 * - Filters automatically enabled after lazy load completes
 * 
 * CONTEXT MENU:
 * - Copy address/label/amount/txid/raw transaction
 * - Show transaction details or memo
 * - Edit address label
 * - View on block explorer (configurable URLs)
 * 
 * PERFORMANCE:
 * - Search uses 200ms debouncing to avoid excessive filtering
 * - Filter changes trigger immediate proxy model updates
 * - Hierarchical display supports expand/collapse for parent transactions
 */
class TransactionView : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionView(const PlatformStyle *platformStyle, QWidget *parent = 0);

    void setModel(WalletModel *model);
    
    QLabel* getLazyLoadStatusLabel() { return lazyLoadStatusLabel; }

    /**
     * @brief Date range filter presets
     * 
     * Defines common date range options for transaction filtering.
     * Range option displays custom date picker widget.
     */
    enum DateEnum
    {
        All,         ///< Show all transactions (no date filter)
        Today,       ///< Today's transactions only
        ThisWeek,    ///< Current week (Monday to today)
        ThisMonth,   ///< Current month (1st to today)
        LastMonth,   ///< Previous month (1st to last day)
        ThisYear,    ///< Current year (Jan 1 to today)
        Range        ///< Custom date range (shows date picker)
    };

    /**
     * @brief Column width constants for transaction table
     * 
     * Fixed widths ensure consistent layout. ToAddress column is
     * dynamically sized using TableViewLastColumnResizingFixer.
     */
    enum ColumnWidths {
        STATUS_COLUMN_WIDTH = 30,              ///< Status icon column
        WATCHONLY_COLUMN_WIDTH = 23,           ///< Watch-only eye icon column
        DATE_COLUMN_WIDTH = 120,               ///< Date/time column
        TYPE_COLUMN_WIDTH = 113,               ///< Transaction type column
        AMOUNT_MINIMUM_COLUMN_WIDTH = 120,     ///< Amount column minimum width
        MINIMUM_COLUMN_WIDTH = 23              ///< Absolute minimum column width
    };

private:
    WalletModel *model;                            ///< Wallet model providing transaction data
    TransactionFilterProxy *transactionProxyModel; ///< Proxy model for filtering transactions
    QTableView *transactionView;                   ///< Table view displaying transactions

    /** @name Filter widgets */
    /**@{*/
    QComboBox *dateWidget;           ///< Date range filter combo box
    QComboBox *typeWidget;           ///< Transaction type filter combo box
    QComboBox *watchOnlyWidget;      ///< Watch-only filter combo box
    QComboBox *limitWidget;          ///< Display limit combo box (50/100/200)
    QLineEdit *addressWidget;        ///< Address/label search field
    QCheckBox *addressOnlyCheckbox;  ///< Show only matching address records checkbox
    QLineEdit *amountWidget;         ///< Minimum amount filter field
    QLabel *lazyLoadStatusLabel;     ///< Lazy load progress indicator
    /**@}*/

    QMenu *contextMenu;                    ///< Right-click context menu
    QSignalMapper *mapperThirdPartyTxUrls; ///< Maps block explorer URLs to actions

    /** @name Custom date range widgets */
    /**@{*/
    QFrame *dateRangeWidget;    ///< Container for custom date range pickers
    QDateTimeEdit *dateFrom;    ///< Start date picker
    QDateTimeEdit *dateTo;      ///< End date picker
    /**@}*/

    /**
     * @brief Create custom date range picker widget
     * @return Widget with from/to date pickers
     */
    QWidget *createDateRangeWidget();

    GUIUtil::TableViewLastColumnResizingFixer *columnResizingFixer; ///< Manages ToAddress column resizing

    virtual void resizeEvent(QResizeEvent* event);

    bool eventFilter(QObject *obj, QEvent *event);

private Q_SLOTS:
    void contextualMenu(const QPoint &);          ///< Show right-click context menu
    void dateRangeChanged();                      ///< Handle custom date range change
    void showDetails();                           ///< Show transaction details dialog
    void showMemo();                              ///< Show transaction memo dialog
    void copyAddress();                           ///< Copy transaction address to clipboard
    void editLabel();                             ///< Edit address label in address book
    void copyLabel();                             ///< Copy address label to clipboard
    void copyAmount();                            ///< Copy transaction amount to clipboard
    void copyTxID();                              ///< Copy transaction ID to clipboard
    void copyTxHex();                             ///< Copy raw transaction hex to clipboard
    void copyTxPlainText();                       ///< Copy full transaction details to clipboard
    void openThirdPartyTxUrl(QString url);        ///< Open block explorer URL for transaction
    void updateWatchOnlyColumn(bool fHaveWatchOnly); ///< Show/hide watch-only column
    void sendResetUnlockSignal();                 ///< Emit signal to reset wallet lock timer
    void handleTransactionClicked(const QModelIndex &index); ///< Handle transaction click (expand/collapse)
    void toggleExpandAll();                       ///< Toggle expand/collapse all transactions
    void toggleAddressOnly(bool checked);         ///< Toggle address-only filter mode
    void clearSearch();                           ///< Clear search field and refresh
    void resetFilters();                          ///< Reset all filters to defaults
    void onLazyLoadComplete();                    ///< Handle lazy load completion (enable filters)
    void onLazyLoadProgress(int loaded, int total); ///< Update lazy load progress indicator

Q_SIGNALS:
    /** Emitted when user double-clicks a transaction */
    void doubleClicked(const QModelIndex&);

    /** Emitted to display message to user (title, message, style) */
    void message(const QString &title, const QString &message, unsigned int style);

    /** Emitted on user activity to reset wallet auto-lock timer */
    void resetUnlockTimerEvent();

public Q_SLOTS:
    void chooseDate(int idx);        ///< Handle date filter selection
    void chooseType(int idx);        ///< Handle type filter selection
    void chooseWatchonly(int idx);   ///< Handle watch-only filter selection
    void chooseLimit(int idx);       ///< Handle limit filter selection
    void changedAmount();            ///< Handle amount filter change (after debounce)
    void changedPrefix();            ///< Handle search field change (after debounce or button click)
    void exportClicked();            ///< Export transactions to CSV
    void focusTransaction(const QModelIndex&); ///< Scroll to and focus specific transaction

};

#endif // KOMODO_QT_TRANSACTIONVIEW_H
