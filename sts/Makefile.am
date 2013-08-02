
bin_PROGRAMS += sts/sts

sts_sts_SOURCES = sts/main.cpp sts/NxGen.cpp sts/StreamParser.cpp \
    sts/h5nx.cpp sts/ComBusTransMon.cpp combus/ComBus.cpp $(COMMON_PARSER)

# TODO pick up the libxml2 include from autoconf/pkgconfig
sts_sts_CPPFLAGS = -I/usr/include/libxml2 -I/usr/include/activemq-cpp-3.4.0 \
                -I/usr/include/apr-1 -Ists $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

# TODO clean this up to compile with these warnings enabled
sts_sts_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
sts_sts_LDADD = -lxml2 -lhdf5 -lboost_system -lboost_filesystem \
        -lboost_program_options -lactivemq-cpp -lboost_thread-mt
