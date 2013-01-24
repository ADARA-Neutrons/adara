TEMPLATE = app
CONFIG += console
CONFIG -= qt

INCLUDEPATH += ../../common \
    ../../../combus \
    ../../../common \
    ../../../sts \
    /usr/include/libxml2 \
    /usr/include/activemq-cpp-3.4.0 \
    /usr/include/apr-1

SOURCES += \
    ../StreamMonitor.cpp \
    ../RuleEngine.cpp \
    ../Rule.cpp \
    ../main.cpp \
    ../ComBusRouter.cpp \
    ../../../common/ADARAParser.cc \
    ../../../common/ADARAPackets.cc \
    ../../../combus/ComBus.cpp

HEADERS += \
    ../StreamMonitor.h \
    ../RuleEngine.h \
    ../Rule.h \
    ../ComBusRouter.h \
    ../../common/RuleDefs.h \
    ../../common/DASMonDefs.h \
    ../../../common/ADARADefs.h \
    ../../../combus/ComBus.h \
    ../../../combus/DASMonMessages.h

LIBS += -lboost_thread-mt -lboost_program_options -lxml2 -lactivemq-cpp

