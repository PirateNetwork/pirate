// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#ifndef KOMODO_QT_TRANSACTIONROWDELEGATE_H
#define KOMODO_QT_TRANSACTIONROWDELEGATE_H

#include <QAbstractItemDelegate>
#include <QColor>

class PlatformStyle;

/** Card-style row delegate for the main Transactions tab's QListView, modeled
 *  on OverviewPage's existing TxViewDelegate (same manual QRect/QPainterPath
 *  drawing technique -- there's no Designer equivalent for delegate painting)
 *  but richer: a rounded card per parent transaction with a tinted direction
 *  circle, a two-line text stack (description+amount, date+status), and a
 *  flat chrome-less indented line for child (Input/Output/Fee) rows.
 *
 *  Reads only roles TransactionTableModel::data() already exposes -- no
 *  model changes needed. The view is pinned to the ToAddress column via
 *  setModelColumn() (see TransactionView::setModel()), so Qt::DisplayRole
 *  here is already the same pre-formatted description text
 *  OverviewPage's TxViewDelegate reads the same way. */
class TransactionRowDelegate : public QAbstractItemDelegate
{
    Q_OBJECT

public:
    explicit TransactionRowDelegate(const PlatformStyle *platformStyle, QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

    /** Re-derive every theme-dependent color this delegate paints with, from
     *  the current "strTheme" setting. Called once at construction and again
     *  from TransactionView::updateIconTint() on every live theme switch --
     *  the same chain phase 1 already uses for icon/shadow retinting. */
    void setThemeColors();

private:
    const PlatformStyle *platformStyle;

    QColor cardBackground;
    QColor cardBorder;
    QColor textPrimary;
    QColor textSecondary;
};

#endif // KOMODO_QT_TRANSACTIONROWDELEGATE_H
