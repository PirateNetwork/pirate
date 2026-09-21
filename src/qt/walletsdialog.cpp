// Copyright (c) 2026 Pirate Chain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletsdialog.h"

#include "platformstyle.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

/** A clickable card -- QFrame has no clicked() of its own. The "..." button
 *  inside it consumes its own presses, so it never triggers the card. */
class WalletCard : public QFrame
{
    Q_OBJECT

public:
    explicit WalletCard(QWidget *parent = nullptr) : QFrame(parent), pressed(false)
    {
        setCursor(Qt::PointingHandCursor);
    }

Q_SIGNALS:
    void clicked();

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        pressed = (event->button() == Qt::LeftButton);
        QFrame::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (pressed && event->button() == Qt::LeftButton && rect().contains(event->pos()))
            Q_EMIT clicked();
        pressed = false;
        QFrame::mouseReleaseEvent(event);
    }

private:
    bool pressed;
};

} // namespace

WalletsDialog::WalletsDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    platformStyle(_platformStyle)
{
    setObjectName("WalletsDialog");
    setWindowTitle(tr("Wallets"));
    setModal(true);
    setMinimumWidth(460);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(16);

    QLabel *title = new QLabel(tr("Wallets"), this);
    title->setObjectName("walletsTitle");
    root->addWidget(title);

    // Cards live in a scroll area so a long wallet list can't grow the
    // dialog off-screen; sized to its content up to a cap.
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setMaximumHeight(320);
    scroll->viewport()->setAutoFillBackground(false);
    QWidget *cardHost = new QWidget(scroll);
    cardHost->setObjectName("walletsCardHost");
    cardsLayout = new QVBoxLayout(cardHost);
    cardsLayout->setContentsMargins(0, 0, 0, 0);
    cardsLayout->setSpacing(10);
    scroll->setWidget(cardHost);
    root->addWidget(scroll);

    emptyLabel = new QLabel(tr("No wallets are open. Create a new one or load an existing wallet file."), this);
    emptyLabel->setObjectName("walletsEmptyLabel");
    emptyLabel->setWordWrap(true);
    root->addWidget(emptyLabel);

    QPushButton *newButton = new QPushButton(tr("New wallet..."), this);
    newButton->setObjectName("walletsNewButton");
    newButton->setToolTip(tr("Create a brand-new, freshly-seeded secondary wallet"));
    connect(newButton, &QPushButton::clicked, this, &WalletsDialog::newWalletRequested);
    root->addWidget(newButton);

    QPushButton *loadButton = new QPushButton(tr("Load wallet..."), this);
    loadButton->setObjectName("walletsLoadButton");
    loadButton->setToolTip(tr("Load an existing wallet file as a secondary wallet"));
    connect(loadButton, &QPushButton::clicked, this, &WalletsDialog::loadWalletRequested);
    root->addWidget(loadButton);

    QPushButton *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName("walletsCloseButton");
    closeButton->setDefault(true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    QHBoxLayout *footer = new QHBoxLayout();
    footer->addWidget(closeButton);
    footer->addStretch();
    root->addLayout(footer);
}

void WalletsDialog::setWallets(const QList<WalletEntry> &wallets, const QString &current)
{
    // Drop the old cards first. deleteLater() rather than delete: this is
    // also called from a slot fired by one of those very cards' menu actions.
    while (QLayoutItem *item = cardsLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    emptyLabel->setVisible(wallets.isEmpty());

    for (const WalletEntry &entry : wallets) {
        const bool isCurrent = (entry.name == current);

        WalletCard *card = new WalletCard();
        card->setObjectName("walletCard");
        card->setProperty("current", isCurrent);
        card->setToolTip(isCurrent ? tr("Currently showing this wallet") : tr("Switch to this wallet"));
        QHBoxLayout *row = new QHBoxLayout(card);
        row->setContentsMargins(16, 14, 12, 14);
        row->setSpacing(14);

        // Encryption state doubles as the card's glyph -- there's no
        // dedicated wallet icon in the resources, and this is the one piece
        // of per-wallet state worth seeing at a glance.
        QLabel *icon = new QLabel(card);
        icon->setPixmap(platformStyle->SingleColorIcon(
            entry.encrypted ? ":/icons/lock_closed" : ":/icons/lock_open").pixmap(24, 24));
        icon->setFixedSize(24, 24);
        row->addWidget(icon);

        QVBoxLayout *text = new QVBoxLayout();
        text->setSpacing(2);
        QLabel *name = new QLabel(entry.name, card);
        name->setObjectName("walletCardName");
        QLabel *sub = new QLabel(entry.encrypted ? tr("Encrypted") : tr("Not encrypted"), card);
        sub->setObjectName("walletCardSub");
        text->addWidget(name);
        text->addWidget(sub);
        row->addLayout(text, 1);

        if (isCurrent) {
            QLabel *badge = new QLabel(tr("ACTIVE"), card);
            badge->setObjectName("walletBadge");
            row->addWidget(badge);
        }

        QToolButton *menuButton = new QToolButton(card);
        menuButton->setObjectName("walletMenuButton");
        menuButton->setText(QString::fromUtf8("\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2")); // "..."
        menuButton->setPopupMode(QToolButton::InstantPopup);
        menuButton->setToolTip(tr("Wallet actions"));
        QMenu *menu = new QMenu(menuButton);
        // Targets this specific wallet by name, not "the current one": closing
        // a wallet doesn't require switching to it first. Enabled for the
        // active wallet too -- PirateOceanGUI::closeWallet() deactivates it.
        QAction *closeAction = menu->addAction(platformStyle->TextColorIcon(":/icons/remove"), tr("Close wallet"));
        const QString walletName = entry.name;
        connect(closeAction, &QAction::triggered, this, [this, walletName]() {
            Q_EMIT closeWalletRequested(walletName);
        });
        menuButton->setMenu(menu);
        row->addWidget(menuButton);

        connect(card, &WalletCard::clicked, this, [this, walletName]() {
            Q_EMIT switchRequested(walletName);
        });

        cardsLayout->addWidget(card);
    }
    cardsLayout->addStretch();
}

#include "walletsdialog.moc"
