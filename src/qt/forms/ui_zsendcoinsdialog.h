/********************************************************************************
** Form generated from reading UI file 'zsendcoinsdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.9.8
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_ZSENDCOINSDIALOG_H
#define UI_ZSENDCOINSDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>
#include "komodoamountfield.h"

QT_BEGIN_NAMESPACE

class Ui_ZSendCoinsDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFrame *frameCoinControl;
    QVBoxLayout *verticalLayoutCoinControl2;
    QVBoxLayout *verticalLayoutCoinControl;
    QHBoxLayout *horizontalLayoutCoinControl1;
    QLabel *labelCoinControlFeatures;
    QHBoxLayout *horizontalLayoutCoinControl2;
    QComboBox *payFromAddress;
    QToolButton *refreshPayFrom;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QVBoxLayout *verticalLayout_2;
    QVBoxLayout *entries;
    QSpacerItem *verticalSpacer;
    QFrame *frameFee;
    QVBoxLayout *verticalLayoutFee1;
    QVBoxLayout *verticalLayoutFee2;
    QSpacerItem *verticalSpacer_2;
    QHBoxLayout *horizontalLayoutFee1;
    QVBoxLayout *verticalLayoutFee7;
    QHBoxLayout *horizontalLayoutSmartFee;
    QLabel *labelFeeHeadline;
    KomodoAmountField *customFee;
    QSpacerItem *horizontalSpacer_4;
    QSpacerItem *verticalSpacerFee;
    QFrame *frameResult;
    QVBoxLayout *verticalLayoutResult1;
    QVBoxLayout *verticalLayoutResult2;
    QSpacerItem *verticalSpacer_3;
    QHBoxLayout *horizontalLayoutResult1;
    QVBoxLayout *verticalLayoutResult3;
    QHBoxLayout *horizontalLayoutResult2;
    QLabel *labelResultHeadline;
    QLineEdit *operationId;
    QSpacerItem *verticalSpacerResult;
    QHBoxLayout *horizontalLayout;
    QPushButton *sendButton;
    QPushButton *clearButton;
    QPushButton *addButton;
    QPushButton *donButton;
    QSpacerItem *horizontalSpacer;
    QHBoxLayout *horizontalLayout_2;
    QLabel *label;
    QLabel *labelBalance;

    void setupUi(QDialog *ZSendCoinsDialog)
    {
        if (ZSendCoinsDialog->objectName().isEmpty())
            ZSendCoinsDialog->setObjectName(QStringLiteral("ZSendCoinsDialog"));
        ZSendCoinsDialog->setEnabled(true);
        ZSendCoinsDialog->resize(850, 526);
        verticalLayout = new QVBoxLayout(ZSendCoinsDialog);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        verticalLayout->setContentsMargins(-1, -1, -1, 8);
        frameCoinControl = new QFrame(ZSendCoinsDialog);
        frameCoinControl->setObjectName(QStringLiteral("frameCoinControl"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(frameCoinControl->sizePolicy().hasHeightForWidth());
        frameCoinControl->setSizePolicy(sizePolicy);
        frameCoinControl->setMaximumSize(QSize(16777215, 16777215));
        frameCoinControl->setFrameShape(QFrame::StyledPanel);
        frameCoinControl->setFrameShadow(QFrame::Sunken);
        verticalLayoutCoinControl2 = new QVBoxLayout(frameCoinControl);
        verticalLayoutCoinControl2->setSpacing(0);
        verticalLayoutCoinControl2->setObjectName(QStringLiteral("verticalLayoutCoinControl2"));
        verticalLayoutCoinControl2->setContentsMargins(0, 0, 0, 6);
        verticalLayoutCoinControl = new QVBoxLayout();
        verticalLayoutCoinControl->setSpacing(0);
        verticalLayoutCoinControl->setObjectName(QStringLiteral("verticalLayoutCoinControl"));
        verticalLayoutCoinControl->setContentsMargins(10, 10, -1, -1);
        horizontalLayoutCoinControl1 = new QHBoxLayout();
        horizontalLayoutCoinControl1->setObjectName(QStringLiteral("horizontalLayoutCoinControl1"));
        horizontalLayoutCoinControl1->setContentsMargins(-1, -1, 0, 10);
        labelCoinControlFeatures = new QLabel(frameCoinControl);
        labelCoinControlFeatures->setObjectName(QStringLiteral("labelCoinControlFeatures"));
        QSizePolicy sizePolicy1(QSizePolicy::Preferred, QSizePolicy::Maximum);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(labelCoinControlFeatures->sizePolicy().hasHeightForWidth());
        labelCoinControlFeatures->setSizePolicy(sizePolicy1);
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        labelCoinControlFeatures->setFont(font);
        labelCoinControlFeatures->setStyleSheet(QStringLiteral("font-weight:bold;"));

        horizontalLayoutCoinControl1->addWidget(labelCoinControlFeatures);


        verticalLayoutCoinControl->addLayout(horizontalLayoutCoinControl1);

        horizontalLayoutCoinControl2 = new QHBoxLayout();
        horizontalLayoutCoinControl2->setSpacing(0);
        horizontalLayoutCoinControl2->setObjectName(QStringLiteral("horizontalLayoutCoinControl2"));
        horizontalLayoutCoinControl2->setContentsMargins(-1, -1, 10, 0);
        payFromAddress = new QComboBox(frameCoinControl);
        payFromAddress->setObjectName(QStringLiteral("payFromAddress"));

        horizontalLayoutCoinControl2->addWidget(payFromAddress);

        refreshPayFrom = new QToolButton(frameCoinControl);
        refreshPayFrom->setObjectName(QStringLiteral("refreshPayFrom"));
        QIcon icon;
        icon.addFile(QStringLiteral(":/icons/refresh"), QSize(), QIcon::Normal, QIcon::Off);
        refreshPayFrom->setIcon(icon);
        refreshPayFrom->setIconSize(QSize(22, 22));

        horizontalLayoutCoinControl2->addWidget(refreshPayFrom);


        verticalLayoutCoinControl->addLayout(horizontalLayoutCoinControl2);


        verticalLayoutCoinControl2->addLayout(verticalLayoutCoinControl);


        verticalLayout->addWidget(frameCoinControl);

        scrollArea = new QScrollArea(ZSendCoinsDialog);
        scrollArea->setObjectName(QStringLiteral("scrollArea"));
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName(QStringLiteral("scrollAreaWidgetContents"));
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 834, 307));
        verticalLayout_2 = new QVBoxLayout(scrollAreaWidgetContents);
        verticalLayout_2->setObjectName(QStringLiteral("verticalLayout_2"));
        verticalLayout_2->setContentsMargins(0, 0, 0, 0);
        entries = new QVBoxLayout();
        entries->setSpacing(6);
        entries->setObjectName(QStringLiteral("entries"));

        verticalLayout_2->addLayout(entries);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_2->addItem(verticalSpacer);

        verticalLayout_2->setStretch(1, 1);
        scrollArea->setWidget(scrollAreaWidgetContents);

        verticalLayout->addWidget(scrollArea);

        frameFee = new QFrame(ZSendCoinsDialog);
        frameFee->setObjectName(QStringLiteral("frameFee"));
        sizePolicy.setHeightForWidth(frameFee->sizePolicy().hasHeightForWidth());
        frameFee->setSizePolicy(sizePolicy);
        frameFee->setMaximumSize(QSize(16777215, 16777215));
        frameFee->setFrameShape(QFrame::StyledPanel);
        frameFee->setFrameShadow(QFrame::Sunken);
        verticalLayoutFee1 = new QVBoxLayout(frameFee);
        verticalLayoutFee1->setSpacing(0);
        verticalLayoutFee1->setObjectName(QStringLiteral("verticalLayoutFee1"));
        verticalLayoutFee1->setContentsMargins(0, 0, 0, 0);
        verticalLayoutFee2 = new QVBoxLayout();
        verticalLayoutFee2->setSpacing(0);
        verticalLayoutFee2->setObjectName(QStringLiteral("verticalLayoutFee2"));
        verticalLayoutFee2->setContentsMargins(10, 0, -1, -1);
        verticalSpacer_2 = new QSpacerItem(40, 5, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayoutFee2->addItem(verticalSpacer_2);

        horizontalLayoutFee1 = new QHBoxLayout();
        horizontalLayoutFee1->setObjectName(QStringLiteral("horizontalLayoutFee1"));
        horizontalLayoutFee1->setContentsMargins(-1, -1, -1, 0);
        verticalLayoutFee7 = new QVBoxLayout();
        verticalLayoutFee7->setSpacing(0);
        verticalLayoutFee7->setObjectName(QStringLiteral("verticalLayoutFee7"));
        horizontalLayoutSmartFee = new QHBoxLayout();
        horizontalLayoutSmartFee->setSpacing(10);
        horizontalLayoutSmartFee->setObjectName(QStringLiteral("horizontalLayoutSmartFee"));
        labelFeeHeadline = new QLabel(frameFee);
        labelFeeHeadline->setObjectName(QStringLiteral("labelFeeHeadline"));
        sizePolicy1.setHeightForWidth(labelFeeHeadline->sizePolicy().hasHeightForWidth());
        labelFeeHeadline->setSizePolicy(sizePolicy1);
        labelFeeHeadline->setFont(font);
        labelFeeHeadline->setStyleSheet(QStringLiteral("font-weight:bold;"));

        horizontalLayoutSmartFee->addWidget(labelFeeHeadline);

        customFee = new KomodoAmountField(frameFee);
        customFee->setObjectName(QStringLiteral("customFee"));

        horizontalLayoutSmartFee->addWidget(customFee);


        verticalLayoutFee7->addLayout(horizontalLayoutSmartFee);


        horizontalLayoutFee1->addLayout(verticalLayoutFee7);

        horizontalSpacer_4 = new QSpacerItem(40, 20, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

        horizontalLayoutFee1->addItem(horizontalSpacer_4);


        verticalLayoutFee2->addLayout(horizontalLayoutFee1);

        verticalSpacerFee = new QSpacerItem(40, 5, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayoutFee2->addItem(verticalSpacerFee);


        verticalLayoutFee1->addLayout(verticalLayoutFee2);


        verticalLayout->addWidget(frameFee);

        frameResult = new QFrame(ZSendCoinsDialog);
        frameResult->setObjectName(QStringLiteral("frameResult"));
        sizePolicy.setHeightForWidth(frameResult->sizePolicy().hasHeightForWidth());
        frameResult->setSizePolicy(sizePolicy);
        frameResult->setMaximumSize(QSize(16777215, 16777215));
        frameResult->setFrameShape(QFrame::StyledPanel);
        frameResult->setFrameShadow(QFrame::Sunken);
        verticalLayoutResult1 = new QVBoxLayout(frameResult);
        verticalLayoutResult1->setSpacing(0);
        verticalLayoutResult1->setObjectName(QStringLiteral("verticalLayoutResult1"));
        verticalLayoutResult1->setContentsMargins(0, 0, 0, 0);
        verticalLayoutResult2 = new QVBoxLayout();
        verticalLayoutResult2->setSpacing(0);
        verticalLayoutResult2->setObjectName(QStringLiteral("verticalLayoutResult2"));
        verticalLayoutResult2->setContentsMargins(10, 0, 10, -1);
        verticalSpacer_3 = new QSpacerItem(40, 5, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayoutResult2->addItem(verticalSpacer_3);

        horizontalLayoutResult1 = new QHBoxLayout();
        horizontalLayoutResult1->setObjectName(QStringLiteral("horizontalLayoutResult1"));
        horizontalLayoutResult1->setContentsMargins(-1, -1, -1, 0);
        verticalLayoutResult3 = new QVBoxLayout();
        verticalLayoutResult3->setSpacing(0);
        verticalLayoutResult3->setObjectName(QStringLiteral("verticalLayoutResult3"));
        horizontalLayoutResult2 = new QHBoxLayout();
        horizontalLayoutResult2->setSpacing(10);
        horizontalLayoutResult2->setObjectName(QStringLiteral("horizontalLayoutResult2"));
        labelResultHeadline = new QLabel(frameResult);
        labelResultHeadline->setObjectName(QStringLiteral("labelResultHeadline"));
        sizePolicy1.setHeightForWidth(labelResultHeadline->sizePolicy().hasHeightForWidth());
        labelResultHeadline->setSizePolicy(sizePolicy1);
        labelResultHeadline->setFont(font);
        labelResultHeadline->setStyleSheet(QStringLiteral("font-weight:bold;"));

        horizontalLayoutResult2->addWidget(labelResultHeadline);

        operationId = new QLineEdit(frameResult);
        operationId->setObjectName(QStringLiteral("operationId"));
        operationId->setReadOnly(true);

        horizontalLayoutResult2->addWidget(operationId);


        verticalLayoutResult3->addLayout(horizontalLayoutResult2);


        horizontalLayoutResult1->addLayout(verticalLayoutResult3);


        verticalLayoutResult2->addLayout(horizontalLayoutResult1);

        verticalSpacerResult = new QSpacerItem(40, 5, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayoutResult2->addItem(verticalSpacerResult);


        verticalLayoutResult1->addLayout(verticalLayoutResult2);


        verticalLayout->addWidget(frameResult);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        sendButton = new QPushButton(ZSendCoinsDialog);
        sendButton->setObjectName(QStringLiteral("sendButton"));
        sendButton->setMinimumSize(QSize(150, 0));
        QIcon icon1;
        icon1.addFile(QStringLiteral(":/icons/send"), QSize(), QIcon::Normal, QIcon::Off);
        sendButton->setIcon(icon1);
        sendButton->setAutoDefault(false);

        horizontalLayout->addWidget(sendButton);

        clearButton = new QPushButton(ZSendCoinsDialog);
        clearButton->setObjectName(QStringLiteral("clearButton"));
        QSizePolicy sizePolicy2(QSizePolicy::Minimum, QSizePolicy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(clearButton->sizePolicy().hasHeightForWidth());
        clearButton->setSizePolicy(sizePolicy2);
        QIcon icon2;
        icon2.addFile(QStringLiteral(":/icons/remove"), QSize(), QIcon::Normal, QIcon::Off);
        clearButton->setIcon(icon2);
        clearButton->setAutoDefault(false);

        horizontalLayout->addWidget(clearButton);

        addButton = new QPushButton(ZSendCoinsDialog);
        addButton->setObjectName(QStringLiteral("addButton"));
        QIcon icon3;
        icon3.addFile(QStringLiteral(":/icons/add"), QSize(), QIcon::Normal, QIcon::Off);
        addButton->setIcon(icon3);
        addButton->setAutoDefault(false);

        horizontalLayout->addWidget(addButton);

        donButton = new QPushButton(ZSendCoinsDialog);
        donButton->setObjectName(QStringLiteral("donButton"));
        donButton->setEnabled(false);
        donButton->setIcon(icon3);
        donButton->setAutoDefault(false);

        horizontalLayout->addWidget(donButton);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout->addItem(horizontalSpacer);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(3);
        horizontalLayout_2->setObjectName(QStringLiteral("horizontalLayout_2"));
        label = new QLabel(ZSendCoinsDialog);
        label->setObjectName(QStringLiteral("label"));
        QSizePolicy sizePolicy3(QSizePolicy::Preferred, QSizePolicy::Fixed);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy3);

        horizontalLayout_2->addWidget(label);

        labelBalance = new QLabel(ZSendCoinsDialog);
        labelBalance->setObjectName(QStringLiteral("labelBalance"));
        sizePolicy3.setHeightForWidth(labelBalance->sizePolicy().hasHeightForWidth());
        labelBalance->setSizePolicy(sizePolicy3);
        labelBalance->setCursor(QCursor(Qt::IBeamCursor));
        labelBalance->setText(QStringLiteral("123.456 Coins"));
        labelBalance->setTextInteractionFlags(Qt::LinksAccessibleByMouse|Qt::TextSelectableByKeyboard|Qt::TextSelectableByMouse);

        horizontalLayout_2->addWidget(labelBalance);


        horizontalLayout->addLayout(horizontalLayout_2);


        verticalLayout->addLayout(horizontalLayout);

        verticalLayout->setStretch(1, 1);

        retranslateUi(ZSendCoinsDialog);

        sendButton->setDefault(false);


        QMetaObject::connectSlotsByName(ZSendCoinsDialog);
    } // setupUi

    void retranslateUi(QDialog *ZSendCoinsDialog)
    {
        ZSendCoinsDialog->setWindowTitle(QApplication::translate("ZSendCoinsDialog", "Z-Send Coins", Q_NULLPTR));
        labelCoinControlFeatures->setText(QApplication::translate("ZSendCoinsDialog", "Pay From:", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        refreshPayFrom->setToolTip(QApplication::translate("ZSendCoinsDialog", "Refresh address list", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        refreshPayFrom->setText(QString());
        labelFeeHeadline->setText(QApplication::translate("ZSendCoinsDialog", "Transaction Fee:", Q_NULLPTR));
        labelResultHeadline->setText(QApplication::translate("ZSendCoinsDialog", "Operation ID (opid):", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        sendButton->setToolTip(QApplication::translate("ZSendCoinsDialog", "Confirm the send action", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        sendButton->setText(QApplication::translate("ZSendCoinsDialog", "S&end", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        clearButton->setToolTip(QApplication::translate("ZSendCoinsDialog", "Clear all fields of the form.", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        clearButton->setText(QApplication::translate("ZSendCoinsDialog", "Clear &All", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        addButton->setToolTip(QApplication::translate("ZSendCoinsDialog", "Send to multiple recipients at once", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        addButton->setText(QApplication::translate("ZSendCoinsDialog", "Add &Recipient", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        donButton->setToolTip(QApplication::translate("ZSendCoinsDialog", "Add a donation to PIRATE devs", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        donButton->setText(QApplication::translate("ZSendCoinsDialog", "Add &Donation", Q_NULLPTR));
        label->setText(QApplication::translate("ZSendCoinsDialog", "Transparent:", Q_NULLPTR));
    } // retranslateUi

};

namespace Ui {
    class ZSendCoinsDialog: public Ui_ZSendCoinsDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_ZSENDCOINSDIALOG_H
