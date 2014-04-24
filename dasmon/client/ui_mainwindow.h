/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created: Tue Feb 26 11:13:20 2013
**      by: Qt User Interface Compiler version 4.8.1
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtGui/QApplication>
#include <QtGui/QButtonGroup>
#include <QtGui/QGridLayout>
#include <QtGui/QHBoxLayout>
#include <QtGui/QHeaderView>
#include <QtGui/QLabel>
#include <QtGui/QLineEdit>
#include <QtGui/QMainWindow>
#include <QtGui/QMenu>
#include <QtGui/QMenuBar>
#include <QtGui/QPushButton>
#include <QtGui/QSpacerItem>
#include <QtGui/QSplitter>
#include <QtGui/QTabWidget>
#include <QtGui/QTableWidget>
#include <QtGui/QVBoxLayout>
#include <QtGui/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionSMS_Connection;
    QAction *actionActiveMQ;
    QAction *actionRules;
    QAction *actionAbout;
    QWidget *centralWidget;
    QVBoxLayout *verticalLayout;
    QHBoxLayout *horizontalLayout;
    QVBoxLayout *verticalLayout_2;
    QLabel *runStatusLabel;
    QLabel *scanStatusLabel;
    QLabel *pauseStatusLabel;
    QLabel *signalStatusLabel;
    QGridLayout *gridLayout;
    QLabel *label_23;
    QLabel *label_17;
    QLabel *label_19;
    QLabel *label_21;
    QLabel *label_12;
    QLineEdit *pchargeLabel;
    QLineEdit *durationLabel;
    QLineEdit *countRateLabel;
    QLineEdit *pfreqLabel;
    QLineEdit *bitRateLabel;
    QLabel *label_2;
    QLineEdit *runNumLabel;
    QLabel *label_3;
    QLineEdit *scanNumLabel;
    QLabel *label_29;
    QLineEdit *startTimeEdit;
    QWidget *widget;
    QVBoxLayout *verticalLayout_3;
    QSplitter *splitter;
    QTabWidget *tabWidget;
    QWidget *runInfoTab;
    QGridLayout *gridLayout_2;
    QSpacerItem *verticalSpacer_2;
    QLabel *label_7;
    QLineEdit *runTitleEdit;
    QLabel *label_24;
    QLineEdit *sampleIdEdit;
    QLabel *label_31;
    QLabel *label_32;
    QLabel *facilityInfoLabel;
    QLabel *label;
    QLineEdit *facilityNameEdit;
    QLabel *label_4;
    QLineEdit *beamIdEdit;
    QLabel *label_5;
    QLineEdit *beamNameShortEdit;
    QLabel *label_6;
    QLineEdit *beamNameLongEdit;
    QLabel *label_11;
    QLineEdit *propIdEdit;
    QLabel *label_25;
    QLabel *label_26;
    QLabel *label_27;
    QLabel *label_28;
    QLineEdit *sampleNameEdit;
    QLineEdit *sampleEnvironmentEdit;
    QLineEdit *sampleFormulaEdit;
    QLineEdit *sampleNatureEdit;
    QLabel *label_30;
    QLabel *label_9;
    QLineEdit *totalCountsEdit;
    QLineEdit *totalChargeEdit;
    QWidget *tab;
    QHBoxLayout *horizontalLayout_4;
    QTableWidget *alertTable;
    QWidget *tab_4;
    QHBoxLayout *horizontalLayout_7;
    QTableWidget *procStatusTable;
    QWidget *tab_5;
    QHBoxLayout *horizontalLayout_8;
    QTableWidget *logTable;
    QTableWidget *monitorTable;
    QHBoxLayout *horizontalLayout_2;
    QLabel *combusStatusLabel;
    QLabel *dasmonStatusLabel;
    QLabel *smsStatusLabel;
    QSpacerItem *horizontalSpacer;
    QPushButton *exitButton;
    QMenuBar *menuBar;
    QMenu *menuConfigure;
    QMenu *menuHelp;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName(QString::fromUtf8("MainWindow"));
        MainWindow->resize(924, 769);
        actionSMS_Connection = new QAction(MainWindow);
        actionSMS_Connection->setObjectName(QString::fromUtf8("actionSMS_Connection"));
        actionActiveMQ = new QAction(MainWindow);
        actionActiveMQ->setObjectName(QString::fromUtf8("actionActiveMQ"));
        actionRules = new QAction(MainWindow);
        actionRules->setObjectName(QString::fromUtf8("actionRules"));
        actionAbout = new QAction(MainWindow);
        actionAbout->setObjectName(QString::fromUtf8("actionAbout"));
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName(QString::fromUtf8("centralWidget"));
        verticalLayout = new QVBoxLayout(centralWidget);
        verticalLayout->setSpacing(6);
        verticalLayout->setContentsMargins(11, 11, 11, 11);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setSpacing(6);
        horizontalLayout->setObjectName(QString::fromUtf8("horizontalLayout"));
        verticalLayout_2 = new QVBoxLayout();
        verticalLayout_2->setSpacing(3);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        runStatusLabel = new QLabel(centralWidget);
        runStatusLabel->setObjectName(QString::fromUtf8("runStatusLabel"));
        QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(runStatusLabel->sizePolicy().hasHeightForWidth());
        runStatusLabel->setSizePolicy(sizePolicy);
        runStatusLabel->setMinimumSize(QSize(100, 0));
        QFont font;
        font.setBold(true);
        font.setWeight(75);
        runStatusLabel->setFont(font);
        runStatusLabel->setFrameShape(QFrame::Box);
        runStatusLabel->setAlignment(Qt::AlignCenter);
        runStatusLabel->setMargin(2);

        verticalLayout_2->addWidget(runStatusLabel);

        scanStatusLabel = new QLabel(centralWidget);
        scanStatusLabel->setObjectName(QString::fromUtf8("scanStatusLabel"));
        scanStatusLabel->setMinimumSize(QSize(100, 0));
        scanStatusLabel->setFont(font);
        scanStatusLabel->setFrameShape(QFrame::Box);
        scanStatusLabel->setAlignment(Qt::AlignCenter);
        scanStatusLabel->setMargin(2);

        verticalLayout_2->addWidget(scanStatusLabel);

        pauseStatusLabel = new QLabel(centralWidget);
        pauseStatusLabel->setObjectName(QString::fromUtf8("pauseStatusLabel"));
        sizePolicy.setHeightForWidth(pauseStatusLabel->sizePolicy().hasHeightForWidth());
        pauseStatusLabel->setSizePolicy(sizePolicy);
        pauseStatusLabel->setMinimumSize(QSize(100, 0));
        pauseStatusLabel->setFont(font);
        pauseStatusLabel->setFrameShape(QFrame::Box);
        pauseStatusLabel->setAlignment(Qt::AlignCenter);
        pauseStatusLabel->setMargin(2);

        verticalLayout_2->addWidget(pauseStatusLabel);

        signalStatusLabel = new QLabel(centralWidget);
        signalStatusLabel->setObjectName(QString::fromUtf8("signalStatusLabel"));
        signalStatusLabel->setFont(font);
        signalStatusLabel->setFrameShape(QFrame::Box);
        signalStatusLabel->setAlignment(Qt::AlignCenter);

        verticalLayout_2->addWidget(signalStatusLabel);


        horizontalLayout->addLayout(verticalLayout_2);

        gridLayout = new QGridLayout();
        gridLayout->setSpacing(6);
        gridLayout->setObjectName(QString::fromUtf8("gridLayout"));
        gridLayout->setVerticalSpacing(3);
        label_23 = new QLabel(centralWidget);
        label_23->setObjectName(QString::fromUtf8("label_23"));

        gridLayout->addWidget(label_23, 3, 0, 1, 1);

        label_17 = new QLabel(centralWidget);
        label_17->setObjectName(QString::fromUtf8("label_17"));

        gridLayout->addWidget(label_17, 1, 3, 1, 1);

        label_19 = new QLabel(centralWidget);
        label_19->setObjectName(QString::fromUtf8("label_19"));

        gridLayout->addWidget(label_19, 2, 3, 1, 1);

        label_21 = new QLabel(centralWidget);
        label_21->setObjectName(QString::fromUtf8("label_21"));

        gridLayout->addWidget(label_21, 3, 3, 1, 1);

        label_12 = new QLabel(centralWidget);
        label_12->setObjectName(QString::fromUtf8("label_12"));

        gridLayout->addWidget(label_12, 0, 3, 1, 1);

        pchargeLabel = new QLineEdit(centralWidget);
        pchargeLabel->setObjectName(QString::fromUtf8("pchargeLabel"));
        pchargeLabel->setReadOnly(true);

        gridLayout->addWidget(pchargeLabel, 1, 4, 1, 1);

        durationLabel = new QLineEdit(centralWidget);
        durationLabel->setObjectName(QString::fromUtf8("durationLabel"));
        durationLabel->setReadOnly(true);

        gridLayout->addWidget(durationLabel, 3, 2, 1, 1);

        countRateLabel = new QLineEdit(centralWidget);
        countRateLabel->setObjectName(QString::fromUtf8("countRateLabel"));
        countRateLabel->setReadOnly(true);

        gridLayout->addWidget(countRateLabel, 2, 4, 1, 1);

        pfreqLabel = new QLineEdit(centralWidget);
        pfreqLabel->setObjectName(QString::fromUtf8("pfreqLabel"));
        pfreqLabel->setReadOnly(true);

        gridLayout->addWidget(pfreqLabel, 3, 4, 1, 1);

        bitRateLabel = new QLineEdit(centralWidget);
        bitRateLabel->setObjectName(QString::fromUtf8("bitRateLabel"));
        bitRateLabel->setReadOnly(true);

        gridLayout->addWidget(bitRateLabel, 0, 4, 1, 1);

        label_2 = new QLabel(centralWidget);
        label_2->setObjectName(QString::fromUtf8("label_2"));

        gridLayout->addWidget(label_2, 0, 0, 1, 1);

        runNumLabel = new QLineEdit(centralWidget);
        runNumLabel->setObjectName(QString::fromUtf8("runNumLabel"));
        runNumLabel->setReadOnly(true);

        gridLayout->addWidget(runNumLabel, 0, 2, 1, 1);

        label_3 = new QLabel(centralWidget);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout->addWidget(label_3, 1, 0, 1, 1);

        scanNumLabel = new QLineEdit(centralWidget);
        scanNumLabel->setObjectName(QString::fromUtf8("scanNumLabel"));
        scanNumLabel->setReadOnly(true);

        gridLayout->addWidget(scanNumLabel, 1, 2, 1, 1);

        label_29 = new QLabel(centralWidget);
        label_29->setObjectName(QString::fromUtf8("label_29"));

        gridLayout->addWidget(label_29, 2, 0, 1, 1);

        startTimeEdit = new QLineEdit(centralWidget);
        startTimeEdit->setObjectName(QString::fromUtf8("startTimeEdit"));
        startTimeEdit->setReadOnly(true);

        gridLayout->addWidget(startTimeEdit, 2, 2, 1, 1);


        horizontalLayout->addLayout(gridLayout);

        horizontalLayout->setStretch(1, 1);

        verticalLayout->addLayout(horizontalLayout);

        widget = new QWidget(centralWidget);
        widget->setObjectName(QString::fromUtf8("widget"));
        verticalLayout_3 = new QVBoxLayout(widget);
        verticalLayout_3->setSpacing(6);
        verticalLayout_3->setContentsMargins(11, 11, 11, 11);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        splitter = new QSplitter(widget);
        splitter->setObjectName(QString::fromUtf8("splitter"));
        splitter->setOrientation(Qt::Vertical);
        tabWidget = new QTabWidget(splitter);
        tabWidget->setObjectName(QString::fromUtf8("tabWidget"));
        runInfoTab = new QWidget();
        runInfoTab->setObjectName(QString::fromUtf8("runInfoTab"));
        gridLayout_2 = new QGridLayout(runInfoTab);
        gridLayout_2->setSpacing(6);
        gridLayout_2->setContentsMargins(11, 11, 11, 11);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        gridLayout_2->setVerticalSpacing(3);
        verticalSpacer_2 = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_2->addItem(verticalSpacer_2, 13, 0, 1, 1);

        label_7 = new QLabel(runInfoTab);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        gridLayout_2->addWidget(label_7, 2, 0, 1, 1);

        runTitleEdit = new QLineEdit(runInfoTab);
        runTitleEdit->setObjectName(QString::fromUtf8("runTitleEdit"));
        runTitleEdit->setReadOnly(true);

        gridLayout_2->addWidget(runTitleEdit, 2, 3, 1, 1);

        label_24 = new QLabel(runInfoTab);
        label_24->setObjectName(QString::fromUtf8("label_24"));

        gridLayout_2->addWidget(label_24, 2, 4, 1, 1);

        sampleIdEdit = new QLineEdit(runInfoTab);
        sampleIdEdit->setObjectName(QString::fromUtf8("sampleIdEdit"));
        sampleIdEdit->setReadOnly(true);

        gridLayout_2->addWidget(sampleIdEdit, 2, 5, 1, 1);

        label_31 = new QLabel(runInfoTab);
        label_31->setObjectName(QString::fromUtf8("label_31"));
        QFont font1;
        font1.setBold(true);
        font1.setUnderline(true);
        font1.setWeight(75);
        label_31->setFont(font1);

        gridLayout_2->addWidget(label_31, 1, 4, 1, 2);

        label_32 = new QLabel(runInfoTab);
        label_32->setObjectName(QString::fromUtf8("label_32"));
        label_32->setFont(font1);

        gridLayout_2->addWidget(label_32, 1, 0, 1, 4);

        facilityInfoLabel = new QLabel(runInfoTab);
        facilityInfoLabel->setObjectName(QString::fromUtf8("facilityInfoLabel"));
        facilityInfoLabel->setFont(font1);

        gridLayout_2->addWidget(facilityInfoLabel, 8, 0, 1, 4);

        label = new QLabel(runInfoTab);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout_2->addWidget(label, 9, 0, 1, 1);

        facilityNameEdit = new QLineEdit(runInfoTab);
        facilityNameEdit->setObjectName(QString::fromUtf8("facilityNameEdit"));
        facilityNameEdit->setReadOnly(true);

        gridLayout_2->addWidget(facilityNameEdit, 9, 3, 1, 1);

        label_4 = new QLabel(runInfoTab);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout_2->addWidget(label_4, 10, 0, 1, 1);

        beamIdEdit = new QLineEdit(runInfoTab);
        beamIdEdit->setObjectName(QString::fromUtf8("beamIdEdit"));
        beamIdEdit->setReadOnly(true);

        gridLayout_2->addWidget(beamIdEdit, 10, 3, 1, 1);

        label_5 = new QLabel(runInfoTab);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout_2->addWidget(label_5, 9, 4, 1, 1);

        beamNameShortEdit = new QLineEdit(runInfoTab);
        beamNameShortEdit->setObjectName(QString::fromUtf8("beamNameShortEdit"));
        beamNameShortEdit->setReadOnly(true);

        gridLayout_2->addWidget(beamNameShortEdit, 9, 5, 1, 1);

        label_6 = new QLabel(runInfoTab);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout_2->addWidget(label_6, 10, 4, 1, 1);

        beamNameLongEdit = new QLineEdit(runInfoTab);
        beamNameLongEdit->setObjectName(QString::fromUtf8("beamNameLongEdit"));
        beamNameLongEdit->setReadOnly(true);

        gridLayout_2->addWidget(beamNameLongEdit, 10, 5, 1, 1);

        label_11 = new QLabel(runInfoTab);
        label_11->setObjectName(QString::fromUtf8("label_11"));

        gridLayout_2->addWidget(label_11, 3, 0, 1, 1);

        propIdEdit = new QLineEdit(runInfoTab);
        propIdEdit->setObjectName(QString::fromUtf8("propIdEdit"));
        propIdEdit->setReadOnly(true);

        gridLayout_2->addWidget(propIdEdit, 3, 3, 1, 1);

        label_25 = new QLabel(runInfoTab);
        label_25->setObjectName(QString::fromUtf8("label_25"));

        gridLayout_2->addWidget(label_25, 3, 4, 1, 1);

        label_26 = new QLabel(runInfoTab);
        label_26->setObjectName(QString::fromUtf8("label_26"));

        gridLayout_2->addWidget(label_26, 4, 4, 1, 1);

        label_27 = new QLabel(runInfoTab);
        label_27->setObjectName(QString::fromUtf8("label_27"));

        gridLayout_2->addWidget(label_27, 5, 4, 1, 1);

        label_28 = new QLabel(runInfoTab);
        label_28->setObjectName(QString::fromUtf8("label_28"));

        gridLayout_2->addWidget(label_28, 6, 4, 1, 1);

        sampleNameEdit = new QLineEdit(runInfoTab);
        sampleNameEdit->setObjectName(QString::fromUtf8("sampleNameEdit"));
        sampleNameEdit->setReadOnly(true);

        gridLayout_2->addWidget(sampleNameEdit, 3, 5, 1, 1);

        sampleEnvironmentEdit = new QLineEdit(runInfoTab);
        sampleEnvironmentEdit->setObjectName(QString::fromUtf8("sampleEnvironmentEdit"));
        sampleEnvironmentEdit->setReadOnly(true);

        gridLayout_2->addWidget(sampleEnvironmentEdit, 4, 5, 1, 1);

        sampleFormulaEdit = new QLineEdit(runInfoTab);
        sampleFormulaEdit->setObjectName(QString::fromUtf8("sampleFormulaEdit"));
        sampleFormulaEdit->setReadOnly(true);

        gridLayout_2->addWidget(sampleFormulaEdit, 5, 5, 1, 1);

        sampleNatureEdit = new QLineEdit(runInfoTab);
        sampleNatureEdit->setObjectName(QString::fromUtf8("sampleNatureEdit"));
        sampleNatureEdit->setReadOnly(true);

        gridLayout_2->addWidget(sampleNatureEdit, 6, 5, 1, 1);

        label_30 = new QLabel(runInfoTab);
        label_30->setObjectName(QString::fromUtf8("label_30"));

        gridLayout_2->addWidget(label_30, 4, 0, 1, 1);

        label_9 = new QLabel(runInfoTab);
        label_9->setObjectName(QString::fromUtf8("label_9"));

        gridLayout_2->addWidget(label_9, 5, 0, 1, 1);

        totalCountsEdit = new QLineEdit(runInfoTab);
        totalCountsEdit->setObjectName(QString::fromUtf8("totalCountsEdit"));
        totalCountsEdit->setReadOnly(true);

        gridLayout_2->addWidget(totalCountsEdit, 4, 3, 1, 1);

        totalChargeEdit = new QLineEdit(runInfoTab);
        totalChargeEdit->setObjectName(QString::fromUtf8("totalChargeEdit"));
        totalChargeEdit->setReadOnly(true);

        gridLayout_2->addWidget(totalChargeEdit, 5, 3, 1, 1);

        tabWidget->addTab(runInfoTab, QString());
        tab = new QWidget();
        tab->setObjectName(QString::fromUtf8("tab"));
        horizontalLayout_4 = new QHBoxLayout(tab);
        horizontalLayout_4->setSpacing(6);
        horizontalLayout_4->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        alertTable = new QTableWidget(tab);
        if (alertTable->columnCount() < 4)
            alertTable->setColumnCount(4);
        alertTable->setObjectName(QString::fromUtf8("alertTable"));
        alertTable->setColumnCount(4);
        alertTable->horizontalHeader()->setStretchLastSection(true);
        alertTable->verticalHeader()->setVisible(false);

        horizontalLayout_4->addWidget(alertTable);

        tabWidget->addTab(tab, QString());
        tab_4 = new QWidget();
        tab_4->setObjectName(QString::fromUtf8("tab_4"));
        horizontalLayout_7 = new QHBoxLayout(tab_4);
        horizontalLayout_7->setSpacing(6);
        horizontalLayout_7->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_7->setObjectName(QString::fromUtf8("horizontalLayout_7"));
        procStatusTable = new QTableWidget(tab_4);
        if (procStatusTable->columnCount() < 2)
            procStatusTable->setColumnCount(2);
        procStatusTable->setObjectName(QString::fromUtf8("procStatusTable"));
        procStatusTable->setColumnCount(2);
        procStatusTable->horizontalHeader()->setStretchLastSection(true);
        procStatusTable->verticalHeader()->setVisible(false);

        horizontalLayout_7->addWidget(procStatusTable);

        tabWidget->addTab(tab_4, QString());
        tab_5 = new QWidget();
        tab_5->setObjectName(QString::fromUtf8("tab_5"));
        horizontalLayout_8 = new QHBoxLayout(tab_5);
        horizontalLayout_8->setSpacing(6);
        horizontalLayout_8->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_8->setObjectName(QString::fromUtf8("horizontalLayout_8"));
        logTable = new QTableWidget(tab_5);
        if (logTable->columnCount() < 1)
            logTable->setColumnCount(1);
        logTable->setObjectName(QString::fromUtf8("logTable"));
        logTable->setColumnCount(1);
        logTable->horizontalHeader()->setVisible(false);
        logTable->horizontalHeader()->setStretchLastSection(true);
        logTable->verticalHeader()->setVisible(false);

        horizontalLayout_8->addWidget(logTable);

        tabWidget->addTab(tab_5, QString());
        splitter->addWidget(tabWidget);
        monitorTable = new QTableWidget(splitter);
        if (monitorTable->columnCount() < 2)
            monitorTable->setColumnCount(2);
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem();
        monitorTable->setHorizontalHeaderItem(0, __qtablewidgetitem);
        QTableWidgetItem *__qtablewidgetitem1 = new QTableWidgetItem();
        monitorTable->setHorizontalHeaderItem(1, __qtablewidgetitem1);
        if (monitorTable->rowCount() < 1)
            monitorTable->setRowCount(1);
        monitorTable->setObjectName(QString::fromUtf8("monitorTable"));
        monitorTable->setEnabled(true);
        sizePolicy.setHeightForWidth(monitorTable->sizePolicy().hasHeightForWidth());
        monitorTable->setSizePolicy(sizePolicy);
        monitorTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
        monitorTable->setTabKeyNavigation(false);
        monitorTable->setProperty("showDropIndicator", QVariant(false));
        monitorTable->setAlternatingRowColors(true);
        monitorTable->setWordWrap(false);
        monitorTable->setCornerButtonEnabled(false);
        monitorTable->setRowCount(1);
        monitorTable->setColumnCount(2);
        splitter->addWidget(monitorTable);
        monitorTable->horizontalHeader()->setVisible(false);
        monitorTable->horizontalHeader()->setHighlightSections(false);
        monitorTable->horizontalHeader()->setStretchLastSection(true);
        monitorTable->verticalHeader()->setVisible(false);
        monitorTable->verticalHeader()->setHighlightSections(false);

        verticalLayout_3->addWidget(splitter);


        verticalLayout->addWidget(widget);

        horizontalLayout_2 = new QHBoxLayout();
        horizontalLayout_2->setSpacing(6);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        combusStatusLabel = new QLabel(centralWidget);
        combusStatusLabel->setObjectName(QString::fromUtf8("combusStatusLabel"));
        combusStatusLabel->setFont(font);
        combusStatusLabel->setFrameShape(QFrame::Box);

        horizontalLayout_2->addWidget(combusStatusLabel);

        dasmonStatusLabel = new QLabel(centralWidget);
        dasmonStatusLabel->setObjectName(QString::fromUtf8("dasmonStatusLabel"));
        dasmonStatusLabel->setFont(font);
        dasmonStatusLabel->setFrameShape(QFrame::Box);

        horizontalLayout_2->addWidget(dasmonStatusLabel);

        smsStatusLabel = new QLabel(centralWidget);
        smsStatusLabel->setObjectName(QString::fromUtf8("smsStatusLabel"));
        smsStatusLabel->setFont(font);
        smsStatusLabel->setFrameShape(QFrame::Box);

        horizontalLayout_2->addWidget(smsStatusLabel);

        horizontalSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(horizontalSpacer);

        exitButton = new QPushButton(centralWidget);
        exitButton->setObjectName(QString::fromUtf8("exitButton"));

        horizontalLayout_2->addWidget(exitButton);


        verticalLayout->addLayout(horizontalLayout_2);

        verticalLayout->setStretch(1, 1);
        MainWindow->setCentralWidget(centralWidget);
        menuBar = new QMenuBar(MainWindow);
        menuBar->setObjectName(QString::fromUtf8("menuBar"));
        menuBar->setGeometry(QRect(0, 0, 924, 25));
        menuConfigure = new QMenu(menuBar);
        menuConfigure->setObjectName(QString::fromUtf8("menuConfigure"));
        menuHelp = new QMenu(menuBar);
        menuHelp->setObjectName(QString::fromUtf8("menuHelp"));
        MainWindow->setMenuBar(menuBar);

        menuBar->addAction(menuConfigure->menuAction());
        menuBar->addAction(menuHelp->menuAction());
        menuConfigure->addAction(actionActiveMQ);
        menuConfigure->addAction(actionRules);
        menuHelp->addAction(actionAbout);

        retranslateUi(MainWindow);
        QObject::connect(exitButton, SIGNAL(clicked()), MainWindow, SLOT(close()));
        QObject::connect(actionActiveMQ, SIGNAL(triggered()), MainWindow, SLOT(configActiveMQ()));
        QObject::connect(actionRules, SIGNAL(triggered()), MainWindow, SLOT(configRules()));
        QObject::connect(actionAbout, SIGNAL(triggered()), MainWindow, SLOT(about()));

        tabWidget->setCurrentIndex(0);


        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QApplication::translate("MainWindow", "SNS DAS Monitor", 0, QApplication::UnicodeUTF8));
        actionSMS_Connection->setText(QApplication::translate("MainWindow", "SMS Connection...", 0, QApplication::UnicodeUTF8));
#ifndef QT_NO_TOOLTIP
        actionSMS_Connection->setToolTip(QApplication::translate("MainWindow", "Set SMS hostname and port", 0, QApplication::UnicodeUTF8));
#endif // QT_NO_TOOLTIP
        actionActiveMQ->setText(QApplication::translate("MainWindow", "ActiveMQ...", 0, QApplication::UnicodeUTF8));
        actionRules->setText(QApplication::translate("MainWindow", "Rules...", 0, QApplication::UnicodeUTF8));
        actionAbout->setText(QApplication::translate("MainWindow", "About...", 0, QApplication::UnicodeUTF8));
        runStatusLabel->setText(QApplication::translate("MainWindow", "Recording", 0, QApplication::UnicodeUTF8));
        scanStatusLabel->setText(QApplication::translate("MainWindow", "No Scan", 0, QApplication::UnicodeUTF8));
        pauseStatusLabel->setText(QApplication::translate("MainWindow", "------", 0, QApplication::UnicodeUTF8));
        signalStatusLabel->setText(QApplication::translate("MainWindow", "------", 0, QApplication::UnicodeUTF8));
        label_23->setText(QApplication::translate("MainWindow", "Duration:", 0, QApplication::UnicodeUTF8));
        label_17->setText(QApplication::translate("MainWindow", "Proton Charge:", 0, QApplication::UnicodeUTF8));
        label_19->setText(QApplication::translate("MainWindow", "Count Rate:", 0, QApplication::UnicodeUTF8));
        label_21->setText(QApplication::translate("MainWindow", "Pulse Frequency:", 0, QApplication::UnicodeUTF8));
        label_12->setText(QApplication::translate("MainWindow", "Stream Rate (KB/s):", 0, QApplication::UnicodeUTF8));
        label_2->setText(QApplication::translate("MainWindow", "Run Number:", 0, QApplication::UnicodeUTF8));
        label_3->setText(QApplication::translate("MainWindow", "Scan Number:", 0, QApplication::UnicodeUTF8));
        label_29->setText(QApplication::translate("MainWindow", "Start Time:", 0, QApplication::UnicodeUTF8));
        label_7->setText(QApplication::translate("MainWindow", "Run Title:", 0, QApplication::UnicodeUTF8));
        label_24->setText(QApplication::translate("MainWindow", "Identifier:", 0, QApplication::UnicodeUTF8));
        label_31->setText(QApplication::translate("MainWindow", "Sample Information", 0, QApplication::UnicodeUTF8));
        label_32->setText(QApplication::translate("MainWindow", "Run Information", 0, QApplication::UnicodeUTF8));
        facilityInfoLabel->setText(QApplication::translate("MainWindow", "Beam Information", 0, QApplication::UnicodeUTF8));
        label->setText(QApplication::translate("MainWindow", "Facility:", 0, QApplication::UnicodeUTF8));
        label_4->setText(QApplication::translate("MainWindow", "Beam ID:", 0, QApplication::UnicodeUTF8));
        label_5->setText(QApplication::translate("MainWindow", "Beam Name:", 0, QApplication::UnicodeUTF8));
        label_6->setText(QApplication::translate("MainWindow", "Beam Name (long)", 0, QApplication::UnicodeUTF8));
        label_11->setText(QApplication::translate("MainWindow", "Proposal ID:", 0, QApplication::UnicodeUTF8));
        label_25->setText(QApplication::translate("MainWindow", "Name:", 0, QApplication::UnicodeUTF8));
        label_26->setText(QApplication::translate("MainWindow", "Environment:", 0, QApplication::UnicodeUTF8));
        label_27->setText(QApplication::translate("MainWindow", "Formula:", 0, QApplication::UnicodeUTF8));
        label_28->setText(QApplication::translate("MainWindow", "Nature:", 0, QApplication::UnicodeUTF8));
        label_30->setText(QApplication::translate("MainWindow", "Total Counts:", 0, QApplication::UnicodeUTF8));
        label_9->setText(QApplication::translate("MainWindow", "Total Charge:", 0, QApplication::UnicodeUTF8));
        tabWidget->setTabText(tabWidget->indexOf(runInfoTab), QApplication::translate("MainWindow", "Info", 0, QApplication::UnicodeUTF8));
        tabWidget->setTabText(tabWidget->indexOf(tab), QApplication::translate("MainWindow", "Signals", 0, QApplication::UnicodeUTF8));
        tabWidget->setTabText(tabWidget->indexOf(tab_4), QApplication::translate("MainWindow", "Status", 0, QApplication::UnicodeUTF8));
        tabWidget->setTabText(tabWidget->indexOf(tab_5), QApplication::translate("MainWindow", "Log", 0, QApplication::UnicodeUTF8));
        QTableWidgetItem *___qtablewidgetitem = monitorTable->horizontalHeaderItem(0);
        ___qtablewidgetitem->setText(QApplication::translate("MainWindow", "Monitor ID", 0, QApplication::UnicodeUTF8));
        QTableWidgetItem *___qtablewidgetitem1 = monitorTable->horizontalHeaderItem(1);
        ___qtablewidgetitem1->setText(QApplication::translate("MainWindow", "Count Rate", 0, QApplication::UnicodeUTF8));
        combusStatusLabel->setText(QApplication::translate("MainWindow", "!!!", 0, QApplication::UnicodeUTF8));
        dasmonStatusLabel->setText(QApplication::translate("MainWindow", "!!!", 0, QApplication::UnicodeUTF8));
        smsStatusLabel->setText(QApplication::translate("MainWindow", "!!!", 0, QApplication::UnicodeUTF8));
        exitButton->setText(QApplication::translate("MainWindow", "Exit", 0, QApplication::UnicodeUTF8));
        menuConfigure->setTitle(QApplication::translate("MainWindow", "Configure", 0, QApplication::UnicodeUTF8));
        menuHelp->setTitle(QApplication::translate("MainWindow", "Help", 0, QApplication::UnicodeUTF8));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
