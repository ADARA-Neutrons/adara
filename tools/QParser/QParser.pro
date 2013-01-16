#-------------------------------------------------
#
# Project created by QtCreator 2013-01-15T11:27:07
#
#-------------------------------------------------

QT       += core gui

TARGET = QParser
TEMPLATE = app


SOURCES += main.cpp\
        mainwindow.cpp \
    ../../common/ADARAPackets.cc \
    ../../common/ADARAParser.cc

HEADERS  += mainwindow.h \
    ../../common/ADARAParser.h

FORMS    += mainwindow.ui
