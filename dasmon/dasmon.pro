#-------------------------------------------------
#
# Project created by QtCreator 2012-11-29T11:26:24
#
#-------------------------------------------------

QT       += core gui

TARGET = dasmon
TEMPLATE = app

INCLUDEPATH +=  ../common \
                ../combus \
                ../sts \
                /usr/include/libxml2 \
                /usr/include/activemq-cpp-3.4.0 \
                /usr/include/apr-1

SOURCES += main.cpp \
        mainwindow.cpp \
        ../common/ADARAParser.cc \
        ../common/ADARAPackets.cc \
        ../combus/ComBus.cpp \
        ../combus/ComBusAppender.cpp \
    ruleengine.cpp \
    rule.cpp \
    StreamMonitor.cpp \
    ComBusRuleRouter.cpp \
    ConfigureSMSConnectionDlg.cpp

HEADERS  += mainwindow.h \
    ruleengine.h \
    rule.h \
    ruledefs.h \
    StreamMonitor.h \
    ComBusRuleRouter.h \
    ../combus/STSMessages.h \
    ../combus/ComBusMessages.h \
    ../combus/ComBusAppender.h \
    ../combus/ComBus.h \
    ConfigureSMSConnectionDlg.h

LIBS += -llog4cxx -lboost_thread-mt -lxml2 -lactivemq-cpp

FORMS    += mainwindow.ui \
    ConfigureSMSConnectionDlg.ui

OTHER_FILES += \
    todo.txt
