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
                ../../common

unix:INCLUDEPATH += /usr/include/activemq-cpp \
                /usr/include/apr-1

SOURCES += main.cpp \
        mainwindow.cpp \
        ../../combus/ComBus.cpp \
    AMQConfigDialog.cpp

HEADERS  += mainwindow.h \
    ../../combus/STSMessages.h \
    ../../combus/ComBusMessages.h \
    ../../combus/ComBusAppender.h \
    ../../combus/ComBus.h \
    ../../combus/DASMonMessages.h \
    AMQConfigDialog.h

unix:LIBS += -lboost_thread-mt -lboost_program_options -lactivemq-cpp

FORMS    += mainwindow.ui \
    AMQConfigDialog.ui

OTHER_FILES += \
    todo.txt
