#-------------------------------------------------
#
# Project created by QtCreator 2012-11-29T11:26:24
#
#-------------------------------------------------

QT       += core gui

TARGET = dasmon
TEMPLATE = app

INCLUDEPATH +=  ../common \
                ../../combus \
                ../../sts \
                ../server/engine \
                ../../common

unix:INCLUDEPATH += /usr/include/activemq-cpp \
                /usr/include/apr-1

SOURCES += main.cpp \
        ../../combus/ComBus.cpp \
    AMQConfigDialog.cpp \
    RuleConfigDialog.cpp \
    SubClient.cpp \
    MainWindow.cpp

HEADERS  += \
    ../../combus/STSMessages.h \
    ../../combus/ComBusMessages.h \
    ../../combus/ComBusAppender.h \
    ../../combus/ComBus.h \
    ../../combus/DASMonMessages.h \
    AMQConfigDialog.h \
    RuleConfigDialog.h \
    SubClient.h \
    MainWindow.h \
    ../../combus/ComBusDefs.h \
    style.h

unix:LIBS += -lboost_thread-mt -lboost_program_options -lactivemq-cpp

FORMS    += \
    AMQConfigDialog.ui \
    RuleConfigDialog.ui \
    MainWindow.ui

OTHER_FILES += \
    todo.txt
