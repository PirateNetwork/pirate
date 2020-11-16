/********************************************************************************
** Form generated from reading UI file 'editzaddressdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.9.8
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_EDITZADDRESSDIALOG_H
#define UI_EDITZADDRESSDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QVBoxLayout>
#include "qvalidatedlineedit.h"

QT_BEGIN_NAMESPACE

class Ui_EditZAddressDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *labelEdit;
    QLabel *label_2;
    QValidatedLineEdit *addressEdit;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *EditZAddressDialog)
    {
        if (EditZAddressDialog->objectName().isEmpty())
            EditZAddressDialog->setObjectName(QStringLiteral("EditZAddressDialog"));
        EditZAddressDialog->resize(650, 126);
        verticalLayout = new QVBoxLayout(EditZAddressDialog);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QStringLiteral("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        label = new QLabel(EditZAddressDialog);
        label->setObjectName(QStringLiteral("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        labelEdit = new QLineEdit(EditZAddressDialog);
        labelEdit->setObjectName(QStringLiteral("labelEdit"));

        formLayout->setWidget(0, QFormLayout::FieldRole, labelEdit);

        label_2 = new QLabel(EditZAddressDialog);
        label_2->setObjectName(QStringLiteral("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        addressEdit = new QValidatedLineEdit(EditZAddressDialog);
        addressEdit->setObjectName(QStringLiteral("addressEdit"));

        formLayout->setWidget(1, QFormLayout::FieldRole, addressEdit);


        verticalLayout->addLayout(formLayout);

        buttonBox = new QDialogButtonBox(EditZAddressDialog);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);

#ifndef QT_NO_SHORTCUT
        label->setBuddy(labelEdit);
        label_2->setBuddy(addressEdit);
#endif // QT_NO_SHORTCUT

        retranslateUi(EditZAddressDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), EditZAddressDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), EditZAddressDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(EditZAddressDialog);
    } // setupUi

    void retranslateUi(QDialog *EditZAddressDialog)
    {
        EditZAddressDialog->setWindowTitle(QApplication::translate("EditZAddressDialog", "Edit z-address", Q_NULLPTR));
        label->setText(QApplication::translate("EditZAddressDialog", "&Label", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        labelEdit->setToolTip(QApplication::translate("EditZAddressDialog", "The label associated with this z-address list entry", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
        label_2->setText(QApplication::translate("EditZAddressDialog", "&Address", Q_NULLPTR));
#ifndef QT_NO_TOOLTIP
        addressEdit->setToolTip(QApplication::translate("EditZAddressDialog", "The z-address associated with this z-address list entry. This can only be modified for sending addresses.", Q_NULLPTR));
#endif // QT_NO_TOOLTIP
    } // retranslateUi

};

namespace Ui {
    class EditZAddressDialog: public Ui_EditZAddressDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_EDITZADDRESSDIALOG_H
