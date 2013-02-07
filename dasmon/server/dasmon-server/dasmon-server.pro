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
    ../main.cpp \
    ../ComBusRouter.cpp \
    ../../../common/ADARAParser.cc \
    ../../../common/ADARAPackets.cc \
    ../../../combus/ComBus.cpp \
    ../../../common/RuleEngine.cpp \
    ../StreamAnalyzer.cpp

HEADERS += \
    ../StreamMonitor.h \
    ../ComBusRouter.h \
    ../../common/DASMonDefs.h \
    ../../../common/ADARADefs.h \
    ../../../combus/ComBus.h \
    ../../../combus/DASMonMessages.h \
    ../../../combus/ComBusMessages.h \
    ../../../common/RuleEngine.h \
    ../StreamAnalyzer.h

LIBS += -lboost_thread-mt -lboost_program_options -lxml2 -lactivemq-cpp

OTHER_FILES += \
    ../signal.cfg

