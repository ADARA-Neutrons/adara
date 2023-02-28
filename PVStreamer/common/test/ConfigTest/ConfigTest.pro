#-------------------------------------------------
#
# Project created by QtCreator 2013-10-15T11:18:12
#
#-------------------------------------------------

QT       += core

QT       -= gui

TARGET = ConfigTest
CONFIG   += console
CONFIG   -= app_bundle

TEMPLATE = app

INCLUDEPATH += ../.. /usr/include/libxml2 /home/d3s/epics/include \
    /home/d3s/epics/include/os/Linux ../../../epics ../../../adara \
    ../../../../common

SOURCES += main.cpp \
    ../../DeviceDescriptor.cpp \
    ../../ConfigManager.cpp \
    ../../StreamService.cpp \
    ../../IInputAdapter.cpp \
    ../../IOutputAdapter.cpp \
    ../../../adara/ADARA_OutputAdapter.cpp \
    ../../../epics/EPICS_InputAdapter.cpp \
    ../../../epics/EPICS_DeviceAgent.cpp

HEADERS += \
    ../../DeviceDescriptor.h \
    ../../CoreDefs.h \
    ../../ConfigManager.h \
    ../../SyncDeque.h \
    ../../StreamService.h \
    ../../IInputAdapter.h \
    ../../IOutputAdapter.h \
    ../../../adara/ADARA_OutputAdapter.h \
    ../../../adara/ADARA.h \
    ../../TraceException.h \
    ../../../epics/EPICS_InputAdapter.h \
    ../../../epics/EPICS_DeviceAgent.h

LIBS += -lboost_thread-mt -lxml2 -lboost_filesystem  \
    -L/home/d3s/epics/lib/linux-x86_64 -lCom -lca

OTHER_FILES += \
    ../../../epics/test/ioc.db \
    ../../../epics/test/start.cmd \
    ../../../epics/test/beamline.xml
