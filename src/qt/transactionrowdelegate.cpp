// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit-license.php.

#include "transactionrowdelegate.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "platformstyle.h"
#include "transactiontablemodel.h"

#include <algorithm>

#include <QApplication>
#include <QDateTime>
#include <QFontMetrics>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QSettings>

namespace {
    const int CARD_RADIUS = 14;
    const int CARD_MARGIN = 4;    // inset from the QListView item's own rect, on top of its setSpacing() gap
    const int CARD_PADDING = 12;
    const int CIRCLE_SIZE = 40;
    const int CIRCLE_TEXT_GAP = 12;
    const int LINE_GAP = 4;
    const int CHILD_INDENT = CIRCLE_SIZE + CIRCLE_TEXT_GAP;
}

TransactionRowDelegate::TransactionRowDelegate(const PlatformStyle *_platformStyle, QObject *parent) :
    QAbstractItemDelegate(parent),
    platformStyle(_platformStyle)
{
    setThemeColors();
}

void TransactionRowDelegate::setThemeColors()
{
    QSettings settings;
    bool fDarkTheme = (settings.value("strTheme", "dark").toString() == "dark");
    if (fDarkTheme) {
        cardBackground = QColor(0x11, 0x17, 0x22); // backgroundSurface
        cardBorder = QColor(255, 255, 255, 10);    // borderSubtle, ~4%
        textPrimary = QColor(0xF7, 0xF9, 0xFC);
        textSecondary = QColor(0xD0, 0xD6, 0xE0);
    } else {
        cardBackground = QColor(0xFF, 0xFF, 0xFF);
        cardBorder = QColor(0, 0, 0, 8);           // borderSubtle, ~3%
        textPrimary = QColor(0x0B, 0x12, 0x20);
        textSecondary = QColor(0x2A, 0x33, 0x42);
    }
}

void TransactionRowDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    bool isParent = index.data(TransactionTableModel::IsParentRole).toBool();
    QRect mainRect = option.rect;

    QString description = index.data(Qt::DisplayRole).toString();
    QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
    qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
    bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();

    QSettings settings;
    bool fDarkTheme = (settings.value("strTheme", "dark").toString() == "dark");
    QColor amountColor;
    if (amount < 0)
        amountColor = fDarkTheme ? COLOR_NEGATIVE_DARK : COLOR_NEGATIVE;
    else if (amount > 0)
        amountColor = fDarkTheme ? COLOR_POSITIVE_DARK : COLOR_POSITIVE;
    else if (!confirmed)
        amountColor = COLOR_UNCONFIRMED;
    else
        amountColor = textPrimary;

    // FormattedAmountRole is already unit-formatted by the model (and kept
    // in sync with OptionsModel::displayUnitChanged there) -- reading it
    // instead of re-deriving from AmountRole avoids this delegate needing
    // its own unit-tracking/sync plumbing.
    QString amountText = index.data(TransactionTableModel::FormattedAmountRole).toString();
    if (!confirmed)
        amountText = QString("[") + amountText + QString("]");

    if (!isParent) {
        // Child (Input/Output/Fee) row: a flat, chrome-less, indented line --
        // no card background/border/icon-circle, matching today's expand/
        // collapse visual distinction (previously italic font + leading
        // spaces in the formatted string; the indentation is preserved,
        // font/color now carry the distinction instead).
        QRect textRect(mainRect.left() + CHILD_INDENT, mainRect.top(), mainRect.width() - CHILD_INDENT, mainRect.height());
        painter->setPen(textSecondary);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, description);
        painter->setPen(amountColor);
        painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, amountText);
        painter->restore();
        return;
    }

    // Card chrome
    QRect cardRect = mainRect.adjusted(0, 0, 0, -CARD_MARGIN);
    QPainterPath path;
    path.addRoundedRect(cardRect, CARD_RADIUS, CARD_RADIUS);
    painter->fillPath(path, cardBackground);
    painter->setPen(QPen(cardBorder, 1));
    painter->drawPath(path);

    // Direction circle -- filled at ~12% alpha of the direction color,
    // holding the existing tx-type icon (mined/input/output/inout) rather
    // than sourcing new arrow glyphs.
    QColor directionColor = (amount >= 0) ? COLOR_POSITIVE_DARK : COLOR_NEGATIVE_DARK;
    QRect circleRect(cardRect.left() + CARD_PADDING, cardRect.top() + (cardRect.height() - CIRCLE_SIZE) / 2, CIRCLE_SIZE, CIRCLE_SIZE);
    QColor circleFill = directionColor;
    circleFill.setAlpha(31); // ~12%
    painter->setPen(Qt::NoPen);
    painter->setBrush(circleFill);
    painter->drawEllipse(circleRect);

    QIcon icon = qvariant_cast<QIcon>(index.data(TransactionTableModel::RawDecorationRole));
    icon = platformStyle->SingleColorIcon(icon);
    int iconInset = 8;
    icon.paint(painter, circleRect.adjusted(iconInset, iconInset, -iconInset, -iconInset));

    if (index.data(TransactionTableModel::WatchonlyRole).toBool())
    {
        QIcon iconWatchonly = qvariant_cast<QIcon>(index.data(TransactionTableModel::WatchonlyDecorationRole));
        iconWatchonly = platformStyle->SingleColorIcon(iconWatchonly);
        QRect watchonlyRect(circleRect.right() - 12, circleRect.bottom() - 12, 16, 16);
        iconWatchonly.paint(painter, watchonlyRect);
    }

    // Text stack: line 1 = description (left) + amount (right);
    // line 2 = date (left) + Confirmed/Pending status (right).
    int textLeft = circleRect.right() + CIRCLE_TEXT_GAP;
    int textRight = cardRect.right() - CARD_PADDING;
    QFontMetrics fm = painter->fontMetrics();
    int lineHeight = fm.height();
    int textTop = cardRect.top() + (cardRect.height() - (2 * lineHeight + LINE_GAP)) / 2;

    QRect line1Rect(textLeft, textTop, textRight - textLeft, lineHeight);
    QRect line2Rect(textLeft, textTop + lineHeight + LINE_GAP, textRight - textLeft, lineHeight);

    painter->setPen(textPrimary);
    QRect descBounding;
    painter->drawText(line1Rect, Qt::AlignLeft | Qt::AlignVCenter, description, &descBounding);
    painter->setPen(amountColor);
    painter->drawText(line1Rect, Qt::AlignRight | Qt::AlignVCenter, amountText);

    painter->setPen(textSecondary);
    painter->drawText(line2Rect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));
    painter->setPen(confirmed ? textSecondary : COLOR_UNCONFIRMED);
    painter->drawText(line2Rect, Qt::AlignRight | Qt::AlignVCenter, confirmed ? QObject::tr("Confirmed") : QObject::tr("Pending"));

    painter->restore();
}

QSize TransactionRowDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    bool isParent = index.data(TransactionTableModel::IsParentRole).toBool();
    QFontMetrics fm(option.font);
    if (!isParent) {
        return QSize(200, fm.height() + 8);
    }
    int textHeight = 2 * fm.height() + LINE_GAP + 2 * CARD_PADDING;
    int height = std::max(textHeight, CIRCLE_SIZE + 2 * CARD_PADDING);
    return QSize(200, height + CARD_MARGIN);
}
