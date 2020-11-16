/********************************************************************************
** Form generated from reading UI file 'zaddressbookpage.ui'
**
** Created by: Qt User Interface Compiler version 5.9.8
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ZADDRESSBOOKPAGE_H
#define UI_ZADDRESSBOOKPAGE_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QTableView>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ZAddressBookPage
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *labelExplanation;
    QTableView *tableView;
    QHBoxLayout *horizontalLayout;
    QPushButton *newAddress;
    QPushButton *copyAddress;
    QPushButton *deleteAddress;
    QSpacerItem *horizontalSpacer;
    QPushButton *exportButton;
    QPushButton *closeButton;

    void setupUi(QWidget *ZAddressBookPage)
    {
        if (ZAddressBookPage->objectName().isEmpty())
            ZAddressBookPage->setObjectName(QStringLiteral("ZAddressBookPage"));
        ZAddressBookPage->resize(800, 380);
        verticalLayout = new QVBoxLayout(ZAddressBookPage);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        labelExplanation = new QLabel(ZAddressBookPage);
        labelExplanation->setObjectName(QStringLiteral("labelExplanation"));
        labelExplanation->setTextFormat(Qt::PlainText);
        labelExplanation->setWordWrap(true);

        verticalLayout->addWidget(labelExplanation);

        tableView = new QTableView(ZAddressBookPage);
        tableView->setObjectName(QStringLiteral("tableView"));
        tableView->setContextMenuPolicy(Qt::CustomContextMenu);
        tableView->setTabKeyNavigation(false);
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionMode(QAbstractItemView::SingleSelection);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSortingEnabled(true);
        tableView->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(tableView);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        newAddress = new QPushButton(ZAddressBookPage);
        newAddress->setObjectName(QStringLiteral("newAddress"));
        QIcon icon;
        icon.addFile(QStringLiteral(":/icons/add"), QSize(), QIcon::Normal, QIcon::Off);
        newAddress->setIcon(icon);
        newAddress->setAutoDefault(false);

        horizontalLayout->addWidget(newAddress);

        copyAddress = new QPushButton(ZAddressBookPage);
        copyAddress->setObjectName(QStringLiteral("copyAddress"));
        QIcon icon1;
        icon1.addFile(QStringLiteral(":/icons/editcopy"), QSize(), QIcon::Normal, QIcon::Off);
        copyAddress->setIcon(icon1);
        copyAddress->setAutoDefault(false);

        horizontalLayout->addWidget(copyAddress);

        deleteAddress = new QPushButton(ZAddressBookPage);
        deleteAddress->setObjectName(QStringLiteral("deleteAddress"));
        QIcon icon2;
        icon2.addFile(QStringLiteral(":/icons/remove"), QSize(), QIcon::Normal, QIcon::Off);
        deleteAddress->setIcon(icon2);
        deleteAddress->setAutoDefault(false);

        horizontalLayout->addWidget(deleteAddress);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        exportButton = new QPushButton(ZAddressBookPage);
        exportButton->setObjectName(QStringLiteral("exportButton"));
        QIcon icon3;
        icon3.addFile(QStringLiteral(":/icons/export"), QSize(), QIcon::Normal, QIcon::Off);
        exportButton->setIcon(icon3);
        exportButton->setAutoDefault(false);

        horizontalLayout->addWidget(exportButton);

        closeButton = new QPushButton(ZAddressBookPage);
        closeButton->setObjectName(QStringLiteral("closeButton"));
        QSizePolicy sizePolicy(QSizePolicy::Minimum, QSizePolicy::Minimum);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(closeButton->sizePolicy().hasHeightForWidth());
        closeButton->setSizePolicy(sizePolicy);
        closeButton->setAutoDefault(false);

        horizontalLayout->addWidget(closeButton);


        verticalLayout->addLayout(horizontalLayout);


        retranslateUi(ZAddressBookPage);

        QMetaObject::connectSlotsByName(ZAddressBookPage);
    } // setupUi

    void retranslateUi(QWidget *ZAddressBookPage)
    {
#ifndef QT_NO_TOOLTIP
        tableView->setToolTip(QApplication::translate("ZAddressBookPage", "Right-click to edit address or label", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
#ifndef QT_NO_TOOLTIP
        newAddress->setToolTip(QApplication::translate("ZAddressBookPage", "Create a new address", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        newAddress->setText(QApplication::translate("ZAddressBookPage", "&New", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        copyAddress->setToolTip(QApplication::translate("ZAddressBookPage", "Copy the currently selected address to the system clipboard", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        copyAddress->setText(QApplication::translate("ZAddressBookPage", "&Copy", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        deleteAddress->setToolTip(QApplication::translate("ZAddressBookPage", "Delete the currently selected address from the list", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        deleteAddress->setText(QApplication::translate("ZAddressBookPage", "&Delete", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        exportButton->setToolTip(QApplication::translate("ZAddressBookPage", "Export the data in the current tab to a file", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        exportButton->setText(QApplication::translate("ZAddressBookPage", "&Export", Q_NULLPTR));
        closeButton->setText(QApplication::translate("ZAddressBookPage", "C&lose", Q_NULLPTR));
        Q_UNUSED(ZAddressBookPage);
    } // retranslateUi

};

namespace Ui {
    class ZAddressBookPage: public Ui_ZAddressBookPage {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ZADDRESSBOOKPAGE_H
