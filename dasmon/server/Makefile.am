
noinst_PROGRAMS += dasmon/server/dasmond

dasmon_server_dasmond_SOURCES = dasmon/server/main.cpp dasmon/server/StreamAnalyzer.cpp \
        dasmon/server/StreamMonitor.cpp dasmon/server/ComBusRouter.cpp \
        combus/ComBus.cpp common/RuleEngine.cpp $(COMMON_PARSER)

# TODO pick up the libxml2 include from autoconf/pkgconfig
dasmon_server_dasmond_CPPFLAGS = -I/usr/include/libxml2 -I/usr/include/activemq-cpp-3.4.0 \
        -I/usr/include/apr-1 -Idasmon/common -Icombus $(COMMON_CPPFLAGS) $(AM_CPPFLAGS)

# TODO clean this up to compile with these warnings enabled
dasmon_server_dasmond_CXXFLAGS = $(AM_CXXFLAGS) -Wno-unused-but-set-variable

# TODO pick up most of these from autoconf
dasmon_server_dasmond_LDADD = -lxml2 -lboost_program_options -lboost_thread-mt -lactivemq-cpp -lboost_filesystem
