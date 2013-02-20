/********************************************************************************
** Form generated from reading UI file 'AMQConfigDialog.ui'
**
** Created: Wed Feb 20 15:17:32 2013
**      by: Qt User Interface Compiler version 4.8.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_AMQCONFIGDIALOG_H
#define UI_AMQCONFIGDIALOG_H

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

class Ui_AMQConfigDialog
{
public:
    QVBoxLayout *verticalLayout;
    QFormLayout *formLayout;
    QLabel *label;
    QLineEdit *amqBrokerEdit;
    QLabel *label_2;
    QLineEdit *amqUsernameEdit;
    QLabel *label_3;
    QLineEdit *amqPasswordEdit;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *AMQConfigDialog)
    {
        if (AMQConfigDialog->objectName().isEmpty())
            AMQConfigDialog->setObjectName(QString::fromUtf8("AMQConfigDialog"));
        AMQConfigDialog->resize(340, 171);
        verticalLayout = new QVBoxLayout(AMQConfigDialog);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        label = new QLabel(AMQConfigDialog);
        label->setObjectName(QString::fromUtf8("label"));

        formLayout->setWidget(0, QFormLayout::LabelRole, label);

        amqBrokerEdit = new QLineEdit(AMQConfigDialog);
        amqBrokerEdit->setObjectName(QString::fromUtf8("amqBrokerEdit"));

        formLayout->setWidget(0, QFormLayout::FieldRole, amqBrokerEdit);

        label_2 = new QLabel(AMQConfigDialog);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        formLayout->setWidget(1, QFormLayout::LabelRole, label_2);

        amqUsernameEdit = new QLineEdit(AMQConfigDialog);
        amqUsernameEdit->setObjectName(QString::fromUtf8("amqUsernameEdit"));

        formLayout->setWidget(1, QFormLayout::FieldRole, amqUsernameEdit);

        label_3 = new QLabel(AMQConfigDialog);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        formLayout->setWidget(2, QFormLayout::LabelRole, label_3);

        amqPasswordEdit = new QLineEdit(AMQConfigDialog);
        amqPasswordEdit->setObjectName(QString::fromUtf8("amqPasswordEdit"));
        amqPasswordEdit->setEchoMode(QLineEdit::Password);

        formLayout->setWidget(2, QFormLayout::FieldRole, amqPasswordEdit);


        verticalLayout->addLayout(formLayout);

        buttonBox = new QDialogButtonBox(AMQConfigDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);


        retranslateUi(AMQConfigDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), AMQConfigDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), AMQConfigDialog, SLOT(reject()));

        QMetaObject::connectSlotsByName(AMQConfigDialog);
    } // setupUi

    void retranslateUi(QDialog *AMQConfigDialog)
    {
        AMQConfigDialog->setWindowTitle(QApplication::translate("AMQConfigDialog", "ActiveMQ Configuration", 0, QApplication::UnicodeUTF8));
        label->setText(QApplication::translate("AMQConfigDialog", "Broker URI:", 0, QApplication::UnicodeUTF8));
        label_2->setText(QApplication::translate("AMQConfigDialog", "User name:", 0, QApplication::UnicodeUTF8));
        label_3->setText(QApplication::translate("AMQConfigDialog", "Password:", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class AMQConfigDialog: public Ui_AMQConfigDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_AMQCONFIGDIALOG_H
