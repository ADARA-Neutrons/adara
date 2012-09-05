TEMPLATE = app
CONFIG += console
CONFIG -= qt

SOURCES += main.cpp \
    h5nx.cpp \
    StreamParser.cpp \
    ../SMS/ADARAPackets.cc \
    ../SMS/ADARAParser.cc \
    NxGen.cpp

HEADERS += \
    h5nx.hpp \
    StreamParser.h \
    Utils.h \
    TransCompletePkt.h \
    ../SMS/ADARA.h \
    ../SMS/ADARAPackets.h \
    ../SMS/ADARAParser.h \
    NxGen.h \
    sfsdefs.h

INCLUDEPATH += /usr/include/libxml2
LIBS += -lxml2 -lhdf5 -lglog -lrt -lboost_system -lboost_filesystem

OTHER_FILES += \
    todo.txt \
    README.sts-prod.txt
