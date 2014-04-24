TEMPLATE = app
CONFIG += console
CONFIG -= qt
TARGET = dasmond

INCLUDEPATH += ../../common \
    ../../../combus \
    ../../../common \
    ../engine \
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
    ../StreamAnalyzer.cpp \
    ../engine/RuleEngine.cpp \
    ../engine/muParserTokenReader.cpp \
    ../engine/muParserError.cpp \
    ../engine/muParserCallback.cpp \
    ../engine/muParserBytecode.cpp \
    ../engine/muParserBase.cpp \
    ../engine/muParser.cpp

HEADERS += \
    ../StreamMonitor.h \
    ../ComBusRouter.h \
    ../../common/DASMonDefs.h \
    ../../../common/ADARADefs.h \
    ../../../combus/ComBus.h \
    ../../../combus/DASMonMessages.h \
    ../../../combus/ComBusMessages.h \
    ../StreamAnalyzer.h \
    ../engine/RuleEngine.h \
    ../engine/muParserTokenReader.h \
    ../engine/muParserToken.h \
    ../engine/muParserStack.h \
    ../engine/muParserFixes.h \
    ../engine/muParserError.h \
    ../engine/muParserDef.h \
    ../engine/muParserCallback.h \
    ../engine/muParserBytecode.h \
    ../engine/muParserBase.h \
    ../engine/muParser.h

LIBS += -lboost_thread-mt -lboost_program_options -lxml2 -lactivemq-cpp -lboost_filesystem

!contains( DEFINES, NO_DB )
{
LIBS += -lpq
}

OTHER_FILES += \
    ../signal.cfg \
    ../schema.txt

