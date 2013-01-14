/********************************************************************************
** Form generated from reading UI file 'ConfigureSMSConnectionDlg.ui'
**
** Created: Mon Jan 14 16:24:33 2013
**      by: Qt User Interface Compiler version 4.8.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_CONFIGURESMSCONNECTIONDLG_H
#define UI_CONFIGURESMSCONNECTIONDLG_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QDialog>
#include <QtGui/QDialogButtonBox>
#include <QtGui/QFormLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_ConfigureSMSConnectionDlg
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *hostEdit;
    QLabel *label_2;
    QLineEdit *portEdit;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *ConfigureSMSConnectionDlg)
    {
        if (ConfigureSMSConnectionDlg->objectName().isEmpty())
            ConfigureSMSConnectionDlg->setObjectName(QString::fromUtf8("ConfigureSMSConnectionDlg"));
        ConfigureSMSConnectionDlg->setWindowModality(Qt::ApplicationModal);
        ConfigureSMSConnectionDlg->resize(357, 113);
        ConfigureSMSConnectionDlg->setSizeGripEnabled(true);
        ConfigureSMSConnectionDlg->setModal(true);
        verticalLayout = new QVBoxLayout(ConfigureSMSConnectionDlg);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        label = new QLabel(ConfigureSMSConnectionDlg);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        hostEdit = new QLineEdit(ConfigureSMSConnectionDlg);
        hostEdit->setObjectName(QString::fromUtf8("hostEdit"));

        formLayout->setWidget(0, QFormLayout::FieldRole, hostEdit);

        label_2 = new QLabel(ConfigureSMSConnectionDlg);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        portEdit = new QLineEdit(ConfigureSMSConnectionDlg);
        portEdit->setObjectName(QString::fromUtf8("portEdit"));

        formLayout->setWidget(1, QFormLayout::FieldRole, portEdit);


        verticalLayout->addLayout(formLayout);

        buttonBox = new QDialogButtonBox(ConfigureSMSConnectionDlg);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(ConfigureSMSConnectionDlg);
        QObject::connect(buttonBox, SIGNAL(accepted()), ConfigureSMSConnectionDlg, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), ConfigureSMSConnectionDlg, SLOT(reject()));

        QMetaObject::connectSlotsByName(ConfigureSMSConnectionDlg);
    } // setupUi

    void retranslateUi(QDialog *ConfigureSMSConnectionDlg)
    {
        ConfigureSMSConnectionDlg->setWindowTitle(QApplication::translate("ConfigureSMSConnectionDlg", "DASMON - SMS Connection Settings", 0, QApplication::UnicodeUTF8));
        label->setText(QApplication::translate("ConfigureSMSConnectionDlg", "SMS Hostname/IP:", 0, QApplication::UnicodeUTF8));
        label_2->setText(QApplication::translate("ConfigureSMSConnectionDlg", "SMS Port:", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class ConfigureSMSConnectionDlg: public Ui_ConfigureSMSConnectionDlg {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_CONFIGURESMSCONNECTIONDLG_H
